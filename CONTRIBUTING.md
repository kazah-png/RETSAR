# Contributing to RETSAR

The goal is a provider that creates independent Windows seats. The transactional engine and its simulator do not establish that capability.

## Reviewable changes

1. Explain the concrete problem and the behavior being changed.
2. Preserve the separation between the simulated provider, laboratory tools and public executable.
3. Build on Windows from a directory on C: using `.\build.ps1 -Test`, or `.\build.ps1 -Test -Lab` when changing the laboratory instrument. Report passed, failed and unexercised cases.
4. Update [PROGRESS.md](docs/PROGRESS.md) by milestone and evidence. Update [ARCHITECTURE.md](docs/ARCHITECTURE.md) or [NATIVE-BACKEND.md](docs/NATIVE-BACKEND.md) when a design decision changes.
5. Keep changes focused and reviewable. An existing session ID or a D3D device does not prove that a new seat was created.

Tests that create sessions, register providers, install drivers or assign devices require an independent laboratory. They must not run as part of the ordinary build, automated suite or read-only commands.

## Evidence and public documentation

Write public documentation, comments, issues and pull requests in English. Organize progress around milestones, observed behavior and acceptance criteria rather than dates or a reconstructed release chronology. Preserve version identifiers, commit hashes and evidence links.

Use primary documentation and distinguish facts, inferences and hypotheses. Attribute code and dependencies under their applicable licenses. Decompiled reports, proprietary components, credentials and personal inventories do not belong in this repository.

Sanitize public logs. Do not include tokens, passwords, cookies or account files in issues or pull requests. Laboratory results should use test identifiers and the technical details needed to reproduce the observation.

## Native provider changes

Keep `WindowsBlockedExecutor` blocked until the relevant acceptance gates are met. A successful compile, listener callback or simulated transaction cannot enable a native capability. Describe how the proposed provider proves ownership, preserves uncertainty and avoids affecting resources belonging to another session.
