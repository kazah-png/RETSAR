# Physical input routing laboratory

This directory contains an original allocation-free routing policy and an **inert KMDF class-service filter adapter**. The policy passes synthetic tests; the adapter can be compiled and linked as an unsigned x64 driver. Neither result demonstrates physical input isolation, input delivered to a child session, device assignment, or a usable second workplace.

The adapter forwards original keyboard and mouse packet ranges, including the original consumed-count pointer, unchanged to the existing class callback. It has no device enrollment, control interface, activation command, installer, INF, broker, event log, or input redirection. Setting `RETSAR_ENABLE_PHYSICAL_ROUTING=1` deliberately fails compilation. Do not install this source experiment on a working host, especially one with only one keyboard and mouse.

## Policy and recovery

`physical_input_router.hpp` is portable C++17 and uses fixed storage. One router represents one exact PnP device incarnation. A lease contains the child session, child-process and broker-process incarnations, a monotonically increasing epoch, and a deadline of at most two seconds. The caller must serialize all operations and provide one monotonic clock plus an independent timer.

Admission requires a trustworthy per-device neutral observation, no held inputs, no unacknowledged target reset, and a newer epoch. Silence is not proof that all physical keys are released. Authentication, spare-device checks, session relationship validation, and the neutral observation are external requirements; the policy's identity fields and boolean admission argument do not supply those proofs themselves.

Expiry, queue overflow, disconnect, or unsupported input revokes the lease and clears the bounded memory queue. New input returns to the host. A key or mouse button already owned by the target remains suppressed through repeats and its physical release, preventing the second half of its pair from leaking to the host. Reconnection requires an explicit target reset acknowledgement and released inputs. Removal is terminal: delayed callbacks from the removed incarnation are discarded; a new arrival needs a new router.

The reset acknowledgement is a delivery barrier, not merely a received message. A future broker must discard or finish old in-flight packets before resetting target-held input and acknowledging that epoch. Its child worker also needs a local watchdog so a dead broker cannot leave keys held indefinitely. Input contents must remain in bounded volatile queues and must not be written to receipts, diagnostics, or disk. The current policy implements no operating-system input delivery.

## What remains before activation

- Validate a separate physical keyboard and mouse while preserving a usable main pair. A single pair supports switching, not simultaneous independent physical control.
- Establish authenticated broker and child-worker endpoints, ordered epoch cancellation and reset barriers, independent watchdogs, and bounded delivery acknowledgements.
- Validate the correct per-device USB/HID filter position and PnP/power/removal lifecycle in a disposable lab. Do not apply a global keyboard or mouse class filter to the host.
- Implement and test class-service consumed-prefix accounting, mixed mouse packets, absolute motion, wheel data, extended scan codes and unusual make/break sequences. Unsupported packets must remain on a safe pass-through path; the portable event model is not a complete Windows packet decoder.
- Validate target delivery independently. Raw Input distinguishes devices but does not prevent another application receiving legacy input. `SendInput` is synthetic and subject to integrity restrictions. VHF alone does not document binding a virtual device to an arbitrary existing client child session.
- Complete driver verification, Secure Boot/HVCI-compatible signing and packaging before considering a real installation. The open-source license is separate from Windows driver signing and certification requirements.

## Build without installing

Use an existing licensed MSVC x64 toolchain and the WDK/SDK packages; no download, install, signing, driver load, service creation, registry edit, boot-setting change, or input action is performed. The checked layout uses MSVC `14.44.35207`, WDK/SDK package `10.0.26100.6584` (headers `10.0.26100.0`) and KMDF `1.35`.

```powershell
python src/lab/input-routing/build.py `
  --msvc-root "$env:MSVC_ROOT" `
  --wdk-root "$env:WDK_ROOT" `
  --sdk-root "$env:SDK_ROOT" `
  --sdk-x64-root "$env:SDK_X64_ROOT" `
  --out "$env:INPUT_LAB_OUTPUT"
```

`MSVC_ROOT` contains `bin/Hostx64/x64/cl.exe`; `WDK_ROOT` is the WDK package's `c` directory; `SDK_ROOT` is the SDK CPP package's `c` directory; `SDK_X64_ROOT` is the SDK CPP x64 package's `c` directory. The output must be a new directory outside this source directory with an existing parent. Reusing an output directory is rejected before any build command runs.

The script compiles and runs only synthetic policy tests, compiles the inert adapter, verifies that enabling routing fails, links an unsigned `.sys`, and writes local logs plus a source-hashed build report. The report explicitly keeps runtime testing and physical exclusivity false. Kernel vendor-header warnings are treated as external; project source is compiled with `/W4 /WX`.

## API references

The implementation is original RETSAR code. Microsoft examples informed the class-service callback boundary; no bundled PS/2 installation manifest is reused.

- [Raw Input flags and their application scope](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawinputdevice)
- [Microsoft keyboard filter example](https://github.com/microsoft/Windows-driver-samples/tree/main/input/kbfiltr) and [mouse filter example](https://github.com/microsoft/Windows-driver-samples/tree/main/input/moufiltr)
- [Windows keyboard and mouse HID stack](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/keyboard-and-mouse-hid-client-drivers)
- [SendInput behavior and restrictions](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)
- [VHF configuration contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/vhf/ns-vhf-_vhf_config)
- [RDS input-handle contract and server-only support](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nf-wtsprotocol-iwrdsprotocolconnection-getinputhandles)
- [Windows driver signing policy](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/kernel-mode-code-signing-policy--windows-vista-and-later-)