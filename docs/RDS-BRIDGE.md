# Native RDS bridge: SDK integration, not a working seat provider

`src/rds/native_rds_bridge.cpp` is an original implementation of a small COM bridge against the Windows SDK's `wtsprotocol.h`. It constructs real SDK interface objects and invokes the supplied SDK callbacks. The automated harness supplies **MOCK callbacks in the same process**. No project test has connected this DLL to the Remote Desktop Services service or created a Windows session.

The bridge remains separate from `WindowsBlockedExecutor`. Compiling it, obtaining a session notification, or receiving `S_OK` from a mock does not enable a native capability or satisfy P0.

## Building and testing

```powershell
.\build.ps1 -RdsLab
.\build.ps1 -Test -Lab -RdsLab
```

The optional flag builds `dist/lab/RETSAR-RdsBridge.dll`. Combined with `-Test`, it builds and runs `RETSAR-rds-bridge-tests.exe`, which loads that explicit DLL, resolves its factory exports, and supplies in-process mock objects. The harness does not use COM registration or `CoCreateInstance`, contact RDS, inspect active sessions, or install a device. `-Lab` continues to invoke only the seat probe's inert help.

There are exactly two explicit DLL exports: `DllGetClassObject` and `DllCanUnloadNow`. There is no self-registration entry point or registration script. Factory creation alone does not start an experiment.

## Implemented interfaces and entry points

| Object | SDK interfaces | Implemented responsibility |
| --- | --- | --- |
| Factory | `IClassFactory` | Class selection, interface negotiation, reference lifetime, aggregation rejection, server locks. |
| Manager | `IWRdsProtocolManager` | Retains the optional settings interface when present, creates listener objects, and releases that interface on replacement or uninitialization. |
| Listener | `IWRdsProtocolListener` | Retains the callback delivered to `StartListen`; stops accepting work on `StopListen`. It does not connect automatically. |
| Connection | `IWRdsProtocolConnection` | Supplies limited protocol metadata; receives and queues session/connection notifications. Unsupported transports fail explicitly. |
| Laboratory control | Project-specific `IRetsarRdsLabControl` | Explicit one-attempt trigger, delayed continuation, connection cancellation request, bounded event drain. |

RETSAR's class identifier is `{E09044AF-A721-4D68-9F49-4527E9E51376}`. Its laboratory interface identifier is `{DB0368DF-9B48-4B88-96FB-84F02AE5137B}`. SDK interfaces use their SDK identifiers; the bridge does not reuse another provider's class.

The laboratory interface is an **in-process contract**, not an IPC endpoint or an authorization boundary. A future service-side trigger must reach the particular listener instance receiving the RDS callback and must have its own authenticated admission and resource accounting. This bridge supplies neither that trigger nor an external broker. Instantiating a second manager in a test process would not reach the service's listener.

`StartListen`, start, continuation, and explicit cancellation require an initialized process MTA. SDK callbacks retain their normal COM ownership contract. The bridge does not marshal callbacks across apartments.

`Initialize` accepts a null optional `IWRdsProtocolSettings` pointer and requires a non-null `WRDS_SETTINGS` pointer, matching the SDK annotations. Rejecting the required pointer leaves any previously retained interface unchanged. The method does not yet interpret or consume service policy from the structure; its successful pointer/reference handling is not complete service initialization.

## Handshake and ordering

1. The supplied host calls `CreateListener` and `StartListen` with its callback. The bridge records listening and performs no outgoing connection call.
2. An explicit caller invokes `StartExperiment(TRUE, nonzeroOperationId, ...)` outside an SDK notification. A listener admits at most one attempt, including attempts that later fail. Invalid arguments or a refused explicit-start gate do not consume that attempt. A stopped listener cannot be restarted; use a new listener object.
3. The bridge calls `OnConnected` with its connection object and level-1 settings, declaring a maximum listener connection count of one. The minimal settings contain no credentials or supplied security descriptor; their remaining zero-initialized fields have only been mock-tested and are not claimed to inherit the host's security defaults. It records the original HRESULT, retains a returned callback, and calls `GetConnectionId`. Acceptance is not treated as proof of a new session.
4. `OnReady` is gated on the service having called `GetLogonErrorRedirector`. That notification records a latch and returns `E_NOTIMPL` with a null pointer, the documented opt-out of error redirection. It never calls RDS or runs a continuation itself.
5. A synchronous notification during `OnConnected` permits the start call to continue only after the notification and `OnConnected` return. Otherwise start returns `E_PENDING`; a later explicit `ContinueExperiment` call can advance the same attempt once the notification has arrived. Repeated continuations do not duplicate `OnReady`.

