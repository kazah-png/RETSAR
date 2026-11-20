#pragma once
#include "child_session_activex.hpp"

namespace seats::lab::child::resolution {
inline constexpr GUID Client9Id = {0x28904001, 0x04b6, 0x436c,
    {0xa5, 0x5b, 0x0a, 0xf1, 0xa0, 0x88, 0x3d, 0xc9}};
inline constexpr ULONGLONG ObservationTimeoutMs = 10000;
inline constexpr ULONGLONG MaximumRequestedPixels = 16'000'000ull;
struct Settings {
    ULONG width = 0, height = 0, physicalWidth = 0, physicalHeight = 0;
    ULONG orientation = 0, desktopScale = 100, deviceScale = 100;
};
inline Settings ForViewport(LONG width, LONG height) noexcept {
    Settings result;
    result.width = width > 0 ? static_cast<ULONG>(width) & ~1ul : 0;
    result.height = height > 0 ? static_cast<ULONG>(height) : 0;
    return result;
}
// Protocol bounds: MS-RDPEDISP DISPLAYCONTROL_MONITOR_LAYOUT. The 16M-pixel
// request ceiling is RETSAR policy, not a Windows protocol maximum. 0/0 mm means
// unknown/ignored; 100/100 explicitly requests 100% scale, not preservation.
// https://learn.microsoft.com/openspecs/windows_protocols/ms-rdpedisp/ea2de591-9203-42cd-9908-be7a55237d1c
inline bool Dimensions(ULONG width, ULONG height) noexcept {
    return width >= 200 && width <= 8192 && !(width & 1) && height >= 200 && height <= 8192;
}
inline bool Valid(const Settings& value) noexcept {
    const bool physical = (!value.physicalWidth && !value.physicalHeight) ||
        (value.physicalWidth >= 10 && value.physicalWidth <= 10000 &&
         value.physicalHeight >= 10 && value.physicalHeight <= 10000);
    return Dimensions(value.width, value.height) &&
        static_cast<ULONGLONG>(value.width) * value.height <= MaximumRequestedPixels &&
        physical && (value.orientation == 0 || value.orientation == 90 ||
        value.orientation == 180 || value.orientation == 270) &&
        value.desktopScale >= 100 && value.desktopScale <= 500 &&
        (value.deviceScale == 100 || value.deviceScale == 140 || value.deviceScale == 180);
}
struct Result {
    HRESULT result = E_PENDING;
    bool supported = false, attempted = false, accepted = false;
};
struct Readback {
    HRESULT result = E_PENDING;
    bool known = false;
    ULONG width = 0, height = 0;
};
namespace detail {
struct Held {
    IDispatch* object; unsigned& depth;
    Held(IDispatch* value, unsigned& count) noexcept : object(value), depth(count) {
        ++depth; object->AddRef();
    }
    ~Held() { object->Release(); --depth; }
};
inline bool Cancelled(const bool* cancelled) noexcept { return cancelled && *cancelled; }
inline bool CanonicalSuccess(HRESULT result) noexcept { return result == S_OK; }
inline HRESULT ExactFailure(HRESULT result) noexcept { return FAILED(result) ? result : E_UNEXPECTED; }
inline HRESULT InvokeUpdate(IDispatch* client, const Settings& settings,
    const bool* cancelled, Result& result) noexcept {
    const auto serial = ax::lastDispatch.serial + 1;
    ax::lastDispatch = {}; ax::lastDispatch.serial = serial;
    auto* name = const_cast<wchar_t*>(L"UpdateSessionDisplaySettings");
    DISPID member = DISPID_UNKNOWN;
    HRESULT hr = client->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &member);
    if (!CanonicalSuccess(hr)) return ax::lastDispatch.result = ExactFailure(hr);
    if (member < 0) return ax::lastDispatch.result = DISP_E_MEMBERNOTFOUND;
    result.supported = true;
    if (Cancelled(cancelled)) return ax::lastDispatch.result = E_ABORT;
    // IMsRdpClient9 documents seven ULONGs; Automation arguments are reversed.
    // https://learn.microsoft.com/previous-versions/windows/desktop/legacy/mt703457(v=vs.85)
    const ULONG values[7]{settings.deviceScale, settings.desktopScale, settings.orientation,
        settings.physicalHeight, settings.physicalWidth, settings.height, settings.width};
    VARIANT arguments[7]{};
    for (unsigned i = 0; i != 7; ++i) { arguments[i].vt = VT_UI4; arguments[i].ulVal = values[i]; }
    DISPPARAMS parameters{arguments, nullptr, 7, 0};
    EXCEPINFO exception{}; UINT argumentError = 0;
    result.attempted = true;
    hr = client->Invoke(member, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD,
        &parameters, nullptr, &exception, &argumentError);
    if (hr == DISP_E_EXCEPTION && exception.pfnDeferredFillIn) {
        ax::lastDispatch.deferred = true;
        ax::lastDispatch.deferredResult = exception.pfnDeferredFillIn(&exception);
    }
    ax::lastDispatch.result = hr;
    ax::lastDispatch.exceptionCode = exception.scode;
    ax::lastDispatch.exceptionWord = exception.wCode;
    SysFreeString(exception.bstrSource); SysFreeString(exception.bstrDescription);
    SysFreeString(exception.bstrHelpFile);
    result.accepted = hr == S_OK;
    return hr;
}
}
// The caller still owns child-session admission and defers calls during startup,
// close, or an unobserved connection. A busy COM stack performs no COM calls.
// No reconnect, disconnect, fullscreen, scaling-property or host-display setter
// is called. S_OK records acceptance only; it does not confirm a remote size.
inline Result Request(IDispatch* client, const Settings& settings,
    unsigned& outboundDepth, const bool* cancelled = nullptr) noexcept {
    Result result;
    if (!client) { result.result = E_POINTER; return result; }
    if (!Valid(settings)) { result.result = E_INVALIDARG; return result; }
    if (outboundDepth) return result;
    if (detail::Cancelled(cancelled)) { result.result = E_ABORT; return result; }
    detail::Held held(client, outboundDepth);
    if (detail::Cancelled(cancelled)) { result.result = E_ABORT; return result; }
    ax::Com<IUnknown> version;
    const HRESULT versionResult = client->QueryInterface(Client9Id, reinterpret_cast<void**>(version.put()));
    if (versionResult != S_OK || !version) {
        result.result = versionResult == S_OK ? E_NOINTERFACE : detail::ExactFailure(versionResult); return result;
    }
    if (detail::Cancelled(cancelled)) { result.result = E_ABORT; return result; }
    // QI verifies IMsRdpClient9 support without declaring its inherited ABI or
    // importing a private generated type library. Invoke uses the held dispatch.
    ax::Variant connected;
    const HRESULT connectedResult = ax::Invoke(client, L"Connected", DISPATCH_PROPERTYGET, nullptr, &connected.value);
    if (connectedResult != S_OK) { result.result = detail::ExactFailure(connectedResult); return result; }
    if ((connected.value.vt != VT_I2 && connected.value.vt != VT_I4)) {
        result.result = DISP_E_TYPEMISMATCH; return result;
    }
    const LONG state = connected.value.vt == VT_I2 ? connected.value.iVal : connected.value.lVal;
    if (state != 1) { result.result = HRESULT_FROM_WIN32(ERROR_INVALID_STATE); return result; }
    if (detail::Cancelled(cancelled)) { result.result = E_ABORT; return result; }
    result.result = detail::InvokeUpdate(client, settings, cancelled, result);
    return result;
}
// These are INITIAL desktop properties. Known readback is informational only;
// matching values never advance Pending or confirm effective remote geometry.
// https://learn.microsoft.com/windows/win32/termserv/imstscax-desktopwidth
inline Readback ReadProperties(IDispatch* client, unsigned& outboundDepth,
    const bool* cancelled = nullptr) noexcept {
    Readback result;
    if (!client) { result.result = E_POINTER; return result; }
    if (outboundDepth) return result;
    if (detail::Cancelled(cancelled)) { result.result = E_ABORT; return result; }
    detail::Held held(client, outboundDepth);
    ax::Variant width, height;
    if (detail::Cancelled(cancelled)) { result.result = E_ABORT; return result; }
    result.result = ax::Invoke(client, L"DesktopWidth", DISPATCH_PROPERTYGET, nullptr, &width.value);
    if (result.result != S_OK) { result.result = detail::ExactFailure(result.result); return result; }
    if (detail::Cancelled(cancelled)) { result.result = E_ABORT; return result; }
    result.result = ax::Invoke(client, L"DesktopHeight", DISPATCH_PROPERTYGET, nullptr, &height.value);
    if (result.result != S_OK) { result.result = detail::ExactFailure(result.result); return result; }
    if (detail::Cancelled(cancelled)) { result.result = E_ABORT; return result; }
    if (width.value.vt != VT_I4 || height.value.vt != VT_I4) { result.result = DISP_E_TYPEMISMATCH; return result; }
    if (width.value.lVal < 0 || height.value.lVal < 0 ||
        !Dimensions(static_cast<ULONG>(width.value.lVal), static_cast<ULONG>(height.value.lVal))) {
        result.result = E_UNEXPECTED; return result;
    }
    result.width = static_cast<ULONG>(width.value.lVal); result.height = static_cast<ULONG>(height.value.lVal);
    result.known = true; return result;
}
enum class Phase { Idle, Awaiting, Confirmed, Unsupported, Failed, TimedOut, Cancelled };
struct Pending {
    Phase phase = Phase::Idle;
    Settings requested{};
    Result result{};
    bool eventObserved = false, requestCompleted = false;
    ULONGLONG started = 0, eventSerial = 0, baselineEventSerial = 0;
    bool Begin(const Settings& settings, ULONGLONG now) noexcept {
        if (phase == Phase::Awaiting || !Valid(settings)) return false;
        requested = settings; started = now; baselineEventSerial = eventSerial;
        result = {}; eventObserved = requestCompleted = false; phase = Phase::Awaiting; return true;
    }
    void Tick(ULONGLONG now) noexcept {
        if (phase != Phase::Awaiting) return;
        if (now < started) { phase = Phase::Failed; result.result = E_UNEXPECTED; }
        else if (now - started >= ObservationTimeoutMs) {
            phase = Phase::TimedOut; result.result = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
    }
    void Complete(const Result& value, ULONGLONG now) noexcept {
        if (phase != Phase::Awaiting) return;
        result = value; requestCompleted = true; Tick(now);
        if (phase != Phase::Awaiting) return;
        if (value.result == E_ABORT) phase = Phase::Cancelled;
        else if (!value.attempted && (value.result == E_NOINTERFACE ||
            value.result == DISP_E_UNKNOWNNAME || value.result == DISP_E_MEMBERNOTFOUND)) phase = Phase::Unsupported;
        else if (value.result != S_OK || !value.supported || !value.attempted || !value.accepted) phase = Phase::Failed;
        else if (eventObserved) phase = Phase::Confirmed;
    }
    // Call ONLY from this control's actual DISPID 12 callback. Do not replay a
    // stored event. Begin must run before Request to preserve synchronous events.
    // Width/height are VT_I4 and reverse-ordered in DISPPARAMS, like other events.
    // https://learn.microsoft.com/windows/win32/termserv/imstscaxevents-onremotedesktopsizechange
    bool ObserveEvent(const DISPPARAMS* parameters, ULONGLONG now) noexcept {
        if (!parameters || parameters->cArgs != 2 || parameters->cNamedArgs ||
            !parameters->rgvarg || parameters->rgvarg[0].vt != VT_I4 || parameters->rgvarg[1].vt != VT_I4)
            return false;
        const auto width = parameters->rgvarg[1].lVal, height = parameters->rgvarg[0].lVal;
        if (width < 0 || height < 0 || !Dimensions(static_cast<ULONG>(width), static_cast<ULONG>(height))) return false;
        if (eventSerial == ~0ull) { Cancel(); return false; }
        ++eventSerial; Tick(now);
        if (phase != Phase::Awaiting || eventSerial <= baselineEventSerial) return false;
        eventObserved = static_cast<ULONG>(width) == requested.width && static_cast<ULONG>(height) == requested.height;
        if (!eventObserved) return false; // A later different size supersedes an earlier candidate.
        if (requestCompleted && result.result == S_OK && result.supported && result.attempted && result.accepted)
            phase = Phase::Confirmed;
        return true;
    }
    void Cancel() noexcept { if (phase == Phase::Awaiting) { phase = Phase::Cancelled; result.result = E_ABORT; } }
};
} // namespace seats::lab::child::resolution