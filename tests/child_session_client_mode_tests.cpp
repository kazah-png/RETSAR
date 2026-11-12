#include "../src/lab/child_session_client_mode.hpp"
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
}
int main(){
    ArgsTests();SnapshotTests();InventoryTests();
    std::cout<<"Child session client mode: "<<passed<<" passed, "<<failed<<" failed; pure fixtures only, no Windows queries or connection.\n";
    return failed?1:0;
}
