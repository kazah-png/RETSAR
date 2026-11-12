#include "probe_args.hpp"
#include "../json.hpp"
#include "../safe_c_path.hpp"
#include <sddl.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>

namespace {
template<class Interface> class ComPtr {
    Interface* value_=nullptr;
public:
    ComPtr()=default;
    ~ComPtr(){if(value_)value_->Release();}
    ComPtr(const ComPtr&)=delete;
    ComPtr& operator=(const ComPtr&)=delete;
    Interface* operator->()const noexcept{return value_;}
    Interface** GetAddressOf()noexcept{return &value_;}
    template<class Other> HRESULT As(ComPtr<Other>* result)const noexcept{
        return value_?value_->QueryInterface(__uuidof(Other),reinterpret_cast<void**>(result->GetAddressOf())):E_POINTER;
    }
};
constexpr wchar_t WindowClass[]=L"RETSAR_LaboratorySeatProbe";
constexpr UINT_PTR RefreshTimer=1;
constexpr auto CounterLimit=static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
void increment(uint64_t& value)noexcept{if(value<CounterLimit)++value;}

std::string utf8(const std::wstring& value){
    if(value.empty())return {};
    const auto count=static_cast<int>(value.size());
    const int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),count,nullptr,0,nullptr,nullptr);
    if(!bytes)throw std::runtime_error("Cannot encode diagnostic text.");
    std::string out(static_cast<size_t>(bytes),'\0');
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),count,out.data(),bytes,nullptr,nullptr))
        throw std::runtime_error("Cannot encode diagnostic text.");
    return out;
}
std::string hex32(uint32_t value){std::ostringstream out;out<<"0x"<<std::hex<<std::uppercase<<std::setw(8)<<std::setfill('0')<<value;return out.str();}
std::wstring wideAscii(const std::string& value){return {value.begin(),value.end()};}

