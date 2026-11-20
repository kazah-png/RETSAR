#pragma once
#include <cstdint>
#include <cwchar>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace retsar::lab::physical_display {
// Pure observations. No type in this model can acquire or configure a display.
struct Call {bool attempted=false,succeeded=false;std::int32_t hresult=0;};
template<class T> struct Observed {Call call{};T value{};};
struct AdapterId {std::uint32_t low=0;std::int32_t high=0;};
struct Target {
    Call retrieval{},adapter{};
    Observed<std::uint32_t> id{};
    Observed<AdapterId> adapterId{};
    Observed<bool> connected{},stale{},indirect{};
    Observed<std::int32_t> usage{};
    bool indirectApiInBuild=false;
};
struct Snapshot {
    Call execution{},apartment{},manager{},enumeration{},stateRead{},stateObject{};
    Observed<std::uint32_t> targetCount{};
    Observed<std::int32_t> stateResult{},stateExtendedError{};
    Observed<bool> stateReadOnly{},stateStale{};
    bool enumerationComplete=false;
    std::vector<Target> targets;
};
inline bool Known(const Call& call)noexcept{return call.attempted&&call.succeeded&&call.hresult>=0;}
template<class T> bool Known(const Observed<T>& observation)noexcept{return Known(observation.call);}
inline bool SameIdentity(const Target& a,const Target& b)noexcept {
    return Known(a.id)&&Known(b.id)&&Known(a.adapterId)&&Known(b.adapterId)&&a.id.value==b.id.value&&
        a.adapterId.value.low==b.adapterId.value.low&&a.adapterId.value.high==b.adapterId.value.high;
}
inline bool TargetQueriesComplete(const Target& target)noexcept {
    return Known(target.retrieval)&&Known(target.adapter)&&Known(target.id)&&Known(target.adapterId)&&
        Known(target.connected)&&Known(target.stale)&&Known(target.usage)&&
        (!target.indirectApiInBuild||Known(target.indirect));
}
// A projection absent from the build SDK is reported separately, not invented
// as a successful direct-adapter observation. It blocks candidate classification.
inline bool Complete(const Snapshot& value)noexcept {
    if(!Known(value.execution)||!Known(value.apartment)||!Known(value.manager)||!Known(value.enumeration)||
       !Known(value.targetCount)||!value.enumerationComplete||value.targetCount.value!=value.targets.size()||
       !Known(value.stateRead)||!Known(value.stateObject)||!Known(value.stateResult)||value.stateResult.value!=0||
       !Known(value.stateExtendedError)||value.stateExtendedError.value<0||!Known(value.stateReadOnly)||
       !value.stateReadOnly.value||!Known(value.stateStale)||value.stateStale.value)return false;
    for(std::size_t i=0;i<value.targets.size();++i){
        if(!TargetQueriesComplete(value.targets[i])||value.targets[i].stale.value)return false;
        for(std::size_t j=0;j<i;++j)if(SameIdentity(value.targets[i],value.targets[j]))return false;
    }
    return true;
}
enum class Eligibility {Unknown,Disconnected,StandardIneligible,UnknownUsage,IndirectUnverified,IndirectIneligible,SpecializedCandidate};
inline Eligibility Classify(const Target& target,bool complete)noexcept {
    if(!complete||!TargetQueriesComplete(target)||target.stale.value)return Eligibility::Unknown;
    if(!target.connected.value)return Eligibility::Disconnected;
    if(target.usage.value==0)return Eligibility::StandardIneligible;
    if(target.usage.value!=1&&target.usage.value!=2)return Eligibility::UnknownUsage;
    if(!target.indirectApiInBuild||!Known(target.indirect))return Eligibility::IndirectUnverified;
    if(target.indirect.value)return Eligibility::IndirectIneligible;
    return Eligibility::SpecializedCandidate;
}
inline const char* EligibilityName(Eligibility value)noexcept {
    switch(value){
    case Eligibility::Disconnected:return "disconnected";
    case Eligibility::StandardIneligible:return "standard-monitor-ineligible";
    case Eligibility::UnknownUsage:return "unknown-usage-kind";
    case Eligibility::IndirectUnverified:return "adapter-kind-unverified";
    case Eligibility::IndirectIneligible:return "indirect-adapter-ineligible";
    case Eligibility::SpecializedCandidate:return "specialized-candidate-unverified";
    default:return "observation-incomplete";
    }
}
enum class Command {Help,Status,Invalid};
inline Command Parse(int argc,const wchar_t* const* argv)noexcept {
    if(!argv||argc<1||!argv[0])return Command::Invalid;
    if(argc==1)return Command::Help;
    if(argc!=2||!argv[1])return Command::Invalid;
    if(std::wcscmp(argv[1],L"--help")==0)return Command::Help;
    if(std::wcscmp(argv[1],L"--status")==0)return Command::Status;
    return Command::Invalid;
}
namespace json {
inline const char* Bool(bool value)noexcept{return value?"true":"false";}
inline void Fields(std::ostringstream& out,const Call& call){
    out<<"\"attempted\":"<<Bool(call.attempted)<<",\"succeeded\":"<<Bool(call.succeeded)<<",\"hresult\":";
    if(call.attempted)out<<static_cast<std::uint32_t>(call.hresult);else out<<"null";
}
inline void Write(std::ostringstream& out,const Call& call){out<<'{';Fields(out,call);out<<'}';}
inline void Value(std::ostringstream& out,bool value){out<<Bool(value);}
inline void Value(std::ostringstream& out,std::uint32_t value){out<<value;}
inline void Value(std::ostringstream& out,std::int32_t value){out<<value;}
inline void Value(std::ostringstream& out,const AdapterId& value){out<<"{\"low\":"<<value.low<<",\"high\":"<<value.high<<'}';}
template<class T> void Write(std::ostringstream& out,const Observed<T>& value){
    out<<'{';Fields(out,value.call);out<<",\"value\":";if(Known(value))Value(out,value.value);else out<<"null";out<<'}';
}
}
inline std::string ToJson(const Snapshot& value){
    std::ostringstream out;out.imbue(std::locale::classic());const bool complete=Complete(value);
    out<<"{\"schema\":\"retsar.physical-display-status.v1\",\"backend\":\"Windows.Devices.Display.Core\",\"complete\":"<<json::Bool(complete)
       <<",\"readOnly\":true,\"execution\":";json::Write(out,value.execution);
    out<<",\"apartment\":";json::Write(out,value.apartment);
    out<<",\"manager\":";json::Write(out,value.manager);
    out<<",\"enumeration\":";json::Write(out,value.enumeration);
    out<<",\"targetCount\":";json::Write(out,value.targetCount);
    out<<",\"enumerationComplete\":"<<json::Bool(value.enumerationComplete)<<",\"stateRead\":";json::Write(out,value.stateRead);
    out<<",\"stateResult\":";json::Write(out,value.stateResult);
    out<<",\"stateExtendedError\":";json::Write(out,value.stateExtendedError);
    out<<",\"stateObject\":";json::Write(out,value.stateObject);
    out<<",\"stateReadOnly\":";json::Write(out,value.stateReadOnly);
    out<<",\"stateStale\":";json::Write(out,value.stateStale);
    out<<",\"targets\":[";
    for(std::size_t i=0;i<value.targets.size();++i){
        if(i)out<<',';const auto& target=value.targets[i];
        out<<"{\"retrieval\":";json::Write(out,target.retrieval);
        out<<",\"targetId\":";json::Write(out,target.id);
        out<<",\"adapter\":";json::Write(out,target.adapter);
        out<<",\"adapterLuid\":";json::Write(out,target.adapterId);
        out<<",\"connected\":";json::Write(out,target.connected);
        out<<",\"stale\":";json::Write(out,target.stale);
        out<<",\"usageKind\":";json::Write(out,target.usage);
        out<<",\"indirectApiInBuild\":"<<json::Bool(target.indirectApiInBuild)<<",\"indirect\":";json::Write(out,target.indirect);
        out<<",\"eligibility\":\""<<EligibilityName(Classify(target,complete))<<"\",\"ownershipVerified\":false}";
    }
    out<<"],\"acquisitionAttempted\":false,\"displayAssignmentAttempted\":false,\"inputAssignmentAttempted\":false,\"sessionBindingVerified\":false,\"physicalSeatVerified\":false}";
    return out.str();
}
}
