#include "lab_control_channel.hpp"
#include <windows.h>
#include <winsvc.h>
#include <objbase.h>
#include <iostream>
#include <string>

namespace {
using namespace seats::rds::lab;
struct File {HANDLE h=INVALID_HANDLE_VALUE;~File(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
DWORD servicePid(){
    const auto manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);if(!manager)return 0;
    const auto service=OpenServiceW(manager,L"TermService",SERVICE_QUERY_STATUS);CloseServiceHandle(manager);
    if(!service)return 0;SERVICE_STATUS_PROCESS status{};DWORD bytes=0;
    const auto ok=QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&bytes);
    CloseServiceHandle(service);return ok&&status.dwCurrentState==SERVICE_RUNNING?status.dwProcessId:0;
}
HRESULT transfer(HANDLE pipe,bool write,void* buffer,DWORD size){
    File event;event.h=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!event.h)return HRESULT_FROM_WIN32(GetLastError());
    OVERLAPPED io{};io.hEvent=event.h;DWORD bytes=0;
    const auto immediate=write?WriteFile(pipe,buffer,size,&bytes,&io):ReadFile(pipe,buffer,size,&bytes,&io);
    if(!immediate){
        const auto error=GetLastError();if(error!=ERROR_IO_PENDING)return HRESULT_FROM_WIN32(error);
        const auto wait=WaitForSingleObject(event.h,5000);
        if(wait!=WAIT_OBJECT_0){
            CancelIoEx(pipe,&io);
            if(WaitForSingleObject(event.h,1000)!=WAIT_OBJECT_0){
                // Only this disposable controller exits. Never free OVERLAPPED
                // storage while the kernel could still refer to it.
                std::cerr<<"Controller I/O cancellation deadline exceeded.\n";
                ExitProcess(ERROR_TIMEOUT);
            }
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
        if(!GetOverlappedResult(pipe,&io,&bytes,FALSE))return HRESULT_FROM_WIN32(GetLastError());
    }
    return bytes==size?S_OK:HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}
void print(const Response& r){
    std::cout<<"{\"schema\":\"retsar.lab-control.v1\",\"state\":"<<static_cast<ULONG>(r.state)
        <<",\"result\":"<<static_cast<ULONG>(r.result)<<",\"operationId\":"<<r.outcome.operationId
        <<",\"onConnectedResult\":"<<static_cast<ULONG>(r.outcome.onConnectedResult)
        <<",\"onConnectedAccepted\":"<<(r.outcome.onConnectedAccepted?"true":"false")
        <<",\"readyAccepted\":"<<(r.outcome.readyAccepted?"true":"false")
        <<",\"connectionIdResult\":"<<static_cast<ULONG>(r.outcome.connectionIdResult)
        <<",\"connectionId\":"<<r.outcome.connectionId
        <<",\"onReadyResult\":"<<static_cast<ULONG>(r.outcome.onReadyResult)
        <<",\"redirectorObserved\":"<<(r.outcome.logonErrorRedirectorObserved?"true":"false")
        <<",\"cancelRequested\":"<<(r.outcome.cancellationRequested?"true":"false")
        <<",\"cancelResult\":"<<static_cast<ULONG>(r.outcome.cancellationResult)
        <<",\"sessionOwnershipProven\":false,\"droppedEvents\":"<<r.droppedEvents<<",\"events\":[";
    for(ULONG i=0;i<r.eventCount;++i){
        if(i)std::cout<<',';const auto& e=r.events[i];
        std::cout<<"{\"sequence\":"<<e.sequence<<",\"kind\":"<<static_cast<ULONG>(e.kind)
            <<",\"result\":"<<static_cast<ULONG>(e.result)<<",\"connectionId\":"<<e.connectionId
            <<",\"hasSession\":"<<(e.hasSession?"true":"false");
        if(e.hasSession){
            wchar_t guid[40]{};char ascii[40]{};
            if(StringFromGUID2(e.session.SessionUniqueGuid,guid,40)>0){for(size_t j=0;j<39;++j)ascii[j]=static_cast<char>(guid[j]);}
            std::cout<<",\"sessionId\":"<<e.session.SessionId<<",\"sessionGuid\":\""<<ascii<<'"';
        }
        std::cout<<'}';
    }
    std::cout<<"]}\n";
}
}
int wmain(int argc,wchar_t** argv){
    if(argc==2&&std::wstring(argv[1])==L"--help"){
        std::cout<<"RETSAR owned Server guest control only.\n"
            <<"Usage: RETSAR-rds-control.exe --start|--continue|--cancel|--snapshot <32-lowercase-hex-run-id>\n"
            <<"One fixed operation (1). No credentials or host registration. Snapshot never starts a connection.\n";return 0;
    }
    if(argc!=3){std::cerr<<"Use --help.\n";return 2;}
    Request request{};request.operationId=1;
    const std::wstring verb=argv[1],id=argv[2];
    if(verb==L"--start")request.command=Command::Start;
    else if(verb==L"--continue")request.command=Command::Continue;
    else if(verb==L"--cancel")request.command=Command::Cancel;
    else if(verb==L"--snapshot")request.command=Command::Snapshot;
    else return 2;
    if(id.size()!=request.runId.size())return 2;
    for(size_t i=0;i<id.size();++i){if(id[i]>127)return 2;request.runId[i]=static_cast<char>(id[i]);}
    Config config{request.runId};if(!ValidRequest(request,config))return 2;
    auto result=VerifyControllerGuest();
    if(FAILED(result)){std::cerr<<"Owned guest/operator check rejected: "<<static_cast<ULONG>(result)<<'\n';return 3;}
    wchar_t name[96]{};if(!PipeName(config,name))return 2;
    File pipe;pipe.h=CreateFileW(name,ClientAccess,0,nullptr,OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IMPERSONATION,nullptr);
    if(pipe.h==INVALID_HANDLE_VALUE){std::cerr<<"Pipe open failed: "<<GetLastError()<<'\n';return 4;}
    ULONG server=0;const auto expected=servicePid();
    if(!GetNamedPipeServerProcessId(pipe.h,&server)||!expected||server!=expected){std::cerr<<"TermService identity mismatch.\n";return 5;}
    DWORD mode=PIPE_READMODE_MESSAGE;
    if(!SetNamedPipeHandleState(pipe.h,&mode,nullptr,nullptr))return 6;
    result=transfer(pipe.h,true,&request,sizeof(request));Response response{};
    if(SUCCEEDED(result))result=transfer(pipe.h,false,&response,sizeof(response));
    if(FAILED(result)){std::cerr<<"Control transfer failed: "<<static_cast<ULONG>(result)<<'\n';return 7;}
    if(response.magic!=ProtocolMagic||response.version!=ProtocolVersion||response.bytes!=sizeof(response)||
        response.eventCount>seats::rds::EventCapacity||response.state>State::Stopping||response.outcome.sessionOwnershipProven){
        std::cerr<<"Invalid response.\n";return 8;
    }
    print(response);return 0;
}