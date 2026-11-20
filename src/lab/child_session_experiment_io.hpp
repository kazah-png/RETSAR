#pragma once
#include "child_session_toggle.hpp"
#include "child_session_inventory.hpp"
#include <cwchar>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace seats::lab::child::experiment {
using namespace seats::lab::child;
constexpr wchar_t ReceiptRoot[]=L"D:\\RETSAR-Lab\\RETSAR\\private\\";
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
struct SessionRow {DWORD id=MAXDWORD;int state=0;bool hasUser=false;};
struct Inventory {bool valid=false;DWORD error=ERROR_SUCCESS;std::vector<SessionRow> rows;};

// Injected WTS boundary lets tests cover listener handling without querying
// Windows sessions. The production wrapper supplies the system functions.
struct InventoryApi {
    decltype(&WTSEnumerateSessionsW) enumerate=nullptr;
    decltype(&WTSQuerySessionInformationW) user=nullptr;
    decltype(&WTSFreeMemory) release=nullptr;
    decltype(&GetLastError) error=nullptr;
};
Inventory ReadInventoryWithApi(DWORD caller,DWORD resumeTarget,const InventoryApi& api) {
    Inventory result{};
    if(!api.enumerate||!api.user||!api.release||!api.error){result.error=ERROR_PROC_NOT_FOUND;return result;}
    PWTS_SESSION_INFOW sessions=nullptr;
    DWORD count=0;
    if(!api.enumerate(WTS_CURRENT_SERVER_HANDLE,0,1,&sessions,&count)) {
        result.error=api.error();return result;
    }
    struct SessionMemory {
        PWTS_SESSION_INFOW value;decltype(&WTSFreeMemory) release;
        ~SessionMemory(){if(value)release(value);}
    } memory{sessions,api.release};
    if(count==0||count>1024||!sessions){result.error=ERROR_INVALID_DATA;return result;}
    bool callerSeen=false,targetSeen=false;
    for(DWORD index=0;index<count;++index) {
        const auto& entry=sessions[index];
        for(const auto& previous:result.rows)if(previous.id==entry.SessionId){result.error=ERROR_INVALID_DATA;return result;}
        if(!RequiresUserObservation(static_cast<int>(entry.State))) {
            if(entry.SessionId==caller||(resumeTarget!=MAXDWORD&&entry.SessionId==resumeTarget)) {
                result.error=ERROR_INVALID_STATE;return result;
            }
            result.rows.push_back({entry.SessionId,static_cast<int>(entry.State),false});
            continue;
        }
        LPWSTR user=nullptr;DWORD bytes=0;
        const BOOL queried=api.user(WTS_CURRENT_SERVER_HANDLE,entry.SessionId,WTSUserName,&user,&bytes);
        const DWORD queryError=queried?ERROR_SUCCESS:api.error();
        struct UserMemory {LPWSTR value;decltype(&WTSFreeMemory) release;~UserMemory(){if(value)release(value);}} userMemory{user,api.release};
        if(!queried){result.error=queryError;return result;}
        if(!user||bytes<sizeof(wchar_t)||bytes>65536||bytes%sizeof(wchar_t)!=0) {
            result.error=ERROR_INVALID_DATA;return result;
        }
        const auto length=static_cast<std::size_t>(bytes/sizeof(wchar_t));
        bool terminated=false;
        for(std::size_t i=0;i<length;++i)if(user[i]==L'\0'){terminated=true;break;}
        if(!terminated){result.error=ERROR_INVALID_DATA;return result;}
        const bool hasUser=user[0]!=L'\0';
        result.rows.push_back({entry.SessionId,static_cast<int>(entry.State),hasUser});
        if(entry.SessionId==caller) {
            if(callerSeen||entry.State!=WTSActive||!hasUser) {
                result.error=ERROR_INVALID_STATE;return result;
            }
            callerSeen=true;
        } else if(resumeTarget!=MAXDWORD&&entry.SessionId==resumeTarget) {
            if(targetSeen||entry.State!=WTSDisconnected||!hasUser) {
                result.error=ERROR_INVALID_STATE;return result;
            }
            targetSeen=true;
        } else if(hasUser) {
            result.error=ERROR_BUSY;return result;
        }
    }
    if(!callerSeen||(resumeTarget!=MAXDWORD&&!targetSeen)){result.error=ERROR_NOT_FOUND;return result;}
    result.valid=true;
    return result;
}
Inventory ReadInventory(DWORD caller,DWORD resumeTarget=MAXDWORD) {
    return ReadInventoryWithApi(caller,resumeTarget,{&WTSEnumerateSessionsW,&WTSQuerySessionInformationW,&WTSFreeMemory,&GetLastError});
}
bool AcceptBaseline(const Snapshot& value) noexcept {
    const bool childAllowed=value.child.attempted&&(
        (value.child.succeeded&&value.child.value==MAXDWORD)||
        (!value.child.succeeded&&value.child.error==ERROR_NOT_FOUND));
    return value.apiAvailable&&value.enabled.attempted&&value.enabled.succeeded&&
        !value.enabled.value&&value.caller.succeeded&&value.caller.value!=0&&
        value.caller.value!=MAXDWORD&&value.console.attempted&&
        value.caller.value==value.console.value&&childAllowed;
}
std::string InventoryJson(const Inventory& value) {
    std::ostringstream out;
    out<<"{\"valid\":"<<(value.valid?"true":"false")<<",\"lastError\":"<<value.error<<",\"sessions\":[";
    for(std::size_t i=0;i<value.rows.size();++i) {
        if(i)out<<',';
        const auto& row=value.rows[i];
        out<<"{\"id\":"<<row.id<<",\"state\":"<<row.state
           <<",\"hasUser\":"<<(row.hasUser?"true":"false")<<'}';
    }
    out<<"]}";
    return out.str();
}
bool ValidReceiptPath(const wchar_t* raw,std::wstring& normalized) {
    if(!raw||std::wcslen(raw)>2048)return false;
    wchar_t full[32768]{};
    const DWORD size=GetFullPathNameW(raw,static_cast<DWORD>(std::size(full)),full,nullptr);
    if(size==0||size>=std::size(full))return false;
    normalized.assign(full,size);
    const std::size_t prefix=std::size(ReceiptRoot)-1;
    if(normalized.size()<=prefix||_wcsnicmp(normalized.c_str(),ReceiptRoot,prefix)!=0||
       normalized.find(L':',2)!=std::wstring::npos||
       normalized.back()==L'\\'||normalized.back()==L'.'||normalized.back()==L' ')return false;
    if(normalized.size()<6||normalized.substr(normalized.size()-6)!=L".jsonl")return false;
    // Inspect only ancestors of this exact authorized path. Reparse points are
    // rejected; no drive inventory, filesystem traversal or directory creation.
    for(std::size_t pos=3;pos<normalized.size();++pos)if(normalized[pos]==L'\\') {
        const auto ancestor=normalized.substr(0,pos);
        const DWORD attributes=GetFileAttributesW(ancestor.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)||
           (attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    }
    return true;
}
PersistenceResult Append(HANDLE receipt,const std::string& text) noexcept {
    if(text.size()>=static_cast<std::size_t>((std::numeric_limits<DWORD>::max)()))
        return {false,ERROR_BUFFER_OVERFLOW};
    DWORD written=0;
    if(!WriteFile(receipt,text.data(),static_cast<DWORD>(text.size()),&written,nullptr))
        return {false,GetLastError()};
    if(written!=text.size())return {false,ERROR_WRITE_FAULT};
    const char newline='\n';
    if(!WriteFile(receipt,&newline,1,&written,nullptr))return {false,GetLastError()};
    if(written!=1)return {false,ERROR_WRITE_FAULT};
    if(!FlushFileBuffers(receipt))return {false,GetLastError()};
    return {true,ERROR_SUCCESS};
}

}
