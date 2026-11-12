# Console indirect-display experiment results

**A bounded console virtual-display lifecycle and acquired-buffer evidence were verified in the disposable Windows Server guest. No new user session or physical seat was created.** This experiment is separate from the [RDS provider/listener activation](../../docs/ACTIVATION-TRACE.md); the two observations do not prove an integrated remote-session display path.

## Environment and source

The experiment used the isolated QEMU Windows Server 2022 guest, build 20348, and a guest-only signed package compiled from the [reference source](README.md). The driver derives from [Microsoft IddSample at the pinned revision](https://github.com/microsoft/Windows-driver-samples/tree/d49bb7d72f3651952b2b6e4ad28d14fe2247eb69/video/IndirectDisplay). [Driver.cpp](Driver.cpp) contains the lifecycle and buffer-counter instrumentation; [SwDeviceHolder.cpp](SwDeviceHolder.cpp) contains the bounded device-lifetime helper. The advertised mode is 1280 × 720 at 60 Hz; that is a mode declaration, not a measured frame rate.

The source files, source-manifest hashes and source licenses were not changed to report these results. No host display/input assignments were changed, and no raw machine logs, credentials, private identifiers or signing material are published here.

## Direct observations

| Evidence | Observed result | Limit |
| --- | --- | --- |
| Device holder | Ran for its 120-second holding interval, exited with code zero and reported a verified lifecycle. Creation, started/present state, handle closure and disappearance were observed. | A device lifecycle alone does not establish frames or session ownership. |
| Driver trace | 22 events for `RETSAR.Lab.IddReference`, all from one PID; ETW reported `EventsLost=0` and `BuffersLost=0`. | Zero reported loss does not supply events that were never instrumented. |
| Initialization | Device, adapter and monitor-arrival events, mode handling and successful D3D initialization were observed. | No physical GPU-output ownership or hardware-acceleration claim follows. |
| Swapchains | Two assignments; the first set-device call failed and the second succeeded. | The recovered error remains part of the result. |
| Acquired buffers | One `FramesAcquired` event reported `Count=1`; `DriverUnload` reported a final total of three acquired buffers. | The code emits a frame event for the first buffer and every sixtieth buffer, so one such event is not a claim that only one buffer was acquired. No pixel contents were exported or compared. |

The counter is incremented after successful `IddCxSwapChainReleaseAndAcquireBuffer` calls in [the driver source](Driver.cpp). Microsoft describes IDDs as user-mode drivers that report monitors/modes and process desktop images; that model does not by itself allocate independent login sessions. [IDD overview](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/indirect-display-driver-model-overview).

## Retained errors and timing

| Trace offset | Observation |
| --- | --- |
| +0.247 seconds | The first `SwapchainSetDevice` returned `0x887A0026`. |
| +0.250 seconds | A second swapchain assignment reached `SwapchainSetDevice=S_OK`. |
| +119.691 seconds | `AcquireStopped` returned `0x887A0026`. |
| About 11 milliseconds later | `DriverUnload` reported three acquired buffers and two assigned swapchains. |

Microsoft names `0x887A0026` **DXGI_ERROR_ACCESS_LOST**. It is distinct from `DXGI_ERROR_ACCESS_DENIED` (`0x887A002B`); the code alone does not establish the cause in this IDD run. [Microsoft DXGI error reference](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-error).

The initial failure was followed by a successful assignment. The final failure and unload occurred near the bounded helper's teardown interval, which is compatible with closing the device. However, there is no timestamped holder-close event to correlate with the driver trace. Therefore teardown causality, harmlessness and prolonged rendering stability remain unproven.

## Cleanup verification and limits

Receipt-based final cleanup reported no failure, verified removal of the owned driver package, device and certificate, and restored the guest boot setting to its recorded baseline. Evidence was retained. **Independent verification after a cold boot passed**, with a confirmed new boot and no verification failure. The owned certificates, driver package, software devices and provider registration were absent, and the trace was stopped. The test-signing setting was absent both in the original baseline and in the verified current configuration. These checks establish restoration after reboot; they do not broaden the rendering or isolation evidence above.

This run demonstrated acquired-buffer counters, not changing pixel content, an exported picture, sustained frame rate or application compatibility. It did not establish remote-session display binding, a separate user logon, physical monitor ownership or independent keyboard/mouse input. A remote IDD additionally requires a device instantiated by the OS remote desktop stack for that session; this console experiment does not satisfy that prerequisite. [Remote IDD requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/iddcx1.4-updates-for-remote-idds).
