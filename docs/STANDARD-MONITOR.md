# Standard-monitor presentation

RETSAR's selected route for ordinary monitors combines an existing native child desktop with a borderless viewer presented by the main Windows desktop compositor. The child remains a real, separate logical session; the physical screen remains part of the main console. This is a presentation mode, not exclusive monitor assignment or a completed physical seat.

The earlier child-desktop pilot established a logical session and supervised application use. A short supervised trial now verifies presentation on the secondary monitor, visible counter progression during principal-desktop use, and return/re-entry retaining the same child. Monitor removal, DPI changes and prolonged operation remain untested. Neither that earlier pilot nor source compilation establishes compatibility, continuous operation or physical device isolation. See [child-session observations](CHILD-SESSIONS.md) and [physical-seat limits](PHYSICAL-SEAT.md).

## Selected behavior

The mode is deliberately scoped to a main display and exactly one secondary monitor. An explicit viewer action selects that secondary monitor. Missing, ambiguous or incomplete monitor observations must leave the action unavailable; the mode must not silently cover the main display.

The viewer saves its window state, removes its outer frame and fits its own window to the selected monitor. A visible native control restores the ordinary window. The existing client-side sizing control continues to determine how the child image fits the viewer. Windows documents monitor-based window placement and requires applications to revalidate saved placement after a monitor moves or disappears. [Multiple-monitor window placement](https://learn.microsoft.com/en-us/windows/win32/gdi/positioning-objects-on-multiple-display-monitors).

Entering or leaving presentation changes only the owned viewer window. Those actions do not change monitor resolution or desktop topology, acquire a display target, create a new session, reconnect the child, move applications inside it, or enable the RDP control's `FullScreen` property. Client-side `SmartSizing` can be changed while connected; it scales the control's image rather than assigning hardware. [SmartSizing contract](https://learn.microsoft.com/en-us/windows/win32/termserv/imsrdpclientadvancedsettings-smartsizing).

Placement must use a non-activating window operation. Remaining visible after the user returns to the main desktop does not require repeatedly requesting foreground focus. `SetWindowPos` provides `SWP_NOACTIVATE` and documents the separate topmost behavior. Neither option grants exclusive display ownership. [SetWindowPos contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos).

A topology change must end or revalidate presentation safely. If the selected monitor disappears, the viewer must return to an accessible ordinary window without changing the child connection. Failed placement or restoration must remain a reported error, not a successful presentation result. Mode transitions must retain the viewer's existing protections against reentrant window and COM callbacks.

## Optional child-desktop resolution

The separate **Match screen** action requests the image viewport's dimensions through `IMsRdpClient9::UpdateSessionDisplaySettings`. It does not resize the host monitor or acquire its connector. When applied, this request changes the child desktop itself, unlike SmartSizing; its applications can move or redraw. No request is generated automatically on presentation, return, window resize or DPI change.

The request uses 100% desktop/device scale, zero rotation and unspecified physical millimeters. Width is even and dimensions are bounded by the [display-control protocol](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpedisp/ea2de591-9203-42cd-9908-be7a55237d1c); RETSAR additionally limits a request to 16 million pixels. A successful return plus a subsequent matching size event is control-level confirmation. DesktopWidth/Height property readback is recorded separately and is not independent session evidence. A bounded passive worker can corroborate the current dimensions from inside the guarded child through [EnumDisplaySettingsEx](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumdisplaysettingsexw); those measurements do not certify delivered image frames or physical isolation.

Unsupported, failed or unconfirmed requests leave the existing connection available. The observation deadline is not cancellation of an unresponsive COM method. Live child-mode support, changed rendering cost and application behavior require supervised evidence; successful fake-interface tests alone are insufficient.

## One keyboard and mouse

A practical pilot is to leave an application or automation running inside the child desktop, display it on the second monitor, and use the existing keyboard and mouse on the main desktop. RETSAR has already observed this logical-session workflow in an earlier supervised pilot; the short secondary-monitor trial also verified visible updates during principal-desktop use.

This arrangement does not create a second physical cursor or reserve the monitor against the host. The host pointer can enter the second screen, host windows can overlap it, and clicking the viewer can transfer interaction to the child. Window presentation must not introduce a global cursor constraint, keyboard hook or focus-stealing loop.

Two people using separate physical desktops simultaneously require separate usable controls and a validated per-device routing and recovery path. A single keyboard/mouse pair can be shared by switching interaction; it cannot become two independent physical pairs. The current [input-routing laboratory](../src/lab/input-routing/README.md) is not an active routing backend.

## Why this route

| Approach | What it provides | Limitation for this milestone |
| --- | --- | --- |
| Host-composited borderless viewer | Presents the existing child desktop on an ordinary secondary screen through the normal Windows window system. | The host retains the output and physical input. This is the selected practical presentation route. |
| DXGI exclusive fullscreen | A swap chain can request fullscreen presentation. | Missing keyboard focus, occlusion or another fullscreen application can prevent the transition; Windows can subsequently change the state. It is not a durable second-seat assignment. |
| Specialized `Display.Core` compositor | A supported compositor can acquire an eligible specialized or head-mounted target. | Ordinary monitors are not eligible without the documented specialized-display designation. This is a different backend with hardware and edition constraints. |
| Indirect display driver | Supplies indirect or virtual monitors and processes desktop surfaces. | It does not reassign an existing GPU connector from the console to a child session. A remote IDD also needs an OS-created remote-session device. |

Microsoft documents the focus and state limits of [DXGI fullscreen](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-setfullscreenstate); applications must not call [TakeOwnership](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgioutput-takeownership) directly as a shortcut. The [specialized-compositor requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/specialized-monitors-compositor) constrain target acquisition and exclude software EDID overrides as a designation mechanism.

The [IDD model](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/indirect-display-driver-model-overview) covers virtual, remote and indirect hardware outputs. Its [remote-session requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/iddcx1.4-updates-for-remote-idds) cannot be met by relabeling a console device. Windows associates local WDDM outputs with the console terminal; presenting a child image on one does not change that association. [Console and remote terminal model](https://learn.microsoft.com/en-us/windows/win32/termserv/consoles-vs-terminals).

## Acceptance coverage

A supervised test must establish all of the following, keeping results separate from pure geometry and lifecycle tests:

1. The explicit presentation action affects only the uniquely observed secondary monitor. Its child desktop and connection identity are the same before and after entry.
2. A visible counter or clock inside the child continues updating after the user returns focus to the main desktop. A screenshot of a static image is insufficient.
3. The main desktop remains usable for typing, pointer movement and ordinary application interaction. RETSAR does not repeatedly reactivate its viewer.
4. The native restore control returns the viewer to an accessible ordinary window, with the original child applications still present and the same connection unchanged.
5. Monitor removal, rearrangement, DPI changes and failed window operations produce verified restoration or an explicit unresolved error. They do not trigger a reconnect, logoff, display reconfiguration or an unnoticed fullscreen window on the main display.
6. Repeated entry and exit preserve the same child session and restore the viewer's saved window state. No physical ownership or independent input claim is inferred from successful presentation.

The short supervised trial covers entry, visible updates with principal-desktop use, and return/re-entry on the observed two-monitor setup. The fault, monitor-removal and DPI scenarios remain pure-test or review evidence only. Exact counts and evidence boundaries are recorded in [Verified progress](PROGRESS.md#supervised-standard-monitor-trial). No resource-use, latency, endurance or broad compatibility result is claimed. Physical-output assignment and independent keyboard/mouse acceptance remain open on the [roadmap](ROADMAP.md).