struct CloseToken {void operator()(void* value)const noexcept{if(value)CloseHandle(value);}};
struct FreeSidText {void operator()(wchar_t* value)const noexcept{if(value)LocalFree(value);}};
struct ProcessIdentity {
    DWORD processId=0,tokenOpenError=0,userSidError=0,authenticationIdError=0,creationTimeError=0;
    std::optional<std::string> userSid;
    std::optional<LUID> authenticationId;
    std::optional<int64_t> creationTime;
};
DWORD lastErrorOrInvalidData(){const auto error=GetLastError();return error?error:ERROR_INVALID_DATA;}
void observeTokenUser(HANDLE token,ProcessIdentity& out){
    DWORD needed=0;
    const BOOL sized=GetTokenInformation(token,TokenUser,nullptr,0,&needed);
    const DWORD sizeError=sized?ERROR_INVALID_DATA:GetLastError();
    if(sizeError!=ERROR_INSUFFICIENT_BUFFER){out.userSidError=sizeError?sizeError:ERROR_INVALID_DATA;return;}
    if(needed<sizeof(TOKEN_USER)||needed>65536){out.userSidError=ERROR_INVALID_DATA;return;}
    std::vector<BYTE> buffer(needed);
    if(!GetTokenInformation(token,TokenUser,buffer.data(),static_cast<DWORD>(buffer.size()),&needed)){
        out.userSidError=lastErrorOrInvalidData();return;
    }
    if(needed<sizeof(TOKEN_USER)||needed>buffer.size()){out.userSidError=ERROR_INVALID_DATA;return;}
    TOKEN_USER user{};std::memcpy(&user,buffer.data(),sizeof(user));
    // TOKEN_USER contains a pointer to the variable-sized SID within this buffer.
    const auto begin=reinterpret_cast<uintptr_t>(buffer.data());
    const auto sidAddress=reinterpret_cast<uintptr_t>(user.User.Sid);
    constexpr size_t sidHeaderBytes=offsetof(SID,SubAuthority);
    if(sidAddress<begin||sidAddress-begin>needed||needed-(sidAddress-begin)<sidHeaderBytes){
        out.userSidError=ERROR_INVALID_SID;return;
    }
    SID sidHeader{};std::memcpy(&sidHeader,user.User.Sid,sidHeaderBytes);
    if(GetSidLengthRequired(sidHeader.SubAuthorityCount)>needed-(sidAddress-begin)||!IsValidSid(user.User.Sid)){
        out.userSidError=ERROR_INVALID_SID;return;
    }
    LPWSTR sidText=nullptr;
    if(!ConvertSidToStringSidW(user.User.Sid,&sidText)){out.userSidError=lastErrorOrInvalidData();return;}
    const std::unique_ptr<wchar_t,FreeSidText> ownedSidText(sidText);
    if(!sidText){out.userSidError=ERROR_INVALID_DATA;return;}
    out.userSid=utf8(sidText);
}
ProcessIdentity observeOwnProcessIdentity(){
    ProcessIdentity out;out.processId=GetCurrentProcessId();
    // This pseudo-handle and token are only for the process running this probe.
    const HANDLE process=GetCurrentProcess();
    FILETIME created{},exited{},kernel{},user{};
    if(!GetProcessTimes(process,&created,&exited,&kernel,&user))out.creationTimeError=lastErrorOrInvalidData();
    else{
        const uint64_t ticks=(static_cast<uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
        if(!ticks)out.creationTimeError=ERROR_INVALID_DATA;
        else if(ticks>CounterLimit)out.creationTimeError=ERROR_ARITHMETIC_OVERFLOW;
        else out.creationTime=static_cast<int64_t>(ticks);
    }
    HANDLE token=nullptr;
    if(!OpenProcessToken(process,TOKEN_QUERY,&token)){
        out.tokenOpenError=lastErrorOrInvalidData();
        out.userSidError=out.tokenOpenError;out.authenticationIdError=out.tokenOpenError;return out;
    }
    const std::unique_ptr<void,CloseToken> ownedToken(token);
    observeTokenUser(token,out);
    TOKEN_STATISTICS statistics{};DWORD returned=0;
    if(!GetTokenInformation(token,TokenStatistics,&statistics,sizeof(statistics),&returned))
        out.authenticationIdError=lastErrorOrInvalidData();
    else if(returned!=sizeof(statistics))out.authenticationIdError=ERROR_INVALID_DATA;
    else out.authenticationId=statistics.AuthenticationId;
    return out;
}

struct ObjectName {
    std::wstring value;
    DWORD error=0;
};
ObjectName objectName(HANDLE object){
    ObjectName out;DWORD needed=0;
    if(!object){out.error=ERROR_INVALID_HANDLE;return out;}
    if(GetUserObjectInformationW(object,UOI_NAME,nullptr,0,&needed)||GetLastError()!=ERROR_INSUFFICIENT_BUFFER||needed<sizeof(wchar_t)||needed>32768){
        out.error=GetLastError();if(!out.error)out.error=ERROR_INVALID_DATA;return out;
    }
    std::vector<wchar_t> buffer((static_cast<size_t>(needed)+sizeof(wchar_t)-1)/sizeof(wchar_t),L'\0');
    if(!GetUserObjectInformationW(object,UOI_NAME,buffer.data(),needed,&needed)){out.error=GetLastError();return out;}
    out.value.assign(buffer.data());return out;
}

struct GraphicsRuntime {
    HMODULE module=nullptr;
    ~GraphicsRuntime(){if(module)FreeLibrary(module);}
    GraphicsRuntime()=default;
    GraphicsRuntime(const GraphicsRuntime&)=delete;
    GraphicsRuntime& operator=(const GraphicsRuntime&)=delete;
};
struct GraphicsObservation {
    // Declared first so device/context are released before the DLL is unloaded.
    GraphicsRuntime runtime;
    HRESULT deviceResult=E_PENDING,adapterResult=E_PENDING;
    D3D_FEATURE_LEVEL featureLevel=static_cast<D3D_FEATURE_LEVEL>(0);
    DXGI_ADAPTER_DESC1 adapter{};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    bool hardwareObserved=false;
};
void observeGraphics(GraphicsObservation& graphics){
    // Do not load the graphics runtime at all on help or rejected arguments.
    graphics.runtime.module=LoadLibraryExW(L"d3d11.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!graphics.runtime.module){graphics.deviceResult=HRESULT_FROM_WIN32(GetLastError());return;}
    const auto address=GetProcAddress(graphics.runtime.module,"D3D11CreateDevice");
    decltype(&D3D11CreateDevice) createDevice=nullptr;
    static_assert(sizeof(address)==sizeof(createDevice));
    std::memcpy(&createDevice,&address,sizeof(createDevice));
    if(!createDevice){graphics.deviceResult=HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);return;}
    graphics.deviceResult=createDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,graphics.device.GetAddressOf(),&graphics.featureLevel,graphics.context.GetAddressOf());
    if(FAILED(graphics.deviceResult))return;
    ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;ComPtr<IDXGIAdapter1> adapter1;
    graphics.adapterResult=graphics.device.As(&dxgi);
    if(SUCCEEDED(graphics.adapterResult))graphics.adapterResult=dxgi->GetAdapter(adapter.GetAddressOf());
    if(SUCCEEDED(graphics.adapterResult))graphics.adapterResult=adapter.As(&adapter1);
    if(SUCCEEDED(graphics.adapterResult))graphics.adapterResult=adapter1->GetDesc1(&graphics.adapter);
    graphics.hardwareObserved=SUCCEEDED(graphics.adapterResult)&&(graphics.adapter.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)==0;
}

