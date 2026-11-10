#pragma once
#include "model.hpp"
#include "json.hpp"

namespace seats {
class Backend {
public:
    virtual ~Backend()=default;
    virtual const char* backendName()const noexcept=0;
    virtual Capabilities capabilities()const noexcept=0;
    virtual Json inspect()const=0;
    virtual OperationResult StartSeat(const SeatConfig& seat)const=0;
};
inline Json capabilityJson(const Capabilities& c){return Json::Object{
    {"inventory",name(c.inventory)},{"assignOutputs",name(c.assignOutputs)},
    {"sessionLifecycle",name(c.sessionLifecycle)},{"independentInput",name(c.independentInput)},
    {"seatLifecycle",name(c.seatLifecycle)},{"systemBackend",name(c.systemBackend)}};}
class WindowsInventoryBackend final:public Backend {
public:
    const char* backendName()const noexcept override{return "WindowsInventoryBackend";}
    Capabilities capabilities()const noexcept override{return {};}
    Json inspect()const override;
    OperationResult StartSeat(const SeatConfig&)const override{
        return {Outcome::Unsupported,"ENGINE_NOT_IMPLEMENTED","This backend only inspects Windows. It cannot start or assign a seat."};
    }
};
}
