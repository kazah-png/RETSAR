#pragma once
#include <Windows.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace seats::lab::child::continuity {
struct Options {
    bool run = false;
    DWORD seconds = 0, expected = MAXDWORD, reference = MAXDWORD;
    std::wstring receipt;
};
inline DWORD Unsigned(const std::wstring& text) {
    if (text.empty()) throw std::invalid_argument("Missing numeric value.");
    DWORD value = 0;
    for (const auto character : text) {
        if (character < L'0' || character > L'9' ||
            value > (MAXDWORD - static_cast<DWORD>(character - L'0')) / 10)
            throw std::invalid_argument("Invalid numeric value.");
        value = value * 10 + static_cast<DWORD>(character - L'0');
    }
    return value;
}
inline bool ValidOptions(const Options& options) noexcept {
    return options.run && options.seconds >= 30 && options.seconds <= 120 &&
        options.expected && options.expected != MAXDWORD && options.reference &&
        options.reference != MAXDWORD && options.expected != options.reference &&
        !options.receipt.empty() && options.receipt.size() <= 2048;
}
inline Options Parse(const std::vector<std::wstring>& arguments) {
    if (arguments.empty() || (arguments.size() == 1 && arguments[0] == L"--help")) return {};
    if (arguments.size() != 8 || arguments[0] != L"--duration-seconds" ||
        arguments[2] != L"--expected-session" || arguments[4] != L"--reference-session" ||
        arguments[6] != L"--receipt")
        throw std::invalid_argument("Use the exact explicit continuity-probe arguments.");
    Options options{true, Unsigned(arguments[1]), Unsigned(arguments[3]),
        Unsigned(arguments[5]), arguments[7]};
    if (!ValidOptions(options)) throw std::invalid_argument("Duration, session or receipt argument rejected.");
    return options;
}
struct Identity {
    bool processKnown = false, tokenKnown = false, noImpersonation = false;
    DWORD process = MAXDWORD, token = MAXDWORD, console = MAXDWORD;
    bool expectedActive = false, referenceActive = false;
};
struct IdentityApi {
    void* context = nullptr;
    bool (*process)(void*, DWORD&) noexcept = nullptr;
    bool (*token)(void*, DWORD&) noexcept = nullptr;
    bool (*noImpersonation)(void*) noexcept = nullptr;
    DWORD (*console)(void*) noexcept = nullptr;
    bool (*active)(void*, DWORD) noexcept = nullptr;
};
// This boundary observes only session metadata. No input, session creation,
// settings, task scheduling, window or file API is available to the contract.
inline Identity ReadIdentity(const Options& options, const IdentityApi& api) noexcept {
    Identity result;
    if (!ValidOptions(options) || !api.process || !api.token || !api.noImpersonation ||
        !api.console || !api.active) return result;
    result.processKnown = api.process(api.context, result.process);
    result.tokenKnown = api.token(api.context, result.token);
    result.noImpersonation = api.noImpersonation(api.context);
    result.console = api.console(api.context);
    result.expectedActive = api.active(api.context, options.expected);
    result.referenceActive = api.active(api.context, options.reference);
    return result;
}
inline bool Allowed(const Options& options, const Identity& identity) noexcept {
    return ValidOptions(options) && identity.processKnown && identity.tokenKnown &&
        identity.noImpersonation && identity.process == options.expected &&
        identity.token == options.expected && identity.console == options.reference &&
        identity.expectedActive && identity.referenceActive;
}
// A current mode is metadata from the admitted child session. It does not
// establish that any frame reached the viewer or that hardware is assigned.
struct DisplayModeResult {
    bool succeeded = false;
    DWORD fields = 0, width = 0, height = 0, lastErrorObserved = 0;
};
struct DisplayModeApi {
    void* context = nullptr;
    DisplayModeResult (*readCurrent)(void*) noexcept = nullptr;
};
struct DisplayObservation {
    bool sessionGuardAccepted = false, attempted = false, succeeded = false, dimensionsKnown = false;
    DWORD fields = 0, width = 0, height = 0;
    DWORD policyError = ERROR_NOT_READY, lastErrorObserved = 0;
};
inline DisplayObservation ReadDisplay(const Options& options, const Identity& identity,
    const DisplayModeApi& api) noexcept {
    DisplayObservation result;
    result.sessionGuardAccepted = Allowed(options, identity);
    if (!result.sessionGuardAccepted) { result.policyError = ERROR_ACCESS_DENIED; return result; }
    if (!api.readCurrent) { result.policyError = ERROR_INVALID_FUNCTION; return result; }
    result.attempted = true;
    const auto mode = api.readCurrent(api.context);
    result.succeeded = mode.succeeded;
    if (!mode.succeeded) {
        // EnumDisplaySettingsEx documents a BOOL, not an extended error-code
        // contract. Preserve last-error only as an observation, never success.
        result.lastErrorObserved = mode.lastErrorObserved;
        result.policyError = ERROR_GEN_FAILURE; return result;
    }
    result.fields = mode.fields;
    constexpr DWORD dimensions = DM_PELSWIDTH | DM_PELSHEIGHT;
    if ((mode.fields & dimensions) != dimensions || !mode.width || !mode.height) {
        result.policyError = ERROR_INVALID_DATA; return result;
    }
    // Observe the actual mode even if it is outside a separate request policy.
    result.width = mode.width; result.height = mode.height;
    result.dimensionsKnown = true; result.policyError = ERROR_SUCCESS;
    return result;
}
struct Counters {
    ULONGLONG ticks = 0, paints = 0, lastTickMs = 0, lastPaintMs = 0;
    ULONGLONG maxTickGapMs = 0, maxPaintGapMs = 0;
    bool monotonic = true;
    void Tick(ULONGLONG elapsed) noexcept {
        if (elapsed < lastTickMs) { monotonic = false; return; }
        const auto gap = elapsed - lastTickMs;
        if (gap > maxTickGapMs) maxTickGapMs = gap;
        lastTickMs = elapsed; ++ticks;
    }
    void Paint(ULONGLONG elapsed) noexcept {
        if (elapsed < lastPaintMs) { monotonic = false; return; }
        const auto gap = elapsed - lastPaintMs;
        if (gap > maxPaintGapMs) maxPaintGapMs = gap;
        lastPaintMs = elapsed; ++paints;
    }
    void Finish(ULONGLONG elapsed) noexcept {
        if (elapsed < lastTickMs || elapsed < lastPaintMs) { monotonic = false; return; }
        const auto tickGap = elapsed - lastTickMs, paintGap = elapsed - lastPaintMs;
        if (tickGap > maxTickGapMs) maxTickGapMs = tickGap;
        if (paintGap > maxPaintGapMs) maxPaintGapMs = paintGap;
    }
};
inline bool DurationReached(const Options& options, ULONGLONG elapsed) noexcept {
    return ValidOptions(options) && elapsed >= static_cast<ULONGLONG>(options.seconds) * 1000;
}
// WM_PAINT counts only this process's GDI work. They are not received video
// frames, visible host pixels, input isolation or a physical display binding.
} // namespace seats::lab::child::continuity