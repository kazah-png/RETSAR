// Original RETSAR laboratory helper. This does not configure physical displays.
// Only its own software device is created; closing its handle ends presence.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cfgmgr32.h>
#include <swdevice.h>
#include <VersionHelpers.h>
#include <objbase.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwchar>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {
constexpr wchar_t kHardwareId[] = L"RETSARIddLab\0";
constexpr wchar_t kEnumerator[] = L"RETSARIddLab";
constexpr wchar_t kExpectedComputer[] = L"RETSARLAB";
std::atomic<bool> g_stopRequested{false};

std::string Quote(const std::wstring& value) {
    std::ostringstream out;
    out << '"';
    for (wchar_t c : value) {
        if (c == L'"') out << "\\\"";
        else if (c == L'\\') out << "\\\\";
        else if (c >= 0x20 && c <= 0x7e) out << static_cast<char>(c);
        else out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                 << static_cast<unsigned int>(static_cast<unsigned short>(c));
    }
    out << '"';
    return out.str();
}
void Emit(const std::string& fields) {
    std::cout << '{' << fields << '}' << std::endl;
}
std::string Number(ULONG value) { return std::to_string(value); }
std::string Bool(bool value) { return value ? "true" : "false"; }
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
struct Device {
    HSWDEVICE value = nullptr;
    void Close() { if (value) { SwDeviceClose(value); value = nullptr; } }
    ~Device() { Close(); }
    Device() = default;
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
};
BOOL WINAPI StopHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
        g_stopRequested.store(true);
        return TRUE;
    }
    return FALSE;
}
struct CallbackState {
    HANDLE completed = nullptr;
    HSWDEVICE callbackHandle = nullptr;
    HRESULT result = E_PENDING;
    wchar_t instance[MAX_DEVICE_ID_LEN]{};
    bool called = false;
    bool instanceFits = false;
};
void CALLBACK Created(HSWDEVICE handle, HRESULT result, PVOID opaque, PCWSTR instance) {
    auto* state = static_cast<CallbackState*>(opaque);
    state->callbackHandle = handle;
    state->result = result;
    state->instanceFits = instance && wcsncpy_s(state->instance, instance, _TRUNCATE) == 0;
    state->called = true;
    SetEvent(state->completed);
}
std::wstring RegistryString(PCWSTR key, PCWSTR name) {
    wchar_t value[256]{};
    DWORD bytes = sizeof(value);
    const LSTATUS result = RegGetValueW(HKEY_LOCAL_MACHINE, key, name,
        RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, value, &bytes);
    if (result != ERROR_SUCCESS || bytes < sizeof(wchar_t) ||
        bytes > sizeof(value) || value[bytes / sizeof(wchar_t) - 1] != L'\0') return L"";
    return value;
}
bool Elevated() {
    Handle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD returned = 0;
    return GetTokenInformation(token.value, TokenElevation, &elevation,
        sizeof(elevation), &returned) && elevation.TokenIsElevated != 0;
}
struct NodeState {
    CONFIGRET locate = CR_FAILURE;
    CONFIGRET statusResult = CR_FAILURE;
    ULONG status = 0;
    ULONG problem = 0;
    bool present = false;
    bool started = false;
};
NodeState Observe(const wchar_t* instance) {
    NodeState result;
    DEVINST node = 0;
    result.locate = CM_Locate_DevNodeW(&node, const_cast<wchar_t*>(instance), CM_LOCATE_DEVNODE_NORMAL);
    result.present = result.locate == CR_SUCCESS;
    if (result.present) {
        result.statusResult = CM_Get_DevNode_Status(&result.status, &result.problem, node, 0);
        result.started = result.statusResult == CR_SUCCESS && (result.status & DN_STARTED) != 0;
    }
    return result;
}
void EmitNode(const NodeState& state) {
    Emit("\"event\":\"device-observation\",\"present\":" + Bool(state.present) +
        ",\"started\":" + Bool(state.started) + ",\"locateResult\":" + Number(state.locate) +
        ",\"statusResult\":" + Number(state.statusResult) + ",\"status\":" + Number(state.status) +
        ",\"problem\":" + Number(state.problem));
}
bool ParseSeconds(const wchar_t* text, unsigned int& seconds) {
    if (!text || !*text) return false;
    unsigned int value = 0;
    for (const wchar_t* p = text; *p; ++p) {
        if (*p < L'0' || *p > L'9') return false;
        value = value * 10 + static_cast<unsigned int>(*p - L'0');
        if (value > 120) return false;
    }
    if (value < 1) return false;
    seconds = value;
    return true;
}
int Run(int argc, wchar_t** argv) {
    bool approved = false, management = false, hasSeconds = false;
    unsigned int seconds = 120;
    if (argc == 2 && wcscmp(argv[1], L"--help") == 0) {
        Emit("\"event\":\"help\",\"usage\":\"RETSARIddHolder --create-approved [--guest-management] [--seconds 1..120]\",\"hardwareId\":\"RETSARIddLab\",\"scope\":\"RETSARLAB QEMU Windows Server build 20348 administrator only\",\"session0RequiresGuestManagement\":true,\"physicalSeatCreated\":false");
        return 0;
    }
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--create-approved") == 0 && !approved) approved = true;
        else if (wcscmp(argv[i], L"--guest-management") == 0 && !management) management = true;
        else if (wcscmp(argv[i], L"--seconds") == 0 && !hasSeconds && i + 1 < argc) {
            hasSeconds = true;
            if (!ParseSeconds(argv[++i], seconds)) {
                Emit("\"event\":\"refused\",\"reason\":\"seconds-must-be-1-through-120\""); return 2;
            }
        } else { Emit("\"event\":\"refused\",\"reason\":\"invalid-or-duplicate-option\""); return 2; }
    }
    if (!approved) { Emit("\"event\":\"refused\",\"reason\":\"explicit-create-option-required\""); return 2; }

    wchar_t computer[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD nameLength = static_cast<DWORD>(std::size(computer));
    DWORD callerSession = MAXDWORD;
    const bool nameRead = GetComputerNameW(computer, &nameLength) != FALSE;
    const bool sessionRead = ProcessIdToSessionId(GetCurrentProcessId(), &callerSession) != FALSE;
    const DWORD consoleSession = WTSGetActiveConsoleSessionId();
    const auto manufacturer = RegistryString(L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemManufacturer");
    const auto build = RegistryString(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber");
    const bool server = IsWindowsServer();
    const bool elevated = Elevated();
    Emit("\"event\":\"guest-guard\",\"computer\":" + Quote(computer) +
        ",\"manufacturer\":" + Quote(manufacturer) + ",\"build\":" + Quote(build) +
        ",\"server\":" + Bool(server) + ",\"elevated\":" + Bool(elevated) +
        ",\"callerSessionId\":" + Number(callerSession) + ",\"activeConsoleSessionId\":" + Number(consoleSession) +
        ",\"guestManagement\":" + Bool(management) + ",\"sessionOwnershipProven\":false");
    if (!nameRead || _wcsicmp(computer, kExpectedComputer) != 0 || manufacturer != L"QEMU" ||
        build != L"20348" || !server || !elevated || !sessionRead || (callerSession == 0 && !management)) {
        Emit("\"event\":\"refused\",\"reason\":\"guest-identity-admin-or-session-opt-in-check-failed\",\"deviceCreateCalled\":false");
        return 2;
    }

    GUID nonce{};
    wchar_t nonceString[40]{};
    if (FAILED(CoCreateGuid(&nonce)) || !StringFromGUID2(nonce, nonceString, static_cast<int>(std::size(nonceString)))) {
        Emit("\"event\":\"failed\",\"reason\":\"instance-id-generation\""); return 3;
    }
    const std::wstring instanceName = std::wstring(L"RETSAR-") + nonceString;
    Handle completion;
    completion.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    if (!completion.value) {
        Emit("\"event\":\"failed\",\"reason\":\"event-creation\",\"win32Error\":" + Number(GetLastError())); return 3;
    }
    // State is declared before Device: SwDeviceClose completes any pending callback
    // before the context or its event can be destroyed, including error paths.
    CallbackState state;
    state.completed = completion.value;
    Device device;
    SW_DEVICE_CREATE_INFO info{};
    info.cbSize = sizeof(info);
    info.pszInstanceId = instanceName.c_str();
    info.pszzHardwareIds = kHardwareId;
    info.pszDeviceDescription = L"RETSAR isolated laboratory display reference";
    info.CapabilityFlags = SWDeviceCapabilitiesRemovable | SWDeviceCapabilitiesSilentInstall | SWDeviceCapabilitiesDriverRequired;
    const auto begun = std::chrono::steady_clock::now();
    const auto deadline = begun + std::chrono::seconds(seconds);
    const HRESULT createResult = SwDeviceCreate(kEnumerator, L"HTREE\\ROOT\\0", &info,
        0, nullptr, Created, &state, &device.value);
    Emit("\"event\":\"create-returned\",\"result\":" + Number(static_cast<ULONG>(createResult)) +
        ",\"maximumLifetimeSeconds\":" + std::to_string(seconds) + ",\"defaultHandleLifetime\":true");
    int exitCode = 0;
    bool observedPresent = false, observedStarted = false, interrupted = false;
    if (FAILED(createResult) || !device.value) exitCode = 3;
    if (!exitCode) {
        const DWORD callbackWait = (std::min)(10000UL, static_cast<DWORD>(seconds * 1000));
        const DWORD waited = WaitForSingleObject(completion.value, callbackWait);
        if (waited != WAIT_OBJECT_0) {
            Emit("\"event\":\"callback-wait-failed\",\"waitResult\":" + Number(waited)); exitCode = 3;
        } else {
            Emit("\"event\":\"create-callback\",\"result\":" + Number(static_cast<ULONG>(state.result)) +
                ",\"instanceId\":" + Quote(state.instance) + ",\"matchingHandle\":" + Bool(state.callbackHandle == device.value));
            if (!state.called || FAILED(state.result) || !state.instanceFits || !*state.instance || state.callbackHandle != device.value) exitCode = 3;
        }
    }
    if (!exitCode) {
        SW_DEVICE_LIFETIME lifetime{};
        const HRESULT lifetimeResult = SwDeviceGetLifetime(device.value, &lifetime);
        Emit("\"event\":\"lifetime-check\",\"result\":" + Number(static_cast<ULONG>(lifetimeResult)) +
            ",\"handleLifetime\":" + Bool(SUCCEEDED(lifetimeResult) && lifetime == SWDeviceLifetimeHandle));
        if (FAILED(lifetimeResult) || lifetime != SWDeviceLifetimeHandle) exitCode = 3;
    }
    if (!exitCode) {
        g_stopRequested.store(false);
        const bool handlerSet = SetConsoleCtrlHandler(StopHandler, TRUE) != FALSE;
        NodeState last;
        bool haveLast = false;
        while (std::chrono::steady_clock::now() < deadline) {
            const NodeState current = Observe(state.instance);
            observedPresent = observedPresent || current.present;
            observedStarted = observedStarted || current.started;
            if (!haveLast || current.locate != last.locate || current.statusResult != last.statusResult ||
                current.status != last.status || current.problem != last.problem) EmitNode(current);
            last = current; haveLast = true;
            if (g_stopRequested.load()) { interrupted = true; break; }
            Sleep(200);
        }
        if (handlerSet) SetConsoleCtrlHandler(StopHandler, FALSE);

    }
    // No callback can run after SwDeviceClose returns. It makes the owned device
    // nonpresent asynchronously; it does not uninstall a driver package.
    device.Close();
    Emit("\"event\":\"handle-closed\",\"callbackCompleted\":" + Bool(state.called));
    bool removalVerified = false;
    CONFIGRET finalLocate = CR_FAILURE;
    if (state.called && state.instanceFits && *state.instance) {
        const auto removalDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        do {
            const NodeState current = Observe(state.instance);
            finalLocate = current.locate;
            if (finalLocate == CR_NO_SUCH_DEVNODE) { removalVerified = true; break; }
            Sleep(200);
        } while (std::chrono::steady_clock::now() < removalDeadline);
    }
    if (!exitCode && (!observedPresent || !observedStarted || !removalVerified)) exitCode = 4;
    if (!exitCode && interrupted) exitCode = 130;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begun).count();
    Emit("\"event\":\"summary\",\"exitCode\":" + std::to_string(exitCode) +
        ",\"instanceId\":" + Quote(state.instance) + ",\"presentObserved\":" + Bool(observedPresent) +
        ",\"startedObserved\":" + Bool(observedStarted) + ",\"removalVerified\":" + Bool(removalVerified) +
        ",\"finalLocateResult\":" + Number(finalLocate) + ",\"interrupted\":" + Bool(interrupted) +
        ",\"elapsedMilliseconds\":" + std::to_string(elapsed) +
        ",\"driverPackageUninstalled\":false,\"physicalSeatCreated\":false,\"remoteSessionDriver\":false,\"frameCaptureVerified\":false");
    return exitCode;
}
}
int wmain(int argc, wchar_t** argv) {
    try { return Run(argc, argv); }
    catch (...) { Emit("\"event\":\"failed\",\"reason\":\"unexpected-exception\",\"cleanupPresenceNotVerified\":true"); return 5; }
}


