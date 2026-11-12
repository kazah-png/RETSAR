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

TraceLogging carries decoding metadata with its events, and modern TDH decodes that metadata without installing a manifest. See [event metadata](https://learn.microsoft.com/en-us/windows/win32/etw/event-metadata-overview) and the [tracerpt interface](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/tracerpt). Actual decoding on the selected guest must still be observed. Missing or dropped records, an empty trace, unknown fields and a rejected method are results to retain, not a reason to infer a successful stage.

A factory call only proves that the DLL's factory was called. `StartListen` returning `S_OK` only proves listener activation. A mock test process can emit the same event names; identify the service process independently. No activation trace by itself demonstrates session creation, independent display/input or replacement of another multiseat product.


## Current validation

The instrumented bridge compiled with MSVC `/W4 /WX`, and its existing in-process harness passed 125 mock checks against the DLL built on D:. Two separate argument checks confirmed rejection of a disallowed drive prefix and a UNC path before COM initialization or DLL loading. The harness now accepts explicit C: or D: paths; this prefix check is not a reparse-point or file-ownership check.

These checks validate the unchanged mock contracts and argument admission. No host trace session was enabled. They do not establish real service activation, successful ETL decoding or native session creation.

A first disposable-guest attempt registered the isolated provider and restarted the guest service. Service Control Manager reported `Running`, but LocalSessionManager subsequently reported error `0x800706BA`. The DLL was absent from the observed service process and the trace contained zero RETSAR events. The standard RDP listener's registry values remained unchanged. The ETL retained two system trace metadata records, including an undecoded partition record (`15005`); these are not provider events. Cold-boot activation and cleanup are separate pending observations. A service state alone is therefore not treated as provider readiness.