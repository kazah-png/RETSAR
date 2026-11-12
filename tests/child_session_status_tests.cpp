#include "../src/lab/child_session_status.hpp"
#include <cstdlib>
#include <iostream>
#include <locale>
#include <string>

using namespace seats::lab::child;
namespace {
unsigned checks=0;
void Check(bool condition,const char* label) {
    ++checks;
    if(!condition){std::cerr<<"FAIL "<<label<<"\n";std::exit(1);}
}
struct Scenario {
    DWORD lastError=0x12345678;
    BOOL enabledResult=TRUE,enabledValue=TRUE;
    DWORD enabledError=ERROR_SUCCESS;
    BOOL childResult=TRUE;
    ULONG childValue=2;
    DWORD childError=ERROR_SUCCESS;
    BOOL callerResult=TRUE;
    DWORD callerValue=1,callerError=ERROR_SUCCESS,consoleValue=1;
    unsigned enabledCalls=0,childCalls=0,callerCalls=0,consoleCalls=0,pidCalls=0,errorReads=0,errorWrites=0;
    DWORD requestedPid=0;
};
Scenario scenario;
BOOL MockEnabled(PBOOL value) {
    ++scenario.enabledCalls;*value=scenario.enabledValue;
    scenario.lastError=scenario.enabledError;return scenario.enabledResult;
}
BOOL MockChild(PULONG value) {
    ++scenario.childCalls;*value=scenario.childValue;
    scenario.lastError=scenario.childError;return scenario.childResult;
}
DWORD WINAPI MockPid() {++scenario.pidCalls;return 42;}
BOOL WINAPI MockCaller(DWORD pid,DWORD* value) {
    ++scenario.callerCalls;scenario.requestedPid=pid;*value=scenario.callerValue;
    scenario.lastError=scenario.callerError;return scenario.callerResult;
}
DWORD WINAPI MockConsole() {++scenario.consoleCalls;scenario.lastError=ERROR_GEN_FAILURE;return scenario.consoleValue;}
DWORD WINAPI MockError() {++scenario.errorReads;return scenario.lastError;}
void WINAPI MockSetError(DWORD value) {++scenario.errorWrites;scenario.lastError=value;}
Api MockApi() {return {&MockEnabled,&MockChild,&MockPid,&MockCaller,&MockConsole,&MockError,&MockSetError};}
void Reset() {scenario=Scenario{};}
bool Contains(const std::string& text,const std::string& part) {return text.find(part)!=std::string::npos;}
class GroupedPunctuation final:public std::numpunct<char> {
protected:
    char do_thousands_sep()const override {return ',';}
    std::string do_grouping()const override {return "\3";}
};
void CheckNonClaims(const Snapshot& snapshot) {
    const auto json=ToJson(snapshot);
    Check(Contains(json,"\"createdSession\":false"),"diagnostic never claims session creation");
    Check(Contains(json,"\"ownershipProven\":false"),"numeric ID never proves ownership");
    Check(Contains(json,"\"physicalSeatVerified\":false"),"diagnostic never claims a physical seat");
    Check(Contains(json,"\"windowsHomeSupport\":\"NOT_VERIFIED\""),"Home support remains unverified");
}
}
int main(int argc,char** argv) {
    if(argc==2&&std::string(argv[1])=="--json-fixture") {
        Reset();scenario.childValue=MAXDWORD;scenario.callerResult=FALSE;
        scenario.callerError=ERROR_ACCESS_DENIED;scenario.enabledResult=FALSE;scenario.enabledError=0;
        std::cout<<ToJson(Query(MockApi()))<<"\n";return 0;
    }
    if(argc!=1)return 2;
    Reset();
    auto snapshot=Query(MockApi());
    Check(snapshot.apiAvailable,"all injected API pointers available");
    Check(snapshot.processId==42&&scenario.requestedPid==42,"query only current process session");
    Check(snapshot.enabled.attempted&&snapshot.enabled.succeeded&&snapshot.enabled.value,"enabled success");
    Check(snapshot.caller.attempted&&snapshot.caller.succeeded&&snapshot.caller.value==1,"caller session success");
    Check(snapshot.child.attempted&&snapshot.child.succeeded&&snapshot.child.value==2,"child ID success");
    Check(snapshot.console.attempted&&snapshot.console.value==1,"console attached observation");
    Check(ClassifyChild(snapshot)==ChildState::IdObserved,"successful numeric child is observation only");
    Check(scenario.lastError==0x12345678,"caller's last error restored");
    Check(scenario.enabledCalls==1&&scenario.childCalls==1&&scenario.callerCalls==1&&
          scenario.consoleCalls==1&&scenario.pidCalls==1,"one bounded call to each requested query");
    CheckNonClaims(snapshot);

    Reset();scenario.enabledValue=FALSE;
    snapshot=Query(MockApi());
    Check(snapshot.enabled.succeeded&&!snapshot.enabled.value,"disabled is a successful false value");
    Check(ClassifyChild(snapshot)==ChildState::IdObserved,"independent observations are not forced consistent");
    Reset();scenario.childValue=MAXDWORD;
    snapshot=Query(MockApi());
    Check(snapshot.child.succeeded&&snapshot.child.value==MAXDWORD,"documented successful no-child sentinel");
    Check(ClassifyChild(snapshot)==ChildState::Absent,"only successful sentinel establishes absence");
    Check(Contains(ToJson(snapshot),"\"childState\":\"absent\""),"absence serialized explicitly");
    Check(Contains(ToJson(snapshot),"\"child\":{\"attempted\":true,\"succeeded\":true,\"lastError\":null,\"sessionId\":4294967295}"),
          "raw successful sentinel is preserved");
    CheckNonClaims(snapshot);

    for(DWORD error:{DWORD(ERROR_NOT_SUPPORTED),DWORD(ERROR_CALL_NOT_IMPLEMENTED),DWORD(ERROR_ACCESS_DENIED),DWORD(0)}) {
        Reset();scenario.childResult=FALSE;scenario.childValue=73;scenario.childError=error;
        snapshot=Query(MockApi());
        Check(snapshot.child.attempted&&!snapshot.child.succeeded&&snapshot.child.error==error,
              "child query failure preserves exact observed error, including zero");
        Check(snapshot.child.value==MAXDWORD,"failed child output is discarded");
        Check(ClassifyChild(snapshot)==ChildState::Unknown,"failed child query is never absence");
        Check(Contains(ToJson(snapshot),"\"child\":{\"attempted\":true,\"succeeded\":false,\"lastError\":"+std::to_string(error)+",\"sessionId\":null}"),
              "failed child query is unambiguous in JSON");
        Check(scenario.enabledCalls==1&&scenario.callerCalls==1&&scenario.consoleCalls==1,"failure does not suppress independent diagnostics");
        Check(scenario.lastError==0x12345678,"last error restored after failed child query");
    }
    Reset();scenario.enabledResult=FALSE;scenario.enabledValue=TRUE;scenario.enabledError=ERROR_ACCESS_DENIED;
    snapshot=Query(MockApi());
    Check(!snapshot.enabled.succeeded&&!snapshot.enabled.value&&snapshot.enabled.error==ERROR_ACCESS_DENIED,
          "failed enablement query discards a partially written BOOL");
    Check(Contains(ToJson(snapshot),"\"enabled\":{\"attempted\":true,\"succeeded\":false,\"lastError\":5,\"value\":null}"),
          "unknown enablement differs from disabled");
    Reset();scenario.enabledResult=FALSE;scenario.enabledError=ERROR_SUCCESS;
    snapshot=Query(MockApi());
    Check(!snapshot.enabled.succeeded&&snapshot.enabled.error==0,"zero last error never turns BOOL failure into success");

    Reset();scenario.callerResult=FALSE;scenario.callerError=ERROR_INVALID_PARAMETER;scenario.callerValue=74;
    snapshot=Query(MockApi());
    Check(snapshot.caller.attempted&&!snapshot.caller.succeeded&&snapshot.caller.error==ERROR_INVALID_PARAMETER,
          "caller session failure preserves error");
    Check(snapshot.caller.value==MAXDWORD,"caller partial output discarded");
    Check(Contains(ToJson(snapshot),"\"caller\":{\"attempted\":true,\"succeeded\":false,\"lastError\":87,\"sessionId\":null}"),
          "caller failure JSON has no invented ID");
    Reset();scenario.consoleValue=MAXDWORD;
    snapshot=Query(MockApi());
    Check(snapshot.console.attempted&&snapshot.console.value==MAXDWORD,"console transition sentinel retained");
    Check(Contains(ToJson(snapshot),"\"console\":{\"attempted\":true,\"rawSessionId\":4294967295,\"attached\":false}"),
          "console no-attachment does not invent a Win32 error");
    Reset();scenario.enabledResult=-1;scenario.enabledValue=-2;scenario.childResult=-1;scenario.callerResult=-1;
    snapshot=Query(MockApi());
    Check(snapshot.enabled.succeeded&&snapshot.enabled.value&&snapshot.child.succeeded&&snapshot.caller.succeeded,
          "Windows BOOL accepts every nonzero success value");
    Reset();scenario.childValue=scenario.callerValue;
    snapshot=Query(MockApi());
    Check(snapshot.child.value==snapshot.caller.value&&ClassifyChild(snapshot)==ChildState::IdObserved,
          "numeric equality is preserved without creation or independence inference");
    CheckNonClaims(snapshot);

    for(unsigned missing=0;missing<7;++missing) {
        Reset();auto api=MockApi();
        switch(missing) {
        case 0:api.isEnabled=nullptr;break;case 1:api.getChildId=nullptr;break;
        case 2:api.getProcessId=nullptr;break;case 3:api.getCallerId=nullptr;break;
        case 4:api.getConsoleId=nullptr;break;case 5:api.getError=nullptr;break;
        case 6:api.setError=nullptr;break;
        }
        snapshot=Query(api);
        Check(!snapshot.apiAvailable&&!snapshot.enabled.attempted&&!snapshot.child.attempted&&
              !snapshot.caller.attempted&&!snapshot.console.attempted,"incomplete bindings execute no partial query");
        Check(scenario.enabledCalls+scenario.childCalls+scenario.callerCalls+scenario.consoleCalls+
              scenario.pidCalls+scenario.errorReads+scenario.errorWrites==0,"missing binding makes zero injected calls");
        Check(ClassifyChild(snapshot)==ChildState::Unknown,"unattempted query is unknown");
        Check(Contains(ToJson(snapshot),"\"apiAvailable\":false,\"processId\":null"),"missing API is explicit in JSON");
        Check(scenario.lastError==0x12345678,"missing API preserves caller error without touching it");
    }
    Snapshot neverQueried{};
    Check(ClassifyChild(neverQueried)==ChildState::Unknown,"default status does not imply absent child");
    neverQueried.child.succeeded=true;neverQueried.child.value=2;
    Check(ClassifyChild(neverQueried)==ChildState::Unknown,"unattempted fabricated success is not an observation");
    Check(std::string(ChildStateName(static_cast<ChildState>(99)))=="unknown","unknown enum diagnostic is bounded");
    Reset();scenario.childValue=12345;
    snapshot=Query(MockApi());
    const auto previousLocale=std::locale();
    std::locale::global(std::locale(previousLocale,new GroupedPunctuation));
    const auto localeJson=ToJson(snapshot);
    std::locale::global(previousLocale);
    Check(Contains(localeJson,"\"sessionId\":12345")&&!Contains(localeJson,"12,345"),
          "JSON decimal numbers are independent of process locale");
    std::cout<<"PASS "<<checks<<" child-session diagnostic checks; injected APIs only, no Windows session query or mutation.\n";
}
