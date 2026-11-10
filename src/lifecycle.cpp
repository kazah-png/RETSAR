#include "lifecycle.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <limits>
#include <mutex>
#include <set>

namespace seats::runtime {
uint64_t SystemClock::nowMs()const{return GetTickCount64();}
std::string SystemNonceSource::next(){
    unsigned char bytes[16];if(BCryptGenRandom(nullptr,bytes,sizeof(bytes),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("secure nonce generation failed");
    constexpr char hex[]="0123456789abcdef";std::string out;for(auto b:bytes){out+=hex[b>>4];out+=hex[b&15];}return out;
}
const char* name(Domain v)noexcept{return v==Domain::Simulated?"SIMULATED_TEST_ONLY":"WINDOWS";}
const char* name(RunState v)noexcept{switch(v){case RunState::Prepared:return "Prepared";case RunState::Starting:return "Starting";case RunState::Running:return "Running";case RunState::Stopping:return "Stopping";case RunState::Stopped:return "Stopped";case RunState::RolledBack:return "RolledBack";case RunState::Expired:return "Expired";default:return "RecoveryRequired";}}
const char* name(ResultCode v)noexcept{switch(v){case ResultCode::Prepared:return "Prepared";case ResultCode::Running:return "Running";case ResultCode::Stopped:return "Stopped";case ResultCode::Unsupported:return "Unsupported";case ResultCode::FailedRolledBack:return "FailedRolledBack";case ResultCode::RecoveryRequired:return "RecoveryRequired";default:return "Rejected";}}
namespace {
struct Writer {
    std::string data;
    void number(uint64_t v){for(unsigned i=0;i<8;++i)data.push_back(static_cast<char>((v>>(8*i))&255));}
    void text(const std::string& v){if(v.size()>65536)throw JournalError("snapshot string too long");number(v.size());data+=v;}
    void list(const std::vector<std::string>& values){if(values.size()>256)throw JournalError("snapshot list too long");number(values.size());for(const auto& value:values)text(value);}
};
struct Reader {
    const std::string& data;size_t at=0;
    uint64_t number(){if(data.size()-at<8)throw JournalError("snapshot truncated");uint64_t v=0;for(unsigned i=0;i<8;++i)v|=uint64_t(static_cast<unsigned char>(data[at++]))<<(8*i);return v;}
    std::string text(){const auto count=number();if(count>65536||count>data.size()-at)throw JournalError("snapshot string invalid");auto value=data.substr(at,static_cast<size_t>(count));at+=static_cast<size_t>(count);return value;}
    std::vector<std::string> list(){const auto count=number();if(count>256)throw JournalError("snapshot list too long");std::vector<std::string> out;for(uint64_t i=0;i<count;++i)out.push_back(text());return out;}
};
void encode(Writer& w,const Acquisition& a){w.number(static_cast<uint64_t>(a.kind));w.text(a.requestedId);w.text(a.runId);w.text(a.ownerNonce);w.text(a.operationId);w.text(a.providerEpoch);w.number(a.inventoryGeneration);w.text(a.parentSessionId);w.text(a.parentSessionIncarnation);w.text(a.parentSessionOperationId);}
Acquisition acquisition(Reader& r){Acquisition a;auto kind=r.number();if(kind>2)throw JournalError("resource kind invalid");a.kind=static_cast<ResourceKind>(kind);a.requestedId=r.text();a.runId=r.text();a.ownerNonce=r.text();a.operationId=r.text();a.providerEpoch=r.text();a.inventoryGeneration=r.number();a.parentSessionId=r.text();a.parentSessionIncarnation=r.text();a.parentSessionOperationId=r.text();return a;}
void encode(Writer& w,const OwnershipReceipt& a){encode(w,a.request);w.text(a.resourceId);w.text(a.incarnation);}
OwnershipReceipt receipt(Reader& r){OwnershipReceipt out;out.request=acquisition(r);out.resourceId=r.text();out.incarnation=r.text();return out;}
SeatConfig canonical(SeatConfig c){c.id=folded(c.id);for(auto& id:c.monitorIds)id=folded(id);for(auto& id:c.inputDeviceIds)id=folded(id);std::sort(c.monitorIds.begin(),c.monitorIds.end());std::sort(c.inputDeviceIds.begin(),c.inputDeviceIds.end());return c;}
void encode(Writer& w,const SeatConfig& c){w.text(c.id);w.number(c.enabled?1:0);w.list(c.monitorIds);w.list(c.inputDeviceIds);}
SeatConfig config(Reader& r){SeatConfig c;c.id=r.text();auto enabled=r.number();if(enabled>1)throw JournalError("enabled flag invalid");c.enabled=enabled!=0;c.monitorIds=r.list();c.inputDeviceIds=r.list();return c;}
std::string digest(const SeatConfig& c){
    Writer writer;encode(writer,c);unsigned char hash[32]{};BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE handle=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 unavailable");
    auto close=[&]{if(handle)BCryptDestroyHash(handle);BCryptCloseAlgorithmProvider(algorithm,0);};
    if(BCryptCreateHash(algorithm,&handle,nullptr,0,nullptr,0,0)<0||BCryptHashData(handle,reinterpret_cast<PUCHAR>(writer.data.data()),static_cast<ULONG>(writer.data.size()),0)<0||BCryptFinishHash(handle,hash,sizeof(hash),0)<0){close();throw std::runtime_error("SHA256 failed");}
    close();constexpr char hex[]="0123456789abcdef";std::string result;for(auto b:hash){result+=hex[b>>4];result+=hex[b&15];}return result;
}
bool terminal(RunState s){return s==RunState::Stopped||s==RunState::RolledBack||s==RunState::Expired;}
bool bounded(const SeatConfig& c){
    if(c.id.size()>65536||c.monitorIds.size()>255||c.inputDeviceIds.size()>255||c.monitorIds.size()+c.inputDeviceIds.size()>255)return false;
    for(const auto& id:c.monitorIds)if(id.size()>65536)return false;for(const auto& id:c.inputDeviceIds)if(id.size()>65536)return false;return true;
}
std::vector<std::string> keys(const SeatConfig& c){std::vector<std::string> out{"seat:"+folded(c.id)};for(const auto& s:c.monitorIds)out.push_back("monitor:"+folded(s));for(const auto& s:c.inputDeviceIds)out.push_back("input:"+folded(s));return out;}
struct Pending {bool release=false;Acquisition request;std::optional<OwnershipReceipt> owned;std::string releaseId;};
struct Run {
    SeatConfig config;PlanToken plan;std::string owner,providerEpoch;
    Domain domain=Domain::Windows;RunState state=RunState::Prepared;uint64_t generation=0;bool consumed=false,restored=false;
    std::vector<OwnershipReceipt> receipts;std::optional<Pending> pending;
};
bool matches(const OwnershipReceipt& owned,const Acquisition& requested){return same(owned.request,requested)&&!owned.resourceId.empty()&&!owned.incarnation.empty();}
}
struct SeatLifecycleEngine::Impl {
    SeatExecutor& executor;Journal& journal;Clock& clock;NonceSource& nonces;mutable std::mutex mutex;
    std::string epoch,error;uint64_t generation=0;bool healthy=true;std::vector<Run> runs;
    Impl(SeatExecutor& e,Journal& j,Clock& c,NonceSource& n):executor(e),journal(j),clock(c),nonces(n){
        try{epoch=nonces.next();if(epoch.empty())throw JournalError("empty engine epoch");uint64_t previous=0;
            for(const auto& record:journal.records()){decode(record);if(generation!=previous+1)throw JournalError("snapshot generation discontinuity");previous=generation;}
            validateState();for(auto& run:runs)run.restored=true;
            for(unsigned retry=0;std::any_of(runs.begin(),runs.end(),[&](const Run& run){return run.plan.engineEpoch==epoch;});++retry){if(retry>=3)throw JournalError("engine epoch collision");epoch=nonces.next();if(epoch.empty())throw JournalError("empty engine epoch");}
        }catch(const std::exception& problem){healthy=false;error=problem.what();}
    }
    bool capable()const{return missingOperationalCapabilities(executor.capabilities()).empty();}
    Result result(ResultCode code,const std::string& detail,const Run* run=nullptr)const{
        Result out;out.code=code;out.domain=executor.domain();out.detail=detail;if(run){out.runId=run->plan.runId;out.generation=run->generation;}return out;
    }
    void validateState()const{
        std::set<std::string> ids,claims,noncesSeen;
        for(const auto& run:runs){
            if(run.plan.runId.empty()||run.owner.empty()||run.plan.nonce.empty()||run.plan.engineEpoch.empty()||run.providerEpoch.empty()||
                !ids.insert(run.plan.runId).second||!noncesSeen.insert(run.owner).second||!noncesSeen.insert(run.plan.nonce).second||
                run.generation>generation||!run.generation||run.plan.generation>run.generation||!bounded(run.config)||digest(run.config)!=run.plan.configDigest||!validateConfiguration({run.config}).empty())throw JournalError("snapshot run invariant failed");
            if(run.domain!=executor.domain())throw JournalError("journal domain does not match executor; simulation cannot become live state");
            if(terminal(run.state)&&(!run.receipts.empty()||run.pending))throw JournalError("terminal run retains resources");
            if(!terminal(run.state))for(const auto& key:keys(run.config))if(!claims.insert(key).second)throw JournalError("snapshot reservations overlap");
            std::set<std::string> operations;
            for(const auto& owned:run.receipts){if(owned.request.runId!=run.plan.runId||owned.request.ownerNonce!=run.owner||owned.request.providerEpoch!=run.providerEpoch||
                owned.resourceId.empty()||owned.incarnation.empty()||!operations.insert(owned.request.operationId).second)throw JournalError("snapshot ownership receipt invalid");
                if(owned.request.kind!=ResourceKind::Session){if(run.receipts.empty()||run.receipts.front().request.kind!=ResourceKind::Session||owned.request.parentSessionId!=run.receipts.front().resourceId||owned.request.parentSessionIncarnation!=run.receipts.front().incarnation||owned.request.parentSessionOperationId!=run.receipts.front().request.operationId)throw JournalError("binding is not tied to the run's owned session");}}
            if(run.pending&&(run.pending->request.runId!=run.plan.runId||run.pending->request.ownerNonce!=run.owner||run.pending->request.providerEpoch!=run.providerEpoch))throw JournalError("snapshot pending ownership invalid");
            if(run.pending&&run.pending->release&&(!run.pending->owned||run.pending->releaseId.empty()||run.receipts.empty()||!same(*run.pending->owned,run.receipts.back())))throw JournalError("snapshot release receipt invalid");
        }
    }
    std::string encodeState(const std::string& event)const{
        Writer w;w.text("retsar.lifecycle.v1");w.text(event);w.number(generation);w.number(runs.size());
        for(const auto& r:runs){encode(w,r.config);w.text(r.plan.runId);w.text(r.plan.nonce);w.text(r.plan.engineEpoch);w.text(r.plan.configDigest);w.number(r.plan.generation);w.number(r.plan.inventoryGeneration);w.number(r.plan.expiresAt);
            w.text(r.owner);w.text(r.providerEpoch);w.number(static_cast<uint64_t>(r.domain));w.number(static_cast<uint64_t>(r.state));w.number(r.generation);w.number(r.consumed?1:0);
            w.number(r.receipts.size());for(const auto& owned:r.receipts)encode(w,owned);w.number(r.pending?1:0);
            if(r.pending){w.number(r.pending->release?1:0);encode(w,r.pending->request);w.number(r.pending->owned?1:0);if(r.pending->owned)encode(w,*r.pending->owned);w.text(r.pending->releaseId);}
        }return w.data;
    }
    void decode(const std::string& bytes){
        Reader r{bytes};const auto schema=r.text();
        // Read the former preview identifier without rewriting an existing journal.
        if(schema!="retsar.lifecycle.v1"&&schema!="zzen-seats.lifecycle.v1")throw JournalError("snapshot version invalid");
        r.text();generation=r.number();const auto count=r.number();if(count>1024)throw JournalError("too many saved runs");std::vector<Run> decoded;
        for(uint64_t i=0;i<count;++i){Run run;run.config=config(r);run.plan.runId=r.text();run.plan.nonce=r.text();run.plan.engineEpoch=r.text();run.plan.configDigest=r.text();run.plan.generation=r.number();run.plan.inventoryGeneration=r.number();run.plan.expiresAt=r.number();
            run.owner=r.text();run.providerEpoch=r.text();auto domain=r.number(),state=r.number();if(domain>1||state>7)throw JournalError("snapshot state invalid");run.domain=static_cast<Domain>(domain);run.state=static_cast<RunState>(state);run.generation=r.number();auto consumed=r.number();if(consumed>1)throw JournalError("snapshot token flag invalid");run.consumed=consumed!=0;
            auto ownedCount=r.number();if(ownedCount>256)throw JournalError("too many receipts");for(uint64_t k=0;k<ownedCount;++k)run.receipts.push_back(receipt(r));
            auto pending=r.number();if(pending>1)throw JournalError("snapshot pending flag invalid");if(pending){Pending p;auto release=r.number();if(release>1)throw JournalError("snapshot action invalid");p.release=release!=0;p.request=acquisition(r);auto hasOwned=r.number();if(hasOwned>1)throw JournalError("snapshot receipt flag invalid");if(hasOwned)p.owned=receipt(r);p.releaseId=r.text();run.pending=p;}decoded.push_back(std::move(run));
        }
        if(r.at!=bytes.size())throw JournalError("snapshot trailing bytes");runs=std::move(decoded);validateState();
    }
    bool persist(Run& run,const std::string& event){
        try{if(generation==std::numeric_limits<uint64_t>::max())throw JournalError("generation exhausted");++generation;run.generation=generation;
            if(run.plan.generation==0)run.plan.generation=generation;validateState();journal.append(encodeState(event));run.restored=false;return true;
        }catch(const std::exception& problem){healthy=false;error=problem.what();run.state=RunState::RecoveryRequired;return false;}
    }
    Result uncertain(Run& run,const std::string& why){run.state=RunState::RecoveryRequired;if(healthy)persist(run,"RecoveryRequired");return result(ResultCode::RecoveryRequired,why,&run);}
    Result releaseAll(Run& run,bool failure){
        while(!run.receipts.empty()){
            const auto owned=run.receipts.back();Ownership ownership=Ownership::Unknown;
            try{ownership=executor.ownership(owned);}catch(...){return uncertain(run,"ownership query failed; reservations retained");}
            if(ownership==Ownership::Foreign||ownership==Ownership::Unknown)return uncertain(run,"foreign/unknown incarnation; no release attempted");
            if(ownership==Ownership::Absent){run.receipts.pop_back();run.pending.reset();if(!persist(run,"OwnedResourceAlreadyAbsent"))return result(ResultCode::RecoveryRequired,error,&run);continue;}
            if(!run.pending){std::string operation;try{operation=nonces.next();}catch(...){return uncertain(run,"release nonce unavailable");}if(operation.empty())return uncertain(run,"release nonce empty");run.pending=Pending{true,owned.request,owned,operation};if(!persist(run,"ReleaseIntent"))return result(ResultCode::RecoveryRequired,error,&run);}
            ReleaseResult released;
            try{released=executor.release(owned,run.pending->releaseId);}catch(...){return uncertain(run,"release threw; effect uncertain");}
            if(released.state!=ReleaseState::Released&&released.state!=ReleaseState::Absent)return uncertain(run,"release refused/uncertain; reservations retained");
            try{if(executor.ownership(owned)!=Ownership::Absent)return uncertain(run,"release could not be confirmed for exact receipt");}catch(...){return uncertain(run,"release confirmation failed");}
            run.receipts.pop_back();run.pending.reset();if(!persist(run,"ReleaseConfirmed"))return result(ResultCode::RecoveryRequired,error,&run);
        }
        run.state=failure?RunState::RolledBack:RunState::Stopped;
        if(!persist(run,failure?"RollbackComplete":"StopComplete"))return result(ResultCode::RecoveryRequired,error,&run);
        return result(failure?ResultCode::FailedRolledBack:ResultCode::Stopped,failure?"start failed; all owned effects removed":"owned effects removed; logical reservations released",&run);
    }
};
SeatLifecycleEngine::SeatLifecycleEngine(SeatExecutor& e,Journal& j,Clock& c,NonceSource& n):impl_(std::make_unique<Impl>(e,j,c,n)){}
SeatLifecycleEngine::~SeatLifecycleEngine()=default;
Result SeatLifecycleEngine::Prepare(const SeatConfig& requested,uint64_t ttl){
    auto& p=*impl_;std::lock_guard lock(p.mutex);
    if(!p.healthy)return p.result(ResultCode::RecoveryRequired,p.error);
    if(!p.capable())return p.result(ResultCode::Unsupported,"P0/capabilities unavailable; no resources reserved or executor mutations called");
    if(!requested.enabled||!bounded(requested)||!validateConfiguration({requested}).empty()||ttl==0||ttl>300000)return p.result(ResultCode::Rejected,"invalid config, bounded resource count or plan lifetime");
    const auto c=canonical(requested);const auto wanted=keys(c);
    for(const auto& r:p.runs)if(!terminal(r.state)){auto held=keys(r.config);for(const auto& key:wanted)if(std::find(held.begin(),held.end(),key)!=held.end())return p.result(ResultCode::Rejected,"resource already reserved by another run");}
    try{
        auto availability=p.executor.inspect(c);if(!availability.ready)return p.result(ResultCode::Rejected,"resources foreign, unknown or unavailable: "+availability.detail);
        Run run;run.config=c;run.domain=p.executor.domain();run.providerEpoch=p.executor.providerEpoch();run.plan.runId=p.nonces.next();run.plan.nonce=p.nonces.next();run.owner=p.nonces.next();run.plan.engineEpoch=p.epoch;run.plan.configDigest=digest(c);run.plan.inventoryGeneration=availability.generation;
        const auto now=p.clock.nowMs();if(now>std::numeric_limits<uint64_t>::max()-ttl)return p.result(ResultCode::Rejected,"clock overflow");run.plan.expiresAt=now+ttl;
        if(p.runs.size()>=1024)return p.result(ResultCode::Rejected,"run history limit reached; no automatic journal deletion");
        p.runs.push_back(std::move(run));auto& added=p.runs.back();if(!p.persist(added,"PlanReserved"))return p.result(ResultCode::RecoveryRequired,p.error,&added);
        auto result=p.result(ResultCode::Prepared,"logical plan reserved; no physical resources created",&added);result.token=added.plan;return result;
    }catch(const std::exception& e){return p.result(ResultCode::Rejected,e.what());}
}
Result SeatLifecycleEngine::Commit(const PlanToken& token){
    auto& p=*impl_;std::lock_guard lock(p.mutex);
    if(!p.healthy)return p.result(ResultCode::RecoveryRequired,p.error);
    auto at=std::find_if(p.runs.begin(),p.runs.end(),[&](const Run& r){return r.plan.runId==token.runId;});if(at==p.runs.end())return p.result(ResultCode::Rejected,"unknown plan");auto& run=*at;
    if(run.state!=RunState::Prepared||run.consumed||token.engineEpoch!=p.epoch||token.engineEpoch!=run.plan.engineEpoch||token.nonce!=run.plan.nonce||token.configDigest!=run.plan.configDigest||token.generation!=run.plan.generation||token.inventoryGeneration!=run.plan.inventoryGeneration||token.expiresAt!=run.plan.expiresAt)return p.result(ResultCode::Rejected,"stale, altered or consumed plan token",&run);
    if(p.clock.nowMs()>token.expiresAt){run.state=RunState::Expired;if(!p.persist(run,"PlanExpired"))return p.result(ResultCode::RecoveryRequired,p.error,&run);return p.result(ResultCode::Rejected,"plan expired; only logical reservations released",&run);}
    if(!p.capable())return p.result(ResultCode::Unsupported,"provider capability gate closed",&run);
    try{auto available=p.executor.inspect(run.config);if(!available.ready||available.generation!=run.plan.inventoryGeneration||p.executor.providerEpoch()!=run.providerEpoch)return p.result(ResultCode::Rejected,"inventory/provider changed since preparation",&run);}catch(...){return p.result(ResultCode::Rejected,"preflight failed",&run);}
    run.consumed=true;run.state=RunState::Starting;if(!p.persist(run,"CommitConsumed"))return p.result(ResultCode::RecoveryRequired,p.error,&run);
    std::vector<std::pair<ResourceKind,std::string>> steps{{ResourceKind::Session,run.config.id}};
    for(const auto& id:run.config.monitorIds)steps.emplace_back(ResourceKind::OutputBinding,id);for(const auto& id:run.config.inputDeviceIds)steps.emplace_back(ResourceKind::InputBinding,id);
    for(const auto& step:steps){
        try{
            if(!p.capable()||p.executor.providerEpoch()!=run.providerEpoch)return p.uncertain(run,"provider changed during transaction");
            Acquisition request{step.first,step.second,run.plan.runId,run.owner,p.nonces.next(),run.providerEpoch,run.plan.inventoryGeneration,{},{},{}};
            if(request.operationId.empty())return p.uncertain(run,"empty acquisition nonce");
            request.operationId=run.plan.runId+":"+request.operationId+":"+std::to_string(p.generation+1);
            if(step.first!=ResourceKind::Session){if(run.receipts.empty()||run.receipts.front().request.kind!=ResourceKind::Session||p.executor.ownership(run.receipts.front())!=Ownership::Owned)return p.uncertain(run,"owned parent session unavailable; no current-session fallback");
                const auto& parent=run.receipts.front();request.parentSessionId=parent.resourceId;request.parentSessionIncarnation=parent.incarnation;request.parentSessionOperationId=parent.request.operationId;}
            run.pending=Pending{false,request,{}, {}};if(!p.persist(run,"AcquireIntent"))return p.result(ResultCode::RecoveryRequired,p.error,&run);
            const auto acquired=p.executor.acquire(request);
            if(acquired.state==AcquireState::NoEffect&&!acquired.receipt){run.pending.reset();if(!p.persist(run,"AcquireNoEffect"))return p.result(ResultCode::RecoveryRequired,p.error,&run);return p.releaseAll(run,true);}
            if(acquired.state!=AcquireState::Created||!acquired.receipt||!matches(*acquired.receipt,request)||p.executor.ownership(*acquired.receipt)!=Ownership::Owned)return p.uncertain(run,"acquisition uncertain or ownership receipt unverified");
            run.receipts.push_back(*acquired.receipt);run.pending.reset();if(!p.persist(run,"AcquireConfirmed"))return p.result(ResultCode::RecoveryRequired,p.error,&run);
        }catch(...){return p.uncertain(run,"provider/acquisition exception; no blind create retry");}
    }
    try{for(const auto& owned:run.receipts)if(p.executor.ownership(owned)!=Ownership::Owned)return p.uncertain(run,"final ownership verification failed");}catch(...){return p.uncertain(run,"final ownership query threw");}
    run.state=RunState::Running;if(!p.persist(run,"RunOwnedResourcesReady"))return p.result(ResultCode::RecoveryRequired,p.error,&run);
    return p.result(ResultCode::Running,"all planned resources owned; domain identifies simulation versus Windows",&run);
}
Result SeatLifecycleEngine::Stop(const std::string& id,uint64_t expected){
    auto& p=*impl_;std::lock_guard lock(p.mutex);if(!p.healthy)return p.result(ResultCode::RecoveryRequired,p.error);
    auto at=std::find_if(p.runs.begin(),p.runs.end(),[&](const Run& r){return r.plan.runId==id;});if(at==p.runs.end())return p.result(ResultCode::Rejected,"unknown run");auto& run=*at;
    if(expected!=run.generation)return p.result(ResultCode::Rejected,"stale stop generation",&run);
    if(terminal(run.state))return p.result(ResultCode::Rejected,"run already terminal",&run);
    if(run.receipts.empty()&&!run.pending){run.consumed=true;run.state=RunState::Stopped;if(!p.persist(run,"LogicalReservationCancelled"))return p.result(ResultCode::RecoveryRequired,p.error,&run);return p.result(ResultCode::Stopped,"only logical reservations cancelled; no executor operation",&run);}
    try{
        if(run.domain!=p.executor.domain()||run.providerEpoch!=p.executor.providerEpoch())return p.uncertain(run,"provider identity changed; cannot release by ID alone");
        if(run.pending&&!run.pending->release){
            const auto query=p.executor.query(run.pending->request);
            if(query.state==QueryState::Owned&&query.receipt&&matches(*query.receipt,run.pending->request)&&p.executor.ownership(*query.receipt)==Ownership::Owned){run.receipts.push_back(*query.receipt);run.pending.reset();if(!p.persist(run,"ReconciledOwnedReceipt"))return p.result(ResultCode::RecoveryRequired,p.error,&run);}
            else if(query.state==QueryState::NoEffectFinal&&!query.receipt){run.pending.reset();if(!p.persist(run,"ReconciledNoEffect"))return p.result(ResultCode::RecoveryRequired,p.error,&run);}
            else return p.uncertain(run,"pending acquisition still uncertain; no retry or foreign cleanup");
        }
        run.consumed=true;run.state=RunState::Stopping;if(!p.persist(run,"StopRequested"))return p.result(ResultCode::RecoveryRequired,p.error,&run);
        return p.releaseAll(run,false);
    }catch(...){return p.uncertain(run,"stop query failed; reservations retained");}
}
std::vector<RecoveryEntry> SeatLifecycleEngine::Recover()const{
    auto& p=*impl_;std::lock_guard lock(p.mutex);std::vector<RecoveryEntry> report;
    if(!p.healthy){report.push_back({{},RunState::RecoveryRequired,p.error});return report;}
    for(const auto& run:p.runs)if(!terminal(run.state)){
        std::string detail="read-only reconciliation; explicit Stop is required for any cleanup";
        try{
            if(run.providerEpoch!=p.executor.providerEpoch())detail="provider epoch differs; ownership cannot be assumed";
            else if(run.pending&&!run.pending->release){auto q=p.executor.query(run.pending->request);detail=q.state==QueryState::Owned?"pending acquisition reported owned; receipt must be verified by explicit Stop":q.state==QueryState::NoEffectFinal?"pending acquisition reported final no effect; no state changed":"pending acquisition unresolved; do not repeat create";}
            else{for(const auto& owned:run.receipts){auto ownership=p.executor.ownership(owned);if(ownership==Ownership::Absent){detail="previously owned resource is absent; run is not established as operational";break;}if(ownership==Ownership::Foreign||ownership==Ownership::Unknown){detail="foreign/unknown receipt present; never release by ID alone";break;}}}
        }catch(...){detail="ownership/query unavailable; no state changed";}
        if(run.restored)detail="restored last-recorded state="+std::string(name(run.state))+"; "+detail;
        report.push_back({run.plan.runId,RunState::RecoveryRequired,detail});
    }return report;
}
EngineStatus SeatLifecycleEngine::Status()const{
    auto& p=*impl_;std::lock_guard lock(p.mutex);EngineStatus out;out.domain=p.executor.domain();out.healthy=p.healthy;out.operationalProvider=p.capable();out.generation=p.generation;out.detail=p.error;
    for(const auto& run:p.runs)out.runs.push_back({run.plan.runId,run.config.id,run.restored&&!terminal(run.state)?RunState::RecoveryRequired:run.state,run.state,run.generation,run.receipts.size(),run.pending.has_value(),terminal(run.state)?std::vector<std::string>{}:keys(run.config)});
    return out;
}
}
