#pragma once
#include "child_session_client_mode.hpp"
#include "child_session_toggle.hpp"

namespace seats::lab::child::client {
struct PreservedSettingResult {OperationObservation read{};HRESULT result=E_PENDING;bool verified=false;};
// This path deliberately has no setter call. It is valid only for explicitly
// resuming an exact child from an already-enabled baseline. Admission remains
// the caller's independently rechecked session/inventory/mutex responsibility.
inline PreservedSettingResult VerifyPreservedEnabled(const Request& request,const ToggleApi& api,
    bool cancelled=false)noexcept {
    PreservedSettingResult result{};
    if(!request.preserveEnabled||request.mode!=Mode::ResumeChild||!request.target||request.target==MAXDWORD){result.result=E_INVALIDARG;return result;}
    if(cancelled){result.result=HRESULT_FROM_WIN32(ERROR_CANCELLED);return result;}
    if(!api.isEnabled||!api.getError||!api.setError){result.result=E_POINTER;return result;}
    result.read=toggle_detail::Read(api);
    result.result=!result.read.succeeded?HRESULT_FROM_WIN32(result.read.error?result.read.error:ERROR_GEN_FAILURE):
        result.read.value?S_OK:E_UNEXPECTED;
    result.verified=SUCCEEDED(result.result);return result;
}
inline bool NeedsSettingRestore(const Request& request,bool enableAttempted,bool restoreAttempted)noexcept {
    return !request.preserveEnabled&&enableAttempted&&!restoreAttempted;
}
inline bool BaselineSettingVerified(const Request& request,const Snapshot& final,
    bool enableAttempted,bool restoreAttempted,bool restored)noexcept {
    if(!ValidSettingPolicy(request)||!final.enabled.attempted||!final.enabled.succeeded)return false;
    if(request.preserveEnabled)
        return final.enabled.value&&!enableAttempted&&!restoreAttempted;
    return !final.enabled.value&&(!enableAttempted||restored);
}
}
