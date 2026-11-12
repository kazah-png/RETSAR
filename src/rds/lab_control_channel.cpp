#include "lab_control_channel.hpp"
#include <sddl.h>
#include <winsvc.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <new>

namespace seats::rds::lab {
namespace {
constexpr wchar_t listenerKey[]=L"SYSTEM\\CurrentControlSet\\Control\\Terminal Server\\WinStations\\RETSAR-Lab";
constexpr wchar_t classKey[]=L"SOFTWARE\\Classes\\CLSID\\{E09044AF-A721-4D68-9F49-4527E9E51376}";
constexpr wchar_t classId[]=L"{E09044AF-A721-4D68-9F49-4527E9E51376}";
constexpr wchar_t dllPath[]=L"C:\\RETSAR-Lab\\provider-activation\\RETSAR-RdsBridge.dll";
constexpr ULONGLONG LeaseMilliseconds=180000;
constexpr DWORD IoMilliseconds=5000;
struct Handle {
    HANDLE value=nullptr;
    Handle()=default;
    explicit Handle(HANDLE handle):value(handle){}
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
};
struct Key {HKEY value=nullptr;~Key(){if(value)RegCloseKey(value);}};
struct ScHandle {SC_HANDLE value=nullptr;~ScHandle(){if(value)CloseServiceHandle(value);}};
HRESULT lastError()noexcept {const auto error=GetLastError();return HRESULT_FROM_WIN32(error?error:ERROR_GEN_FAILURE);}
bool readString(HKEY key,const wchar_t* name,wchar_t* value,DWORD capacity)noexcept{
    DWORD type=0,bytes=capacity*sizeof(wchar_t);
    const auto result=RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(value),&bytes);
    return result==ERROR_SUCCESS&&type==REG_SZ&&bytes>=sizeof(wchar_t)&&
        bytes<=capacity*sizeof(wchar_t)&&bytes%sizeof(wchar_t)==0&&
        value[bytes/sizeof(wchar_t)-1]==L'\0'&&
        wcsnlen_s(value,capacity)==bytes/sizeof(wchar_t)-1;
}
bool readStringAt(const wchar_t* path,const wchar_t* name,wchar_t* value,DWORD capacity)noexcept{
    Key key;if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,path,0,KEY_READ|KEY_WOW64_64KEY,&key.value)!=ERROR_SUCCESS)return false;
    return readString(key.value,name,value,capacity);
}
bool exactDword(HKEY key,const wchar_t* name,DWORD expected)noexcept{
    DWORD value=0,type=0,bytes=sizeof(value);
    return RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(&value),&bytes)==ERROR_SUCCESS&&
        type==REG_DWORD&&bytes==sizeof(value)&&value==expected;
}
bool shape(HKEY key,DWORD children,DWORD values)noexcept{
    DWORD actualChildren=0,actualValues=0;
    return RegQueryInfoKeyW(key,nullptr,nullptr,nullptr,&actualChildren,nullptr,nullptr,
        &actualValues,nullptr,nullptr,nullptr,nullptr)==ERROR_SUCCESS&&
        children==actualChildren&&values==actualValues;
}
bool noThreadToken()noexcept{
    Handle token;
    if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&token.value))return false;
    return GetLastError()==ERROR_NO_TOKEN;
}
bool tokenSid(HANDLE token,WELL_KNOWN_SID_TYPE wanted)noexcept{
    alignas(TOKEN_USER) BYTE user[256]{};DWORD bytes=0;
    if(!GetTokenInformation(token,TokenUser,user,sizeof(user),&bytes))return false;
    return IsWellKnownSid(reinterpret_cast<TOKEN_USER*>(user)->User.Sid,wanted)!=FALSE;
}
bool appContainer(HANDLE token,bool& result)noexcept{
    DWORD value=0,bytes=0;
    if(!GetTokenInformation(token,TokenIsAppContainer,&value,sizeof(value),&bytes))return false;
    result=value!=0;return true;
}
bool elevatedAdministrator(HANDLE token)noexcept{
    bool container=true;if(!appContainer(token,container)||container)return false;
    if(tokenSid(token,WinLocalSystemSid))return true;
    TOKEN_ELEVATION elevation{};DWORD bytes=0;
    if(!GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&bytes)||!elevation.TokenIsElevated)return false;
    BYTE administrators[SECURITY_MAX_SID_SIZE]{};DWORD sidBytes=sizeof(administrators);
    if(!CreateWellKnownSid(WinBuiltinAdministratorsSid,nullptr,administrators,&sidBytes))return false;
    // CheckTokenMembership accepts an impersonation token, including a duplicate
    // of the controller's primary token. Deny-only groups never authorize.
    Handle impersonation;
    if(!DuplicateTokenEx(token,TOKEN_QUERY,nullptr,SecurityImpersonation,TokenImpersonation,&impersonation.value))return false;
    BOOL member=FALSE;
    return CheckTokenMembership(impersonation.value,administrators,&member)&&member;
}
HRESULT verifyGuest()noexcept{
    if constexpr(sizeof(void*)!=8)return E_ACCESSDENIED;
    wchar_t name[64]{};DWORD count=static_cast<DWORD>(std::size(name));
    if(!GetComputerNameW(name,&count)||wcscmp(name,L"RETSARLAB")!=0)return E_ACCESSDENIED;
    wchar_t manufacturer[128]{},product[128]{},build[32]{},type[32]{};
    if(!readStringAt(L"HARDWARE\\DESCRIPTION\\System\\BIOS",L"SystemManufacturer",manufacturer,static_cast<DWORD>(std::size(manufacturer)))||
        wcscmp(manufacturer,L"QEMU")!=0||
        !readStringAt(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",L"ProductName",product,static_cast<DWORD>(std::size(product)))||
        !wcsstr(product,L"Windows Server")||
        !readStringAt(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",L"CurrentBuildNumber",build,static_cast<DWORD>(std::size(build)))||
        wcscmp(build,L"20348")!=0||
        !readStringAt(L"SYSTEM\\CurrentControlSet\\Control\\ProductOptions",L"ProductType",type,static_cast<DWORD>(std::size(type)))||
        (wcscmp(type,L"ServerNT")!=0&&wcscmp(type,L"LanmanNT")!=0))return E_ACCESSDENIED;
    MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);
    const DWORD processors=GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if(!GlobalMemoryStatusEx(&memory)||memory.ullTotalPhys>5ULL*1024*1024*1024||
        memory.ullTotalPhys==0||processors<1||processors>2)return E_ACCESSDENIED;
    return S_OK;
}
HRESULT verifyService()noexcept{
    const auto guest=verifyGuest();if(FAILED(guest)||!noThreadToken())return E_ACCESSDENIED;
    Handle token;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.value))return lastError();
    bool container=true;DWORD session=MAXDWORD;
    if(!tokenSid(token.value,WinNetworkServiceSid)||!appContainer(token.value,container)||container||
        !ProcessIdToSessionId(GetCurrentProcessId(),&session)||session!=0)return E_ACCESSDENIED;
    ScHandle scm;scm.value=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);
    if(!scm.value)return lastError();
    ScHandle service;service.value=OpenServiceW(scm.value,L"TermService",SERVICE_QUERY_STATUS);
    if(!service.value)return lastError();
    SERVICE_STATUS_PROCESS status{};DWORD bytes=0;
    if(!QueryServiceStatusEx(service.value,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&bytes))return lastError();
    return status.dwProcessId==GetCurrentProcessId()&&
        (status.dwCurrentState==SERVICE_RUNNING||status.dwCurrentState==SERVICE_START_PENDING)?S_OK:E_ACCESSDENIED;
}
bool validateRegistration(const Config& config)noexcept{
    wchar_t expectedRun[33]{};
    for(size_t i=0;i<config.runId.size();++i)expectedRun[i]=static_cast<wchar_t>(config.runId[i]);
    wchar_t run[64]{},value[260]{};
    Key key;if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,classKey,0,KEY_READ|KEY_WOW64_64KEY,&key.value)!=ERROR_SUCCESS)return false;
    if(!shape(key.value,1,1)||!readString(key.value,L"RETSARRunId",run,static_cast<DWORD>(std::size(run)))||wcscmp(run,expectedRun)!=0)return false;
    Key server;if(RegOpenKeyExW(key.value,L"InprocServer32",0,KEY_READ|KEY_WOW64_64KEY,&server.value)!=ERROR_SUCCESS)return false;
    if(!shape(server.value,0,2)||!readString(server.value,nullptr,value,static_cast<DWORD>(std::size(value)))||wcscmp(value,dllPath)!=0||
        !readString(server.value,L"ThreadingModel",value,static_cast<DWORD>(std::size(value)))||wcscmp(value,L"Free")!=0)return false;
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&ReadEnabledGuestConfig),&module))return false;
    const DWORD length=GetModuleFileNameW(module,value,static_cast<DWORD>(std::size(value)));
    return length>0&&length<std::size(value)&&_wcsicmp(value,dllPath)==0;
}
// Normalize every field individually into zeroed storage. Never send native
// structure padding or unused queue entries from the handler back to a client.
Response normalized(const Response& source)noexcept{
    Response target;std::memset(&target,0,sizeof(target));
    target.magic=ProtocolMagic;target.version=ProtocolVersion;target.bytes=sizeof(target);
    if(source.eventCount>EventCapacity||source.state>State::Stopping){target.result=E_UNEXPECTED;return target;}
    target.state=source.state;target.result=source.result;target.eventCount=source.eventCount;target.droppedEvents=source.droppedEvents;
    auto& a=target.outcome;const auto& b=source.outcome;
    a.operationId=b.operationId;a.onConnectedAccepted=b.onConnectedAccepted;a.readyAccepted=b.readyAccepted;
    a.connectionId=b.connectionId;a.onConnectedResult=b.onConnectedResult;a.connectionIdResult=b.connectionIdResult;
    a.onReadyResult=b.onReadyResult;a.logonErrorRedirectorObserved=b.logonErrorRedirectorObserved;
    a.cancellationRequested=b.cancellationRequested;a.cancellationResult=b.cancellationResult;
    a.sessionOwnershipProven=FALSE;
    for(ULONG i=0;i<source.eventCount;++i){
        auto& dst=target.events[i];const auto& src=source.events[i];
        dst.sequence=src.sequence;dst.operationId=src.operationId;dst.kind=src.kind;dst.result=src.result;
        dst.connectionId=src.connectionId;dst.hasSession=src.hasSession;
        if(src.hasSession){dst.session.SessionId=src.session.SessionId;dst.session.SessionUniqueGuid=src.session.SessionUniqueGuid;}
    }
    return target;
}
struct Shared {
    Config config{};
    std::shared_ptr<Handler> handler;
    Handle stop,pipe,ioEvent;
    OVERLAPPED overlapped{};
    Request request{};
    Response response{};
    BYTE disconnectByte=0;
    std::atomic<bool> stopped{false};
    std::atomic<HRESULT> result{E_PENDING};
    ULONGLONG expires=0;
};
class PipeServer final:public Server {
    std::shared_ptr<Shared> state_;
public:
    explicit PipeServer(std::shared_ptr<Shared> state):state_(std::move(state)){}
    ~PipeServer()override{RequestStop();}
    void RequestStop()noexcept override{SetEvent(state_->stop.value);}
    bool Stopped()const noexcept override{return state_->stopped.load();}
    HRESULT Result()const noexcept override{return state_->result.load();}
};
struct ThreadContext {std::shared_ptr<Shared> state;HMODULE module=nullptr;};
// Unexpected cancellation or reversion failure must not kill shared svchost,
// destroy a COM-owning handler under impersonation, or free a pending buffer.
// Retain the single bounded context and module pin until process termination.
// Stopped means no worker thread remains; Result reports the unresolved fault.
[[noreturn]] void quarantine(ThreadContext* context,HRESULT error)noexcept{
    context->state->result.store(error);context->state->stopped.store(true);
    SetEvent(context->state->stop.value);
    ExitThread(static_cast<DWORD>(error));
}
bool stopping(Shared& state)noexcept{
    return WaitForSingleObject(state.stop.value,0)!=WAIT_TIMEOUT||GetTickCount64()>=state.expires;
}
DWORD remaining(Shared& state)noexcept{
    const auto now=GetTickCount64();if(now>=state.expires)return 0;
    return static_cast<DWORD>(std::min<ULONGLONG>(IoMilliseconds,state.expires-now));
}
void beginIo(Shared& state)noexcept{
    state.overlapped={};state.overlapped.hEvent=state.ioEvent.value;ResetEvent(state.ioEvent.value);
}
DWORD finishIo(ThreadContext* context,BOOL immediate,DWORD error,DWORD& transferred)noexcept{
    auto& state=*context->state;
    if(immediate){if(GetOverlappedResult(state.pipe.value,&state.overlapped,&transferred,FALSE))return ERROR_SUCCESS;return GetLastError();}
    if(error!=ERROR_IO_PENDING)return error;
    HANDLE waits[]{state.stop.value,state.ioEvent.value};
    const DWORD wait=WaitForMultipleObjects(2,waits,FALSE,remaining(state));
    if(wait==WAIT_OBJECT_0+1){
        if(GetOverlappedResult(state.pipe.value,&state.overlapped,&transferred,FALSE))return ERROR_SUCCESS;
        return GetLastError();
    }
    const DWORD failure=wait==WAIT_TIMEOUT?ERROR_TIMEOUT:ERROR_OPERATION_ABORTED;
    (void)CancelIoEx(state.pipe.value,&state.overlapped);
    // The OVERLAPPED and its buffers are shared heap storage, never stack
    // storage. Even a broken cancellation cannot create a use-after-free.
    if(WaitForSingleObject(state.ioEvent.value,1000)!=WAIT_OBJECT_0)
        quarantine(context,HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    DWORD ignored=0;(void)GetOverlappedResult(state.pipe.value,&state.overlapped,&ignored,FALSE);
    return failure;
}
HRESULT authenticate(ThreadContext* context)noexcept{
    auto& state=*context->state;
    if(!ImpersonateNamedPipeClient(state.pipe.value))return lastError();
    HRESULT result=E_ACCESSDENIED;
    {
        Handle token;
        if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY|TOKEN_DUPLICATE,TRUE,&token.value)){
            SECURITY_IMPERSONATION_LEVEL level=SecurityAnonymous;DWORD bytes=0;
            if(GetTokenInformation(token.value,TokenImpersonationLevel,&level,sizeof(level),&bytes)&&
                level>=SecurityImpersonation&&elevatedAdministrator(token.value))result=S_OK;
        }
    }
    if(!RevertToSelf()){
        // Do not dispatch even if clearing the token through the equivalent
        // primitive succeeds: the request's authentication path failed.
        const auto failed=lastError();
        if(!SetThreadToken(nullptr,nullptr)||!noThreadToken())quarantine(context,failed);
        return failed;
    }
    if(!noThreadToken())quarantine(context,E_ACCESSDENIED);
    return result;
}
void run(ThreadContext* context)noexcept{
    auto& state=*context->state;
    while(!stopping(state)){
        beginIo(state);
        const BOOL connected=ConnectNamedPipe(state.pipe.value,&state.overlapped);
        const DWORD connectError=connected?ERROR_SUCCESS:GetLastError();DWORD count=0;
        DWORD status=connectError==ERROR_PIPE_CONNECTED?ERROR_SUCCESS:finishIo(context,connected,connectError,count);
        if(status!=ERROR_SUCCESS){
            (void)DisconnectNamedPipe(state.pipe.value);
            if(status==ERROR_TIMEOUT||status==ERROR_OPERATION_ABORTED)continue;
            state.result.store(HRESULT_FROM_WIN32(status));return;
        }
        state.request={};beginIo(state);
        const BOOL read=ReadFile(state.pipe.value,&state.request,sizeof(state.request),nullptr,&state.overlapped);
        status=finishIo(context,read,read?ERROR_SUCCESS:GetLastError(),count);
        if(status==ERROR_SUCCESS&&count==sizeof(Request)&&!stopping(state)){
            const auto authenticated=authenticate(context);
            Config live{};
            if(SUCCEEDED(authenticated)&&ValidRequest(state.request,state.config)&&!stopping(state)&&
                ReadEnabledGuestConfig(live)==S_OK&&live.runId==state.config.runId){
                // No token, COM apartment, RDS interface or callback is retained
                // by this transport thread. The handler must only queue/query.
                state.response=normalized(state.handler->Handle(state.request));
                beginIo(state);
                const BOOL written=WriteFile(state.pipe.value,&state.response,sizeof(state.response),nullptr,&state.overlapped);
                status=finishIo(context,written,written?ERROR_SUCCESS:GetLastError(),count);
                if(status==ERROR_SUCCESS&&count==sizeof(Response)&&!stopping(state)){
                    // Preserve the reply until the client reads and closes, or
                    // the bounded I/O deadline expires. Never FlushFileBuffers:
                    // it may wait indefinitely for a client to read the pipe.
                    beginIo(state);
                    const BOOL end=ReadFile(state.pipe.value,&state.disconnectByte,1,nullptr,&state.overlapped);
                    (void)finishIo(context,end,end?ERROR_SUCCESS:GetLastError(),count);
                }
            }
        }
        (void)DisconnectNamedPipe(state.pipe.value);
    }
    state.result.store(S_OK);
}
DWORD WINAPI threadMain(void* parameter)noexcept{
    auto* context=static_cast<ThreadContext*>(parameter);
    const HMODULE module=context->module;
    run(context);
    CloseHandle(context->state->pipe.value);context->state->pipe.value=INVALID_HANDLE_VALUE;
    context->state->handler.reset();
    context->state->stopped.store(true);
    delete context;
    // All C++ objects belonging to this thread have already been destroyed.
    FreeLibraryAndExitThread(module,0);
}
}

