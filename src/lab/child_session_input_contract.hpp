#pragma once
#include <Windows.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace seats::lab::child::input {
struct Options {
    bool run=false;
    DWORD child=MAXDWORD,parent=MAXDWORD;
    std::wstring nonce,receipt;
};
inline DWORD SessionId(const std::wstring& text) {
    if(text.empty())throw std::invalid_argument("Missing session ID.");
    DWORD value=0;
    for(wchar_t ch:text) {
        if(ch<L'0'||ch>L'9'||value>(MAXDWORD-static_cast<DWORD>(ch-L'0'))/10)
            throw std::invalid_argument("Invalid session ID.");
        value=value*10+static_cast<DWORD>(ch-L'0');
    }
    if(!value||value==MAXDWORD)throw std::invalid_argument("Noninteractive session ID.");
    return value;
}
inline bool ValidNonce(const std::wstring& text)noexcept {
    if(text.size()!=32)return false;
    for(wchar_t ch:text)if(!((ch>=L'0'&&ch<=L'9')||(ch>=L'a'&&ch<=L'f')))return false;
    return true;
}
// This 31-bit value is only a per-run input correlation tag, not authentication
// or proof of ownership. Collisions are possible; the independent token/session
// and owned-window guards remain mandatory. A positive value avoids sign
// extension and survives the metadata width observed in the child transport.
// Received values must still be compared as exact, unmodified ULONG_PTR values.
inline ULONG_PTR CorrelationMarkerFromNonce(const std::wstring& nonce)noexcept {
    if(!ValidNonce(nonce))return 0;
    ULONGLONG hash=1469598103934665603ull;
    for(wchar_t ch:nonce)hash=(hash^static_cast<ULONGLONG>(ch))*1099511628211ull;
    const auto positive=static_cast<ULONG_PTR>(hash&0x7fffffffull);
    return positive?positive:1;
}
inline Options Parse(const std::vector<std::wstring>& args) {
    if(args.empty()||(args.size()==1&&args[0]==L"--help"))return {};
    if(args.size()!=9||args[0]!=L"--run"||args[1]!=L"--child-session"||
       args[3]!=L"--parent-session"||args[5]!=L"--nonce"||args[7]!=L"--receipt")
        throw std::invalid_argument("Use the exact explicit child-probe arguments.");
    Options result;result.run=true;result.child=SessionId(args[2]);result.parent=SessionId(args[4]);
    result.nonce=args[6];result.receipt=args[8];
    if(result.child==result.parent||!ValidNonce(result.nonce)||result.receipt.empty())
        throw std::invalid_argument("Child, parent, nonce or receipt rejected.");
    return result;
}
// A pure decision over fresh observations, not a claim of physical isolation.
// SendInput has no destination HWND; focus can change after any observation.
struct Identity {
    bool processKnown=false,tokenKnown=false,noImpersonation=false;
    DWORD processSession=MAXDWORD,tokenSession=MAXDWORD,consoleSession=MAXDWORD;
    bool childActive=false,parentActive=false;
};
inline bool SessionAllowed(const Options& options,const Identity& identity)noexcept {
    return options.run&&options.child!=0&&options.child!=MAXDWORD&&options.parent!=0&&
        options.parent!=MAXDWORD&&options.child!=options.parent&&ValidNonce(options.nonce)&&
        identity.processKnown&&identity.tokenKnown&&identity.noImpersonation&&
        identity.processSession==options.child&&identity.tokenSession==options.child&&
        identity.consoleSession==options.parent&&identity.childActive&&identity.parentActive;
}
struct WindowEvidence {
    bool inputDesktopMatches=false,foregroundIsOwnedWindow=false;
    bool windowProcessMatches=false,windowThreadMatches=false,windowEnabled=false,windowVisible=false;
};
inline bool InputAllowed(const Options& options,const Identity& identity,const WindowEvidence& window)noexcept {
    return SessionAllowed(options,identity)&&window.inputDesktopMatches&&window.foregroundIsOwnedWindow&&
        window.windowProcessMatches&&window.windowThreadMatches&&window.windowEnabled&&window.windowVisible;
}
}
