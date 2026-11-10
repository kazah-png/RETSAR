#pragma once
// Explicit test provider. Never included by or selectable in the product CLI.
#include "../src/executor.hpp"
#include <functional>
#include <map>
#include <set>

namespace seats::runtime::test {
class SimulatedSeatExecutor final:public SeatExecutor {
public:
    bool enabled=true,resourcesFree=true,forgeReceipt=false;
    uint64_t revision=7;
    std::string epoch="SIMULATED-provider-v1";
    int acquireCalls=0,releaseCalls=0,failAcquireAt=0,uncertainAcquireAt=0,failReleaseAt=0;
    bool uncertainCreates=true;
    std::map<std::string,OwnershipReceipt> live;
    std::map<std::string,OwnershipReceipt> operations;
    std::set<std::string> finalNoEffects;
    std::vector<ResourceKind> releaseOrder;
    std::function<void(const Acquisition&)> beforeAcquire;
    std::function<void(const OwnershipReceipt&)> beforeRelease;
    Domain domain()const noexcept override{return Domain::Simulated;}
    Capabilities capabilities()const noexcept override{Capabilities c;if(enabled)c.assignOutputs=c.sessionLifecycle=c.independentInput=c.seatLifecycle=c.systemBackend=Capability::Available;return c;}
    std::string providerEpoch()const override{return epoch;}
    Availability inspect(const SeatConfig&)const override{return {resourcesFree,revision,resourcesFree?"SIMULATED free":"SIMULATED foreign/unknown"};}
    AcquireResult acquire(const Acquisition& request)override{
        ++acquireCalls;if(beforeAcquire)beforeAcquire(request);
        if(auto found=operations.find(request.operationId);found!=operations.end())return same(found->second.request,request)?AcquireResult{AcquireState::Created,found->second,"SIMULATED idempotent"}:AcquireResult{AcquireState::Uncertain,{},"operation id reused with different payload"};
        if(request.providerEpoch!=epoch||request.inventoryGeneration!=revision||acquireCalls==failAcquireAt){finalNoEffects.insert(request.operationId);return {AcquireState::NoEffect,{},"SIMULATED final no effect"};}
        if(request.kind!=ResourceKind::Session){
            auto parent=live.find(request.parentSessionId);
            if(parent==live.end()||parent->second.request.kind!=ResourceKind::Session||parent->second.incarnation!=request.parentSessionIncarnation||
                parent->second.request.operationId!=request.parentSessionOperationId||parent->second.request.runId!=request.runId||parent->second.request.ownerNonce!=request.ownerNonce||parent->second.request.providerEpoch!=request.providerEpoch){
                finalNoEffects.insert(request.operationId);return {AcquireState::NoEffect,{},"SIMULATED owned parent missing; no fallback"};
            }
        }else if(!request.parentSessionId.empty()||!request.parentSessionIncarnation.empty()||!request.parentSessionOperationId.empty()){
            finalNoEffects.insert(request.operationId);return {AcquireState::NoEffect,{},"SIMULATED invalid session parent"};
        }
        if(acquireCalls==uncertainAcquireAt&&!uncertainCreates)return {AcquireState::Uncertain,{},"SIMULATED in flight, NOT final no effect"};
        OwnershipReceipt owned{request,"SIMULATED-resource-"+std::to_string(acquireCalls),"SIMULATED-incarnation-"+std::to_string(acquireCalls)};
        live.emplace(owned.resourceId,owned);operations.emplace(request.operationId,owned);
        if(acquireCalls==uncertainAcquireAt)return {AcquireState::Uncertain,{},"SIMULATED effect occurred, reply lost"};
        if(forgeReceipt){auto forged=owned;forged.request.ownerNonce="foreign-owner";return {AcquireState::Created,forged,"SIMULATED forged response"};}
        return {AcquireState::Created,owned,"SIMULATED created"};
    }
    Ownership ownership(const OwnershipReceipt& receipt)const override{
        auto found=live.find(receipt.resourceId);if(found==live.end())return Ownership::Absent;
        return same(found->second,receipt)?Ownership::Owned:Ownership::Foreign;
    }
    ReleaseResult release(const OwnershipReceipt& receipt,const std::string&)override{
        ++releaseCalls;if(beforeRelease)beforeRelease(receipt);
        const auto current=ownership(receipt);if(current==Ownership::Foreign||current==Ownership::Unknown)return {ReleaseState::Refused,"SIMULATED foreign resource preserved"};
        if(current==Ownership::Absent)return {ReleaseState::Absent,"SIMULATED already absent"};
        if(releaseCalls==failReleaseAt)return {ReleaseState::Uncertain,"SIMULATED release uncertain"};
        releaseOrder.push_back(receipt.request.kind);live.erase(receipt.resourceId);return {ReleaseState::Released,"SIMULATED removed exact owned incarnation"};
    }
    QueryResult query(const Acquisition& request)const override{
        auto found=operations.find(request.operationId);
        if(found!=operations.end()&&same(found->second.request,request))return {QueryState::Owned,found->second,"SIMULATED operation receipt"};
        if(finalNoEffects.count(request.operationId))return {QueryState::NoEffectFinal,{},"SIMULATED terminated with no effect"};
        return {QueryState::Unknown,{},"SIMULATED pending/unknown; absence is not completion"};
    }
};
}
