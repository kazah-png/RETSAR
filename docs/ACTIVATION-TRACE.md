# RDS provider activation trace

The optional RDS bridge emits diagnostic ETW events when an external trace controller enables its provider. The trace identifies which methods were entered and which HRESULTs were returned. It does not activate a listener, request a connection, prove session ownership or implement a physical seat.

## Provider and event format

| Item | Value |
| --- | --- |
| Provider name | `RETSAR.RdsActivation` |
| Provider GUID | `{77E2B2C5-62F8-4C0C-AB1F-94C6D49173E1}` |
| Encoding | Self-describing TraceLogging; no installed manifest |
| Event name | `Activation` |
| Level | `4` (information) |
| Keyword | `0x1` |
| `Method` | Fixed method name from the bridge |
| `Phase` | `enter`, `return` or `event` |
| `HResult` | Unsigned 32-bit representation of the original HRESULT on `return` |

The ETW header supplies process and thread identity. A phase of `enter` means the call began; its zero-valued `HResult` is only an unused field, not success. `event` marks process attach/detach and likewise has no method result. Only a `return` record reports the method's result. For example, `0x80004001` is the unsigned representation of `E_NOTIMPL`, not a successful operation.

The instrumented surface includes the DLL factory exports, factory interface negotiation/creation/locks, manager activation and notifications, and listener interface negotiation/settings/start/stop. AddRef/Release, connection traffic and the project's explicit experiment control are not traced by this activation channel. The existing in-memory connection-event queue remains separate.

All payload strings are fixed source literals. No names of users or listeners, credentials, tokens, security descriptors, configuration contents, device paths or connection payloads are included. ETL headers still contain diagnostic context such as process/thread identifiers and timing; traces must be reviewed before publication.

## Lifetime and behavior

