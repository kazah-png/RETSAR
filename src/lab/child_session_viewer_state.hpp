#pragma once

namespace seats::lab::child::viewer {
// Connection end and restoration of the Windows setting are independent facts.
// This model authorizes no operation and never equates a deadline or object
// release with a disconnected transport.
enum class EndEvidence { NotAttempted, Unconfirmed, DisconnectedEvent, ConnectedZero };
struct ConnectionEnd {
    bool connectAttempted=false,disconnectedEvent=false,connectedZero=false;
    bool deadlineExpired=false,disconnectFailed=false,closeFailed=false;
    unsigned long long eventRevision=0;
    void ActiveEvent()noexcept {++eventRevision;disconnectedEvent=false;connectedZero=false;}
    void EndEvent()noexcept {++eventRevision;disconnectedEvent=true;connectedZero=false;}
    void Read(bool known,int value,unsigned long long startedRevision)noexcept {
        // A callback delivered during the read is newer than its sampled value.
        if(!known||startedRevision!=eventRevision)return;
        connectedZero=value==0;
        if(value!=0)disconnectedEvent=false;
    }
    bool Verified()const noexcept{return connectAttempted&&(disconnectedEvent||connectedZero);}
    bool Satisfied()const noexcept{return !connectAttempted||Verified();}
    bool CallsFailed()const noexcept{return disconnectFailed||closeFailed;}
    EndEvidence Evidence()const noexcept {
        if(!connectAttempted)return EndEvidence::NotAttempted;
        if(disconnectedEvent)return EndEvidence::DisconnectedEvent;
        if(connectedZero)return EndEvidence::ConnectedZero;
        return EndEvidence::Unconfirmed;
    }
};
inline const char* EvidenceName(EndEvidence evidence)noexcept {
    switch(evidence){
    case EndEvidence::NotAttempted:return "not-attempted";
    case EndEvidence::DisconnectedEvent:return "disconnected-event";
    case EndEvidence::ConnectedZero:return "connected-zero";
    default:return "unconfirmed";
    }
}
// Presentation only: these supplied observations authorize no session operation.
enum class Phase { Preparing, Connecting, WaitingForWindows, SigningIn, Verifying,
                   Connected, Disconnecting, Disconnected, NotConnected, NeedsAttention };
struct State {
    bool connectionStarted=false,authenticationPrompt=false,transportConnected=false;
    bool loginComplete=false,desktopObserved=false,closing=false,finished=false;
    bool baselineRestored=false,operationFailed=false,residualChild=false;
    ConnectionEnd end{};bool preserveEnabled=false;
};
struct Presentation {Phase phase;const wchar_t* badge;const wchar_t* message;};
inline Presentation Present(const State& state)noexcept {
    if(state.finished) {
        if(!state.end.Satisfied())
            return {Phase::NeedsAttention,L"CLOSE UNCONFIRMED",state.end.deadlineExpired?
                L"The close deadline passed without confirming disconnection. Review the diagnostic report, then close this viewer before reconnecting.":
                L"Disconnection could not be confirmed. Review the diagnostic report, then close this viewer before reconnecting."};
        if(!state.baselineRestored)
            return {Phase::NeedsAttention,L"NEEDS ATTENTION",state.preserveEnabled?
                L"The existing Windows setting could not be verified. Check the diagnostic report before retrying.":
                L"A setting could not be restored. Check the diagnostic report before retrying."};
        if(state.operationFailed||state.end.CallsFailed())
            return {Phase::NeedsAttention,L"VIEWER STOPPED",state.preserveEnabled?
                L"The viewer stopped with an error. The existing Windows setting remains enabled. Close this viewer, then refresh RETSAR Desktop.":
                state.residualChild?L"The viewer stopped with an error; the desktop may still be running. Close this viewer, then refresh RETSAR Desktop to retry.":
                L"The viewer stopped with an error. Settings restored. Close this viewer, then refresh RETSAR Desktop to retry."};
        if(!state.end.connectAttempted)
            return {Phase::NotConnected,L"VIEWER CLOSED",state.preserveEnabled?
                L"Viewer closed before a connection was attempted. The existing Windows setting remains enabled.":
                L"Viewer closed before a connection was attempted. Your previous settings were restored."};
        if(state.preserveEnabled)
            return {Phase::Disconnected,L"DISCONNECTED",state.residualChild?
                L"Viewer disconnected. The existing Windows setting remains enabled; your desktop may still be running.":
                L"Viewer disconnected. The existing Windows setting remains enabled."};
        return {Phase::Disconnected,L"DISCONNECTED",state.residualChild?
            L"Viewer disconnected. Your desktop may still be running.":
            L"Viewer disconnected. Your previous settings were restored."};
    }
    if(state.closing)
        return {Phase::Disconnecting,L"DISCONNECTING",state.preserveEnabled?
            L"Disconnecting this viewer. Preserving the existing Windows setting...":
            L"Disconnecting and restoring your previous settings..."};
    if(state.desktopObserved)
        return {Phase::Connected,L"DESKTOP CONNECTED",L"Your desktop is ready. Click inside to use it."};
    if(state.authenticationPrompt)
        return {Phase::WaitingForWindows,L"CHECK WINDOWS",L"Windows needs your attention. Check its sign-in or confirmation prompt."};
    if(state.loginComplete)
        return {Phase::Verifying,L"VERIFYING DESKTOP",L"Windows sign-in completed. Verifying the independent desktop..."};
    if(state.transportConnected)
        return {Phase::SigningIn,L"SIGNING IN",L"Connected to Windows. Waiting for desktop sign-in..."};
    if(state.connectionStarted)
        return {Phase::Connecting,L"CONNECTING",L"Connecting to your desktop. Windows may ask you to confirm your identity."};
    return {Phase::Preparing,L"PREPARING",L"Preparing your desktop..."};
}
}
