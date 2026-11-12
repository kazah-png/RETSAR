# Native provider investigation

This document defines a research direction and its acceptance tests. It does not describe an operational provider or installation instructions for an everyday computer.

The [original SDK bridge](RDS-BRIDGE.md) implements an opt-in, in-process integration scaffold. Its callback tests use mocks; service integration remains unverified. The [isolated laboratory](../lab/README.md) documents the separate guest preparation and supervision path.

The [integration-gaps review](RDS-INTEGRATION-GAPS.md) details the service, licensing, authentication and device contracts still missing from that bridge.

**RETSAR has no validated mechanism for creating a second simultaneous Windows 11 seat with independent display and input.** The absence of a proven implementation does not establish that every possible implementation is technically impossible. The transactional core is separate from that question: its tests do not prove session creation.

## Candidate with public interfaces and source

Microsoft publishes [Remote Desktop Services Protocol Sample](https://github.com/Azure/Remote-Desktop-Services-Protocol-Sample), an MIT-licensed example of an RDS protocol provider. Its C++/ATL solution is `Sample/TestProtocol_Ext.sln`. The provider submits connections to RDS and can participate in associating input devices and an indirect display driver with a session. It is not a complete multiseat implementation.

The [sample README](https://github.com/Azure/Remote-Desktop-Services-Protocol-Sample/blob/main/README.md) states that the client editions it discusses support one active session and directs concurrent scenarios toward Windows Server with RDSH or Enterprise multi-session. The [IWRdsProtocolManager documentation](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nn-wtsprotocol-iwrdsprotocolmanager) lists Windows Server 2012 as the minimum supported server and no supported Windows client. The presence of a DLL or these interfaces does not establish that the installed edition supports the intended scenario.

### Sequence the laboratory must implement and observe

| Interface or event | Verifiable function | What it does not establish on its own |
| --- | --- | --- |
| `IWRdsProtocolManager::CreateListener` | RDS requests the object that will handle connections. | A created or connected session. |
| `IWRdsProtocolListener::StartListen` | Supplies the RDS callback to the provider. | Available display or input. |
| `IWRdsProtocolListenerCallback::OnConnected` | Reports an incoming connection and starts RDS processing. | Allocation of a new simultaneous session. |
| `IWRdsProtocolConnectionCallback::OnReady` | Reports that the connection is ready to proceed. | Completed user logon. |
| `IWRdsProtocolConnection::NotifySessionId` | Reports the session associated with the connection; the association can change. | Permanent ownership of a numeric ID. |
| `IWRdsProtocolConnection::LogonNotify` | Reports user logon. | Display or input isolation. |
| `IWRdsWddmIddProps::GetHardwareId` / `OnDriverLoad` | Selects the IDD and receives its load notification. | Exclusive control of a physical output. |
| `IWRdsProtocolConnection::GetInputHandles` | Supplies input handles consumed by the stack. | Correct isolation of the devices. |

The service/provider relationship is documented in [Method Call Sequence](https://learn.microsoft.com/en-us/windows/win32/termserv/method-call-sequence). [OnConnected](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nf-wtsprotocol-iwrdsprotocollistenercallback-onconnected) begins an RDS-managed sequence; it is not a generic local-seat reservation call.

[TestProtocolAPI.cpp](https://github.com/Azure/Remote-Desktop-Services-Protocol-Sample/blob/main/Sample/TestProtocol_Ext/TestProtocolAPI.cpp) shows `OnConnected`, `GetConnectionId` and `OnReady`. This sequence is an integration reference. The sample's test trigger and authentication handling require suitable replacements before use outside a laboratory.

### Gaps in the sample

In [WRdsProtocolConnection.cpp](https://github.com/Azure/Remote-Desktop-Services-Protocol-Sample/blob/main/Sample/TestProtocol_Ext/WRdsProtocolConnection.cpp), `GetInputHandles` remains a TODO and does not supply operational input handles. `GetHardwareId` names a sample driver; that string does not amount to an installed, validated implementation. Session callbacks are extension points, not a durable ownership record. Results, outputs, object lifetimes and recovery require review before integration with `SeatExecutor`.

Compiling that solution therefore cannot mark RETSAR's native capabilities as implemented. It also does not resolve Windows edition, licensing or authentication requirements.

## Public-source distinction: allocation and coordination

ReactOS provides a concrete creation chain: `SmStartCsr` requests work from SMSS; `SmpStartCsr` loads the session's subsystems and starts its initial command. That path reaches `SystemSessionCreate` and the memory manager before starting the Win32 environment and the default initial command, Winlogon. Its internal subsystem session IDs must not be confused with the Terminal Services session ID. This is evidence about ReactOS, not an implementation contract for current Windows. Sources are pinned to one revision: [SM client](https://github.com/reactos/reactos/blob/1103f43a0c0eae4906828b708a8726587888ef5f/sdk/lib/smlib/smclient.c), [SMSS dispatch](https://github.com/reactos/reactos/blob/1103f43a0c0eae4906828b708a8726587888ef5f/base/system/smss/smloop.c), [subsystem initialization](https://github.com/reactos/reactos/blob/1103f43a0c0eae4906828b708a8726587888ef5f/base/system/smss/smsubsys.c).

In that implementation, the user-mode allocation request requires the driver-loading privilege and the memory manager enforces session-leader conditions. These requirements do not establish that an arbitrary elevated Windows application can create an interactive session. [ReactOS privilege check](https://github.com/reactos/reactos/blob/1103f43a0c0eae4906828b708a8726587888ef5f/ntoskrnl/ex/sysinfo.c), [session allocation](https://github.com/reactos/reactos/blob/1103f43a0c0eae4906828b708a8726587888ef5f/ntoskrnl/mm/ARM3/session.c). Microsoft's [historical Terminal Server walkthrough](https://learn.microsoft.com/en-us/troubleshoot/windows-server/remote/terminal-server-startup-connection-application) describes the service requesting new session space from SMSS, but supplies no current Windows 11 Home integration contract. By contrast, [WTSConnectSession](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsconnectsessionw) connects existing sessions. No ReactOS code, message layout or inferred private protocol is incorporated here; **a Windows 11 Home creation contract remains unvalidated**.

A bounded future client-guest control can use normal Windows user switching between two laboratory accounts, collecting WTS observations and each process's own identity before and after. [Microsoft's user-switching model](https://learn.microsoft.com/en-us/windows/win32/shell/fastuserswitching) distinguishes logon persistence from console connection. Such a control can investigate creation and persistence on the target edition while recording the first session's disconnection; it cannot pass the simultaneous-session acceptance gate or demonstrate exclusive physical display/input assignment. It has not been run by this investigation, and neither it nor the Server RDP reference completes the physical-seat milestone.

## Display depends on a real session

The [IddCx documentation for remote sessions](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/iddcx1.4-updates-for-remote-idds) sets a specific prerequisite: the system remote desktop stack must have created the device for an IDD using `IDDCX_ADAPTER_FLAGS_REMOTE_SESSION_DRIVER`. `IddCxAdapterInitAsync` fails when that condition and the flag do not agree. Creating a virtual monitor and assigning it a session number is insufficient.

Once the appropriate context exists, the driver declares monitors and configures that session's topology through IddCx. [IWRdsWddmIddProps](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nn-wtsprotocol-iwrdswddmiddprops) provides the IDD integration with RDS. Render-adapter selection can use [IddCxAdapterSetRenderAdapter](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/iddcx/nf-iddcx-iddcxadaptersetrenderadapter), but it requires measurement: loading a driver proves neither effective GPU acceleration nor application compatibility.

[GetInputHandles](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nf-wtsprotocol-iwrdsprotocolconnection-getinputhandles) documents keyboard and mouse device handles. It is not an independent session-creation operation and does not prove that a particular input route is isolated. The laboratory must verify that end to end.

## Prerequisites for the first experiment

Implementation work must be buildable without registering providers, installing drivers, changing services or touching sessions. Execution with effects requires a separate environment prepared specifically for testing:

- A laboratory machine or disposable VM with verified recovery and no everyday sessions.
- An edition and configuration supporting the scenario being tested. Windows Server with RDSH is the documented candidate for validating the interfaces first. That does not establish later Windows 11 support.
- A test account, normal authentication and laboratory administrator permissions; reports must contain no secrets.
- Compatible C++/ATL tools, SDK and WDK, plus a driver-signing route supported by the environment. The plan does not depend on disabling protections on an everyday computer.
- Separate records for pre-existing and experiment-created resources, with run identifiers and durable operations.

Enterprise multi-session has specific deployment requirements; it is not proposed as an automatic replacement for a client installation. See the [official Windows Enterprise multi-session FAQ](https://learn.microsoft.com/en-us/azure/virtual-desktop/windows-multisession-faq). This phase does not change concurrency policies, license mechanisms or another product's components to force a successful result.

## Acceptance gates

Every stage must retain failed results and unexercised cases. No single stage enables `WindowsBlockedExecutor`.

| Stage | Required evidence | Reason to stop progression |
| --- | --- | --- |
| **A. Session identity** | A new owned connection, a new interactive session and recorded correlation between run, connection and session. The reference main session remains connected. | Reuse of another owner's session, unreconciled identity changes or disconnection of the main seat. |
| **B. Graphics** | An IDD associated by the correct stack, an updating surface, a graphics application inside the session and recorded adapter/render mode. | Only a monitor added to the main desktop, no image or unmeasured rendering. |
| **C. Input** | Events directed to the test seat, with simultaneous observation that the main cursor and applications do not receive them. | A result based only on focus, coordinates, global hooks or a screenshot. |
| **D. Recovery** | Intermediate failures and lost replies preserve ownership and uncertainty; cleanup checks the complete identity of each owned resource. | Release by numeric ID, acquisition of another owner's resources or repeated creation after an uncertain reply. |
| **E. Physical seat** | Independent physical output and input on laboratory hardware while preserving the first seat. Reproducible creation, use and teardown. | Only a visible remote connection, an alternate desktop or an image viewer. |

Stages A–D investigate necessary components; **an RDS laboratory session still does not satisfy the physical-seat P0 milestone**. Stage E and the criteria in [ROADMAP.md](ROADMAP.md) remain mandatory. A VM can support protocol and fault research; it does not replace physical validation or establish game compatibility.

Reports must specify Windows version and edition, laboratory configuration, exact provider and driver revisions, observed transitions, original errors and cleanup performed. Incompatibility is a result to record. It is not converted into success by changing system restrictions.

## C++ laboratory instrument

`src/lab/seat_probe.cpp` implements a separate evidence-collection instrument for a candidate session. Build it with `build.ps1 -Lab`; `-Test` adds pure argument and admission-gate tests. The build runs only its help, never interactive mode.

With no arguments or `--help`, it prints help without observing the host. `--interactive` requires `--expected-session N` and `--reference-session M`, both positive and different integers. First it verifies that its own process already belongs to N and not M. If that gate fails, it exits before opening output files, inspecting graphics objects, creating a D3D11 device or opening a window. It does not seek or create a matching session.

After the gate, it queries its own window station and desktop, loads the system D3D11 runtime, requests a hardware device and shows a window that must be activated manually. It counts only mouse and keyboard messages delivered to that window; it does not record specific keys, install hooks or inject input. It also does not verify whether another process originated those messages. Animation uses GDI. **Creating a D3D11 device does not prove GPU presentation or association with a physical display.** Adapter classification follows the requested type and DXGI software flag; it does not prove exclusive assignment of a physical GPU.

The same gate precedes collection of its own process identity: PID, process creation FILETIME, user SID and the token's `AuthenticationId` LUID. Missing values remain null with explicit Win32 errors. This distinguishes observations more precisely than a numeric session ID alone, but `ownershipProven` stays false: the fields are not an authenticated operation receipt. No other process token is opened. The expanded probe compiled with `/W4 /WX` and passed its inert help invocation; live identity collection remains unverified.

On exit, it emits JSON with `createdSession=false`, `independenceNotProven=true` and `referenceSessionObserved=false`. The reference ID is an operator declaration, not a session inspected by the instrument. `--output` writes the report to a new file in an existing directory on C: using the same restricted-open helper; it never overwrites. A write failure can leave a partial new file and is reported as an error.

The probe does not grant ownership by observing an ID. Simultaneous comparison with the main seat remains a separate test. Interactive mode and graphics-device creation are pending execution in an independent laboratory; adding or compiling the probe completes neither a native capability nor P0.

### Instrument evidence

The earlier core/probe baseline passed C++17 compilation with `/W4 /WX`, help invocation and **29 pure probe checks**, without interactive execution. That local `build.ps1 -Test -Lab` run passed **117 checks**, plus independent JSON validation, with one adversarial junction branch not exercised. The corresponding public CI passed **118 checks**, including that branch. See [PROGRESS.md](PROGRESS.md) for its exact revision and the separate results for later bridge and supervisor changes.

Neither run validates the interactive window, D3D11 creation, event delivery or a real-session report.

## Provenance and scope

This investigation uses Microsoft documentation and the public Azure sample at [commit `0b5a0bae1cc1c2a667b5962bd7847d5181ae375f`](https://github.com/Azure/Remote-Desktop-Services-Protocol-Sample/tree/0b5a0bae1cc1c2a667b5962bd7847d5181ae375f). The implementation is original SDK-based code; no Azure sample implementation is incorporated. Before any future code reuse, preserve the exact source revision and license.

There is no project-owned experimental validation of this provider candidate in this preview. The conclusion is **a candidate provider awaiting validation**, not guaranteed compatibility or a general claim that Windows multiseat is impossible.
