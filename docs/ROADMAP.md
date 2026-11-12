# Roadmap

RETSAR aims to provide functional, general-purpose Windows desktops on one computer, with explicit session, display and input management. Browsing, productivity, development, media and automation are intended use cases, not blanket compatibility claims. A gaming/automation pilot is one validation case rather than the product definition. This roadmap uses acceptance criteria; it is not a completion percentage or a delivery schedule.

## Available foundation · 0.2.0-engine-preview

- [x] Read-only inventory and JSON output.
- [x] Configuration validation and rejection of duplicate assignments.
- [x] Temporary plans, reservations and single-use tokens.
- [x] Durable intent before acquiring or releasing resources.
- [x] Ownership receipts and compensation in reverse order.
- [x] Conservative restart recovery and uncertain-outcome handling.
- [x] Simulated provider isolated from the public executable.
- [x] Automated Windows build and tests.
- [x] Separate laboratory probe with pure argument and session-gate tests.
- [x] Optional SDK bridge with explicit mock callback and cancellation tests.
- [x] Opt-in laboratory process supervision with fake-API and owned-helper tests.

A completed foundation does not amount to a complete physical seat. Native child-desktop creation, resumption and a supervised application pilot are recorded separately in [Verified progress](PROGRESS.md).

## Desktop experience

- [x] Provide a native management console with actual desktop/display state and clear actions.
- [x] Offer guarded create/resume actions through the existing child-session client; Windows handles identity confirmation when required.
- [ ] Make keyboard focus and shortcuts reliable in the viewer.
- [ ] Make connecting, connected, disconnected and recovery states understandable.
- [ ] Validate ordinary browsing, documents and development applications separately.
- [ ] Measure idle work, connection cost, memory and sustained desktop use before setting performance targets.
- [x] Package a reproducible developer preview with a usage guide and checksums; production installation and owned-session teardown remain open.

**Acceptance criterion:** a user can open, use, pause interaction with and recover an independent desktop without guessing at its state. Completing this experience does not by itself prove physical device exclusivity.

## P0 · Demonstrate a native seat in an independent laboratory

- [x] Identify and document native child-session creation on the tested Core host, with the parent still active. Physical assignment and lifecycle acceptance remain separate.
- [ ] Demonstrate its identity and lifecycle without relying on a session already created by another product.
- [ ] Assign an independent physical output and input without taking over existing resources.
- [ ] Remove only the experiment's resources, including after intermediate failures.
- [ ] Reproduce the result and retain reviewable evidence of the environment and limitations.

**Acceptance criterion:** an isolated experiment creates and removes a real second seat, preserves the first, and proves which run owns each resource. Launching a process in another session or displaying a captured image does not satisfy this criterion.

The [native backend investigation](NATIVE-BACKEND.md) separates protocol research from this physical-seat requirement. The [observed child desktop](CHILD-SESSIONS.md) establishes one logical-session mechanism on the tested host; it does not complete P0.

## P1 · Integrate the Windows provider

- [ ] Implement the `SeatExecutor` contract with atomic ownership checks.
- [ ] Bind display and input to the exact session created by the same transaction.
- [ ] Make operations idempotent and queryable after a crash.
- [ ] Invalidate plans on external interference without invalidating the transaction's own effects.
- [ ] Pass tests for foreign resources, reused IDs, lost replies and provider restarts.

**Acceptance criterion:** the real provider satisfies the ownership and recovery invariants required by the core. Simulated tests remain useful; they do not replace real-provider tests.

## P2 · Supervised use

- [ ] Resolve keyboard/mouse selection and switching between seats.
- [ ] Add a configuration interface that reflects the engine's real state.
- [ ] Design installation, removal and updates for whatever components prove necessary.
- [ ] Test shared resources, suspension, disconnections and application failures.

**Acceptance criterion:** two seats can be prepared, started, stopped and inspected with clear recovery procedures and no hidden configuration.

## P3 · Stability and distribution

- [ ] Run prolonged tests and measure CPU, memory and GPU use.
- [ ] Document Windows versions and hardware actually tested.
- [ ] Address journal growth and maintenance without losing pending ownership.
- [ ] Review packaging, code provenance and reproducibility of releases.

Do not announce support for particular applications or games until it has been tested. Performance claims must include conditions and measurements.

## Updating this roadmap

Mark an item complete only when reviewable code or reproducible evidence meets its criterion. Record the result in [PROGRESS.md](PROGRESS.md), including skipped cases and remaining limits. A design or simulated interface must not be recorded as completed native functionality.
