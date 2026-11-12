#pragma once
#include <Windows.h>
#include <OleAuto.h>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>

namespace seats::lab::child::diagnostics {
// Numeric DISPIDs verified from the installed mstscax.dll type library with
// LoadTypeLibEx(REGKIND_NONE); no control was instantiated or registered.
inline const char* EventName(DISPID id)noexcept {
    switch(id) {
    case 1:return "OnConnecting";
    case 2:return "OnConnected";
    case 3:return "OnLoginComplete";
    case 4:return "OnDisconnected";
    case 5:return "OnEnterFullScreenMode";
    case 6:return "OnLeaveFullScreenMode";
    case 7:return "OnChannelReceivedData";
    case 8:return "OnRequestGoFullScreen";
    case 9:return "OnRequestLeaveFullScreen";
    case 10:return "OnFatalError";
    case 11:return "OnWarning";
    case 12:return "OnRemoteDesktopSizeChange";
    case 13:return "OnIdleTimeoutNotification";
    case 14:return "OnRequestContainerMinimize";
    case 15:return "OnConfirmClose";
    case 16:return "OnReceivedTSPublicKey";
    case 17:return "OnAutoReconnecting";
    case 18:return "OnAuthenticationWarningDisplayed";
    case 19:return "OnAuthenticationWarningDismissed";
    case 20:return "OnRemoteProgramResult";
    case 21:return "OnRemoteProgramDisplayed";
    case 22:return "OnLogonError";
    case 23:return "OnFocusReleased";
    case 24:return "OnUserNameAcquired";
    case 26:return "OnMouseInputModeChanged";
    case 28:return "OnServiceMessageReceived";
    case 29:return "OnRemoteWindowDisplayed";
    case 30:return "OnConnectionBarPullDown";
    case 32:return "OnNetworkStatusChanged";
    case 33:return "OnAutoReconnected";
    case 34:return "OnAutoReconnecting2";
    case 35:return "OnDevicesButtonPressed";
    default:return "UnmappedEvent";
    }
}
// No conversion, dereference, COM calls or VariantCopyInd. Strings, arrays,
// records, interface pointers and all BYREF values are represented by type only.
// Scalar numeric values are copied without changing event arguments or decisions.
inline bool WriteNumeric(std::ostream& out,const VARIANT& value) {
    switch(value.vt) {
    case VT_I1:out<<static_cast<int>(value.cVal);return true;
    case VT_UI1:out<<static_cast<unsigned>(value.bVal);return true;
    case VT_I2:out<<value.iVal;return true;
    case VT_UI2:out<<value.uiVal;return true;
    case VT_I4:out<<value.lVal;return true;
    case VT_UI4:out<<value.ulVal;return true;
    case VT_I8:out<<value.llVal;return true;
    case VT_UI8:out<<value.ullVal;return true;
    case VT_INT:out<<value.intVal;return true;
    case VT_UINT:out<<value.uintVal;return true;
    case VT_BOOL:out<<value.boolVal;return true;
    case VT_ERROR:out<<value.scode;return true;
    case VT_R4:if(std::isfinite(value.fltVal)){out<<std::setprecision(std::numeric_limits<float>::max_digits10)<<value.fltVal;return true;}return false;
    case VT_R8:if(std::isfinite(value.dblVal)){out<<std::setprecision(std::numeric_limits<double>::max_digits10)<<value.dblVal;return true;}return false;
    default:return false;
    }
}
inline std::string JsonEvent(DISPID id,const DISPPARAMS* args,ULONGLONG elapsed) {
    std::ostringstream out;out.imbue(std::locale::classic());
    const UINT count=args?args->cArgs:0;
    const bool readable=count==0||(args&&args->rgvarg&&count<=64);
    out<<"{\"schema\":\"retsar.child-session-event.v1\",\"stage\":\"activex-event\","
       <<"\"elapsedMs\":"<<elapsed<<",\"dispid\":"<<id<<",\"eventName\":\""<<EventName(id)
       <<"\",\"argumentCount\":"<<count<<",\"argumentsReadable\":"<<(readable?"true":"false")
       <<",\"argumentOrder\":\"dispatch-reversed\",\"arguments\":[";
    if(readable&&args)for(UINT i=0;i<count;++i) {
        if(i)out<<',';
        out<<"{\"dispatchIndex\":"<<i<<",\"vartype\":"<<args->rgvarg[i].vt<<",\"numeric\":";
        if(!WriteNumeric(out,args->rgvarg[i]))out<<"null";
        out<<'}';
    }
    out<<"],\"eventArgumentsModified\":false}";
    return out.str();
}
struct StateObservation {
    bool seen=false;HRESULT result=E_PENDING;LONG state=-1;
    bool Changed(HRESULT nextResult,LONG nextState)noexcept {
        const bool changed=!seen||nextResult!=result||(SUCCEEDED(nextResult)&&nextState!=state);
        seen=true;result=nextResult;state=nextState;return changed;
    }
};
}

