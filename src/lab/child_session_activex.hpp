#pragma once
#include <Windows.h>
#include <Ole2.h>
#include <OCIdl.h>
#include <olectl.h>
#include <atomic>

namespace seats::lab::child::ax {
inline constexpr GUID ClientClass={0xa0c63c30,0xf08d,0x4ab4,{0x90,0x7c,0x34,0x90,0x5d,0x77,0x0c,0x7d}};
inline constexpr GUID ClientEvents={0x336d5562,0xefa8,0x482e,{0x8c,0xb3,0xc5,0xc0,0xfc,0x7a,0x7d,0xb6}};
inline constexpr UINT FrameSyncMessage=WM_APP+43;
inline bool KeyboardMessage(UINT message)noexcept{return message>=WM_KEYFIRST&&message<=WM_KEYLAST;}
inline HRESULT NotifyFrameWindow(IOleInPlaceActiveObject* object,BOOL active,unsigned& outboundDepth)noexcept {
    if(!object)return E_POINTER;
    if(outboundDepth)return E_PENDING;
    ++outboundDepth;object->AddRef();
    const HRESULT result=object->OnFrameWindowActivate(active);
    object->Release();--outboundDepth;return result;
}
// Original declaration of the documented ABI; verified against MsTscAx's type library.
// https://learn.microsoft.com/windows/win32/termserv/imsrdpextendedsettings-property
struct __declspec(uuid("302d8188-0052-4807-806a-362b628f9ac5")) ExtendedSettings: IUnknown {
    virtual HRESULT STDMETHODCALLTYPE put_Property(BSTR,VARIANT*)=0;
    virtual HRESULT STDMETHODCALLTYPE get_Property(BSTR,VARIANT*)=0;
};
template<class T> struct Com {
    T* value=nullptr;
    Com()=default;Com(const Com&)=delete;Com& operator=(const Com&)=delete;
    ~Com(){reset();}
    void reset()noexcept{if(value){auto old=value;value=nullptr;old->Release();}}
    T** put()noexcept{reset();return &value;}
    T* operator->()const noexcept{return value;}
    explicit operator bool()const noexcept{return value!=nullptr;}
};
struct Variant {
    VARIANT value{};
    Variant(){VariantInit(&value);}
    ~Variant(){VariantClear(&value);}
    Variant(const Variant&)=delete;Variant& operator=(const Variant&)=delete;
};
struct DispatchDiagnostic {unsigned long long serial=0;HRESULT result=S_OK,exceptionCode=S_OK,deferredResult=S_OK;WORD exceptionWord=0;bool deferred=false;};
inline thread_local DispatchDiagnostic lastDispatch{};
inline HRESULT Invoke(IDispatch* object,const wchar_t* name,WORD flags,VARIANT* argument=nullptr,VARIANT* result=nullptr)noexcept {
    const auto serial=lastDispatch.serial+1;lastDispatch={};lastDispatch.serial=serial;
    if(!object)return lastDispatch.result=E_POINTER;
    DISPID id=DISPID_UNKNOWN;auto mutableName=const_cast<wchar_t*>(name);
    HRESULT hr=object->GetIDsOfNames(IID_NULL,&mutableName,1,LOCALE_USER_DEFAULT,&id);
    if(FAILED(hr))return lastDispatch.result=hr;
    DISPID named=DISPID_PROPERTYPUT;DISPPARAMS parameters{};
    if(argument){parameters.rgvarg=argument;parameters.cArgs=1;}
    if(flags&DISPATCH_PROPERTYPUT){parameters.rgdispidNamedArgs=&named;parameters.cNamedArgs=1;}
    EXCEPINFO exception{};UINT argumentError=0;
    hr=object->Invoke(id,IID_NULL,LOCALE_USER_DEFAULT,flags,&parameters,result,&exception,&argumentError);
    if(hr==DISP_E_EXCEPTION&&exception.pfnDeferredFillIn) {
        lastDispatch.deferred=true;lastDispatch.deferredResult=exception.pfnDeferredFillIn(&exception);
    }
    lastDispatch.result=hr;lastDispatch.exceptionCode=exception.scode;lastDispatch.exceptionWord=exception.wCode;
    SysFreeString(exception.bstrSource);SysFreeString(exception.bstrDescription);SysFreeString(exception.bstrHelpFile);
    return hr;
}
inline HRESULT PutNumber(IDispatch* object,const wchar_t* name,LONG value,bool boolean=false)noexcept {
    VARIANT variant{};variant.vt=static_cast<VARTYPE>(boolean?VT_BOOL:VT_I4);
    if(boolean)variant.boolVal=value?VARIANT_TRUE:VARIANT_FALSE;else variant.lVal=value;
    return Invoke(object,name,DISPATCH_PROPERTYPUT,&variant);
}
inline HRESULT PutText(IDispatch* object,const wchar_t* name,const wchar_t* value)noexcept {
    Variant variant;variant.value.vt=VT_BSTR;variant.value.bstrVal=SysAllocString(value);
    if(!variant.value.bstrVal)return E_OUTOFMEMORY;
    return Invoke(object,name,DISPATCH_PROPERTYPUT,&variant.value);
}
inline HRESULT GetNumber(IDispatch* object,const wchar_t* name,LONG& value)noexcept {
    Variant variant;HRESULT hr=Invoke(object,name,DISPATCH_PROPERTYGET,nullptr,&variant.value);
    if(FAILED(hr))return hr;
    if(variant.value.vt==VT_I2)value=variant.value.iVal;
    else if(variant.value.vt==VT_I4)value=variant.value.lVal;
    else return DISP_E_TYPEMISMATCH;
    return S_OK;
}
inline HRESULT VerifyExtendedTrue(ExtendedSettings* settings,const wchar_t* property,bool write)noexcept {
    if(!settings||!property)return E_POINTER;
    BSTR name=SysAllocString(property);
    if(!name)return E_OUTOFMEMORY;
    VARIANT requested{};requested.vt=VT_BOOL;requested.boolVal=VARIANT_TRUE;
    HRESULT hr=write?settings->put_Property(name,&requested):S_OK;
    if(SUCCEEDED(hr)) {
        Variant read;hr=settings->get_Property(name,&read.value);
        if(SUCCEEDED(hr)&&(read.value.vt!=VT_BOOL||read.value.boolVal!=VARIANT_TRUE))hr=E_UNEXPECTED;
    }
    SysFreeString(name);return hr;
}
inline HRESULT VerifyChildMode(ExtendedSettings* settings,bool write)noexcept {
    return VerifyExtendedTrue(settings,L"ConnectToChildSession",write);
}
struct ConnectResult {HRESULT propertyResult=E_PENDING,connectResult=E_PENDING;bool attempted=false;};
inline ConnectResult ConnectChildOnly(ExtendedSettings* settings,IDispatch* client,
    bool enabledVerified,bool intentDurable,const bool* cancelled=nullptr)noexcept {
    ConnectResult result{};
    if(!enabledVerified||!intentDurable){result.propertyResult=E_ACCESSDENIED;return result;}
    if(cancelled&&*cancelled){result.propertyResult=E_ABORT;return result;}
    result.propertyResult=VerifyChildMode(settings,false);
    if(FAILED(result.propertyResult))return result;
    if(cancelled&&*cancelled){result.propertyResult=E_ABORT;return result;}
    result.attempted=true;
    result.connectResult=Invoke(client,L"Connect",DISPATCH_METHOD);
    return result;
}
// The owning App outlives every reference from its control. Reference counts do
// not delete the stack-owned site during reentrant COM callbacks.
class Site final:public IOleClientSite,public IOleInPlaceSite,public IOleInPlaceFrame,
                 public IOleControlSite,public IDispatch {
    std::atomic<ULONG> refs_{1};
public:
    HWND window=nullptr;
    LONG headerHeight=88;
    IOleInPlaceObject* inplace=nullptr;
    unsigned* outboundDepth=nullptr;
    const bool* closing=nullptr;
    bool changingRect=false;
    Com<IOleInPlaceActiveObject> active;
    RECT Area()const noexcept{RECT r{};GetClientRect(window,&r);r.top=(headerHeight<r.bottom?headerHeight:r.bottom);return r;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override {
        if(!out)return E_POINTER;*out=nullptr;
        if(iid==IID_IUnknown||iid==IID_IOleClientSite)*out=static_cast<IOleClientSite*>(this);
        else if(iid==IID_IOleWindow||iid==IID_IOleInPlaceSite)*out=static_cast<IOleInPlaceSite*>(this);
        else if(iid==IID_IOleInPlaceUIWindow||iid==IID_IOleInPlaceFrame)*out=static_cast<IOleInPlaceFrame*>(this);
        else if(iid==IID_IOleControlSite)*out=static_cast<IOleControlSite*>(this);
        else if(iid==IID_IDispatch)*out=static_cast<IDispatch*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release()override{return --refs_;}
    HRESULT STDMETHODCALLTYPE SaveObject()override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetMoniker(DWORD,DWORD,IMoniker** out)override{if(out)*out=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetContainer(IOleContainer** out)override{if(!out)return E_POINTER;*out=nullptr;return E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE ShowObject()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnShowWindow(BOOL)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE RequestNewObjectLayout()override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetWindow(HWND* out)override{if(!out)return E_POINTER;*out=window;return S_OK;}
    HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CanInPlaceActivate()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnInPlaceActivate()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnUIActivate()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE GetWindowContext(IOleInPlaceFrame** frame,IOleInPlaceUIWindow** document,
        LPRECT position,LPRECT clipping,LPOLEINPLACEFRAMEINFO info)override {
        if(!frame||!document||!position||!clipping||!info)return E_POINTER;
        *frame=static_cast<IOleInPlaceFrame*>(this);AddRef();*document=nullptr;
        *position=Area();*clipping=*position;
        info->cb=sizeof(*info);info->fMDIApp=FALSE;info->hwndFrame=window;info->haccel=nullptr;info->cAccelEntries=0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Scroll(SIZE)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE OnUIDeactivate(BOOL)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnInPlaceDeactivate()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE DiscardUndoState()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE DeactivateAndUndo()override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE OnPosRectChange(LPCRECT rect)override {
        if(!inplace||changingRect||(closing&&*closing))return S_OK;
        changingRect=true;if(outboundDepth)++*outboundDepth;
        const HRESULT result=inplace->SetObjectRects(rect,rect);
        if(outboundDepth)--*outboundDepth;changingRect=false;return result;
    }
    HRESULT STDMETHODCALLTYPE GetBorder(LPRECT)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE RequestBorderSpace(LPCBORDERWIDTHS)override{return INPLACE_E_NOTOOLSPACE;}
    HRESULT STDMETHODCALLTYPE SetBorderSpace(LPCBORDERWIDTHS)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE SetActiveObject(IOleInPlaceActiveObject* object,LPCOLESTR)override {
        if(object)object->AddRef();active.reset();active.value=object;
        if(window)PostMessageW(window,FrameSyncMessage,0,0);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE InsertMenus(HMENU,LPOLEMENUGROUPWIDTHS)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetMenu(HMENU,HOLEMENU,HWND)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE RemoveMenus(HMENU)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE SetStatusText(LPCOLESTR)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE EnableModeless(BOOL)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG,WORD)override{return S_FALSE;}
    HRESULT STDMETHODCALLTYPE OnControlInfoChanged()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE LockInPlaceActive(BOOL)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE GetExtendedControl(IDispatch** out)override{if(out)*out=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE TransformCoords(POINTL*,POINTF*,DWORD)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG,DWORD)override{return S_FALSE;}
    HRESULT STDMETHODCALLTYPE OnFocus(BOOL)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE ShowPropertyFrame()override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* out)override{if(!out)return E_POINTER;*out=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT,LCID,ITypeInfo** out)override{if(out)*out=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID,LPOLESTR*,UINT,LCID,DISPID*)override{return DISP_E_UNKNOWNNAME;}
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id,REFIID,LCID,WORD flags,DISPPARAMS*,VARIANT* out,EXCEPINFO*,UINT*)override {
        if(!(flags&DISPATCH_PROPERTYGET)||!out)return DISP_E_MEMBERNOTFOUND;
        VariantInit(out);
        if(id==DISPID_AMBIENT_USERMODE||id==DISPID_AMBIENT_SUPPORTSMNEMONICS) {
            out->vt=VT_BOOL;out->boolVal=VARIANT_TRUE;return S_OK;
        }
        if(id==DISPID_AMBIENT_UIDEAD||id==DISPID_AMBIENT_SHOWHATCHING||id==DISPID_AMBIENT_SHOWGRABHANDLES) {
            out->vt=VT_BOOL;out->boolVal=VARIANT_FALSE;return S_OK;
        }
        if(id==DISPID_AMBIENT_BACKCOLOR||id==DISPID_AMBIENT_FORECOLOR) {
            out->vt=VT_I4;out->lVal=id==DISPID_AMBIENT_BACKCOLOR?RGB(18,24,35):RGB(235,240,245);return S_OK;
        }
        if(id==DISPID_AMBIENT_LOCALEID){out->vt=VT_I4;out->lVal=static_cast<LONG>(GetUserDefaultLCID());return S_OK;}
        return DISP_E_MEMBERNOTFOUND;
    }
};
class Events final:public IDispatch {
    std::atomic<ULONG> refs_{1};
public:
    void* context=nullptr;
    void (*callback)(void*,DISPID,DISPPARAMS*)noexcept=nullptr;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override {
        if(!out)return E_POINTER;*out=nullptr;
        if(iid!=IID_IUnknown&&iid!=IID_IDispatch&&iid!=ClientEvents)return E_NOINTERFACE;
        *out=static_cast<IDispatch*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release()override{return --refs_;}
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* out)override{if(!out)return E_POINTER;*out=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT,LCID,ITypeInfo** out)override{if(out)*out=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID,LPOLESTR*,UINT,LCID,DISPID*)override{return DISP_E_UNKNOWNNAME;}
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id,REFIID,LCID,WORD,DISPPARAMS* args,VARIANT*,EXCEPINFO*,UINT*)override {
        if(callback)callback(context,id,args);return S_OK;
    }
};
}

