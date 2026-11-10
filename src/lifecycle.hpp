#pragma once
#include "executor.hpp"
#include "journal.hpp"
#include <memory>

namespace seats::runtime {
class Clock {public:virtual ~Clock()=default;virtual uint64_t nowMs()const=0;};
class SystemClock final:public Clock {public:uint64_t nowMs()const override;};
class NonceSource {public:virtual ~NonceSource()=default;virtual std::string next()=0;};
class SystemNonceSource final:public NonceSource {public:std::string next()override;};
enum class RunState {Prepared,Starting,Running,Stopping,Stopped,RolledBack,RecoveryRequired,Expired};
enum class ResultCode {Prepared,Running,Stopped,Rejected,Unsupported,FailedRolledBack,RecoveryRequired};
const char* name(RunState)noexcept;
const char* name(ResultCode)noexcept;
const char* name(Domain)noexcept;
struct PlanToken {
    std::string runId,nonce,engineEpoch,configDigest;
    uint64_t generation=0,inventoryGeneration=0,expiresAt=0;
};
struct Result {
    ResultCode code=ResultCode::Rejected;
    Domain domain=Domain::Windows;
    std::string detail,runId;
    uint64_t generation=0;
    std::optional<PlanToken> token;
};
struct RunStatus {
    std::string runId,seatId;
    RunState state=RunState::Prepared;
    RunState lastRecordedState=RunState::Prepared;
    uint64_t generation=0;
    size_t ownedReceipts=0;
    bool pendingOperation=false;
    std::vector<std::string> reservations;
};
struct EngineStatus {
    Domain domain=Domain::Windows;
    bool healthy=false,operationalProvider=false;
    uint64_t generation=0;
    std::string detail;
    std::vector<RunStatus> runs;
};
struct RecoveryEntry {std::string runId;RunState state=RunState::RecoveryRequired;std::string detail;};
class SeatLifecycleEngine {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    SeatLifecycleEngine(SeatExecutor&,Journal&,Clock&,NonceSource&);
    ~SeatLifecycleEngine();
    SeatLifecycleEngine(const SeatLifecycleEngine&)=delete;
    SeatLifecycleEngine& operator=(const SeatLifecycleEngine&)=delete;
    Result Prepare(const SeatConfig&,uint64_t ttlMs=30000);
    Result Commit(const PlanToken&);
    Result Stop(const std::string& runId,uint64_t expectedGeneration);
    std::vector<RecoveryEntry> Recover()const; // read-only; never retries or cleans up
    EngineStatus Status()const;
};
}
