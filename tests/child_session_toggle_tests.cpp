#include "../src/lab/child_session_toggle.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace child=seats::lab::child;
namespace {
int checks=0;
void Check(bool condition,const char* description) {
    ++checks;
    if(!condition) {
        std::cerr<<"FAIL: "<<description<<"\n";
        std::exit(1);
    }
}
struct Plan {
    BOOL result=TRUE;
    DWORD error=ERROR_SUCCESS;
    BOOL output=FALSE;
    bool throws=false;
    bool writesError=true;
};
struct State {
    DWORD lastError=193;
    std::array<Plan,2> setters{};
    std::array<Plan,2> readers{{{TRUE,0,TRUE},{TRUE,0,FALSE}}};
    std::array<child::PersistenceResult,3> persistResults{{{true,0},{true,0},{true,0}}};
    std::array<bool,3> persistThrows{};
    std::vector<std::string> calls;
    std::vector<child::ToggleReport> receipts;
    std::vector<std::string> receiptJson;
    std::size_t settersCalled=0,readersCalled=0,persistCalled=0;
    bool clearBeforeEveryApi=true;
};
State* active=nullptr;
DWORD WINAPI ErrorGet() {return active->lastError;}
void WINAPI ErrorSet(DWORD value) {active->lastError=value;}
BOOL Set(BOOL value) {
    auto& state=*active;
    state.clearBeforeEveryApi=state.clearBeforeEveryApi&&(state.lastError==ERROR_SUCCESS);
    state.calls.push_back(value==FALSE?"disable":"enable");
    Check(state.settersCalled<state.setters.size(),"setter must not retry");
    const auto& plan=state.setters[state.settersCalled++];
    if(plan.throws)throw std::runtime_error("injected setter exception");
    if(plan.writesError)state.lastError=plan.error;
    return plan.result;
}
BOOL Read(BOOL* output) {
    auto& state=*active;
    state.clearBeforeEveryApi=state.clearBeforeEveryApi&&(state.lastError==ERROR_SUCCESS);
    state.calls.push_back("read");
    Check(state.readersCalled<state.readers.size(),"reader must not retry");
    const auto& plan=state.readers[state.readersCalled++];
    *output=plan.output; // Deliberately write even when failing.
    if(plan.throws)throw std::runtime_error("injected read exception");
    if(plan.writesError)state.lastError=plan.error;
    return plan.result;
}
child::PersistenceResult Persist(const child::ToggleReport& report,void* context) {
    auto& state=*static_cast<State*>(context);
    Check(&state==active,"persistence receives caller context");
    const auto index=state.persistCalled++;
    Check(index<state.persistResults.size(),"persistence must not retry");
    state.calls.push_back(child::TogglePhaseName(report.phase));
    state.receipts.push_back(report);
    state.receiptJson.push_back(child::ToJson(report));
    state.lastError=887; // Must not contaminate the next API error.
    if(state.persistThrows[index])throw std::runtime_error("injected persistence exception");
    return state.persistResults[index];
}
child::ToggleApi MockApi() {return {&Set,&Read,&ErrorGet,&ErrorSet};}
child::TogglePreconditions Allowed() {return {true,{true,true,0,false}};}
child::ToggleReport Run(State& state) {
    active=&state;
    return child::RunToggle(MockApi(),Allowed(),&Persist,&state);
}
void Sequence(const State& state) {
    const std::vector<std::string> expected{
        "intent","enable","read","after-enable","disable","read","restored"};
    Check(state.calls==expected,"single enable/read/restore/read with persisted intent first");
    Check(state.settersCalled==2&&state.readersCalled==2,"exactly one enable and restoration");
    Check(state.clearBeforeEveryApi,"clear LastError before every API");
    Check(state.lastError==193,"restore caller LastError");
}
void SuccessfulObservations(const child::ToggleReport& report) {
    Check(report.phase==child::TogglePhase::Restored,"terminal phase");
    Check(report.refusal==child::ToggleRefusal::None,"no refusal");
    Check(report.enableConfirmed,"enable acknowledged and observed");
    Check(report.safetyRestored,"disable acknowledged and observed");
    Check(report.durableComplete,"all writes acknowledged");
    Check(report.enable.value&&!report.restore.value,"setter requested values on success");
    Check(report.enabledAfter.value&&!report.restoredAfter.value,"readback values preserved");
}
void TestSuccess() {
    State state{};
    const auto report=Run(state);
    Sequence(state);
    SuccessfulObservations(report);
    Check(state.receipts.size()==3,"three persisted checkpoints");
    Check(!state.receipts[0].enable.attempted,"intent persisted before enable");
    Check(state.receipts[0].persistence[0].attempted&&
        !state.receipts[0].persistence[0].completed,"intent write is pending inside callback");
    Check(state.receipts[1].enableConfirmed&&!state.receipts[1].restore.attempted,
        "after-enable checkpoint precedes rollback");
    Check(state.receipts[2].safetyRestored&&!state.receipts[2].durableComplete,
        "terminal receipt records restoration without self-attesting its write");
    for(std::size_t i=0;i<report.persistence.size();++i) {
        Check(report.persistence[i].attempted&&report.persistence[i].completed&&
            report.persistence[i].succeeded,"completed writes in returned report");
        Check(!state.receipts[i].persistence[i].completed,"current receipt outcome pending");
    }
    Check(child::ToJson(report).find("\"createdSession\":false")!=std::string::npos,
        "never claims a created session");
    Check(child::ToJson(report).find("\"physicalSeatVerified\":false")!=std::string::npos,
        "never claims physical seat");
}
void TestRefusals() {
    for(int which=0;which<10;++which) {
        State state{};
        active=&state;
        auto api=MockApi();
        auto pre=Allowed();
        auto callback=&Persist;
        child::ToggleRefusal expected=child::ToggleRefusal::ApiUnavailable;
        switch(which) {
        case 0:api.setEnabled=nullptr;break;
        case 1:api.isEnabled=nullptr;break;
        case 2:api.getError=nullptr;break;
        case 3:api.setError=nullptr;break;
        case 4:pre.guardComplete=false;expected=child::ToggleRefusal::GuardIncomplete;break;
        case 5:pre.baseline.attempted=false;expected=child::ToggleRefusal::BaselineUnknown;break;
        case 6:pre.baseline.succeeded=false;pre.baseline.error=0;
            expected=child::ToggleRefusal::BaselineUnknown;break;
        case 7:pre.baseline.succeeded=false;pre.baseline.error=ERROR_ACCESS_DENIED;
            expected=child::ToggleRefusal::BaselineUnknown;break;
        case 8:pre.baseline.value=true;expected=child::ToggleRefusal::BaselineEnabled;break;
        case 9:callback=nullptr;expected=child::ToggleRefusal::PersistenceUnavailable;break;
        }
        const auto report=child::RunToggle(api,pre,callback,&state);
        Check(report.refusal==expected,"strict precondition refusal reason");
        Check(report.phase==child::TogglePhase::Refused,"refused before intent");
        Check(state.calls.empty(),"refused probe performs no API or persistence");
        Check(!report.enable.attempted&&!report.restore.attempted,"no effects on refusal");
        Check(!report.safetyRestored&&!report.enableConfirmed&&!report.durableComplete,
            "refusal is not a successful probe");
    }
}
void TestPersistence() {
    for(std::size_t phase=0;phase<3;++phase) {
        for(int failure=0;failure<3;++failure) {
            State state{};
            state.persistResults[phase]={false,static_cast<DWORD>(failure==0?ERROR_ACCESS_DENIED:ERROR_SUCCESS)};
            state.persistThrows[phase]=failure==2;
            const auto report=Run(state);
            Check(!report.durableComplete,"failed or thrown receipt never durable");
            const auto& observed=report.persistence[phase];
            Check(observed.attempted&&observed.completed&&!observed.succeeded,
                "failed persistence recorded");
            Check(observed.exceptionThrown==(failure==2),"persistence exception explicit");
            Check(observed.error==static_cast<DWORD>(failure==2?ERROR_UNHANDLED_EXCEPTION:
                (failure==0?ERROR_ACCESS_DENIED:ERROR_SUCCESS)),
                "raw persistence error zero retained; exception marked separately");
            if(phase==0) {
                Check(report.refusal==child::ToggleRefusal::IntentNotDurable,
                    "failed intent prevents effects");
                Check(state.calls==std::vector<std::string>{"intent"},"no API after failed intent");
                Check(!report.enable.attempted&&!report.restore.attempted,
                    "no unnecessary restore when enable never attempted");
                Check(!report.safetyRestored&&!report.enableConfirmed,"no false status after abort");
                Check(state.lastError==193,"caller LastError after failed intent");
            } else {
                Sequence(state);
                Check(report.safetyRestored&&report.enableConfirmed,"rollback survives write failure");
                Check(report.phase==child::TogglePhase::Restored,"rollback terminal despite write failure");
                Check(report.persistence[2].attempted,"final receipt attempted even after prior write failure");
            }
        }
    }
}
void TestApiFailures() {
    for(std::size_t call=0;call<4;++call) {
        for(int failure=0;failure<3;++failure) {
            State state{};
            auto& plan=(call%2==0)?state.setters[call/2]:state.readers[call/2];
            plan.result=FALSE;
            plan.error=failure==0?ERROR_ACCESS_DENIED:ERROR_SUCCESS;
            plan.writesError=failure!=1; // Failure that leaves LastError unchanged.
            plan.throws=failure==2;
            const auto report=Run(state);
            Sequence(state);
            const auto& observed=call==0?report.enable:(call==1?report.enabledAfter:
                (call==2?report.restore:report.restoredAfter));
            Check(observed.attempted&&!observed.succeeded,"failed API not success");
            Check(observed.exceptionThrown==(failure==2),"API exception separated");
            if(failure!=2) {
                Check(observed.returnValue==FALSE,"raw failed BOOL retained");
                Check(observed.error==static_cast<DWORD>(failure==0?ERROR_ACCESS_DENIED:ERROR_SUCCESS),
                    "exact API LastError including unset zero");
            }
            Check(!observed.value,"partial or failed API output discarded");
            Check(report.restore.attempted&&report.restoredAfter.attempted,
                "restoration and readback attempted after every enable attempt");
            Check(report.durableComplete,"API outcome independent of successful receipts");
            Check(report.enableConfirmed==(call>=2),"enable confirmation requires both enable observations");
            Check(report.safetyRestored==(call<2),"restoration requires both restore observations");
            Check(child::ToJson(report).find("\"value\":null")!=std::string::npos,
                "failed output serialized as unknown");
        }
    }
}
void TestContradictionsAndRawReturns() {
    {
        State state{};
        state.readers[0].output=FALSE;
        const auto report=Run(state);
        Sequence(state);
        Check(!report.enableConfirmed&&report.safetyRestored,"enable return alone cannot confirm enabled");
    }
    {
        State state{};
        state.readers[1].output=TRUE;
        const auto report=Run(state);
        Sequence(state);
        Check(report.enableConfirmed&&!report.safetyRestored,"restore return alone cannot confirm disabled");
    }
    {
        State state{};
        state.setters[0].result=7;
        state.setters[1].result=-13;
        state.readers[0].result=29;
        state.readers[0].output=-9;
        state.readers[1].result=23;
        const auto report=Run(state);
        Sequence(state);
        SuccessfulObservations(report);
        Check(report.enable.returnValue==7&&report.restore.returnValue==-13&&
            report.enabledAfter.returnValue==29&&report.restoredAfter.returnValue==23,
            "noncanonical nonzero BOOL values retained exactly");
        Check(child::ToJson(report).find("\"returnValue\":-13")!=std::string::npos,
            "raw BOOL emitted without Boolean normalization");
    }
    {
        State state{};
        state.setters[0].result=FALSE;
        state.setters[0].error=0;
        state.readers[0].result=FALSE;
        state.readers[0].error=ERROR_CALL_NOT_IMPLEMENTED;
        state.persistThrows[1]=true;
        state.setters[1].result=FALSE;
        state.setters[1].error=ERROR_ACCESS_DENIED;
        state.readers[1].result=FALSE;
        state.readers[1].error=0;
        state.persistResults[2]={false,ERROR_DISK_FULL};
        const auto report=Run(state);
        Sequence(state);
        Check(!report.enableConfirmed&&!report.safetyRestored&&!report.durableComplete,
            "multiple failures preserved without short-circuiting cleanup");
        Check(report.enable.error==0&&report.enabledAfter.error==ERROR_CALL_NOT_IMPLEMENTED&&
            report.restore.error==ERROR_ACCESS_DENIED&&report.restoredAfter.error==0,
            "each individual failure survives later failures");
        Check(report.persistence[1].exceptionThrown&&report.persistence[2].error==ERROR_DISK_FULL,
            "independent persistence failures survive");
    }
}
struct GroupedNumbers final:std::numpunct<char> {
    char do_thousands_sep() const override {return ',';}
    std::string do_grouping() const override {return "\3";}
};
void TestJson() {
    State state{};
    state.setters[0].result=FALSE;
    state.setters[0].error=123456;
    const auto report=Run(state);
    const auto previous=std::locale::global(std::locale(std::locale::classic(),new GroupedNumbers));
    const auto json=child::ToJson(report);
    std::locale::global(previous);
    Check(json.find("\"lastError\":123456")!=std::string::npos,"JSON numeric errors ignore process locale");
    Check(json.find("123,456")==std::string::npos,"JSON numbers have no grouping");
    Check(json.find("\"connectedClient\":false")!=std::string::npos,"JSON no connection claim");
    Check(json.find("\"windowsHomeSupport\":\"NOT_VERIFIED\"")!=std::string::npos,
        "JSON does not infer Home support");
    Check(child::ToJson(child::ToggleReport{}).find("\"returnValue\":null")!=std::string::npos,
        "unattempted return values are null");
}
}
int main(int argc,char** argv) {
    if(argc==2&&std::string(argv[1])=="--json-fixture") {
        State state{};
        state.setters[0].result=FALSE;
        state.setters[0].error=0;
        const auto report=Run(state);
        std::cout<<child::ToJson(report)<<"\n";
        return 0;
    }
    TestSuccess();
    TestRefusals();
    TestPersistence();
    TestApiFailures();
    TestContradictionsAndRawReturns();
    TestJson();
    std::cout<<"Child session toggle: "<<checks<<" inert checks passed; no WTS API invoked.\n";
    return 0;
}