#include "../src/lab/child_session_viewport.hpp"
#include <cwchar>
#include <iostream>
using namespace seats::lab::child;
namespace {
unsigned passed=0,failed=0;
void Check(bool value,const char* label){if(value)++passed;else{++failed;std::cerr<<"FAIL "<<label<<'\n';}}
class Settings final:public IDispatch {
public:
    unsigned& depth;ULONG refs=1;unsigned adds=0,releases=0,reads=0,writes=0;
    bool guarded=true,validShape=true,fit=false,ignoreWrite=false,advancedNull=false;
    bool reenterAdd=false,reenterWrite=false,reenterRelease=false;
    HRESULT reentry=S_OK,advancedResult=S_OK,writeResult=S_OK,readResult=S_OK;
    VARTYPE advancedType=VT_DISPATCH,readType=VT_BOOL;
    VARIANT_BOOL forcedBool=VARIANT_TRUE;bool forceBool=false;
    explicit Settings(unsigned& value):depth(value){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** out)override{if(out)*out=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override {
        ++adds;guarded=guarded&&depth==1;++refs;
        if(reenterAdd)reentry=viewport::SetFit(this,true,depth).result;
        return refs;
    }
    ULONG STDMETHODCALLTYPE Release()override {
        ++releases;guarded=guarded&&depth==1;
        if(reenterRelease)reentry=viewport::SetFit(this,true,depth).result;
        return --refs;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* out)override{if(out)*out=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT,LCID,ITypeInfo** out)override{if(out)*out=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID,LPOLESTR* names,UINT count,LCID,DISPID* out)override {
        guarded=guarded&&depth==1;
        if(!names||count!=1||!out)return E_INVALIDARG;
        if(!std::wcscmp(names[0],L"AdvancedSettings9"))*out=1;
        else if(!std::wcscmp(names[0],L"SmartSizing"))*out=2;
        else {validShape=false;return DISP_E_UNKNOWNNAME;}
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id,REFIID,LCID,WORD flags,DISPPARAMS* args,VARIANT* out,EXCEPINFO*,UINT*)override {
        guarded=guarded&&depth==1;
        if(id==1&&flags==DISPATCH_PROPERTYGET){
            if(FAILED(advancedResult))return advancedResult;
            if(!out)return E_POINTER;VariantInit(out);out->vt=advancedType;
            if(advancedType==VT_DISPATCH&&!advancedNull){out->pdispVal=this;AddRef();}
            return S_OK;
        }
        if(id==2&&flags==DISPATCH_PROPERTYPUT){
            ++writes;
            validShape=validShape&&args&&args->cArgs==1&&args->cNamedArgs==1&&args->rgvarg&&args->rgdispidNamedArgs&&
                args->rgdispidNamedArgs[0]==DISPID_PROPERTYPUT&&args->rgvarg[0].vt==VT_BOOL&&
                (args->rgvarg[0].boolVal==VARIANT_TRUE||args->rgvarg[0].boolVal==VARIANT_FALSE);
            if(reenterWrite)reentry=viewport::SetFit(this,false,depth).result;
            if(SUCCEEDED(writeResult)&&!ignoreWrite&&validShape)fit=args->rgvarg[0].boolVal==VARIANT_TRUE;
            return writeResult;
        }
        if(id==2&&flags==DISPATCH_PROPERTYGET){
            ++reads;if(FAILED(readResult))return readResult;
            if(!out)return E_POINTER;VariantInit(out);out->vt=readType;
            if(readType==VT_BOOL)out->boolVal=forceBool?forcedBool:fit?VARIANT_TRUE:VARIANT_FALSE;
            else if(readType==VT_I4)out->lVal=fit?-1:0;
            return S_OK;
        }
        validShape=false;return DISP_E_MEMBERNOTFOUND;
    }
};
}
int main(){
    unsigned depth=0;
    Check(viewport::SetFit(nullptr,true,depth).result==E_POINTER&&depth==0,"null client leaves depth unchanged");
    Settings client(depth);depth=2;
    Check(viewport::SetFit(&client,true,depth).result==E_PENDING&&client.adds==0&&depth==2,"nested COM context never changes sizing");
    depth=0;auto value=viewport::SetFit(&client,true,depth);
    Check(value.result==S_OK&&value.known&&value.fit,"fit needs canonical true readback");
    Check(client.reads==1&&client.writes==1&&client.validShape,"only documented property and exact bool shape used");
    Check(client.refs==1&&client.adds==2&&client.releases==2&&depth==0&&client.guarded,"entire reference and dispatch lifetime guarded and balanced");
    value=viewport::SetFit(&client,false,depth);
    Check(value.result==S_OK&&value.known&&!value.fit,"actual size needs canonical false readback");
    {
        Settings f(depth);f.fit=true;f.writeResult=E_ACCESSDENIED;
        value=viewport::SetFit(&f,true,depth);
        Check(value.result==E_ACCESSDENIED&&value.known&&value.fit,"matching state never hides rejected write");
        Check(f.reads==1&&f.refs==1&&depth==0,"rejected write still inspects actual state and balances refs");
    }
    {
        Settings f(depth);f.readResult=E_FAIL;value=viewport::SetFit(&f,true,depth);
        Check(value.result==E_FAIL&&!value.known,"failed readback never claims fit after successful write");
        Check(f.refs==1&&depth==0&&f.guarded,"readback failure preserves ownership guard");
    }
    {
        Settings f(depth);f.ignoreWrite=true;value=viewport::SetFit(&f,true,depth);
        Check(value.result==E_UNEXPECTED&&value.known&&!value.fit,"ignored property write reports observed actual size");
    }
    {
        Settings f(depth);f.readType=VT_I4;value=viewport::SetFit(&f,true,depth);
        Check(value.result==DISP_E_TYPEMISMATCH&&!value.known,"numeric bool substitute is rejected");
    }
    {
        Settings f(depth);f.forceBool=true;f.forcedBool=1;value=viewport::SetFit(&f,true,depth);
        Check(value.result==DISP_E_TYPEMISMATCH&&!value.known,"noncanonical VARIANT_BOOL is rejected");
    }
    {
        Settings f(depth);f.advancedResult=E_NOINTERFACE;value=viewport::SetFit(&f,true,depth);
        Check(value.result==E_NOINTERFACE&&!value.known&&f.writes==0&&f.reads==0,"missing advanced settings cannot attempt a write");
        Check(f.refs==1&&depth==0,"missing advanced settings releases only held client");
    }
    for(const bool missing:{false,true}){
        Settings f(depth);f.advancedNull=missing;if(!missing)f.advancedType=VT_EMPTY;
        value=viewport::SetFit(&f,true,depth);
        Check(value.result==DISP_E_TYPEMISMATCH&&f.writes==0&&!value.known,"invalid advanced interface never permits mutation");
        Check(f.refs==1&&depth==0,"invalid advanced interface does not leak refs");
    }
    for(unsigned stage=0;stage<3;++stage){
        Settings f(depth);f.reenterAdd=stage==0;f.reenterWrite=stage==1;f.reenterRelease=stage==2;
        value=viewport::SetFit(&f,true,depth);
        Check(value.result==S_OK&&f.reentry==E_PENDING&&f.writes==1,"reentry during add/write/release cannot recurse into sizing");
        Check(f.refs==1&&depth==0&&f.guarded,"reentrant attempt preserves refs and guard");
    }
    std::cout<<"Desktop viewport: "<<passed<<" passed, "<<failed<<" failed; fake COM only, no viewer, session or display change.\n";
    return failed?1:0;
}
