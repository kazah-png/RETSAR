# RETSAR Desktop

RETSAR Desktop is the native management interface for a general-purpose Windows desktop environment. It does not depend on Roblox, Zzen or any other application. The current development build presents observed desktop/display state and launches the separate experimental child-session viewer through explicit user actions.

## Build and open

Use Windows x64, the Visual Studio C++ Build Tools and a complete Windows SDK.

```powershell
.\build.ps1 -Desktop
.\dist\RETSAR-Desktop.exe
```

`-Desktop` also builds the child-session viewer and runs the inert desktop/child contracts. The build never opens a desktop or asks Windows to create a session. Sources and binaries can stay on D:. The complete `-Test` suite uses a small temporary directory on C: only for the core's existing journal/output path-policy fixtures.

The normal GUI reads session/display metadata asynchronously on startup and when **Refresh** or **F5** is used. It does not watch keys, capture application contents or poll continuously. Opening the console does not alter or close an existing desktop.

## The interface

- **Overview** shows the main and observed independent desktop, with an action derived from actual readiness.
- **Displays** shows screens visible to the current desktop. This is an observation, not an exclusive hardware assignment.
- **Activity** shows this window's recent observations and a whole-computer memory snapshot. Memory use is not attributed to RETSAR or a particular desktop.
- **Project guide** opens this documentation in the default browser.

Buttons support keyboard navigation, the layout responds to Windows display scaling, and the window uses a retained drawing buffer rather than continuous redraw. The interface stores no application list or credential.

## Opening or resuming a desktop

The action is available only when the supplied viewer, current console context and session observations satisfy the existing child-session guards. **Open desktop** and **Resume desktop** launch the viewer; Windows may request administrator approval and identity confirmation. RETSAR neither requests credentials in its own interface nor stores them.

This preview still uses the development receipt directory:

```text
D:\RETSAR-Lab\RETSAR\private
```

That ordinary directory must already exist. An absent directory, unknown session state, an incompatible sign-in context or an already-enabled connection prevents a duplicate launch. The existing viewer independently rechecks its admission conditions before changing Windows state. The directory requirement is a development limitation, not a portable installation contract.

An exactly identified viewer built beside the console can be brought forward with **Show desktop**. A desktop created by another viewer instance may be observed as active without being controlled by this console. A visible viewer window by itself does not prove that its connection succeeded.

The viewer renders a normal Windows desktop. Open applications there as you would in any Windows session. Application compatibility must be verified individually; the [supervised pilot](CHILD-SESSION-INPUT.md) is one example, not a universal guarantee.

## Closing and recovery

Closing the management console leaves the viewer and applications alone. Closing the experimental viewer disconnects its own connection and restores its original child-session setting; it does **not** log off the child or guarantee that disconnected applications keep running normally. Session termination, crash recovery and unattended reconnect are still being developed. Preserve the private receipt when reporting a connection failure, and remove personal data before sharing any excerpt.

The earlier pilot exposed an F8 activation problem through the viewer. Use the application's own controls when a shortcut does not arrive. The updated viewer's focus notifications still require a dedicated live reconnect test before the shortcut can be called fixed. Do not replace a working connected viewer merely to update its appearance.

## Interface preview

```powershell
.\dist\RETSAR-Desktop.exe --preview
```

This renders clearly labelled sample data without collecting session or device inventory. Desktop launch and refresh actions are disabled; it is intended for interface review and screenshots. `--help` creates no window and performs no host query.

## Current limits

The child desktop shares the signed-in user's profile and files. It is not a security boundary for untrusted applications. The current logical-session path does not exclusively assign physical monitors, mice or keyboards, integrate the production transaction engine, or establish support on all Windows editions. The [product definition](PRODUCT.md), [roadmap](ROADMAP.md) and [verified progress](PROGRESS.md) distinguish the general desktop goal from observed results.