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
| Separate laboratory seat probe | Compiled; pure argument/gate tests and help verified |
| Optional RDS SDK bridge | Compiled; in-process mock callback contracts tested; not connected to RDS |
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

The build runs only the probe's help and pure tests. Interactive mode requires explicit candidate and reference session IDs. **Interactive mode has not been tested in a laboratory seat.** The probe neither creates sessions nor proves independence by observing an ID. See its [contract and scope](docs/NATIVE-BACKEND.md#c-laboratory-instrument).

## Verification and limits

The optional [SDK bridge](docs/RDS-BRIDGE.md) now implements the factory, manager, listener and connection contracts, including delayed readiness and cancellation. Build it with `build.ps1 -Test -Lab -RdsLab`. Its **109 local mock checks** do not exercise a real RDS service. The combined C++/CLI local run passed **226 checks**, with the same one unexercised junction branch described below.

The [laboratory supervisor](lab/README.md) has separate fake-API tests and an explicit owned-process smoke test. It is for a disposable guest, with its own resource and evaluation-license approval. It is not a way to create a physical second seat on the host.

The earlier probe/core baseline passed **117 local checks**, plus independent JSON validation, with **one adversarial branch not exercised**. Its public CI run passed **118 checks** because it also exercised the junction branch. Those baseline results do not validate the newly added bridge or supervisor. The [progress record](docs/PROGRESS.md) links each tested revision and explains the difference.

These checks do not establish physical isolation, application compatibility, prolonged stability or an operational Windows provider. The journal's CRC detects corruption; it does not authenticate the file or prevent restoring an older complete version.

[Windows CI](.github/workflows/windows.yml) rebuilds and tests the project in GitHub Actions. Its badge reports that workflow's status, not native multiseat availability.

## Contributing

The [roadmap](docs/ROADMAP.md) defines priorities and acceptance gates. Reviews of ownership and persistence, reproducible fault tests, and documented evidence for a native provider are especially useful. State what was observed, the environment used and what remains unverified.

Changes that act on sessions or devices must preserve unrelated resources and supply independent validation. Until those requirements are met, `WindowsBlockedExecutor` stays blocked. See [CONTRIBUTING.md](CONTRIBUTING.md) for the contribution process.

## License and acknowledgements

RETSAR's original code and visual assets are released under [MIT](LICENSE). Maintained by [kazah-png](https://github.com/kazah-png).

Acknowledgements include **Ghidra**, **REA**, and assistance identified by the maintainer as **GPT-6 Astra ULTRA**. [Acknowledgements and provenance](ACKNOWLEDGEMENTS.md) describe their roles, sources and licenses. They are not required dependencies for building RETSAR.
