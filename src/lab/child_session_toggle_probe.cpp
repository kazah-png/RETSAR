#include "child_session_status.hpp"
#include "child_session_toggle.hpp"
#include <cwchar>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {
using namespace seats::lab::child;
constexpr wchar_t ReceiptRoot[]=L"D:\\RETSAR-Lab\\RETSAR\\private\\";
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
struct SessionRow {DWORD id=MAXDWORD;int state=0;bool hasUser=false;};
struct Inventory {bool valid=false;DWORD error=ERROR_SUCCESS;std::vector<SessionRow> rows;};

Inventory ReadInventory(DWORD caller) {
    Inventory result{};
    PWTS_SESSION_INFOW sessions=nullptr;
    DWORD count=0;
    if(!WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE,0,1,&sessions,&count)) {
        result.error=GetLastError();return result;
    }
    struct SessionMemory {
        PWTS_SESSION_INFOW value;
        ~SessionMemory(){if(value)WTSFreeMemory(value);}
    } memory{sessions};
    if(count==0||count>1024||!sessions){result.error=ERROR_INVALID_DATA;return result;}
    bool callerSeen=false;
    for(DWORD index=0;index<count;++index) {
        const auto& entry=sessions[index];
        LPWSTR user=nullptr;DWORD bytes=0;
        if(!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,entry.SessionId,
            WTSUserName,&user,&bytes)) {
            result.error=GetLastError();return result;
        }
        struct UserMemory {LPWSTR value;~UserMemory(){if(value)WTSFreeMemory(value);}} userMemory{user};
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
        } else if(hasUser) {
            result.error=ERROR_BUSY;return result;
        }
    }
    if(!callerSeen){result.error=ERROR_NOT_FOUND;return result;}
    result.valid=true;
    return result;
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
PersistenceResult Persist(const ToggleReport& report,void* context) noexcept {
    try{return Append(static_cast<Handle*>(context)->value,ToJson(report));}
    catch(...){return {false,ERROR_UNHANDLED_EXCEPTION};}
}
}

int wmain(int argc,wchar_t* argv[]) {
    if(argc==1||(argc==2&&std::wcscmp(argv[1],L"--help")==0)) {
        std::cout<<"RETSAR child-session enable/read/restore experiment\n"
            <<"Usage: RETSAR-child-session-toggle.exe --enable-read-restore --receipt <new.jsonl>\n"
            <<"The receipt must be under D:\\RETSAR-Lab\\RETSAR\\private\\ in an existing directory.\n"
            <<"Opening help makes no WTS queries. The explicit experiment temporarily enables\n"
            <<"child sessions, reads the setting, restores the original disabled setting and\n"
            <<"reads it again. It never connects or creates a session, desktop or display.\n"
            <<"A successful toggle does not establish Windows Home child-session support.\n";
        return 0;
    }
    if(argc!=4||std::wcscmp(argv[1],L"--enable-read-restore")!=0||
       std::wcscmp(argv[2],L"--receipt")!=0) {
        std::cerr<<"Invalid arguments. No session query or change was made.\n";return 64;
    }
    try {
        std::wstring receiptPath;
        if(!ValidReceiptPath(argv[3],receiptPath)) {
            std::cerr<<"Receipt path rejected. No session query or change was made.\n";return 64;
        }
        // Serialize RETSAR toggle probes in this caller's session. This cannot
        // lock unrelated applications out of the global Windows setting.
        Handle owner;
        owner.value=CreateMutexW(nullptr,TRUE,L"Local\\RETSAR.ChildSessionToggle");
        const DWORD mutexError=GetLastError();
        if(!owner.value||mutexError==ERROR_ALREADY_EXISTS) {
            if(!owner.value)owner.value=INVALID_HANDLE_VALUE;
            std::cerr<<"Another probe owns this experiment, or its lock could not be created.\n";
            return 3;
        }
        const auto first=Query(SystemApi());
        if(!AcceptBaseline(first)) {
            std::cout<<"{\"phase\":\"baseline-rejected\",\"baseline\":"<<ToJson(first)<<"}\n";
            return 3;
        }
        const auto inventory=ReadInventory(first.caller.value);
        const auto baseline=Query(SystemApi());
        if(!inventory.valid||!AcceptBaseline(baseline)||
           first.caller.value!=baseline.caller.value||first.console.value!=baseline.console.value) {
            std::cout<<"{\"phase\":\"guard-rejected\",\"baseline\":"<<ToJson(baseline)
                <<",\"inventory\":"<<InventoryJson(inventory)<<"}\n";return 3;
        }
        Handle receipt;
        receipt.value=CreateFileW(receiptPath.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,
            CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(receipt.value==INVALID_HANDLE_VALUE) {
            std::cout<<"{\"phase\":\"receipt-create-failed\",\"lastError\":"<<GetLastError()<<"}\n";
            return 4;
        }
        BY_HANDLE_FILE_INFORMATION fileInfo{};
        wchar_t finalPath[32768]{};
        const DWORD finalSize=GetFinalPathNameByHandleW(receipt.value,finalPath,
            static_cast<DWORD>(std::size(finalPath)),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        const std::wstring expectedFinal=L"\\\\?\\"+receiptPath;
        if(GetFileType(receipt.value)!=FILE_TYPE_DISK||
           !GetFileInformationByHandle(receipt.value,&fileInfo)||
           (fileInfo.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||
           finalSize==0||finalSize>=std::size(finalPath)||
           _wcsicmp(finalPath,expectedFinal.c_str())!=0) {
            std::cerr<<"Receipt handle identity rejected. No session change was made.\n";
            return 4;
        }
        FILETIME created{},exited{},kernel{},user{};
        if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)) {
            std::cout<<"{\"phase\":\"process-identity-failed\",\"lastError\":"<<GetLastError()<<"}\n";
            return 4;
        }
        const ULONGLONG creation=(static_cast<ULONGLONG>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
        std::ostringstream before;
        before<<"{\"schema\":\"retsar.child-session-toggle-owner.v1\",\"phase\":\"baseline\","
              <<"\"ownerProcessId\":"<<GetCurrentProcessId()<<",\"ownerCreationTime\":"<<creation
              <<",\"baseline\":"<<ToJson(baseline)<<",\"inventory\":"<<InventoryJson(inventory)
              <<",\"childAbsenceProven\":"<<(ClassifyChild(baseline)==ChildState::Absent?"true":"false")
              <<",\"connectAttempted\":false,\"physicalSeatVerified\":false}";
        const auto persisted=Append(receipt.value,before.str());
        if(!persisted.succeeded) {
            std::cout<<"{\"phase\":\"baseline-persist-failed\",\"lastError\":"<<persisted.error<<"}\n";
            return 4;
        }
        const auto result=RunToggle(SystemToggleApi(),{true,baseline.enabled},Persist,&receipt);
        std::cout<<ToJson(result)<<'\n';
        return result.safetyRestored&&result.enableConfirmed&&result.durableComplete?0:2;
    } catch(...) {
        std::cerr<<"Experiment preparation or output failed. Consult the private receipt if created.\n";
        return 5;
    }
}
