# Verified progress

Progress is organized by capability and evidence. A compiled component, simulated transaction or existing session ID is not recorded as an operational native seat.

## General-purpose desktop console and product identity

The [desktop preview CI run](https://github.com/kazah-png/RETSAR/actions/runs/43312642066/job/131512414242) at [`da9491c`](https://github.com/kazah-png/RETSAR/commit/da9491ccc636bcef6d184ba043c0ffd9d08991b6) passed **1,508 C++/CLI checks, 46 Python tests and 220 inert PowerShell checks**, with no failures or skipped adversarial branches. It successfully packaged and uploaded `RETSAR-desktop-preview-windows-x64`: the console, core CLI, child-session viewer, guide, license and SHA-256 manifest. The artifact is a developer snapshot, not a production installer.

RETSAR now presents itself as a general-purpose Windows desktop project. The native **RETSAR Desktop** console separates Overview, Displays and Activity, reads current session/display state asynchronously, and offers guarded open/resume actions through the existing child-session client. A clearly labeled `--preview` mode uses sample data without collecting session or device inventory or changing Windows sessions. The earlier gaming/automation pilot remains one application example, not a product dependency.

The project has an original two-desktop logo, scalable artwork, executable icons, a documented visual system, contributor guidance and structured issue templates. Public documentation distinguishes the working child-desktop preview from the unfinished physical multiseat and production lifecycle work. [Desktop guide](DESKTOP.md) · [Product scope](PRODUCT.md) · [Design system](DESIGN.md).

The management console refreshes only on explicit request and does not run an idle polling timer. The viewer cancels its 250 ms observation timer once a child connection is observed, eliminating 14,400 scheduled timer callbacks per connected hour; the previous connected-state callback already skipped COM/WTS queries. This is a control-flow reduction, not a measured CPU or memory saving. The new viewer also limits status invalidation to its header and filters accelerator forwarding to keyboard messages. Focus activation has new guarded forwarding, but F8 reliability remains unverified in a live session.

The complete local build passed **1,508 C++/CLI checks**, including 27 desktop decision checks and 30 inert viewer checks. Independent JSON parsing also passed. The final interface rebuild retained the same passing focused contracts. The three native sample-data views were inspected visually. Separate render fixtures checked the same layout at 100%, 125% and 150% scaling, including a short 430-DIP case; these fixtures do not establish physical monitor DPI behavior. The normal console observed the already-active child desktop and disabled duplicate opening. Manual refresh was checked without reconnecting it. Session admission and asynchronous shutdown paths were also reviewed independently. These checks do not create or reconnect a child desktop.

The active supervised pilot was preserved while the new interface and viewer were built separately. A new interface build does not retrofit the executable already running in that pilot. Automated tests, visual checks and CI packaging are recorded separately from live session evidence.

## First supervised Roblox and Zzen pilot

One Roblox account and an isolated Zzen copy ran in the native child desktop. After the user prepared the game and started Zzen with its own button, the bot detected **Play Again**, sent two bounded click attempts and recorded **one confirmed replay transition**. The next match was observed. The user confirmed normal mouse and keyboard use in the parent desktop while farming continued. [Trial details and limitations](CHILD-SESSION-INPUT.md#first-supervised-farming-cycle).

F8 did not activate Zzen in the observed viewer attempts; the button worked, and the hotkey cause remains unresolved. This is a supervised one-account pilot, not evidence of eight-hour stability, four-account capacity, exclusive physical devices or a complete ASTER replacement. The active pilot was left running.

## Child resumption and input diagnosis

The [resumption and input CI run](https://github.com/kazah-png/RETSAR/actions/runs/43303118692/job/131479239551) at [`d1c8719`](https://github.com/kazah-png/RETSAR/commit/d1c87195d66ad5b364232f09527dc35ca00078a6) passed **1,451 C++/CLI checks, 46 Python tests and 220 inert PowerShell checks**, with no failures or skipped adversarial branches. The real session/input observations below are separate supervised experiments.

The native child desktop was disconnected and later resumed as the same Windows session after the user completed Windows identity confirmation. The parent remained active. This establishes creation and interactive resumption on the observed host, while unattended authentication, owned-session logoff, physical device assignment and a production lifecycle remain open. [Observed client results](CHILD-SESSIONS.md).

A fixed, nonce-correlated Task Scheduler action reached the active child and rendered its own test window. A diagnostic mouse movement reached that window, while its 64-bit input marker arrived with only the low 32 bits. The strict probe rejected that mismatch and sent no F24 pair; its own cursor was restored and its temporary task was removed. A repeat observation recorded no cursor or foreground change in 493 parent samples. The corrected positive 31-bit tag then passed exact mouse and F24 down/up receipt, with cursor restoration, a zero task result, task removal and 39 unchanged parent samples. This is bounded synthetic session-input evidence, not physical keyboard/mouse exclusivity. [Input experiment and limitations](CHILD-SESSION-INPUT.md).

The combined local checks for this increment passed **1,451 C++/CLI checks**: the optional build covered the child and bridge contracts; the lifecycle and CLI suites were completed separately using a small C: test fixture because their existing file policy rejects a D: fixture. No adversarial branch was skipped. Build products and private reports stayed on D:. These local results are separate from the previously verified public CI run below.

An isolated existing Zzen preview copy was subsequently observed running in the child session, with its application window visible and initialization logged. Its default is paused; no farming was activated. The temporary launch task was independently confirmed absent while the same application process remained. The validly signed official Roblox application subsequently started in the same child and displayed its home screen; its temporary launch task was also removed. The supervised one-account replay described above subsequently verified a first farming cycle; prolonged use remains unverified.

## Native child desktop and remote-display preparation

The [child-client CI run](https://github.com/kazah-png/RETSAR/actions/runs/43295046562/job/131450985228) at [`95f4ad1`](https://github.com/kazah-png/RETSAR/commit/95f4ad16eeefadd229c902b7b47decdfaf6b645b) passed **1,242 C++/CLI checks, 46 Python tests and 220 inert PowerShell checks**, with no failures or skipped adversarial branches. Live child-desktop observations below are separate from these inert checks.

The [preparation CI run](https://github.com/kazah-png/RETSAR/actions/runs/43291934940/job/131440125569) at [`1ef0a47`](https://github.com/kazah-png/RETSAR/commit/1ef0a475eaf803436373c6bd868faac465753347) passed **1,204 C++/CLI checks, 46 Python tests and 220 inert PowerShell checks**, with no failures or skipped adversarial branches. The remote driver was separately compiled and validated locally; this workflow does not install or execute it.

The [child-session diagnostics](CHILD-SESSIONS.md) distinguish unsupported conclusions from actual API errors. On the observed Core-edition host, enabling failed without elevation, then succeeded after the user accepted Windows elevation; disabled state was restored and verified. That toggle did not connect a client. A subsequent original ActiveX client successfully configured explicit child mode, but its first real `Connect` returned `DISP_E_EXCEPTION` with underlying `E_INVALIDARG`. Disabled state was restored and the parent remained active. After explicitly enabling and reading back per-connection CredSSP support, a second experiment completed connection and logon, observed a new active child ID and rendered its desktop while the parent stayed active. This establishes a native child desktop on the tested Core host, not physical device isolation or a production lifecycle. The focused local suite added 100 status and 608 toggle checks using injected APIs only. The new client gate also passes 38 fake-interface checks; these do not establish a real connection.

A separate [remote IDD source package](../lab/idd-remote-reference/README.md) now compiles with the reviewed MSVC/WDK toolchain and passes universal INF validation. It uses a distinct remote hardware ID and the OS remote-session device contract, with an explicit 1280x720 path. Source/manifest/patch/build-artifact checks passed separately. It is unsigned, unstaged and unexecuted; the console IDD runtime evidence does not transfer to it. No remote display or physical input success is claimed.

## Authenticated service-control increment

The verified [service-control CI run](https://github.com/kazah-png/RETSAR/actions/runs/43289887808/job/131433002658) at [`2a063f4`](https://github.com/kazah-png/RETSAR/commit/2a063f4a89b409d67f29614c4a2bb27f16a75e88) passed **496 C++/CLI checks, 46 Python tests and 220 separate inert PowerShell checks**, with no failures or skipped adversarial branches. This run predates the child-session and remote-IDD-source increment.

The new [local control channel](SERVICE-CONTROL.md) reached the listener created by actual TermService in the disposable Server guest. A real explicit Start returned `E_INVALIDARG` (`0x80070057`) from `OnConnected`. No new user session or device isolation was observed. The service-restart capture initially missed delayed provider loading; a cold-start capture then recorded 34 provider events with zero reported loss. Exact owned registration, tracing and DLL cleanup was verified afterward.

The local focused C++ suite passed **378 checks**: 125 existing bridge mocks, 31 control-protocol checks, 54 worker checks, 129 licensing-framing checks and 39 remote-display-contract checks. These are inert tests; the live guest observation is separate. PowerShell 5.1 passed **97 activation** and **87 boot-trace** pure checks for explicit control opt-in and exact cleanup, including compatibility with previous receipts. These new focused results do not replace the historical complete core/CLI baseline below.

Review fixed stop-time callback retention and loss accounting. The worker tests exercise an actual fixture DLL remaining loaded while a fake callback is blocked, then unloading after stop and return. They do not claim that a stuck Windows callback can be forcibly canceled. The [licensing adapter](LICENSE-TRANSPORT.md) validates only transport framing; the genuine licensing engine, remote display driver and physical input backend are still missing.

## SDK bridge and laboratory supervision checkpoint

The last complete local C++/CLI baseline passed **242 checks**, including **125 mock RDS checks**. The supervisor suite now has **46 separate inert Python tests**, and local guest-helper checks total **175**: 27 observer, nine reference-marker, 73 activation and 66 boot-trace checks. These helpers exercise pure logic and synthetic buffers; they do not run guest operations.

The earlier complete [Windows CI baseline](https://github.com/kazah-png/RETSAR/actions/runs/43277022578/job/131388022533) passed at commit [`8b5f425`](https://github.com/kazah-png/RETSAR/commit/8b5f425439ef328964cbf51c9508c33b3626ffa6): **243 C++/CLI checks, 46 separate Python tests and 175 separate inert PowerShell checks**, with no failed or skipped checks. Its PowerShell scope was 27 observer, nine reference-marker, 73 activation and 66 boot-trace checks; it also parsed five guest scripts without executing them. The table below keeps local and CI scopes separate.

The bridge tests load the explicit DLL and fake callback objects; they do not register COM classes or call an RDS service. The Python suite uses fake Windows API calls and inert configuration/template checks. Separate local native checks verified termination and kill-on-job-close with two newly created sleeping Python helpers. Those native process checks were not part of CI and did not create a VM or Windows session.

Review corrected the required readiness notification order, serialized outgoing cancellation, preserved original cancellation failures, and retained early QEMU startup errors in the laboratory log.

The narrow settings fix accepts a null optional `IWRdsProtocolSettings` interface and still rejects a null required `WRDS_SETTINGS` structure. Sixteen added mock checks cover reference retention, replacement, rejection and release. The structure's service policy is still not interpreted; successful reference management is not complete RDS initialization. The [integration-gaps review](RDS-INTEGRATION-GAPS.md) records the remaining contracts.

An earlier guest installation reached Windows Setup but failed during `oobeSystem`. Read-only inspection of the guest logs identified `FirstLogonCommands/SynchronousCommand/CommandLine` as invalid with error `0x80220005`. Its generated command contained 1,318 characters, exceeding the documented limit of 1,024. The fixed template now contains a 309-character launcher and no encoded-command placeholder; a regression test checks the constraint. [Microsoft CommandLine documentation](https://learn.microsoft.com/en-us/windows-hardware/customize/desktop/unattend/microsoft-windows-shell-setup-firstlogoncommands-synchronouscommand-commandline).

The qcow2 structural check passed, but that did not establish successful Windows setup or repair the invalid answer file. Laboratory asset relocation was verified before starting a fresh corrected installation from a private root on D:, under the same 4 GiB RAM and two-vCPU budget. That corrected installation reached its first Administrator desktop. A guest screenshot showed the desktop and the bootstrap collecting role data; the serial report recorded `retsar.guest-bootstrap.v1`, `phase=environment-verified`, `computer=RETSARLAB`, Windows Server 2022 Standard Evaluation and `createdSeat=false`.

The TCG guest was then shut down cleanly: QMP reported `guest-shutdown` and the process exited with code zero. A stopped-guest checkpoint of the disk and writable firmware was verified with SHA-256. A separate read-only host capability query returned `S_OK`, four bytes and `HypervisorPresent=TRUE`; no Windows feature or service was changed. The explicit WHPX launch reached the guest desktop under the same resource budget. QEMU emitted `Ignoring request for interrupt vector 0`; that warning did not prevent the observed desktop and bootstrap result, but its wider impact has not been established.

Rerunning the bootstrap inside that guest emitted `environment-verified` and then `rdsh-configured`, with `installed=true`, `serviceStatus=Running`, `providerRegistered=false` and `createdSeat=false`. This verified the role and running service. The subsequent guest-local RDP reference has now produced a distinct active user logon and a running probe inside it, as detailed below; it is not an operational RETSAR provider. The native executor stays blocked and physical-seat P0 remains open. These guest observations are separate from the automated test totals and do not grant approval for another deployment.

The laboratory supervisor now permits a private root on C: or D: while monitoring free space on both C: and the selected laboratory volume. Other volumes are rejected before filesystem access. This change applies only to the disposable laboratory; the core's C-only journal and CLI output policy remains unchanged.

## Current evidence · 0.2.0-engine-preview

| Check | Latest local result | Verified public CI result (`8b5f425`) |
| --- | --- | --- |
| Model, validation and capabilities | 25 passed | 25 passed |
| Lifecycle, journal and recovery | 57 passed | 58 passed |
| CLI contracts and file output | 6 passed | 6 passed |
| Laboratory probe argument/session-gate tests | 29 passed | 29 passed |
| RDS bridge mock callback contracts | 125 passed | 125 passed |
| **C++/CLI total** | **242 passed, 0 failed** | **243 passed, 0 failed, 0 skipped** |
| Python supervisor and answer-template tests; no guest execution | 46 passed, separate from C++/CLI total | 46 passed, 0 failed, 0 skipped; separate from C++/CLI total |
| Pure observer PowerShell checks; no WTS calls | 27 passed, separate from C++/CLI and Python totals | 27 passed, 0 failed; separate from other totals |
| Pure reference-marker PowerShell checks; no account operations | 9 passed, separate from other totals | 9 passed, 0 failed; separate from other totals |
| Inert activation PowerShell checks | 73 passed | 73 passed, 0 failed |
| Inert boot-trace PowerShell checks | 66 passed | 66 passed, 0 failed |
| **PowerShell helper total** | **175 passed, separate from other totals** | **175 passed, 0 failed, 0 skipped; separate from other totals** |
| Native supervisor checks with owned helper processes | 2 passed, separate from test-suite totals | Not run |
| Guest PowerShell script syntax; no script execution | All 5 guest scripts parsed separately | 5 scripts parsed; separate from test-suite totals |
| Independent JSON parsing by PowerShell | Passed, additional to the total | Passed, additional to the total |
| Adversarial junction branch | **1 not exercised** | Exercised and included among lifecycle passes |
| x64 C++17 build with `/W4 /WX` | Passed | Passed |
| Probe interactive mode | Executed inside the Server guest's RDP candidate; exported JSON and account SID comparison verified | **Not run** |

The local adversarial test tried to convert its own temporary directory on C: into a junction targeting another temporary directory on C:. Windows rejected the mutation with error **5 — access denied**, so the redirected-child branch was not exercised locally and is not counted as passed. CI allowed and verified that branch, accounting for the one-check difference between the C++/CLI totals.

Historical baselines: [`b4eb74e`](https://github.com/kazah-png/RETSAR/actions/runs/42802709727/job/129784014483) passed 227 C++/CLI checks and 16 Python tests in CI; the earlier core/probe-only [`27893ec`](https://github.com/kazah-png/RETSAR/actions/runs/42795998244) passed 117 checks locally and 118 in CI. Those checks are included in the current suite, not additional passes to add to its totals. Each linked run validates only its tested revision.

## Milestone: transactional foundation

**Status: implemented and tested with an explicit simulated provider.**

Evidence covers:

- Exclusive journal writers, including a separate-process test.
- Rejection of corruption, partial records, invalid sequences and accidental initialization of a missing journal.
- Reservations, configuration bounds, altered or expired tokens and external inventory changes.
- Simulated startup and reverse compensation, with bindings tied to the exact session owned by the run.
- Uncertain outcomes, write failures, lost receipts, engine restarts and reused resource IDs.
- Read-only reconciliation and conservative presentation of historical states.
- Writing the RETSAR schema and reading the former identifier without rewriting the journal.
- Blocked native Windows capabilities and creation-only CLI output.

The public executable is linked with the transactional core. The simulated provider is compiled only into tests. This suite does not inventory host devices or create or modify Windows seats.

## Milestone: laboratory observation instrument

**Status: instrument compiled and executed in the Server RDP candidate; exported report and account SID comparison verified.**

`RETSAR-seat-probe` is separate from the main executable. Its 29 pure checks cover arguments and the gate comparing the declared candidate and reference sessions. The build still invokes only its help. In a separate live guest experiment, interactive mode ran inside the newly observed candidate with the original console declared as reference.

The instrument declares `createdSession=false` and `independenceNotProven=true`. It requires manual window activation and counts messages without treating their presence as proof of origin. Its design and limits are documented in [NATIVE-BACKEND.md](NATIVE-BACKEND.md#c-laboratory-instrument).

The exported probe JSON confirmed `WinSta0` / `Default` and complete own-process SID, authentication LUID, PID and creation-time fields, with zero identity-query errors. Its user SID matched the separately retained reference-account receipt, and its session matched the candidate in the connected WTS snapshot. The observed adapter was Microsoft Basic Render Driver with `hardwareAdapterObserved=false`. Input counters showed three mouse movements, one button event and one key-down event delivered through the synthetic QMP/VNC/RDP path. The probe terminated when its own window closed. This validates the report and observed delivery to the probe, not physical routing, exclusive input, provider ownership or GPU presentation.

## Milestone: Server reference experiment

**Status: concurrent reference-account and console logons verified; an exported probe report passed inspection, and a separate manual same-boot sign-out/cleanup cycle passed independent checks. The connection helper still reaches its observation timeout.**

The latest [guest-local RDP reference](../lab/RDP-REFERENCE.md) repeat produced complete baseline, connected and cleaned WTS snapshots from one boot. They show the fresh reference account absent, active and absent respectively, with a new account SID. The original console was active at all three observations with unchanged logon time. Sign-out was requested inside the candidate desktop. Guarded cleanup reported `owned-account-and-reference-files-removed`; separate checks confirmed account, credential and reference-directory absence. The probe was not rerun in this cycle: its independently inspected report belongs to the earlier experiment described above. The laboratory then shut down normally, with QMP `guest-shutdown` and supervisor exit code zero.

Both connection-helper runs reported `Observation window expired`, with no normal `client-exited-observation-required` phase. The manually observed lifecycle and receipt-based cleanup succeeded independently of that timeout. They do not establish normal helper completion, continuous input isolation between snapshots or an operational RETSAR provider. The connection preserved Windows authentication and licensing decisions, and its public certificate fingerprint was compared with the guest listener's reference.

Live execution exposed two diagnostic errors. The observer queried two `WTSListen` rows for logon identity; it now preserves them as listeners and retains every applicable-session error, covered by 27 inert checks. Account creation initially failed because its description was 53 characters rather than at most 48; both creation and cleanup now derive the same 43-character marker with the full GUID, covered by nine AST/string checks.

The earlier experiment lost its controller before final WTS enumeration and cleanup. QEMU was no longer running; its stopped disk passed a read-only qcow2 structural check, which does not prove a clean shutdown or filesystem integrity. Receipt-based recovery after a guest restart verified account, credential, directory and candidate-session absence. That recovery is evidence from another boot, distinct from the later same-boot repeat.

Neither display names, session IDs nor a standalone token report prove ownership by the RETSAR provider. This reference uses Microsoft's existing Server RDP stack and does not satisfy the native provider or physical multiseat milestone.

## Milestone: real provider activation

**Status: TermService loaded the provider, initialized the manager and started its listener in the disposable Server guest. No client connection, licensing exchange or RETSAR-created user session has been demonstrated.**

The first two [activation trace experiments](ACTIVATION-TRACE.md) produced four service captures: minimal registration with a service restart and a cold boot, followed by an owned listener with `fEnableWinStation=1` under the same two startup routes. Each retained two system metadata records, zero RETSAR events and `EventsLost=0`. The first service restart also produced LocalSessionManager event 17, `0x800706BA`, despite TermService reporting Running. Successful AutoLogger startup statuses were not treated as provider readiness.

A separate explicit COM control constructed the manager and emitted 31 decoded RETSAR events, all from the control process rather than TermService. Those first two experiments completed verified cleanup: owned traces were stopped, AutoLogger and provider keys and DLLs were removed, and the standard RDP listener remained unchanged. Receipts and captured evidence were retained for targeted diagnosis. The listener flag is an inference from Microsoft's RDP-Tcp troubleshooting guidance; its presence alone did not produce observed custom-provider activation.

The third attempt corrected the staged DLL's observed DACL, adding explicit NetworkService read/execute while retaining SYSTEM/Administrators full control. Its trace contained **21 decoded RETSAR events, all from the independently identified TermService process**, and the collector observed the loaded module. Manager initialization, listener creation and `StartListen` returned `S_OK`; WTS showed a new listener without an added interactive user. `GetSettings` returned `E_NOTIMPL` and the service continued to `StartListen`; a later `NotifyServiceStateChange` also returned `E_NOTIMPL`. The third attempt completed verified removal of its owned provider registration and DLL, with the standard RDP listener unchanged and captured evidence retained. No boot trace was armed. This is service/listener activation evidence, not a connection or seat.

A guest restart under WHPX reached virtual-processor exit 4 and QMP `guest-panic`, with process exit code zero. That exit code was not a clean-shutdown result. The stopped disk passed a read-only qcow2 structural check, which does not establish filesystem integrity. Clean guest power-off and a fresh supervised launch subsequently worked; reset reliability remains unresolved.

Current development, builds and heavy laboratory assets are on D:. Host multiseat software, host display/input assignments and unrelated volumes remain untouched. These observations do not advance physical-seat P0 to complete.

## Milestone: console indirect-display reference

**Status: a bounded guest device lifecycle and acquired-buffer evidence were observed; cleanup was independently verified after a cold boot.**

The separate [console IDD experiment](../lab/idd-reference/RESULTS.md) installed its guest-only signed package and ran the device holder for 120 seconds. The helper exited with code zero and verified creation, started/present state, handle closure and disappearance. Its trace contained 22 provider events from one PID, with device/adapter initialization, monitor arrival, mode handling and successful D3D initialization. `FramesAcquired` reported `Count=1`; `DriverUnload` later reported a total of three acquired buffers and two assigned swapchains. ETW reported zero lost events and buffers.

The first `SwapchainSetDevice` returned `DXGI_ERROR_ACCESS_LOST` (`0x887A0026`) at trace offset +0.247 seconds; a second assignment succeeded at +0.250 seconds. `AcquireStopped` returned the same error at +119.691 seconds, followed by unload about 11 milliseconds later. This timing is compatible with teardown, but the holder's close action has no corresponding timestamped event, so causality is not established. No moving-pixel output, sustained rendering or hardware acceleration was demonstrated.

Receipt-based cleanup reported removal of the owned package, device and certificate and restoration of the guest boot setting, with no recorded cleanup failure. A separate cold boot confirmed the new boot, absence of owned certificates, package, software devices and provider registration, and a stopped trace. The test-signing setting was absent both in the original baseline and in the verified current configuration. This console virtual display is separate from the RDS provider/listener experiment: it establishes neither remote-session display binding nor a new user session, independent input or a physical second seat. Source licenses and compilation constraints remain recorded in the [reference directory](../lab/idd-reference/README.md).

## Milestone: native session research

**Status: candidate interfaces identified; no operational provider demonstrated.**

Static analysis identified coordination of existing sessions, communication with a driver and process launch within a specified session. The investigated indirect call resolved to an update of the active-console identifier. That path did not provide a verified contract for creating a new independent interactive session. It also does not establish that such creation is impossible through other mechanisms.

Those investigated operations were not executed and system sessions were not modified. This behavioral summary includes no proprietary binaries, decompilations or private protocols. It adds no test passes and establishes no native Windows capability.

The [native backend investigation](NATIVE-BACKEND.md) identifies a public RDS protocol-provider candidate and separates its laboratory acceptance gates from the physical-seat requirement. The [integration-gaps review](RDS-INTEGRATION-GAPS.md) explains why the partial bridge cannot yet create a supported first session. The remaining step is to validate a mechanism that creates a new interactive session, proves ownership, preserves the reference seat and removes only the created instance.

## Milestone: reproducible public build

**Status: public Windows CI passed at the linked revision.**

[Windows CI](../.github/workflows/windows.yml) copies the required sources, tests, laboratory files and build script to a verified temporary directory on C: to preserve the engine's file policy. It compiles the engine, laboratory instrument and optional SDK bridge, then runs pure tests, mock callback checks, CLI checks, the Python fake-API suite and inert PowerShell helper checks. The PowerShell checks parse source, compile extracted native declarations and inspect pure values or synthetic buffers without executing guest scripts or invoking WTS, ETW control, account, registry or service operations. CI invokes only the probe's help and does not launch a VM or run the native helper-process smoke test.

The SDK selection checks for the required UCRT headers, x64 libraries and manifest tool before choosing an installed version. A previous CI build exposed an incomplete SDK directory; the passing run validates the corrected selection in that environment.

The workflow badge refers to CI for its specific revision. It is not a native-seat readiness indicator.

## Reproducing the checks

From an ordinary directory on C:, with the [build prerequisites](../README.md#build-and-explore):

```powershell
.\build.ps1 -Test -Lab -RdsLab
python -B -m unittest discover -s lab -p test_supervise_qemu.py -v
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\guest_session_observer_tests.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\guest_rdp_reference_tests.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\guest_provider_activation_tests.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\guest_provider_boot_trace_tests.ps1
```

The suite prints individual outcomes and identifies unexercised branches. Temporary artifacts are written under `build/` and CLI reports under `reports/`; those directories are not published source. The CLI validation report includes the hash of the checked local executable.

## Unverified capabilities

The available evidence does not establish:

- Creation of a second real Windows seat.
- Independent physical displays or input routing between users.
- Support for specific applications, simultaneous accounts or prolonged operation.
- Recovery from every combination of power loss, hardware failure or malicious journal changes.

The next acceptance milestone is [P0](ROADMAP.md): an independent laboratory demonstrates a real second seat with verifiable ownership, safe teardown and preservation of the existing seat. Native capabilities remain `NOT_IMPLEMENTED` until the necessary evidence exists.

Open tracking: [create an owned session](https://github.com/kazah-png/RETSAR/issues/1) and [demonstrate independent display/input](https://github.com/kazah-png/RETSAR/issues/2).
