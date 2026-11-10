# Verified progress

Progress is organized by capability and evidence. A compiled component, simulated transaction or existing session ID is not recorded as an operational native seat.

## Evidence summary · 0.2.0-engine-preview

| Check | Recorded local result | Recorded public CI result |
| --- | --- | --- |
| Model, validation and capabilities | 25 passed | 25 passed |
| Lifecycle, journal and recovery | 57 passed | 58 passed |
| CLI contracts and file output | 6 passed | 6 passed |
| Laboratory probe argument/session-gate tests | 29 passed | 29 passed |
| **Total** | **117 passed, 0 failed** | **118 passed, 0 failed** |
| Independent JSON parsing by PowerShell | Passed, additional to the total | Passed, additional to the total |
| Adversarial junction branch | **1 not exercised** | Exercised and included among lifecycle passes |
| x64 C++17 build with `/W4 /WX` | Passed | Passed |
| Probe interactive mode | **Not run** | **Not run** |

Public evidence: [Windows CI run 42795998244](https://github.com/kazah-png/RETSAR/actions/runs/42795998244), commit [`27893ecee2b825c5d03d99b8f5b2180043713736`](https://github.com/kazah-png/RETSAR/commit/27893ecee2b825c5d03d99b8f5b2180043713736). These results describe that tested revision; they do not automatically validate later changes.

The local adversarial test tried to convert its own temporary directory on C: into a junction targeting another temporary directory on C:. Windows rejected the mutation with error **5 — access denied**, so the redirected-child branch was not exercised locally and is not counted as passed. CI allowed the experiment and verified the branch, accounting for the one-check difference.

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

## Milestone: native session research

**Status: candidate interfaces identified; no operational provider demonstrated.**

Static analysis identified coordination of existing sessions, communication with a driver and process launch within a specified session. The investigated indirect call resolved to an update of the active-console identifier. That path did not provide a verified contract for creating a new independent interactive session. It also does not establish that such creation is impossible through other mechanisms.

Those investigated operations were not executed and system sessions were not modified. This behavioral summary includes no proprietary binaries, decompilations or private protocols. It adds no test passes and establishes no native Windows capability.

The [native backend investigation](NATIVE-BACKEND.md) identifies a public RDS protocol-provider candidate and separates its laboratory acceptance gates from the physical-seat requirement. The remaining step is to validate a mechanism that creates a new interactive session, proves ownership, preserves the reference seat and removes only the created instance.

## Milestone: reproducible public build

**Status: public Windows CI passed at the linked revision.**

[Windows CI](../.github/workflows/windows.yml) copies the required sources, tests and build script to a verified temporary directory on C: to preserve the engine's file policy. It compiles the engine and laboratory instrument, runs pure tests and CLI checks, and invokes only the probe's help.

The SDK selection checks for the required UCRT headers, x64 libraries and manifest tool before choosing an installed version. A previous CI build exposed an incomplete SDK directory; the passing run validates the corrected selection in that environment.

The workflow badge refers to CI for its specific revision. It is not a native-seat readiness indicator.

## Reproducing the checks

From an ordinary directory on C:, with the [build prerequisites](../README.md#getting-started):

```powershell
.\build.ps1 -Test -Lab
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
