#pragma once
#include <windows.h>
#include <wtsprotocol.h>
#include <cstdint>

namespace seats::rds {
// RETSAR identifiers, not the identifiers of Microsoft's sample classes.
inline constexpr CLSID NativeRdsBridgeClass={0xe09044af,0xa721,0x4d68,{0x9f,0x49,0x45,0x27,0xe9,0xe5,0x13,0x76}};
inline constexpr ULONG EventCapacity=64;

enum class EventKind : ULONG {
    Listening=1,ListenerStopped,ExperimentStarted,OnConnectedReturned,ConnectionIdObserved,
    OnReadyReturned,SessionAssigned,LogonObserved,ConnectObserved,PreDisconnectObserved,
    DisconnectObserved,ConnectionClosed,CommandProcessObserved,ErrorObserved,CancellationRequested,
    BrokenConnectionReturned,LogonErrorRedirectorObserved
};
struct BridgeEvent {
    ULONGLONG sequence=0,operationId=0;
    EventKind kind=EventKind::Listening;
    HRESULT result=S_OK;
    ULONG connectionId=0;
    BOOL hasSession=FALSE;
    WRDS_SESSION_ID session{};
};
struct ExperimentOutcome {
    ULONGLONG operationId=0;
    BOOL onConnectedAccepted=FALSE,readyAccepted=FALSE;
    ULONG connectionId=0;
    HRESULT onConnectedResult=E_PENDING,connectionIdResult=E_PENDING,onReadyResult=E_PENDING;
    BOOL logonErrorRedirectorObserved=FALSE,cancellationRequested=FALSE;
    HRESULT cancellationResult=E_PENDING;
    // These events cannot establish creation, ownership, display or input isolation.
    BOOL sessionOwnershipProven=FALSE;
};

// In-process laboratory interface only. It is not a broker or IPC authorization
// boundary. StartListen and StartExperiment require the process MTA; outbound
// RDS calls must occur outside an RDS notification. A listener accepts one attempt.
// E_PENDING requires a later explicit continuation/cancellation from the MTA;
// notifications never run a continuation or wait for one. Cancellation is a
// connection request only and does not establish destruction of a session.
MIDL_INTERFACE("DB0368DF-9B48-4B88-96FB-84F02AE5137B")
IRetsarRdsLabControl : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE StartExperiment(BOOL callerStartedExperiment,ULONGLONG operationId,
        ExperimentOutcome* outcome)=0;
    virtual HRESULT STDMETHODCALLTYPE ContinueExperiment(ExperimentOutcome* outcome)=0;
    virtual HRESULT STDMETHODCALLTYPE CancelExperiment()=0;
    virtual HRESULT STDMETHODCALLTYPE DrainEvents(BridgeEvent* events,ULONG capacity,ULONG* written,
        ULONGLONG* dropped)=0;
};
}
