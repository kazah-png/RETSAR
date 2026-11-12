#pragma once
#include <Windows.h>
#include <wtsapi32.h>
#include <sstream>
#include <locale>
#include <string>

namespace seats::lab::child {
// Read-only API boundary. Merely constructing SystemApi does not query Windows.
// Injected functions allow tests to exercise failures without invoking WTS.
struct Api {
    decltype(&WTSIsChildSessionsEnabled) isEnabled=nullptr;
    decltype(&WTSGetChildSessionId) getChildId=nullptr;
    decltype(&GetCurrentProcessId) getProcessId=nullptr;
    decltype(&ProcessIdToSessionId) getCallerId=nullptr;
    decltype(&WTSGetActiveConsoleSessionId) getConsoleId=nullptr;
    decltype(&GetLastError) getError=nullptr;
    decltype(&SetLastError) setError=nullptr;
};
inline Api SystemApi() noexcept {
    return {&WTSIsChildSessionsEnabled,&WTSGetChildSessionId,&GetCurrentProcessId,
        &ProcessIdToSessionId,&WTSGetActiveConsoleSessionId,&GetLastError,&SetLastError};
}
struct BoolObservation {
    bool attempted=false;
    bool succeeded=false;
    DWORD error=ERROR_SUCCESS; // Captured immediately on failure; may remain zero.
    bool value=false;          // Meaningful only when succeeded is true.
};
struct IdObservation {
    bool attempted=false;
    bool succeeded=false;
    DWORD error=ERROR_SUCCESS;
    DWORD value=MAXDWORD;      // Discarded/unknown on failed calls.
};
struct ConsoleObservation {
    bool attempted=false;
    DWORD value=MAXDWORD;
    // This API has no documented GetLastError contract. MAXDWORD means that no
    // console session is attached, including temporary attach/detach transitions.
};
struct Snapshot {
    bool apiAvailable=false;
    DWORD processId=0;
    BoolObservation enabled{};
    IdObservation caller{};
    IdObservation child{};
    ConsoleObservation console{};
};
enum class ChildState { Unknown, Absent, IdObserved };
inline ChildState ClassifyChild(const Snapshot& value) noexcept {
    if(!value.child.attempted||!value.child.succeeded)return ChildState::Unknown;
    return value.child.value==MAXDWORD?ChildState::Absent:ChildState::IdObserved;
}
inline const char* ChildStateName(ChildState state) noexcept {
    switch(state) {
    case ChildState::Unknown:return "unknown";
    case ChildState::Absent:return "absent";
    case ChildState::IdObserved:return "id-observed";
    }
    return "unknown";
}

// Only a successful WTSGetChildSessionId returning (ULONG)-1 establishes absence.
// A returned numeric ID does not establish ownership, connection, usability,
// creation by RETSAR, a distinct account, or physical display/input isolation.
// https://learn.microsoft.com/windows/win32/api/wtsapi32/nf-wtsapi32-wtsgetchildsessionid
inline Snapshot Query(const Api& api) noexcept {
    Snapshot result{};
    result.apiAvailable=api.isEnabled&&api.getChildId&&api.getProcessId&&
        api.getCallerId&&api.getConsoleId&&api.getError&&api.setError;
    if(!result.apiAvailable)return result;
    const DWORD previousError=api.getError();
    result.processId=api.getProcessId();

    BOOL enabled=FALSE;
    result.enabled.attempted=true;
    api.setError(ERROR_SUCCESS);
    result.enabled.succeeded=api.isEnabled(&enabled)!=FALSE;
    if(result.enabled.succeeded)result.enabled.value=enabled!=FALSE;
    else result.enabled.error=api.getError();

    ULONG child=MAXDWORD;
    result.child.attempted=true;
    api.setError(ERROR_SUCCESS);
    result.child.succeeded=api.getChildId(&child)!=FALSE;
    if(result.child.succeeded)result.child.value=child;
    else result.child.error=api.getError();

    DWORD caller=MAXDWORD;
    result.caller.attempted=true;
    api.setError(ERROR_SUCCESS);
    result.caller.succeeded=api.getCallerId(result.processId,&caller)!=FALSE;
    if(result.caller.succeeded)result.caller.value=caller;
    else result.caller.error=api.getError();

    result.console.attempted=true;
    result.console.value=api.getConsoleId();
    api.setError(previousError);
    return result;
}
namespace detail {
inline const char* Boolean(bool value) noexcept {return value?"true":"false";}
inline void Error(std::ostringstream& out,bool attempted,bool succeeded,DWORD error) {
    if(attempted&&!succeeded)out<<error;
    else out<<"null";
}
inline void Id(std::ostringstream& out,const IdObservation& value) {
    out<<"{\"attempted\":"<<Boolean(value.attempted)
       <<",\"succeeded\":"<<Boolean(value.succeeded)<<",\"lastError\":";
    Error(out,value.attempted,value.succeeded,value.error);
    out<<",\"sessionId\":";
    if(value.succeeded&&value.attempted)out<<value.value;else out<<"null";
    out<<"}";
}
}
// Contains only fixed strings, Boolean values and numeric observations.
// No user name, credential, token content, device inventory or file path.
inline std::string ToJson(const Snapshot& value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out<<"{\"schema\":\"retsar.child-session-status.v1\","
       <<"\"mode\":\"read-only-child-session-status\","
       <<"\"apiAvailable\":"<<detail::Boolean(value.apiAvailable)
       <<",\"processId\":";
    if(value.apiAvailable)out<<value.processId;else out<<"null";
    out<<",\"enabled\":{\"attempted\":"<<detail::Boolean(value.enabled.attempted)
       <<",\"succeeded\":"<<detail::Boolean(value.enabled.succeeded)<<",\"lastError\":";
    detail::Error(out,value.enabled.attempted,value.enabled.succeeded,value.enabled.error);
    out<<",\"value\":";
    if(value.enabled.attempted&&value.enabled.succeeded)out<<detail::Boolean(value.enabled.value);
    else out<<"null";
    out<<"},\"caller\":";
    detail::Id(out,value.caller);
    out<<",\"child\":";
    detail::Id(out,value.child);
    out<<",\"childState\":\""<<ChildStateName(ClassifyChild(value))<<"\","
       <<"\"console\":{\"attempted\":"<<detail::Boolean(value.console.attempted)<<",\"rawSessionId\":";
    if(value.console.attempted)out<<value.console.value;else out<<"null";
    out<<",\"attached\":";
    if(value.console.attempted)out<<detail::Boolean(value.console.value!=MAXDWORD);
    else out<<"null";
    out<<"},\"createdSession\":false,\"ownershipProven\":false,"
       <<"\"physicalSeatVerified\":false,\"windowsHomeSupport\":\"NOT_VERIFIED\"}";
    return out.str();
}
}
