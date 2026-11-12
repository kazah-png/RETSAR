# Child-session diagnostics and client experiment

Windows documents a special loopback RDP child session tied to an existing user's session. Its ActiveX client must select `ConnectToChildSession` before connecting. Only one connected child session is supported by that documented model. This is a candidate for logical session isolation, not an API that assigns a physical monitor, keyboard or mouse. [Microsoft child-session contract](https://learn.microsoft.com/en-us/windows/win32/termserv/child-sessions).

## Implemented diagnostics

Build the optional executables with `.\build.ps1 -ChildSessionLab`. The build invokes help and injected-API tests only; it does not query or change Windows sessions. The ordinary `-Test` suite also includes those inert tests.

- `RETSAR-child-session-probe --status` queries enabled state, caller/console IDs and the current user's child-session ID. It records each API's success and error independently. A failed call returning Win32 error 1168 remains unknown; it is not rewritten as an authoritative absence result.
- `RETSAR-child-session-toggle --enable-read-restore --receipt <new-private-file>` is a separate explicit experiment. It requires a disabled baseline, caller equal to the active console, successful user-session inventory with no other user present, a new private receipt and flushed intent before changing the setting. It enables, reads back, restores disabled and reads back again, including after an enable or persistence failure. It never connects a client or creates a session.

The toggle's receipt destination is deliberately limited to the existing private laboratory tree on D:. It rejects existing files and reparse ancestors, then verifies the final opened handle before writing evidence or changing state. Ancestor checks are not an atomic namespace lock: an empty CREATE_NEW file can precede the final handle check. Do not describe this diagnostic as a race-proof general file writer. A RETSAR-local mutex serializes its probes, not unrelated applications changing the same global Windows setting.

A receipt line cannot attest to completion of the very write currently producing that line. The final persisted record therefore describes rollback/readback, while a zero process exit is the additional acknowledgement that all writes succeeded. Failed or incomplete persistence remains an error; restoration is separately reported.

## Explicit client experiment

`RETSAR-child-session-client --connect-child --receipt <new-private-file>` hosts an original minimal OLE/ActiveX container. It requires the same disabled console-session baseline and single-user inventory, records flushed intent, and sets and reads back `ConnectToChildSession` as exact `VT_BOOL` true before admitting `Connect`. The server property is fixed to localhost; arguments cannot select an ordinary RDP destination. It explicitly enables and reads back per-connection CredSSP support, while retaining the independent child-mode gate. It supplies no credentials. Clipboard, drive, printer, port and smart-card redirection and automatic reconnection are disabled.

The client opens its own window on a secondary monitor when one is available. It limits initial connection observation to 60 seconds. An observed child stays open for supervised use. Connection events alone are insufficient: observation also requires a connected control, successful child-ID query, an ID distinct from the parent and absent from the baseline inventory, a new active WTS row and unchanged parent/console IDs. These are correlated observations, not exclusive ownership against another concurrent application.

Closing disconnects and releases only this control, then restores disabled state and reads it back. The user-close path queues window destruction after restoration, outside the COM cleanup stack; a failure result remains available for inspection until closed. Reentrant COM callbacks cannot release the control underneath initialization, outbound calls or cleanup. Remaining child IDs are reported; the client never calls session logoff. A zero exit additionally requires observed child identity, a verified final disabled baseline, no recorded failure and successful receipt persistence. Process termination during cleanup and unresponsive external COM calls are not a transactional rollback guarantee.

The optional build invokes only `--help` and fake-interface tests, never ActiveX activation or a connection. The gate suite passes **45 checks**, including false/wrong-type/read failures, missing enable or durable intent, cancellation before and during verification, exact one-call dispatch and retained exception information. The tests do not establish real Windows connectivity.

## Observed target-edition result

On the observed Core-edition host, build 26300, the read-only query returned disabled. `WTSGetChildSessionId` failed with error 1168. The first toggle ran without an administrator token: enabling failed with error 5, while disabled readback and restoration succeeded. This did not establish an edition limitation.

After the user accepted Windows elevation, the same reviewed toggle enabled the setting, observed true, restored disabled and observed false. It exited 0. A separate read-only check is required after any further experiment. No child client was connected and no additional session was created by either toggle.

**A successful setting change does not prove that a child desktop will connect on Home.** Microsoft's Power Automate picture-in-picture product explicitly excludes Home; that product restriction is relevant evidence, but it must not be silently transformed into the return value of this separate Windows API experiment. The separate ActiveX experiments below distinguish an initial rejection from a subsequent observed child desktop on this specific host; they do not establish support for every Home installation. [Power Automate requirements](https://learn.microsoft.com/en-us/power-automate/desktop-flows/run-desktop-flows-pip).

### First explicit ActiveX connection

On the same Core-edition host, the elevated client successfully created and configured its control, verified the child property, enabled the Windows setting and verified enabled state. The call to `Connect` returned `DISP_E_EXCEPTION` (`0x80020009`) with retained `EXCEPINFO.scode = E_INVALIDARG` (`0x80070057`). No connected or logon-complete event was observed. `ExtendedDisconnectReason` was zero; the disconnect call on the rejected control also failed. The original disabled setting was then restored and verified, with no receipt-write failure.

