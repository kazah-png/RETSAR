#pragma once
#include "native_rds_bridge.hpp"
#include <array>
#include <memory>
#include <type_traits>

namespace seats::rds::lab {
inline constexpr ULONG ProtocolMagic=0x524C4352; // RCLR, little endian.
inline constexpr ULONG ProtocolVersion=1;
inline constexpr DWORD ClientAccess=FILE_READ_DATA|FILE_WRITE_DATA|FILE_READ_ATTRIBUTES|
    FILE_WRITE_ATTRIBUTES|READ_CONTROL|SYNCHRONIZE; // Never FILE_CREATE_PIPE_INSTANCE.
inline constexpr wchar_t ListenerName[]=L"RETSAR-Lab";
inline constexpr wchar_t EnableValue[]=L"RETSARControlEnabled";
using RunId=std::array<char,32>;
struct Config { RunId runId{}; };
enum class Command:ULONG { Start=1,Continue=2,Cancel=3,Snapshot=4 };
enum class State:ULONG { Idle=0,Queued=1,Working=2,Pending=3,Finished=4,Stopping=5 };
// Fixed little-endian Windows x64 laboratory protocol. No pointers, names,
// credentials, serialized COM interfaces, handles or arbitrary payloads.
struct Request {
    ULONG magic=ProtocolMagic,version=ProtocolVersion,bytes=sizeof(Request);
    Command command=Command::Snapshot;
    ULONGLONG operationId=0;
    RunId runId{};
    ULONGLONG reserved=0;
};
struct Response {
    ULONG magic=ProtocolMagic,version=ProtocolVersion,bytes=sizeof(Response);
    State state=State::Idle;
    HRESULT result=E_PENDING;
    ULONG eventCount=0;
    ULONGLONG droppedEvents=0;
    ExperimentOutcome outcome{};
    std::array<BridgeEvent,EventCapacity> events{};
};
static_assert(sizeof(Request)==64);
static_assert(sizeof(Response)==3664);
static_assert(std::is_trivially_copyable_v<Request> && std::is_trivially_copyable_v<Response>);

// These helpers never create a pipe or alter registry/service state.
bool ValidRunId(const RunId& id) noexcept;
bool ValidRequest(const Request& request,const Config& config) noexcept;
bool PipeName(const Config& config,wchar_t (&name)[96]) noexcept;
HRESULT VerifyControllerGuest() noexcept;
// S_FALSE means absent opt-in. Any malformed/non-owned key or failed guest
// identity check fails closed. Requires NetworkService in Session0.
HRESULT ReadEnabledGuestConfig(Config& config) noexcept;

class Handler {
public:
    virtual ~Handler()=default;
    // Called only after client authentication AND successful impersonation
    // reversion. Must be bounded: enqueue/read state only, never call RDS/COM,
    // wait for a worker, or release the last RDS interface on this thread.
    virtual Response Handle(const Request& request) noexcept=0;
};
class Server {
public:
    virtual ~Server()=default;
    // Nonblocking. Never join this server from an RDS notification.
    virtual void RequestStop() noexcept=0;
    virtual bool Stopped() const noexcept=0;
    virtual HRESULT Result() const noexcept=0;
};
// Revalidates the live owned opt-in and guest/service identity. One persistent
// pipe instance, one bounded request per connection. Its dedicated I/O thread
// retains Handler and pins this module until it exits. Stop is asynchronous;
// callers must break Listener->Server->Handler->Listener ownership cycles.
HRESULT StartServer(const Config& config,std::shared_ptr<Handler> handler,
    std::shared_ptr<Server>& server) noexcept;
}
