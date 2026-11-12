#include "../src/lab/child_session_activex.hpp"
#include <cwchar>
#include <iostream>

// Pure fake COM objects only. No CoCreateInstance, OLE apartment, WTS call,
// window, setting change, session or network connection is used by this test.
namespace {
using namespace seats::lab::child::ax;
int passed=0,failed=0;
void check(bool value,const char* label){
    if(value)++passed;else{++failed;std::cerr<<"FAIL "<<label<<'\n';}
}
class FakeSettings final:public ExtendedSettings {
    ULONG refs_=1;
public:
    HRESULT writeResult=S_OK,readResult=S_OK;
    VARTYPE readType=VT_BOOL;
    VARIANT_BOOL readValue=VARIANT_TRUE;
    unsigned reads=0,writes=0;
    bool validWrite=false,validName=true;
    const wchar_t* expectedName=L"ConnectToChildSession";
    bool* cancelDuringRead=nullptr;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(iid!=IID_IUnknown&&iid!=__uuidof(ExtendedSettings))return E_NOINTERFACE;
        *out=static_cast<ExtendedSettings*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release()override{return --refs_;}
    HRESULT STDMETHODCALLTYPE put_Property(BSTR name,VARIANT* value)override{
        ++writes;validName=validName&&name&&std::wcscmp(name,expectedName)==0;
        validWrite=value&&value->vt==VT_BOOL&&value->boolVal==VARIANT_TRUE;
        return writeResult;
    }
    HRESULT STDMETHODCALLTYPE get_Property(BSTR name,VARIANT* out)override{
        ++reads;validName=validName&&name&&std::wcscmp(name,expectedName)==0;
        if(cancelDuringRead)*cancelDuringRead=true;
        if(!out)return E_POINTER;VariantInit(out);
        if(FAILED(readResult))return readResult;
        out->vt=readType;
        if(readType==VT_BOOL)out->boolVal=readValue;
        else if(readType==VT_I4)out->lVal=-1;
        else if(readType==VT_BSTR)out->bstrVal=SysAllocString(L"true");
        return readResult;
    }
};
class FakeClient final:public IDispatch {
    ULONG refs_=1;
public:
    HRESULT lookupResult=S_OK,connectResult=S_OK;
    unsigned lookups=0,connects=0;
    bool methodShapeValid=true,deferred=false;
    static HRESULT STDAPICALLTYPE FillException(EXCEPINFO* info){
        info->scode=E_ACCESSDENIED;info->wCode=0;
        info->bstrDescription=SysAllocString(L"synthetic test error");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(iid!=IID_IUnknown&&iid!=IID_IDispatch)return E_NOINTERFACE;
        *out=static_cast<IDispatch*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release()override{return --refs_;}
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* out)override{if(!out)return E_POINTER;*out=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT,LCID,ITypeInfo** out)override{if(out)*out=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID,LPOLESTR* names,UINT count,LCID,DISPID* out)override{
        ++lookups;
        if(FAILED(lookupResult))return lookupResult;
        if(!names||count!=1||!names[0]||std::wcscmp(names[0],L"Connect")!=0||!out)return DISP_E_UNKNOWNNAME;
        *out=17;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id,REFIID iid,LCID,WORD flags,DISPPARAMS* args,
        VARIANT*,EXCEPINFO* exception,UINT*)override{
        ++connects;
        methodShapeValid=id==17&&iid==IID_NULL&&flags==DISPATCH_METHOD&&args&&
            args->cArgs==0&&args->cNamedArgs==0;
        if(!methodShapeValid)return E_INVALIDARG;
        if(connectResult==DISP_E_EXCEPTION&&exception){
            if(deferred)exception->pfnDeferredFillIn=FillException;
            else exception->scode=E_ACCESSDENIED;
        }
        return connectResult;
    }
};

void verifyTests(){
    check(VerifyChildMode(nullptr,false)==E_POINTER,"null settings rejected");
    {
        FakeSettings settings;
        check(VerifyChildMode(&settings,true)==S_OK,"set plus exact true readback accepted");
        check(settings.writes==1&&settings.reads==1&&settings.validWrite&&settings.validName,
              "property write uses documented name and VT_BOOL true");
    }
    {
        FakeSettings settings;settings.writeResult=E_ACCESSDENIED;
        check(VerifyChildMode(&settings,true)==E_ACCESSDENIED,"write failure preserved");
        check(settings.writes==1&&settings.reads==0,"failed write cannot be rescued by a successful read");
    }
    {
        FakeSettings settings;settings.readResult=E_ABORT;
        check(VerifyChildMode(&settings,true)==E_ABORT,"read failure after write preserved");
    }
    for(const auto type:{VT_I4,VT_BSTR,VT_EMPTY}){
        FakeSettings settings;settings.readType=static_cast<VARTYPE>(type);
        check(FAILED(VerifyChildMode(&settings,false)),"wrong VARIANT type never coerced to true");
        check(settings.writes==0&&settings.reads==1,"verification never writes a property");
    }
    for(const auto value:{VARIANT_FALSE,static_cast<VARIANT_BOOL>(1)}){
        FakeSettings settings;settings.readValue=value;
        check(FAILED(VerifyChildMode(&settings,false)),"false or noncanonical Boolean rejected");
    }
}

void mousePositionPolicyTests(){
    constexpr auto name=L"IgnoreServerGeneratedMouseMoves";
    {
        FakeSettings settings;settings.expectedName=name;
        check(VerifyExtendedTrue(&settings,name,true)==S_OK&&settings.validName&&settings.validWrite&&
              settings.writes==1&&settings.reads==1,"server mouse update policy uses exact property and VT_BOOL true");
    }
    {
        FakeSettings settings;settings.expectedName=name;settings.writeResult=E_INVALIDARG;
        check(VerifyExtendedTrue(&settings,name,true)==E_INVALIDARG&&settings.reads==0,
              "unsupported mouse update policy preserves failure and cannot be rescued by readback");
    }
    {
        FakeSettings settings;settings.expectedName=name;settings.readResult=E_ACCESSDENIED;
        check(VerifyExtendedTrue(&settings,name,true)==E_ACCESSDENIED,
              "failed mouse update policy readback remains a failure");
    }
    for(const auto type:{VT_I4,VT_BSTR,VT_EMPTY}){
        FakeSettings settings;settings.expectedName=name;settings.readType=static_cast<VARTYPE>(type);
        check(FAILED(VerifyExtendedTrue(&settings,name,false))&&settings.writes==0&&settings.validName,
              "mouse update policy verification does not coerce a foreign type");
    }
    {
        FakeSettings settings;settings.expectedName=name;settings.readValue=VARIANT_FALSE;
        check(FAILED(VerifyExtendedTrue(&settings,name,false)),"false mouse update policy cannot admit connection");
    }
}

void connectionTests(){
    for(const auto flags:{0u,1u,2u}){
        FakeSettings settings;FakeClient client;
        const auto result=ConnectChildOnly(&settings,&client,(flags&1)!=0,(flags&2)!=0);
        check(result.propertyResult==E_ACCESSDENIED&&!result.attempted,
              "unconfirmed enable or nondurable intent prevents connection");
        check(settings.reads==0&&client.lookups==0&&client.connects==0,"failed admission performs no COM dispatch");
    }
    {
        FakeClient client;const auto result=ConnectChildOnly(nullptr,&client,true,true);
        check(result.propertyResult==E_POINTER&&!result.attempted&&client.connects==0,"null property interface never connects");
    }
    {
        FakeSettings settings;FakeClient client;bool cancelled=true;
        const auto result=ConnectChildOnly(&settings,&client,true,true,&cancelled);
        check(result.propertyResult==E_ABORT&&!result.attempted&&settings.reads==0&&client.lookups==0,
              "cancellation before verification performs no outgoing call");
    }
    {
        FakeSettings settings;FakeClient client;bool cancelled=false;settings.cancelDuringRead=&cancelled;
        const auto result=ConnectChildOnly(&settings,&client,true,true,&cancelled);
        check(cancelled&&result.propertyResult==E_ABORT&&!result.attempted&&settings.reads==1&&client.lookups==0,
              "reentrant cancellation during readback prevents Connect");
    }
    {
        FakeSettings settings;
        const auto result=ConnectChildOnly(&settings,nullptr,true,true);
        check(result.attempted&&result.connectResult==E_POINTER,"null client dispatch preserves E_POINTER without dereference");
    }
    {
        FakeSettings settings;FakeClient client;settings.readResult=E_ACCESSDENIED;
        const auto result=ConnectChildOnly(&settings,&client,true,true);
        check(result.propertyResult==E_ACCESSDENIED&&!result.attempted&&client.connects==0,
              "property HRESULT failure prevents Connect and stays original");
    }
    for(const auto type:{VT_I4,VT_BSTR,VT_EMPTY}){
        FakeSettings settings;FakeClient client;settings.readType=static_cast<VARTYPE>(type);
        const auto result=ConnectChildOnly(&settings,&client,true,true);
        check(FAILED(result.propertyResult)&&!result.attempted&&client.lookups==0&&client.connects==0,
              "wrong property type prevents even Connect name lookup");
    }
    {
        FakeSettings settings;FakeClient client;
        check(VerifyChildMode(&settings,true)==S_OK,"initial child readback succeeds");
        settings.readValue=VARIANT_FALSE;
        const auto result=ConnectChildOnly(&settings,&client,true,true);
        check(FAILED(result.propertyResult)&&!result.attempted&&client.connects==0,
              "property changed after setup is rechecked and connection refused");
    }
    for(const auto returned:{S_OK,E_INVALIDARG}){
        FakeSettings settings;FakeClient client;client.connectResult=returned;
        const auto result=ConnectChildOnly(&settings,&client,true,true);
        check(result.propertyResult==S_OK&&result.attempted&&result.connectResult==returned,
              "valid gate preserves original Connect result");
        check(settings.reads==1&&settings.writes==0&&client.connects==1&&client.methodShapeValid,
              "valid gate invokes exactly one parameterless Connect without mode fallback");
    }
    {
        FakeSettings settings;FakeClient client;client.lookupResult=DISP_E_UNKNOWNNAME;
        const auto result=ConnectChildOnly(&settings,&client,true,true);
        check(result.attempted&&result.connectResult==DISP_E_UNKNOWNNAME&&client.connects==0,
              "missing Connect method preserves failure without alternatives");
    }
    {
        FakeSettings settings;FakeClient client;client.connectResult=DISP_E_EXCEPTION;client.deferred=true;
        const auto result=ConnectChildOnly(&settings,&client,true,true);
        check(result.connectResult==DISP_E_EXCEPTION&&lastDispatch.result==DISP_E_EXCEPTION,
              "dispatch exception HRESULT retained");
        check(lastDispatch.deferred&&lastDispatch.deferredResult==S_OK&&lastDispatch.exceptionCode==E_ACCESSDENIED,
              "deferred EXCEPINFO retains underlying error before BSTR cleanup");
        check(client.connects==1,"dispatch exception never retries ordinary RDP");
    }
}
}
int main(){
    verifyTests();mousePositionPolicyTests();connectionTests();
    std::cout<<"Child session client gate: "<<passed<<" passed, "<<failed<<" failed; fake interfaces only, no ActiveX activation or Windows session action.\n";
    return failed?1:0;
}
