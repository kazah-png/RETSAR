#include "child_session_experiment_io.hpp"
#include "child_session_activex.hpp"
#include "child_session_client_mode.hpp"
#include "child_session_client_diagnostics.hpp"
#include <algorithm>
#include <iostream>

namespace {
using namespace seats::lab::child;
using namespace seats::lab::child::experiment;
using namespace seats::lab::child::ax;
constexpr UINT StepMessage=WM_APP+41,StopMessage=WM_APP+42;
constexpr UINT_PTR PollTimer=41;
constexpr ULONGLONG ConnectionDeadlineMs=60000,DisconnectDeadlineMs=5000;
struct App {
    HWND window=nullptr;Site site;Events events;
    Com<IOleObject> object;Com<IDispatch> dispatch;Com<ExtendedSettings> extended;
    Com<IOleInPlaceObject> inplace;Com<IConnectionPoint> point;
    DWORD cookie=0;HANDLE receipt=INVALID_HANDLE_VALUE;
    Snapshot baseline{};Inventory inventory{};client::Request request{};
    bool enableAttempted=false,restoreAttempted=false,restored=false,finalBaselineRestored=false;
    bool connectAttempted=false,childPropertyVerified=false;
    bool connected=false,loggedIn=false,disconnected=false;
    bool observed=false,closing=false,finished=false,journalFailed=false;
    bool initializing=true,stopRequested=false,cleanupStarted=false,userCloseRequested=false;
    unsigned outboundDepth=0;
    ULONG observedId=MAXDWORD;
    HRESULT failure=S_OK;const char* failureStage="none";
    ULONGLONG start=0,closeStart=0,lastRecordedDispatch=0;
    const ULONGLONG diagnosticEpoch=GetTickCount64();
    diagnostics::StateObservation connectionObservation{};
    wchar_t status[512]=L"Preparing a local child session. Physical display/input isolation is not implemented.";
    ~App(){Cleanup();}
    bool Record(const char* stage,HRESULT result=S_OK,LONG value=0,bool valuePresent=false)noexcept {
        try {
            std::ostringstream out;out.imbue(std::locale::classic());
            out<<"{\"schema\":\"retsar.child-session-client.v1\",\"stage\":\""<<stage
               <<"\",\"elapsedMs\":"<<(GetTickCount64()-diagnosticEpoch)
               <<",\"hresult\":"<<static_cast<ULONG>(result)<<",\"value\":";
            if(valuePresent)out<<value;else out<<"null";
            out<<",\"connectionMode\":\""<<client::ModeName(request.mode)<<"\",\"requestedChildId\":";
            if(request.mode==client::Mode::ResumeChild)out<<request.target;else out<<"null";
            out<<",\"resumedExistingChild\":"<<((observed&&request.mode==client::Mode::ResumeChild)?"true":"false");
            out<<",\"connectedEvent\":"<<(connected?"true":"false")
               <<",\"loginCompleteEvent\":"<<(loggedIn?"true":"false")
               <<",\"observedChildSession\":"<<(observed?"true":"false")<<",\"observedChildId\":";
            if(observed)out<<observedId;else out<<"null";
            if(lastDispatch.serial!=lastRecordedDispatch) {
                out<<",\"dispatch\":{\"serial\":"<<lastDispatch.serial
                   <<",\"hresult\":"<<static_cast<ULONG>(lastDispatch.result)
                   <<",\"exceptionScode\":"<<static_cast<ULONG>(lastDispatch.exceptionCode)
                   <<",\"exceptionWcode\":"<<lastDispatch.exceptionWord
                   <<",\"deferredFillAttempted\":"<<(lastDispatch.deferred?"true":"false")
                   <<",\"deferredFillResult\":"<<static_cast<ULONG>(lastDispatch.deferredResult)<<'}';
                lastRecordedDispatch=lastDispatch.serial;
            }
            out<<",\"physicalSeatVerified\":false}";
            if(!Append(receipt,out.str()).succeeded){journalFailed=true;return false;}
            return true;
        }catch(...){journalFailed=true;return false;}
    }
    void RecordEvent(DISPID id,const DISPPARAMS* args)noexcept {
        try {
            if(!Append(receipt,diagnostics::JsonEvent(id,args,GetTickCount64()-diagnosticEpoch)).succeeded)
                journalFailed=true;
        }catch(...){journalFailed=true;}
    }
    void Status(const wchar_t* text)noexcept {
        wcsncpy_s(status,text,_TRUNCATE);if(window)InvalidateRect(window,nullptr,TRUE);
    }
    HRESULT Step(const char* name,HRESULT result)noexcept {
        if(!Record(name,result)&&SUCCEEDED(result))result=HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
        if(FAILED(result)&&SUCCEEDED(failure)){failure=result;failureStage=name;}
        return result;
    }
    void DeferStop(const char* name,HRESULT result)noexcept {
        stopRequested=true;
        if(SUCCEEDED(failure)){failure=result;failureStage=name;}
        if(window)PostMessageW(window,StopMessage,0,0);
    }
    static void Event(void* context,DISPID id,DISPPARAMS* args)noexcept {
        auto& app=*static_cast<App*>(context);LONG value=0;bool present=false;
        app.RecordEvent(id,args);
        if(args&&args->cArgs>=1&&args->rgvarg) {
            if(args->rgvarg[0].vt==VT_I4){value=args->rgvarg[0].lVal;present=true;}
            else if(args->rgvarg[0].vt==VT_I2){value=args->rgvarg[0].iVal;present=true;}
        }
        // Documented DISPIDs, never event strings or user names.
        // https://learn.microsoft.com/windows/win32/termserv/imstscaxevents-interface
        if(id==2){app.connected=true;app.Record("OnConnected");}
        else if(id==3){app.loggedIn=true;app.Record("OnLoginComplete");}
        else if(id==4) {
            app.disconnected=true;app.Record("OnDisconnected",S_OK,value,present);
            if(!app.closing)app.DeferStop("disconnected",E_FAIL);
        }else if(id==10) {
            app.Record("OnFatalError",S_OK,value,present);app.DeferStop("fatal-event",E_FAIL);
        }else if(id==5||id==8) {
            app.Record("fullscreen-request-rejected");
            if(id==5)app.DeferStop("unexpected-fullscreen",E_UNEXPECTED);
        }
        if(app.journalFailed)app.DeferStop("receipt-write",HRESULT_FROM_WIN32(ERROR_WRITE_FAULT));
        if(app.window)PostMessageW(app.window,StepMessage,0,0);
    }
    HRESULT VerifyChildProperty(bool write)noexcept {
        const HRESULT hr=VerifyChildMode(extended.value,write);
        childPropertyVerified=SUCCEEDED(hr);return hr;
    }
    HRESULT Prepare()noexcept {
        events.context=this;events.callback=Event;site.window=window;
        site.outboundDepth=&outboundDepth;site.closing=&closing;
        HRESULT hr=Step("CoCreateInstance",CoCreateInstance(ClientClass,nullptr,CLSCTX_INPROC_SERVER,
            IID_IOleObject,reinterpret_cast<void**>(object.put())));
        if(FAILED(hr))return hr;
        if(FAILED(hr=Step("SetClientSite",object->SetClientSite(&site))))return hr;
        Com<IPersistStreamInit> initial;
        if(FAILED(hr=Step("QueryPersistStreamInit",object->QueryInterface(IID_IPersistStreamInit,reinterpret_cast<void**>(initial.put())))))return hr;
        if(FAILED(hr=Step("InitNew",initial->InitNew())))return hr;
        if(FAILED(hr=Step("SetHostNames",object->SetHostNames(L"RETSAR Child session experiment",nullptr))))return hr;
        if(FAILED(hr=Step("SetContainedObject",OleSetContainedObject(object.value,TRUE))))return hr;
        if(FAILED(hr=Step("QueryDispatch",object->QueryInterface(IID_IDispatch,reinterpret_cast<void**>(dispatch.put())))))return hr;
        if(FAILED(hr=Step("QueryExtendedSettings",object->QueryInterface(__uuidof(ExtendedSettings),reinterpret_cast<void**>(extended.put())))))return hr;
        if(FAILED(hr=Step("ChildPropertySetAndVerified",VerifyChildProperty(true))))return hr;
        // Before connection, ignore server-generated pointer-position updates.
        // This does not disable keyboard or mouse input inside the child.
        // https://learn.microsoft.com/windows/win32/termserv/imsrdpextendedsettings-property
        if(FAILED(hr=Step("IgnoreServerMouseMovesSetAndVerified",
            VerifyExtendedTrue(extended.value,L"IgnoreServerGeneratedMouseMoves",true))))return hr;
        // Server is required by Connect. The checked child property redirects it
        // to the local child transport. No executable argument accepts a server.
        if(FAILED(hr=Step("ServerLocalhost",PutText(dispatch.value,L"Server",L"localhost"))))return hr;
        if(FAILED(hr=Step("DesktopWidth",PutNumber(dispatch.value,L"DesktopWidth",1024))))return hr;
        if(FAILED(hr=Step("DesktopHeight",PutNumber(dispatch.value,L"DesktopHeight",768))))return hr;
        if(FAILED(hr=Step("FullScreenFalse",PutNumber(dispatch.value,L"FullScreen",0,true))))return hr;
        Variant advanced;
        if(FAILED(hr=Step("AdvancedSettings",Invoke(dispatch.value,L"AdvancedSettings9",DISPATCH_PROPERTYGET,nullptr,&advanced.value))))return hr;
        if(advanced.value.vt!=VT_DISPATCH||!advanced.value.pdispVal)return Step("AdvancedSettingsType",DISP_E_TYPEMISMATCH);
        // Explicit per-connection CredSSP support; no authentication policy or
        // credentials are changed. Child mode remains independently mandatory.
        // https://learn.microsoft.com/windows/win32/termserv/imsrdpclientadvancedsettings6-enablecredsspsupport
        if(FAILED(hr=Step("CredSspEnable",PutNumber(advanced.value.pdispVal,L"EnableCredSspSupport",1,true))))return hr;
        Variant credSspReadback;
        if(FAILED(hr=Step("CredSspReadback",Invoke(advanced.value.pdispVal,L"EnableCredSspSupport",
            DISPATCH_PROPERTYGET,nullptr,&credSspReadback.value))))return hr;
        if(credSspReadback.value.vt!=VT_BOOL||credSspReadback.value.boolVal!=VARIANT_TRUE)
            return Step("CredSspReadbackType",E_UNEXPECTED);
        if(FAILED(hr=Step("CredSspVerified",S_OK)))return hr;
        if(FAILED(hr=Step("ContainerHandledFullScreen",PutNumber(advanced.value.pdispVal,L"ContainerHandledFullScreen",1))))return hr;
        if(FAILED(hr=Step("AutoReconnectFalse",PutNumber(advanced.value.pdispVal,L"EnableAutoReconnect",0,true))))return hr;
        for(const auto* setting:{L"RedirectDrives",L"RedirectPrinters",L"RedirectPorts",L"RedirectSmartCards",L"RedirectClipboard"})
            if(FAILED(hr=Step("ResourceRedirectionFalse",PutNumber(advanced.value.pdispVal,setting,0,true))))return hr;
        Com<IConnectionPointContainer> points;
        if(FAILED(hr=Step("QueryConnectionPoints",object->QueryInterface(IID_IConnectionPointContainer,reinterpret_cast<void**>(points.put())))))return hr;
        if(FAILED(hr=Step("FindEvents",points->FindConnectionPoint(ClientEvents,point.put()))))return hr;
        if(FAILED(hr=Step("AdviseEvents",point->Advise(static_cast<IDispatch*>(&events),&cookie))))return hr;
        if(FAILED(hr=Step("QueryInPlace",object->QueryInterface(IID_IOleInPlaceObject,reinterpret_cast<void**>(inplace.put())))))return hr;
        site.inplace=inplace.value;RECT area=site.Area();
        if(FAILED(hr=Step("InPlaceActivate",object->DoVerb(OLEIVERB_INPLACEACTIVATE,nullptr,&site,0,window,&area))))return hr;
        return Step("SetObjectRects",inplace->SetObjectRects(&area,&area));
    }
    bool EnableAndConnect() {
        const auto current=Query(SystemApi());
        const auto currentInventory=ReadInventory(baseline.caller.value,request.target);
        if(!client::AcceptSnapshot(request,current)||
           !client::AcceptInventory(request,baseline.caller.value,currentInventory.valid,
                currentInventory.rows.data(),currentInventory.rows.size())||
           current.caller.value!=baseline.caller.value||current.console.value!=baseline.console.value) {
            Step("pre-enable-guard",HRESULT_FROM_WIN32(ERROR_BUSY));return false;
        }
        if(!Record("enable-intent"))return false;
        enableAttempted=true;
        const auto enabled=toggle_detail::Set(SystemToggleApi(),TRUE);
        const auto read=toggle_detail::Read(SystemToggleApi());
        Step("enable-result",enabled.succeeded?S_OK:HRESULT_FROM_WIN32(enabled.error?enabled.error:ERROR_GEN_FAILURE));
        Record("enable-last-error-observed",S_OK,static_cast<LONG>(enabled.error),!enabled.succeeded);
        if(!enabled.succeeded||!read.succeeded||!read.value) {
            Step("enable-readback",read.succeeded?E_UNEXPECTED:HRESULT_FROM_WIN32(read.error?read.error:ERROR_GEN_FAILURE));
            return false;
        }
        if(FAILED(Step("enable-readback",S_OK))||FAILED(Step("pre-connect-child-property",VerifyChildProperty(false)))||
           FAILED(Step("pre-connect-ignore-server-mouse",
                VerifyExtendedTrue(extended.value,L"IgnoreServerGeneratedMouseMoves",false))))return false;
        const auto beforeConnect=Query(SystemApi());
        const auto connectInventory=ReadInventory(baseline.caller.value,request.target);
        if(!client::AcceptSnapshot(request,beforeConnect,true)||
           !client::AcceptInventory(request,baseline.caller.value,connectInventory.valid,
                connectInventory.rows.data(),connectInventory.rows.size())||
           beforeConnect.caller.value!=baseline.caller.value||beforeConnect.console.value!=baseline.console.value) {
            Step("pre-connect-session-relation",E_ACCESSDENIED);return false;
        }
        if(!Record("connect-intent")||stopRequested)return false;
        start=GetTickCount64();
        const auto connection=ConnectChildOnly(extended.value,dispatch.value,true,true,&stopRequested);
        connectAttempted=connection.attempted;
        if(FAILED(Step("connect-gate-child-property",connection.propertyResult)))return false;
        if(!connectAttempted||FAILED(Step("Connect",connection.connectResult)))return false;
        Status(L"Child session experiment - connecting (60 second limit). Physical isolation is not implemented.");
        return true;
    }
    bool NewActiveChild(ULONG id)noexcept {
        if(!client::AcceptObservedId(request,baseline.caller.value,id,inventory.rows.data(),inventory.rows.size()))return false;
        PWTS_SESSION_INFOW rows=nullptr;DWORD count=0;
        if(!WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE,0,1,&rows,&count))return false;
        bool found=false;
        if(rows&&count<=1024)for(DWORD i=0;i<count;++i)
            if(rows[i].SessionId==id&&rows[i].State==WTSActive)found=true;
        if(rows)WTSFreeMemory(rows);return found;
    }
    void Poll()noexcept {
        if(finished||initializing||cleanupStarted||outboundDepth)return;
        if(closing) {
            LONG state=-1;
            ++outboundDepth;const HRESULT hr=GetNumber(dispatch.value,L"Connected",state);--outboundDepth;
            if(disconnected||(SUCCEEDED(hr)&&state==0)||GetTickCount64()-closeStart>=DisconnectDeadlineMs)Cleanup();
            return;
        }
        if(!connectAttempted)return;
        LONG state=-1;HRESULT connectedResult=E_PENDING;
        if(!observed) {
            ++outboundDepth;connectedResult=GetNumber(dispatch.value,L"Connected",state);--outboundDepth;
            if(connectionObservation.Changed(connectedResult,state))
                Record("ConnectedState",connectedResult,state,SUCCEEDED(connectedResult));
        }
        if(stopRequested){BeginStop(failureStage,failure);return;}
        if(connected&&loggedIn&&SUCCEEDED(connectedResult)&&state==1&&!observed) {
            const auto current=Query(SystemApi());
            if(current.child.succeeded&&current.child.value!=MAXDWORD&&current.child.value!=baseline.caller.value&&
               current.caller.succeeded&&current.caller.value==baseline.caller.value&&
               current.console.value==baseline.console.value&&NewActiveChild(current.child.value)) {
                observed=true;observedId=current.child.value;
                if(request.mode==client::Mode::ResumeChild) {
                    Record("existing-child-resumed",S_OK,static_cast<LONG>(observedId),true);
                    Status(L"Child session experiment - existing child session resumed. No new session or physical isolation claimed.");
                }else {
                    Record("active-child-observed",S_OK,static_cast<LONG>(observedId),true);
                    Status(L"Child session experiment - native child session observed. Windowed access; no physical isolation.");
                }
            }
        }
        if(journalFailed){BeginStop("receipt-write",HRESULT_FROM_WIN32(ERROR_WRITE_FAULT));return;}
        if(!observed&&GetTickCount64()-start>=ConnectionDeadlineMs)BeginStop("connection-deadline",HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    }
    void BeginStop(const char* stage,HRESULT reason)noexcept {
        if(finished||closing||cleanupStarted)return;
        if(FAILED(reason)&&SUCCEEDED(failure)){failure=reason;failureStage=stage;}
        if(initializing||outboundDepth){stopRequested=true;return;}
        closing=true;closeStart=GetTickCount64();
        Status(L"Closing only this client and restoring the original child-session setting...");Record(stage,reason);
        if(connectAttempted&&dispatch) {
            ++outboundDepth;const HRESULT hr=Invoke(dispatch.value,L"Disconnect",DISPATCH_METHOD);--outboundDepth;
            Record("DisconnectOwnControl",hr);
        }else Cleanup();
    }
    void Cleanup()noexcept {
        if(finished||cleanupStarted)return;
        if(outboundDepth){stopRequested=true;return;}
        cleanupStarted=true;closing=true;
        LONG extendedReason=0;
        if(dispatch) {
            const HRESULT hr=GetNumber(dispatch.value,L"ExtendedDisconnectReason",extendedReason);
            Record("ExtendedDisconnectReason",hr,extendedReason,SUCCEEDED(hr));
        }
        // Only this owned control is closed. No session-directed API or logoff.
        if(point&&cookie){const auto hr=point->Unadvise(cookie);cookie=0;Record("UnadviseEvents",hr);}
        point.reset();events.callback=nullptr;
        if(object){Record("CloseOwnObject",object->Close(OLECLOSE_NOSAVE));object->SetClientSite(nullptr);}
        site.inplace=nullptr;site.active.reset();inplace.reset();extended.reset();dispatch.reset();object.reset();
        if(enableAttempted&&!restoreAttempted) {
            restoreAttempted=true;Record("restore-intent");
            const auto restore=toggle_detail::Set(SystemToggleApi(),FALSE);
            const auto read=toggle_detail::Read(SystemToggleApi());
            restored=restore.succeeded&&read.succeeded&&!read.value;
            Record("restore-result",restore.succeeded?S_OK:HRESULT_FROM_WIN32(restore.error?restore.error:ERROR_GEN_FAILURE));
            Record("restore-last-error-observed",S_OK,static_cast<LONG>(restore.error),!restore.succeeded);
            Record("restore-readback",read.succeeded?S_OK:HRESULT_FROM_WIN32(read.error?read.error:ERROR_GEN_FAILURE),read.value?1:0,read.succeeded);
        }
        const auto final=Query(SystemApi());
        const bool baselineRestored=final.enabled.succeeded&&!final.enabled.value&&(!enableAttempted||restored);
        finalBaselineRestored=baselineRestored;
        try {
            std::ostringstream out;out<<"{\"schema\":\"retsar.child-session-client-final.v1\","
                <<"\"connectionMode\":\""<<client::ModeName(request.mode)<<"\","
                <<"\"resumedExistingChild\":"<<((observed&&request.mode==client::Mode::ResumeChild)?"true":"false")
                <<",\"observedChildSession\":"<<(observed?"true":"false")
                <<",\"restoreAttempted\":"<<(restoreAttempted?"true":"false")
                <<",\"baselineRestored\":"<<(baselineRestored?"true":"false")
                <<",\"failureStage\":\""<<failureStage<<"\",\"failureHresult\":"<<static_cast<ULONG>(failure)
                <<",\"journalFailed\":"<<(journalFailed?"true":"false")
                <<",\"finalStatus\":"<<ToJson(final)<<",\"logoffAttempted\":false,\"physicalSeatVerified\":false}";
            if(!Append(receipt,out.str()).succeeded)journalFailed=true;
        }catch(...){journalFailed=true;}
        finished=true;if(window)KillTimer(window,PollTimer);
        const bool residual=final.child.succeeded&&final.child.value!=MAXDWORD;
        if(!baselineRestored)Status(L"Experiment stopped. Original setting NOT verified restored. Review the private receipt before retrying.");
        else if(residual)Status(L"Client closed; setting restored. A child session ID remains. No session was logged off. Review the receipt.");
        else if(observed)Status(L"Child session observed; client closed and setting restored. Session termination and physical isolation are unverified.");
        else {
            swprintf_s(status,L"Child session experiment stopped. HRESULT 0x%08lX. Original setting restored; review private receipt.",static_cast<ULONG>(failure));
            if(window)InvalidateRect(window,nullptr,TRUE);
        }
        // Destruction is queued outside the COM cleanup stack. User-requested
        // close needs one click; automatic connection failure can show results.
        if(userCloseRequested&&window)PostMessageW(window,WM_CLOSE,0,0);
    }
};


LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        app->window=window;app->site.window=window;
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));
    }
    if(!app)return DefWindowProcW(window,message,wp,lp);
    try {
        switch(message) {
        case WM_PAINT: {
            PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT bounds{};
            GetClientRect(window,&bounds);bounds.bottom=60;
            FillRect(dc,&bounds,reinterpret_cast<HBRUSH>(COLOR_WINDOW+1));
            SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(28,35,45));
            bounds.left+=12;bounds.top+=10;bounds.right-=12;
            DrawTextW(dc,app->status,-1,&bounds,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
            EndPaint(window,&paint);return 0;
        }
        case WM_SIZE:
            if(app->inplace&&!app->initializing&&!app->closing&&!app->cleanupStarted&&!app->outboundDepth) {
                RECT area=app->site.Area();
                ++app->outboundDepth;app->inplace->SetObjectRects(&area,&area);--app->outboundDepth;
                if(app->stopRequested)app->BeginStop(app->failureStage,app->failure);
            }
            return 0;
        case WM_TIMER:if(wp==PollTimer)app->Poll();return 0;
        case StepMessage:app->Poll();return 0;
        case StopMessage:app->BeginStop(app->failureStage,app->failure);return 0;
        case WM_CLOSE:
            if(app->finished){DestroyWindow(window);return 0;}
            app->userCloseRequested=true;app->BeginStop("user-close",S_OK);return 0;
        case WM_DESTROY:
            app->Cleanup();app->window=nullptr;PostQuitMessage(0);return 0;
        }
    }catch(...){app->BeginStop("window-exception",E_UNEXPECTED);return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
BOOL CALLBACK ChooseMonitor(HMONITOR monitor,HDC,LPRECT,LPARAM context) {
    auto* chosen=reinterpret_cast<RECT*>(context);MONITORINFO info{sizeof(info)};
    if(!GetMonitorInfoW(monitor,&info))return TRUE;
    if(chosen->right==0||!(info.dwFlags&MONITORINFOF_PRIMARY))*chosen=info.rcWork;
    return (info.dwFlags&MONITORINFOF_PRIMARY)?TRUE:FALSE;
}
bool TokenIdentity(std::ostringstream& out) {
    Handle token;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.value))return false;
    TOKEN_STATISTICS statistics{};TOKEN_ELEVATION elevation{};DWORD session=MAXDWORD,bytes=0;
    if(!GetTokenInformation(token.value,TokenStatistics,&statistics,sizeof(statistics),&bytes)||
       !GetTokenInformation(token.value,TokenSessionId,&session,sizeof(session),&bytes)||
       !GetTokenInformation(token.value,TokenElevation,&elevation,sizeof(elevation),&bytes))return false;
    FILETIME created{},exited{},kernel{},user{};
    if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))return false;
    out<<"\"ownerProcessId\":"<<GetCurrentProcessId()<<",\"ownerCreationTime\":"
       <<((static_cast<ULONGLONG>(created.dwHighDateTime)<<32)|created.dwLowDateTime)
       <<",\"tokenSessionId\":"<<session<<",\"tokenElevated\":"<<(elevation.TokenIsElevated?"true":"false")
       <<",\"authenticationIdLow\":"<<statistics.AuthenticationId.LowPart
       <<",\"authenticationIdHigh\":"<<statistics.AuthenticationId.HighPart;
    return true;
}
}

