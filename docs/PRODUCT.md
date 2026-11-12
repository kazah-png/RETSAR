# Product direction

RETSAR is a desktop runtime and management application for **general-purpose Windows workspaces on a shared computer**. Its full name is Runtime Environment for Terminal Sharing and Allocation of Resources.

The intended outcome is straightforward: one person can continue using the principal desktop while another desktop runs its own ordinary Windows applications. A complete RETSAR seat must also make its display and input assignments explicit, with reliable startup, shutdown and recovery.

## What a functional desktop means

A working session must offer a normal Windows desktop, application windows, usable text entry, pointer interaction, navigation and understandable lifecycle controls. Opening a viewer window or obtaining a session ID is only part of that experience.

RETSAR should support workflows such as browsing and communication, development and application testing, productivity, media and supervised background applications. These are **product goals**, not a compatibility list. An application is described as tested only when its specific scenario has been observed.

The Roblox/Zzen pilot is one example of application validation. Game automation, trading logic, account management and application-specific configuration belong to those applications, not to the RETSAR desktop runtime.

## Product boundaries

| RETSAR owns | Applications own | Windows owns |
| --- | --- | --- |
| Desktop discovery, connection and lifecycle presentation | Their own user interface, documents and tasks | Authentication and account identity |
| Explicit display/input resource planning and future assignment | Their own settings and application accounts | Platform session and display mechanisms |
| Receipts, diagnostics and bounded recovery | Their own performance and compatibility requirements | Operating-system security and licensing behavior |

The current native child-session path uses the parent's user profile. Shared files and startup settings therefore remain shared. This mechanism must not be described as a sandbox, a separate-account boundary or a virtual machine.

## Interface principles

- **Start with the desktop.** Overview explains what is available and what can be done next. Displays distinguishes observed monitors from assigned resources. Activity presents actionable events.
- **Show state, not assumptions.** Active, disconnected, unavailable and unknown are different states. A previous success is not a current connection.
- **Make actions deliberate.** Refresh is read-only. Opening a viewer is an explicit action. Closing a viewer must be distinguished from signing out a desktop.
- **Keep workspaces intact.** An existing viewer or application must not be terminated merely to make a new launch appear successful.
- **Use accessible controls.** Readable text, keyboard navigation, visible focus, clear status wording and DPI-aware layout matter as much as appearance.
- **Disclose sample data.** Design previews are clearly marked, do not collect session or device inventory and cannot execute desktop actions.

## Performance principles

The interface uses native Windows components and cached drawing resources. It should avoid unnecessary repainting, continuous device enumeration and idle polling. Observation work must not freeze interaction.

Optimization is evaluated against a reproducible scenario: operating-system build, display geometry, number of desktops and applications, idle and active intervals, and which process was measured. Working-set sums are not an accurate measurement of incremental memory use because processes can share pages. Report measurements with their scope; do not turn a single pilot into a general efficiency claim.

## Acceptance gates

A general-purpose desktop milestone requires evidence in each area:

1. **Session identity:** a real second desktop starts or resumes without confusing an existing session with a newly created one.
2. **Interaction:** pointer, text entry, shortcuts and application focus work inside the desktop while the principal desktop stays usable.
3. **Applications:** ordinary applications launch and complete documented tasks; incompatible or untested cases remain visible.
4. **Lifecycle:** connection loss, viewer closure, application exit, reconnect and intentional desktop shutdown have distinct, recoverable outcomes.
5. **Resources:** physical display and input assignments are explicit and exclusive where claimed; unassigned resources remain available to their current user.
6. **Operation:** prolonged runs and resource consumption are measured under stated conditions.

The [roadmap](ROADMAP.md) tracks implementation gates. The [progress record](PROGRESS.md) retains the observed evidence. Neither document should present a target behavior as a verified capability.
