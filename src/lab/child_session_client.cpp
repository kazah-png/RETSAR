#include "child_session_experiment_io.hpp"
#include "child_session_activex.hpp"
#include "child_session_client_mode.hpp"
#include "child_session_setting_policy.hpp"
#include "child_session_client_diagnostics.hpp"
#include "child_session_viewer_state.hpp"
#include "child_session_viewport.hpp"
#include "child_session_presentation.hpp"
#include "child_session_resolution.hpp"
#include "child_session_navigation.hpp"
#include <dbt.h>
#include <algorithm>
#include <iostream>

namespace {
using namespace seats::lab::child;
using namespace seats::lab::child::experiment;
using namespace seats::lab::child::ax;
constexpr UINT StepMessage=WM_APP+41,StopMessage=WM_APP+42;
constexpr UINT_PTR PollTimer=41,ResolutionTimer=42;
constexpr int SizingButton=1040,PresentationButton=1041,ResolutionButton=1042;
constexpr UINT ViewSyncMessage=WM_APP+44;
constexpr ULONGLONG ConnectionDeadlineMs=60000,DisconnectDeadlineMs=5000;
struct NativePresentation final:presentation::WindowApi {
    HWND window;
    explicit NativePresentation(HWND value)noexcept:window(value){}
    bool Inspect(presentation::Snapshot& out,DWORD& error)noexcept override {
        SetLastError(ERROR_SUCCESS);const auto style=GetWindowLongPtrW(window,GWL_STYLE);
        error=GetLastError();if(!style&&error)return false;
        out.style=static_cast<LONG_PTR>(static_cast<DWORD>(style));
        if(!GetWindowRect(window,&out.rect)){error=GetLastError();return false;}
        MONITORINFO monitor{sizeof(monitor)};
        if(!GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor)){error=GetLastError();return false;}
        out.work=monitor.rcWork;error=ERROR_SUCCESS;return true;
    }
    bool Style(LONG_PTR value,DWORD& error)noexcept override {
        SetLastError(ERROR_SUCCESS);const auto previous=SetWindowLongPtrW(window,GWL_STYLE,value);
        error=GetLastError();return previous!=0||error==ERROR_SUCCESS;
    }
    bool Place(const RECT& area,DWORD& error)noexcept override {
        if(!presentation::Valid(area)){error=ERROR_INVALID_DATA;return false;}
        const bool ok=SetWindowPos(window,nullptr,area.left,area.top,area.right-area.left,area.bottom-area.top,
            SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER)!=FALSE;
        error=ok?ERROR_SUCCESS:GetLastError();return ok;
    }
};
presentation::Catalog PresentationMonitors()noexcept {
    presentation::Catalog result;
    const auto callback=[](HMONITOR monitor,HDC,LPRECT,LPARAM context)->BOOL {
        auto& out=*reinterpret_cast<presentation::Catalog*>(context);MONITORINFO info{sizeof(info)};
        if(out.monitors.size()>=64||!GetMonitorInfoW(monitor,&info))return FALSE;
        try{out.monitors.push_back({reinterpret_cast<std::uintptr_t>(monitor),info.rcMonitor,info.rcWork,(info.dwFlags&MONITORINFOF_PRIMARY)!=0});}
        catch(...){return FALSE;}return TRUE;
    };
    result.complete=EnumDisplayMonitors(nullptr,nullptr,callback,reinterpret_cast<LPARAM>(&result))!=FALSE;
    return result;
}
struct NativeNavigation final:navigation::WindowApi {
    HWND frame;HWND buttons[3];
    NativeNavigation(HWND owner,HWND presentation,HWND resolution,HWND sizing)noexcept:
        frame(owner),buttons{presentation,resolution,sizing}{}
    bool ForegroundOwned()noexcept override{return frame&&GetForegroundWindow()==frame;}
    HWND CurrentFocus()noexcept override{return GetFocus();}
    bool OwnedChild(HWND target)noexcept override {
        if(!frame||!target||!IsWindow(frame)||!IsWindow(target)||target==frame||!IsChild(frame,target))return false;
        DWORD ownerProcess=0,targetProcess=0;
        const DWORD ownerThread=GetWindowThreadProcessId(frame,&ownerProcess);
        const DWORD targetThread=GetWindowThreadProcessId(target,&targetProcess);
        return ownerThread==GetCurrentThreadId()&&targetThread==ownerThread&&
            ownerProcess==GetCurrentProcessId()&&targetProcess==ownerProcess;
    }
    bool Available(HWND target)noexcept override{return IsWindowVisible(target)&&IsWindowEnabled(target);}
    bool Toolbar(HWND target)noexcept override {
        return target&&(target==buttons[0]||target==buttons[1]||target==buttons[2]);
    }
    bool Descendant(HWND parent,HWND child)noexcept override{return IsChild(parent,child)!=FALSE;}
    DWORD Set(HWND target)noexcept override {
        SetLastError(ERROR_SUCCESS);const HWND previous=SetFocus(target);
        return previous?ERROR_SUCCESS:GetLastError();
    }
};
struct App {
    HWND window=nullptr,sizingButton=nullptr,presentationButton=nullptr,resolutionButton=nullptr;Site site;Events events;
    resolution::Pending resolutionPending;resolution::Result resolutionCallResult;bool resolutionCallCompleted=false;
    resolution::Phase resolutionReported=resolution::Phase::Idle;
    bool matchQueued=false,matchPollPending=false,matchingResolution=false,resolutionTimerArmed=false,resolutionPropertiesLogged=false;
    LONG requestedViewportWidth=0,requestedViewportHeight=0;
    DWORD resolutionTimerError=ERROR_SUCCESS;
    wchar_t resolutionNotice[192]{};
    presentation::State screen;presentation::Shortcut presentationShortcut;
    navigation::Keys navigationKeys;navigation::Action navigationPending=navigation::Action::None;
    HWND navigationOrigin=nullptr;bool navigating=false;
    bool enterPresentationPending=false,exitPresentationPending=false,topologyPending=false,viewportPending=false,viewSyncQueued=false,drainingView=false;
    wchar_t presentationNotice[192]{};
    bool sizingKnown=false,fitDesktop=false,changingSizing=false;HRESULT sizingResult=E_PENDING;
    Com<IOleObject> object;Com<IDispatch> dispatch;Com<ExtendedSettings> extended;
    Com<IOleInPlaceObject> inplace;Com<IConnectionPoint> point;
    DWORD cookie=0;HANDLE receipt=INVALID_HANDLE_VALUE;
    Snapshot baseline{};Inventory inventory{};client::Request request{};
    bool enableAttempted=false,restoreAttempted=false,restored=false,finalBaselineRestored=false,finalBaselinePreserved=false;
    bool connectAttempted=false,childPropertyVerified=false;
    bool connected=false,loggedIn=false,disconnected=false;
    bool connectingEvent=false,authenticationPrompt=false,residualChild=false;
    bool observed=false,closing=false,finished=false,journalFailed=false;
    bool initializing=true,stopRequested=false,cleanupStarted=false,userCloseRequested=false,destroyQueued=false;
    unsigned outboundDepth=0;
    ULONG observedId=MAXDWORD;
    HRESULT failure=S_OK;const char* failureStage="none";
    ULONGLONG start=0,closeStart=0,lastRecordedDispatch=0;
    const ULONGLONG diagnosticEpoch=GetTickCount64();
    diagnostics::StateObservation connectionObservation{};
    bool frameActive=false,frameActivationPending=false,pollTimerArmed=false;
    UINT dpi=96;HFONT titleFont=nullptr,bodyFont=nullptr,smallFont=nullptr;HICON brandIcon=nullptr;
    wchar_t status[512]=L"Preparing your desktop...";
    ~App(){Cleanup();if(titleFont)DeleteObject(titleFont);if(bodyFont)DeleteObject(bodyFont);if(smallFont)DeleteObject(smallFont);}
    bool Record(const char* stage,HRESULT result=S_OK,LONG value=0,bool valuePresent=false)noexcept {
        try {
            std::ostringstream out;out.imbue(std::locale::classic());
            out<<"{\"schema\":\"retsar.child-session-client.v1\",\"stage\":\""<<stage
               <<"\",\"elapsedMs\":"<<(GetTickCount64()-diagnosticEpoch)
               <<",\"hresult\":"<<static_cast<ULONG>(result)<<",\"value\":";
            if(valuePresent)out<<value;else out<<"null";
            out<<",\"connectionMode\":\""<<client::ModeName(request)<<"\",\"requestedChildId\":";
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
    int Scale(int value)const noexcept{return MulDiv(value,static_cast<int>(dpi),96);}
    RECT Header()const noexcept{RECT r{};if(window)GetClientRect(window,&r);r.bottom=site.headerHeight;return r;}
    void InvalidateHeader()noexcept{if(window){const RECT r=Header();InvalidateRect(window,&r,FALSE);}}
    void Status(const wchar_t* text)noexcept {
        if(std::wcscmp(status,text)==0)return;
        wcsncpy_s(status,text,_TRUNCATE);InvalidateHeader();
    }
    viewer::Presentation Presentation()const noexcept {
        auto result=viewer::Present({connectAttempted||connectingEvent,authenticationPrompt,connected,
            loggedIn,observed,closing,finished,finalBaselineRestored||finalBaselinePreserved,FAILED(failure)||journalFailed,residualChild});
        if(request.preserveEnabled){
            if(finished){
                if(!finalBaselinePreserved)result.message=L"Viewer disconnected. The existing Windows setting could not be verified. Check the diagnostic report.";
                else if(FAILED(failure)||journalFailed)result.message=L"Connection stopped. The existing Windows setting remains enabled. Close this viewer, then refresh RETSAR Desktop.";
                else result.message=residualChild?L"Viewer disconnected. The existing Windows setting remains enabled; your desktop may still be running.":L"Viewer disconnected. The existing Windows setting remains enabled.";
            }else if(closing)result.message=L"Disconnecting this viewer. Preserving the existing Windows setting...";
        }
        return result;
    }
    void LayoutSizing()noexcept {
        if(!sizingButton||!window)return;
        RECT client{};GetClientRect(window,&client);
        const bool compact=screen.saved;
        ShowWindow(sizingButton,client.right>=Scale(600)?SW_SHOWNOACTIVATE:SW_HIDE);
        MoveWindow(sizingButton,std::max(0,static_cast<int>(client.right)-Scale(194)),Scale(compact?10:82),Scale(170),Scale(30),TRUE);
        SendMessageW(sizingButton,WM_SETFONT,reinterpret_cast<WPARAM>(bodyFont),TRUE);
        SetWindowTextW(sizingButton,sizingKnown&&fitDesktop?L"Actual size":L"Fit desktop");
        EnableWindow(sizingButton,observed&&!initializing&&!closing&&!finished&&!cleanupStarted&&!changingSizing&&!matchingResolution&&resolutionPending.phase!=resolution::Phase::Awaiting);
        if(presentationButton){
            MoveWindow(presentationButton,Scale(compact?16:24),Scale(compact?10:82),compact?std::max(1,std::min(Scale(190),static_cast<int>(client.right)-Scale(32))):Scale(190),Scale(30),TRUE);
            SendMessageW(presentationButton,WM_SETFONT,reinterpret_cast<WPARAM>(bodyFont),TRUE);
            SetWindowTextW(presentationButton,compact?L"Back to window":L"Use second monitor");
            EnableWindow(presentationButton,screen.saved||(!initializing&&observed&&!closing&&!finished&&!cleanupStarted));
        }
        LayoutResolution();
    }
    bool ResolutionReady()const noexcept {
        return observed&&connected&&loggedIn&&!disconnected&&!initializing&&!closing&&!finished&&!cleanupStarted&&!stopRequested;
    }
    void LayoutResolution()noexcept {
        if(!resolutionButton||!window)return;
        RECT client{};GetClientRect(window,&client);const bool compact=screen.saved;
        ShowWindow(resolutionButton,client.right>=Scale(400)?SW_SHOWNOACTIVATE:SW_HIDE);
        MoveWindow(resolutionButton,Scale(compact?222:226),Scale(compact?10:82),Scale(140),Scale(30),TRUE);
        SendMessageW(resolutionButton,WM_SETFONT,reinterpret_cast<WPARAM>(bodyFont),TRUE);
        SetWindowTextW(resolutionButton,resolutionPending.phase==resolution::Phase::Awaiting?L"Matching...":L"Match screen");
        EnableWindow(resolutionButton,ResolutionReady()&&!matchQueued&&!matchingResolution&&!changingSizing&&
            resolutionPending.phase!=resolution::Phase::Awaiting&&screen.mode!=presentation::Mode::Recovery);
    }
    void UpdateStatus()noexcept{Status(Presentation().message);LayoutSizing();}
    void ApplySizing(bool fit)noexcept {
        changingSizing=true;LayoutSizing();
        const auto result=viewport::SetFit(dispatch.value,fit,outboundDepth);
        sizingKnown=result.known;fitDesktop=result.fit;sizingResult=result.result;
        changingSizing=false;
        Record(fit?"SmartSizingFit":"SmartSizingActualSize",result.result,result.fit?1:0,result.known);
        LayoutSizing();InvalidateHeader();
    }
    void ToggleSizing()noexcept {
        if(!observed||initializing||closing||finished||cleanupStarted||outboundDepth||changingSizing||matchingResolution||resolutionPending.phase==resolution::Phase::Awaiting)return;
        ApplySizing(!sizingKnown||!fitDesktop);
        if(stopRequested||journalFailed)BeginStop(journalFailed?"receipt-write":failureStage,
            journalFailed?HRESULT_FROM_WIN32(ERROR_WRITE_FAULT):failure);
    }
    void ConfigureAppearance(UINT value)noexcept {
        dpi=value?value:96;site.headerHeight=Scale(screen.saved?52:126);
        // Shared icon uses the project's canonical resource; Windows owns its lifetime.
        brandIcon=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101),
            IMAGE_ICON,Scale(32),Scale(32),LR_SHARED));
        const auto makeFont=[&](int size,int weight)noexcept {
            return CreateFontW(-Scale(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        };
        HFONT title=makeFont(23,FW_SEMIBOLD),body=makeFont(13,FW_NORMAL),caption=makeFont(11,FW_SEMIBOLD);
        if(title){if(titleFont)DeleteObject(titleFont);titleFont=title;}
        if(body){if(bodyFont)DeleteObject(bodyFont);bodyFont=body;}
        if(caption){if(smallFont)DeleteObject(smallFont);smallFont=caption;}
        LayoutSizing();InvalidateHeader();
    }
    void PaintHeader(HDC dc)noexcept {
        const int saved=SaveDC(dc);RECT bounds=Header();
        SelectObject(dc,GetStockObject(DC_BRUSH));SelectObject(dc,GetStockObject(NULL_PEN));
        SetDCBrushColor(dc,RGB(11,16,32));FillRect(dc,&bounds,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        SetBkMode(dc,TRANSPARENT);
        const auto font=[&](HFONT selected)noexcept{SelectObject(dc,selected?selected:GetStockObject(DEFAULT_GUI_FONT));};
        const auto text=[&](const wchar_t* value,RECT r,UINT flags)noexcept{DrawTextW(dc,value,-1,&r,flags|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);};
        if(screen.saved){
            font(bodyFont);SetTextColor(dc,RGB(155,173,206));
            const wchar_t* notice=closing||finished?status:presentationNotice[0]?presentationNotice:resolutionNotice[0]?resolutionNotice:L"Screen presentation - Ctrl+Alt+F12 returns to window";
            const int noticeLeft=Scale(bounds.right>=Scale(400)?380:222);
            const int noticeRight=bounds.right-Scale(bounds.right>=Scale(600)?210:24);
            if(noticeRight>noticeLeft)text(notice,{noticeLeft,Scale(10),noticeRight,Scale(40)},DT_LEFT);
            if(saved)RestoreDC(dc,saved);return;
        }
        if(brandIcon)DrawIconEx(dc,Scale(24),Scale(11),brandIcon,Scale(32),Scale(32),0,nullptr,DI_NORMAL);
        const int brandOffset=brandIcon?44:0;
        font(titleFont);SetTextColor(dc,RGB(239,245,255));
        text(L"RETSAR",{Scale(24+brandOffset),Scale(10),Scale(145+brandOffset),Scale(43)},DT_LEFT);
        font(smallFont);SetTextColor(dc,RGB(93,228,199));
        text(L"DESKTOP VIEWER",{Scale(155+brandOffset),Scale(16),Scale(320+brandOffset),Scale(42)},DT_LEFT);
        const bool badgeVisible=bounds.right>=Scale(660);
        const int statusRight=badgeVisible?bounds.right-Scale(238):bounds.right-Scale(24);
        font(bodyFont);SetTextColor(dc,RGB(155,173,206));
        text(closing||finished?status:presentationNotice[0]?presentationNotice:resolutionNotice[0]?resolutionNotice:status,{Scale(24),Scale(46),statusRight,Scale(72)},DT_LEFT);
        if(badgeVisible) {
            const int right=bounds.right-Scale(24),left=right-Scale(194);
            SetDCBrushColor(dc,RGB(21,30,49));RoundRect(dc,left,Scale(13),right,Scale(43),Scale(12),Scale(12));
            SetDCBrushColor(dc,observed&&!closing&&!finished?RGB(93,228,199):RGB(155,173,206));
            Ellipse(dc,left+Scale(12),Scale(24),left+Scale(19),Scale(31));
            const wchar_t* phase=Presentation().badge;
            font(smallFont);SetTextColor(dc,RGB(239,245,255));
            text(phase,{left+Scale(28),Scale(14),right-Scale(8),Scale(42)},DT_LEFT);
            wchar_t sessions[100]{};
            if(observed)swprintf_s(sessions,L"Parent %lu  /  Desktop %lu",baseline.caller.value,observedId);
            else swprintf_s(sessions,L"Parent %lu  /  Desktop pending",baseline.caller.value);
            font(bodyFont);SetTextColor(dc,RGB(155,173,206));
            text(sessions,{left,Scale(47),right,Scale(72)},DT_RIGHT);
        }
        font(bodyFont);SetTextColor(dc,RGB(155,173,206));
        const wchar_t* display=sizingKnown?(fitDesktop?L"Display: Fit to window":L"Display: Actual size"):
            L"Display: Sizing unavailable";
        if(FAILED(sizingResult)&&sizingResult!=E_PENDING)display=L"Display change was not confirmed. You can retry.";
        if(bounds.right>=Scale(900))text(display,{Scale(382),Scale(84),bounds.right-Scale(214),Scale(110)},DT_LEFT);
        SetDCBrushColor(dc,RGB(21,30,49));RECT line{0,bounds.bottom-1,bounds.right,bounds.bottom};
        FillRect(dc,&line,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        if(saved)RestoreDC(dc,saved);
    }
    static const char* ResolutionPhase(resolution::Phase phase)noexcept {
        switch(phase){case resolution::Phase::Awaiting:return "awaiting";case resolution::Phase::Confirmed:return "confirmed";
        case resolution::Phase::Unsupported:return "unsupported";case resolution::Phase::Failed:return "failed";
        case resolution::Phase::TimedOut:return "timed-out";case resolution::Phase::Cancelled:return "cancelled";default:return "idle";}
    }
    void RecordResolution(const char* stage,const resolution::Readback* properties=nullptr)noexcept {
        try {
            std::ostringstream out;out.imbue(std::locale::classic());
            out<<"{\"schema\":\"retsar.child-session-resolution.v1\",\"stage\":\""<<stage
                <<"\",\"elapsedMs\":"<<(GetTickCount64()-diagnosticEpoch)<<",\"phase\":\""<<ResolutionPhase(resolutionPending.phase)
                <<"\",\"viewportWidth\":"<<requestedViewportWidth<<",\"viewportHeight\":"<<requestedViewportHeight
                <<",\"requestedWidth\":"<<resolutionPending.requested.width<<",\"requestedHeight\":"<<resolutionPending.requested.height
                <<",\"desktopScalePercent\":100,\"deviceScalePercent\":100,\"callCompleted\":"<<(resolutionCallCompleted?"true":"false")
                <<",\"callHresult\":"<<static_cast<ULONG>(resolutionCallResult.result)
                <<",\"phaseHresult\":"<<static_cast<ULONG>(resolutionPending.result.result)
                <<",\"supported\":"<<(resolutionCallResult.supported?"true":"false")
                <<",\"attempted\":"<<(resolutionCallResult.attempted?"true":"false")
                <<",\"accepted\":"<<(resolutionCallResult.accepted?"true":"false")
                <<",\"matchingSizeEventObserved\":"<<(resolutionPending.eventObserved?"true":"false")
                <<",\"remoteSizeEventConfirmed\":"<<(resolutionPending.phase==resolution::Phase::Confirmed?"true":"false")
                <<",\"observationTimerError\":"<<resolutionTimerError<<",\"controlProperties\":";
            if(properties){out<<"{\"hresult\":"<<static_cast<ULONG>(properties->result)<<",\"known\":"<<(properties->known?"true":"false")
                <<",\"width\":"<<properties->width<<",\"height\":"<<properties->height<<",\"confirmsRemoteSize\":false}";}
            else out<<"null";
            out<<",\"reconnectAttempted\":false,\"physicalSeatVerified\":false}";
            if(!Append(receipt,out.str()).succeeded)journalFailed=true;
        }catch(...){journalFailed=true;}
    }
    void DisarmResolutionTimer()noexcept {
        if(window&&resolutionTimerArmed)KillTimer(window,ResolutionTimer);resolutionTimerArmed=false;
    }
    void RequestMatchScreen()noexcept {
        if(!ResolutionReady()||matchQueued||matchingResolution||outboundDepth||drainingView||screen.busy||changingSizing||
            resolutionPending.phase==resolution::Phase::Awaiting||screen.mode==presentation::Mode::Recovery)return;
        presentationNotice[0]=L'\0';wcscpy_s(resolutionNotice,L"Preparing to match the desktop to this image area...");
        matchQueued=true;LayoutSizing();InvalidateHeader();ScheduleView();
    }
    void CancelResolution()noexcept {
        matchQueued=false;matchPollPending=false;DisarmResolutionTimer();
        if(resolutionPending.phase==resolution::Phase::Awaiting){
            resolutionPending.Cancel();resolutionReported=resolutionPending.phase;RecordResolution("resolution-cancelled");
            wcscpy_s(resolutionNotice,L"Screen matching stopped.");
        }
    }
    void UpdateResolutionObservation()noexcept {
        matchPollPending=false;resolutionPending.Tick(GetTickCount64());
        const auto phase=resolutionPending.phase;
        if(phase!=resolution::Phase::Awaiting)DisarmResolutionTimer();
        const bool changed=phase!=resolutionReported;
        if(changed){
            resolutionReported=phase;RecordResolution("resolution-state");
            switch(phase){
            case resolution::Phase::Awaiting:
                swprintf_s(resolutionNotice,L"Requested %lu x %lu. Waiting for the desktop...",resolutionPending.requested.width,resolutionPending.requested.height);break;
            case resolution::Phase::Confirmed:
                swprintf_s(resolutionNotice,L"Resize confirmed: %lu x %lu.",resolutionPending.requested.width,resolutionPending.requested.height);break;
            case resolution::Phase::Unsupported:wcscpy_s(resolutionNotice,L"Screen matching is not supported here. Use Fit desktop.");break;
            case resolution::Phase::TimedOut:wcscpy_s(resolutionNotice,L"The size change was not confirmed within 10 seconds.");break;
            case resolution::Phase::Failed:wcscpy_s(resolutionNotice,L"Could not request this size. Fit desktop is still available.");break;
            case resolution::Phase::Cancelled:wcscpy_s(resolutionNotice,resolutionTimerError?L"Could not start size monitoring. No resize was requested.":L"Screen matching stopped.");break;
            default:break;
            }
        }
        // These initial-size properties are diagnostic only. Confirmation comes
        // exclusively from a matching, subsequent size event plus accepted call.
        if(phase!=resolution::Phase::Idle&&phase!=resolution::Phase::Awaiting&&ResolutionReady()&&
            resolutionCallCompleted&&resolutionCallResult.attempted&&!resolutionPropertiesLogged){
            resolutionPropertiesLogged=true;matchingResolution=true;
            const auto properties=resolution::ReadProperties(dispatch.value,outboundDepth,&stopRequested);
            matchingResolution=false;RecordResolution("resolution-control-properties",&properties);
        }
        if(changed){LayoutSizing();InvalidateHeader();}
    }
    void RunMatchScreen()noexcept {
        matchQueued=false;
        if(!ResolutionReady()||resolutionPending.phase==resolution::Phase::Awaiting||screen.mode==presentation::Mode::Recovery)return;
        const RECT area=site.Area();requestedViewportWidth=area.right-area.left;requestedViewportHeight=area.bottom-area.top;
        const auto settings=resolution::ForViewport(requestedViewportWidth,requestedViewportHeight);
        if(!resolution::Valid(settings)){
            wcscpy_s(resolutionNotice,L"This image area is outside the supported size. Resize the viewer and try again.");
            Record("resolution-size-rejected",E_INVALIDARG);LayoutSizing();InvalidateHeader();return;
        }
        resolutionCallResult={};resolutionCallCompleted=false;resolutionPropertiesLogged=false;resolutionTimerError=ERROR_SUCCESS;
        if(!resolutionPending.Begin(settings,GetTickCount64()))return;
        resolutionReported=resolution::Phase::Idle; // A new request can finish in the same phase as its predecessor.
        // Set the pending state BEFORE any COM call: the update can synchronously
        // emit OnRemoteDesktopSizeChange through a nested message pump.
        RecordResolution("resolution-intent");matchingResolution=true;LayoutSizing();
        if(journalFailed)resolutionPending.Cancel();
        else {
            resolutionTimerArmed=window&&SetTimer(window,ResolutionTimer,250,nullptr)!=0;
            if(!resolutionTimerArmed){resolutionTimerError=GetLastError();if(!resolutionTimerError)resolutionTimerError=ERROR_GEN_FAILURE;resolutionPending.Cancel();}
            else {
                resolutionCallResult=resolution::Request(dispatch.value,settings,outboundDepth,&stopRequested);resolutionCallCompleted=true;
                resolutionPending.Complete(resolutionCallResult,GetTickCount64());RecordResolution("resolution-request");
            }
        }
        matchingResolution=false;UpdateResolutionObservation();LayoutSizing();InvalidateHeader();
    }
    void ScheduleView()noexcept {
        if(window&&!viewSyncQueued){viewSyncQueued=PostMessageW(window,ViewSyncMessage,0,0)!=FALSE;}
    }
    void RequestPresentation(bool exit)noexcept {
        if(exit){exitPresentationPending=true;enterPresentationPending=false;}
        else enterPresentationPending=true;
        ScheduleView();
    }
    void RecordPresentation(const char* stage,const presentation::Result& value)noexcept {
        try {
            std::ostringstream out;out.imbue(std::locale::classic());
            out<<"{\"schema\":\"retsar.child-session-presentation.v1\",\"stage\":\""<<stage
                <<"\",\"elapsedMs\":"<<(GetTickCount64()-diagnosticEpoch)<<",\"attempted\":"<<(value.attempted?"true":"false")
                <<",\"succeeded\":"<<(value.succeeded?"true":"false")<<",\"verified\":"<<(value.verified?"true":"false")
                <<",\"rolledBack\":"<<(value.rolledBack?"true":"false")<<",\"error\":"<<value.error
                <<",\"mode\":\""<<presentation::Name(value.mode)<<"\",\"actual\":";
            if(value.actualKnown)out<<"{\"left\":"<<value.actual.rect.left<<",\"top\":"<<value.actual.rect.top
                <<",\"right\":"<<value.actual.rect.right<<",\"bottom\":"<<value.actual.rect.bottom<<",\"style\":"<<static_cast<ULONG_PTR>(value.actual.style)<<'}';
            else out<<"null";
            out<<",\"hostComposited\":true,\"physicalSeatVerified\":false}";
            if(!Append(receipt,out.str()).succeeded)journalFailed=true;
        }catch(...){journalFailed=true;}
    }
    void PresentationResult(const char* stage,const presentation::Result& result)noexcept {
        RecordPresentation(stage,result);
        if(result.succeeded)presentationNotice[0]=L'\0';
        else if(result.error==ERROR_MORE_DATA)wcscpy_s(presentationNotice,L"Several secondary screens found. Choose a single-screen setup first.");
        else if(result.error==ERROR_NOT_FOUND)wcscpy_s(presentationNotice,L"No single secondary screen is available.");
        else if(result.error==ERROR_INVALID_STATE&&!result.attempted)wcscpy_s(presentationNotice,L"Restore the viewer to a normal window before presenting.");
        else swprintf_s(presentationNotice,L"Display change could not be verified (error %lu). Use Back to window to retry.",result.error);
        ConfigureAppearance(GetDpiForWindow(window));viewportPending=true;
    }
    bool KnownToolbar(HWND target)const noexcept {
        return target&&(target==presentationButton||target==resolutionButton||target==sizingButton);
    }
    void QueueNavigation(navigation::Action action,HWND origin=nullptr)noexcept {
        if(action==navigation::Action::None||navigating||closing||finished||cleanupStarted||stopRequested)return;
        // A focus-release callback only stores intent. Never focus or call the
        // control from inside its event callback or its nested message pump.
        if(action==navigation::Action::First&&navigationPending!=navigation::Action::None)return;
        navigationPending=action;navigationOrigin=origin;ScheduleView();
    }
    void RecordNavigation(const navigation::Result& value)noexcept {
        try {
            std::ostringstream out;out.imbue(std::locale::classic());
            out<<"{\"schema\":\"retsar.child-session-navigation.v1\",\"target\":\""
                <<(value.target==navigation::Target::Desktop?"desktop":"toolbar")
                <<"\",\"elapsedMs\":"<<(GetTickCount64()-diagnosticEpoch)
                <<",\"hresult\":"<<static_cast<ULONG>(value.result)
                <<",\"attempted\":"<<(value.attempted?"true":"false")
                <<",\"focusVerified\":"<<(value.verified?"true":"false")
                <<",\"foregroundActivationAttempted\":false,\"physicalSeatVerified\":false}";
            if(!Append(receipt,out.str()).succeeded)journalFailed=true;
        }catch(...){journalFailed=true;}
    }
    void DrainNavigation()noexcept {
        if(navigationPending==navigation::Action::None||!window||initializing||outboundDepth||
            drainingView||screen.busy||changingSizing||matchingResolution||navigating)return;
        const auto action=navigationPending;const HWND origin=navigationOrigin;
        navigationPending=navigation::Action::None;navigationOrigin=nullptr;
        if(closing||finished||cleanupStarted||stopRequested)return;
        if((action==navigation::Action::Desktop||action==navigation::Action::First)&&!ResolutionReady())return;
        NativeNavigation api(window,presentationButton,resolutionButton,sizingButton);
        if(!api.ForegroundOwned())return;
        const HWND focus=api.CurrentFocus();
        if(action!=navigation::Action::First&&
            (origin!=focus||!KnownToolbar(origin)||!api.OwnedChild(origin)||!api.Available(origin)))return;
        if(action==navigation::Action::First&&
            (KnownToolbar(focus)||(focus!=window&&!api.OwnedChild(focus))))return;
        // Enter runs the same explicit action as clicking its native button.
        // There is no synthetic key, dialog-wide Tab interception, or retry.
        if(action==navigation::Action::Activate){
            if(origin==presentationButton)RequestPresentation(screen.saved);
            else if(origin==resolutionButton)RequestMatchScreen();
            else if(origin==sizingButton)ToggleSizing();
            return;
        }
        navigating=true;navigation::Result result;
        if(action==navigation::Action::Desktop){
            result=navigation::FocusDesktop(site.active.value,api,outboundDepth,&stopRequested,
                &site.activeGeneration,site.activeGeneration);
        }else {
            const navigation::Button buttons[]={{presentationButton,api.Available(presentationButton)},
                {resolutionButton,api.Available(resolutionButton)},{sizingButton,api.Available(sizingButton)}};
            const HWND target=action==navigation::Action::First?navigation::First(buttons,std::size(buttons)):
                navigation::Next(buttons,std::size(buttons),origin,action==navigation::Action::Previous);
            result=navigation::FocusButton(api,target,outboundDepth,&stopRequested);
        }
        RecordNavigation(result);navigating=false;
        if((stopRequested||journalFailed)&&!closing)BeginStop(journalFailed?"receipt-write":failureStage,
            journalFailed?HRESULT_FROM_WIN32(ERROR_WRITE_FAULT):failure);
        if(finished&&userCloseRequested&&window&&!destroyQueued)destroyQueued=PostMessageW(window,WM_CLOSE,0,0)!=FALSE;
    }
    void DrainView()noexcept {
        if(!window||initializing||outboundDepth||screen.busy||changingSizing||matchingResolution||drainingView||navigating)return;
        drainingView=true;
        if(exitPresentationPending){
            exitPresentationPending=false;enterPresentationPending=false;topologyPending=false;
            if(screen.saved){auto monitors=PresentationMonitors();NativePresentation api(window);
                PresentationResult("presentation-exit",presentation::Exit(screen,api,monitors));}
        }else if(topologyPending){
            topologyPending=false;
            if(screen.saved){auto monitors=PresentationMonitors();presentation::Monitor fresh{};NativePresentation api(window);
                if(screen.mode==presentation::Mode::Presented&&presentation::SameTarget(monitors,screen.target,fresh)){
                    const auto result=presentation::Refit(screen,api,monitors);PresentationResult("presentation-topology",result);
                    if(!result.succeeded)PresentationResult("presentation-recovery",presentation::Exit(screen,api,monitors));
                }else PresentationResult("presentation-monitor-lost",presentation::Exit(screen,api,monitors));
            }
        }else if(enterPresentationPending){
            enterPresentationPending=false;
            if(observed&&!closing&&!finished&&!cleanupStarted){auto monitors=PresentationMonitors();NativePresentation api(window);
                PresentationResult("presentation-enter",presentation::Enter(screen,api,monitors));}
        }
        if(viewportPending&&inplace&&!closing&&!cleanupStarted){
            viewportPending=false;const RECT area=site.Area();const bool wasChanging=site.changingRect;site.changingRect=true;
            ++outboundDepth;const HRESULT result=inplace->SetObjectRects(&area,&area);--outboundDepth;site.changingRect=wasChanging;
            if(FAILED(result))Record("ViewportRectFailed",result);
        }
        if(matchQueued&&!closing&&!cleanupStarted)RunMatchScreen();
        else if(matchPollPending&&!closing&&!cleanupStarted)UpdateResolutionObservation();
        drainingView=false;
        if((stopRequested||journalFailed)&&!closing)BeginStop(journalFailed?"receipt-write":failureStage,journalFailed?HRESULT_FROM_WIN32(ERROR_WRITE_FAULT):failure);
        if(finished&&userCloseRequested&&window&&!destroyQueued)destroyQueued=PostMessageW(window,WM_CLOSE,0,0)!=FALSE;
    }
    bool ArmPollTimer()noexcept {
        if(pollTimerArmed)return true;
        pollTimerArmed=window&&SetTimer(window,PollTimer,250,nullptr)!=0;return pollTimerArmed;
    }
    void DisarmPollTimer()noexcept{if(window&&pollTimerArmed)KillTimer(window,PollTimer);pollTimerArmed=false;}
    void NotifyFrameActivation()noexcept {
        if(!frameActivationPending||initializing||closing||finished||cleanupStarted||outboundDepth||navigating||!site.active)return;
        const bool active=frameActive;
        // Notify this owned control when its containing frame gains/loses activation.
        // No keyboard hook, global focus change, or remote key injection.
        // https://learn.microsoft.com/windows/win32/api/oleidl/nf-oleidl-ioleinplaceactiveobject-onframewindowactivate
        const HRESULT hr=NotifyPendingFrameWindow(site.active.value,active?TRUE:FALSE,
            frameActivationPending,false,outboundDepth);
        Record("FrameWindowActivation",hr,active?1:0,true);
        if(stopRequested||journalFailed)BeginStop(journalFailed?"receipt-write":failureStage,journalFailed?HRESULT_FROM_WIN32(ERROR_WRITE_FAULT):failure);
        else if(frameActivationPending&&window)PostMessageW(window,FrameSyncMessage,0,0);
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
        if(id==23&&navigation::FocusReleased(args))app.QueueNavigation(navigation::Action::First);
        if(id==12){
            // Only this control's actual callback can supply resize evidence.
            app.resolutionPending.ObserveEvent(args,GetTickCount64());
            app.matchPollPending=true;app.ScheduleView();
        }
        if(args&&args->cArgs>=1&&args->rgvarg) {
            if(args->rgvarg[0].vt==VT_I4){value=args->rgvarg[0].lVal;present=true;}
            else if(args->rgvarg[0].vt==VT_I2){value=args->rgvarg[0].iVal;present=true;}
        }
        // Documented DISPIDs, never event strings or user names.
        // https://learn.microsoft.com/windows/win32/termserv/imstscaxevents-interface
        if(id==1)app.connectingEvent=true;
        else if(id==2){app.connected=true;app.Record("OnConnected");}
        else if(id==3){app.loggedIn=true;app.authenticationPrompt=false;app.Record("OnLoginComplete");}
        else if(id==4) {
            app.disconnected=true;app.Record("OnDisconnected",S_OK,value,present);
            if(!app.closing)app.DeferStop("disconnected",E_FAIL);
        }else if(id==10) {
            app.Record("OnFatalError",S_OK,value,present);app.DeferStop("fatal-event",E_FAIL);
        }else if(id==5||id==8) {
            app.Record("fullscreen-request-rejected");
            if(id==5)app.DeferStop("unexpected-fullscreen",E_UNEXPECTED);
        }
        if(id==18)app.authenticationPrompt=true;
        else if(id==19)app.authenticationPrompt=false;
        app.UpdateStatus();
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
        site.stopRequested=&stopRequested;site.frameActivationPending=&frameActivationPending;
        HRESULT hr=Step("CoCreateInstance",CoCreateInstance(ClientClass,nullptr,CLSCTX_INPROC_SERVER,
            IID_IOleObject,reinterpret_cast<void**>(object.put())));
        if(FAILED(hr))return hr;
        if(FAILED(hr=Step("SetClientSite",object->SetClientSite(&site))))return hr;
        Com<IPersistStreamInit> initial;
        if(FAILED(hr=Step("QueryPersistStreamInit",object->QueryInterface(IID_IPersistStreamInit,reinterpret_cast<void**>(initial.put())))))return hr;
        if(FAILED(hr=Step("InitNew",initial->InitNew())))return hr;
        if(FAILED(hr=Step("SetHostNames",object->SetHostNames(L"RETSAR Desktop Viewer",nullptr))))return hr;
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
        // Display scaling is optional; a failure is shown, never mistaken for
        // verified fit or used to change session/authentication behavior.
        ApplySizing(true);
        if(journalFailed)return Step("receipt-write",HRESULT_FROM_WIN32(ERROR_WRITE_FAULT));
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
        if(request.preserveEnabled){
            const auto existing=client::VerifyPreservedEnabled(request,SystemToggleApi(),stopRequested);
            if(FAILED(Step("preserve-enabled-readback",existing.result)))return false;
        }else{
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
            if(FAILED(Step("enable-readback",S_OK)))return false;
        }
        if(FAILED(Step("pre-connect-child-property",VerifyChildProperty(false)))||
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
        connectAttempted=connection.attempted;UpdateStatus();
        if(FAILED(Step("connect-gate-child-property",connection.propertyResult)))return false;
        if(!connectAttempted||FAILED(Step("Connect",connection.connectResult)))return false;
        UpdateStatus();
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
                observed=true;observedId=current.child.value;DisarmPollTimer();
                if(request.mode==client::Mode::ResumeChild) {
                    Record("existing-child-resumed",S_OK,static_cast<LONG>(observedId),true);
                    UpdateStatus();
                }else {
                    Record("active-child-observed",S_OK,static_cast<LONG>(observedId),true);
                    UpdateStatus();
                }
            }
        }
        if(journalFailed){BeginStop("receipt-write",HRESULT_FROM_WIN32(ERROR_WRITE_FAULT));return;}
        if(!observed&&GetTickCount64()-start>=ConnectionDeadlineMs)BeginStop("connection-deadline",HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    }
    void BeginStop(const char* stage,HRESULT reason)noexcept {
        if(finished||closing||cleanupStarted)return;
        if(FAILED(reason)&&SUCCEEDED(failure)){failure=reason;failureStage=stage;}
        if(initializing||outboundDepth||drainingView||screen.busy||navigating){stopRequested=true;return;}
        navigationPending=navigation::Action::None;navigationOrigin=nullptr;
        CancelResolution();enterPresentationPending=false;if(screen.saved){exitPresentationPending=true;ScheduleView();}
        closing=true;closeStart=GetTickCount64();
        const bool timed=ArmPollTimer();
        if(!timed)Record("disconnect-timer-unavailable",HRESULT_FROM_WIN32(GetLastError()));
        UpdateStatus();Record(stage,reason);
        if(connectAttempted&&dispatch) {
            ++outboundDepth;const HRESULT hr=Invoke(dispatch.value,L"Disconnect",DISPATCH_METHOD);--outboundDepth;
            Record("DisconnectOwnControl",hr);
            if(!timed)Cleanup();
        }else Cleanup();
    }
    void Cleanup()noexcept {
        if(finished||cleanupStarted)return;
        if(outboundDepth||drainingView||screen.busy||navigating){stopRequested=true;return;}
        navigationPending=navigation::Action::None;navigationOrigin=nullptr;
        CancelResolution();cleanupStarted=true;closing=true;
        LONG extendedReason=0;
        if(dispatch) {
            const HRESULT hr=GetNumber(dispatch.value,L"ExtendedDisconnectReason",extendedReason);
            Record("ExtendedDisconnectReason",hr,extendedReason,SUCCEEDED(hr));
        }
        // Only this owned control is closed. No session-directed API or logoff.
        if(point&&cookie){const auto hr=point->Unadvise(cookie);cookie=0;Record("UnadviseEvents",hr);}
        point.reset();events.callback=nullptr;
        if(object){Record("CloseOwnObject",object->Close(OLECLOSE_NOSAVE));object->SetClientSite(nullptr);}
        site.inplace=nullptr;site.SetActiveObject(nullptr,nullptr);inplace.reset();extended.reset();dispatch.reset();object.reset();
        if(client::NeedsSettingRestore(request,enableAttempted,restoreAttempted)) {
            restoreAttempted=true;Record("restore-intent");
            const auto restore=toggle_detail::Set(SystemToggleApi(),FALSE);
            Record("restore-result",restore.succeeded?S_OK:HRESULT_FROM_WIN32(restore.error?restore.error:ERROR_GEN_FAILURE));
            Record("restore-last-error-observed",S_OK,static_cast<LONG>(restore.error),!restore.succeeded);
            const auto read=toggle_detail::Read(SystemToggleApi());
            restored=restore.succeeded&&read.succeeded&&!read.value;
            Record("restore-readback",read.succeeded?S_OK:HRESULT_FROM_WIN32(read.error?read.error:ERROR_GEN_FAILURE),read.value?1:0,read.succeeded);
        }
        const auto final=Query(SystemApi());
        const bool baselineVerified=client::BaselineSettingVerified(request,final,enableAttempted,restoreAttempted,restored);
        const bool baselineRestored=!request.preserveEnabled&&baselineVerified;
        finalBaselineRestored=baselineRestored;
        finalBaselinePreserved=request.preserveEnabled&&baselineVerified;
        if(request.preserveEnabled)Record("preserve-enabled-final-readback",finalBaselinePreserved?S_OK:E_UNEXPECTED,
            final.enabled.value?1:0,final.enabled.succeeded);
        try {
            std::ostringstream out;out<<"{\"schema\":\"retsar.child-session-client-final.v1\","
                <<"\"connectionMode\":\""<<client::ModeName(request)<<"\","
                <<"\"resumedExistingChild\":"<<((observed&&request.mode==client::Mode::ResumeChild)?"true":"false")
                <<",\"observedChildSession\":"<<(observed?"true":"false")
                <<",\"restoreAttempted\":"<<(restoreAttempted?"true":"false")
                <<",\"baselineRestored\":"<<(baselineRestored?"true":"false")
                <<",\"baselinePreserved\":"<<(finalBaselinePreserved?"true":"false")
                <<",\"settingMutationOwned\":"<<(enableAttempted?"true":"false")
                <<",\"failureStage\":\""<<failureStage<<"\",\"failureHresult\":"<<static_cast<ULONG>(failure)
                <<",\"journalFailed\":"<<(journalFailed?"true":"false")
                <<",\"finalStatus\":"<<ToJson(final)<<",\"logoffAttempted\":false,\"physicalSeatVerified\":false}";
            if(!Append(receipt,out.str()).succeeded)journalFailed=true;
        }catch(...){journalFailed=true;}
        finished=true;DisarmPollTimer();
        residualChild=final.child.succeeded&&final.child.value!=MAXDWORD;
        UpdateStatus();
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
        case WM_CREATE:
            app->ConfigureAppearance(GetDpiForWindow(window));
            app->sizingButton=CreateWindowExW(0,L"BUTTON",L"Fit desktop",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
                0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(SizingButton)),GetModuleHandleW(nullptr),nullptr);
            app->presentationButton=CreateWindowExW(0,L"BUTTON",L"Use second monitor",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
                0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(PresentationButton)),GetModuleHandleW(nullptr),nullptr);
            app->resolutionButton=CreateWindowExW(0,L"BUTTON",L"Match screen",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
                0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ResolutionButton)),GetModuleHandleW(nullptr),nullptr);
            if(!app->sizingButton||!app->presentationButton||!app->resolutionButton)return -1;
            app->LayoutSizing();return 0;
        case WM_COMMAND:
            if(LOWORD(wp)==SizingButton&&HIWORD(wp)==BN_CLICKED&&reinterpret_cast<HWND>(lp)==app->sizingButton)
                app->ToggleSizing();
            else if(LOWORD(wp)==PresentationButton&&HIWORD(wp)==BN_CLICKED&&reinterpret_cast<HWND>(lp)==app->presentationButton)
                app->RequestPresentation(app->screen.saved);
            else if(LOWORD(wp)==ResolutionButton&&HIWORD(wp)==BN_CLICKED&&reinterpret_cast<HWND>(lp)==app->resolutionButton)
                app->RequestMatchScreen();
            return 0;
        case WM_ERASEBKGND: {
            const HDC dc=reinterpret_cast<HDC>(wp);RECT area{};GetClientRect(window,&area);
            const COLORREF previous=SetDCBrushColor(dc,RGB(11,16,32));
            FillRect(dc,&area,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));SetDCBrushColor(dc,previous);return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);
            app->PaintHeader(dc);EndPaint(window,&paint);return 0;
        }
        case WM_DPICHANGED: {
            app->ConfigureAppearance(HIWORD(wp));
            const auto* suggested=reinterpret_cast<const RECT*>(lp);
            if(app->screen.saved){app->topologyPending=true;app->ScheduleView();}
            else if(suggested)SetWindowPos(window,nullptr,suggested->left,suggested->top,
                suggested->right-suggested->left,suggested->bottom-suggested->top,SWP_NOZORDER|SWP_NOACTIVATE);
            app->viewportPending=true;app->ScheduleView();return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* limits=reinterpret_cast<MINMAXINFO*>(lp);
            MONITORINFO monitor{sizeof(monitor)};
            const bool known=GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor)!=FALSE;
            limits->ptMinTrackSize=app->screen.busy||app->screen.saved?POINT{1,1}:POINT{
                known?std::min<LONG>(app->Scale(640),monitor.rcWork.right-monitor.rcWork.left):app->Scale(640),
                known?std::min<LONG>(app->Scale(360),monitor.rcWork.bottom-monitor.rcWork.top):app->Scale(360)};return 0;
        }
        case WM_ACTIVATE:
            app->frameActive=LOWORD(wp)!=WA_INACTIVE;app->frameActivationPending=true;
            if(!app->frameActive){
                app->navigationPending=navigation::Action::None;app->navigationOrigin=nullptr;
            }
            PostMessageW(window,FrameSyncMessage,0,0);
            return DefWindowProcW(window,message,wp,lp);
        case FrameSyncMessage:app->NotifyFrameActivation();return 0;
        case WM_SIZE:
            app->LayoutSizing();app->viewportPending=true;app->ScheduleView();return 0;
        case WM_DISPLAYCHANGE:
            if(app->screen.saved){app->topologyPending=true;app->ScheduleView();}return 0;
        case WM_SETTINGCHANGE:
            if(wp==SPI_SETWORKAREA&&app->screen.saved){app->topologyPending=true;app->ScheduleView();}
            return DefWindowProcW(window,message,wp,lp);
        case WM_DEVICECHANGE:
            if(wp==DBT_DEVNODES_CHANGED&&app->screen.saved){app->topologyPending=true;app->ScheduleView();}
            return DefWindowProcW(window,message,wp,lp);
        case ViewSyncMessage:
            app->viewSyncQueued=false;app->DrainView();app->DrainNavigation();return 0;
        case WM_TIMER:
            if(wp==PollTimer)app->Poll();
            else if(wp==ResolutionTimer){app->matchPollPending=true;app->ScheduleView();}
            return 0;
        case StepMessage:app->Poll();return 0;
        case StopMessage:app->BeginStop(app->failureStage,app->failure);return 0;
        case WM_CLOSE:
            if(app->finished){
                app->destroyQueued=false;
                if(app->drainingView||app->screen.busy||app->navigating||app->outboundDepth){app->userCloseRequested=true;return 0;}
                DestroyWindow(window);return 0;
            }
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
        std::cout<<"RETSAR Desktop Viewer\n"
            <<"Usage: RETSAR-child-session-client.exe --connect-child --receipt <new.jsonl>\n"
            <<"       RETSAR-child-session-client.exe --resume-child-session <id> --receipt <new.jsonl>\n"
            <<"       RETSAR-child-session-client.exe --resume-enabled-child-session <id> --receipt <new.jsonl>\n"
            <<"Explicit mode opens or resumes one local Windows child desktop.\n"
            <<"Receipt must be new under D:\\RETSAR-Lab\\RETSAR\\private\\.\n"
            <<"Connection deadline: 60 seconds. An observed connection stays open until closed.\n"
            <<"Closing disconnects only this client and restores the original disabled setting.\n"
            <<"Resume requires an exact existing disconnected child; it never claims new creation.\n"
            <<"Enabled resume preserves the enabled baseline and never calls the setting setter, including cleanup.\n"
            <<"Remaining child IDs are reported, never logged off automatically.\n"
            <<"Display requests fit by default; the header reports verified mode and toggles actual size without reconnecting.\n"
            <<"Use second monitor presents the host window on the unique secondary; Back to window / local Ctrl+Alt+F12 restores it without reconnecting.\n"
            <<"Presentation requires a normal (not maximized or minimized) viewer window.\n"
            <<"Match screen explicitly requests the image viewport size at 100 percent desktop/device scale, without reconnecting.\n"
            <<"Odd viewport widths round down by one pixel. Size confirmation needs a subsequent matching remote-size event within 10 seconds.\n"
            <<"A timeout does not undo or cancel an in-flight update. Initial-size property readback alone is never confirmation.\n"
            <<"Ctrl+Alt+Left/Right releases desktop focus to the first available toolbar button.\n"
            <<"Only on the toolbar: Tab / Shift+Tab selects, Enter activates, Escape returns to the owned desktop control.\n"
            <<"No ordinary RDP fallback, credentials, RDP-control fullscreen or physical device assignment.\n"
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
        initial<<",\"connectionMode\":\""<<client::ModeName(request)<<"\",\"requestedChildId\":";
        if(request.mode==client::Mode::ResumeChild)initial<<request.target;else initial<<"null";
        initial<<",\"settingPolicy\":\""<<(request.preserveEnabled?"preserve-existing-enabled":"enable-and-restore-disabled")<<"\""
            <<",\"baseline\":"<<ToJson(baseline)<<",\"inventory\":"<<InventoryJson(inventory)
            <<",\"childAbsenceProven\":"<<(ClassifyChild(baseline)==ChildState::Absent?"true":"false")
            <<",\"connectionDeadlineMs\":"<<ConnectionDeadlineMs<<",\"physicalSeatVerified\":false}";
        if(!Append(receipt.value,initial.str()).succeeded)return 4;
        const HRESULT initialized=OleInitialize(nullptr);
        if(FAILED(initialized)){std::cerr<<"OLE initialization failed.\n";return 5;}
        struct OleLifetime{~OleLifetime(){OleUninitialize();}} oleLifetime;
        App app;app.receipt=receipt.value;app.baseline=baseline;app.inventory=inventory;app.request=request;
        if(request.mode==client::Mode::ResumeChild)
            app.Status(L"Preparing to resume your desktop...");
        WNDCLASSEXW definition{sizeof(definition)};
        definition.lpfnWndProc=WindowProc;definition.hInstance=GetModuleHandleW(nullptr);
        definition.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        definition.hIcon=LoadIconW(definition.hInstance,MAKEINTRESOURCEW(101));
        definition.hIconSm=static_cast<HICON>(LoadImageW(definition.hInstance,MAKEINTRESOURCEW(101),IMAGE_ICON,16,16,LR_SHARED));
        definition.lpszClassName=L"RETSAR.ChildSessionExperiment";
        if(!RegisterClassExW(&definition))return 5;
        RECT work{};EnumDisplayMonitors(nullptr,nullptr,ChooseMonitor,reinterpret_cast<LPARAM>(&work));
        if(work.right<=work.left||work.bottom<=work.top)return 5;
        const int width=std::min(1120,static_cast<int>(work.right-work.left)-32);
        const int height=std::min(860,static_cast<int>(work.bottom-work.top)-48);
        HWND window=CreateWindowExW(0,definition.lpszClassName,L"RETSAR Desktop Viewer",
            WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,work.left+16,work.top+24,width,height,nullptr,nullptr,definition.hInstance,&app);
        if(!window)return 5;
        ShowWindow(window,SW_SHOWNOACTIVATE);
        // A launcher's STARTUPINFO can override the first ShowWindow argument.
        // The second call explicitly shows this owned window without activation.
        ShowWindow(window,SW_SHOWNOACTIVATE);
        if(!app.ArmPollTimer())app.BeginStop("timer-create",HRESULT_FROM_WIN32(GetLastError()));
        else {
            const HRESULT prepared=app.Prepare();
            if(FAILED(prepared))app.BeginStop(app.failureStage,prepared);
            else if(!app.stopRequested&&SUCCEEDED(app.failure)&&!app.EnableAndConnect())
                app.BeginStop(app.failureStage,FAILED(app.failure)?app.failure:E_FAIL);
        }
        app.initializing=false;app.LayoutSizing();app.viewportPending=true;app.ScheduleView();
        PostMessageW(window,FrameSyncMessage,0,0);
        if(app.stopRequested||FAILED(app.failure))app.BeginStop(app.failureStage,app.failure);
        MSG message{};BOOL messageResult=0;
        while((messageResult=GetMessageW(&message,nullptr,0,0))>0) {
            // A nested COM message pump can consume FrameSyncMessage while the
            // outbound guard is held. Drain the retained request at this safe
            // outer boundary even after the observation timer has stopped.
            app.NotifyFrameActivation();app.DrainView();app.DrainNavigation();
            const bool ownedMessage=message.hwnd==window||IsChild(window,message.hwnd);
            bool exitPresentation=false;
            if(ownedMessage&&app.presentationShortcut.Handle(message.message,message.wParam,message.lParam,
                GetForegroundWindow()==window,app.screen.saved,(GetKeyState(VK_CONTROL)&0x8000)!=0,(GetKeyState(VK_MENU)&0x8000)!=0,exitPresentation)){
                if(exitPresentation)app.RequestPresentation(true);
                app.DrainView();continue;
            }
            if(KeyboardMessage(message.message)){
                const HWND focus=GetFocus();
                const auto routed=app.navigationKeys.Handle(message.message,message.wParam,message.lParam,
                    ownedMessage,GetForegroundWindow()==window,message.hwnd==focus&&app.KnownToolbar(focus),
                    (GetKeyState(VK_SHIFT)&0x8000)!=0,(GetKeyState(VK_CONTROL)&0x8000)!=0,(GetKeyState(VK_MENU)&0x8000)!=0);
                if(routed.consumed){
                    app.QueueNavigation(routed.action,focus);app.DrainNavigation();app.DrainView();continue;
                }
            }
            HRESULT translated=S_FALSE;
            const bool ownTarget=message.hwnd!=app.sizingButton&&message.hwnd!=app.presentationButton&&message.hwnd!=app.resolutionButton&&ownedMessage;
            if(KeyboardMessage(message.message)&&ownTarget&&app.site.active&&!app.closing&&
               !app.cleanupStarted&&GetForegroundWindow()==window) {
                translated=TranslateOwnedAccelerator(app.site.active.value,&message,app.outboundDepth);
                if(app.stopRequested&&!app.closing)app.BeginStop(app.failureStage,app.failure);
            }
            if(translated!=S_OK){TranslateMessage(&message);DispatchMessageW(&message);}
            app.NotifyFrameActivation();app.DrainView();app.DrainNavigation();
        }
        if(messageResult<0) {
            const DWORD messageError=GetLastError();
            app.Step("GetMessage",HRESULT_FROM_WIN32(messageError?messageError:ERROR_GEN_FAILURE));
        }
        if(!app.finished)app.BeginStop("message-loop-ended",app.failure);
        app.Cleanup();
        return app.observed&&(app.finalBaselineRestored||app.finalBaselinePreserved)&&!app.journalFailed&&SUCCEEDED(app.failure)?0:2;
    }catch(...){std::cerr<<"Desktop viewer failed; inspect the private receipt.\n";return 5;}
}

