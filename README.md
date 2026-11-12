<p align="center">
  <img src="assets/retsar-banner.svg" alt="RETSAR — Runtime Environment for Terminal Sharing and Allocation of Resources. Engine preview." width="100%">
</p>

<p align="center">
  <a href="https://github.com/kazah-png/RETSAR/actions/workflows/windows.yml"><img src="https://github.com/kazah-png/RETSAR/actions/workflows/windows.yml/badge.svg" alt="Windows build and tests"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-6FE7C8?style=flat-square" alt="MIT"></a>
  <img src="https://img.shields.io/badge/C%2B%2B-17-8EA8FF?style=flat-square" alt="C++17">
  <img src="https://img.shields.io/badge/status-engine_preview-F0C879?style=flat-square" alt="Engine preview">
</p>

<p align="center"><b>An open core for independent Windows seats.</b><br>
Verifiable plans · explicit ownership · persistent recovery</p>

<p align="center">
  <a href="#getting-started">Build</a> ·
  <a href="docs/ARCHITECTURE.md">Architecture</a> ·
  <a href="docs/NATIVE-BACKEND.md">Native backend research</a> ·
  <a href="docs/RDS-BRIDGE.md">SDK bridge</a> ·
  <a href="docs/RDS-INTEGRATION-GAPS.md">Integration gaps</a> ·
  <a href="lab/README.md">Isolated laboratory</a> ·
  <a href="docs/ROADMAP.md">Roadmap</a> ·
  <a href="docs/PROGRESS.md">Verified progress</a> ·
  <a href="ACKNOWLEDGEMENTS.md">Acknowledgements</a>
</p>

## Current status

**RETSAR 0.2.0-engine-preview cannot create a second real Windows seat yet.** It includes a C++17 transactional core, a durable journal and read-only diagnostics. The native provider remains blocked: interactive session creation, physical display assignment and independent input are still pending.

The goal is two independent seats on one computer, with explicit resource assignments. This preview makes the foundations of that engine available for review and testing. It is not a multiseat application ready for everyday use.

| Component | Verified status |
| --- | --- |
| Windows, display and input metadata inventory | Implemented, read-only |
| Transactional planning, reservations, startup and teardown | Implemented in the core; effects tested with a simulated provider |
| Journal, ownership receipts and restart recovery | Implemented and tested |
| Separate laboratory seat probe | Executed inside a Server RDP candidate; own-process identity and software rendering observed |
| Optional RDS SDK bridge | Mock contracts tested; manual COM control decoded; real service activation remains unproven |
| Isolated laboratory supervisor | Fake-API tests and local owned-process termination checks passed |
| Windows Server laboratory guest | Desktop booted with WHPX; bootstrap verified the RD Session Host role and running service |
| Provider activation diagnostics | Four service captures had no RETSAR events; [targeted diagnosis remains open](docs/ACTIVATION-TRACE.md) |
| Server reference observation tools | Concurrent guest logons and a manual same-boot sign-out/cleanup cycle verified; helper observation timeout remains |
| Creation of a real Windows seat | **Not implemented** |
| Independent physical display and keyboard/mouse | **Not implemented** |
| Service, drivers and management interface | Pending |

## What the core provides

- **Plan before acting.** Each plan binds configuration, reservations, expiration and provider identity. It can be consumed only once.
- **Record before changing resources.** Intent is committed to the journal before asking the provider for an effect.
- **Release only owned resources.** Receipts identify the run, operation and resource incarnation. A reused ID does not authorize cleanup.
- **Preserve uncertainty.** An uncertain operation requires reconciliation; it does not trigger a blind second creation.
- **Keep simulation explicit.** The simulated provider is compiled only into tests. The public executable reports native capabilities as pending.

## Getting started

Use Windows x64, Visual Studio Build Tools with C++ tools and the Windows SDK, and PowerShell. Run the suite from an ordinary directory on **C:**: this preview restricts journal and report files to that drive.

```powershell
.\build.ps1 -Test
.\dist\RETSAR.exe --engine-status
```

The first command compiles with C++17 and `/W4 /WX`, then runs the model, persistence, lifecycle and CLI tests. The second reports provider readiness **without inspecting devices, opening a journal or creating sessions**.

To request the read-only inventory:

```powershell
.\dist\RETSAR.exe --json
```

The report includes the Windows version, current session, visible monitors, Raw Input metadata and sessions exposed by WTS. It does not record keystrokes. Session visibility may be incomplete.

To save a report, `--output` requires a **new** file in an existing directory on C: and never overwrites one. See `--help` for the syntax. The current CLI has no commands to start seats, assign devices or install components.

## Laboratory instrument

A separate probe is available to collect evidence inside a candidate session. Compile it without opening its window:

```powershell
.\build.ps1 -Test -Lab
.\dist\lab\RETSAR-seat-probe.exe --help
```

