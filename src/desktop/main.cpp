#include "desktop_model.hpp"
#include <shellapi.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <algorithm>
#include <array>
#include <cwchar>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace retsar::desktop;
constexpr UINT RefreshReady=WM_APP+41;
constexpr int NavOverview=1010,NavDisplays=1011,NavActivity=1012,RefreshButton=1020,OpenButton=1021,GuideButton=1022;
constexpr COLORREF Background=RGB(11,16,32),Sidebar=RGB(8,13,26),Surface=RGB(21,30,49),Border=RGB(39,53,77);
constexpr COLORREF Accent=RGB(93,228,199),Cyan=RGB(121,207,242),Text=RGB(239,244,252),Muted=RGB(155,173,206),Amber=RGB(240,198,115);
constexpr wchar_t ReceiptDirectory[]=L"D:\\RETSAR-Lab\\RETSAR\\private";
struct Handle {HANDLE value=nullptr;~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}Handle()=default;Handle(const Handle&)=delete;Handle&operator=(const Handle&)=delete;};
struct Viewer {HWND window=nullptr;DWORD process=0;ULONGLONG created=0;std::wstring path;};
struct Display {RECT area{};bool primary=false;};
struct Observation {
    Facts facts;Viewer viewer;std::vector<Display> displays;
    DWORD inventoryError=0,displayError=0,memoryError=0;bool memoryKnown=false;DWORD memoryLoad=0;ULONGLONG memoryTotal=0,memoryAvailable=0;
};
struct QueryBox {
    std::mutex mutex;bool closed=false,ready=false;std::unique_ptr<Observation> result;
};
bool Equal(const std::wstring& a,const std::wstring& b)noexcept{return _wcsicmp(a.c_str(),b.c_str())==0;}
std::wstring Parent(const std::wstring& value){const auto at=value.find_last_of(L"\\/");return at==std::wstring::npos?L"":value.substr(0,at);}
std::wstring ExecutableDirectory(){wchar_t path[32768]{};const DWORD n=GetModuleFileNameW(nullptr,path,static_cast<DWORD>(std::size(path)));return n&&n<std::size(path)?Parent(path):L"";}
bool Present(const std::wstring& path,bool directory)noexcept {
    const DWORD value=GetFileAttributesW(path.c_str());return value!=INVALID_FILE_ATTRIBUTES&&!(value&FILE_ATTRIBUTE_REPARSE_POINT)&&
        ((value&FILE_ATTRIBUTE_DIRECTORY)!=0)==directory;
}
ULONGLONG Creation(HANDLE process)noexcept {FILETIME c{},e{},k{},u{};if(!GetProcessTimes(process,&c,&e,&k,&u))return 0;return (static_cast<ULONGLONG>(c.dwHighDateTime)<<32)|c.dwLowDateTime;}
bool ViewerMatches(Viewer& viewer,const std::wstring& expected,bool fresh) {
    if(!viewer.window||!IsWindow(viewer.window))return false;
    wchar_t cls[128]{};if(!GetClassNameW(viewer.window,cls,static_cast<int>(std::size(cls)))||std::wcscmp(cls,L"RETSAR.ChildSessionExperiment"))return false;
    DWORD process=0,session=0;GetWindowThreadProcessId(viewer.window,&process);
    DWORD current=0;if(!ProcessIdToSessionId(GetCurrentProcessId(),&current)||!ProcessIdToSessionId(process,&session)||session!=current)return false;
    Handle handle;handle.value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,process);if(!handle.value)return false;
    wchar_t path[32768]{};DWORD count=static_cast<DWORD>(std::size(path));if(!QueryFullProcessImageNameW(handle.value,0,path,&count)||!Equal(path,expected))return false;
    const auto created=Creation(handle.value);if(!created)return false;
    if(!fresh&&(viewer.process!=process||viewer.created!=created||!Equal(viewer.path,path)))return false;
    viewer.process=process;viewer.created=created;viewer.path=path;return true;
}
struct FindContext {const std::wstring* path;Viewer result;unsigned count=0;};
BOOL CALLBACK FindViewer(HWND window,LPARAM value) {
    auto& context=*reinterpret_cast<FindContext*>(value);Viewer candidate{};candidate.window=window;
    if(ViewerMatches(candidate,*context.path,true)){context.result=std::move(candidate);++context.count;}return TRUE;
}
BOOL CALLBACK FindDisplay(HMONITOR monitor,HDC,LPRECT,LPARAM value) {
    auto& result=*reinterpret_cast<Observation*>(value);MONITORINFO info{sizeof(info)};
    if(!GetMonitorInfoW(monitor,&info)){result.displayError=GetLastError();return FALSE;}
    result.displays.push_back({info.rcMonitor,(info.dwFlags&MONITORINFOF_PRIMARY)!=0});return result.displays.size()<64;
}
Observation Read(const std::wstring& viewerPath) {
    Observation out;out.facts.session=seats::lab::child::Query(seats::lab::child::SystemApi());
    out.facts.viewerFilePresent=Present(viewerPath,false);out.facts.receiptDirectoryPresent=Present(ReceiptDirectory,true);
    PWTS_SESSION_INFOW rows=nullptr;DWORD count=0;
    if(WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE,0,1,&rows,&count)) {
        struct Memory{PWTS_SESSION_INFOW p;~Memory(){if(p)WTSFreeMemory(p);}} memory{rows};
        out.facts.inventoryComplete=rows&&count>0&&count<=1024;
        if(!out.facts.inventoryComplete)out.inventoryError=ERROR_INVALID_DATA;
        if(out.facts.inventoryComplete)for(DWORD i=0;i<count;++i) {
            LPWSTR name=nullptr;DWORD bytes=0;
            const BOOL valid=WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,rows[i].SessionId,WTSUserName,&name,&bytes);
            const DWORD error=valid?ERROR_SUCCESS:GetLastError();
            const bool bounded=valid&&name&&bytes>=sizeof(wchar_t)&&bytes%sizeof(wchar_t)==0&&bytes<=65536&&name[bytes/sizeof(wchar_t)-1]==L'\0';
            if(!bounded){out.facts.inventoryComplete=false;out.inventoryError=error?error:ERROR_INVALID_DATA;}
            const bool hasUser=bounded&&name[0]!=L'\0';if(name)WTSFreeMemory(name);
            out.facts.rows.push_back({rows[i].SessionId,static_cast<int>(rows[i].State),hasUser});
        }
    } else out.inventoryError=GetLastError();
    if(!EnumDisplayMonitors(nullptr,nullptr,FindDisplay,reinterpret_cast<LPARAM>(&out))&&!out.displayError){out.displayError=GetLastError();if(!out.displayError)out.displayError=ERROR_GEN_FAILURE;}
    if(out.displays.empty()&&!out.displayError)out.displayError=ERROR_NOT_FOUND;
    MEMORYSTATUSEX memory{sizeof(memory)};
    if(GlobalMemoryStatusEx(&memory)){out.memoryKnown=true;out.memoryLoad=memory.dwMemoryLoad;out.memoryTotal=memory.ullTotalPhys;out.memoryAvailable=memory.ullAvailPhys;}else out.memoryError=GetLastError();
    FindContext context{&viewerPath};EnumWindows(FindViewer,reinterpret_cast<LPARAM>(&context));
    if(context.count==1){out.viewer=std::move(context.result);out.facts.verifiedViewerPresent=true;}
    return out;
}
Observation Preview() {
    Observation out;out.facts.designPreview=true;
    auto& s=out.facts.session;s.apiAvailable=true;s.enabled={true,true,0,true};s.caller={true,true,0,2};s.child={true,true,0,3};s.console={true,2};
    out.facts.rows={{2,WTSActive,true},{3,WTSActive,true}};out.facts.inventoryComplete=true;
    out.displays={{{0,0,1920,1080},true},{{1920,0,3840,1080},false}};out.memoryKnown=true;out.memoryLoad=38;out.memoryTotal=16ull*1024*1024*1024;out.memoryAvailable=10ull*1024*1024*1024;return out;
}
const wchar_t* StateLabel(int value)noexcept {
    switch(value){case WTSActive:return L"Active";case WTSDisconnected:return L"Disconnected";case WTSConnected:return L"Connecting";case WTSListen:return L"Listener";default:return L"Not active";}
}
std::wstring SessionDetail(const Observation& value,DWORD id) {
    if(id==MAXDWORD)return L"Not observed";
    std::wstring text=L"Session "+std::to_wstring(id);
    for(const auto& row:value.facts.rows)if(row.id==id){text+=L"  \u00b7  ";text+=StateLabel(row.state);break;}return text;
}
struct BackBuffer {
    HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ original=nullptr;int width=0,height=0;
    ~BackBuffer(){Reset();}
    void Reset()noexcept {if(dc&&original)SelectObject(dc,original);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);dc=nullptr;bitmap=nullptr;original=nullptr;}
    bool Fit(HDC source,int w,int h)noexcept {
        if(dc&&width==w&&height==h)return true;Reset();width=w;height=h;if(w<=0||h<=0)return false;
        dc=CreateCompatibleDC(source);bitmap=CreateCompatibleBitmap(source,w,h);if(!dc||!bitmap){Reset();return false;}
        original=SelectObject(dc,bitmap);return true;
    }
};
struct App {
    HWND window=nullptr;HINSTANCE instance=nullptr;HICON appIcon=nullptr;UINT dpi=96;int tab=0;bool preview=false,refreshing=false,launchPending=false;
    std::wstring viewerPath;Observation observation;std::shared_ptr<QueryBox> box=std::make_shared<QueryBox>();
    std::vector<std::wstring> activity;std::array<HWND,6> buttons{};HWND statusLabel=nullptr,detailLabel=nullptr;HBRUSH backgroundBrush=CreateSolidBrush(Background);HFONT title=nullptr,heading=nullptr,body=nullptr,fontSmall=nullptr;BackBuffer buffer;
    ~App(){CloseFonts();if(backgroundBrush)DeleteObject(backgroundBrush);std::lock_guard lock(box->mutex);box->closed=true;}
    int P(int value)const noexcept{return MulDiv(value,static_cast<int>(dpi),96);}
    RECT R(int x,int y,int w,int h)const noexcept{return {P(x),P(y),P(x+w),P(y+h)};}
    int Width()const noexcept {RECT r{};GetClientRect(window,&r);return MulDiv(r.right,96,static_cast<int>(dpi));}
    int Height()const noexcept {RECT r{};GetClientRect(window,&r);return MulDiv(r.bottom,96,static_cast<int>(dpi));}
    void CloseFonts()noexcept{for(HFONT f:{title,heading,body,fontSmall})if(f)DeleteObject(f);title=heading=body=fontSmall=nullptr;}
    HFONT Font(int size,int weight)const noexcept{return CreateFontW(-P(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");}
    void Fonts(){CloseFonts();title=Font(30,FW_SEMIBOLD);heading=Font(19,FW_SEMIBOLD);body=Font(14,FW_NORMAL);fontSmall=Font(12,FW_NORMAL);for(HWND b:buttons)if(b)SendMessageW(b,WM_SETFONT,reinterpret_cast<WPARAM>(body),TRUE);if(statusLabel)SendMessageW(statusLabel,WM_SETFONT,reinterpret_cast<WPARAM>(body),TRUE);if(detailLabel)SendMessageW(detailLabel,WM_SETFONT,reinterpret_cast<WPARAM>(fontSmall),TRUE);}
    void Note(std::wstring value){activity.insert(activity.begin(),std::move(value));if(activity.size()>8)activity.resize(8);InvalidateRect(window,nullptr,FALSE);}
    Decision Current()const noexcept {if(refreshing||launchPending)return {Action::None,Availability::Loading};return Decide(observation.facts);}
    bool Compact()const noexcept{return Height()<720;}
    int CardY()const noexcept{return Compact()?126:145;}
    int CardHeight()const noexcept{return Compact()?142:188;}
    int ActionY()const noexcept{return Compact()?290:370;}
    int DetailY()const noexcept{return Compact()?Height()-106:614;}
    void Layout(){
        const int w=Width(),h=Height(),actionY=ActionY();
        MoveWindow(buttons[0],P(18),P(154),P(176),P(44),TRUE);MoveWindow(buttons[1],P(18),P(208),P(176),P(44),TRUE);MoveWindow(buttons[2],P(18),P(262),P(176),P(44),TRUE);
        MoveWindow(buttons[3],P(w-152),P(Compact()?28:38),P(124),P(38),TRUE);
        MoveWindow(buttons[4],P(240),P(actionY),P(194),P(44),TRUE);ShowWindow(buttons[4],tab==0?SW_SHOW:SW_HIDE);
        MoveWindow(buttons[5],P(18),P(h-82),P(176),P(38),TRUE);
        const auto decision=Current();SetWindowTextW(buttons[4],ActionLabel(decision));
        MoveWindow(statusLabel,P(452),P(actionY+3),P(w-480),P(24),TRUE);MoveWindow(detailLabel,P(240),P(DetailY()),P(w-268),P(58),TRUE);
        ShowWindow(statusLabel,tab==0?SW_SHOW:SW_HIDE);ShowWindow(detailLabel,tab==0&&h>=475?SW_SHOW:SW_HIDE);
        SetWindowTextW(statusLabel,preview?L"Design preview - sample data":AvailabilityTitle(decision.availability));
        SetWindowTextW(detailLabel,preview?L"Sample desktops and displays. No system inventory was collected.":AvailabilityDetail(decision.availability));
        EnableWindow(buttons[4],!preview&&decision.action!=Action::None);
        EnableWindow(buttons[3],!refreshing&&!launchPending&&!preview);InvalidateRect(window,nullptr,FALSE);
    }
    void Refresh(){
        if(refreshing||launchPending||preview)return;
        refreshing=true;Layout();const auto mailbox=box;const auto path=viewerPath;const auto target=window;
        try{std::thread([mailbox,path,target]{
            std::unique_ptr<Observation> value;
            try{value=std::make_unique<Observation>(Read(path));}catch(...){value.reset();}
            std::lock_guard lock(mailbox->mutex);if(mailbox->closed)return;
            mailbox->result=std::move(value);mailbox->ready=true;PostMessageW(target,RefreshReady,0,0);
        }).detach();}catch(...){refreshing=false;Note(L"Could not start the read-only refresh.");Layout();}
    }
    void Accept(){
        {std::lock_guard lock(box->mutex);if(!box->ready)return;if(box->result)observation=std::move(*box->result);else observation={};box->result.reset();box->ready=false;}
        refreshing=false;
        const auto& session=observation.facts.session;
        bool partial=false;
        if(!observation.facts.inventoryComplete){partial=true;Note(L"Session inventory unavailable or incomplete. Error "+std::to_wstring(observation.inventoryError)+L".");}
        if(!session.apiAvailable||!session.enabled.succeeded||!session.caller.succeeded||!session.child.succeeded||!session.console.attempted||session.console.value==MAXDWORD){
            partial=true;
            std::wstring detail=L"Session status incomplete.";
            if(!session.apiAvailable)detail+=L" Required APIs unavailable.";
            if(!session.enabled.succeeded)detail+=L" Enabled-state error "+std::to_wstring(session.enabled.error)+L".";
            if(!session.caller.succeeded)detail+=L" Caller error "+std::to_wstring(session.caller.error)+L".";
            if(!session.child.succeeded)detail+=L" Child relationship error "+std::to_wstring(session.child.error)+L" (unknown).";
            if(!session.console.attempted||session.console.value==MAXDWORD)detail+=L" No console observed.";
            Note(std::move(detail));
        }
        if(observation.displayError){partial=true;Note(L"Display information unavailable or incomplete. Error "+std::to_wstring(observation.displayError)+L".");}
        if(!observation.memoryKnown){partial=true;Note(L"Computer memory information unavailable. Error "+std::to_wstring(observation.memoryError)+L".");}
        Note(partial?L"Refresh finished with unavailable details. See the observations below.":L"Desktop and display information refreshed. No session settings changed.");Layout();
    }
    void Open(){
        if(preview||refreshing||launchPending)return;
        const auto decision=Current();
        if(decision.action==Action::ShowViewer){auto existing=observation.viewer;if(!ViewerMatches(existing,viewerPath,false)){Note(L"The observed viewer changed. Refresh before trying again.");Refresh();return;}
            if(IsIconic(existing.window))ShowWindow(existing.window,SW_RESTORE);
            if(!SetForegroundWindow(existing.window))Note(L"The viewer is open. Select it from the Windows taskbar.");
            else Note(L"Desktop viewer brought forward.");return;}
        if(!CanLaunch(decision))return;
        // The viewer repeats admission immediately before mutation. The GUI is
        // an explicit launch surface, never an alternate session backend.
        launchPending=true;Layout();
        GUID id{};wchar_t guid[40]{};
        if(FAILED(CoCreateGuid(&id))||!StringFromGUID2(id,guid,static_cast<int>(std::size(guid)))){launchPending=false;Note(L"Could not prepare a connection receipt.");Layout();return;}
        std::wstring token;for(wchar_t c:guid)if((c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f')||(c>=L'A'&&c<=L'F'))token+=c;
        const std::wstring receipt=std::wstring(ReceiptDirectory)+L"\\desktop-"+token+L".jsonl";
        std::wstring args=decision.action==Action::ResumeDesktop?L"--resume-child-session "+std::to_wstring(decision.child):L"--connect-child";
        args+=L" --receipt \""+receipt+L"\"";
        Handle pinned;pinned.value=CreateFileW(viewerPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(pinned.value==INVALID_HANDLE_VALUE){launchPending=false;Note(L"The desktop viewer is unavailable. Rebuild it and refresh.");Layout();return;}
        BY_HANDLE_FILE_INFORMATION info{};wchar_t finalPath[32768]{};const DWORD n=GetFinalPathNameByHandleW(pinned.value,finalPath,static_cast<DWORD>(std::size(finalPath)),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        const bool exact=GetFileInformationByHandle(pinned.value,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&n&&n<std::size(finalPath)&&Equal(finalPath,L"\\\\?\\"+viewerPath);
        if(!exact){launchPending=false;Note(L"The desktop viewer path could not be verified.");Layout();return;}
        SHELLEXECUTEINFOW execute{sizeof(execute)};execute.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC;execute.hwnd=window;execute.lpVerb=L"runas";execute.lpFile=viewerPath.c_str();execute.lpParameters=args.c_str();execute.nShow=SW_SHOWNORMAL;
        const BOOL launched=ShellExecuteExW(&execute);const DWORD error=launched?0:GetLastError();if(execute.hProcess)CloseHandle(execute.hProcess);
        launchPending=false;
        if(launched)Note(L"Viewer started. Complete Windows identity confirmation there, then refresh this overview.");
        else Note(error==ERROR_CANCELLED?L"Opening was cancelled. Existing desktops were left alone.":L"Windows could not start the viewer. Error "+std::to_wstring(error)+L".");
        Refresh();Layout();
    }
    void Fill(HDC dc,RECT rect,COLORREF color)const {HBRUSH brush=CreateSolidBrush(color);FillRect(dc,&rect,brush);DeleteObject(brush);}
    void Round(HDC dc,RECT rect,COLORREF fill,COLORREF stroke,int radius=14)const {
        HBRUSH brush=CreateSolidBrush(fill);HPEN pen=CreatePen(PS_SOLID,P(1),stroke);auto oldBrush=SelectObject(dc,brush);auto oldPen=SelectObject(dc,pen);
        RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,P(radius),P(radius));SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(pen);DeleteObject(brush);
    }
    void TextAt(HDC dc,const std::wstring& value,RECT rect,HFONT font,COLORREF color,UINT flags=DT_LEFT|DT_TOP|DT_WORDBREAK|DT_NOPREFIX)const {
        const auto previous=SelectObject(dc,font);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);DrawTextW(dc,value.c_str(),static_cast<int>(value.size()),&rect,flags);SelectObject(dc,previous);
    }
    void Logo(HDC dc,int x,int y,int size)const {if(appIcon){DrawIconEx(dc,P(x),P(y),appIcon,P(size),P(size),0,nullptr,DI_NORMAL);return;}Round(dc,R(x+size/4,y,size*3/4,size*3/4),Surface,Cyan,8);Round(dc,R(x,y+size/4,size*3/4,size*3/4),Sidebar,Accent,8);Fill(dc,R(x+size/8,y+size*5/8,size/2,2),Accent);}
    void Chip(HDC dc,const std::wstring& value,int x,int y,int w,COLORREF color)const {Round(dc,R(x,y,w,26),Surface,Border,12);TextAt(dc,value,R(x+10,y+5,w-20,18),fontSmall,color,DT_CENTER|DT_SINGLELINE|DT_NOPREFIX);}
    void DesktopTile(HDC dc,int x,int y,int width,const std::wstring& caption,const std::wstring& detail,bool main)const {
        const bool compact=Compact();Round(dc,R(x,y,width,CardHeight()),Surface,Border);Logo(dc,x+22,y+(compact?15:22),compact?26:32);
        TextAt(dc,caption,R(x+22,y+(compact?53:75),width-44,27),heading,Text);
        TextAt(dc,detail,R(x+22,y+(compact?88:111),width-44,compact?24:42),body,Muted);
        TextAt(dc,main?L"Your everyday workspace":L"A separate Windows session",R(x+22,y+(compact?119:158),width-44,18),fontSmall,main?Cyan:Accent);
    }
    void PaintOverview(HDC dc){
        const int w=Width(),h=Height(),content=w-268,tile=(content-18)/2;const auto decision=Current();const bool compact=Compact();
        TextAt(dc,L"Your desktops",R(240,compact?29:43,content-140,42),title,Text);
        TextAt(dc,L"Keep everyday work and another workspace side by side.",R(240,compact?80:94,content,24),body,Muted);
        const DWORD parent=observation.facts.session.console.attempted?observation.facts.session.console.value:MAXDWORD;
        const DWORD child=observation.facts.session.child.succeeded?observation.facts.session.child.value:MAXDWORD;
        DesktopTile(dc,240,CardY(),tile,L"Main desktop",refreshing?L"Reading session...":SessionDetail(observation,parent),true);
        DesktopTile(dc,258+tile,CardY(),tile,L"Independent desktop",refreshing?L"Reading session...":SessionDetail(observation,child),false);
        const wchar_t* hint=preview?L"Viewer actions are disabled in this preview.":CanLaunch(decision)?L"Windows may ask you to confirm your identity.":decision.action==Action::ShowViewer?L"Bring its existing viewer to the front.":decision.availability==Availability::AlreadyActive?L"Select its viewer from the Windows taskbar.":L"Refresh to check availability.";
        TextAt(dc,hint,R(452,ActionY()+32,content-212,35),fontSmall,Muted);
        // This informational card yields space on short screens; no functional
        // action, state or observation is scaled down or hidden to make it fit.
        if(h>=580){const int top=compact?362:450,height=compact?104:142;
            Round(dc,R(240,top,content,height),Surface,Border);
            TextAt(dc,L"A workspace for your applications",R(262,top+16,content-44,27),heading,Text);
            TextAt(dc,L"Browse, work, build, or run tools in a separate desktop. Each application keeps its normal Windows interface.",R(262,top+52,content-44,48),body,Muted);
        }
    }
    void PaintDisplays(HDC dc){
        const int content=Width()-268,h=Height();TextAt(dc,L"Displays",R(240,Compact()?29:43,content-140,42),title,Text);
        TextAt(dc,L"Screens visible to this desktop.",R(240,Compact()?80:94,content,24),body,Muted);
        int y=Compact()?126:145,index=0;for(const auto& display:observation.displays){if(index>=4||y+92>h-140)break;
            Round(dc,R(240,y,content,92),Surface,Border);Logo(dc,262,y+27,32);
            TextAt(dc,L"Display "+std::to_wstring(index+1),R(316,y+20,200,26),heading,Text);
            TextAt(dc,std::to_wstring(display.area.right-display.area.left)+L" \u00d7 "+std::to_wstring(display.area.bottom-display.area.top)+L" pixels",R(316,y+52,content-100,25),body,Muted);
            Chip(dc,display.primary?L"Primary":L"Extended",Width()-164,y+28,110,display.primary?Accent:Cyan);y+=106;++index;}
        if(observation.displays.empty())TextAt(dc,refreshing?L"Reading connected screens...":L"No display information is available. Try Refresh.",R(240,164,content,60),body,Muted);
        if(observation.displays.size()>static_cast<std::size_t>(index))TextAt(dc,L"Additional screens: "+std::to_wstring(observation.displays.size()-static_cast<std::size_t>(index)),R(240,y,content,24),body,Muted);
        TextAt(dc,L"The desktop viewer can be moved between screens. Exclusive physical monitor and keyboard/mouse assignment is still in development.",R(240,h-116,content,67),body,Muted);
    }
    void PaintActivity(HDC dc){
        const int content=Width()-268,h=Height();TextAt(dc,L"Activity",R(240,Compact()?29:43,content-140,42),title,Text);
        TextAt(dc,L"Local observations from this window. No background polling.",R(240,Compact()?80:94,content,24),body,Muted);
        Round(dc,R(240,145,content,100),Surface,Border);
        TextAt(dc,L"Computer memory",R(262,165,content-44,25),heading,Text);
        std::wstring memory=observation.memoryKnown?std::to_wstring(BoundedPercent(observation.memoryLoad))+L"% in use across the whole computer":L"Not available";
        TextAt(dc,memory,R(262,202,content-44,24),body,Muted);
        int y=274;for(const auto& line:activity){if(y+47>h-125)break;Fill(dc,R(241,y+7,5,5),Accent);TextAt(dc,line,R(259,y,content-20,47),body,Muted);y+=54;}
        if(activity.empty())TextAt(dc,L"Refresh to read the current state.",R(240,y,content,40),body,Muted);
        TextAt(dc,L"Private connection receipts stay on this computer. The development viewer currently uses D:\\RETSAR-Lab\\RETSAR\\private.",R(240,h-106,content,52),fontSmall,Muted);
    }
    void Paint(HDC dc){
        const int w=Width(),h=Height();Fill(dc,R(0,0,w,h),Background);Fill(dc,R(0,0,212,h),Sidebar);Fill(dc,R(211,0,1,h),Border);
        Logo(dc,24,30,34);TextAt(dc,L"RETSAR",R(72,28,125,30),heading,Text);TextAt(dc,L"DESKTOP",R(72,58,125,20),fontSmall,Muted);
        Chip(dc,preview?L"DESIGN PREVIEW":L"RESEARCH PREVIEW",18,104,176,preview?Amber:Accent);
        if(h>=500)TextAt(dc,L"One computer.\nMore room to work.",R(24,h-166,166,55),body,Muted);
        if(tab==0)PaintOverview(dc);else if(tab==1)PaintDisplays(dc);else PaintActivity(dc);
        Fill(dc,R(240,h-42,w-268,1),Border);TextAt(dc,preview?L"Sample data only":refreshing?L"Refreshing...":L"Read-only overview  \u00b7  Refresh on demand",R(240,h-26,w-268,18),fontSmall,Muted);
    }
    void DrawButton(const DRAWITEMSTRUCT& item){
        const bool selected=(item.CtlID>=NavOverview&&item.CtlID<=NavActivity&&static_cast<int>(item.CtlID-NavOverview)==tab);
        const bool primary=item.CtlID==OpenButton;const bool disabled=(item.itemState&ODS_DISABLED)!=0;
        const COLORREF fill=disabled?Surface:primary?Accent:selected?Surface:Sidebar;
        Fill(item.hDC,item.rcItem,(item.CtlID==OpenButton||item.CtlID==RefreshButton)?Background:Sidebar);
        Round(item.hDC,item.rcItem,(item.itemState&ODS_SELECTED)&&!disabled?Border:fill,selected?Border:fill,10);
        wchar_t caption[160]{};GetWindowTextW(item.hwndItem,caption,static_cast<int>(std::size(caption)));
        RECT textRect=item.rcItem;InflateRect(&textRect,-P(12),0);
        TextAt(item.hDC,caption,textRect,body,disabled?Muted:primary?Background:selected?Text:Muted,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        if(item.itemState&ODS_FOCUS){RECT focus=item.rcItem;InflateRect(&focus,-P(4),-P(4));DrawFocusRect(item.hDC,&focus);}
    }
};
LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);app->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(!app)return DefWindowProcW(window,message,wp,lp);
    switch(message){
    case WM_CREATE:{app->dpi=GetDpiForWindow(window);if(!app->dpi)app->dpi=96;const BOOL dark=TRUE;DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
        constexpr int ids[]={NavOverview,NavDisplays,NavActivity,RefreshButton,OpenButton,GuideButton};
        constexpr const wchar_t* captions[]={L"Overview",L"Displays",L"Activity",L"Refresh  \u00b7  F5",L"Open desktop",L"Project guide"};
        for(std::size_t i=0;i<app->buttons.size();++i)app->buttons[i]=CreateWindowExW(0,L"BUTTON",captions[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ids[i])),app->instance,nullptr);
        app->statusLabel=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,1,1,window,nullptr,app->instance,nullptr);
        app->detailLabel=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,1,1,window,nullptr,app->instance,nullptr);
        app->Fonts();app->Layout();if(app->preview){app->observation=Preview();app->Note(L"Design preview uses sample data. No session or device inventory was collected.");app->Layout();}else app->Refresh();return 0;}
    case WM_COMMAND:{const int id=LOWORD(wp);if(id>=NavOverview&&id<=NavActivity){app->tab=id-NavOverview;app->Layout();}
        else if(id==RefreshButton)app->Refresh();else if(id==OpenButton)app->Open();
        else if(id==GuideButton)ShellExecuteW(window,L"open",L"https://github.com/kazah-png/RETSAR/blob/main/docs/DESKTOP.md",nullptr,nullptr,SW_SHOWNORMAL);return 0;}
    case RefreshReady:app->Accept();return 0;
    case WM_SIZE:app->Layout();return 0;
    case WM_DPICHANGED:{app->dpi=HIWORD(wp);const auto* area=reinterpret_cast<const RECT*>(lp);SetWindowPos(window,nullptr,area->left,area->top,area->right-area->left,area->bottom-area->top,SWP_NOZORDER|SWP_NOACTIVATE);app->Fonts();app->Layout();return 0;}
    case WM_GETMINMAXINFO:{auto* info=reinterpret_cast<MINMAXINFO*>(lp);MONITORINFO monitor{sizeof(monitor)};
        if(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor))info->ptMinTrackSize={std::min<LONG>(app->P(900),monitor.rcWork.right-monitor.rcWork.left),std::min<LONG>(app->P(640),monitor.rcWork.bottom-monitor.rcWork.top)};
        else info->ptMinTrackSize={app->P(900),app->P(640)};return 0;}
    case WM_CTLCOLORSTATIC:{HDC dc=reinterpret_cast<HDC>(wp);SetBkColor(dc,Background);SetTextColor(dc,reinterpret_cast<HWND>(lp)==app->statusLabel?(app->preview?Amber:Accent):Muted);return reinterpret_cast<LRESULT>(app->backgroundBrush);}
    case WM_ERASEBKGND:return 1;
    case WM_DRAWITEM:app->DrawButton(*reinterpret_cast<const DRAWITEMSTRUCT*>(lp));return TRUE;
    case WM_PAINT:{PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT r{};GetClientRect(window,&r);if(app->buffer.Fit(dc,r.right,r.bottom)){app->Paint(app->buffer.dc);BitBlt(dc,0,0,r.right,r.bottom,app->buffer.dc,0,0,SRCCOPY);}else app->Paint(dc);EndPaint(window,&paint);return 0;}
    case WM_CLOSE:{std::lock_guard lock(app->box->mutex);app->box->closed=true;DestroyWindow(window);return 0;}
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }return DefWindowProcW(window,message,wp,lp);
}
void Output(const char* value){const HANDLE stream=GetStdHandle(STD_OUTPUT_HANDLE);if(stream&&stream!=INVALID_HANDLE_VALUE){DWORD written=0;WriteFile(stream,value,static_cast<DWORD>(std::strlen(value)),&written,nullptr);}}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
    int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)return 64;
    bool preview=false;int result=0;
    if(argc==2&&std::wcscmp(argv[1],L"--help")==0){Output("RETSAR Desktop\nUsage: RETSAR-Desktop.exe [--preview | --help]\nDefault opens a read-only desktop overview. Session changes require an explicit Open or Resume action.\n--preview shows clearly labelled sample data without session or device inventory; viewer actions are disabled.\n--help performs no GUI, session query, file write, or setting change.\n");LocalFree(argv);return 0;}
    if(argc==2&&std::wcscmp(argv[1],L"--preview")==0)preview=true;else if(argc!=1)result=64;
    LocalFree(argv);if(result)return result;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    App app;app.dpi=GetDpiForSystem();if(!app.dpi)app.dpi=96;app.instance=instance;app.appIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));app.preview=preview;app.viewerPath=ExecutableDirectory()+L"\\lab\\RETSAR-child-session-client.exe";
    WNDCLASSEXW cls{sizeof(cls)};cls.hInstance=instance;cls.lpfnWndProc=WindowProc;cls.lpszClassName=L"RETSAR.DesktopConsole";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));if(!cls.hIcon)cls.hIcon=LoadIconW(nullptr,IDI_APPLICATION);cls.hIconSm=cls.hIcon;
    if(!RegisterClassExW(&cls))return 5;
    MONITORINFO monitor{sizeof(monitor)};if(!GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&monitor))return 5;
    const int workWidth=monitor.rcWork.right-monitor.rcWork.left,workHeight=monitor.rcWork.bottom-monitor.rcWork.top;
    const int width=std::min(app.P(1080),workWidth-24),height=std::min(app.P(820),workHeight-24);
    HWND window=CreateWindowExW(0,cls.lpszClassName,preview?L"RETSAR Desktop - Design preview":L"RETSAR Desktop",WS_OVERLAPPEDWINDOW,monitor.rcWork.left+(workWidth-width)/2,monitor.rcWork.top+(workHeight-height)/2,width,height,nullptr,nullptr,instance,&app);
    if(!window)return 5;ShowWindow(window,show);UpdateWindow(window);
    ACCEL key{FVIRTKEY,VK_F5,RefreshButton};HACCEL keys=CreateAcceleratorTableW(&key,1);
    MSG message{};BOOL fetched=0;while((fetched=GetMessageW(&message,nullptr,0,0))>0){if(keys&&TranslateAcceleratorW(window,keys,&message))continue;if(IsDialogMessageW(window,&message))continue;TranslateMessage(&message);DispatchMessageW(&message);}
    if(keys)DestroyAcceleratorTable(keys);return fetched<0?5:0;
}