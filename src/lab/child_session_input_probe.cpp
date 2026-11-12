#include "child_session_input_contract.hpp"
#include "child_session_input_diagnostics.hpp"
#include "child_session_experiment_io.hpp"
#include <iostream>
#include <iomanip>

namespace {
using namespace seats::lab::child::input;
using seats::lab::child::experiment::Handle;
using seats::lab::child::experiment::Append;
using seats::lab::child::experiment::ValidReceiptPath;
constexpr UINT_PTR Timer=61;
constexpr ULONGLONG DeadlineMs=30000;
constexpr WORD TestKey=VK_F24;

bool ActiveSession(DWORD session)noexcept {
    LPWSTR data=nullptr;DWORD bytes=0;
    const BOOL ok=WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,session,WTSConnectState,&data,&bytes);
    const bool active=ok&&data&&bytes==sizeof(WTS_CONNECTSTATE_CLASS)&&
        *reinterpret_cast<WTS_CONNECTSTATE_CLASS*>(data)==WTSActive;
    if(data)WTSFreeMemory(data);
    return active;
}
Identity ReadIdentity(const Options& options)noexcept {
    Identity value{};
    value.processKnown=ProcessIdToSessionId(GetCurrentProcessId(),&value.processSession)!=FALSE;
    value.consoleSession=WTSGetActiveConsoleSessionId();
    HANDLE rawToken=nullptr;
    if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&rawToken)) {
        DWORD bytes=0;
        value.tokenKnown=GetTokenInformation(rawToken,TokenSessionId,&value.tokenSession,
            sizeof(value.tokenSession),&bytes)&&bytes==sizeof(value.tokenSession);
        CloseHandle(rawToken);
    }
    rawToken=nullptr;
    if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&rawToken))CloseHandle(rawToken);
    else value.noImpersonation=GetLastError()==ERROR_NO_TOKEN;
    value.childActive=ActiveSession(options.child);value.parentActive=ActiveSession(options.parent);
    return value;
}
bool ObjectName(HANDLE object,wchar_t (&name)[256])noexcept {
    DWORD bytes=0;
    if(!object||!GetUserObjectInformationW(object,UOI_NAME,name,sizeof(name),&bytes)||
       bytes<sizeof(wchar_t)||bytes>sizeof(name)||bytes%sizeof(wchar_t))return false;
    return name[bytes/sizeof(wchar_t)-1]==L'\0';
}
bool InputDesktopMatches()noexcept {
    wchar_t own[256]{},input[256]{};
    HDESK desktop=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
    if(!desktop)return false;
    const bool match=ObjectName(GetThreadDesktop(GetCurrentThreadId()),own)&&ObjectName(desktop,input)&&
        std::wcscmp(own,input)==0;
    CloseDesktop(desktop);return match;
}
std::string JsonWide(const wchar_t* value) {
    std::ostringstream out;out<<'"';
    for(const wchar_t* ch=value;*ch;++ch) {
        const auto code=static_cast<unsigned short>(*ch);
        if(code>=32&&code<127&&code!='"'&&code!='\\')out<<static_cast<char>(code);
        else out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<code<<std::dec;
    }
    out<<'"';return out.str();
}
struct Probe {
    Options options;HANDLE receipt=INVALID_HANDLE_VALUE;HWND window=nullptr;
    DWORD process=GetCurrentProcessId(),thread=GetCurrentThreadId();
    ULONG_PTR marker=0;
    POINT originalCursor{};
    bool cursorSaved=false,cursorMoved=false,cursorRestored=false,journalFailed=false,finished=false;
    bool mouseSent=false,keySent=false,keyReleasePending=false,closing=false;
    unsigned mouseMoves=0,keyDowns=0,keyUps=0,paints=0;
    diagnostics::MouseMessages mouseMessages{};
    POINT mouseBefore{},mouseTarget{},mouseAfter{},mouseSettled{};
    bool mouseBeforeKnown=false,mouseTargetKnown=false,mouseAfterKnown=false,mouseSettledKnown=false;
    bool mouseBeforeHitOwn=false,mouseTargetHitOwn=false,mouseAfterHitOwn=false,mouseSettledHitOwn=false;
    bool mouseSettledRecorded=false;
    UINT mouseInserted=0;DWORD mouseSendError=0;
    DWORD error=ERROR_SUCCESS;const char* failureStage="none";
    ULONGLONG started=0;
    ~Probe(){Finish();}
    void Fail(const char* stage,DWORD value=ERROR_ACCESS_DENIED)noexcept {
        if(error==ERROR_SUCCESS){error=value?value:ERROR_GEN_FAILURE;failureStage=stage;}
    }
    bool Guard()const noexcept {
        const auto identity=ReadIdentity(options);WindowEvidence evidence{};
        DWORD owner=0;const DWORD ownerThread=window?GetWindowThreadProcessId(window,&owner):0;
        evidence.inputDesktopMatches=InputDesktopMatches();
        evidence.foregroundIsOwnedWindow=window&&GetForegroundWindow()==window;
        evidence.windowProcessMatches=owner==process;evidence.windowThreadMatches=ownerThread==thread;
        evidence.windowEnabled=window&&IsWindowEnabled(window);
        evidence.windowVisible=window&&IsWindowVisible(window);
        return InputAllowed(options,identity,evidence);
    }
    bool Record(const char* stage,bool final=false)noexcept {
        try {
            const bool observed=mouseMoves>0&&keyDowns>0&&keyUps>0;
            const bool success=final&&observed&&paints>0&&cursorRestored&&!keyReleasePending&&
                !journalFailed&&error==ERROR_SUCCESS;
            std::ostringstream out;out.imbue(std::locale::classic());
            out<<"{\"schema\":\"retsar.child-session-input-probe.v1\",\"stage\":\""<<stage
               <<"\",\"nonce\":"<<JsonWide(options.nonce.c_str())
               <<",\"childSessionId\":"<<options.child<<",\"parentSessionId\":"<<options.parent
               <<",\"processId\":"<<process<<",\"success\":"<<(success?"true":"false")
               <<",\"probeComplete\":"<<(final?"true":"false")
               <<",\"inputObserved\":"<<(observed?"true":"false")
               <<",\"mouseMoveCount\":"<<mouseMoves<<",\"keyDownCount\":"<<keyDowns<<",\"keyUpCount\":"<<keyUps
               <<",\"expectedExtraInfo\":"<<marker<<",\"expectedExtraInfoLow32\":"<<static_cast<DWORD>(marker)
               <<",\"extraInfoWidthBits\":"<<(sizeof(ULONG_PTR)*8)
               <<",\"correlationMarkerBits\":31,\"correlationMarkerIsAuthentication\":false,";
            mouseMessages.JsonMembers(out);
            const auto position=[&](const char* name,POINT point,bool known,bool hitOwn) {
                out<<",\""<<name<<"\":{\"known\":"<<(known?"true":"false")<<",\"x\":";
                if(known)out<<point.x;else out<<"null";
                out<<",\"y\":";if(known)out<<point.y;else out<<"null";
                out<<",\"hitOwnWindow\":"<<(known?(hitOwn?"true":"false"):"null")<<'}';
            };
            position("mouseBefore",mouseBefore,mouseBeforeKnown,mouseBeforeHitOwn);
            position("mouseTarget",mouseTarget,mouseTargetKnown,mouseTargetHitOwn);
            position("mouseAfterImmediate",mouseAfter,mouseAfterKnown,mouseAfterHitOwn);
            position("mouseAfterSettled",mouseSettled,mouseSettledKnown,mouseSettledHitOwn);
            out<<",\"mouseInsertedCount\":"<<mouseInserted<<",\"mouseSendError\":"<<mouseSendError
               <<",\"paintCount\":"<<paints<<",\"cursorRestored\":"<<(cursorRestored?"true":"false")
               <<",\"keyReleasePending\":"<<(keyReleasePending?"true":"false")
               <<",\"error\":"<<error<<",\"failureStage\":\""<<failureStage<<"\",\"physicalSeatVerified\":false}";
            if(!Append(receipt,out.str()).succeeded){journalFailed=true;Fail("receipt-write",ERROR_WRITE_FAULT);return false;}
            return true;
        }catch(...){journalFailed=true;Fail("receipt-exception",ERROR_WRITE_FAULT);return false;}
    }
    bool Baseline() {
        wchar_t desktop[256]{},station[256]{};
        if(!ObjectName(GetThreadDesktop(thread),desktop)||!ObjectName(GetProcessWindowStation(),station)||
           !InputDesktopMatches()||!GetCursorPos(&originalCursor))return false;
        cursorSaved=true;
        std::ostringstream out;
        out<<"{\"schema\":\"retsar.child-session-input-probe-display.v1\",\"nonce\":"<<JsonWide(options.nonce.c_str())
           <<",\"processId\":"<<process<<",\"childSessionId\":"<<options.child
           <<",\"parentSessionId\":"<<options.parent<<",\"desktop\":"<<JsonWide(desktop)
           <<",\"windowStation\":"<<JsonWide(station)
           <<",\"virtualX\":"<<GetSystemMetrics(SM_XVIRTUALSCREEN)<<",\"virtualY\":"<<GetSystemMetrics(SM_YVIRTUALSCREEN)
           <<",\"virtualWidth\":"<<GetSystemMetrics(SM_CXVIRTUALSCREEN)<<",\"virtualHeight\":"<<GetSystemMetrics(SM_CYVIRTUALSCREEN)
           <<",\"monitorCount\":"<<GetSystemMetrics(SM_CMONITORS)
           <<",\"cursorX\":"<<originalCursor.x<<",\"cursorY\":"<<originalCursor.y
           <<",\"captureTaken\":false,\"physicalSeatVerified\":false}";
        return Append(receipt,out.str()).succeeded;
    }
    bool NeutralKeyboard()const noexcept {
        // Never release preexisting user keys or synthesize a modified shortcut.
        for(int key=1;key<256;++key)if(GetAsyncKeyState(key)&0x8000)return false;
        return true;
    }
    void ReleaseOwnKey()noexcept {
        if(!keyReleasePending)return;
        // Recovery is still confined to the same owned foreground. F24 down/up
        // are normally inserted together; a partial insertion is a failed probe.
        if(!Guard()){Fail("key-release-guard");return;}
        INPUT up{};up.type=INPUT_KEYBOARD;up.ki.wVk=TestKey;up.ki.dwFlags=KEYEVENTF_KEYUP;up.ki.dwExtraInfo=marker;
        if(SendInput(1,&up,sizeof(up))==1)keyReleasePending=false;
        else Fail("key-release-send",GetLastError());
    }
    void Tick()noexcept {
        if(finished||closing)return;
        const auto elapsed=GetTickCount64()-started;
        if(elapsed>=DeadlineMs){Fail("deadline",ERROR_TIMEOUT);Finish();return;}
        if(!Guard()) {
            if(mouseSent||keySent||elapsed>5000){Fail("input-guard");Finish();}
            return;
        }
        if(!mouseSent) {
            if(!NeutralKeyboard()){Fail("preexisting-input",ERROR_BUSY);Finish();return;}
            RECT client{};POINT target{},beforeClient{},originalClient=originalCursor;
            if(!GetClientRect(window,&client)){Fail("client-rect",GetLastError());Finish();return;}
            mouseBeforeKnown=GetCursorPos(&mouseBefore)!=FALSE;
            if(!mouseBeforeKnown){Fail("mouse-before-position",GetLastError());Finish();return;}
            mouseBeforeHitOwn=WindowFromPoint(mouseBefore)==window;beforeClient=mouseBefore;
            if(!ScreenToClient(window,&beforeClient)||!ScreenToClient(window,&originalClient)||
               !diagnostics::ChooseTarget(client,beforeClient,originalClient,target)||!ClientToScreen(window,&target)) {
                Fail("distinct-mouse-target",ERROR_INVALID_DATA);Finish();return;
            }
            mouseTarget=target;mouseTargetKnown=true;mouseTargetHitOwn=WindowFromPoint(target)==window;
            if(!mouseTargetHitOwn){Fail("mouse-target");Finish();return;}
            const int left=GetSystemMetrics(SM_XVIRTUALSCREEN),top=GetSystemMetrics(SM_YVIRTUALSCREEN);
            const int width=GetSystemMetrics(SM_CXVIRTUALSCREEN),height=GetSystemMetrics(SM_CYVIRTUALSCREEN);
            if(width<=1||height<=1){Fail("display-metrics",ERROR_INVALID_DATA);Finish();return;}
            INPUT move{};move.type=INPUT_MOUSE;
            move.mi.dx=static_cast<LONG>((static_cast<LONGLONG>(target.x)-left)*65535/(width-1));
            move.mi.dy=static_cast<LONG>((static_cast<LONGLONG>(target.y)-top)*65535/(height-1));
            move.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
            move.mi.dwExtraInfo=marker;
            if(!Record("mouse-intent")||!Guard()){Fail("mouse-admission");Finish();return;}
            mouseSent=true;cursorMoved=true;
            mouseInserted=SendInput(1,&move,sizeof(move));
            mouseSendError=mouseInserted==1?ERROR_SUCCESS:GetLastError();
            mouseAfterKnown=GetCursorPos(&mouseAfter)!=FALSE;
            if(mouseAfterKnown)mouseAfterHitOwn=WindowFromPoint(mouseAfter)==window;
            if(mouseInserted!=1){Fail("mouse-send",mouseSendError);Finish();return;}
            Record("mouse-sent");return;
        }
        if(!mouseSettledRecorded) {
            mouseSettledKnown=GetCursorPos(&mouseSettled)!=FALSE;
            if(mouseSettledKnown)mouseSettledHitOwn=WindowFromPoint(mouseSettled)==window;
            mouseSettledRecorded=true;
            if(!Record("mouse-settled")){Finish();return;}
        }
        if(!keySent&&mouseMoves>0) {
            if(!NeutralKeyboard()){Fail("preexisting-key",ERROR_BUSY);Finish();return;}
            INPUT keys[2]{};
            for(auto& key:keys){key.type=INPUT_KEYBOARD;key.ki.wVk=TestKey;key.ki.dwExtraInfo=marker;}
            keys[1].ki.dwFlags=KEYEVENTF_KEYUP;
            if(!Record("key-pair-intent")||!Guard()){Fail("key-admission");Finish();return;}
            keySent=true;keyReleasePending=true;
            // One atomic insertion batch prevents normal input from being
            // interspersed between the test's key down and key up.
            const UINT inserted=SendInput(2,keys,sizeof(INPUT));
            const DWORD sendError=GetLastError();
            if(inserted==2||inserted==0)keyReleasePending=false;
            if(inserted!=2){Fail("key-pair-send",sendError);ReleaseOwnKey();Finish();return;}
            Record("key-pair-sent");return;
        }
        if(keySent&&keyDowns>0&&keyUps>0&&elapsed>=1500)Finish();
    }
    void Finish()noexcept {
        if(finished||closing||receipt==INVALID_HANDLE_VALUE)return;
        closing=true;
        if(window)KillTimer(window,Timer);
        ReleaseOwnKey();
        if(cursorSaved&&!cursorMoved)cursorRestored=true;
        else if(cursorSaved&&Guard()) {
            POINT current{};
            if(SetCursorPos(originalCursor.x,originalCursor.y)&&GetCursorPos(&current)&&
               current.x==originalCursor.x&&current.y==originalCursor.y)cursorRestored=true;
            else Fail("cursor-restore",GetLastError());
        }else if(cursorMoved)Fail("cursor-restore-guard");
        if(!SessionAllowed(options,ReadIdentity(options)))Fail("final-session-guard");
        Record("final",true);finished=true;
        if(window)DestroyWindow(window);
    }
};
LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* probe=reinterpret_cast<Probe*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        probe=static_cast<Probe*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        probe->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(probe));
    }
    if(!probe)return DefWindowProcW(window,message,wp,lp);
    switch(message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT bounds{};GetClientRect(window,&bounds);
        HBRUSH background=CreateSolidBrush(RGB(20,48,64));FillRect(dc,&bounds,background);DeleteObject(background);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(230,245,240));
        bounds.left+=20;bounds.top+=20;bounds.right-=20;
        DrawTextW(dc,L"RETSAR: child-session input/display probe\nOwn window only. No physical device isolation claim.\nThis window closes automatically within 30 seconds.",-1,&bounds,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
        RECT tile{30,140,130,240};HBRUSH color=CreateSolidBrush(RGB(40,190,150));FillRect(dc,&tile,color);DeleteObject(color);
        ++probe->paints;EndPaint(window,&paint);return 0;
    }
    case WM_MOUSEMOVE:
    {
        const auto extra=static_cast<ULONG_PTR>(GetMessageExtraInfo());
        probe->mouseMessages.Observe(extra,probe->marker,probe->mouseSent,
            probe->started?GetTickCount64()-probe->started:0,
            static_cast<SHORT>(LOWORD(lp)),static_cast<SHORT>(HIWORD(lp)));
        // Diagnostics do not relax admission: only the original full-width
        // marker comparison advances the key test or counts as received input.
        if(extra==probe->marker&&probe->mouseSent)++probe->mouseMoves;
        return 0;
    }
    case WM_KEYDOWN:
        if(wp==TestKey&&static_cast<ULONG_PTR>(GetMessageExtraInfo())==probe->marker&&probe->keySent)++probe->keyDowns;
        return 0;
    case WM_KEYUP:
        if(wp==TestKey&&static_cast<ULONG_PTR>(GetMessageExtraInfo())==probe->marker&&probe->keySent)++probe->keyUps;
        return 0;
    case WM_TIMER:if(wp==Timer)probe->Tick();return 0;
    case WM_CLOSE:probe->Fail("user-close",ERROR_CANCELLED);probe->Finish();return 0;
    case WM_DESTROY:probe->window=nullptr;PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
}

