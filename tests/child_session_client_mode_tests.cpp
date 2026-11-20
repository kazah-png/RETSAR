#include "../src/lab/child_session_client_mode.hpp"
#include "../src/lab/child_session_setting_policy.hpp"
#include "../src/lab/child_session_experiment_io.hpp"
#include <iostream>
#include <vector>

namespace {
using namespace seats::lab::child;
using namespace seats::lab::child::client;
unsigned passed=0,failed=0;
void check(bool result,const char* name){if(result)++passed;else{++failed;std::cerr<<"FAIL "<<name<<'\n';}}
Snapshot Baseline(DWORD child=MAXDWORD) {
    Snapshot value;value.apiAvailable=true;
    value.enabled.attempted=value.enabled.succeeded=true;value.enabled.value=false;
    value.caller.attempted=value.caller.succeeded=true;value.caller.value=2;
    value.console.attempted=true;value.console.value=2;
    value.child.attempted=value.child.succeeded=true;value.child.value=child;
    return value;
}
bool Parses(const std::vector<const wchar_t*>& args,Request& request,const wchar_t*& receipt) {
    return ParseModeArgs(static_cast<int>(args.size()),args.data(),request,receipt);
}
bool Rejects(const std::vector<const wchar_t*>& args) {
    Request request;const wchar_t* receipt=nullptr;return !Parses(args,request,receipt);
}
void ArgsTests(){
    Request request;const wchar_t* receipt=nullptr;
    check(Parses({L"client",L"--connect-child",L"--receipt",L"synthetic.jsonl"},request,receipt)&&
        request.mode==Mode::NewChild&&request.target==MAXDWORD&&receipt,
        "new-child arguments remain explicit and targetless");
    check(Parses({L"client",L"--resume-child-session",L"3",L"--receipt",L"synthetic.jsonl"},request,receipt)&&
        request.mode==Mode::ResumeChild&&request.target==3&&receipt,
        "resume names one exact child ID");
    check(Rejects({L"client",L"--resume-child-session",L"3",L"--receipt"}),"missing receipt rejected");
    check(Rejects({L"client",L"--resume-child-session",L"3",L"--receipt",L""}),"empty receipt rejected");
    check(Rejects({L"client",L"--resume-child-session",L"--receipt",L"synthetic.jsonl"}),"missing child ID rejected");
    check(Rejects({L"client",L"--connect-child",L"--resume-child-session",L"3",L"--receipt",L"synthetic.jsonl"}),
        "new and resume modes cannot be combined");
    check(Rejects({L"client",L"--help",L"--resume-child-session",L"3"}),"help cannot hide resume arguments");
    check(Rejects({L"client",L"--connect",L"--receipt",L"synthetic.jsonl"}),"ordinary connection mode rejected");
    check(Rejects({L"client",L"--resume-child-session",L"3",L"--receipt",L"synthetic.jsonl",L"--receipt",L"other.jsonl"}),
        "duplicate or extra arguments rejected");
    for(const auto* id:{L"0",L"4294967295",L"4294967296",L"99999999999999999999999",L"-1",L"+3",L"3x",L""})
        check(Rejects({L"client",L"--resume-child-session",id,L"--receipt",L"synthetic.jsonl"}),"invalid resume ID rejected");
}
void SnapshotTests(){
    Request fresh;Request resume{Mode::ResumeChild,3};
    auto snapshot=Baseline();
    check(AcceptSnapshot(fresh,snapshot),"new mode accepts successful absence observation");
    snapshot.child.succeeded=false;snapshot.child.error=ERROR_NOT_FOUND;
    check(AcceptSnapshot(fresh,snapshot),"new mode preserves original ERROR_NOT_FOUND policy without absence claim");
    check(!AcceptSnapshot(resume,snapshot),"resume never treats ERROR_NOT_FOUND as relationship proof");
    snapshot=Baseline(3);
    check(!AcceptSnapshot(fresh,snapshot),"new mode rejects an existing related child");
    check(AcceptSnapshot(resume,snapshot),"resume accepts exact successful child relationship");
    snapshot.child.value=4;check(!AcceptSnapshot(resume,snapshot),"different related child rejected");
    snapshot.child.value=MAXDWORD;check(!AcceptSnapshot(resume,snapshot),"absence cannot satisfy resume");
    snapshot=Baseline(3);snapshot.child.attempted=false;
    check(!AcceptSnapshot(resume,snapshot),"unqueried child relationship rejected");
    snapshot=Baseline(3);snapshot.child.succeeded=false;snapshot.child.error=ERROR_ACCESS_DENIED;
    check(!AcceptSnapshot(resume,snapshot),"failed relationship query rejected");
    snapshot=Baseline(3);snapshot.enabled.value=true;
    check(!AcceptSnapshot(resume,snapshot),"already enabled baseline rejected");
    check(AcceptSnapshot(resume,snapshot,true),"pre-connect guard requires verified enabled state");
    snapshot.enabled.value=false;
    check(!AcceptSnapshot(resume,snapshot,true),"disabled state cannot satisfy enabled pre-connect guard");
    snapshot=Baseline(3);snapshot.caller.value=3;snapshot.console.value=3;
    check(!AcceptSnapshot(resume,snapshot),"caller cannot be the requested child");
    snapshot=Baseline(3);snapshot.console.value=4;
    check(!AcceptSnapshot(resume,snapshot),"parent must remain attached console");
    snapshot=Baseline(3);snapshot.caller.value=0;snapshot.console.value=0;
    check(!AcceptSnapshot(resume,snapshot),"Session 0 caller rejected");
    snapshot=Baseline(3);snapshot.caller.succeeded=false;
    check(!AcceptSnapshot(resume,snapshot),"unknown caller rejected");
    snapshot=Baseline(3);snapshot.console.attempted=false;
    check(!AcceptSnapshot(resume,snapshot),"unqueried console rejected");
    snapshot=Baseline(3);snapshot.enabled.succeeded=false;
    check(!AcceptSnapshot(resume,snapshot),"unknown original setting rejected");
    snapshot=Baseline(3);snapshot.apiAvailable=false;
    check(!AcceptSnapshot(resume,snapshot),"missing API rejected");
}
void InventoryTests(){
    Request fresh;Request resume{Mode::ResumeChild,3};
    const std::vector<Row> original={{0,WTSListen,false},{2,WTSActive,true}};
    const std::vector<Row> resumable={{0,WTSListen,false},{2,WTSActive,true},{3,WTSDisconnected,true}};
    auto allowed=[&](const Request& mode,const std::vector<Row>& rows,bool complete=true){
        return AcceptInventory(mode,2,complete,rows.data(),rows.size());
    };
    check(allowed(fresh,original),"new mode accepts only active parent plus empty service rows");
    check(!allowed(fresh,resumable),"new mode still rejects a disconnected logged-on child");
    check(allowed(resume,resumable),"resume permits only its exact disconnected logged-on child");
    check(!allowed(resume,resumable,false),"partial enumeration cannot authorize resume");
    check(!AcceptInventory<Row>(resume,2,true,nullptr,3),"null inventory rejected");
    check(!allowed(resume,{}),"empty inventory rejected");
    check(!allowed(resume,original),"missing target rejected");
    for(const auto state:{WTSActive,WTSConnected,WTSConnectQuery,WTSShadow,WTSIdle,WTSListen,WTSReset,WTSDown,WTSInit}) {
        auto rows=resumable;rows.back().state=state;
        check(!allowed(resume,rows),"target must be disconnected, never active or another state");
    }
    auto rows=resumable;rows.back().hasUser=false;
    check(!allowed(resume,rows),"empty target session rejected");
    rows=resumable;rows.back().id=4;
    check(!allowed(resume,rows),"unrelated disconnected user rejected");
    rows=resumable;rows.push_back({4,WTSDisconnected,true});
    check(!allowed(resume,rows),"another disconnected user rejected");
    rows=resumable;rows.push_back({4,WTSActive,true});
    check(!allowed(resume,rows),"another active user rejected");
    rows=resumable;rows.push_back({3,WTSDisconnected,true});
    check(!allowed(resume,rows),"duplicate target rejected");
    rows=resumable;rows.push_back({2,WTSActive,true});
    check(!allowed(resume,rows),"duplicate parent rejected");
    rows=resumable;rows[1].state=WTSDisconnected;
    check(!allowed(resume,rows),"parent must remain active");
    rows=resumable;rows[1].hasUser=false;
    check(!allowed(resume,rows),"parent must remain logged on");
    check(AcceptObservedId(resume,2,3,resumable.data(),resumable.size()),"resume accepts only the same target becoming active");
    check(!AcceptObservedId(resume,2,4,resumable.data(),resumable.size()),"a newly created different child is not resume success");
    check(!AcceptObservedId(resume,2,2,resumable.data(),resumable.size()),"parent cannot be observed child");
    check(AcceptObservedId(fresh,2,3,original.data(),original.size()),"new mode accepts an ID absent from baseline");
    check(!AcceptObservedId(fresh,2,3,resumable.data(),resumable.size()),"existing ID cannot be new-child success");
    check(!AcceptObservedId(fresh,2,0,original.data(),original.size()),"Session 0 cannot be new-child success");
    check(!AcceptObservedId(fresh,2,MAXDWORD,original.data(),original.size()),"absence sentinel cannot be child success");
}
struct InventoryFixture {
    std::vector<WTS_SESSION_INFOW> rows;
    std::vector<DWORD> queried;
    unsigned released=0;
    DWORD failedId=MAXDWORD;
    bool failWithBuffer=false;
    wchar_t signedIn[5]=L"user",empty[1]=L"";
} fixture;
BOOL WINAPI EnumerateFixture(HANDLE,DWORD,DWORD,PWTS_SESSION_INFOW* rows,DWORD* count) {
    *rows=fixture.rows.data();*count=static_cast<DWORD>(fixture.rows.size());return TRUE;
}
BOOL WINAPI QueryFixture(HANDLE,DWORD id,WTS_INFO_CLASS kind,LPWSTR* name,DWORD* bytes) {
    fixture.queried.push_back(id);
    if(kind!=WTSUserName)return FALSE;
    *name=(id==2||id==3)?fixture.signedIn:fixture.empty;
    *bytes=(id==2||id==3)?sizeof(fixture.signedIn):sizeof(fixture.empty);
    if(id==fixture.failedId){if(!fixture.failWithBuffer)*name=nullptr;return FALSE;}
    return TRUE;
}
void WINAPI ReleaseFixture(PVOID){++fixture.released;}
DWORD WINAPI ErrorFixture(){return ERROR_ACCESS_DENIED;}
void InventoryObservationTests(){
    const experiment::InventoryApi api{&EnumerateFixture,&QueryFixture,&ReleaseFixture,&ErrorFixture};
    const auto reset=[](){fixture={};fixture.rows={{0,nullptr,WTSDisconnected},{2,nullptr,WTSActive},{65536,nullptr,WTSListen}};};
    reset();fixture.failedId=65536;
    auto value=experiment::ReadInventoryWithApi(2,MAXDWORD,api);
    check(value.valid&&value.rows.size()==3&&value.rows.back().state==WTSListen&&!value.rows.back().hasUser,
        "listener is retained without inventing a logged-on user");
    check(fixture.queried==std::vector<DWORD>{0,2}&&fixture.released==3,
        "listener never calls WTSUserName; applicable buffers and inventory are released");
    reset();fixture.rows.push_back({3,nullptr,WTSDisconnected});
    value=experiment::ReadInventoryWithApi(2,3,api);
    check(value.valid&&fixture.queried==std::vector<DWORD>{0,2,3},"listener does not block verified disconnected-child baseline");
    reset();fixture.rows[1].State=WTSListen;
    value=experiment::ReadInventoryWithApi(2,MAXDWORD,api);
    check(!value.valid&&value.error==ERROR_INVALID_STATE&&fixture.queried==std::vector<DWORD>{0},"listener with parent ID rejects before user query");
    reset();fixture.rows.push_back({3,nullptr,WTSListen});
    value=experiment::ReadInventoryWithApi(2,3,api);
    check(!value.valid&&value.error==ERROR_INVALID_STATE&&fixture.queried==std::vector<DWORD>{0,2},"listener with target ID rejects before user query");
    reset();fixture.rows.push_back({65536,nullptr,WTSListen});
    value=experiment::ReadInventoryWithApi(2,MAXDWORD,api);
    check(!value.valid&&value.error==ERROR_INVALID_DATA,"duplicate listener IDs cannot yield a complete inventory");
    reset();fixture.failedId=2;
    value=experiment::ReadInventoryWithApi(2,MAXDWORD,api);
    check(!value.valid&&value.error==ERROR_ACCESS_DENIED,"failed applicable query remains blocking and preserves error");
    reset();fixture.failedId=2;fixture.failWithBuffer=true;
    value=experiment::ReadInventoryWithApi(2,MAXDWORD,api);
    check(!value.valid&&fixture.released==3,"unexpected buffer from a failed user query is still released");
    reset();fixture.rows.back().State=static_cast<WTS_CONNECTSTATE_CLASS>(65536);fixture.failedId=65536;
    value=experiment::ReadInventoryWithApi(2,MAXDWORD,api);
    check(!value.valid&&value.error==ERROR_ACCESS_DENIED&&fixture.queried==std::vector<DWORD>{0,2,65536},
        "unknown state is queried and failure is never treated as a listener");
    reset();value=experiment::ReadInventoryWithApi(2,MAXDWORD,{});
    check(!value.valid&&value.error==ERROR_PROC_NOT_FOUND&&fixture.queried.empty()&&fixture.released==0,
        "incomplete API table cannot observe or admit inventory");
}

struct PreserveFixture {unsigned reads=0,sets=0;bool enabled=true,fail=false,throws=false;DWORD error=0;} preserveFixture;
BOOL WINAPI PreserveSet(BOOL){++preserveFixture.sets;return TRUE;}
BOOL WINAPI PreserveRead(BOOL* out){
    ++preserveFixture.reads;
    if(preserveFixture.throws)throw 1;
    if(preserveFixture.fail){preserveFixture.error=ERROR_ACCESS_DENIED;return FALSE;}
    *out=preserveFixture.enabled?TRUE:FALSE;return TRUE;
}
DWORD WINAPI PreserveError(){return preserveFixture.error;}
void WINAPI PreserveSetError(DWORD value){preserveFixture.error=value;}
void EnabledResumeTests(){
    Request enabled{Mode::ResumeChild,3,true},ordinary{Mode::ResumeChild,3},fresh,invalid{Mode::NewChild,MAXDWORD,true};
    Request parsed;const wchar_t* receipt=nullptr;
    check(Parses({L"client",L"--resume-enabled-child-session",L"3",L"--receipt",L"synthetic.jsonl"},parsed,receipt)&&
        parsed.mode==Mode::ResumeChild&&parsed.target==3&&parsed.preserveEnabled&&receipt,
        "enabled resume is an explicit exact-target command");
    check(Parses({L"client",L"--resume-child-session",L"3",L"--receipt",L"synthetic.jsonl"},parsed,receipt)&&!parsed.preserveEnabled,
        "ordinary resume parsing never enables preservation");
    for(const auto* id:{L"0",L"-1",L"4294967295",L"3x",L""})
        check(Rejects({L"client",L"--resume-enabled-child-session",id,L"--receipt",L"synthetic.jsonl"}),"enabled resume rejects invalid target");
    check(Rejects({L"client",L"--resume-enabled-child-session",L"3",L"--receipt",L""}),"enabled resume requires nonempty receipt");
    check(Rejects({L"client",L"--connect-child",L"--resume-enabled-child-session",L"3",L"--receipt",L"synthetic.jsonl"}),"enabled resume cannot combine with creation");
    check(std::string(ModeName(enabled))=="resume-enabled-existing-child"&&std::string(ModeName(ordinary))=="resume-existing-child",
        "receipt mode distinguishes enabled preservation from ordinary resume");
    auto snapshot=Baseline(3);snapshot.enabled.value=true;
    check(AcceptSnapshot(enabled,snapshot)&&AcceptSnapshot(enabled,snapshot,true),"enabled resume verifies true baseline and preconnect state");
    check(!AcceptSnapshot(enabled,snapshot,false),"explicit disabled expectation cannot override enabled-preservation policy");
    check(!AcceptSnapshot(ordinary,snapshot)&&!AcceptSnapshot(fresh,snapshot),"default modes still reject an enabled baseline");
    snapshot.enabled.value=false;check(!AcceptSnapshot(enabled,snapshot),"changed setting cannot admit enabled resume");
    snapshot=Baseline(3);snapshot.enabled.value=true;snapshot.child.value=4;check(!AcceptSnapshot(enabled,snapshot),"changed child relationship rejected");
    snapshot.child.succeeded=false;snapshot.child.error=ERROR_NOT_FOUND;check(!AcceptSnapshot(enabled,snapshot),"unknown child relationship never permits enabled resume");
    snapshot=Baseline(3);snapshot.enabled.value=true;snapshot.console.value=4;check(!AcceptSnapshot(enabled,snapshot),"enabled resume requires attached parent console");
    snapshot=Baseline(3);snapshot.enabled.value=true;snapshot.enabled.succeeded=false;check(!AcceptSnapshot(enabled,snapshot),"failed setting query rejects enabled resume");
    snapshot=Baseline(3);snapshot.enabled.value=true;snapshot.apiAvailable=false;check(!AcceptSnapshot(enabled,snapshot),"missing APIs reject enabled resume");
    snapshot=Baseline();snapshot.enabled.value=true;check(!AcceptSnapshot(invalid,snapshot,true),"creation cannot use preservation flag");
    const std::vector<Row> rows={{0,WTSListen,false},{2,WTSActive,true},{3,WTSDisconnected,true}};
    check(AcceptInventory(enabled,2,true,rows.data(),rows.size()),"enabled resume accepts exact disconnected child with no other users");
    check(!AcceptInventory(enabled,2,false,rows.data(),rows.size()),"partial inventory rejects enabled resume");
    auto changed=rows;changed.back().state=WTSActive;check(!AcceptInventory(enabled,2,true,changed.data(),changed.size()),"active target blocks a second enabled viewer");
    changed=rows;changed.push_back({4,WTSDisconnected,true});check(!AcceptInventory(enabled,2,true,changed.data(),changed.size()),"foreign user blocks enabled resume");
    changed=rows;changed[1].state=WTSDisconnected;check(!AcceptInventory(enabled,2,true,changed.data(),changed.size()),"inactive parent blocks enabled resume");
    check(!AcceptInventory(invalid,2,true,rows.data(),rows.size())&&!AcceptObservedId(invalid,2,4,rows.data(),rows.size()),"invalid preservation policy rejected at inventory and outcome gates");
    check(AcceptObservedId(enabled,2,3,rows.data(),rows.size())&&!AcceptObservedId(enabled,2,4,rows.data(),rows.size()),"enabled resume can observe only its original exact target");
    const ToggleApi api{&PreserveSet,&PreserveRead,&PreserveError,&PreserveSetError};
    preserveFixture={};auto result=VerifyPreservedEnabled(enabled,api);
    check(result.verified&&result.result==S_OK&&preserveFixture.reads==1&&preserveFixture.sets==0,"enabled setting verified without setter");
    preserveFixture={};result=VerifyPreservedEnabled(enabled,api,true);
    check(!result.verified&&result.result==HRESULT_FROM_WIN32(ERROR_CANCELLED)&&!preserveFixture.reads&&!preserveFixture.sets,"cancellation before connection reads or changes nothing");
    preserveFixture={};preserveFixture.enabled=false;result=VerifyPreservedEnabled(enabled,api);
    check(!result.verified&&result.result==E_UNEXPECTED&&preserveFixture.sets==0,"false readback is rejected rather than enabled");
    preserveFixture={};preserveFixture.fail=true;result=VerifyPreservedEnabled(enabled,api);
    check(!result.verified&&result.result==HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED)&&preserveFixture.sets==0,"readback error preserved without repair mutation");
    preserveFixture={};preserveFixture.throws=true;result=VerifyPreservedEnabled(enabled,api);
    check(!result.verified&&result.read.exceptionThrown&&preserveFixture.sets==0,"readback exception cannot trigger setter or success");
    preserveFixture={};result=VerifyPreservedEnabled(invalid,api);
    check(!result.verified&&result.result==E_INVALIDARG&&!preserveFixture.reads&&!preserveFixture.sets,"invalid preservation mode invokes no API");
    preserveFixture={};result=VerifyPreservedEnabled(ordinary,api);
    check(!result.verified&&result.result==E_INVALIDARG&&!preserveFixture.reads&&!preserveFixture.sets,"ordinary mode cannot enter read-only preservation helper");
    preserveFixture={};auto noSetter=api;noSetter.setEnabled=nullptr;result=VerifyPreservedEnabled(enabled,noSetter);
    check(result.verified&&preserveFixture.reads==1&&preserveFixture.sets==0,"preservation does not even require a setter pointer");
    preserveFixture={};auto missingRead=api;missingRead.isEnabled=nullptr;result=VerifyPreservedEnabled(enabled,missingRead);
    check(!result.verified&&result.result==E_POINTER&&!preserveFixture.reads&&!preserveFixture.sets,"missing reader fails without mutation");
    for(bool attempted:{false,true})for(bool restored:{false,true})
        check(!NeedsSettingRestore(enabled,attempted,restored),"enabled-preservation cleanup never authorizes a setter even with inconsistent attempt flags");
    check(NeedsSettingRestore(ordinary,true,false)&&!NeedsSettingRestore(ordinary,false,false)&&!NeedsSettingRestore(ordinary,true,true),"ordinary restore ownership behavior unchanged");
    snapshot=Baseline(3);snapshot.enabled.value=true;
    check(BaselineSettingVerified(enabled,snapshot,false,false,false),"preserved true baseline verifies after disconnect or connection failure");
    check(!BaselineSettingVerified(enabled,snapshot,true,false,false)&&!BaselineSettingVerified(enabled,snapshot,false,true,true),"mutation flags cannot produce preserved-baseline success");
    snapshot.enabled.value=false;check(!BaselineSettingVerified(enabled,snapshot,false,false,false),"unexpected disabled final state is not called preserved");
    snapshot.enabled.value=true;snapshot.enabled.succeeded=false;check(!BaselineSettingVerified(enabled,snapshot,false,false,false),"failed final query cannot claim preserved state");
    snapshot.enabled.succeeded=true;snapshot.enabled.attempted=false;check(!BaselineSettingVerified(enabled,snapshot,false,false,false),"unqueried final state remains unknown");
    snapshot=Baseline(3);check(BaselineSettingVerified(ordinary,snapshot,true,true,true)&&!BaselineSettingVerified(ordinary,snapshot,true,true,false),"ordinary restored baseline still requires successful owned restoration");
}

}
int main(){
    ArgsTests();SnapshotTests();InventoryTests();InventoryObservationTests();EnabledResumeTests();
    std::cout<<"Child session client mode: "<<passed<<" passed, "<<failed<<" failed; pure fixtures only, no Windows queries or connection.\n";
    return failed?1:0;
}