A separate post-test inventory showed the parent console session still active and no additional user session in the visible inventory. The child-ID API still returned error 1168. This is a rejected connection experiment, not a working child desktop, and the error alone does not identify an edition restriction or its cause. The remaining result process was stopped only after the final restored receipt and exact process incarnation were verified; no child session had been observed in that attempt.

### CredSSP correction and observed child desktop

The corrected client explicitly enabled `EnableCredSspSupport` and required exact true readback before connection. This is a documented per-connection setting, not a Windows authentication-policy modification. An upstream child-client implementation also sets it explicitly; that reference guided diagnosis, and no implementation from it was copied. [Microsoft property contract](https://learn.microsoft.com/en-us/windows/win32/termserv/imsrdpclientadvancedsettings6-enablecredsspsupport), [Anode implementation reference](https://github.com/skulitom/Anode/blob/main/src/Anode/Daemon/RdpViewer.cs).

With that correction on the same observed host, the user completed Windows identity confirmation and `Connect` returned `S_OK`; the control delivered `OnConnected` and `OnLoginComplete`. A successful `WTSGetChildSessionId` returned a new ID distinct from the parent, with an active WTS row absent from the baseline. Independent inventory observed **parent session 2 and child session 3 active simultaneously**, and the client visibly rendered a Windows desktop. These numeric IDs describe this experiment only and must never become future cleanup authority.

This is evidence of an actual native child desktop on the tested Core-edition build, not a VM or a mock. The client remained connected for further supervised tests. Successful disconnect, residual-session cleanup, restart recovery and physical monitor/input assignment are separate milestones and are not claimed by this observation. The desktop is displayed through an ActiveX window owned by the parent session. It uses the same user profile: application data and startup configuration are shared on disk, and startup applications were observed launching in the new session. A child desktop is not a separate-account security boundary or a measured low-resource configuration.

### Observed disconnect and remaining child

The client later received a user-close request. `Disconnect` returned `S_OK`, `OnDisconnected` reported reason 1, and the client restored the original disabled setting with successful readback and no receipt failure. Independent inventory observed the parent still active and the same child now disconnected. Thus closing the client did not terminate the child session; no logoff was attempted. The first subsequent input-launcher attempt refused that disconnected baseline before creating a task or receipt.

### Explicit resumption and first rejected trial

`--resume-child-session <id> --receipt <new-private-file>` is a distinct mode. It requires disabled feature state, a successful current-user child relationship matching the exact target, a disconnected target with a user, the active parent console and no other user session. The relationship and complete inventory are rechecked before enabling and immediately before connection. Resumption only reports success if that same existing ID becomes active with connected/logon-complete events; it does not claim a newly created session. The original new-session mode retains its stricter baseline.

The client also sets and reads back `IgnoreServerGeneratedMouseMoves = VT_BOOL true` before connection. Microsoft documents this as ignoring remote programmatic pointer-position updates. It is not a physical mouse assignment. [Property contract](https://learn.microsoft.com/en-us/windows/win32/termserv/imsrdpextendedsettings-property).

The first resumption trial accepted `Connect` with `S_OK` but observed no connected/logon-complete event. The client reached its 60-second observation deadline (`0x800705B4`), disconnected its own control and verified restoration to disabled. The child remained disconnected and the parent remained active. The planned input trial was not launched.

Private Windows events recorded `TsSslEventStartHandshakeFailed`, `E_FAIL 0x80004005`, approximately **88 ms after the initial connection event**. This precedes the deadline. However, a later comparison found the **same early event in the successful creation**, followed by connection around 12.8 seconds, and the user confirmed completing Windows identity confirmation for that successful attempt. The TLS event is therefore not sufficient to identify the terminal cause of the failed resumption. Both runs used the same parent authentication ID and elevation. Neither the feature disable/enable cycle nor the pointer setting has been established as the cause. The client supplies no credentials and leaves any Windows identity confirmation to the user.

Mode tests pass **67 pure checks** and the extended-property gate suite now passes **45 checks**. The single-click close path queues window destruction after restoration, outside the COM cleanup stack. Physical input assignment remains unverified. Successful resumption and the separate input experiment are described below.

### Diagnostic resumption and identity confirmation

A further supervised resumption used the same guards, properties and 60-second deadline, with numeric-only ActiveX event and connection-state diagnostics. The control reported `OnAuthenticationWarningDisplayed`; the user completed Windows identity confirmation. It then delivered `OnConnected` and `OnLoginComplete`, and the previously observed child ID returned to active state while the parent remained active. The client recorded `existing-child-resumed`, not a newly created desktop. The viewer was left connected for a separate [input experiment](CHILD-SESSION-INPUT.md).

This demonstrates resumption on the tested host with interactive Windows authentication. It does not establish unattended sign-in. Diagnostics retain event IDs, argument types and bounded numeric scalar values only; strings, interfaces, arrays and referenced values are not read into the receipt. The diagnostic contract passes **59 pure checks**.

## Verification and limits

The status suite passes 100 injected-API checks. The toggle suite passes 608 inert checks, including rejection before mutation, error/exception preservation, failed persistence, contradictory readbacks, restoration attempts and JSON output. These checks invoke no actual WTS API. They are distinct from the real target-edition toggle observations.

The observed child desktop does not establish independent physical input, Roblox compatibility or a complete RETSAR seat. No ordinary RDP connection, alternate account logon, session logoff or license-policy modification is implemented by these diagnostics.
