#include "../src/lab/child_session_activex.hpp"
#include "../src/lab/child_session_viewer_state.hpp"
#include "../src/lab/child_session_close.hpp"
#include <iostream>
#include <string>
#include <functional>
#include <utility>
#include <initializer_list>
#include <cwchar>
using namespace seats::lab::child::ax;
namespace {
unsigned passed=0,failed=0;
void Check(bool value,const char* label){if(value)++passed;else{++failed;std::cerr<<"FAIL "<<label<<'\n';}}
class ActiveObject final:public IOleInPlaceActiveObject {
public:
    unsigned& depth;ULONG refs=1;unsigned calls=0,guardedAdd=0,guardedRelease=0;
    BOOL activation=FALSE;HRESULT result=S_OK,reentry=E_UNEXPECTED;bool requestReentry=false,guardedCallback=false;
    unsigned keyCalls=0;bool addReentry=false,keyReentry=false;HRESULT nestedKey=E_UNEXPECTED;MSG key{nullptr,WM_KEYDOWN};
    std::function<void()> onAdd,onRelease;
    explicit ActiveObject(unsigned& value):depth(value){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** out)override{if(out)*out=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{
        if(depth)++guardedAdd;++refs;
        auto callback=std::move(onAdd);onAdd=nullptr;if(callback)callback();
        if(addReentry){addReentry=false;nestedKey=TranslateOwnedAccelerator(this,&key,depth);}return refs;
    }
    ULONG STDMETHODCALLTYPE Release()override{
        if(depth)++guardedRelease;--refs;
        auto callback=std::move(onRelease);onRelease=nullptr;if(callback)callback();return refs;
    }
    HRESULT STDMETHODCALLTYPE GetWindow(HWND* out)override{if(out)*out=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG message)override{
        ++keyCalls;guardedCallback=depth==1&&refs==2&&message==&key;
        if(keyReentry)nestedKey=TranslateOwnedAccelerator(this,&key,depth);return result;
    }
    HRESULT STDMETHODCALLTYPE OnFrameWindowActivate(BOOL value)override {
        ++calls;activation=value;guardedCallback=depth==1&&refs==2;
        if(requestReentry)reentry=NotifyFrameWindow(this,FALSE,depth);
        return result;
    }
    HRESULT STDMETHODCALLTYPE OnDocWindowActivate(BOOL)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE ResizeBorder(LPCRECT,IOleInPlaceUIWindow*,BOOL)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnableModeless(BOOL)override{return S_OK;}
};
class ConnectionProperty final:public IDispatch {
public:
    unsigned& depth;ULONG refs=1;unsigned additions=0,releases=0,reads=0;
    bool guarded=true,propertyOnly=true;HRESULT lookupResult=S_OK,readResult=S_OK;
    VARTYPE type=VT_I2;LONG value=0;std::function<void()> duringRead,duringRelease;
    explicit ConnectionProperty(unsigned& valueDepth):depth(valueDepth){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** out)override{if(out)*out=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{guarded=guarded&&depth==1;++additions;return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{
        guarded=guarded&&depth==1;++releases;--refs;
        auto callback=std::move(duringRelease);duringRelease=nullptr;if(callback)callback();return refs;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* out)override{if(out)*out=0;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT,LCID,ITypeInfo** out)override{if(out)*out=nullptr;return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID,LPOLESTR* names,UINT count,LCID,DISPID* out)override{
        guarded=guarded&&depth==1&&refs==2;
        propertyOnly=propertyOnly&&count==1&&names&&names[0]&&std::wcscmp(names[0],L"Connected")==0;
        if(out)*out=41;return lookupResult;
    }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id,REFIID,LCID,WORD flags,DISPPARAMS* args,VARIANT* out,EXCEPINFO*,UINT*)override{
        ++reads;guarded=guarded&&depth==1&&refs==2;
        propertyOnly=propertyOnly&&id==41&&flags==DISPATCH_PROPERTYGET&&args&&args->cArgs==0&&args->cNamedArgs==0;
        if(out){
            out->vt=type;
            if(type==VT_I2)out->iVal=static_cast<SHORT>(value);
            else if(type==VT_I4)out->lVal=value;
            else if(type==(VT_I4|VT_BYREF))out->plVal=&value;
            else if(type==VT_BOOL)out->boolVal=VARIANT_FALSE;
        }
        auto callback=std::move(duringRead);duringRead=nullptr;if(callback)callback();return readResult;
    }
};}
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
    object.requestReentry=false;
    {
        bool pending=true;depth=1;const auto count=object.calls;
        Check(NotifyPendingFrameWindow(&object,TRUE,pending,false,depth)==E_PENDING&&pending&&object.calls==count,
            "nested message dispatch preserves pending activation without calling COM");
        depth=0;
        Check(NotifyPendingFrameWindow(&object,TRUE,pending,true,depth)==E_PENDING&&pending,
            "closing or initialization preserves a pending notification without delivery");
        Check(NotifyPendingFrameWindow(nullptr,TRUE,pending,false,depth)==E_PENDING&&pending,
            "missing active object does not consume pending activation");
        Check(NotifyPendingFrameWindow(&object,TRUE,pending,false,depth)==S_OK&&!pending&&object.calls==count+1,
            "outer message-loop drain delivers the retained activation once");
        Check(NotifyPendingFrameWindow(&object,TRUE,pending,false,depth)==S_FALSE&&object.calls==count+1,
            "idle outer loop performs no extra COM notification");
    }
    Check(TranslateOwnedAccelerator(nullptr,&object.key,depth)==E_POINTER&&depth==0,
        "accelerator without an object performs no COM call");
    Check(TranslateOwnedAccelerator(&object,nullptr,depth)==E_POINTER&&depth==0,
        "accelerator without a message preserves the guard");
    MSG unrelated{nullptr,WM_PAINT};
    Check(TranslateOwnedAccelerator(&object,&unrelated,depth)==S_FALSE&&object.keyCalls==0,
        "non-key messages never reach the control translator");
    for(const HRESULT result:{S_OK,S_FALSE,E_FAIL}) {
        object.result=result;const auto add=object.guardedAdd,release=object.guardedRelease,calls=object.keyCalls;
        Check(TranslateOwnedAccelerator(&object,&object.key,depth)==result&&object.keyCalls==calls+1,
            "accelerator preserves consumed unhandled and failed results");
        Check(object.guardedCallback&&object.guardedAdd==add+1&&object.guardedRelease==release+1&&depth==0&&object.refs==1,
            "accelerator guards AddRef translation and Release with balanced ownership");
    }
    depth=2;const auto keyCalls=object.keyCalls;
    Check(TranslateOwnedAccelerator(&object,&object.key,depth)==E_PENDING&&object.keyCalls==keyCalls&&depth==2,
        "already-guarded accelerator is deferred without recursion");
    depth=0;object.result=S_OK;object.addReentry=true;
    Check(TranslateOwnedAccelerator(&object,&object.key,depth)==S_OK&&object.nestedKey==E_PENDING&&object.keyCalls==keyCalls+1,
        "AddRef reentry is guarded before the interface lifetime begins");
    object.keyReentry=true;
    Check(TranslateOwnedAccelerator(&object,&object.key,depth)==S_OK&&object.nestedKey==E_PENDING&&object.keyCalls==keyCalls+2&&depth==0&&object.refs==1,
        "TranslateAccelerator reentry cannot leak references or recursively deliver input");
    {
        unsigned nestedDepth=0;bool pending=false,closing=false,stop=false;
        ActiveObject first(nestedDepth),second(nestedDepth),third(nestedDepth);
        Site site;site.outboundDepth=&nestedDepth;site.closing=&closing;
        site.stopRequested=&stop;site.frameActivationPending=&pending;
        Check(site.SetActiveObject(&first,nullptr)==S_OK&&pending&&first.refs==2&&nestedDepth==0,
            "active object publication retains a guarded reference and rearms frame delivery");
        Check(NotifyPendingFrameWindow(site.active.value,TRUE,pending,false,nestedDepth)==S_OK&&!pending&&first.activation,
            "new object receives current active-frame state without a new WM_ACTIVATE");
        const auto generation=site.activeGeneration;
        Check(site.SetActiveObject(&first,nullptr)==S_OK&&pending&&first.refs==2&&site.activeGeneration==generation+1,
            "same object replacement balances AddRef and Release and rearms delivery");
        pending=false;
        second.onAdd=[&](){Check(nestedDepth==1&&site.active.value==&first,"candidate AddRef observes guarded old registration");
            Check(site.SetActiveObject(&third,nullptr)==S_OK,"nested AddRef can publish newer registration");};
        Check(site.SetActiveObject(&second,nullptr)==S_OK&&site.active.value==&third&&pending,
            "outer AddRef callback cannot overwrite a newer active registration");
        Check(first.refs==1&&second.refs==1&&third.refs==2&&nestedDepth==0,
            "nested AddRef replacement releases stale candidate and old object exactly once");
        site.SetActiveObject(nullptr,nullptr);
        Check(third.refs==1&&site.active.value==nullptr,"null deactivation clears the retained object");
        site.SetActiveObject(&first,nullptr);
        first.onRelease=[&](){Check(nestedDepth==1&&site.active.value==&second,"old Release sees replacement published before release");
            site.SetActiveObject(&third,nullptr);};
        Check(site.SetActiveObject(&second,nullptr)==S_OK&&site.active.value==&third&&
            first.refs==1&&second.refs==1&&third.refs==2&&nestedDepth==0,
            "Release replacement keeps the newer callback registration with balanced ownership");
        site.SetActiveObject(nullptr,nullptr);site.SetActiveObject(&first,nullptr);
        second.onAdd=[&](){site.SetActiveObject(nullptr,nullptr);};
        Check(site.SetActiveObject(&second,nullptr)==S_OK&&!site.active&&first.refs==1&&second.refs==1&&nestedDepth==0,
            "null callback during AddRef cancels publication and releases both stale references");
        site.SetActiveObject(&first,nullptr);
        first.onRelease=[&](){site.SetActiveObject(nullptr,nullptr);};
        Check(site.SetActiveObject(&second,nullptr)==S_OK&&!site.active&&first.refs==1&&second.refs==1&&nestedDepth==0,
            "null callback during old Release clears the newly published reference once");
        site.SetActiveObject(&first,nullptr);const auto additions=second.guardedAdd;closing=true;
        Check(site.SetActiveObject(&second,nullptr)==E_ABORT&&site.active.value==&first&&second.guardedAdd==additions,
            "closing rejects replacement before retaining its reference");
        Check(site.SetActiveObject(nullptr,nullptr)==S_OK&&!site.active&&first.refs==1,
            "null deactivation remains legal while closing");
        closing=false;stop=true;
        Check(site.SetActiveObject(&second,nullptr)==E_ABORT&&!site.active&&second.guardedAdd==additions,
            "deferred stop blocks new registrations before cleanup starts");
        stop=false;site.SetActiveObject(&first,nullptr);
        second.onAdd=[&](){closing=true;};
        Check(site.SetActiveObject(&second,nullptr)==E_ABORT&&site.active.value==&first&&second.refs==1&&first.refs==2&&nestedDepth==0,
            "close during AddRef releases the candidate and retains existing object for cleanup");
        site.SetActiveObject(nullptr,nullptr);closing=false;
        second.onAdd=[&](){closing=true;second.onRelease=[&](){site.SetActiveObject(nullptr,nullptr);};};
        Check(site.SetActiveObject(&second,nullptr)==E_ABORT&&!site.active&&second.refs==1&&nestedDepth==0,
            "reentry while releasing cancelled candidate cannot mask the close result");
        closing=false;site.activeGeneration=~0ull;
        Check(site.SetActiveObject(&second,nullptr)==E_UNEXPECTED&&!site.active&&second.refs==1&&nestedDepth==0,
            "generation overflow rejects publication without retaining a reference");
        site.activeGeneration=0;nestedDepth=~0u;
        Check(site.SetActiveObject(&second,nullptr)==E_UNEXPECTED&&!site.active&&second.refs==1&&nestedDepth==~0u,
            "depth overflow rejects registration without wrapping the lifecycle guard");
        nestedDepth=0;
        Check(first.guardedAdd==first.guardedRelease&&second.guardedAdd==second.guardedRelease&&third.guardedAdd==third.guardedRelease,
            "all replacement paths balance guarded references");
    }
    {
        unsigned nestedDepth=0;bool pending=false;
        ActiveObject first(nestedDepth),candidate(nestedDepth);HRESULT rejected=S_OK,cleared=E_FAIL;
        {
            Site site;site.outboundDepth=&nestedDepth;site.frameActivationPending=&pending;
            site.SetActiveObject(&first,nullptr);pending=false;
            first.onRelease=[&](){
                Check(nestedDepth==1&&!site.active&&site.retiring,"destructor publishes null before guarded Release");
                rejected=site.SetActiveObject(&candidate,nullptr);cleared=site.SetActiveObject(nullptr,nullptr);
            };
        }
        Check(rejected==E_ABORT&&cleared==S_OK&&first.refs==1&&candidate.refs==1&&!pending&&nestedDepth==0,
            "retiring site rejects new ownership and tolerates reentrant null deactivation");
        Check(first.guardedRelease==1&&candidate.guardedAdd==0,
            "destruction releases its sole retained reference and never retains a replacement");
    }
    namespace viewer=seats::lab::child::viewer;
    namespace closure=seats::lab::child::closure;
    {
        VARIANT reason{};reason.vt=VT_I4;reason.lVal=1;DISPPARAMS args{&reason,nullptr,1,0};
        Check(closure::DisconnectedEvent(&args),"documented one-LONG disconnect event supplies evidence");
        for(const LONG value:{0L,3L,264L,-1L}){reason.lVal=value;
            Check(closure::DisconnectedEvent(&args),"event validation does not mistake reason codes for transport state");}
        Check(!closure::DisconnectedEvent(nullptr),"missing event arguments never confirm disconnection");
        args.cArgs=0;Check(!closure::DisconnectedEvent(&args),"missing reason cannot confirm a disconnect event");
        args.cArgs=2;Check(!closure::DisconnectedEvent(&args),"extra reason arguments are rejected");
        args.cArgs=1;args.cNamedArgs=1;Check(!closure::DisconnectedEvent(&args),"named event arguments are rejected");
        args.cNamedArgs=0;args.rgvarg=nullptr;Check(!closure::DisconnectedEvent(&args),"absent event values are rejected");
        args.rgvarg=&reason;
        for(const VARTYPE type:std::initializer_list<VARTYPE>{VT_I2,VT_UI4,VT_BOOL,static_cast<VARTYPE>(VT_I4|VT_BYREF),VT_EMPTY}){
            reason.vt=type;Check(!closure::DisconnectedEvent(&args),"only the documented LONG reason can be closure evidence");}
    }
    {
        unsigned readDepth=0;ConnectionProperty property(readDepth);
        Check(closure::ReadConnected(nullptr,readDepth).result==E_POINTER&&readDepth==0,
            "closure read without a control is unknown and has no side effects");
        readDepth=1;const auto busy=closure::ReadConnected(&property,readDepth);
        Check(busy.result==E_PENDING&&!busy.known&&property.additions==0&&readDepth==1,
            "nested closure read never reenters the control");readDepth=0;
        for(const VARTYPE type:std::initializer_list<VARTYPE>{VT_I2,VT_I4}){
            property.type=type;const auto reading=closure::ReadConnected(&property,readDepth);
            Check(reading.result==S_OK&&reading.known&&reading.value==0,
                "S_OK with the native or supported integer zero is a positive readback");
        }
        for(const LONG value:{1L,2L,-1L,3L}){
            property.value=value;const auto reading=closure::ReadConnected(&property,readDepth);
            Check(reading.known&&reading.value==value,"active connecting and unknown integer states are preserved without becoming zero");
        }
        property.value=0;
        for(const HRESULT result:{S_FALSE,E_FAIL,DISP_E_EXCEPTION}){
            property.readResult=result;const auto reading=closure::ReadConnected(&property,readDepth);
            Check(reading.result==result&&!reading.known,"a returned zero cannot conceal non-S_OK property results");
        }
        property.readResult=S_OK;
        for(const VARTYPE type:std::initializer_list<VARTYPE>{VT_BOOL,VT_EMPTY,VT_UI4,static_cast<VARTYPE>(VT_I4|VT_BYREF)}){
            property.type=type;const auto reading=closure::ReadConnected(&property,readDepth);
            Check(reading.result==DISP_E_TYPEMISMATCH&&!reading.known,"coercible and by-reference values are not strict closure evidence");
        }
        property.type=VT_I2;property.lookupResult=DISP_E_UNKNOWNNAME;const auto beforeReads=property.reads;
        Check(closure::ReadConnected(&property,readDepth).result==DISP_E_UNKNOWNNAME&&property.reads==beforeReads,
            "missing property preserves its lookup error without invoking another member");
        Check(property.propertyOnly&&property.guarded&&property.refs==1&&readDepth==0&&property.additions==property.releases,
            "every closure read is a read-only property access with guarded balanced references");
        property.lookupResult=S_OK;
        viewer::ConnectionEnd end;end.connectAttempted=true;
        auto revision=end.eventRevision;property.duringRead=[&](){end.ActiveEvent();};
        auto reading=closure::ReadConnected(&property,readDepth);end.Read(reading.known,reading.value,revision);
        Check(!end.Verified(),"active event inside a property read invalidates its older zero sample");
        property.value=1;revision=end.eventRevision;property.duringRead=[&](){end.EndEvent();};
        reading=closure::ReadConnected(&property,readDepth);end.Read(reading.known,reading.value,revision);
        Check(end.Verified()&&end.Evidence()==viewer::EndEvidence::DisconnectedEvent,
            "synchronous disconnect event survives an older active readback");
        property.value=0;revision=end.eventRevision;property.duringRelease=[&](){end.ActiveEvent();};
        reading=closure::ReadConnected(&property,readDepth);end.Read(reading.known,reading.value,revision);
        Check(!end.Verified()&&property.refs==1&&readDepth==0,
            "event delivered from Release also supersedes a stale zero without leaking a reference");
    }
    {
        viewer::State stopped;stopped.closing=true;stopped.finished=true;stopped.baselineRestored=true;
        Check(viewer::Present(stopped).phase==viewer::Phase::NotConnected&&!stopped.end.Verified()&&stopped.end.Satisfied(),
            "closing before Connect is not presented as an observed disconnection");
        Check(stopped.end.Evidence()==viewer::EndEvidence::NotAttempted,
            "no transport attempt is distinguishable from positive end evidence");
        stopped.end.EndEvent();
        Check(!stopped.end.Verified(),"synchronous disconnect observation is retained but not claimed as an attempted connection yet");
        stopped.end.connectAttempted=true;
        Check(stopped.end.Verified()&&viewer::Present(stopped).phase==viewer::Phase::Disconnected,
            "disconnect event delivered inside Connect remains evidence when the attempted call returns");
        stopped.end={};stopped.end.connectAttempted=true;
        Check(viewer::Present(stopped).phase==viewer::Phase::NeedsAttention,
            "restoring Windows settings alone cannot claim that a transport ended");
        stopped.end.deadlineExpired=true;
        for(const bool enabled:{false,true}){
            stopped.preserveEnabled=enabled;
            const auto result=viewer::Present(stopped);
            Check(result.phase==viewer::Phase::NeedsAttention&&std::wstring(result.badge)==L"CLOSE UNCONFIRMED"&&
                std::wstring(result.message).find(L"deadline")!=std::wstring::npos,
                "deadline uncertainty survives both restored-disabled and preserved-enabled policies");
        }
        stopped.end.Read(false,0,stopped.end.eventRevision);
        Check(!stopped.end.Verified(),"failed Connected read carrying zero cannot resolve a timeout");
        stopped.end.Read(true,0,stopped.end.eventRevision);
        Check(stopped.end.Verified()&&stopped.end.Evidence()==viewer::EndEvidence::ConnectedZero&&
            viewer::Present(stopped).phase==viewer::Phase::Disconnected,
            "a positive final read can independently confirm disconnection after the observation deadline");
        stopped.end.disconnectFailed=true;
        Check(viewer::Present(stopped).phase==viewer::Phase::NeedsAttention&&stopped.end.Verified(),
            "a failed Disconnect is still an error even when later evidence confirms the transport ended");
        stopped.end.disconnectFailed=false;stopped.end.closeFailed=true;
        Check(viewer::Present(stopped).phase==viewer::Phase::NeedsAttention&&stopped.end.Verified(),
            "failed object closure cannot be hidden by positive transport and setting evidence");
        stopped.end.closeFailed=false;stopped.operationFailed=true;
        Check(viewer::Present(stopped).phase==viewer::Phase::NeedsAttention,
            "a failed Connect attempt retains its earlier error even after a verified disconnect");
        stopped.operationFailed=false;stopped.baselineRestored=false;
        Check(viewer::Present(stopped).phase==viewer::Phase::NeedsAttention,
            "positive connection-end evidence cannot conceal an unverified Windows baseline");
        stopped.baselineRestored=true;stopped.end.ActiveEvent();
        Check(!stopped.end.Verified()&&viewer::Present(stopped).phase==viewer::Phase::NeedsAttention,
            "a newer connection event revokes older closure evidence");
        stopped.end.EndEvent();stopped.end.Read(true,2,stopped.end.eventRevision);
        Check(!stopped.end.Verified(),"a newer confirmed connecting state contradicts historical disconnection");
        stopped.end={};stopped.end.connectAttempted=true;stopped.operationFailed=true;
        Check(viewer::Present(stopped).phase==viewer::Phase::NeedsAttention&&
            std::wstring(viewer::Present(stopped).badge)==L"CLOSE UNCONFIRMED",
            "failed Connect with no end evidence never becomes an implicitly clean shutdown");
    }
    viewer::State state;
    const auto phase=[&](){return viewer::Present(state).phase;};
    Check(phase()==viewer::Phase::Preparing,"initial presentation does not claim a connection");
    state.connectionStarted=true;
    Check(phase()==viewer::Phase::Connecting,"Connect attempt explains connection without claiming readiness");
    state.transportConnected=true;
    Check(phase()==viewer::Phase::SigningIn,"transport connection alone does not claim a usable desktop");
    state.authenticationPrompt=true;
    Check(phase()==viewer::Phase::WaitingForWindows,"Windows confirmation remains visible until dismissed");
    state.authenticationPrompt=false;
    Check(phase()==viewer::Phase::SigningIn,"dismissing confirmation does not imply successful logon");
    state.loginComplete=true;
    Check(phase()==viewer::Phase::Verifying,"completed logon still waits for the exact child observation");
    state.desktopObserved=true;
    Check(phase()==viewer::Phase::Connected,"only the observed desktop produces connected presentation");
    state.closing=true;
    Check(phase()==viewer::Phase::Disconnecting,"disconnect supersedes all earlier successful events");
    state.finished=true;
    Check(phase()==viewer::Phase::NeedsAttention,"unverified restoration cannot display clean disconnection");
    state.baselineRestored=true;state.end.connectAttempted=true;
    Check(phase()==viewer::Phase::NeedsAttention,"completed restoration does not substitute for connection-end evidence");
    state.end.EndEvent();
    Check(phase()==viewer::Phase::Disconnected,"independently verified baseline and transport display disconnection");
    state.residualChild=true;
    Check(std::wstring(viewer::Present(state).message).find(L"may still be running")!=std::wstring::npos,
        "closing a viewer does not promise child logoff");
    state.operationFailed=true;
    Check(phase()==viewer::Phase::NeedsAttention,"restored settings do not hide a connection failure");
    Check(std::wstring(viewer::Present(state).message).find(L"Close this viewer, then refresh RETSAR Desktop")!=std::wstring::npos,
        "a failed connection provides an explicit retry path without initiating it");
    std::cout<<"Desktop viewer: "<<passed<<" passed, "<<failed<<" failed; fake COM and message values only, no windows, input or sessions.\n";
    return failed?1:0;
}