The DLL registers its module-local TraceLogging provider on `DLL_PROCESS_ATTACH` and unregisters it on `DLL_PROCESS_DETACH`. This follows the documented [TraceLogging registration lifecycle](https://learn.microsoft.com/en-us/windows/win32/api/traceloggingprovider/nf-traceloggingprovider-traceloggingregister). Registration is process-local; it is not provider installation, a trace session, a service start or a registry change.

Tracing errors are ignored. The helper preserves `GetLastError`, returns each original HRESULT unchanged and does not change output parameters, admission, ownership or policy. Call bodies retain their existing exception boundaries. An unexpected exception or process termination can leave an entry without a return; that is not evidence of success.

The instrumentation creates no worker, listener, pipe, network connection or file. It makes no COM or RDS call from `DllMain`, and never starts `OnConnected` from `StartListen`. It does not change the deliberately unimplemented licensing, graphics or input methods.

## Collection and interpretation

Collection belongs in an explicitly prepared disposable Server laboratory. The separate guest activation workflow is responsible for bounded trace capture, provider registration and guarded cleanup. The ordinary build and mock tests do not start or stop a trace session, register the COM provider or contact RDS. Enabling a capture is a separate action from loading the diagnostic DLL.

Use an ETW controller such as inbox `logman` with the fixed GUID, level and keyword above. Stop the capture before decoding the ETL with inbox `tracerpt` to XML. The collector must preserve command exit codes, the original ETL and any unrecognized events. A successfully generated XML file is insufficient: verify the provider GUID, event fields and expected service process before interpreting records.

TraceLogging carries decoding metadata with its events, and modern TDH decodes that metadata without installing a manifest. See [event metadata](https://learn.microsoft.com/en-us/windows/win32/etw/event-metadata-overview) and the [tracerpt interface](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/tracerpt). A separate manual COM control on the selected guest produced 31 decoded RETSAR events; that validates decoding for the control process, not service activation. Missing or dropped records, an empty trace, unknown fields and a rejected method are results to retain, not a reason to infer a successful stage.

A factory call only proves that the DLL's factory was called. `StartListen` returning `S_OK` only proves listener activation. A mock test process can emit the same event names; identify the service process independently. No activation trace by itself demonstrates session creation, independent display/input or replacement of another multiseat product.


## Guest experiment workflow

The [activation helper](../lab/guest-provider-activation.ps1) and [boot-trace helper](../lab/guest-provider-boot-trace.ps1) run only in the small, disposable QEMU Server guest. They require explicit approval, administrator identity and either its console or explicitly enabled authenticated PowerShell management. Session0 management may operate with a Connected console only when a bounded WTS identity query proves that console has no user; a failed query is not an empty account.

1. Stage the reviewed DLL in the helper's fixed guest directory. Activation `Prepare` verifies its hash, the session baseline and the absence of the project's registry keys. It records ownership, registers only the project's CLSID/listener, starts a bounded trace and restarts only the guest service with explicit opt-in.
2. Activation `Collect` stops the owned trace and retains the original ETL, decoded XML and service-process/module evidence. The service reporting Running does not establish protocol readiness.
3. If a cold-boot experiment is needed, boot-trace `Arm` requires the collected activation receipt and matching registration/DLL. It creates one owned AutoLogger with a separate session GUID, the single RETSAR provider and a 16 MiB circular file. `Start=1` is the final registry change. The helper performs no reboot.
4. After one separately authorized guest reboot, boot-trace `Collect` validates ownership and writes `Start=0` before stopping or decoding. This prevents another boot from overwriting the capture. It verifies the running trace's GUID, name, output path, mode and size limit through ControlTrace.
5. Boot-trace `Cleanup` removes only its verified AutoLogger. Activation `Cleanup` then removes only the owned provider registration, restarts the guest service and requires module absence before removing the DLL. Receipts and trace evidence remain available.

The boot trace follows Microsoft's [AutoLogger configuration contract](https://learn.microsoft.com/en-us/windows/win32/etw/configuring-and-starting-an-autologger-session), without early-kernel tracing or additional providers. Windows added a DWORD `Status` to both the logger and its provider key; the ownership validator permits those result fields while rejecting unrelated values. Both statuses are retained separately. The live query also returned circular mode with `EVENT_TRACE_PERSIST_ON_HYBRID_SHUTDOWN` (`0x00800002`). ETW documents automatically selecting a hybrid-shutdown default; the validator accepts only circular mode alone or with exactly one of the documented stop/persist defaults, preserving the observed mode. [Logging modes](https://learn.microsoft.com/en-us/windows/win32/etw/logging-mode-constants).

## Observed activation results

**Service activation and listener startup are now verified in the disposable Server guest. No client connection, licensing exchange, user session or physical seat was created by this experiment.**

| Experiment | Observed result |
| --- | --- |
| Minimal owned registration, guest service restart | Service Control Manager reported Running, but LocalSessionManager event 17 reported `0x800706BA`. The provider DLL was absent from the observed service process. Zero RETSAR events. |
| Minimal registration, cold boot | Zero RETSAR events; service activation was not demonstrated. |
| Own listener with `fEnableWinStation=1`, guest service restart | Zero RETSAR events; the additional listener flag did not demonstrate activation. |
| Own enabled listener, clean power-off and new guest launch | Zero RETSAR events; service activation was not demonstrated in this attempt. |
| Own enabled listener, explicit DLL read/execute permission, guest service restart | 21 RETSAR events, all from the independently identified TermService process; manager initialization, listener creation and `StartListen` returned `S_OK`. |

Each of the first four traces contained two system metadata records and no RETSAR event, with `EventsLost=0`. The retained metadata included the undecoded partition record `15005`. Those records are not provider calls. Successful AutoLogger startup statuses likewise prove neither DLL loading nor manager/listener initialization.

A separate, explicitly invoked COM control constructed the manager and produced **31 decoded RETSAR events**, all from its own control process rather than the independently identified TermService process. This establishes that COM activation and trace decoding work in that control context. It does not convert the four service traces into successful activation evidence.

The enabled-listener experiment adds only `fEnableWinStation` DWORD `1` to the project's key. Microsoft documents this prerequisite for RDP-Tcp; applying it to the custom listener is an engineering hypothesis, not a documented guarantee. No other listener defaults or security policies are copied. [Microsoft listener troubleshooting](https://learn.microsoft.com/en-us/troubleshoot/windows-server/remote/remote-desktop-cannot-connect-remote-computer).

The first two attempts completed verified cleanup: their owned traces were stopped, AutoLogger and provider keys were removed, and the provider DLLs were removed. The standard RDP listener's registry values remained unchanged. Their receipts and captured evidence were retained. The third attempt also completed verified cleanup: its owned provider registration and DLL were removed, the standard RDP listener remained unchanged, and captured evidence was retained. No boot trace was armed for that attempt. No host provider, driver, service or physical device was changed.

## Verified service activation after the DLL permission correction

The staged DLL's observed DACL initially granted full control only to SYSTEM, Administrators and the local Administrator; it lacked the NetworkService read/execute grant. The helper now applies a protected descriptor directly to the hash-verified DLL: SYSTEM and Administrators retain full control, and NetworkService receives read/execute. The existing directory grant alone did not supply those rights to the staged file. The change preserves the DLL's bytes and the listener registration.

After this correction, the third service-restart capture contained **21 decoded RETSAR events, all from TermService's independently identified PID**. The collector also observed the provider module in that process. The trace includes `DLL.ProcessAttach`, `DllGetClassObject`, factory creation, `Manager.Initialize`, `Manager.CreateListener` and `Listener.StartListen`; the returned methods succeeded with `S_OK`. WTS showed a new listener row and no added interactive user session.

`Listener.GetSettings` returned `E_NOTIMPL` (`0x80004001`), and the service continued to successful `StartListen`. `Manager.NotifyServiceStateChange` later returned `E_NOTIMPL`. Neither return is relabelled as success, but the observed continuation rules out treating `GetSettings` alone as a startup blocker in this run. The [official listener sample](https://github.com/Azure/Remote-Desktop-Services-Protocol-Sample/blob/main/Sample/TestProtocol_Ext/WRdsProtocolListener.cpp) also leaves this optional method unimplemented. This milestone proves activation and the supplied listener callback, not connection handling, licensing or a usable seat.

## Hypervisor outcome

An in-guest restart under WHPX encountered virtual-processor exit 4 and QMP `guest-panic`, even though the QEMU process exited with code zero. This was **not a clean guest shutdown**. A subsequent read-only qcow2 check found no structural errors; it did not verify guest filesystem integrity. Clean guest power-off followed by a new supervised launch subsequently worked. The reset failure remains an observed limitation, not a repaired or harmless condition.

## Validation scope

The instrumented bridge compiled with MSVC `/W4 /WX`, and its in-process harness passed 125 mock checks. Two separate argument checks rejected a disallowed drive prefix and a UNC path before COM initialization or DLL loading. Those prefix checks do not establish path ownership or reject every reparse point.

The current local helper suites passed **73 activation checks and 66 boot-trace checks**, covering pure admission/ownership logic, in-memory DLL security descriptors, registry-shape consistency, native structure layout and synthetic buffers. They do not invoke WTS, ControlTrace, registry mutations or service operations. The live experiments above are separate evidence, not additional automated passes. See the [revision-specific CI results](PROGRESS.md).

The next integration step is an authenticated, bounded trigger reaching the service-created listener, followed by real connection and licensing contracts. That trigger is not implemented by this milestone. Session creation, physical display assignment, keyboard/mouse isolation and Windows Home compatibility remain unvalidated.