struct ProbeState {
    seats::lab::ProbeOptions options;
    DWORD actualSession=0;
    ProcessIdentity processIdentity;
    ObjectName station,desktop;
    GraphicsObservation graphics;
    uint64_t mouseMoves=0,mouseButtons=0,mouseWheels=0,keyDowns=0,refreshes=0;
    std::chrono::steady_clock::time_point started;
    std::string termination="windowClosed";
};

void paint(HWND window,ProbeState& state){
    PAINTSTRUCT ps{};const HDC dc=BeginPaint(window,&ps);
    RECT rect{};GetClientRect(window,&rect);
    const HBRUSH background=CreateSolidBrush(RGB(21,25,36));FillRect(dc,&rect,background);DeleteObject(background);
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(230,236,249));
    const auto oldFont=SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));
    const auto& graphics=state.graphics;
    std::wostringstream text;
    text<<L"RETSAR - LABORATORY PROBE (does not create a seat)\n\n"
        <<L"Current / expected session: "<<state.actualSession<<L" / "<<state.options.expectedSession
        <<L"     Reference: "<<state.options.referenceSession<<L" (not inspected)\n"
        <<L"Window station: "<<(state.station.error?L"unavailable":state.station.value)
        <<L"     Desktop: "<<(state.desktop.error?L"unavailable":state.desktop.value)<<L"\n\n"
        <<L"D3D11 hardware-device creation: "<<wideAscii(hex32(static_cast<uint32_t>(graphics.deviceResult)))<<L"\n"
        <<L"Hardware adapter observed: "<<(graphics.hardwareObserved?L"YES":L"NO / UNVERIFIED")<<L"\n"
        <<L"Adapter: "<<(SUCCEEDED(graphics.adapterResult)?graphics.adapter.Description:L"unavailable")<<L"\n"
        <<L"A device is not proof of GPU presentation or display ownership.\n\n"
        <<L"Messages delivered ONLY to this window (no key contents stored):\n"
        <<L"Mouse move: "<<state.mouseMoves<<L"     Buttons: "<<state.mouseButtons
        <<L"     Wheel: "<<state.mouseWheels<<L"     Key down: "<<state.keyDowns<<L"\n\n"
        <<L"Activate this window manually to test keyboard input.\n"
        <<L"Compare the reference seat independently; these counters do not prove isolation.\n"
        <<L"The animation below uses GDI, not a D3D swap chain.\n"
        <<L"Close the window to finish and emit the report.";
    auto content=text.str();RECT textRect{20,18,rect.right-20,rect.bottom-55};
    DrawTextW(dc,content.c_str(),static_cast<int>(content.size()),&textRect,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_NOPREFIX);
    const LONG available=rect.right>130?rect.right-130:1;
    const LONG x=20+static_cast<LONG>((state.refreshes*8)%static_cast<uint64_t>(available));
    RECT bar{x,rect.bottom-35,x+80,rect.bottom-21};
    const HBRUSH color=CreateSolidBrush(RGB(111,231,200));FillRect(dc,&bar,color);DeleteObject(color);
    SelectObject(dc,oldFont);EndPaint(window,&ps);
}

