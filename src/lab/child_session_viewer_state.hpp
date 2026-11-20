#pragma once

namespace seats::lab::child::viewer {
// Presentation only: these supplied observations authorize no session operation.
enum class Phase { Preparing, Connecting, WaitingForWindows, SigningIn, Verifying,
                   Connected, Disconnecting, Disconnected, NeedsAttention };
struct State {
    bool connectionStarted=false,authenticationPrompt=false,transportConnected=false;
    bool loginComplete=false,desktopObserved=false,closing=false,finished=false;
    bool baselineRestored=false,operationFailed=false,residualChild=false;
};
struct Presentation {Phase phase;const wchar_t* badge;const wchar_t* message;};
inline Presentation Present(const State& state)noexcept {
    if(state.finished) {
        if(!state.baselineRestored)
            return {Phase::NeedsAttention,L"NEEDS ATTENTION",L"A setting could not be restored. Check the diagnostic report before retrying."};
        if(state.operationFailed)
            return {Phase::NeedsAttention,L"CONNECTION STOPPED",state.residualChild?
                L"Connection stopped; the desktop may still be running. Close this viewer, then refresh RETSAR Desktop to retry.":
                L"Connection stopped. Settings restored. Close this viewer, then refresh RETSAR Desktop to retry."};
        return {Phase::Disconnected,L"DISCONNECTED",state.residualChild?
            L"Viewer disconnected. Your desktop may still be running.":
            L"Viewer disconnected. Your previous settings were restored."};
    }
    if(state.closing)
        return {Phase::Disconnecting,L"DISCONNECTING",L"Disconnecting and restoring your previous settings..."};
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
