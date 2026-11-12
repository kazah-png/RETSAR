#include "../src/rds/lab_control_channel.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>
using namespace seats::rds::lab;
namespace {
int failures=0,checks=0;
void check(bool condition,const char* description){++checks;if(!condition){++failures;std::cerr<<"FAIL "<<description<<'\n';}}
}
int main(){
    // Inert protocol tests: no registry/service writes, named pipes, token
    // impersonation, controller verification, or live server are exercised.
    Config config{};
    constexpr char id[]="0123456789abcdef0123456789abcdef";
    std::copy(std::begin(id),std::end(id)-1,config.runId.begin());
    check(sizeof(Request)==64&&sizeof(Response)==3664,"fixed Windows x64 packet sizes");
    check(ValidRunId(config.runId),"lowercase nonzero GUID-shaped run ID");
    auto bad=config;bad.runId.fill('0');check(!ValidRunId(bad.runId),"zero run rejected");
    bad=config;bad.runId[0]='A';check(!ValidRunId(bad.runId),"uppercase run rejected");
    bad=config;bad.runId[0]='g';check(!ValidRunId(bad.runId),"nonhex run rejected");
    bad=config;bad.runId[0]='\0';check(!ValidRunId(bad.runId),"embedded terminator rejected");
    bad=config;bad.runId[0]='\\';check(!ValidRunId(bad.runId),"path separator rejected");
    wchar_t name[96]{};check(PipeName(config,name),"valid local pipe name formed");
    check(std::wstring(name)==L"\\\\.\\pipe\\RETSAR-RdsLab-0123456789abcdef0123456789abcdef","pipe name stays in fixed local namespace");
    std::fill(std::begin(name),std::end(name),L'x');
    check(!PipeName(bad,name)&&std::all_of(std::begin(name),std::end(name),[](wchar_t c){return c==0;}),"invalid run clears name");
    Request request{};request.runId=config.runId;
    check(ValidRequest(request,config),"snapshot accepts zero operation for initial observation");
    request.command=Command::Start;check(!ValidRequest(request,config),"start requires operation");
    request.operationId=7;check(ValidRequest(request,config),"explicit start operation accepted");
    request.command=Command::Continue;check(ValidRequest(request,config),"explicit continue operation accepted");
    request.command=Command::Cancel;check(ValidRequest(request,config),"explicit cancel operation accepted");
    request.operationId=0;check(!ValidRequest(request,config),"cancel requires operation");
    request.command=Command::Continue;check(!ValidRequest(request,config),"continue requires operation");
    request.command=Command::Snapshot;
    auto original=request;request.magic^=1;check(!ValidRequest(request,config),"foreign magic rejected");
    request=original;request.version++;check(!ValidRequest(request,config),"unknown protocol version rejected");
    request=original;request.bytes--;check(!ValidRequest(request,config),"short declared packet rejected");
    request=original;request.bytes++;check(!ValidRequest(request,config),"oversized declared packet rejected");
    request=original;request.reserved=1;check(!ValidRequest(request,config),"reserved payload rejected");
    request=original;request.command=static_cast<Command>(0);check(!ValidRequest(request,config),"zero command rejected");
    request=original;request.command=static_cast<Command>(5);check(!ValidRequest(request,config),"unknown command rejected");
    request=original;request.runId[0]='f';check(!ValidRequest(request,config),"other run rejected");
    request=original;check(!ValidRequest(request,bad),"malformed active configuration rejected");
    check((ClientAccess&FILE_CREATE_PIPE_INSTANCE)==0,"client cannot create pipe instances");
    check((ClientAccess&(FILE_READ_DATA|FILE_WRITE_DATA|SYNCHRONIZE))==(FILE_READ_DATA|FILE_WRITE_DATA|SYNCHRONIZE),"client has bounded duplex data rights");
    check((ClientAccess&(WRITE_DAC|WRITE_OWNER|DELETE|GENERIC_ALL|GENERIC_WRITE))==0,"client rights exclude mutation and generic-write rights");
    check(ClientAccess==0x00120183,"client rights agree with explicit server DACL mask");
    Response response{};check(response.result==E_PENDING&&!response.outcome.sessionOwnershipProven&&response.eventCount==0,"default response cannot assert an operation or session success");
    std::cout<<"Laboratory control protocol: "<<checks<<" checks, "<<failures<<" failures. No live control exercised.\n";
    return failures?1:0;
}
