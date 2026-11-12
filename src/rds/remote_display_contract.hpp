#pragma once
#include <windows.h>
#include <wtsprotocol.h>
#include <array>
#include <string_view>

namespace seats::rds {

struct RemoteDisplayState {
    bool enabled=false;
    bool sessionBound=false;
    ULONG sessionId=0;
    bool driverHandlePresent=false;
    ULONG64 loadGeneration=0;
};

// A connection-owned interface tear-off. The owner must expose this interface
// from QueryInterface and outlive it; IUnknown identity and reference counting
// delegate to that owner. No registration, device creation or RDS call occurs.
// Configure a distinct remote-driver HWID, never the console RETSARIddLab ID.
class RemoteDisplayContract final : public IWRdsWddmIddProps {
    IUnknown& owner_;
    std::array<wchar_t,200> hardwareId_{};
    DWORD hardwareIdCharacters_=0;
    SRWLOCK lock_=SRWLOCK_INIT;
    RemoteDisplayState state_{};
    HANDLE driver_=nullptr;
    void closeDriverLocked() noexcept;
public:
    RemoteDisplayContract(IUnknown& owner,std::wstring_view remoteHardwareId) noexcept;
    ~RemoteDisplayContract();
    RemoteDisplayContract(const RemoteDisplayContract&)=delete;
    RemoteDisplayContract& operator=(const RemoteDisplayContract&)=delete;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE GetHardwareId(WCHAR* out,DWORD count) override;
    HRESULT STDMETHODCALLTYPE OnDriverLoad(ULONG sessionId,HANDLE_PTR handle) override;
    HRESULT STDMETHODCALLTYPE OnDriverUnload(ULONG sessionId) override;
    HRESULT STDMETHODCALLTYPE EnableWddmIdd(BOOL enabled) override;

    // Call only with the actual NotifySessionId value. A number alone does not
    // establish a newly created session, input ownership or a usable display.
    HRESULT bindSession(ULONG sessionId) noexcept;
    void disconnect() noexcept;
    RemoteDisplayState snapshot() noexcept;
    // Worker receives its own duplicate, so unload/disconnect cannot race a
    // borrowed handle. The worker must CloseHandle it. No IOCTL is issued here.
    HRESULT duplicateDriverHandle(ULONG sessionId,HANDLE* out) noexcept;
};

// Only negative checks: even valid, distinct HANDLEs are not proof of keyboard/
// mouse driver identity or per-session routing. Never translate the final enum
// value into GetInputHandles=S_OK without a real, verified input-device backend.
enum class InputHandlePrerequisite {
    MissingKeyboard,MissingMouse,BeepMustBeNull,AliasedDevices,
    InvalidKeyboard,InvalidMouse,DeviceAndSessionBindingUnverified
};
InputHandlePrerequisite inspectInputHandlePrerequisites(
    HANDLE_PTR keyboard,HANDLE_PTR mouse,HANDLE_PTR beep) noexcept;

} // namespace seats::rds
