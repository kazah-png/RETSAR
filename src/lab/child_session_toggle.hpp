#pragma once
#include "child_session_status.hpp"
#include <array>
#include <cstddef>

namespace seats::lab::child {
// This boundary never connects a client or creates a session. Constructing the
// system boundary only obtains function pointers; RunToggle performs the probe.
struct ToggleApi {
    decltype(&WTSEnableChildSessions) setEnabled=nullptr;
    decltype(&WTSIsChildSessionsEnabled) isEnabled=nullptr;
    decltype(&GetLastError) getError=nullptr;
    decltype(&SetLastError) setError=nullptr;
};
inline ToggleApi SystemToggleApi() noexcept {
    return {&WTSEnableChildSessions,&WTSIsChildSessionsEnabled,&GetLastError,&SetLastError};
}
struct TogglePreconditions {
    // The caller must finish its inventory/ownership guard and serialize against
    // other owners before setting this. This flag does not enforce isolation.
    bool guardComplete=false;
    BoolObservation baseline{};
};
struct OperationObservation {
    bool attempted=false;
    bool succeeded=false;
    bool exceptionThrown=false;
    BOOL returnValue=FALSE; // Exact returned BOOL, only if attempted and not thrown.
    DWORD error=ERROR_SUCCESS; // Raw failure LastError, including zero.
    bool value=false; // Successful readback or successful requested setter value.
};
struct PersistenceResult {
    bool succeeded=false;
    DWORD error=ERROR_SUCCESS;
};
struct PersistenceObservation {
    bool attempted=false;
    bool completed=false;
    bool succeeded=false;
    bool exceptionThrown=false;
    DWORD error=ERROR_SUCCESS;
};
enum class TogglePhase { Refused, Intent, AfterEnable, Restored };
enum class ToggleRefusal {
    None, ApiUnavailable, GuardIncomplete, BaselineUnknown, BaselineEnabled,
    PersistenceUnavailable, IntentNotDurable
};
inline const char* TogglePhaseName(TogglePhase value) noexcept {
    switch(value) {
    case TogglePhase::Refused:return "refused";
    case TogglePhase::Intent:return "intent";
    case TogglePhase::AfterEnable:return "after-enable";
    case TogglePhase::Restored:return "restored";
    }
    return "refused";
}
inline const char* ToggleRefusalName(ToggleRefusal value) noexcept {
    switch(value) {
    case ToggleRefusal::None:return "none";
    case ToggleRefusal::ApiUnavailable:return "api-unavailable";
    case ToggleRefusal::GuardIncomplete:return "guard-incomplete";
    case ToggleRefusal::BaselineUnknown:return "baseline-unknown";
    case ToggleRefusal::BaselineEnabled:return "baseline-enabled";
    case ToggleRefusal::PersistenceUnavailable:return "persistence-unavailable";
    case ToggleRefusal::IntentNotDurable:return "intent-not-durable";
    }
    return "unknown";
}
struct ToggleReport {
    TogglePhase phase=TogglePhase::Refused;
    ToggleRefusal refusal=ToggleRefusal::None;
    bool apiAvailable=false;
    bool guardComplete=false;
    BoolObservation baseline{};
    OperationObservation enable{},enabledAfter{},restore{},restoredAfter{};
    // Index 0: intent; 1: after enable/readback; 2: after restore/readback.
    // During a callback its own slot is attempted but not yet completed.
    std::array<PersistenceObservation,3> persistence{};
    bool enableConfirmed=false;
    bool safetyRestored=false;
    // An in-memory acknowledgement after all writes return success. A receipt
    // cannot attest to completion of the callback currently writing that receipt.
    bool durableComplete=false;
};
using PersistToggle=PersistenceResult (*)(const ToggleReport&,void*);
namespace toggle_detail {
inline OperationObservation Set(const ToggleApi& api,BOOL requested) noexcept {
    OperationObservation result{};
    result.attempted=true;
    try {
        api.setError(ERROR_SUCCESS);
        result.returnValue=api.setEnabled(requested);
        result.succeeded=result.returnValue!=FALSE;
        if(result.succeeded)result.value=requested!=FALSE;
        else result.error=api.getError();
    } catch(...) {
        result.exceptionThrown=true;
        result.succeeded=false;
    }
    return result;
}
inline OperationObservation Read(const ToggleApi& api) noexcept {
    OperationObservation result{};
    result.attempted=true;
    BOOL output=FALSE;
    try {
        api.setError(ERROR_SUCCESS);
        result.returnValue=api.isEnabled(&output);
        result.succeeded=result.returnValue!=FALSE;
        if(result.succeeded)result.value=output!=FALSE;
        else result.error=api.getError();
    } catch(...) {
        result.exceptionThrown=true;
        result.succeeded=false;
    }
    return result;
}
inline void Persist(ToggleReport& report,std::size_t index,
                    PersistToggle callback,void* context) noexcept {
    auto& observation=report.persistence[index];
    observation.attempted=true;
    try {
        const auto result=callback(report,context);
        observation.succeeded=result.succeeded;
        observation.error=result.error;
    } catch(...) {
        observation.exceptionThrown=true;
        observation.error=ERROR_UNHANDLED_EXCEPTION;
    }
    observation.completed=true;
}
inline void Operation(std::ostringstream& out,const OperationObservation& value) {
    out<<"{\"attempted\":"<<detail::Boolean(value.attempted)
       <<",\"succeeded\":"<<detail::Boolean(value.succeeded)
       <<",\"exceptionThrown\":"<<detail::Boolean(value.exceptionThrown)
       <<",\"returnValue\":";
    if(value.attempted&&!value.exceptionThrown)out<<value.returnValue;else out<<"null";
    out<<",\"lastError\":";
    if(value.attempted&&!value.succeeded&&!value.exceptionThrown)out<<value.error;
    else out<<"null";
    out<<",\"value\":";
    if(value.attempted&&value.succeeded)out<<detail::Boolean(value.value);else out<<"null";
    out<<"}";
}
}

// Precondition: the externally checked baseline is strictly disabled. Persist
// intent before the first setter. After any enable attempt, attempt exactly one
// disable and its readback, even if enable, its readback or persistence failed.
// "Restored" names a completed rollback attempt, not a claim of restoration.
// Only a successful disable AND successful disabled readback prove restoration
// within this probe. No retry, connection, logoff or session creation occurs.
// https://learn.microsoft.com/windows/win32/api/wtsapi32/nf-wtsapi32-wtsenablechildsessions
inline ToggleReport RunToggle(const ToggleApi& api,const TogglePreconditions& preconditions,
                              PersistToggle callback,void* context) noexcept {
    ToggleReport report{};
    report.apiAvailable=api.setEnabled&&api.isEnabled&&api.getError&&api.setError;
    report.guardComplete=preconditions.guardComplete;
    report.baseline=preconditions.baseline;
    if(!report.apiAvailable)report.refusal=ToggleRefusal::ApiUnavailable;
    else if(!report.guardComplete)report.refusal=ToggleRefusal::GuardIncomplete;
    else if(!report.baseline.attempted||!report.baseline.succeeded)
        report.refusal=ToggleRefusal::BaselineUnknown;
    else if(report.baseline.value)report.refusal=ToggleRefusal::BaselineEnabled;
    else if(!callback)report.refusal=ToggleRefusal::PersistenceUnavailable;
    if(report.refusal!=ToggleRefusal::None)return report;

    const DWORD previousError=api.getError();
    report.phase=TogglePhase::Intent;
    toggle_detail::Persist(report,0,callback,context);
    if(!report.persistence[0].succeeded) {
        report.refusal=ToggleRefusal::IntentNotDurable;
        api.setError(previousError);
        return report;
    }
    report.enable=toggle_detail::Set(api,TRUE);
    report.enabledAfter=toggle_detail::Read(api);
    report.enableConfirmed=report.enable.succeeded&&report.enabledAfter.succeeded&&
        report.enabledAfter.value;
    report.phase=TogglePhase::AfterEnable;
    toggle_detail::Persist(report,1,callback,context);

    report.restore=toggle_detail::Set(api,FALSE);
    report.restoredAfter=toggle_detail::Read(api);
    report.safetyRestored=report.restore.succeeded&&report.restoredAfter.succeeded&&
        !report.restoredAfter.value;
    report.phase=TogglePhase::Restored;
    toggle_detail::Persist(report,2,callback,context);
    report.durableComplete=report.persistence[0].succeeded&&
        report.persistence[1].succeeded&&report.persistence[2].succeeded;
    api.setError(previousError);
    return report;
}

// Contains only fixed strings and Boolean/numeric observations. No credentials,
// account names, file paths or device inventory. Failed API outputs stay null.
inline std::string ToJson(const ToggleReport& value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out<<"{\"schema\":\"retsar.child-session-toggle.v1\","
       <<"\"mode\":\"enable-then-restore-without-connection\","
       <<"\"phase\":\""<<TogglePhaseName(value.phase)<<"\","
       <<"\"refusal\":\""<<ToggleRefusalName(value.refusal)<<"\","
       <<"\"apiAvailable\":"<<detail::Boolean(value.apiAvailable)
       <<",\"guardComplete\":"<<detail::Boolean(value.guardComplete)
       <<",\"baseline\":{\"attempted\":"<<detail::Boolean(value.baseline.attempted)
       <<",\"succeeded\":"<<detail::Boolean(value.baseline.succeeded)<<",\"lastError\":";
    detail::Error(out,value.baseline.attempted,value.baseline.succeeded,value.baseline.error);
    out<<",\"value\":";
    if(value.baseline.attempted&&value.baseline.succeeded)out<<detail::Boolean(value.baseline.value);
    else out<<"null";
    out<<"},\"enable\":";toggle_detail::Operation(out,value.enable);
    out<<",\"enabledAfter\":";toggle_detail::Operation(out,value.enabledAfter);
    out<<",\"restore\":";toggle_detail::Operation(out,value.restore);
    out<<",\"restoredAfter\":";toggle_detail::Operation(out,value.restoredAfter);
    out<<",\"persistence\":[";
    for(std::size_t i=0;i<value.persistence.size();++i) {
        const auto& observation=value.persistence[i];
        if(i)out<<",";
        out<<"{\"attempted\":"<<detail::Boolean(observation.attempted)
           <<",\"completed\":"<<detail::Boolean(observation.completed)
           <<",\"succeeded\":"<<detail::Boolean(observation.succeeded)
           <<",\"exceptionThrown\":"<<detail::Boolean(observation.exceptionThrown)
           <<",\"error\":";
        if(observation.completed)out<<observation.error;else out<<"null";
        out<<"}";
    }
    out<<"],\"enableConfirmed\":"<<detail::Boolean(value.enableConfirmed)
       <<",\"safetyRestored\":"<<detail::Boolean(value.safetyRestored)
       <<",\"durableComplete\":"<<detail::Boolean(value.durableComplete)
       <<",\"createdSession\":false,\"connectedClient\":false,"
       <<"\"physicalSeatVerified\":false,\"windowsHomeSupport\":\"NOT_VERIFIED\"}";
    return out.str();
}
}