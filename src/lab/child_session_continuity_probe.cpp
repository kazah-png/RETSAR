#include "child_session_continuity_contract.hpp"
#include "child_session_experiment_io.hpp"
#include <shellapi.h>
#include <cstring>

namespace {
using namespace seats::lab::child::continuity;
using seats::lab::child::experiment::Handle;
using seats::lab::child::experiment::Append;
using seats::lab::child::experiment::ValidReceiptPath;
constexpr UINT_PTR AnimationTimer = 73;
constexpr wchar_t WindowClass[] = L"RETSAR.ChildSessionContinuityProbe";
// GUI-subsystem executable: write only to handles explicitly inherited from the
// launcher. Never allocate or attach a console just to display help or errors.
void WriteOutput(const char* text, bool error = false) noexcept {
    const auto handle = GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    if (!text || !handle || handle == INVALID_HANDLE_VALUE) return;
    auto remaining = std::strlen(text);
    while (remaining) {
        const DWORD chunk = remaining > MAXDWORD ? MAXDWORD : static_cast<DWORD>(remaining);
        DWORD written = 0;
        if (!WriteFile(handle, text, chunk, &written, nullptr) || !written) return;
        text += written; remaining -= written;
    }
}

bool ProcessSession(void*, DWORD& session) noexcept {
    return ProcessIdToSessionId(GetCurrentProcessId(), &session) != FALSE;
}
bool TokenSession(void*, DWORD& session) noexcept {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    DWORD bytes = 0;
    const bool success = GetTokenInformation(token, TokenSessionId, &session,
        sizeof(session), &bytes) && bytes == sizeof(session);
    CloseHandle(token); return success;
}
bool NoImpersonation(void*) noexcept {
    HANDLE token = nullptr;
    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token)) {
        CloseHandle(token); return false;
    }
    return GetLastError() == ERROR_NO_TOKEN;
}
DWORD ConsoleSession(void*) noexcept { return WTSGetActiveConsoleSessionId(); }
bool SessionActive(void*, DWORD session) noexcept {
    LPWSTR data = nullptr; DWORD bytes = 0;
    const BOOL queried = WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,
        session, WTSConnectState, &data, &bytes);
    const bool active = queried && data && bytes == sizeof(WTS_CONNECTSTATE_CLASS) &&
        *reinterpret_cast<WTS_CONNECTSTATE_CLASS*>(data) == WTSActive;
    if (data) WTSFreeMemory(data);
    return active;
}
const IdentityApi SystemIdentity{nullptr, ProcessSession, TokenSession,
    NoImpersonation, ConsoleSession, SessionActive};

// Current settings only; no mode enumeration, configuration, device names or
// EDID. This API reports physical pixels without DPI virtualization.
// https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-enumdisplaysettingsexw
DisplayModeResult CurrentDisplayMode(void*) noexcept {
    DEVMODEW mode{}; mode.dmSize = sizeof(mode);
    SetLastError(ERROR_SUCCESS);
    const bool succeeded = EnumDisplaySettingsExW(nullptr, ENUM_CURRENT_SETTINGS, &mode, 0) != FALSE;
    const DWORD lastError = succeeded ? ERROR_SUCCESS : GetLastError();
    DisplayModeResult result; result.succeeded = succeeded;
    result.lastErrorObserved = lastError;
    if (succeeded) {
        result.fields = mode.dmFields;
        if (mode.dmFields & DM_PELSWIDTH) result.width = mode.dmPelsWidth;
        if (mode.dmFields & DM_PELSHEIGHT) result.height = mode.dmPelsHeight;
    }
    return result;
}
DisplayObservation ObserveDisplay(const Options& options) noexcept {
    return ReadDisplay(options, ReadIdentity(options, SystemIdentity), {nullptr, CurrentDisplayMode});
}
void WriteDisplay(std::ostream& out, const DisplayObservation& display) {
    out << "{\"sessionGuardAccepted\":" << (display.sessionGuardAccepted ? "true" : "false")
        << ",\"queryAttempted\":" << (display.attempted ? "true" : "false")
        << ",\"querySucceeded\":" << (display.succeeded ? "true" : "false")
        << ",\"dimensionsKnown\":" << (display.dimensionsKnown ? "true" : "false")
        << ",\"fields\":" << display.fields << ",\"width\":";
    if (display.dimensionsKnown) out << display.width; else out << "null";
    out << ",\"height\":";
    if (display.dimensionsKnown) out << display.height; else out << "null";
    out << ",\"policyError\":" << display.policyError << ",\"queryLastErrorObserved\":";
    if (display.attempted && !display.succeeded) out << display.lastErrorObserved; else out << "null";
    out << '}';
}

