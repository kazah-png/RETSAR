# Verified progress

Progress is organized by capability and evidence. A compiled component, simulated transaction or existing session ID is not recorded as an operational native seat.

## SDK bridge and laboratory supervision checkpoint

The latest local C++/CLI run passed **242 checks**, including **125 mock RDS checks**. The supervisor and answer template separately passed **28 inert Python tests**. These results include the corrected optional settings-interface contract, the installed-guest launch mode, a first-logon command-length regression check, restricted C:/D: paths and checks of both relevant volumes.

The current [Windows CI run](https://github.com/kazah-png/RETSAR/actions/runs/43238991828/job/131253989959) passed at commit [`9735d585feb1c02bd0eefa66b0d36109543ea16c`](https://github.com/kazah-png/RETSAR/commit/9735d585feb1c02bd0eefa66b0d36109543ea16c): **243 C++/CLI checks and 28 separate Python tests**, with no failed or skipped checks. It covers the core, probe, optional [RDS SDK bridge](RDS-BRIDGE.md) and [laboratory supervisor](../lab/README.md) at that revision. The table below separates the evidence by scope.

The bridge tests load the explicit DLL and fake callback objects; they do not register COM classes or call an RDS service. The Python suite uses fake Windows API calls and inert configuration/template checks. Separate local native checks verified termination and kill-on-job-close with two newly created sleeping Python helpers. Those native process checks were not part of CI and did not create a VM or Windows session.

Review corrected the required readiness notification order, serialized outgoing cancellation, preserved original cancellation failures, and retained early QEMU startup errors in the laboratory log.

The narrow settings fix accepts a null optional `IWRdsProtocolSettings` interface and still rejects a null required `WRDS_SETTINGS` structure. Sixteen added mock checks cover reference retention, replacement, rejection and release. The structure's service policy is still not interpreted; successful reference management is not complete RDS initialization. The [integration-gaps review](RDS-INTEGRATION-GAPS.md) records the remaining contracts.

The separate guest experiment reached Windows Setup but failed during `oobeSystem`. Read-only inspection of the guest logs identified `FirstLogonCommands/SynchronousCommand/CommandLine` as invalid with error `0x80220005`. Its generated command contained 1,318 characters, exceeding the documented limit of 1,024. The fixed template now contains a 309-character launcher and no encoded-command placeholder; a regression test checks the constraint. [Microsoft CommandLine documentation](https://learn.microsoft.com/en-us/windows-hardware/customize/desktop/unattend/microsoft-windows-shell-setup-firstlogoncommands-synchronouscommand-commandline).

The qcow2 structural check passed, but that did not establish successful Windows setup or repair the invalid answer file. Laboratory asset relocation was verified before starting a fresh corrected installation from a private root on D:, under the same 4 GiB RAM and two-vCPU budget. That installation is in progress. Successful installation, RDS configuration, provider readiness and real session creation remain unverified. The native executor stays blocked and P0 remains open.

The laboratory supervisor now permits a private root on C: or D: while monitoring free space on both C: and the selected laboratory volume. Other volumes are rejected before filesystem access. This change applies only to the disposable laboratory; the core's C-only journal and CLI output policy remains unchanged.

## Current evidence · 0.2.0-engine-preview

| Check | Latest local result | Current public CI result (`9735d58`) |
| --- | --- | --- |
| Model, validation and capabilities | 25 passed | 25 passed |
| Lifecycle, journal and recovery | 57 passed | 58 passed |
| CLI contracts and file output | 6 passed | 6 passed |
| Laboratory probe argument/session-gate tests | 29 passed | 29 passed |
| RDS bridge mock callback contracts | 125 passed | 125 passed |
| **C++/CLI total** | **242 passed, 0 failed** | **243 passed, 0 failed, 0 skipped** |
| Python supervisor and answer-template tests; no guest execution | 28 passed, separate from C++/CLI total | 28 passed, 0 failed, 0 skipped; separate from C++/CLI total |
| Native supervisor checks with owned helper processes | 2 passed, separate from test-suite totals | Not run |
| Guest PowerShell script syntax; no script execution | Passed | 3 scripts parsed; separate from test-suite totals |
| Independent JSON parsing by PowerShell | Passed, additional to the total | Passed, additional to the total |
| Adversarial junction branch | **1 not exercised** | Exercised and included among lifecycle passes |
| x64 C++17 build with `/W4 /WX` | Passed | Passed |
| Probe interactive mode | **Not run** | **Not run** |

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

**Status: instrument compiled; pure tests and help verified; interactive execution pending.**

`RETSAR-seat-probe` is separate from the main executable. Its 29 pure checks cover arguments and the gate comparing the declared candidate and reference sessions. The build invokes only its help; it has not opened the probe window or exercised D3D11 creation in a laboratory session.

The instrument declares `createdSession=false` and `independenceNotProven=true`. It requires manual window activation and counts messages without treating their presence as proof of origin. Its design and limits are documented in [NATIVE-BACKEND.md](NATIVE-BACKEND.md#c-laboratory-instrument).

Compiling the probe does not validate its interactive window, graphics device, event routing or real-session report.

## Milestone: Server reference experiment

**Status: guest tools prepared; real connection not yet executed.**

The [read-only session observer](../lab/SESSION-OBSERVATION.md) records bounded WTS snapshots with user, state and logon/connection times. Its PowerShell syntax, extracted C# compilation, native layouts and synthetic timestamp conversions were checked without invoking WTS on the host. These checks are separate from the suite totals above.

The [guest-local RDP reference](../lab/RDP-REFERENCE.md) prepares a dedicated temporary account and launches Microsoft's client inside the guest console. It preserves Windows authentication and licensing decisions. It does not close sessions by numeric ID or claim a new session when a client window opens. Guest execution and cleanup behavior remain unverified.

The C++ probe now records its own user SID, authentication LUID, PID and process creation time after the existing session gate. Compilation and help passed; interactive identity collection still requires guest evidence. The reference experiment can validate observation and recovery, not physical multiseat on the host.

## Milestone: native session research

**Status: candidate interfaces identified; no operational provider demonstrated.**

Static analysis identified coordination of existing sessions, communication with a driver and process launch within a specified session. The investigated indirect call resolved to an update of the active-console identifier. That path did not provide a verified contract for creating a new independent interactive session. It also does not establish that such creation is impossible through other mechanisms.

Those investigated operations were not executed and system sessions were not modified. This behavioral summary includes no proprietary binaries, decompilations or private protocols. It adds no test passes and establishes no native Windows capability.

The [native backend investigation](NATIVE-BACKEND.md) identifies a public RDS protocol-provider candidate and separates its laboratory acceptance gates from the physical-seat requirement. The [integration-gaps review](RDS-INTEGRATION-GAPS.md) explains why the partial bridge cannot yet create a supported first session. The remaining step is to validate a mechanism that creates a new interactive session, proves ownership, preserves the reference seat and removes only the created instance.

## Milestone: reproducible public build

**Status: public Windows CI passed at the linked revision.**

[Windows CI](../.github/workflows/windows.yml) copies the required sources, tests, laboratory files and build script to a verified temporary directory on C: to preserve the engine's file policy. It compiles the engine, laboratory instrument and optional SDK bridge, then runs pure tests, mock callback checks, CLI checks and the Python fake-API suite. It invokes only the probe's help and does not launch a VM, execute the guest bootstrap or run the native helper-process smoke test.

The SDK selection checks for the required UCRT headers, x64 libraries and manifest tool before choosing an installed version. A previous CI build exposed an incomplete SDK directory; the passing run validates the corrected selection in that environment.

The workflow badge refers to CI for its specific revision. It is not a native-seat readiness indicator.

## Reproducing the checks

From an ordinary directory on C:, with the [build prerequisites](../README.md#getting-started):

```powershell
.\build.ps1 -Test -Lab -RdsLab
python -B -m unittest discover -s lab -p test_supervise_qemu.py -v
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
