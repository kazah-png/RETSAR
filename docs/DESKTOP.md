# RETSAR Desktop

RETSAR Desktop is the native management interface for a general-purpose Windows desktop environment. It does not depend on Roblox, Zzen or any other application. The current development build presents observed desktop/display state and launches the separate experimental child-session viewer through explicit user actions.

## Build and open

Use Windows x64, the Visual Studio C++ Build Tools and a complete Windows SDK.

```powershell
.\build.ps1 -Desktop
.\dist\RETSAR-Desktop.exe
```

`-Desktop` also builds the child-session viewer and runs the inert desktop/child contracts. The build never opens a desktop or asks Windows to create a session. Sources and binaries can stay on D:. The complete `-Test` suite uses a small temporary directory on C: only for the core's existing journal/output path-policy fixtures.

The normal GUI reads session/display metadata asynchronously on startup, when you return to the console, and when **Refresh** or **F5** is used. Return-to-console observations are coalesced and deferred while another refresh or viewer launch is busy; they never reconnect a desktop automatically. After its own viewer launch, it also follows connection startup every 1.5 seconds for at most 90 seconds, then stops observing automatically. It retains the exact process handle while the outcome is unresolved, preventing a duplicate launch; **Refresh** can reconcile the observation later. It does not watch keys, capture application contents or poll continuously. Opening the console does not alter or close an existing desktop.

## The interface

- **Overview** shows the main and observed independent desktop, with an action derived from actual readiness.
- **Displays** shows screens visible to the current desktop. This is an observation, not an exclusive hardware assignment.
- **Devices** groups keyboard and mouse collections by observed PnP container. Missing or ambiguous identities stay incomplete; additional groups do not imply spare peripherals or assignment support. It reads metadata only, never key or pointer events. [Physical-seat work](PHYSICAL-SEAT.md).
- **Activity** shows this window's recent observations and a whole-computer memory snapshot. Memory use is not attributed to RETSAR or a particular desktop.
- **Project guide** opens this documentation in the default browser.

Buttons support keyboard navigation, the layout responds to Windows display scaling, and the window uses a retained drawing buffer rather than continuous redraw. The interface stores no application list or credential.

## Opening or resuming a desktop

The action is available only when the supplied viewer, current console context and session observations satisfy the existing child-session guards. **Open desktop** and **Resume desktop** launch the viewer; Windows may request administrator approval and identity confirmation. RETSAR neither requests credentials in its own interface nor stores them.

This preview still uses the development receipt directory:

```text
D:\RETSAR-Lab\RETSAR\private
```

That ordinary directory must already exist. An absent directory, unknown session state, an incompatible sign-in context or an active child prevents a new launch. An enabled setting is eligible for resumption only when the exact related child is disconnected and all resumption guards pass. The existing viewer independently rechecks its admission conditions before changing Windows state. The directory requirement is a development limitation, not a portable installation contract.

An exactly identified viewer built beside the console can be brought forward with **Show desktop**. A desktop created by another viewer instance may be observed as active without being controlled by this console. A visible viewer window by itself does not prove that its connection succeeded.

The viewer requests **Fit to window** on startup so the full desktop can fit in the available viewing area. Click **Actual size** in the header to use unscaled display with scrollbars when needed, or **Fit desktop** to fit it again. This uses the control's [SmartSizing property](https://learn.microsoft.com/en-us/windows/win32/termserv/imsrdpclientadvancedsettings-smartsizing); it changes client-side presentation, not the child desktop's resolution, application windows or connection. The header reports the mode read back from Windows. If that change cannot be confirmed, an explanatory message appears and the connection stays available. Current toolbar access is by clicking the button; an explicit keyboard route into the toolbar is not yet implemented.

## Matching the desktop to the viewing area

**Match screen** requests a new child-desktop resolution using the image area currently available below the toolbar. The initial connection remains configured at 1024×768. This optional action is separate from **Fit desktop**, which only scales the image. A real resolution change can rearrange application windows inside the child and increase rendering or memory cost.

Use it after choosing the viewer size or entering second-monitor presentation. The request uses explicit 100% desktop/device scaling and no rotation. Odd widths are rounded down by one pixel; dimensions outside 200–8192 or above RETSAR's 16-million-pixel limit are rejected. This limit is a preview policy, not a measured performance target.

Only one request may be pending. RETSAR requires a successful method result and a subsequent matching remote-size notification before reporting control confirmation. Property readback alone is not proof of the actual child desktop. An unsupported operation, failure or missing confirmation is reported without reconnecting or closing the desktop. The ten-second observation deadline begins around the request; it cannot cancel a blocked Windows COM call. Returning to a window, resizing it or changing DPI does not automatically request another resolution or restore the former one.

