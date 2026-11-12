#include "../src/lab/child_session_client_diagnostics.hpp"
#include <cstring>
#include <iostream>
#include <vector>

namespace {
using namespace seats::lab::child::diagnostics;
unsigned passed=0,failed=0;
void check(bool result,const char* name){if(result)++passed;else{++failed;std::cerr<<"FAIL "<<name<<'\n';}}
bool Has(const std::string& value,const std::string& token){return value.find(token)!=std::string::npos;}
std::string One(VARIANT& value){DISPPARAMS args{};args.cArgs=1;args.rgvarg=&value;return JsonEvent(22,&args,123);}
void EventTests(){
    const auto empty=JsonEvent(18,nullptr,42);
    check(Has(empty,"\"dispid\":18")&&Has(empty,"\"eventName\":\"OnAuthenticationWarningDisplayed\"")&&
          Has(empty,"\"elapsedMs\":42"),"authentication event identity and elapsed time retained");
    check(Has(empty,"\"argumentCount\":0")&&Has(empty,"\"arguments\":[]"),"no-argument event accepted");
    check(std::strcmp(EventName(19),"OnAuthenticationWarningDismissed")==0&&
          std::strcmp(EventName(22),"OnLogonError")==0,"authentication dismissal and logon errors remain distinguishable");
    check(std::strcmp(EventName(16),"OnReceivedTSPublicKey")==0,"public-key event can be named without exposing its payload");
    check(Has(JsonEvent(-77,nullptr,0),"\"eventName\":\"UnmappedEvent\"")&&
          Has(JsonEvent(-77,nullptr,0),"\"dispid\":-77"),"unknown event preserves raw DISPID");
    VARIANT pair[2]{};pair[0].vt=VT_I4;pair[0].lVal=17;pair[1].vt=VT_I4;pair[1].lVal=23;
    DISPPARAMS args{};args.cArgs=2;args.rgvarg=pair;
    const auto ordered=JsonEvent(32,&args,0);
    check(Has(ordered,"\"argumentOrder\":\"dispatch-reversed\"")&&
          Has(ordered,"{\"dispatchIndex\":0,\"vartype\":3,\"numeric\":17}")&&
          Has(ordered,"{\"dispatchIndex\":1,\"vartype\":3,\"numeric\":23}"),
          "COM argument order is labeled and never silently reversed");
    args.cArgs=1;args.rgvarg=nullptr;
    const auto missing=JsonEvent(22,&args,0);
    check(Has(missing,"\"argumentsReadable\":false")&&Has(missing,"\"arguments\":[]"),"missing argument storage is never dereferenced");
    args.cArgs=65;args.rgvarg=reinterpret_cast<VARIANT*>(static_cast<ULONG_PTR>(1));
    const auto oversized=JsonEvent(22,&args,0);
    check(Has(oversized,"\"argumentCount\":65")&&Has(oversized,"\"argumentsReadable\":false"),
          "oversized argument count refuses even intentionally invalid storage");
    args.cArgs=0;
    check(Has(JsonEvent(22,&args,0),"\"arguments\":[]"),"zero count never touches storage");
}
void NumericTests(){
    VARIANT value{};
    value.vt=VT_I1;value.cVal=-7;check(Has(One(value),"\"numeric\":-7"),"signed byte is numeric, not a character");
    value.vt=VT_UI1;value.bVal=255;check(Has(One(value),"\"numeric\":255"),"unsigned byte is numeric");
    value.vt=VT_I2;value.iVal=-32000;check(Has(One(value),"\"numeric\":-32000"),"signed short retained");
    value.vt=VT_UI2;value.uiVal=65000;check(Has(One(value),"\"numeric\":65000"),"unsigned short retained");
    value.vt=VT_I4;value.lVal=-123456;check(Has(One(value),"\"numeric\":-123456"),"signed long retained");
    value.vt=VT_UI4;value.ulVal=4294967295ul;check(Has(One(value),"\"numeric\":4294967295"),"unsigned 32-bit value is not sign-extended");
    value.vt=VT_I8;value.llVal=(-9223372036854775807ll-1);check(Has(One(value),"\"numeric\":-9223372036854775808"),"signed 64-bit boundary retained");
    value.vt=VT_UI8;value.ullVal=18446744073709551615ull;check(Has(One(value),"\"numeric\":18446744073709551615"),"unsigned 64-bit boundary retained without floating conversion");
    value.vt=VT_INT;value.intVal=-19;check(Has(One(value),"\"numeric\":-19"),"native integer retained");
    value.vt=VT_UINT;value.uintVal=19;check(Has(One(value),"\"numeric\":19"),"unsigned native integer retained");
    value.vt=VT_BOOL;value.boolVal=VARIANT_TRUE;check(Has(One(value),"\"numeric\":-1"),"VARIANT_BOOL is recorded without changing its representation");
    value.vt=VT_ERROR;value.scode=E_ACCESSDENIED;check(Has(One(value),"\"numeric\":-2147024891"),"error scalar retains signed HRESULT bits");
    value.vt=VT_R4;value.fltVal=1.25f;check(Has(One(value),"\"numeric\":1.25"),"finite float produces valid decimal JSON");
    value.vt=VT_R8;value.dblVal=-2.5;check(Has(One(value),"\"numeric\":-2.5"),"finite double produces valid decimal JSON");
    for(const auto nonfinite:{std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
        value.vt=VT_R8;value.dblVal=nonfinite;
        check(Has(One(value),"\"numeric\":null"),"nonfinite double cannot produce invalid JSON");
    }
    value.vt=VT_R4;value.fltVal=std::numeric_limits<float>::quiet_NaN();
    check(Has(One(value),"\"numeric\":null"),"nonfinite float cannot produce invalid JSON");
}
void PrivacyTests(){
    // These invalid payload addresses deliberately crash any accidental dereference.
    // VARIANTs are plain test storage and are never passed to VariantClear.
    const VARTYPE types[]={VT_BSTR,VT_UNKNOWN,VT_DISPATCH,VT_RECORD,VT_VARIANT,
        static_cast<VARTYPE>(VT_ARRAY|VT_UI1),static_cast<VARTYPE>(VT_BYREF|VT_I4),
        static_cast<VARTYPE>(VT_BYREF|VT_BOOL),static_cast<VARTYPE>(VT_BYREF|VT_BSTR),
        static_cast<VARTYPE>(VT_BYREF|VT_VARIANT),static_cast<VARTYPE>(VT_BYREF|VT_ARRAY|VT_UI1)};
    for(const auto type:types){
        VARIANT value{};value.vt=type;value.byref=reinterpret_cast<void*>(static_cast<ULONG_PTR>(1));
        const auto original=value;const auto text=One(value);
        check(Has(text,"\"vartype\":"+std::to_string(type))&&Has(text,"\"numeric\":null"),
              "pointer, bytes, strings and every BYREF payload are type-only");
        check(std::memcmp(&original,&value,sizeof(value))==0,"diagnostic never mutates callback arguments");
    }
    wchar_t secret[]=L"synthetic-private-user-and-channel-data";
    VARIANT value{};value.vt=VT_BSTR;value.bstrVal=secret;
    check(!Has(One(value),"synthetic-private")&&Has(One(value),"\"numeric\":null"),"literal string payload never reaches the log");
    value={};value.vt=VT_BOOL|VT_BYREF;VARIANT_BOOL permitted=VARIANT_FALSE;value.pboolVal=&permitted;
    const auto text=One(value);
    check(permitted==VARIANT_FALSE&&Has(text,"\"eventArgumentsModified\":false"),
          "confirmation or authentication BYREF decisions remain untouched");
}
void StateTests(){
    StateObservation state;
    check(state.Changed(S_OK,0),"initial state is recorded");
    check(!state.Changed(S_OK,0),"unchanged state is not logged repeatedly");
    check(state.Changed(S_OK,2),"connecting transition is recorded");
    check(state.Changed(S_OK,1),"connected transition is recorded");
    check(state.Changed(E_ACCESSDENIED,-1),"getter failure is recorded");
    check(!state.Changed(E_ACCESSDENIED,123),"invalid state values do not create false failure transitions");
    check(state.Changed(E_FAIL,123),"different HRESULT remains visible");
    check(state.Changed(S_OK,1),"getter recovery is recorded even for previous connected state");
}
}
int main(){
    EventTests();NumericTests();PrivacyTests();StateTests();
    std::cout<<"Child session client diagnostics: "<<passed<<" passed, "<<failed<<" failed; synthetic arguments only, no COM activation or Windows query.\n";
    return failed?1:0;
}
