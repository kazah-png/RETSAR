#include "../src/lab/child_session_activex.hpp"
#include <iostream>
using namespace seats::lab::child::ax;
namespace {
unsigned passed=0,failed=0;
void Check(bool value,const char* label){if(value)++passed;else{++failed;std::cerr<<"FAIL "<<label<<'\n';}}
class ActiveObject final:public IOleInPlaceActiveObject {
public:
    unsigned& depth;ULONG refs=1;unsigned calls=0,guardedAdd=0,guardedRelease=0;
    BOOL activation=FALSE;HRESULT result=S_OK,reentry=E_UNEXPECTED;bool requestReentry=false,guardedCallback=false;
    explicit ActiveObject(unsigned& value):depth(value){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** out)override{if(out)*out=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{if(depth)++guardedAdd;return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{if(depth)++guardedRelease;return --refs;}
    HRESULT STDMETHODCALLTYPE GetWindow(HWND* out)override{if(out)*out=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG)override{return S_FALSE;}
    HRESULT STDMETHODCALLTYPE OnFrameWindowActivate(BOOL value)override {
        ++calls;activation=value;guardedCallback=depth==1&&refs==2;
        if(requestReentry)reentry=NotifyFrameWindow(this,FALSE,depth);
        return result;
    }
    HRESULT STDMETHODCALLTYPE OnDocWindowActivate(BOOL)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE ResizeBorder(LPCRECT,IOleInPlaceUIWindow*,BOOL)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnableModeless(BOOL)override{return S_OK;}
};
}
int main(){
    for(const UINT message:{WM_KEYDOWN,WM_KEYUP,WM_CHAR,WM_DEADCHAR,WM_SYSKEYDOWN,WM_SYSKEYUP,WM_SYSCHAR,WM_SYSDEADCHAR,WM_UNICHAR})
        Check(KeyboardMessage(message),"keyboard and system-key messages retain accelerator translation");
    for(const UINT message:{WM_PAINT,WM_ERASEBKGND,WM_MOUSEMOVE,WM_TIMER,WM_ACTIVATE,WM_CLOSE,WM_HOTKEY,WM_IME_COMPOSITION,WM_APP})
        Check(!KeyboardMessage(message),"non-key messages stay out of the ActiveX accelerator call");
    unsigned depth=0;ActiveObject object(depth);
    Check(NotifyFrameWindow(nullptr,TRUE,depth)==E_POINTER&&depth==0,"missing object cannot alter outbound depth");
    Check(NotifyFrameWindow(&object,TRUE,depth)==S_OK&&object.activation==TRUE&&object.calls==1,"activation reaches only the supplied control");
    Check(object.guardedCallback&&object.guardedAdd==1&&object.guardedRelease==1,"all COM calls retain the object inside the outbound guard");
    Check(depth==0&&object.refs==1,"successful notification balances references and depth");
    object.result=E_FAIL;
    Check(NotifyFrameWindow(&object,FALSE,depth)==E_FAIL&&object.activation==FALSE,"deactivation preserves the exact COM failure");
    Check(depth==0&&object.refs==1,"failed notification still balances references and depth");
    const auto before=object.calls;depth=2;
    Check(NotifyFrameWindow(&object,TRUE,depth)==E_PENDING&&object.calls==before&&depth==2,"busy or reentrant COM stack never receives a nested activation");
    depth=0;object.result=S_OK;object.requestReentry=true;
    Check(NotifyFrameWindow(&object,TRUE,depth)==S_OK&&object.reentry==E_PENDING&&object.calls==before+1,"reentrant notification attempt is deferred without recursion");
    Check(depth==0&&object.refs==1,"reentrant attempt cannot leak ownership or the guard");
    {
        Site site;
        Check(site.SetActiveObject(&object,nullptr)==S_OK&&object.refs==2,"inactive site retains its active object without a window");
        Check(site.SetActiveObject(nullptr,nullptr)==S_OK&&object.refs==1,"deactivation releases only the site's reference");
    }
    Check(object.refs==1,"site teardown does not release external ownership");
    std::cout<<"Desktop viewer: "<<passed<<" passed, "<<failed<<" failed; fake COM and message values only, no windows, input or sessions.\n";
    return failed?1:0;
}