bool ValidRunId(const RunId& id)noexcept{
    bool nonzero=false;
    for(char ch:id){if(!((ch>='0'&&ch<='9')||(ch>='a'&&ch<='f')))return false;if(ch!='0')nonzero=true;}
    return nonzero;
}
bool ValidRequest(const Request& request,const Config& config)noexcept{
    return ValidRunId(config.runId)&&request.magic==ProtocolMagic&&request.version==ProtocolVersion&&
        request.bytes==sizeof(Request)&&request.reserved==0&&request.runId==config.runId&&
        request.command>=Command::Start&&request.command<=Command::Snapshot&&
        (request.command==Command::Snapshot||request.operationId!=0);
}
bool PipeName(const Config& config,wchar_t (&name)[96])noexcept{
    std::fill(std::begin(name),std::end(name),L'\0');
    if(!ValidRunId(config.runId))return false;
    constexpr wchar_t prefix[]=L"\\\\.\\pipe\\RETSAR-RdsLab-";
    std::copy(std::begin(prefix),std::end(prefix)-1,name);
    for(size_t i=0;i<config.runId.size();++i)name[std::size(prefix)-1+i]=static_cast<wchar_t>(config.runId[i]);
    return true;
}
HRESULT VerifyControllerGuest()noexcept{
    const auto guest=verifyGuest();if(FAILED(guest)||!noThreadToken())return E_ACCESSDENIED;
    Handle token;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_DUPLICATE,&token.value))return lastError();
    return elevatedAdministrator(token.value)?S_OK:E_ACCESSDENIED;
}
HRESULT ReadEnabledGuestConfig(Config& config)noexcept{
    config={};const auto service=verifyService();if(FAILED(service))return service;
    Key key;const auto opened=RegOpenKeyExW(HKEY_LOCAL_MACHINE,listenerKey,0,KEY_READ|KEY_WOW64_64KEY,&key.value);
    if(opened==ERROR_FILE_NOT_FOUND)return S_FALSE;
    if(opened!=ERROR_SUCCESS)return HRESULT_FROM_WIN32(opened);
    DWORD value=0,type=0,bytes=sizeof(value);
    const auto enabled=RegQueryValueExW(key.value,EnableValue,nullptr,&type,reinterpret_cast<BYTE*>(&value),&bytes);
    if(enabled==ERROR_FILE_NOT_FOUND)return S_FALSE;
    if(enabled!=ERROR_SUCCESS||type!=REG_DWORD||bytes!=sizeof(value)||value!=1||!shape(key.value,0,4))return E_ACCESSDENIED;
    wchar_t run[64]{},protocol[64]{};
    if(!exactDword(key.value,L"fEnableWinStation",1)||
        !readString(key.value,L"LoadableProtocol_Object",protocol,static_cast<DWORD>(std::size(protocol)))||wcscmp(protocol,classId)!=0||
        !readString(key.value,L"RETSARRunId",run,static_cast<DWORD>(std::size(run)))||wcslen(run)!=32)return E_ACCESSDENIED;
    Config found{};
    for(size_t i=0;i<found.runId.size();++i){if(run[i]>127)return E_ACCESSDENIED;found.runId[i]=static_cast<char>(run[i]);}
    if(!ValidRunId(found.runId)||!validateRegistration(found))return E_ACCESSDENIED;
    config=found;return S_OK;
}
HRESULT StartServer(const Config& config,std::shared_ptr<Handler> handler,std::shared_ptr<Server>& server)noexcept{
    server.reset();
    if(!handler||!ValidRunId(config.runId))return E_INVALIDARG;
    Config live{};const auto allowed=ReadEnabledGuestConfig(live);
    if(allowed!=S_OK)return FAILED(allowed)?allowed:E_ACCESSDENIED;
    if(live.runId!=config.runId)return E_ACCESSDENIED;
    try{
        auto state=std::make_shared<Shared>();state->config=config;state->handler=std::move(handler);
        state->stop.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        state->ioEvent.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!state->stop.value||!state->ioEvent.value)return lastError();
        wchar_t name[96]{};if(!PipeName(config,name))return E_INVALIDARG;
        PSECURITY_DESCRIPTOR descriptor=nullptr;
        // Individual access bits exclude FILE_CREATE_PIPE_INSTANCE. No generic
        // write, Everyone, Authenticated Users or network-client allowance.
        // https://learn.microsoft.com/windows/win32/ipc/named-pipe-security-and-access-rights
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;0x00120183;;;SY)(A;;0x00120183;;;BA)",SDDL_REVISION_1,&descriptor,nullptr))return lastError();
        SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};
        state->pipe.value=CreateNamedPipeW(name,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,
            sizeof(Response),sizeof(Request),IoMilliseconds,&attributes);
        const auto pipeError=GetLastError();LocalFree(descriptor);
        if(state->pipe.value==INVALID_HANDLE_VALUE)return HRESULT_FROM_WIN32(pipeError);
        auto result=std::make_shared<PipeServer>(state);
        auto context=std::make_unique<ThreadContext>();context->state=state;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&threadMain),&context->module))return lastError();
        state->expires=GetTickCount64()+LeaseMilliseconds;
        Handle worker(CreateThread(nullptr,0,threadMain,context.get(),0,nullptr));
        if(!worker.value){const auto failure=lastError();FreeLibrary(context->module);return failure;}
        (void)context.release();server=std::move(result);return S_OK;
    }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(...){return E_FAIL;}
}
}
