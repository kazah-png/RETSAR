#include "backend.hpp"
#include "version.hpp"
#include <windows.h>
#include <wtsapi32.h>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace seats {
namespace {
std::string utf8(const std::wstring& wide){
    if(wide.empty())return {};
    const int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide.data(),static_cast<int>(wide.size()),nullptr,0,nullptr,nullptr);
    if(count<=0)throw std::runtime_error("Windows returned text that could not be encoded as UTF-8.");
    std::string out(static_cast<size_t>(count),'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide.data(),static_cast<int>(wide.size()),out.data(),count,nullptr,nullptr);return out;
}
Json rect(const RECT& r){return Json::Object{{"left",r.left},{"top",r.top},{"right",r.right},{"bottom",r.bottom}};}
void issue(Json::Array& issues,const char* api,DWORD code,const char* detail){issues.emplace_back(Json::Object{{"api",api},{"win32Code",code},{"detail",detail}});}
std::string utcNow(){SYSTEMTIME t{};GetSystemTime(&t);std::ostringstream out;out<<std::setfill('0')<<std::setw(4)<<t.wYear<<'-'<<std::setw(2)<<t.wMonth<<'-'<<std::setw(2)<<t.wDay<<'T'<<std::setw(2)<<t.wHour<<':'<<std::setw(2)<<t.wMinute<<':'<<std::setw(2)<<t.wSecond<<'Z';return out.str();}
Json windowsVersion(Json::Array& issues){
    // RtlGetVersion is a read-only query; GetVersionEx can report compatibility versions.
    using VersionQuery=LONG(WINAPI*)(OSVERSIONINFOEXW*);
    const HMODULE module=GetModuleHandleW(L"ntdll.dll");
    const auto query=module?reinterpret_cast<VersionQuery>(GetProcAddress(module,"RtlGetVersion")):nullptr;
    OSVERSIONINFOEXW version{};version.dwOSVersionInfoSize=sizeof(version);
    if(!query){issue(issues,"RtlGetVersion",ERROR_PROC_NOT_FOUND,"Version query unavailable.");return nullptr;}
    const LONG status=query(&version);
    if(status<0){issues.emplace_back(Json::Object{{"api","RtlGetVersion"},{"ntStatus",status},{"detail","Version query failed."}});return nullptr;}
    return Json::Object{{"major",version.dwMajorVersion},{"minor",version.dwMinorVersion},{"build",version.dwBuildNumber},
        {"productType",version.wProductType==VER_NT_WORKSTATION?"workstation":version.wProductType==VER_NT_SERVER?"server":"domain_controller"},{"query","RtlGetVersion"}};
}
struct MonitorContext{Json::Array items;Json::Array* issues;bool complete=true;};
BOOL CALLBACK monitorCallback(HMONITOR monitor,HDC,LPRECT,LPARAM value){
    auto& context=*reinterpret_cast<MonitorContext*>(value);
    MONITORINFOEXW info{};info.cbSize=sizeof(info);
    if(!GetMonitorInfoW(monitor,&info)){issue(*context.issues,"GetMonitorInfoW",GetLastError(),"A visible monitor could not be read.");context.complete=false;return TRUE;}
    try{context.items.emplace_back(Json::Object{{"id",utf8(info.szDevice)},{"primary",bool(info.dwFlags&MONITORINFOF_PRIMARY)},
        {"desktopRect",rect(info.rcMonitor)},{"workRect",rect(info.rcWork)}});}
    catch(...){context.complete=false;return FALSE;}
    return TRUE;
}
Json monitors(Json::Array& issues){
    MonitorContext context{{},&issues,true};SetLastError(ERROR_SUCCESS);
    const BOOL ok=EnumDisplayMonitors(nullptr,nullptr,monitorCallback,reinterpret_cast<LPARAM>(&context));
    if(!ok)issue(issues,"EnumDisplayMonitors",GetLastError(),"The current desktop monitor inventory is incomplete.");
    return Json::Object{{"querySucceeded",ok&&context.complete},{"scope","monitors visible to the current desktop; not a machine-wide seat map"},
        {"coordinateSpace","desktop coordinates of this per-monitor-DPI-aware diagnostic process"},{"items",std::move(context.items)}};
}
Json inputDevices(Json::Array& issues){
    std::vector<RAWINPUTDEVICELIST> devices;UINT count=0;bool succeeded=false;
    for(unsigned attempt=0;attempt<3&&!succeeded;++attempt){
        if(GetRawInputDeviceList(nullptr,&count,sizeof(RAWINPUTDEVICELIST))==static_cast<UINT>(-1)){
            issue(issues,"GetRawInputDeviceList",GetLastError(),"Could not obtain Raw Input metadata count.");break;
        }
        if(count>10000){issue(issues,"GetRawInputDeviceList",ERROR_NOT_ENOUGH_MEMORY,"Unexpected device count exceeds bounded inventory limit.");break;}
        devices.resize(count);
        if(count==0){succeeded=true;break;}
        const UINT received=GetRawInputDeviceList(devices.data(),&count,sizeof(RAWINPUTDEVICELIST));
        if(received==static_cast<UINT>(-1)){
            const DWORD code=GetLastError();if(code==ERROR_INSUFFICIENT_BUFFER)continue;
            issue(issues,"GetRawInputDeviceList",code,"Could not enumerate Raw Input metadata.");break;
        }
        devices.resize(received);succeeded=true;
    }
    if(!succeeded&&issues.empty())issue(issues,"GetRawInputDeviceList",ERROR_RETRY,"Device inventory changed repeatedly.");
    Json::Array items;unsigned ignoredHid=0;bool complete=succeeded;
    if(succeeded)for(const auto& device:devices){
        if(device.dwType!=RIM_TYPEMOUSE&&device.dwType!=RIM_TYPEKEYBOARD){++ignoredHid;continue;}
        RID_DEVICE_INFO info{};info.cbSize=sizeof(info);UINT size=sizeof(info);
        if(GetRawInputDeviceInfoW(device.hDevice,RIDI_DEVICEINFO,&info,&size)==static_cast<UINT>(-1)){
            issue(issues,"GetRawInputDeviceInfoW(RIDI_DEVICEINFO)",GetLastError(),"Device metadata incomplete.");complete=false;continue;
        }
        UINT chars=0;
        if(GetRawInputDeviceInfoW(device.hDevice,RIDI_DEVICENAME,nullptr,&chars)==static_cast<UINT>(-1)||chars>32767){
            issue(issues,"GetRawInputDeviceInfoW(RIDI_DEVICENAME)",GetLastError(),"Device interface name unavailable.");complete=false;continue;
        }
        std::vector<wchar_t> path(static_cast<size_t>(chars)+1,L'\0');
        if(GetRawInputDeviceInfoW(device.hDevice,RIDI_DEVICENAME,path.data(),&chars)==static_cast<UINT>(-1)){
            issue(issues,"GetRawInputDeviceInfoW(RIDI_DEVICENAME)",GetLastError(),"Device interface name changed or became unavailable.");complete=false;continue;
        }
        Json::Object item{{"id",utf8(path.data())},{"kind",device.dwType==RIM_TYPEMOUSE?"mouse":"keyboard"},{"assignedSeat",nullptr}};
        if(device.dwType==RIM_TYPEMOUSE)item.emplace_back("metadata",Json::Object{{"buttons",info.mouse.dwNumberOfButtons},{"sampleRate",info.mouse.dwSampleRate},{"horizontalWheel",bool(info.mouse.fHasHorizontalWheel)}});
        else item.emplace_back("metadata",Json::Object{{"type",info.keyboard.dwType},{"subtype",info.keyboard.dwSubType},{"functionKeys",info.keyboard.dwNumberOfFunctionKeys},{"totalKeys",info.keyboard.dwNumberOfKeysTotal}});
        items.emplace_back(std::move(item));
    }
    return Json::Object{{"querySucceeded",succeeded},{"complete",complete},{"scope","Raw Input mouse/keyboard metadata visible to this process"},
        {"otherHidDevicesIgnored",ignoredHid},{"readsInputEvents",false},{"opensHidHandles",false},{"items",std::move(items)}};
}
const char* sessionState(WTS_CONNECTSTATE_CLASS state){
    switch(state){case WTSActive:return "active";case WTSConnected:return "connected";case WTSConnectQuery:return "connect_query";
    case WTSShadow:return "shadow";case WTSDisconnected:return "disconnected";case WTSIdle:return "idle";case WTSListen:return "listen";
    case WTSReset:return "reset";case WTSDown:return "down";case WTSInit:return "initializing";default:return "unknown";}
}
Json sessions(Json::Array& issues){
    PWTS_SESSION_INFOW list=nullptr;DWORD count=0;Json::Array items;
    const bool ok=WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE,0,1,&list,&count)!=FALSE;
    if(ok){for(DWORD i=0;i<count;++i)items.emplace_back(Json::Object{{"id",list[i].SessionId},{"state",sessionState(list[i].State)}});}
    else issue(issues,"WTSEnumerateSessionsW",GetLastError(),"Session enumeration unavailable to this process.");
    if(list)WTSFreeMemory(list);
    return Json::Object{{"querySucceeded",ok},{"scope","WTSEnumerateSessionsW results visible to the current user"},{"items",std::move(items)},
        {"limitations",Json::Array{"An absent session does not prove that no other workstation/session is running.","No user names, credentials or other-session processes are queried."}}};
}
}
Json WindowsInventoryBackend::inspect()const{
    Json::Array issues;DWORD session=0;
    const bool currentOk=ProcessIdToSessionId(GetCurrentProcessId(),&session)!=FALSE;
    if(!currentOk)issue(issues,"ProcessIdToSessionId",GetLastError(),"Current session identifier unavailable.");
    auto version=windowsVersion(issues);auto outputs=monitors(issues);auto inputs=inputDevices(issues);auto visibleSessions=sessions(issues);
    return Json::Object{{"schema","retsar.inventory.v1"},{"capturedAtUtc",utcNow()},
        {"project",Json::Object{{"name","RETSAR"},{"version",Version},{"license","MIT"},{"operationalMultiseat",false}}},
        {"backend",Json::Object{{"name",backendName()},{"readOnly",true},{"capabilities",capabilityJson(capabilities())}}},
        {"windows",std::move(version)},{"currentSession",Json::Object{{"querySucceeded",currentOk},{"id",currentOk?Json(session):Json(nullptr)}}},
        {"monitors",std::move(outputs)},{"inputDevices",std::move(inputs)},{"sessions",std::move(visibleSessions)},{"issues",std::move(issues)},
        {"limitations",Json::Array{"This is an inventory component, not an operational multiseat engine.",
            "Monitor visibility and Raw Input metadata do not establish ownership or independent routing.",
            "No services, users, drivers, input hooks, display assignments or session state are changed.",
            "The future target of two independent workstations and four Roblox clients is not implemented or validated."}}};
}
}