LRESULT CALLBACK windowProcedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
    auto state=reinterpret_cast<ProbeState*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){
        const auto create=reinterpret_cast<const CREATESTRUCTW*>(lparam);
        state=static_cast<ProbeState*>(create->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));
    }
    if(!state)return DefWindowProcW(window,message,wparam,lparam);
    switch(message){
    case WM_TIMER:if(wparam==RefreshTimer){increment(state->refreshes);InvalidateRect(window,nullptr,FALSE);}return 0;
    case WM_PAINT:paint(window,*state);return 0;
    case WM_MOUSEMOVE:increment(state->mouseMoves);break;
    case WM_LBUTTONDOWN:case WM_MBUTTONDOWN:case WM_RBUTTONDOWN:case WM_XBUTTONDOWN:increment(state->mouseButtons);break;
    case WM_MOUSEWHEEL:case WM_MOUSEHWHEEL:increment(state->mouseWheels);break;
    case WM_KEYDOWN:case WM_SYSKEYDOWN:increment(state->keyDowns);break;
    case WM_DESTROY:KillTimer(window,RefreshTimer);PostQuitMessage(0);return 0;
    default:break;
    }
    return DefWindowProcW(window,message,wparam,lparam);
}

seats::Json objectJson(const ObjectName& value){return seats::Json::Object{{"name",utf8(value.value)},{"win32Error",value.error}};}
seats::Json identityJson(const ProcessIdentity& value){
    seats::Json authenticationId;
    if(value.authenticationId){
        const auto luid=*value.authenticationId;
        authenticationId=seats::Json::Object{{"highPart",luid.HighPart},{"lowPart",luid.LowPart},
            {"hex",hex32(static_cast<uint32_t>(luid.HighPart))+hex32(luid.LowPart).substr(2)}};
    }
    return seats::Json::Object{{"scope","own process only"},{"processId",value.processId},
        {"userSid",value.userSid?seats::Json(*value.userSid):seats::Json()},
        {"authenticationId",std::move(authenticationId)},
        {"processCreationTimeFileTime",value.creationTime?seats::Json(*value.creationTime):seats::Json()},
        {"creationTimeUnit","100-nanosecond intervals since 1601-01-01 UTC"},
        {"identityEvidenceComplete",value.userSid.has_value()&&value.authenticationId.has_value()&&value.creationTime.has_value()},
        {"ownershipProven",false},
        {"win32Errors",seats::Json::Object{{"openProcessToken",value.tokenOpenError},{"tokenUser",value.userSidError},
            {"tokenStatistics",value.authenticationIdError},{"getProcessTimes",value.creationTimeError}}}};
}
seats::Json report(const ProbeState& state){
    const auto& graphics=state.graphics;
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-state.started).count();
    return seats::Json::Object{
        {"schema","retsar.lab-seat-probe.v1"},{"createdSession",false},{"independenceNotProven",true},
        {"scope","current process session and own window only"},{"sessionGatePassed",true},
        {"actualSession",state.actualSession},{"expectedSession",state.options.expectedSession},
        {"referenceSession",state.options.referenceSession},{"referenceSessionObserved",false},
        {"processIdentity",identityJson(state.processIdentity)},
        {"windowStation",objectJson(state.station)},{"desktop",objectJson(state.desktop)},
        {"graphics",seats::Json::Object{
            {"requestedDriver","HARDWARE"},{"deviceCreationHresult",hex32(static_cast<uint32_t>(graphics.deviceResult))},
            {"adapterQueryHresult",hex32(static_cast<uint32_t>(graphics.adapterResult))},{"hardwareAdapterObserved",graphics.hardwareObserved},
            {"adapterDescription",SUCCEEDED(graphics.adapterResult)?utf8(graphics.adapter.Description):std::string{}},
            {"adapterLuidHigh",graphics.adapter.AdapterLuid.HighPart},{"adapterLuidLow",graphics.adapter.AdapterLuid.LowPart},
            {"featureLevel",hex32(static_cast<uint32_t>(graphics.featureLevel))},{"gpuPresentationTested",false},
            {"physicalOutputBindingTested",false},{"animationRenderer","GDI"}}},
        {"input",seats::Json::Object{{"captureScope","own HWND messages"},{"contentsRecorded",false},
            {"mouseMoves",state.mouseMoves},{"buttonDownMessages",state.mouseButtons},{"wheelMessages",state.mouseWheels},
            {"keyDownMessages",state.keyDowns},{"inputInjectedByProbe",false},{"eventOriginVerified",false},{"referenceInputObserved",false}}},
        {"durationMs",elapsed},{"termination",state.termination}};
}

