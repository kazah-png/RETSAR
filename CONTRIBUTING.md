# Contributing to RETSAR

RETSAR is building general-purpose Windows desktop workspaces on one computer. Contributions should improve a usable desktop experience while preserving clear ownership and honest evidence. Application-specific automation belongs outside the desktop runtime.

## Before making a change

Read the [product direction](docs/PRODUCT.md), [roadmap](docs/ROADMAP.md) and relevant component documentation. Describe the concrete trigger, current behavior and intended result. Keep changes focused enough to review independently.

The management console, native child-session laboratory and transactional core have different capability boundaries. Preserve them. The simulated provider is a test fixture, not a fallback that can be presented to users as a real desktop.

## Build and test

Use Windows x64, Visual Studio Build Tools with C++ tools, the Windows SDK and PowerShell.

To build the native console and optional child-session tools:

```powershell
.\build.ps1 -Desktop
.\dist\RETSAR-Desktop.exe --help
```

To run the complete inert build/test configuration, keep the checkout on your preferred development volume. The build places the small core lifecycle and CLI fixtures in a temporary directory on **C:** automatically, preserving their existing file policy.

```powershell
.\build.ps1 -Test -Lab -RdsLab -ChildSessionLab -Desktop
```

Code, builds, reports and laboratory assets can remain on D:. Do not weaken file-policy checks just to make a test fixture use another volume. The [Windows workflow](.github/workflows/windows.yml) shows the authoritative CI invocation and the separate Python and PowerShell checks.

Normal builds run help and inert tests; they must not connect a session, register a provider, install a driver or assign a device. Live experiments require an explicitly selected laboratory or supervised target and an exact cleanup plan. They are not part of CI.

For interface changes, inspect the actual window at supported sizes and DPI settings. A `--preview` fixture is useful for layout checks but must remain visibly labeled as sample data. Record which view and state were checked; a screenshot alone does not prove that its actions work.

## Validation to report

Include the relevant build and test commands, their results, and any failed or unexercised cases. Separate these kinds of evidence:

- **Pure or simulated tests:** supplied values, fake APIs and fixture providers.
- **Read-only observations:** current state, with no session or device mutation.
- **Live experiments:** actual effects, correlated resource identities and independently verified cleanup.
- **User experience:** the tested workflow and the user's observed result.

A successful compile, an existing session ID or a D3D device does not by itself establish a working seat. Describe performance measurements with the environment, workload and measurement scope; do not advertise unmeasured improvements.

## Documentation and provenance

Write public documentation, interface text, comments, issues and pull requests in English. Organize progress around capabilities and acceptance criteria rather than hand-maintained dates. Preserve useful version identifiers, commit hashes and evidence links.

Update [Verified progress](docs/PROGRESS.md) when evidence changes and the [roadmap](docs/ROADMAP.md) when an acceptance gate is met. Update the relevant architecture or product document when behavior changes. Keep historical failures when they explain a current limitation.

Use primary documentation and distinguish facts, inferences and hypotheses. Attribute dependencies and incorporated material under their actual licenses. The [acknowledgements](ACKNOWLEDGEMENTS.md) describe the project's research references and assistance.

## Private evidence

Never include credentials, tokens, cookies, account files, personal inventories or proprietary/decompiled components in a commit, issue or pull request. Keep raw laboratory evidence under the ignored private workspace. Share only the technical details needed to reproduce a result, with personal data removed.

Reports about sessions and devices should use test identifiers. Do not attach full process command lines or unsanitized screenshots when they may reveal authentication or account data.

## Native lifecycle changes

Keep `WindowsBlockedExecutor` blocked until its acceptance gates are met. A new path must establish which resources it owns, handle reused identifiers and preserve uncertainty after a partial failure. It must not terminate another desktop or application to hide a failed operation.

If an action changes sessions, services, drivers or devices, explain how it is admitted, observed and reversed. A connection closing is not equivalent to a user signing out. Keep unrelated workspaces intact throughout the experiment.