int wmain(int argc,wchar_t* argv[]) {
    if(argc==1||(argc==2&&std::wcscmp(argv[1],L"--help")==0)) {
        std::cout<<"RETSAR Child session experiment\n"
            <<"Usage: RETSAR-child-session-client.exe --connect-child --receipt <new.jsonl>\n"
            <<"       RETSAR-child-session-client.exe --resume-child-session <id> --receipt <new.jsonl>\n"
            <<"Explicit mode hosts one local Remote Desktop ActiveX child session.\n"
            <<"Receipt must be new under D:\\RETSAR-Lab\\RETSAR\\private\\.\n"
            <<"Connection deadline: 60 seconds. An observed connection stays open until closed.\n"
            <<"Closing disconnects only this client and restores the original disabled setting.\n"
            <<"Resume requires an exact existing disconnected child; it never claims new creation.\n"
            <<"Remaining child IDs are reported, never logged off automatically.\n"
            <<"No ordinary RDP fallback, credentials, fullscreen or physical device assignment.\n"
            <<"Help makes no COM, WTS, window or setting changes.\n";return 0;
    }
    client::Request request{};const wchar_t* receiptArgument=nullptr;
    if(!client::ParseModeArgs(argc,argv,request,receiptArgument)) {
        std::cerr<<"Invalid arguments; no session queries or changes.\n";return 64;
    }
    try {
        std::wstring path;
        if(!ValidReceiptPath(receiptArgument,path)){std::cerr<<"Receipt path rejected.\n";return 64;}
        Handle mutex;mutex.value=CreateMutexW(nullptr,TRUE,L"Local\\RETSAR.ChildSessionToggle");
        const DWORD mutexError=GetLastError();
        if(!mutex.value||mutexError==ERROR_ALREADY_EXISTS){if(!mutex.value)mutex.value=INVALID_HANDLE_VALUE;return 3;}
        const auto baseline=Query(SystemApi());
        if(!client::AcceptSnapshot(request,baseline)){std::cout<<ToJson(baseline)<<'\n';return 3;}
        const auto inventory=ReadInventory(baseline.caller.value,request.target);
        if(!client::AcceptInventory(request,baseline.caller.value,inventory.valid,inventory.rows.data(),inventory.rows.size())) {
            std::cout<<InventoryJson(inventory)<<'\n';return 3;
        }
        Handle receipt;
        receipt.value=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(receipt.value==INVALID_HANDLE_VALUE){std::cerr<<"Receipt creation failed.\n";return 4;}
        BY_HANDLE_FILE_INFORMATION fileInfo{};wchar_t finalPath[32768]{};
        const DWORD finalSize=GetFinalPathNameByHandleW(receipt.value,finalPath,static_cast<DWORD>(std::size(finalPath)),
            FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        const std::wstring expected=L"\\\\?\\"+path;
        if(GetFileType(receipt.value)!=FILE_TYPE_DISK||!GetFileInformationByHandle(receipt.value,&fileInfo)||
           (fileInfo.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||
           !finalSize||finalSize>=std::size(finalPath)||_wcsicmp(finalPath,expected.c_str())!=0)return 4;
        std::ostringstream initial;initial<<"{\"schema\":\"retsar.child-session-client-owner.v1\",";
        if(!TokenIdentity(initial))return 4;
        initial<<",\"connectionMode\":\""<<client::ModeName(request.mode)<<"\",\"requestedChildId\":";
        if(request.mode==client::Mode::ResumeChild)initial<<request.target;else initial<<"null";
        initial<<",\"baseline\":"<<ToJson(baseline)<<",\"inventory\":"<<InventoryJson(inventory)
            <<",\"childAbsenceProven\":"<<(ClassifyChild(baseline)==ChildState::Absent?"true":"false")
            <<",\"connectionDeadlineMs\":"<<ConnectionDeadlineMs<<",\"physicalSeatVerified\":false}";
        if(!Append(receipt.value,initial.str()).succeeded)return 4;
        const HRESULT initialized=OleInitialize(nullptr);
        if(FAILED(initialized)){std::cerr<<"OLE initialization failed.\n";return 5;}
        struct OleLifetime{~OleLifetime(){OleUninitialize();}} oleLifetime;
        App app;app.receipt=receipt.value;app.baseline=baseline;app.inventory=inventory;app.request=request;
        if(request.mode==client::Mode::ResumeChild)
            app.Status(L"Preparing to resume the verified existing child session. No new session is being requested.");
        WNDCLASSEXW definition{sizeof(definition)};
        definition.lpfnWndProc=WindowProc;definition.hInstance=GetModuleHandleW(nullptr);
        definition.hCursor=LoadCursorW(nullptr,IDC_ARROW);definition.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
        definition.lpszClassName=L"RETSAR.ChildSessionExperiment";
        if(!RegisterClassExW(&definition))return 5;
        RECT work{};EnumDisplayMonitors(nullptr,nullptr,ChooseMonitor,reinterpret_cast<LPARAM>(&work));
        if(work.right<=work.left||work.bottom<=work.top)return 5;
        const int width=std::min(1120,static_cast<int>(work.right-work.left)-32);
        const int height=std::min(860,static_cast<int>(work.bottom-work.top)-48);
        HWND window=CreateWindowExW(0,definition.lpszClassName,L"RETSAR - Child session experiment",
            WS_OVERLAPPEDWINDOW,work.left+16,work.top+24,width,height,nullptr,nullptr,definition.hInstance,&app);
        if(!window)return 5;
        ShowWindow(window,SW_SHOWNOACTIVATE);
        // A launcher's STARTUPINFO can override the first ShowWindow argument.
        // The second call explicitly shows this owned window without activation.
        ShowWindow(window,SW_SHOWNOACTIVATE);
        if(!SetTimer(window,PollTimer,250,nullptr))app.BeginStop("timer-create",HRESULT_FROM_WIN32(GetLastError()));
        else {
            const HRESULT prepared=app.Prepare();
            if(FAILED(prepared))app.BeginStop(app.failureStage,prepared);
            else if(!app.stopRequested&&SUCCEEDED(app.failure)&&!app.EnableAndConnect())
                app.BeginStop(app.failureStage,FAILED(app.failure)?app.failure:E_FAIL);
        }
        app.initializing=false;
        if(app.stopRequested||FAILED(app.failure))app.BeginStop(app.failureStage,app.failure);
        MSG message{};BOOL messageResult=0;
        while((messageResult=GetMessageW(&message,nullptr,0,0))>0) {
            HRESULT translated=S_FALSE;
            if(app.site.active&&GetForegroundWindow()==window) {
                ++app.outboundDepth;translated=app.site.active->TranslateAccelerator(&message);--app.outboundDepth;
                if(app.stopRequested&&!app.closing)app.BeginStop(app.failureStage,app.failure);
            }
            if(translated==S_OK)continue;
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        if(messageResult<0) {
            const DWORD messageError=GetLastError();
            app.Step("GetMessage",HRESULT_FROM_WIN32(messageError?messageError:ERROR_GEN_FAILURE));
        }
        if(!app.finished)app.BeginStop("message-loop-ended",app.failure);
        app.Cleanup();
        return app.observed&&app.finalBaselineRestored&&!app.journalFailed&&SUCCEEDED(app.failure)?0:2;
    }catch(...){std::cerr<<"Child-session experiment failed; inspect the private receipt.\n";return 5;}
}