int wmain(int argc,wchar_t* argv[]) {
    try {
        const auto options=Parse(std::vector<std::wstring>(argv+1,argv+argc));
        if(!options.run) {
            std::cout<<"RETSAR child-session input/display probe\n"
                <<"Usage: RETSAR-child-session-input-probe.exe --run --child-session <id> --parent-session <id> --nonce <32 lowercase hex> --receipt <new private.jsonl>\n"
                <<"Explicit run only; refuses Session 0, the parent, mismatched token/session, inactive parent/child and foreign foreground.\n"
                <<"Draws its own window and sends mouse movement plus an F24 down/up pair in the child; no clicks, settings, logoff or screenshots.\n"
                <<"Deadline 30 seconds; restores only the child cursor when the own-window guard remains true.\n"
                <<"Mouse diagnostics retain only eight messages received by this owned window; success still requires the full marker.\n"
                <<"The generated marker has 31 positive bits for transport compatibility; it is correlation, not authentication.\n"
                <<"SendInput is desktop-scoped; this does not prove physical device isolation.\n"
                <<"Help performs no session queries, input, GUI or file writes.\n";return 0;
        }
        // This admission happens before creating a file, window, mutex or input.
        if(!SessionAllowed(options,ReadIdentity(options))){std::cerr<<"Child/token/parent session guard rejected. No input.\n";return 3;}
        std::wstring path;
        if(!ValidReceiptPath(options.receipt.c_str(),path)){std::cerr<<"Receipt path rejected.\n";return 64;}
        Handle mutex;mutex.value=CreateMutexW(nullptr,TRUE,L"Local\\RETSAR.ChildSessionInputProbe");
        const DWORD mutexError=GetLastError();
        if(!mutex.value||mutexError==ERROR_ALREADY_EXISTS){if(!mutex.value)mutex.value=INVALID_HANDLE_VALUE;return 3;}
        Handle receipt;receipt.value=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(receipt.value==INVALID_HANDLE_VALUE)return 4;
        BY_HANDLE_FILE_INFORMATION info{};wchar_t finalPath[32768]{};
        const DWORD length=GetFinalPathNameByHandleW(receipt.value,finalPath,static_cast<DWORD>(std::size(finalPath)),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        const auto expected=L"\\\\?\\"+path;
        if(GetFileType(receipt.value)!=FILE_TYPE_DISK||!GetFileInformationByHandle(receipt.value,&info)||
           (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||
           !length||length>=std::size(finalPath)||_wcsicmp(finalPath,expected.c_str())!=0)return 4;
        Probe probe;probe.options=options;probe.receipt=receipt.value;
        probe.marker=CorrelationMarkerFromNonce(options.nonce);
        if(!probe.marker){probe.Fail("correlation-marker",ERROR_INVALID_DATA);return 4;}
        if(!probe.Baseline()){probe.Fail("baseline",GetLastError());return 4;}
        if(!probe.Record("admitted"))return 4;
        WNDCLASSEXW cls{sizeof(cls)};cls.lpfnWndProc=WindowProc;cls.hInstance=GetModuleHandleW(nullptr);
        cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=L"RETSAR.ChildSessionInputProbe";
        if(!RegisterClassExW(&cls)){probe.Fail("register-window",GetLastError());return 5;}
        const int width=520,height=300;
        const int x=GetSystemMetrics(SM_XVIRTUALSCREEN)+(GetSystemMetrics(SM_CXVIRTUALSCREEN)-width)/2;
        const int y=GetSystemMetrics(SM_YVIRTUALSCREEN)+(GetSystemMetrics(SM_CYVIRTUALSCREEN)-height)/2;
        HWND window=CreateWindowExW(0,cls.lpszClassName,L"RETSAR child-session input/display probe",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,
            x,y,width,height,nullptr,nullptr,cls.hInstance,&probe);
        if(!window){probe.Fail("create-window",GetLastError());return 5;}
        probe.started=GetTickCount64();ShowWindow(window,SW_SHOW);SetForegroundWindow(window);SetFocus(window);UpdateWindow(window);
        if(!SetTimer(window,Timer,250,nullptr)){probe.Fail("timer",GetLastError());probe.Finish();}
        MSG message{};BOOL result=0;
        while(!probe.finished&&(result=GetMessageW(&message,nullptr,0,0))>0){TranslateMessage(&message);DispatchMessageW(&message);}
        if(result<0)probe.Fail("message-loop",GetLastError());
        if(!probe.finished){probe.Fail("message-loop-ended",ERROR_CANCELLED);probe.Finish();}
        return probe.error==ERROR_SUCCESS&&!probe.journalFailed&&probe.cursorRestored&&probe.mouseMoves&&probe.keyDowns&&probe.keyUps?0:2;
    }catch(const std::invalid_argument& error){std::cerr<<error.what()<<'\n';return 64;}
    catch(...){std::cerr<<"Input probe failed. Inspect the private receipt.\n";return 5;}
}