struct Probe {
    Options options;
    HANDLE receipt = INVALID_HANDLE_VALUE;
    HWND window = nullptr;
    ULONGLONG started = 0;
    DWORD actualSession = MAXDWORD, error = ERROR_SUCCESS;
    Counters counters;
    DisplayObservation initialDisplay;
    bool finished = false, completed = false, receiptWritten = false;
    ~Probe() { if (!finished) Finish("aborted", ERROR_CANCELLED); }
    ULONGLONG Elapsed() const noexcept { return GetTickCount64() - started; }
    void Finish(const char* reason, DWORD failure = ERROR_SUCCESS) noexcept {
        if (finished) return;
        finished = true; error = failure;
        const auto elapsed = Elapsed(); counters.Finish(elapsed);
        if (window) KillTimer(window, AnimationTimer);
        const auto finalDisplay = ObserveDisplay(options);
        if (!finalDisplay.sessionGuardAccepted && error == ERROR_SUCCESS) error = ERROR_ACCESS_DENIED;
        completed = error == ERROR_SUCCESS && DurationReached(options, elapsed) &&
            counters.monotonic && counters.ticks > 0 && counters.paints > 0;
        try {
            std::ostringstream out; out.imbue(std::locale::classic());
            out << "{\"schema\":\"retsar.child-session-continuity.v1\",\"stage\":\"final\""
                << ",\"reason\":\"" << reason << "\",\"completed\":" << (completed ? "true" : "false")
                << ",\"expectedSession\":" << options.expected << ",\"referenceSession\":" << options.reference
                << ",\"actualSession\":" << actualSession << ",\"requestedDurationSeconds\":" << options.seconds
                << ",\"elapsedMs\":" << elapsed << ",\"timerTicks\":" << counters.ticks
                << ",\"paintCount\":" << counters.paints << ",\"lastTickElapsedMs\":" << counters.lastTickMs
                << ",\"lastPaintElapsedMs\":" << counters.lastPaintMs
                << ",\"maxTickGapMs\":" << counters.maxTickGapMs
                << ",\"maxPaintGapMs\":" << counters.maxPaintGapMs
                << ",\"monotonic\":" << (counters.monotonic ? "true" : "false")
                << ",\"error\":" << error << ",\"initialDisplay\":";
            WriteDisplay(out, initialDisplay);
            out << ",\"finalDisplay\":"; WriteDisplay(out, finalDisplay);
            out << ",\"inputInjected\":false,\"screenCaptured\":false,\"clientDisplayContinuityVerified\":false"
                << ",\"physicalIsolationVerified\":false}";
            receiptWritten = Append(receipt, out.str()).succeeded;
        } catch (...) { receiptWritten = false; }
        if (window) { const auto owned = window; window = nullptr; DestroyWindow(owned); }
    }
    void Tick() noexcept {
        const auto identity = ReadIdentity(options, SystemIdentity);
        if (!Allowed(options, identity)) { Finish("session-guard-lost", ERROR_ACCESS_DENIED); return; }
        const auto elapsed = Elapsed();
        counters.Tick(elapsed);
        if (!counters.monotonic) { Finish("clock-regressed", ERROR_INVALID_DATA); return; }
        if (DurationReached(options, elapsed)) { Finish("duration-reached"); return; }
        InvalidateRect(window, nullptr, FALSE);
    }
    void Paint(HDC dc) noexcept {
        const auto elapsed = Elapsed();
        counters.Paint(elapsed);
        RECT area{}; GetClientRect(window, &area);
        const int saved = SaveDC(dc);
        SelectObject(dc, GetStockObject(DC_BRUSH));
        SetDCBrushColor(dc, RGB(12, 20, 34));
        FillRect(dc, &area, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(226, 238, 248));
        SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
        wchar_t text[320]{};
        swprintf_s(text, L"RETSAR continuity test\nSession %lu | %llu / %lu seconds\nTicks: %llu | Paints: %llu\n"
            L"This window generates no input. Close it to stop early.", actualSession,
            elapsed / 1000, options.seconds, counters.ticks, counters.paints);
        RECT caption{20, 16, area.right - 20, 105};
        DrawTextW(dc, text, -1, &caption, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        const LONG available = area.right > 80 ? area.right - 80 : 1;
        const LONG x = 20 + static_cast<LONG>((elapsed / 25) % static_cast<ULONGLONG>(available));
        SetDCBrushColor(dc, RGB(78, 220, 190));
        RECT pulse{x, 120, x + 32, 152};
        FillRect(dc, &pulse, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        if (saved) RestoreDC(dc, saved);
    }
};
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto* probe = reinterpret_cast<Probe*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        probe = static_cast<Probe*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        probe->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(probe));
    }
    if (!probe) return DefWindowProcW(window, message, wp, lp);
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{}; const auto dc = BeginPaint(window, &paint);
        if (!probe->finished) probe->Paint(dc);
        EndPaint(window, &paint); return 0;
    }
    case WM_TIMER: if (wp == AnimationTimer) probe->Tick(); return 0;
    case WM_CLOSE: probe->Finish("user-closed", ERROR_CANCELLED); return 0;
    case WM_DESTROY: probe->window = nullptr; PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}
}
int Run(int argc, wchar_t* argv[]) {
    try {
        const auto options = Parse(std::vector<std::wstring>(argv + 1, argv + argc));
        if (!options.run) {
            WriteOutput("RETSAR child-session continuity probe\n"
                "Usage: RETSAR-child-session-continuity-probe.exe --duration-seconds <30..120> --expected-session <child> --reference-session <parent> --receipt <new-private.jsonl>\n"
                "Own passive animation window only; no input, screenshots, hooks, focus stealing or tasks.\n"
                "Counts local timer/paint work, not delivered video frames or physical isolation.\n"
                "Initial/final current display-mode metadata is read inside the admitted child; no display settings are changed.\n"
                "Requires actual process/token in the expected active child, distinct from the active console reference.\n"
                "Help/default performs no session queries, windows or receipt writes; output uses inherited handles only.\n");
            return 0;
        }
        const auto identity = ReadIdentity(options, SystemIdentity);
        if (!Allowed(options, identity)) { WriteOutput("Session admission rejected. No window or receipt created.\n", true); return 3; }
        std::wstring path;
        if (!ValidReceiptPath(options.receipt.c_str(), path)) { WriteOutput("Receipt path rejected.\n", true); return 64; }
        Handle receipt;
        receipt.value = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (receipt.value == INVALID_HANDLE_VALUE) return 4;
        BY_HANDLE_FILE_INFORMATION info{}; wchar_t finalPath[32768]{};
        const auto length = GetFinalPathNameByHandleW(receipt.value, finalPath,
            static_cast<DWORD>(std::size(finalPath)), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        const auto expectedPath = L"\\\\?\\" + path;
        if (GetFileType(receipt.value) != FILE_TYPE_DISK || !GetFileInformationByHandle(receipt.value, &info) ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
            !length || length >= std::size(finalPath) || _wcsicmp(finalPath, expectedPath.c_str()) != 0) return 4;
        Probe probe; probe.options = options; probe.receipt = receipt.value;
        probe.actualSession = identity.process; probe.started = GetTickCount64();
        probe.initialDisplay = ObserveDisplay(options);
        if (!probe.initialDisplay.sessionGuardAccepted) {
            probe.Finish("session-guard-lost", ERROR_ACCESS_DENIED); return 3;
        }
        WNDCLASSEXW definition{sizeof(definition)};
        definition.lpfnWndProc = WindowProc; definition.hInstance = GetModuleHandleW(nullptr);
        definition.hCursor = LoadCursorW(nullptr, IDC_ARROW); definition.lpszClassName = WindowClass;
        if (!RegisterClassExW(&definition)) { probe.Finish("register-window", GetLastError()); return 5; }
        HWND window = CreateWindowExW(WS_EX_NOACTIVATE, WindowClass, L"RETSAR continuity test",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, 30, 30, 540, 220,
            nullptr, nullptr, definition.hInstance, &probe);
        if (!window) { probe.Finish("create-window", GetLastError()); return 5; }
        // Unlike a first ShowWindow call, this does not let STARTUPINFO replace
        // the requested no-activation behavior. Keep the passive window style.
        if (!SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
            const DWORD error = GetLastError();
            probe.Finish("show-window", error ? error : ERROR_GEN_FAILURE); return 5;
        }
        if (!SetTimer(window, AnimationTimer, 250, nullptr)) probe.Finish("timer-create", GetLastError());
        MSG message{}; BOOL messageResult = 0;
        while (!probe.finished && (messageResult = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (!probe.finished) probe.Finish("message-loop-ended",
            messageResult < 0 ? GetLastError() : ERROR_CANCELLED);
        UnregisterClassW(WindowClass, definition.hInstance);
        return probe.completed && probe.receiptWritten ? 0 : 2;
    } catch (const std::invalid_argument& error) { WriteOutput(error.what(), true); WriteOutput("\n", true); return 64; }
      catch (...) { WriteOutput("Continuity probe failed.\n", true); return 5; }
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    auto** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) { WriteOutput("Command-line parsing failed.\n", true); return 64; }
    const int result = Run(argc, argv);
    LocalFree(argv);
    return result;
}