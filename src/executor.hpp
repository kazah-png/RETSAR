#pragma once
#include "model.hpp"
#include <optional>

namespace seats::runtime {
enum class Domain { Windows, Simulated };
enum class ResourceKind { Session, OutputBinding, InputBinding };
enum class Ownership { Owned, Absent, Foreign, Unknown };
enum class AcquireState { Created, NoEffect, Uncertain };
enum class ReleaseState { Released, Absent, Refused, Uncertain };
enum class QueryState { Owned, NoEffectFinal, Unknown };
struct Acquisition {
    ResourceKind kind=ResourceKind::Session;
    std::string requestedId,runId,ownerNonce,operationId,providerEpoch;
    uint64_t inventoryGeneration=0;
    std::string parentSessionId,parentSessionIncarnation,parentSessionOperationId;
};
struct OwnershipReceipt {
    Acquisition request;
    std::string resourceId,incarnation;
};
inline bool same(const Acquisition& a,const Acquisition& b){
    return a.kind==b.kind&&a.requestedId==b.requestedId&&a.runId==b.runId&&a.ownerNonce==b.ownerNonce&&
        a.operationId==b.operationId&&a.providerEpoch==b.providerEpoch&&a.inventoryGeneration==b.inventoryGeneration&&
        a.parentSessionId==b.parentSessionId&&a.parentSessionIncarnation==b.parentSessionIncarnation&&a.parentSessionOperationId==b.parentSessionOperationId;
}
inline bool same(const OwnershipReceipt& a,const OwnershipReceipt& b){return same(a.request,b.request)&&a.resourceId==b.resourceId&&a.incarnation==b.incarnation;}
struct Availability {bool ready=false;uint64_t generation=0;std::string detail;};
struct AcquireResult {AcquireState state=AcquireState::Uncertain;std::optional<OwnershipReceipt> receipt;std::string detail;};
struct ReleaseResult {ReleaseState state=ReleaseState::Uncertain;std::string detail;};
struct QueryResult {QueryState state=QueryState::Unknown;std::optional<OwnershipReceipt> receipt;std::string detail;};
class SeatExecutor {
public:
    virtual ~SeatExecutor()=default;
    virtual Domain domain()const noexcept=0;
    virtual Capabilities capabilities()const noexcept=0;
    virtual std::string providerEpoch()const=0;
    // ready requires authoritative knowledge that every requested resource is
    // free; unknown/foreign ownership must never be interpreted as free.
    virtual Availability inspect(const SeatConfig&)const=0;
    // generation versions EXTERNAL ownership/interference, not this run's own
    // acquisitions. The provider must retain a transaction-scoped reservation
    // view; external changes revoke it. Each step still rechecks exact ownership.
    // Acquisition atomically revalidates that view and creates only a new
    // resource/binding. Bindings require the exact owned parent session tuple;
    // never use the current/default session. operationId is idempotent only for
    // the exact same Acquisition; reuse with different fields must be rejected.
    // AcquireState::NoEffect is FINAL: no operation remains in flight.
    virtual AcquireResult acquire(const Acquisition&)=0;
    virtual Ownership ownership(const OwnershipReceipt&)const=0;
    // Never release by numeric ID alone. All receipt fields must match.
    virtual ReleaseResult release(const OwnershipReceipt&,const std::string& releaseOperationId)=0;
    // NoEffectFinal guarantees durable completion/cancellation with no possible
    // later effect. "Not seen yet" or any in-flight operation MUST be Unknown.
    virtual QueryResult query(const Acquisition&)const=0;
};
class WindowsBlockedExecutor final:public SeatExecutor {
public:
    Domain domain()const noexcept override{return Domain::Windows;}
    Capabilities capabilities()const noexcept override{return {};}
    std::string providerEpoch()const override{return "windows-p0-not-implemented";}
    Availability inspect(const SeatConfig&)const override{return {false,0,"P0_NOT_IMPLEMENTED"};}
    AcquireResult acquire(const Acquisition&)override{return {AcquireState::NoEffect,{},"P0_NOT_IMPLEMENTED"};}
    Ownership ownership(const OwnershipReceipt&)const override{return Ownership::Unknown;}
    ReleaseResult release(const OwnershipReceipt&,const std::string&)override{return {ReleaseState::Refused,"P0_NOT_IMPLEMENTED"};}
    QueryResult query(const Acquisition&)const override{return {QueryState::Unknown,{},"P0_NOT_IMPLEMENTED"};}
};
}
