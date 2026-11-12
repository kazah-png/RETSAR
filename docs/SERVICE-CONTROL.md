# Owned RDS service control

The optional control channel lets an authenticated local laboratory operator request one connection attempt through the listener that TermService actually created. It separates pipe handling from outbound RDS calls. Enabling the channel or taking a snapshot does not start a connection.

This document describes the implemented contract and its inert validation. The earlier [service activation observation](ACTIVATION-TRACE.md#verified-service-activation-after-the-dll-permission-correction) establishes DLL loading and listener startup. It does not validate this new channel. No real connection, licensing exchange, user session, remote display, independent input or physical seat is established by the control tests below.

## Build and validation

From the repository root, with Visual Studio C++ Build Tools and a complete x64 Windows SDK:

```powershell
.\build.ps1 -RdsLab -RdsTests
```

`-RdsTests` requires `-RdsLab`. The optional build produces `dist/lab/RETSAR-RdsBridge.dll` and `dist/lab/RETSAR-rds-control.exe`, checks controller help, and runs the laboratory contract suites. It does not register the provider, open a live control pipe, restart a service or create a session. The normal `-Test` suites remain separately selectable.

| Validation | Observed scope |
| --- | --- |
| 31 control-protocol checks | Fixed packet sizes, allowed commands, run/operation validation, rejection cases and declared client access rights. No live pipe or token impersonation. |
| 54 worker checks | Real MTA worker threads against mock COM control; explicit admission, idempotency, original failure results, continuation, bounded event storage, nonblocking stop and retained references. |
| Separate 125 bridge checks | In-process mock RDS callback contract. These are not direct service-control or real session tests. |

The channel and worker tests passed with MSVC `/W4 /WX`. The worker fixture is a real test DLL loaded by a separate executable: releasing the executable's library reference while a mock callback is blocked leaves the DLL loaded, and the final worker exit releases its module pin. This validates code lifetime with a mock callback, not the duration or completion of a Windows service callback. The broader build also runs separate licensing and remote-display contract suites; those do not supply a live licensing or display transport.

## Prerequisites and explicit enablement

The implementation accepts only the small disposable laboratory guest: 64-bit `RETSARLAB`, QEMU manufacturer, Windows Server product type and build `20348`, one or two logical processors, and at most 5 GiB of physical memory. These checks prevent accidental use on the ordinary workstation; they are not a security boundary against a machine administrator who controls the guest.

The server additionally requires NetworkService in Session0 and verifies that its process is the service process reported for TermService. The controller requires an elevated local administrator or SYSTEM. AppContainer tokens are rejected. A hostname alone does not enable either role.

Use the guarded [activation helper](../lab/guest-provider-activation.ps1) to prepare a fresh, reviewed experiment. Only `Prepare -EnableConnectionControl` writes the opt-in; `Collect` and `Cleanup` reject that switch. Preparation retains the existing approval, authenticated caller/session baseline, independently checked DLL hash, exact owned registration and cleanup safeguards. Older receipts without the new Boolean flag remain activation-only.

The owned listener key is `HKLM\SYSTEM\CurrentControlSet\Control\Terminal Server\WinStations\RETSAR-Lab`. When control is enabled, it has exactly these values and no child keys:

| Value | Type and required content |
| --- | --- |
| `fEnableWinStation` | DWORD `1` |
| `LoadableProtocol_Object` | String `{E09044AF-A721-4D68-9F49-4527E9E51376}` |
| `RETSARRunId` | String containing the receipt's nonzero, 32-character lowercase hexadecimal run ID |
| `RETSARControlEnabled` | DWORD `1` |

The matching project CLSID key must carry the same run ID. Its only child, `InprocServer32`, identifies the fixed guest DLL path `C:\RETSAR-Lab\provider-activation\RETSAR-RdsBridge.dll` and threading model `Free`. The loaded module's path must agree. The transport checks this ownership shape when it starts and again after authenticating each accepted request. Missing opt-in leaves the ordinary activation experiment without a control endpoint; malformed or changed registration fails closed. The channel does not write registry values itself.

## Pipe and authentication contract

The local endpoint is `\\.\pipe\RETSAR-RdsLab-<run-id>`. The run ID correlates an experiment; it is not a password or an authentication token.

The server creates one persistent message-mode pipe instance with `FILE_FLAG_FIRST_PIPE_INSTANCE`, `PIPE_REJECT_REMOTE_CLIENTS` and overlapped I/O. Its explicit protected DACL grants only SYSTEM and Administrators the individual client rights `0x00120183`. The client requests those exact rights, not `GENERIC_WRITE`: generic write also includes the right to create another pipe instance. [Microsoft pipe access rights](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights).

After reading one complete bounded request, the server impersonates that pipe client, queries its token, requires sufficient impersonation level and an elevated administrator or SYSTEM identity, and rejects AppContainer identity. It must successfully remove impersonation before dispatching the request. Failed impersonation or reversion never authorizes work. Authentication applies to the last message actually read, following the [named-pipe impersonation contract](https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-impersonatenamedpipeclient).

The controller checks `GetNamedPipeServerProcessId` against the currently running TermService PID before sending its request. One connection carries one request and one response. The server keeps the pipe instance while disconnecting clients so another process cannot claim the name between requests.

The fixed Windows x64 protocol carries a 64-byte request and a 3,664-byte response. It validates magic, version, size, command, run ID, reserved field and required operation ID. It accepts no variable-length command, executable, path, handle, pointer, credential, serialized COM object or arbitrary payload. Response padding and unused event slots are cleared before transmission.

## Commands and interpretation

Run the controller only inside the prepared guest. `--help` is inert. Each operational command takes the current receipt's run ID:

```text
RETSAR-rds-control.exe --snapshot <run-id>
RETSAR-rds-control.exe --start <run-id>
RETSAR-rds-control.exe --continue <run-id>
RETSAR-rds-control.exe --cancel <run-id>
```

The supplied controller uses fixed operation ID `1`. The worker admits one operation for that listener lifetime. Repeating `Start` with the same ID returns its existing state; a different ID cannot replace or cancel it. Continue or Cancel before admission is rejected. Snapshot only observes state and never admits a connection.

The pipe handler only enqueues commands or copies a bounded snapshot. A separate COM MTA worker invokes the retained `IRetsarRdsLabControl`. After an explicit Start, a pending attempt is polled on that worker; no notification callback runs a continuation. The bridge permits `OnReady` only after observing `GetLogonErrorRedirector` and retaining the returned connection callback, consistent with [Microsoft's ordering requirement](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nf-wtsprotocol-iwrdsprotocolconnectioncallback-onready). It does not fabricate a licensing result or a session identity to move forward.

A successful controller process exit means a valid response was received and printed. It is not connection success. Inspect `state`, `result`, the separate original HRESULTs for OnConnected/GetConnectionId/OnReady, their acceptance fields, and cancellation results. `E_PENDING` denotes an unfinished step. A queued cancellation flag does not establish that `BrokenConnection` has run or succeeded.

The JSON report contains operation/state information, HRESULTs, connection IDs and a bounded event list. When a session identity is observed, it retains both numeric ID and GUID; neither observation proves creation or ownership. `sessionOwnershipProven` remains false. No account name, password, authentication token, RDS settings payload or connection payload is carried by this interface. Diagnostic identifiers and ETW files still require review before publication.

## Lifetime, stop and failure behavior

The transport has a fixed 180-second lifetime. Each pipe I/O wait is at most five seconds, with a further one-second cancellation-completion wait. It never uses an unbounded client-dependent flush. The dedicated worker has a fixed 120-second lease and releases its retained COM reference on its MTA thread after finishing cooperative cleanup.

These are admission and cooperative cleanup limits, not a hard timeout on an RDS callback. A callback that is already executing may outlive the lease. Stop signals do not join a worker, terminate its thread or report that callback as completed. A Cancel received while that worker is inside a callback is queued until it returns.

`StopListen` revokes listener admission, requests both workers to stop and detaches their owning references without waiting for an outbound RDS call. For an owned service-control attempt, the bridge may retain the accepted connection callback until the MTA worker can request `BrokenConnection`; it releases that reference outside its state lock. A late return cannot resurrect listener admission or start a new attempt. Cancellation remains a connection request, not proof of session destruction.

Both worker threads pin their containing module. Normal exit destroys thread-owned C++ state before `FreeLibraryAndExitThread`, preventing code unloading during the thread epilogue. If the transport cannot prove impersonation reversion or completion of canceled I/O, it dispatches no further command and retains its single bounded context/module pin with an error result. It does not terminate shared `svchost`, free buffers that the kernel may still use, or claim successful cleanup. The separate controller may terminate only itself on its own unresolved I/O cancellation.

Event storage is bounded to 64 records. Provider-reported cumulative loss and local snapshot overflow are tracked separately and added; empty drains do not recount either. Missing events or nonzero loss constrain what can be concluded from a snapshot.

## Laboratory sequence and remaining evidence

1. Build and review the DLL/controller; prepare a fresh owned guest experiment with control explicitly enabled and the reviewed DLL hash.
2. Observe the live service PID/module, activation trace and listener baseline. Read a snapshot before requesting work.
3. Send one explicit Start, then retain snapshots, actual HRESULTs and trace records. Stop at an unresolved protocol contract rather than substituting a successful placeholder.
4. Request cancellation, collect the owned evidence and use receipt-checked cleanup. Preserve the standard RDP listener; require actual module absence before removing the owned DLL. An unresolved reference or trace is reported and retained.


## Observed service request

An explicitly opted-in Server 20348 guest run reached the real TermService listener. A pre-start snapshot was idle, operation ID zero, with no connection request. The authenticated Start then ran on the separate MTA worker. `OnConnected` returned **`0x80070057` (`E_INVALIDARG`)**. No connection callback, connection ID, `OnReady`, `NotifySessionId` or logon was observed. Cancellation of this rejected attempt returned `S_OK`; it did not demonstrate destruction of a created session.

The boot capture contained 34 provider events from the observed TermService process, including five failed connection-interface queries. Requested IIDs were not recorded in that first binary, so those failures cannot yet identify the cause of rejection. The updated diagnostic records the requested interface GUID without credentials or account data.

The initial short service-restart capture ended before the provider loaded: its controller saw Win32 error 2 and the trace contained no provider events. A later snapshot reached the authenticated endpoint after the worker lease had expired. This is evidence of delayed initialization, not a permanent loading failure. The subsequent cold-start capture had zero events or buffers reported lost.

After collection, the exact owned boot logger, provider registrations and DLL were removed; module absence and the unchanged standard RDP listener were checked. Private raw traces remain outside the repository. These observations establish service-control delivery and a real rejected request, not a working native seat.

## Interface-identification repeat

A second service-restart run waited for actual endpoint readiness instead of ending after a short fixed delay. It captured 39 provider events from the observed service PID and repeated the same OnConnected rejection. The requested interfaces, in order, were:

| Requested IID | Identified contract |
| --- | --- |
| `753E526F-8470-45B0-BC2E-8F7FC057A351` | Not identified in the reviewed public SDK |
| `E32B099D-E76E-4FC5-861D-EE9BC1715E19` | Not identified in the reviewed public SDK |
| `1382DF4D-A289-43D1-A184-144726F9AF90` | `IWRdsWddmIddProps` |
| `5718AE9B-47F2-499F-B634-D8175BD51131` | `IWRdsEnhancedFastReconnectArbitrator` |
| `0FAD5DCF-C6D3-423C-B097-163D6A676151` | Not identified in the reviewed public SDK |

All returned E_NOINTERFACE. The provider does not yet expose its standalone remote-display contract. The trace establishes that Windows queried it, not that it alone caused E_INVALIDARG. COM interface probes can be optional; a successful empty implementation would not supply a remote driver. Exact registration/DLL cleanup was again verified, with the standard RDP listener unchanged and only the original console/user-session baseline remaining.
