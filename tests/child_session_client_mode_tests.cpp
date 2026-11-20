#include "../src/lab/child_session_client_mode.hpp"
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

}
int main(){
    ArgsTests();SnapshotTests();InventoryTests();InventoryObservationTests();
    std::cout<<"Child session client mode: "<<passed<<" passed, "<<failed<<" failed; pure fixtures only, no Windows queries or connection.\n";
    return failed?1:0;
}