This feature uses the documented [session-display update](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/legacy/mt703457(v=vs.85)) and [remote-size event](https://learn.microsoft.com/en-us/windows/win32/termserv/imstscaxevents-onremotedesktopsizechange). Availability in the child-session mode must be verified on the actual host; the existence of the method does not guarantee support. See [Verified progress](PROGRESS.md) for the distinction between inert tests and supervised results.

On the tested host, the [supervised resolution trial](PROGRESS.md#supervised-desktop-resolution-trial) changed the existing child to 1920×1028 under the second-monitor toolbar. Independent measurements inside the child corroborated the mode; a repeated visible-counter test and user report confirmed short continued updates while the principal desktop was used. The same connection and Notepad window were retained. This does not establish prolonged stability or resource cost.

## Using an ordinary second monitor

After the desktop connects, click **Use second monitor** in the viewer. This first version requires exactly one secondary monitor and a normal, non-maximized viewer window. An ambiguous display layout or incomplete observation prevents entry. The viewer keeps a compact toolbar with **Back to window**; **Ctrl+Alt+F12** also requests the return while the viewer has focus. This is a local shortcut, not a system-wide hotkey.

The presentation changes the viewer window and keeps its existing child connection. It does not switch monitor resolution, assign the physical connector, or enable the RDP control's fullscreen mode. Window placement does not request activation. Keep the viewer visible while using the principal desktop; minimizing is a separate state and can suppress remote image updates. The host cursor can still enter that screen and host windows can overlap it.

**Back to window** restores the saved normal window. Monitor changes cause revalidation; losing the target requests restoration into an available work area. Placement and restoration are checked afterward, and an unverified change is reported rather than shown as success. Real monitor removal and DPI changes still require supervised validation. See [Standard-monitor presentation](STANDARD-MONITOR.md) for the mechanism, evidence boundaries and acceptance checks.

The viewer renders a normal Windows desktop. Open applications there as you would in any Windows session. Application compatibility must be verified individually; the [supervised pilot](CHILD-SESSION-INPUT.md) is one example, not a universal guarantee.

## Closing and recovery

When an existing, related child is disconnected and passes the admission checks, the console shows **Ready to resume** and **Resume desktop**. After that explicit click, it selects the mode matching the observed setting: ordinary resumption from a disabled baseline, or enabled-baseline resumption that preserves the already-enabled setting. Both require the current console parent, an exact current parent/child relationship, complete inventory with no other signed-in user, and a disconnected target. The viewer repeats these checks before connecting. An existing viewer still prevents duplicate opening; close a stopped viewer before returning to the console. A disabled connection setting alone is never treated as proof that a viewer stopped.

Closing the management console leaves the viewer and applications alone. Closing the experimental viewer requests disconnection of its own connection. New creation and ordinary disabled-baseline resumption attempt to restore their original disabled setting. Enabled-baseline resumption never calls the setting-change API and leaves the setting enabled, checking whether that original state was preserved. Restoration or preservation is considered verified only after successful readback and a completed cleanup receipt; interrupted cleanup remains uncertain. It does **not** log off the child or guarantee that disconnected applications keep running normally. Session termination, crash recovery and unattended reconnect are still being developed. Preserve the private receipt when reporting a connection failure, and remove personal data before sharing any excerpt.

Enabled-baseline resumption is a guarded connection path, not a repair for an earlier interrupted cleanup. It does not establish why the setting remained enabled or claim ownership of that earlier change. A supervised enabled-baseline connection returned to the existing Notepad desktop with Fit to window visibly active. The user also confirmed both sizing buttons and a subsequent close/resume cycle returning to that same Notepad window; a later inspection corroborated the first resumed viewer closing while preserving its enabled baseline, with no setting mutation or restore attempt. The sizing toggles and second resumption remain user-reported because no corresponding events were present. This is limited evidence from the tested host, not a general cleanup or stability guarantee.

The viewer distinguishes preparation, connection, Windows confirmation, sign-in, desktop verification, connected state and cleanup. A transport connection or completed sign-in alone is never shown as a verified independent desktop. Windows confirmation stays interactive; RETSAR does not enter credentials.

The earlier pilot exposed an F8 activation problem through the viewer. Activation and accelerator forwarding now retain their COM objects inside reentrancy guards, and pending activation is drained after the outer message dispatch. A supervised manual check confirmed typing and F5 date/time insertion in Notepad inside RETSAR, followed by normal typing on the principal desktop. This does not establish that F8 works in Zzen or every application. Use an application's own controls when a shortcut does not arrive. Do not replace a working connected viewer merely to update it.

The console and viewer retain Windows listener rows as metadata without asking them for a signed-in user. A listener is not a desktop. Failed identity queries on actual sessions still prevent opening; the listener exception cannot hide another signed-in user.

## Interface preview

```powershell
.\dist\RETSAR-Desktop.exe --preview
```

This renders clearly labelled sample data without collecting session or device inventory. Desktop launch and refresh actions are disabled; it is intended for interface review and screenshots. `--help` creates no window and performs no host query.

## Current limits

The child desktop shares the signed-in user's profile and files. It is not a security boundary for untrusted applications. The current logical-session path does not exclusively assign physical monitors, mice or keyboards, integrate the production transaction engine, or establish support on all Windows editions. The [product definition](PRODUCT.md), [roadmap](ROADMAP.md) and [verified progress](PROGRESS.md) distinguish the general desktop goal from observed results.