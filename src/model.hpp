#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace seats {
enum class Capability { Available, Unsupported, NotImplemented };
inline const char* name(Capability value){
    switch(value){case Capability::Available:return "AVAILABLE";case Capability::Unsupported:return "UNSUPPORTED";default:return "NOT_IMPLEMENTED";}
}
struct Capabilities {
    Capability inventory=Capability::Available;
    Capability assignOutputs=Capability::NotImplemented;
    Capability sessionLifecycle=Capability::NotImplemented;
    Capability independentInput=Capability::NotImplemented;
    Capability seatLifecycle=Capability::NotImplemented;
    Capability systemBackend=Capability::NotImplemented;
};
struct SeatConfig {
    std::string id;
    bool enabled=true;
    std::vector<std::string> monitorIds;
    std::vector<std::string> inputDeviceIds;
};
struct ValidationError {std::string code,seatId,detail;};
inline std::string folded(std::string value){
    for(auto& c:value)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    return value;
}
inline std::vector<ValidationError> validateConfiguration(const std::vector<SeatConfig>& config){
    std::vector<ValidationError> errors;
    std::unordered_map<std::string,std::string> ids,monitors,devices;
    for(const auto& seat:config){
        if(seat.id.empty()){errors.push_back({"missingSeatId",seat.id,"A seat needs a nonempty identifier."});}
        else if(!ids.emplace(folded(seat.id),seat.id).second){errors.push_back({"duplicateSeatId",seat.id,"Seat identifiers must be unique."});}
        if(!seat.enabled)continue;
        if(seat.monitorIds.empty())errors.push_back({"requiredMonitor",seat.id,"An enabled seat needs at least one monitor."});
        auto assign=[&](const std::vector<std::string>& values,std::unordered_map<std::string,std::string>& owners,const char* duplicate){
            for(const auto& value:values){
                if(value.empty()){errors.push_back({"emptyResourceId",seat.id,"Resource identifiers must not be empty."});continue;}
                auto inserted=owners.emplace(folded(value),seat.id);
                if(!inserted.second)errors.push_back({duplicate,seat.id,"Resource is already assigned to "+inserted.first->second+"."});
            }
        };
        assign(seat.monitorIds,monitors,"duplicateMonitorAssignment");
        assign(seat.inputDeviceIds,devices,"duplicateDeviceAssignment");
    }
    return errors;
}
inline std::vector<std::string> missingOperationalCapabilities(const Capabilities& caps){
    std::vector<std::string> missing;
    if(caps.assignOutputs!=Capability::Available)missing.push_back("assignOutputs");
    if(caps.sessionLifecycle!=Capability::Available)missing.push_back("sessionLifecycle");
    if(caps.independentInput!=Capability::Available)missing.push_back("independentInput");
    if(caps.seatLifecycle!=Capability::Available)missing.push_back("seatLifecycle");
    if(caps.systemBackend!=Capability::Available)missing.push_back("systemBackend");
    return missing;
}
inline bool canStartSeats(const std::vector<SeatConfig>& config,const Capabilities& caps){
    return std::any_of(config.begin(),config.end(),[](const SeatConfig& seat){return seat.enabled;})&&
        validateConfiguration(config).empty()&&missingOperationalCapabilities(caps).empty();
}
enum class Outcome { Success, Unsupported, Failed };
struct OperationResult {Outcome outcome=Outcome::Unsupported;std::string code,message;};
}
