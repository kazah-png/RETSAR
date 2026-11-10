#include "../src/backend.hpp"
#include <iostream>

namespace {
unsigned passes=0,failures=0;
void check(bool ok,const char* message){std::cout<<(ok?"PASS ":"FAIL ")<<message<<'\n';if(ok)++passes;else ++failures;}
bool has(const std::vector<seats::ValidationError>& errors,const std::string& code){return std::any_of(errors.begin(),errors.end(),[&](const auto& error){return error.code==code;});}
}
int main(int argc,char** argv){
    if(argc==2&&std::string(argv[1])=="--json-fixture"){
        // Deliberately a TEST schema, never presented as real device inventory.
        std::cout<<seats::Json(seats::Json::Object{{"schema","retsar.test-fixture.v1"},{"testOnly",true},
            {"escaped",std::string("quote\" slash\\ newline\n tab\t control")+char(1)},
            {"utf8",u8"Unicode · café"},{"array",seats::Json::Array{nullptr,true,false,-2,0,42}},
            {"capabilities",seats::capabilityJson({})}}).dump()<<'\n';return 0;
    }
    using namespace seats;
    SeatConfig main{"main",true,{"display-1"},{"keyboard-1","mouse-1"}};
    SeatConfig farm{"farm",true,{"display-2"},{}};
    std::vector<SeatConfig> valid{main,farm};
    check(validateConfiguration(valid).empty(),"valid separate resource declarations accepted structurally");
    auto config=valid;config[1].inputDeviceIds={"MOUSE-1"};check(has(validateConfiguration(config),"duplicateDeviceAssignment"),"same mouse cannot belong to two enabled seats (case-insensitive id)");
    config=valid;config[0].inputDeviceIds.push_back("keyboard-1");check(has(validateConfiguration(config),"duplicateDeviceAssignment"),"duplicate within same seat rejected");
    config=valid;config[1].monitorIds={"DISPLAY-1"};check(has(validateConfiguration(config),"duplicateMonitorAssignment"),"same monitor cannot belong to two enabled seats");
    config=valid;config[1].monitorIds.clear();check(has(validateConfiguration(config),"requiredMonitor"),"enabled seat requires a monitor");
    config[1].enabled=false;check(validateConfiguration(config).empty(),"disabled seat does not require a monitor");
    config=valid;config[1].enabled=false;config[1].monitorIds={"display-1"};config[1].inputDeviceIds={"mouse-1"};check(validateConfiguration(config).empty(),"disabled seat does not reserve resources");
    config=valid;config[1].id="MAIN";check(has(validateConfiguration(config),"duplicateSeatId"),"seat ids must be unique");
    config=valid;config[1].id="";check(has(validateConfiguration(config),"missingSeatId"),"missing seat id rejected");
    config=valid;config[1].monitorIds={""};check(has(validateConfiguration(config),"emptyResourceId"),"empty monitor id rejected");
    config=valid;config[1].inputDeviceIds={""};check(has(validateConfiguration(config),"emptyResourceId"),"empty input id rejected");
    Capabilities caps{};
    check(missingOperationalCapabilities(caps).size()==5,"inventory backend declares all five missing engine capabilities");
    check(!canStartSeats(valid,caps),"structurally valid configuration cannot bypass capability gate");
    caps.assignOutputs=caps.sessionLifecycle=caps.independentInput=caps.seatLifecycle=caps.systemBackend=Capability::Available;
    check(canStartSeats(valid,caps),"pure planning gate accepts valid config only with all operational capabilities");
    auto incomplete=caps;incomplete.independentInput=Capability::NotImplemented;check(!canStartSeats(valid,incomplete),"unimplemented independent input blocks startup");
    incomplete=caps;incomplete.sessionLifecycle=Capability::Unsupported;check(!canStartSeats(valid,incomplete),"unsupported session lifecycle blocks startup");
    config=valid;config[1].monitorIds={"display-1"};check(!canStartSeats(config,caps),"available capabilities never override conflicting resources");
    check(!canStartSeats({},caps),"empty plan cannot be started");
    config=valid;for(auto& seat:config)seat.enabled=false;check(!canStartSeats(config,caps),"all-disabled plan cannot be started");
    WindowsInventoryBackend backend; // Constructor performs no inventory or device initialization.
    check(backend.capabilities().inventory==Capability::Available,"read-only inventory is implemented");
    const auto start=backend.StartSeat(main);
    check(start.outcome==Outcome::Unsupported&&start.code=="ENGINE_NOT_IMPLEMENTED","real inventory backend StartSeat returns Unsupported, never success");
    check(Json::quote("a\"b\\c\n")=="\"a\\\"b\\\\c\\n\"","JSON escapes quotes backslashes and newlines");
    check(Json::quote(std::string(1,char(1)))=="\"\\u0001\"","JSON escapes low control characters");
    check(Json(Json::Array{nullptr,true,false,-2}).dump()=="[null,true,false,-2]","JSON primitive types remain valid");
    check(Json(std::string(u8"café")).dump()==std::string(u8"\"café\""),"UTF-8 text preserved");
    std::cout<<"TOTAL "<<passes<<" passed, "<<failures<<" failed. No device inventory was executed.\n";
    return failures?1:0;
}