The build runs only the probe's help and pure tests. Interactive mode requires explicit candidate and reference session IDs. It has now executed inside the disposable Server guest's RDP candidate, reporting its own process identity and a software render adapter. The probe neither creates sessions nor proves independence by observing an ID or receiving synthetic input. See its [contract and scope](docs/NATIVE-BACKEND.md#c-laboratory-instrument).

The [guest session observer](lab/SESSION-OBSERVATION.md) and [guest-local RDP reference](lab/RDP-REFERENCE.md) verified a distinct active guest logon beside the original console. The first candidate's exported probe report was validated, including its user SID matching the account receipt. After recovering an interrupted cleanup, a focused repeat produced complete before/connected/cleaned snapshots from one guest boot: the reference account was absent, active, then absent, while the original console was active at all three observations with unchanged logon time. Receipt-based cleanup and separate checks confirmed removal of the temporary account, credential and reference directory. The probe was not repeated in that cycle.

Both connection-helper runs exceeded their five-minute observation window. Manual sign-out and guarded cleanup were independently verified; normal helper completion is not claimed. These are guest-only observations of Microsoft's RDP path, not a RETSAR native-seat backend or physical-display/input isolation.

## Verification and limits

The last complete local C++/CLI baseline passed **242 checks**, including **125 mock checks** for the optional [SDK bridge](docs/RDS-BRIDGE.md). One adversarial junction branch could not run locally because Windows denied its setup; it is not counted as passed. The current supervisor suite has **46 separate inert Python tests**, and local observer/reference/activation/boot-trace helpers total **168 inert PowerShell checks**. These totals do not include or substitute for live guest evidence.

The latest verified [public Windows CI run](https://github.com/kazah-png/RETSAR/actions/runs/43265307026/job/131347162820) passed **243 C++/CLI checks, 46 separate Python tests and 97 separate inert PowerShell checks**, with no failures or skips, at [`aa8761a`](https://github.com/kazah-png/RETSAR/commit/aa8761ad26302153b45e70d0df824d4ee32d5230). It additionally parsed four guest scripts without executing them. The expanded local helper checks are newer than that revision; their next CI run is pending. Two separate local supervisor checks terminated only newly created sleeping helper processes. The [progress record](docs/PROGRESS.md) separates these scopes.

Offline guest logs confirmed that the earlier setup failed in `oobeSystem`: the generated first-logon command was 1,318 characters, exceeding Microsoft's 1,024-character limit. The template now uses a fixed 309-character launcher, covered by a regression test. The corrected installation reached the Windows Server desktop. After a clean guest shutdown and verified disk/firmware checkpoint, an explicit WHPX launch also reached the desktop. The guest bootstrap emitted `rdsh-configured`, with the role installed and `TermService` running, while reporting `providerRegistered=false` and `createdSeat=false`. The later RDP reference observation does not establish provider readiness or a physical second seat. [CommandLine constraint](https://learn.microsoft.com/en-us/windows-hardware/customize/desktop/unattend/microsoft-windows-shell-setup-firstlogoncommands-synchronouscommand-commandline).

Live validation corrected listener identity queries, an overlong temporary-account marker and ownership checks for documented AutoLogger status/default fields. Four subsequent service activation captures contained no RETSAR events; a separate manual COM control produced 31 decoded events from its own process. [Activation results and limits](docs/ACTIVATION-TRACE.md) retain those failures, the WHPX reset failure and the pending second-attempt cleanup.

The supervisor's `--installed-guest` mode omits Windows installation media when resuming an existing disk; it does not repair failed setup. The [integration gaps](docs/RDS-INTEGRATION-GAPS.md) identify the remaining service, licensing, authentication and device contracts. The laboratory requires its own resource and evaluation-license approval; it does not provide a physical second seat on the host.

Current development, builds and laboratory assets are on D:. The supervisor permits C: or D: laboratory roots with free-space checks on both C: and the selected volume. The core's journals and CLI output remain restricted to C:.

These checks do not establish physical isolation, application compatibility, prolonged stability or an operational Windows provider. The journal's CRC detects corruption; it does not authenticate the file or prevent restoring an older complete version.

[Windows CI](.github/workflows/windows.yml) rebuilds and tests the project in GitHub Actions. Its badge reports that workflow's status, not native multiseat availability.

## Contributing

The [roadmap](docs/ROADMAP.md) defines priorities and acceptance gates. Reviews of ownership and persistence, reproducible fault tests, and documented evidence for a native provider are especially useful. State what was observed, the environment used and what remains unverified.

Changes that act on sessions or devices must preserve unrelated resources and supply independent validation. Until those requirements are met, `WindowsBlockedExecutor` stays blocked. See [CONTRIBUTING.md](CONTRIBUTING.md) for the contribution process.

## License and acknowledgements

RETSAR's original code and visual assets are released under [MIT](LICENSE). Maintained by [kazah-png](https://github.com/kazah-png).

Acknowledgements include **Ghidra**, **REA**, and assistance identified by the maintainer as **GPT-6 Astra ULTRA**. [Acknowledgements and provenance](ACKNOWLEDGEMENTS.md) describe their roles, sources and licenses. They are not required dependencies for building RETSAR.