void writeNewReport(const std::wstring& path,const std::string& bytes){
    seats::LockedCParents parents{std::filesystem::path(path)};bool existed=false;
    const HANDLE file=parents.openFile(GENERIC_WRITE,CREATE_NEW,existed);
    try{
        size_t offset=0;
        while(offset<bytes.size()){
            DWORD written=0;
            if(!WriteFile(file,bytes.data()+offset,static_cast<DWORD>(bytes.size()-offset),&written,nullptr)||!written)
                throw std::runtime_error("Report write failed; a partial new file may remain.");
            offset+=written;
        }
        if(!FlushFileBuffers(file))throw std::runtime_error("Report flush failed; new file durability is uncertain.");
    }catch(...){CloseHandle(file);throw;}
    CloseHandle(file);
}

int interactive(const seats::lab::ProbeOptions& options){
    // This gate is before output paths, user objects, D3D or any window activity.
    DWORD current=0;
    if(!ProcessIdToSessionId(GetCurrentProcessId(),&current))throw std::runtime_error("Cannot obtain this process session ID.");
    if(!seats::lab::currentSessionAllowed(options,current)){
        std::cerr<<"Session gate refused: current="<<current<<", expected="<<options.expectedSession
            <<", reference="<<options.referenceSession<<". No window, graphics device or output file opened.\n";
        return 2;
    }
    ProbeState state;state.options=options;state.actualSession=current;state.started=std::chrono::steady_clock::now();
    state.processIdentity=observeOwnProcessIdentity();
    state.station=objectName(GetProcessWindowStation());state.desktop=objectName(GetThreadDesktop(GetCurrentThreadId()));
    observeGraphics(state.graphics);
    const HINSTANCE instance=GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};windowClass.cbSize=sizeof(windowClass);windowClass.lpfnWndProc=windowProcedure;
    windowClass.hInstance=instance;windowClass.hCursor=LoadCursorW(nullptr,IDC_ARROW);windowClass.lpszClassName=WindowClass;
    if(!RegisterClassExW(&windowClass))throw std::runtime_error("Cannot register probe window class.");
    const HWND window=CreateWindowExW(0,WindowClass,L"RETSAR laboratory probe - isolation NOT proven",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,900,490,nullptr,nullptr,instance,&state);
    if(!window){UnregisterClassW(WindowClass,instance);throw std::runtime_error("Cannot create probe window.");}
    if(!SetTimer(window,RefreshTimer,200,nullptr)){
        DestroyWindow(window);UnregisterClassW(WindowClass,instance);throw std::runtime_error("Cannot start probe animation timer.");
    }
    if(!SetWindowPos(window,nullptr,0,0,0,0,SWP_SHOWWINDOW|SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER)){
        DestroyWindow(window);UnregisterClassW(WindowClass,instance);throw std::runtime_error("Cannot show probe window without activation.");
    }
    MSG message{};BOOL result=0;
    while((result=GetMessageW(&message,nullptr,0,0))>0){TranslateMessage(&message);DispatchMessageW(&message);}
    if(result==-1){state.termination="messageLoopError";DestroyWindow(window);}
    UnregisterClassW(WindowClass,instance);
    const auto json=report(state).dump()+"\n";
    if(options.output)writeNewReport(*options.output,json);
    std::cout<<json;
    return result==-1?3:0;
}
}

int wmain(int argc,wchar_t** argv){
    try{
        const std::vector<std::wstring> args(argv+1,argv+argc);
        const auto options=seats::lab::parseProbeArgs(args);
        if(!options.interactive){
            std::cout<<"RETSAR laboratory seat probe\n"
                <<"Default / --help: print this text without observing the host.\n"
                <<"--interactive --expected-session N --reference-session M [--output C:\\existing-folder\\new.json]\n"
                <<"N and M must be different positive session IDs. Run only in an explicitly prepared laboratory session.\n"
                <<"The current process must already belong to N; this program creates no session.\n"
                <<"Opens its own non-activating window; records counts only from that window. Activate it manually.\n"
                <<"Creates a D3D11 hardware device, but does not test GPU presentation or bind displays. Animation uses GDI.\n"
                <<"Does not inspect the reference session, inject input, install components or prove independence.\n"
                <<"On close: emit JSON; optional C: file must be new. No overwrite.\n";
            return 0;
        }
        return interactive(options);
    }catch(const std::exception& error){std::cerr<<"RETSAR probe: "<<error.what()<<'\n';return 1;}
}
