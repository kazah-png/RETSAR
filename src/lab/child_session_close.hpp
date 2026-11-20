#pragma once
#include "child_session_activex.hpp"

namespace seats::lab::child::closure {
// OnDisconnected is accepted only from the subscribed control's DISPID 4
// callback with the documented single LONG argument. The reason is not proof
// of a new session or of logging the existing desktop off.
inline bool DisconnectedEvent(const DISPPARAMS* args)noexcept {
    return args&&args->cArgs==1&&args->cNamedArgs==0&&args->rgvarg&&args->rgvarg[0].vt==VT_I4;
}
struct Reading {HRESULT result=E_PENDING;bool known=false;LONG value=-1;};
inline Reading ReadConnected(IDispatch* object,unsigned& outboundDepth)noexcept {
    Reading out;
    if(!object){out.result=E_POINTER;return out;}
    if(outboundDepth)return out;
    ax::OutboundScope guard(&outboundDepth);object->AddRef();
    {
        ax::Variant value;
        out.result=ax::Invoke(object,L"Connected",DISPATCH_PROPERTYGET,nullptr,&value.value);
        // GetNumber intentionally accepts general success HRESULTs elsewhere.
        // Closure evidence instead requires the property's documented S_OK and
        // an actual integer value; S_FALSE with a zero output is not evidence.
        if(out.result==S_OK){
            if(value.value.vt==VT_I2){out.value=value.value.iVal;out.known=true;}
            else if(value.value.vt==VT_I4){out.value=value.value.lVal;out.known=true;}
            else out.result=DISP_E_TYPEMISMATCH;
        }
    }
    object->Release();return out;
}
} // namespace seats::lab::child::closure
