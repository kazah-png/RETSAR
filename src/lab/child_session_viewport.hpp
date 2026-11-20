#pragma once
#include "child_session_activex.hpp"

namespace seats::lab::child::viewport {
struct Result {
    HRESULT result=E_PENDING;
    bool known=false,fit=false;
};
// Client-side rendering only. SmartSizing can change while connected and does
// not change desktop resolution, window placement, full-screen mode or input.
// https://learn.microsoft.com/windows/win32/termserv/imsrdpclientadvancedsettings-smartsizing
inline Result SetFit(IDispatch* client,bool requested,unsigned& outboundDepth)noexcept {
    Result out;
    if(!client){out.result=E_POINTER;return out;}
    if(outboundDepth)return out;
    struct Held {
        IDispatch* object;unsigned& depth;
        Held(IDispatch* value,unsigned& count):object(value),depth(count){++depth;object->AddRef();}
        ~Held(){object->Release();--depth;}
    } held(client,outboundDepth);
    ax::Variant advanced;
    out.result=ax::Invoke(client,L"AdvancedSettings9",DISPATCH_PROPERTYGET,nullptr,&advanced.value);
    if(FAILED(out.result))return out;
    if(advanced.value.vt!=VT_DISPATCH||!advanced.value.pdispVal){out.result=DISP_E_TYPEMISMATCH;return out;}
    const HRESULT written=ax::PutNumber(advanced.value.pdispVal,L"SmartSizing",requested?1:0,true);
    ax::Variant actual;
    const HRESULT read=ax::Invoke(advanced.value.pdispVal,L"SmartSizing",DISPATCH_PROPERTYGET,nullptr,&actual.value);
    HRESULT observed=read;
    if(SUCCEEDED(read)) {
        if(actual.value.vt!=VT_BOOL||(actual.value.boolVal!=VARIANT_TRUE&&actual.value.boolVal!=VARIANT_FALSE))
            observed=DISP_E_TYPEMISMATCH;
        else {out.known=true;out.fit=actual.value.boolVal==VARIANT_TRUE;}
    }
    // A failed write is not called success even if readback happens to match.
    out.result=FAILED(written)?written:FAILED(observed)?observed:
        out.fit==requested?S_OK:E_UNEXPECTED;
    return out;
}
}
