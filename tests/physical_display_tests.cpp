#include "../src/lab/physical_display_model.hpp"
#include <iostream>
#include <locale>
#include <string>
using namespace retsar::lab::physical_display;
namespace {
unsigned passed=0,failed=0;
void Check(bool result,const char* label){result?++passed:++failed;std::cout<<(result?"PASS ":"FAIL ")<<label<<'\n';}
constexpr Call Success{true,true,0};
template<class T> Observed<T> Good(T value){return {Success,value};}
Target TargetFixture(){
    Target result;result.retrieval=Success;result.adapter=Success;result.id=Good<std::uint32_t>(5);
    result.adapterId=Good(AdapterId{42,-2});result.connected=Good(true);result.stale=Good(false);
    result.usage=Good<std::int32_t>(2);result.indirectApiInBuild=true;result.indirect=Good(false);return result;
}
Snapshot Fixture(){
    Snapshot result;result.execution=Success;result.apartment=Success;result.manager=Success;result.enumeration=Success;
    result.stateRead=Success;result.stateObject=Success;result.targetCount=Good<std::uint32_t>(1);
    result.stateResult=Good<std::int32_t>(0);result.stateExtendedError=Good<std::int32_t>(0);
    result.stateReadOnly=Good(true);result.stateStale=Good(false);result.enumerationComplete=true;
    result.targets.push_back(TargetFixture());return result;
}
class Grouped final:public std::numpunct<char>{
    char do_thousands_sep()const override{return ',';}
    std::string do_grouping()const override{return "\3";}
};
}
int main(int argc,char** argv){
    if(argc==2&&std::string(argv[1])=="--json-fixture"){
        auto sample=Fixture();sample.targets[0].connected.call={true,false,static_cast<std::int32_t>(0x80070005u)};
        std::cout<<ToJson(sample)<<'\n';return 0;
    }
    if(argc!=1)return 64;
    const wchar_t* help[]={L"probe",L"--help"};const wchar_t* status[]={L"probe",L"--status"};
    const wchar_t* extra[]={L"probe",L"--status",L"extra"};const wchar_t* unknown[]={L"probe",L"--acquire"};
    const wchar_t* nullArgument[]={L"probe",nullptr};
    Check(Parse(1,help)==Command::Help&&Parse(2,help)==Command::Help,"default and help are inert modes");
    Check(Parse(2,status)==Command::Status,"status is the single observation command");
    Check(Parse(3,extra)==Command::Invalid&&Parse(2,unknown)==Command::Invalid,"extra and mutation arguments are rejected");
    Check(Parse(2,nullArgument)==Command::Invalid&&Parse(0,help)==Command::Invalid&&Parse(1,nullptr)==Command::Invalid,"malformed arguments never authorize observation");
    Check(!Complete({})&&Classify({},true)==Eligibility::Unknown,"unknown observations cannot establish a candidate");
    const auto baseline=Fixture();Check(Complete(baseline),"complete coherent read-only fixture is accepted");
    Check(Classify(baseline.targets[0],Complete(baseline))==Eligibility::SpecializedCandidate,"direct specialized target is only an unverified candidate");
    auto target=TargetFixture();target.usage.value=1;Check(Classify(target,true)==Eligibility::SpecializedCandidate,"head-mounted target has the same limited candidate status");
    target.usage.value=0;Check(Classify(target,true)==Eligibility::StandardIneligible,"ordinary monitor never becomes eligible");
    target.indirectApiInBuild=false;target.indirect={};Check(Classify(target,true)==Eligibility::StandardIneligible,"ordinary monitor remains ineligible when optional projection is absent");
    target.usage.value=2;Check(Classify(target,true)==Eligibility::IndirectUnverified,"missing adapter-kind projection cannot manufacture eligibility");
    target=TargetFixture();target.indirect.value=true;Check(Classify(target,true)==Eligibility::IndirectIneligible,"indirect adapter is not a physical scanout candidate");
    target=TargetFixture();target.usage.value=999;Check(Classify(target,true)==Eligibility::UnknownUsage,"unrecognized usage enum remains unsupported");
    target=TargetFixture();target.connected.value=false;Check(Classify(target,true)==Eligibility::Disconnected,"disconnected connector cannot be a candidate");
    target=TargetFixture();target.stale.value=true;Check(Classify(target,true)==Eligibility::Unknown,"stale target cannot be a candidate");
    Check(Classify(TargetFixture(),false)==Eligibility::Unknown,"incomplete global evidence blocks a seemingly valid target");
    for(auto member:{&Snapshot::execution,&Snapshot::apartment,&Snapshot::manager,&Snapshot::enumeration,&Snapshot::stateRead,&Snapshot::stateObject}){
        auto value=baseline;value.*member={true,false,static_cast<std::int32_t>(0x80070005u)};
        Check(!Complete(value),"failed required operation keeps entire observation incomplete");
        value.*member={false,true,0};Check(!Complete(value),"success without an attempted operation is rejected");
    }
    auto value=baseline;value.stateResult.value=4;Check(!Complete(value),"remote-session-not-supported result is not success");
    value=baseline;value.stateExtendedError.value=static_cast<std::int32_t>(0x80004005u);Check(!Complete(value),"failing extended HRESULT cannot accompany successful state");
    value=baseline;value.stateReadOnly.value=false;Check(!Complete(value),"unexpected mutable state is rejected");
    value=baseline;value.stateStale.value=true;Check(!Complete(value),"state change during collection invalidates the snapshot");
    value=baseline;value.stateStale.call={};Check(!Complete(value),"missing final staleness check is unknown");
    value=baseline;value.enumerationComplete=false;Check(!Complete(value),"partial enumeration is incomplete");
    value=baseline;value.targetCount.value=2;Check(!Complete(value),"reported target count must match collected targets");
    value=baseline;value.targets.push_back(value.targets[0]);value.targetCount.value=2;Check(!Complete(value),"duplicate connector identity invalidates the snapshot");
    value.targets[1].adapterId.value.low=43;Check(Complete(value),"same relative connector on distinct adapters is unambiguous");
    value=baseline;value.targets.clear();value.targetCount.value=0;Check(Complete(value),"successful empty enumeration is not a fabricated display");
    value=baseline;value.targets[0].indirectApiInBuild=false;value.targets[0].indirect={};
    Check(Complete(value)&&Classify(value.targets[0],true)==Eligibility::IndirectUnverified,"absent optional SDK projection is disclosed separately from required query completion");
    value=baseline;value.targets[0].indirect.call={true,false,static_cast<std::int32_t>(0x80004002u)};
    Check(!Complete(value)&&Classify(value.targets[0],true)==Eligibility::Unknown,"available projection failing at runtime is never treated as direct hardware");
    for(auto member:{&Target::retrieval,&Target::adapter}){
        value=baseline;value.targets[0].*member={};Check(!Complete(value),"missing target or adapter object cannot qualify");
    }
    value=baseline;value.targets[0].id.call={};Check(!Complete(value),"unknown target identity blocks completeness");
    value=baseline;value.targets[0].adapterId.call={};Check(!Complete(value),"unknown adapter identity blocks completeness");
    value=baseline;value.targets[0].connected.call={};Check(!Complete(value),"unknown connected state is not false or true");
    value=baseline;value.targets[0].usage.call={};Check(!Complete(value),"unknown usage cannot become standard or specialized");
    value=baseline;value.targets[0].stale.call={};Check(!Complete(value),"unknown target staleness blocks completeness");
    value=baseline;value.targets[0].connected.call={true,false,static_cast<std::int32_t>(0x80070005u)};
    const auto json=ToJson(value);
    Check(json.find("\"hresult\":2147942405,\"value\":null")!=std::string::npos,"JSON retains original HRESULT and hides invalid output value");
    Check(json.find("\"complete\":false")!=std::string::npos,"JSON exposes incomplete observation");
    const auto goodJson=ToJson(baseline);
    for(const auto* claim:{"\"acquisitionAttempted\":false","\"displayAssignmentAttempted\":false","\"inputAssignmentAttempted\":false","\"sessionBindingVerified\":false","\"physicalSeatVerified\":false","\"ownershipVerified\":false"})
        Check(goodJson.find(claim)!=std::string::npos,"even an eligible fixture never claims an assignment or ownership");
    Check(goodJson.find("serial")==std::string::npos&&goodJson.find("devicePath")==std::string::npos&&goodJson.find("EDID")==std::string::npos,"JSON has no monitor serial, path or EDID fields");
    const auto previous=std::locale::global(std::locale(std::locale::classic(),new Grouped));
    value=baseline;value.targets[0].adapterId.value.low=1234567;const auto invariant=ToJson(value);std::locale::global(previous);
    Check(invariant.find("1234567")!=std::string::npos&&invariant.find("1,234,567")==std::string::npos,"JSON numbers ignore system grouping locale");
    std::cout<<"Physical display model: "<<passed<<" passed, "<<failed<<" failed; pure fixtures only, no Windows API or device assignment.\n";
    return failed?1:0;
}
