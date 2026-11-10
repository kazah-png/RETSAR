#include "../src/lifecycle.hpp"
#include "simulated_executor.hpp"
#include "../src/safe_c_path.hpp"
#include <windows.h>
#include <winioctl.h>
#include <fstream>
#include <iostream>
#include <cstring>

using namespace seats;
using namespace seats::runtime;
using seats::runtime::test::SimulatedSeatExecutor;
namespace {
unsigned passed=0,failed=0,notExercised=0;
void check(bool value,const char* label){std::cout<<(value?"PASS ":"FAIL ")<<label<<'\n';if(value)++passed;else ++failed;}
template<class F>bool throws(F&& action){try{action();return false;}catch(const JournalError&){return true;}}
struct TestClock:Clock{uint64_t tick=1000;uint64_t nowMs()const override{return tick;}};
struct TestNonces:NonceSource{unsigned n=0;std::string next()override{return "SIMULATED-nonce-"+std::to_string(++n);}};
struct FailingJournal:Journal {
    Journal& base;unsigned calls=0,failAt;bool after;
    FailingJournal(Journal& j,unsigned at,bool a=false):base(j),failAt(at),after(a){}
    const std::vector<std::string>& records()const noexcept override{return base.records();}
    void append(const std::string& record)override{++calls;if(calls==failAt&&!after)throw JournalError("TEST injected before durable append");base.append(record);if(calls==failAt&&after)throw JournalError("TEST injected after durable append");}
};
SeatConfig configuration(){return {"second-seat",true,{"SIMULATED-monitor"},{"SIMULATED-input"}};}
std::string readFile(const std::filesystem::path& path){std::ifstream stream(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(stream),{});}
void writeFile(const std::filesystem::path& path,const std::string& data){std::ofstream stream(path,std::ios::binary|std::ios::trunc);stream.write(data.data(),data.size());}
bool childLockProbe(const std::filesystem::path& journal){
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
    std::wstring command=L"\""+std::wstring(exe)+L"\" --lock-child \""+journal.wstring()+L"\"";
    STARTUPINFOW start{};start.cb=sizeof(start);start.dwFlags=STARTF_USESHOWWINDOW;start.wShowWindow=SW_HIDE;PROCESS_INFORMATION child{};
    if(!CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&child))return false;
    const DWORD waited=WaitForSingleObject(child.hProcess,5000);DWORD result=99;
    if(waited==WAIT_OBJECT_0)GetExitCodeProcess(child.hProcess,&result);else{TerminateProcess(child.hProcess,98);WaitForSingleObject(child.hProcess,1000);}
    CloseHandle(child.hThread);CloseHandle(child.hProcess);return waited==WAIT_OBJECT_0&&result==0;
}
std::optional<bool> junctionRace(const std::filesystem::path& root){
    const auto source=root/L"junction-source",destination=root/L"junction-destination";
    std::filesystem::create_directory(source);std::filesystem::create_directory(destination);
    // Only our own fresh C: directories. This handle intentionally predates the
    // guard so the test can try an in-place reparse change, not a rename.
    HANDLE attributes=CreateFileW(source.c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(attributes==INVALID_HANDLE_VALUE){std::cout<<"SKIP junction attribute handle unavailable, code="<<GetLastError()<<'\n';return {};}
    bool created=false,opened=false,cleaned=false;DWORD mutationError=0;
    try{
        LockedCParents guard(source/L"probe.bin");
        const std::wstring substitute=L"\\??\\"+destination.wstring(),print=destination.wstring();
        const size_t chars=substitute.size()+1+print.size()+1;
        std::vector<unsigned char> bytes(16+chars*sizeof(wchar_t),0);
        auto dword=[&](size_t at,DWORD value){std::memcpy(bytes.data()+at,&value,sizeof(value));};
        auto word=[&](size_t at,WORD value){std::memcpy(bytes.data()+at,&value,sizeof(value));};
        dword(0,IO_REPARSE_TAG_MOUNT_POINT);word(4,static_cast<WORD>(bytes.size()-8));
        word(8,0);word(10,static_cast<WORD>(substitute.size()*sizeof(wchar_t)));
        word(12,static_cast<WORD>((substitute.size()+1)*sizeof(wchar_t)));word(14,static_cast<WORD>(print.size()*sizeof(wchar_t)));
        std::memcpy(bytes.data()+16,substitute.c_str(),(substitute.size()+1)*sizeof(wchar_t));
        std::memcpy(bytes.data()+16+(substitute.size()+1)*sizeof(wchar_t),print.c_str(),(print.size()+1)*sizeof(wchar_t));
        DWORD returned=0;created=DeviceIoControl(attributes,FSCTL_SET_REPARSE_POINT,bytes.data(),static_cast<DWORD>(bytes.size()),nullptr,0,&returned,nullptr)!=FALSE;mutationError=GetLastError();
        if(created){
            try{bool existed=false;HANDLE file=guard.openFile(GENERIC_READ|GENERIC_WRITE,CREATE_NEW,existed);opened=true;CloseHandle(file);}catch(const std::exception&){}
            // Remove only the reparse metadata we just installed through our
            // original handle; never traverse it for deletion or cleanup.
            struct DeleteBuffer{DWORD tag;WORD length,reserved;} remove{IO_REPARSE_TAG_MOUNT_POINT,0,0};
            cleaned=DeviceIoControl(attributes,FSCTL_DELETE_REPARSE_POINT,&remove,sizeof(remove),nullptr,0,&returned,nullptr)!=FALSE;
        }
    }catch(const std::exception& error){std::cout<<"SKIP junction guard setup unavailable: "<<error.what()<<'\n';CloseHandle(attributes);return {};}
    CloseHandle(attributes);
    if(!created){std::cout<<"SKIP in-place junction conversion was refused, code="<<mutationError<<"; redirected-child branch not exercised\n";return {};}
    const bool destinationUntouched=GetFileAttributesW((destination/L"probe.bin").c_str())==INVALID_FILE_ATTRIBUTES;
    const bool safeOriginal=!opened||(cleaned&&GetFileAttributesW((source/L"probe.bin").c_str())!=INVALID_FILE_ATTRIBUTES);
    return cleaned&&destinationUntouched&&safeOriginal;
}
}
int wmain(int argc,wchar_t** argv){
    if(argc==3&&std::wstring(argv[1])==L"--lock-child")return throws([&]{DurableJournal journal(argv[2]);})?0:77;
    if(argc!=2){std::cerr<<"Provide the test artifact parent directory on C:.\n";return 2;}
    try{
        SystemNonceSource systemNonce;const auto suffix=systemNonce.next();std::filesystem::path root=std::filesystem::path(argv[1])/(L"lifecycle-tests-"+std::wstring(suffix.begin(),suffix.end()));
        const auto absolute=root.wstring();if(absolute.size()<4||absolute.substr(0,3)!=L"C:\\")throw std::runtime_error("test files must be on C:");
        std::filesystem::create_directory(root);unsigned fileNumber=0;
        auto path=[&]{return root/(L"journal-"+std::to_wstring(++fileNumber)+L".bin");};
        {
            auto file=path();check(throws([&]{DurableJournal missing(file);}),"missing existing journal never silently becomes a new journal");
            {DurableJournal journal(file,JournalOpenMode::CreateNew);journal.append("alpha");journal.append(std::string("binary\0value",12));check(journal.records().size()==2,"durable journal appends ordered opaque records");check(throws([&]{DurableJournal second(file);}),"second writer in same process denied");check(childLockProbe(file),"second writer in separate process denied");}
            check(throws([&]{DurableJournal overwrite(file,JournalOpenMode::CreateNew);}),"explicit new journal cannot overwrite existing state");
            {DurableJournal reopened(file);check(reopened.records().size()==2&&reopened.records()[0]=="alpha"&&reopened.records()[1].size()==12,"closed journal reopens exact durable records");}
            auto corrupt=readFile(file);corrupt.back()^=1;auto corruptFile=path();writeFile(corruptFile,corrupt);check(throws([&]{DurableJournal rejected(corruptFile);}),"checksum corruption blocks opening instead of discarding tail");
            auto truncated=readFile(file);truncated.pop_back();auto truncatedFile=path();writeFile(truncatedFile,truncated);check(throws([&]{DurableJournal rejected(truncatedFile);}),"partial payload tail blocks opening");
            auto headerTail=readFile(file)+std::string(1,'x');auto tailFile=path();writeFile(tailFile,headerTail);check(throws([&]{DurableJournal rejected(tailFile);}),"partial record header blocks opening");
            auto sequence=readFile(file);sequence[20]=2;auto sequenceFile=path();writeFile(sequenceFile,sequence);check(throws([&]{DurableJournal rejected(sequenceFile);}),"nonmonotonic journal sequence blocks opening");
            auto empty=path();writeFile(empty,{});check(throws([&]{DurableJournal rejected(empty);}),"existing empty journal is never reinitialized");
            check(throws([&]{DurableJournal rejected(L"D:\\must-not-be-opened.bin");}),"non-C path rejected before filesystem access");
            check(throws([&]{DurableJournal rejected(root/L"NUL");}),"reserved DOS output path rejected");
            check(throws([&]{DurableJournal rejected(root/L"journal.bin:stream");}),"alternate data stream path rejected");
            check(throws([&]{DurableJournal rejected(root/L"trailing. ");}),"ambiguous trailing path rejected");
            const auto adversarial=junctionRace(root);if(adversarial)check(*adversarial,"held-parent relative open never writes through C-to-C junction substitution");else ++notExercised;
        }
        {
            DurableJournal journal(path(),JournalOpenMode::CreateNew);WindowsBlockedExecutor executor;TestClock clock;TestNonces nonce;SeatLifecycleEngine engine(executor,journal,clock,nonce);
            auto result=engine.Prepare(configuration());check(result.code==ResultCode::Unsupported&&journal.records().empty(),"Windows executor blocked before reservation or effects");
            check(engine.Status().domain==Domain::Windows&&!engine.Status().operationalProvider,"production status never claims operational multiseat");
        }
        {
            DurableJournal journal(path(),JournalOpenMode::CreateNew);SimulatedSeatExecutor executor;TestClock clock;TestNonces nonce;SeatLifecycleEngine engine(executor,journal,clock,nonce);
            auto plan=engine.Prepare(configuration());const std::string current="retsar.lifecycle.v1",legacy="zzen-seats.lifecycle.v1";
            auto record=journal.records().back();check(record.substr(8,current.size())==current,"new snapshots use RETSAR schema");
            record.replace(8,current.size(),legacy);for(unsigned i=0;i<8;++i)record[i]=static_cast<char>((static_cast<uint64_t>(legacy.size())>>(8*i))&255);
            DurableJournal former(path(),JournalOpenMode::CreateNew);former.append(record);TestNonces restartNonces;SeatLifecycleEngine restored(executor,former,clock,restartNonces);
            check(restored.Status().healthy&&restored.Status().runs.size()==1&&restored.Status().runs[0].runId==plan.runId&&former.records().size()==1&&executor.acquireCalls==0,
                "former preview journal schema loads without rewriting or physical effects");
        }
        {
            DurableJournal journal(path(),JournalOpenMode::CreateNew);SimulatedSeatExecutor executor;TestClock clock;TestNonces nonce;SeatLifecycleEngine engine(executor,journal,clock,nonce);
            auto invalid=configuration();invalid.monitorIds.push_back(invalid.monitorIds[0]);check(engine.Prepare(invalid).code==ResultCode::Rejected&&journal.records().empty(),"duplicate configuration rejected before journaling");
            auto oversized=configuration();oversized.inputDeviceIds.clear();for(int i=0;i<255;++i)oversized.inputDeviceIds.push_back("input-"+std::to_string(i));check(engine.Prepare(oversized).code==ResultCode::Rejected&&journal.records().empty(),"configuration exceeding 256 total resources rejected before persistence");
            executor.resourcesFree=false;check(engine.Prepare(configuration()).code==ResultCode::Rejected&&executor.acquireCalls==0,"unknown or foreign resources never treated as free");executor.resourcesFree=true;
            auto plan=engine.Prepare(configuration());check(plan.code==ResultCode::Prepared&&plan.token&&executor.acquireCalls==0,"Prepare only persists logical reservation");
            auto other=configuration();other.id="different-seat";check(engine.Prepare(other).code==ResultCode::Rejected,"second plan cannot reserve already reserved monitor or input");
            auto bad=*plan.token;bad.nonce+="tampered";check(engine.Commit(bad).code==ResultCode::Rejected&&executor.acquireCalls==0,"altered nonce rejected without executor calls");
            bad=*plan.token;++bad.generation;check(engine.Commit(bad).code==ResultCode::Rejected,"stale plan generation rejected");
            bad=*plan.token;bad.configDigest="different";check(engine.Commit(bad).code==ResultCode::Rejected,"altered configuration digest rejected");
            ++executor.revision;check(engine.Commit(*plan.token).code==ResultCode::Rejected&&executor.acquireCalls==0,"external ownership generation change revokes commit");--executor.revision;
            auto started=engine.Commit(*plan.token);check(started.code==ResultCode::Running&&started.domain==Domain::Simulated&&executor.live.size()==3,"simulated start creates exactly session output-binding input-binding");
            bool bound=true;for(const auto& pair:executor.live)if(pair.second.request.kind!=ResourceKind::Session)bound=bound&&!pair.second.request.parentSessionId.empty()&&!pair.second.request.parentSessionIncarnation.empty()&&!pair.second.request.parentSessionOperationId.empty();
            check(bound,"each binding is tied to exact newly owned parent session");
            const int calls=executor.acquireCalls;check(engine.Commit(*plan.token).code==ResultCode::Rejected&&executor.acquireCalls==calls,"consumed token cannot start twice");
            check(engine.Stop(started.runId,started.generation-1).code==ResultCode::Rejected&&executor.releaseCalls==0,"stale stop generation cannot release resources");
            auto stopped=engine.Stop(started.runId,started.generation);check(stopped.code==ResultCode::Stopped&&executor.live.empty(),"explicit stop releases only owned resources");
            check(executor.releaseOrder==std::vector<ResourceKind>{ResourceKind::InputBinding,ResourceKind::OutputBinding,ResourceKind::Session},"compensation executes in dependency-reverse order");
            check(engine.Status().runs.front().reservations.empty(),"reservations released only after durable stop completion");
        }
        {
            DurableJournal journal(path(),JournalOpenMode::CreateNew);SimulatedSeatExecutor executor;TestClock clock;TestNonces nonce;SeatLifecycleEngine engine(executor,journal,clock,nonce);
            auto plan=engine.Prepare(configuration(),10);clock.tick+=11;check(engine.Commit(*plan.token).code==ResultCode::Rejected&&executor.acquireCalls==0&&engine.Status().runs[0].state==RunState::Expired,"expired token releases only unexecuted logical reservations");
        }
        {
            DurableJournal journal(path(),JournalOpenMode::CreateNew);SimulatedSeatExecutor executor;TestClock clock;TestNonces nonce;SeatLifecycleEngine engine(executor,journal,clock,nonce);
            auto plan=engine.Prepare(configuration());executor.epoch="SIMULATED-changed";check(engine.Stop(plan.runId,plan.generation).code==ResultCode::Stopped&&executor.releaseCalls==0,"uncommitted reservation cancellation does not require live provider epoch");
        }
        {
            DurableJournal journal(path(),JournalOpenMode::CreateNew);SimulatedSeatExecutor executor;executor.failAcquireAt=3;TestClock clock;TestNonces nonce;SeatLifecycleEngine engine(executor,journal,clock,nonce);
            auto plan=engine.Prepare(configuration());auto result=engine.Commit(*plan.token);check(result.code==ResultCode::FailedRolledBack&&executor.live.empty(),"known no-effect failure rolls back only prior owned effects");
            check(executor.releaseOrder==std::vector<ResourceKind>{ResourceKind::OutputBinding,ResourceKind::Session},"failed acquisition never invents a receipt to clean up");
        }
        {
            DurableJournal journal(path(),JournalOpenMode::CreateNew);SimulatedSeatExecutor executor;executor.failAcquireAt=3;executor.failReleaseAt=1;TestClock clock;TestNonces nonce;SeatLifecycleEngine engine(executor,journal,clock,nonce);
            auto plan=engine.Prepare(configuration());auto result=engine.Commit(*plan.token);auto state=engine.Status();check(result.code==ResultCode::RecoveryRequired&&executor.live.size()==2&&!state.runs[0].reservations.empty()&&state.runs[0].pendingOperation,"uncertain rollback retains resources and reservations");
            const auto records=journal.records().size();const int acquire=executor.acquireCalls,release=executor.releaseCalls;auto recovery=engine.Recover();check(!recovery.empty()&&records==journal.records().size()&&acquire==executor.acquireCalls&&release==executor.releaseCalls,"Recover reconciles read-only without cleanup or retry");
            executor.failReleaseAt=0;check(engine.Stop(result.runId,result.generation).code==ResultCode::Stopped&&executor.live.empty(),"explicit Stop resumes idempotent owned-release intent");
        }
        {
            DurableJournal journal(path(),JournalOpenMode::CreateNew);SimulatedSeatExecutor executor;executor.uncertainAcquireAt=1;executor.uncertainCreates=false;TestClock clock;TestNonces nonce;SeatLifecycleEngine engine(executor,journal,clock,nonce);
            auto plan=engine.Prepare(configuration());auto result=engine.Commit(*plan.token);auto stopped=engine.Stop(result.runId,result.generation);
            check(stopped.code==ResultCode::RecoveryRequired&&executor.acquireCalls==1&&!engine.Status().runs[0].reservations.empty(),"not-yet-observed acquisition is Unknown, never final no effect");
        }
        {
            DurableJournal journal(path(),JournalOpenMode::CreateNew);SimulatedSeatExecutor executor;executor.forgeReceipt=true;TestClock clock;TestNonces nonce;SeatLifecycleEngine engine(executor,journal,clock,nonce);
            auto plan=engine.Prepare(configuration());auto result=engine.Commit(*plan.token);check(result.code==ResultCode::RecoveryRequired&&executor.releaseCalls==0&&engine.Status().runs[0].ownedReceipts==0,"forged ownership receipt never authorizes cleanup");
        }
        {
            DurableJournal journal(path(),JournalOpenMode::CreateNew);SimulatedSeatExecutor executor;TestClock clock;TestNonces nonce;SeatLifecycleEngine engine(executor,journal,clock,nonce);
            auto plan=engine.Prepare(configuration());auto started=engine.Commit(*plan.token);auto replaced=executor.live.rbegin();replaced->second.incarnation="FOREIGN-reused-id";
            auto result=engine.Stop(started.runId,started.generation);check(result.code==ResultCode::RecoveryRequired&&executor.releaseCalls==0&&executor.live.size()==3,"same resource id with new incarnation is never released");
        }
        {
            DurableJournal journal(path(),JournalOpenMode::CreateNew);SimulatedSeatExecutor executor;TestClock clock;TestNonces nonce;SeatLifecycleEngine engine(executor,journal,clock,nonce);
            auto plan=engine.Prepare(configuration());auto started=engine.Commit(*plan.token);
            executor.beforeRelease=[&](const OwnershipReceipt& receipt){executor.live[receipt.resourceId].request.ownerNonce="FOREIGN-race";};
            auto result=engine.Stop(started.runId,started.generation);check(result.code==ResultCode::RecoveryRequired&&executor.live.size()==3,"provider atomically refuses ownership change between check and release");
        }
        {
            auto file=path();SimulatedSeatExecutor executor;TestClock clock;TestNonces nonce;Result started;
            {DurableJournal journal(file,JournalOpenMode::CreateNew);SeatLifecycleEngine engine(executor,journal,clock,nonce);auto plan=engine.Prepare(configuration());started=engine.Commit(*plan.token);}
            executor.live.clear();{DurableJournal journal(file);SeatLifecycleEngine engine(executor,journal,clock,nonce);auto status=engine.Status();auto recovery=engine.Recover();
                check(status.healthy&&status.runs[0].state==RunState::RecoveryRequired&&status.runs[0].lastRecordedState==RunState::Running,"restored Running state is never presented as a live operational seat");
                check(recovery[0].state==RunState::RecoveryRequired&&recovery[0].detail.find("absent")!=std::string::npos,"read-only recovery reports disappeared owned resources");
                check(engine.Stop(started.runId,status.runs[0].generation).code==ResultCode::Stopped&&executor.releaseCalls==0,"explicit stop can resolve already absent exact resources without physical release");}
        }
        {
            auto file=path();SimulatedSeatExecutor executor;TestClock clock;TestNonces nonce;PlanToken old;
            {DurableJournal journal(file,JournalOpenMode::CreateNew);SeatLifecycleEngine engine(executor,journal,clock,nonce);old=*engine.Prepare(configuration()).token;}
            {DurableJournal journal(file);SeatLifecycleEngine engine(executor,journal,clock,nonce);check(engine.Commit(old).code==ResultCode::Rejected&&executor.acquireCalls==0,"tokens from prior engine epoch cannot be committed after restart");}
            {DurableJournal journal(file);WindowsBlockedExecutor windows;SeatLifecycleEngine engine(windows,journal,clock,nonce);check(!engine.Status().healthy,"simulation journal cannot become Windows execution state");}
        }
        {
            DurableJournal disk(path(),JournalOpenMode::CreateNew);FailingJournal journal(disk,3);SimulatedSeatExecutor executor;TestClock clock;TestNonces nonce;SeatLifecycleEngine engine(executor,journal,clock,nonce);
            auto plan=engine.Prepare(configuration());auto result=engine.Commit(*plan.token);check(result.code==ResultCode::RecoveryRequired&&executor.acquireCalls==0&&!engine.Status().healthy,"failed durable intent prevents the first executor call");
        }
        {
            auto file=path();SimulatedSeatExecutor executor;TestClock clock;TestNonces nonce;Result failedStart;
            {DurableJournal disk(file,JournalOpenMode::CreateNew);FailingJournal journal(disk,4);SeatLifecycleEngine engine(executor,journal,clock,nonce);auto plan=engine.Prepare(configuration());failedStart=engine.Commit(*plan.token);check(failedStart.code==ResultCode::RecoveryRequired&&executor.live.size()==1,"effect followed by receipt-journal failure remains uncertain");}
            {DurableJournal disk(file);SeatLifecycleEngine engine(executor,disk,clock,nonce);const auto before=disk.records().size();auto recovered=engine.Recover();check(!recovered.empty()&&disk.records().size()==before&&executor.acquireCalls==1,"restart queries pending operation without repeating creation");
                auto status=engine.Status();check(engine.Stop(failedStart.runId,status.runs[0].generation).code==ResultCode::Stopped&&executor.live.empty()&&executor.acquireCalls==1,"explicit stop recovers verified lost receipt and removes only owned effect");}
        }
        {
            auto file=path();SimulatedSeatExecutor executor;TestClock clock;TestNonces nonce;
            {DurableJournal disk(file,JournalOpenMode::CreateNew);FailingJournal journal(disk,4,true);SeatLifecycleEngine engine(executor,journal,clock,nonce);auto plan=engine.Prepare(configuration());check(engine.Commit(*plan.token).code==ResultCode::RecoveryRequired,"error even after durable append never reports success");}
            {DurableJournal disk(file);SeatLifecycleEngine engine(executor,disk,clock,nonce);auto status=engine.Status();check(status.healthy&&status.runs[0].ownedReceipts==1&&status.runs[0].state==RunState::RecoveryRequired,"durable completion survives lost acknowledgement without optimistic running");}
        }
        std::cout<<"TOTAL "<<passed<<" passed, "<<failed<<" failed, "<<notExercised<<" adversarial branches not exercised. Provider: SIMULATED_TEST_ONLY. No Windows seat/device mutation was executed.\n";
        std::cout<<"Artifacts: "<<root.string()<<'\n';return failed?1:0;
    }catch(const std::exception& error){std::cerr<<"ERROR "<<error.what()<<'\n';return 2;}
}