Microsoft documents this ordering in [OnReady](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nf-wtsprotocol-iwrdsprotocolconnectioncallback-onready), and the error-redirection opt-out in [GetLogonErrorRedirector](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nf-wtsprotocol-iwrdsprotocolconnection-getlogonerrorredirector). The SDK's [method call sequence](https://learn.microsoft.com/en-us/windows/win32/termserv/method-call-sequence) remains the service integration contract; the mock tests are not evidence that a real service accepts this partial provider.

## Notifications, ownership and cancellation

Notification handlers copy minimal observations into a fixed 64-entry queue. A session observation contains the full `WRDS_SESSION_ID` pair of GUID and numeric ID, the operation identifier, and a sequence number. A rebinding remains a separate observation. Token handles, user names, domain strings and command-process details are not retained. The callbacks perform no file writes, registry operations, session calls or waiting for an external controller. A caller can drain observations later; overflow is reported, and the cumulative dropped count is preserved.

An observation is not an ownership receipt. `ExperimentOutcome.sessionOwnershipProven` is always false. No method deletes a session or calls `WTSLogoffSession`. Durable correlation, restart recovery and cleanup authority still belong to an unimplemented service integration.

One nonblocking operation gate serializes outgoing calls. Cancellation first marks the request. If an outgoing call is in progress, cancellation returns `E_PENDING`; it does not wait inside an RDS notification or call `BrokenConnection` concurrently. The active operation checks the request at safe boundaries and cannot report final success after an observed cancellation. A call already admitted may complete, after which cancellation is handled. The explicit caller can retry cancellation to obtain its retained result.

`BrokenConnection(0, 0)` is issued at most once for the retained callback. Its original failure remains a failure on repeated cancellation. `StopListen` never sends an outgoing RDS call; an active attempt receiving a callback after stop uses only its local reference to cancel it, without restoring the listener's callback. If stop released a previously accepted callback and closure was not observed, cancellation reports uncertainty rather than asserting success.

The [BrokenConnection contract](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nf-wtsprotocol-iwrdsprotocolconnectioncallback-brokenconnection) reports loss of a connection. Even a successful result does **not** prove session destruction. `Close` and `StopListen` are distinct notifications, not permission to destroy a numbered session.

## Intentionally incomplete areas

Credential exchange, licensing transport, input and video handles, authentication, client monitor topology and virtual channels are not implemented. Those outputs are initialized and their methods return `E_NOTIMPL`; the IDD property interface is not advertised and returns `E_NOINTERFACE`. There are no successful placeholder credentials, license responses, device handles or display bindings.

There is no network listener, service-side trigger, authenticated IPC, Remote IDD driver, input transport, external supervisor, native session ownership journal, or session teardown implementation. The bridge does not alter session concurrency policy, authentication policy, licensing enforcement, ASTER, graphics drivers or the host's sessions.

The SDK identifies the RDS provider interfaces as supported server interfaces; it does not establish Windows 11 workstation support. Validation must first use an explicitly prepared separate environment compatible with the documented scenario. Its licensing and authentication must remain normal. A future successful RDS session would still require independent display, input, recovery and physical-seat validation described in [NATIVE-BACKEND.md](NATIVE-BACKEND.md).

The [integration-gaps review](RDS-INTEGRATION-GAPS.md) separates early service activation from the licensing, authentication and device contracts required for a first supported session. The current bridge does not meet those contracts.

## Evidence and provenance

The harness covers factory negotiation and lifetime, explicit admission, synchronous and delayed readiness, original failure propagation, full session identity observations, queue overflow, unsupported outputs, and cancellation at paused mock callbacks on MTA threads. It also tests `StopListen` during `OnConnected` and preservation of `BrokenConnection` failure. All callback outcomes are mock outcomes, not claims about an RDS service.

The latest local `-Test -Lab -RdsLab` run compiled with `/W4 /WX` and passed **125 MOCK RDS checks**, including sixteen checks for the optional settings-interface contract and reference lifetime. Together with the existing suites it passed **242 checks**, plus independent JSON validation, with one pre-existing adversarial junction branch not exercised locally. This is contract and in-process concurrency evidence only. No service registration, native session, input transport, IDD or graphics presentation was exercised. [PROGRESS.md](PROGRESS.md) distinguishes this result from the earlier public CI revision.

Implementation is project-owned code against the public Windows SDK. No implementation from the Azure sample is copied. The sample is only a design reference: [Microsoft's source at commit `0b5a0bae`](https://github.com/Azure/Remote-Desktop-Services-Protocol-Sample/tree/0b5a0bae1cc1c2a667b5962bd7847d5181ae375f). Its incomplete transport and licensing methods are not copied as working behavior.
