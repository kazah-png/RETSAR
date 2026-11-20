#pragma once
#include "desktop_model.hpp"
#include <cstdint>

namespace retsar::desktop::launch {
constexpr std::uint64_t ObservationLimitMs=90000;
enum class Result { Waiting, Connected, Exited, Unavailable, TimedOut };
struct Evidence {
    bool processAlive=false, processKnown=false, fresh=false, exactViewer=false;
    std::uint64_t elapsedMs=0;
    DWORD parent=MAXDWORD, requestedChild=MAXDWORD;
};
// A viewer window alone is not a successful connection. This is a bounded
// observation of the requested launch, not proof of session ownership.
inline Result Evaluate(const Evidence& evidence,const Facts& facts)noexcept {
    if(!evidence.processKnown)return Result::Unavailable;
    if(!evidence.processAlive)return Result::Exited;
    if(evidence.elapsedMs>=ObservationLimitMs)return Result::TimedOut;
    if(!evidence.fresh||!evidence.exactViewer||!facts.verifiedViewerPresent||!facts.inventoryComplete)return Result::Waiting;
    const auto& s=facts.session;
    if(!s.apiAvailable||!s.enabled.attempted||!s.enabled.succeeded||!s.enabled.value||!s.caller.attempted||!s.caller.succeeded||
       !s.console.attempted||!s.child.attempted||!s.child.succeeded||!evidence.parent||evidence.parent==MAXDWORD||s.caller.value!=evidence.parent||
       s.console.value!=evidence.parent||s.child.value==MAXDWORD||!s.child.value||
       s.child.value==evidence.parent)return Result::Waiting;
    if(evidence.requestedChild!=MAXDWORD&&s.child.value!=evidence.requestedChild)return Result::Waiting;
    unsigned parents=0,children=0;
    for(const auto& row:facts.rows){
        if(row.id==evidence.parent){if(row.state!=WTSActive||!row.hasUser)return Result::Waiting;++parents;}
        if(row.id==s.child.value){if(row.state!=WTSActive||!row.hasUser)return Result::Waiting;++children;}
    }
    return parents==1&&children==1?Result::Connected:Result::Waiting;
}
}
