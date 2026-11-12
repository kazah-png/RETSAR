# Verified progress

Progress is organized by capability and evidence. A compiled component, simulated transaction or existing session ID is not recorded as an operational native seat.

## SDK bridge and laboratory supervision checkpoint

The last complete local C++/CLI baseline passed **242 checks**, including **125 mock RDS checks**. The supervisor suite now has **46 separate inert Python tests**, and local guest-helper checks total **168**: 27 observer, nine reference-marker, 66 activation and 66 boot-trace checks. These helpers exercise pure logic and synthetic buffers; they do not run guest operations.

The latest verified [Windows CI run](https://github.com/kazah-png/RETSAR/actions/runs/43265307026/job/131347162820) passed at commit [`aa8761a`](https://github.com/kazah-png/RETSAR/commit/aa8761ad26302153b45e70d0df824d4ee32d5230): **243 C++/CLI checks, 46 separate Python tests and 97 separate inert PowerShell checks**, with no failed or skipped checks. Its PowerShell scope was 27 observer, nine reference-marker and 61 activation checks; it also parsed four guest scripts without executing them. The later listener-flag and boot-trace changes have local checks but their next CI run is pending. The table below keeps those revision boundaries explicit.

The bridge tests load the explicit DLL and fake callback objects; they do not register COM classes or call an RDS service. The Python suite uses fake Windows API calls and inert configuration/template checks. Separate local native checks verified termination and kill-on-job-close with two newly created sleeping Python helpers. Those native process checks were not part of CI and did not create a VM or Windows session.

Review corrected the required readiness notification order, serialized outgoing cancellation, preserved original cancellation failures, and retained early QEMU startup errors in the laboratory log.

The narrow settings fix accepts a null optional `IWRdsProtocolSettings` interface and still rejects a null required `WRDS_SETTINGS` structure. Sixteen added mock checks cover reference retention, replacement, rejection and release. The structure's service policy is still not interpreted; successful reference management is not complete RDS initialization. The [integration-gaps review](RDS-INTEGRATION-GAPS.md) records the remaining contracts.

An earlier guest installation reached Windows Setup but failed during `oobeSystem`. Read-only inspection of the guest logs identified `FirstLogonCommands/SynchronousCommand/CommandLine` as invalid with error `0x80220005`. Its generated command contained 1,318 characters, exceeding the documented limit of 1,024. The fixed template now contains a 309-character launcher and no encoded-command placeholder; a regression test checks the constraint. [Microsoft CommandLine documentation](https://learn.microsoft.com/en-us/windows-hardware/customize/desktop/unattend/microsoft-windows-shell-setup-firstlogoncommands-synchronouscommand-commandline).

The qcow2 structural check passed, but that did not establish successful Windows setup or repair the invalid answer file. Laboratory asset relocation was verified before starting a fresh corrected installation from a private root on D:, under the same 4 GiB RAM and two-vCPU budget. That corrected installation reached its first Administrator desktop. A guest screenshot showed the desktop and the bootstrap collecting role data; the serial report recorded `retsar.guest-bootstrap.v1`, `phase=environment-verified`, `computer=RETSARLAB`, Windows Server 2022 Standard Evaluation and `createdSeat=false`.

The TCG guest was then shut down cleanly: QMP reported `guest-shutdown` and the process exited with code zero. A stopped-guest checkpoint of the disk and writable firmware was verified with SHA-256. A separate read-only host capability query returned `S_OK`, four bytes and `HypervisorPresent=TRUE`; no Windows feature or service was changed. The explicit WHPX launch reached the guest desktop under the same resource budget. QEMU emitted `Ignoring request for interrupt vector 0`; that warning did not prevent the observed desktop and bootstrap result, but its wider impact has not been established.

Rerunning the bootstrap inside that guest emitted `environment-verified` and then `rdsh-configured`, with `installed=true`, `serviceStatus=Running`, `providerRegistered=false` and `createdSeat=false`. This verified the role and running service. The subsequent guest-local RDP reference has now produced a distinct active user logon and a running probe inside it, as detailed below; it is not an operational RETSAR provider. The native executor stays blocked and physical-seat P0 remains open. These guest observations are separate from the automated test totals and do not grant approval for another deployment.

The laboratory supervisor now permits a private root on C: or D: while monitoring free space on both C: and the selected laboratory volume. Other volumes are rejected before filesystem access. This change applies only to the disposable laboratory; the core's C-only journal and CLI output policy remains unchanged.

## Current evidence · 0.2.0-engine-preview

| Check | Latest local result | Verified public CI result (`aa8761a`) |
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
| Inert activation PowerShell checks | 66 passed | 61 passed at the linked revision |
| Inert boot-trace PowerShell checks | 66 passed | Not included at the linked revision |
| **PowerShell helper total** | **168 passed, separate from other totals** | **97 passed at the linked revision** |
| Native supervisor checks with owned helper processes | 2 passed, separate from test-suite totals | Not run |
| Guest PowerShell script syntax; no script execution | Edited helpers parsed by their inert suites | 4 scripts parsed; separate from test-suite totals |
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

**Status: service activation remains unproven; manual COM activation and trace decoding were observed in a separate control process.**

The [activation trace experiments](ACTIVATION-TRACE.md) produced four service captures: minimal registration with a service restart and a cold boot, followed by an owned listener with `fEnableWinStation=1` under the same two startup routes. Each retained two system metadata records, zero RETSAR events and `EventsLost=0`. The first service restart also produced LocalSessionManager event 17, `0x800706BA`, despite TermService reporting Running. Successful AutoLogger startup statuses were not treated as provider readiness.

A separate explicit COM control constructed the manager and emitted 31 decoded RETSAR events, all from the control process rather than TermService. The first experiment's owned AutoLogger, provider keys and DLL were removed with the standard RDP listener unchanged. The second experiment remains registered for targeted diagnosis and still needs final cleanup. The listener flag is an inference from Microsoft's RDP-Tcp troubleshooting guidance; its presence did not produce observed custom-provider activation.

A guest restart under WHPX reached virtual-processor exit 4 and QMP `guest-panic`, with process exit code zero. That exit code was not a clean-shutdown result. The stopped disk passed a read-only qcow2 structural check, which does not establish filesystem integrity. Clean guest power-off and a fresh supervised launch subsequently worked; reset reliability remains unresolved.

Current development, builds and heavy laboratory assets are on D:. Host multiseat software, host display/input assignments and unrelated volumes remain untouched. These observations do not advance physical-seat P0 to complete.

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

From an ordinary directory on C:, with the [build prerequisites](../README.md#getting-started):

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
