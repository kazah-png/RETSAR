# Physical display and input

RETSAR can create and resume a child desktop. Assigning a physical monitor, keyboard and mouse exclusively is a separate capability and remains unavailable. This guide describes the new hardware observations and input-routing components without treating source compilation or simulated events as physical isolation.

## What the current desktop does

The viewer presents a child desktop inside a window owned by the main Windows session. Moving that window to a second monitor does not reassign the physical output. Microsoft documents one console terminal with local input/output, and physical WDDM display outputs associated with that console. Remote terminals use remote indirect displays. [Windows terminal model](https://learn.microsoft.com/en-us/windows/win32/termserv/consoles-vs-terminals).

The **Devices** page reads keyboard and mouse metadata on the same bounded refresh schedule as the console. It groups top-level input collections by PnP container and function, rather than treating every collection as a separate peripheral. Only recognized USB ancestry within the same container contributes to the verified counts. Unknown identities, changing device lists or unclassified buses remain incomplete. Paths and container identifiers are discarded before the UI receives its observations; no key values or pointer events are read.

These are keyboard-capable and mouse-capable device groups, not verified spare peripherals. A gaming mouse may expose a keyboard function, and one wireless receiver may group multiple peripherals. The preview cannot identify which device a person is currently using. Additional groups are only a hardware candidate: no device is selected, reserved or assigned. Two simultaneous physical users still need separate usable controls.

## Exclusive display eligibility

The opt-in `RETSAR-physical-display-probe` reads the public `Windows.Devices.Display.Core` API. It enumerates target identities, connectivity, usage and staleness, and requests a read-only state snapshot. It performs no acquisition, mode change, topology change, driver installation or session operation. The report contains no monitor names, serial numbers, EDID, device paths or screen pixels.

```powershell
.\build.ps1 -Desktop -PhysicalSeatLab
.\dist\lab\RETSAR-physical-display-probe.exe --status
```

Build and CI invoke only `--help` and inert tests. `--status` is a separate read-only host observation. Its exit code indicates whether required observations completed coherently, not whether a monitor is available for assignment. Unknown, stale or duplicate observations cannot authorize a candidate. When the build SDK lacks the optional indirect-adapter property, that property stays unknown.

The specialized-display API permits third-party ownership only for predesignated head-mounted or specialized targets. A standard monitor is ineligible for that route. The documented Windows UI override for ordinary monitors requires specific Windows editions; software EDID overrides are explicitly excluded. RETSAR does not change that designation or bypass those restrictions. [Custom compositor requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/specialized-monitors-compositor).

Even an eligible specialized target would establish only a candidate for a future exclusive compositor. It would not prove acquisition, frame presentation, child-session binding or keyboard/mouse isolation. A later experiment must acquire an explicitly chosen eligible target, render its own test pattern, release it, and independently verify the principal output. Child-frame transport remains separate.

## Per-device input work

Windows owns the standard keyboard and mouse HID collections. Raw Input metadata and application-level event handling cannot establish exclusive routing. Microsoft supports filter-driver extension points above the HID mapper or class drivers; the supplied PS/2 samples are not a USB deployment recipe. [Keyboard and mouse driver architecture](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/keyboard-and-mouse-hid-client-drivers).

The allocation-free routing policy in `src/lab/input-routing` is tested with synthetic events. It tracks device, broker and child incarnations, lease epochs, bounded queues and held-input ownership. Expiry, queue overflow and disconnection stop new routing. Previously routed held inputs require an explicit release/reset discipline so that half of a key or mouse-button sequence is not delivered to a different desktop. No event contents are written to disk.

The accompanying KMDF adapter is a pass-through source experiment. It has compiled and linked as an unsigned x64 kernel driver with the reviewed local WDK; a negative build check rejects enabling its routing gate. Its routing gate is disabled. It supplies no installation INF, assignment command or activation endpoint. Compilation does not install, sign, load or attach it to any input stack. There is no production broker or authenticated child receiver behind this adapter yet.

## Remaining integration gates

- Verify a supported exclusive display path on the target hardware and edition, with presentation and cleanup evidence.
- Establish physical per-device identity and explicit primary-device protection; collection counts are insufficient.
- Build and review the kernel adapter, then validate its device placement, power/removal behavior and failure recovery in the disposable laboratory.
- Implement authenticated broker/child delivery, reset acknowledgements and durable ownership before enabling interception.
- Use a separate physical keyboard/mouse pair for a simultaneous-use trial, with independent observations on both desktops.
- Preserve the main seat through interruption, device removal, reconnect and teardown; measure sustained resource use.

The source and opt-in build procedure are in [the input-routing laboratory guide](../src/lab/input-routing/README.md). The [roadmap](ROADMAP.md) remains open for physical-seat acceptance. Exact build and live-observation results belong in [Verified progress](PROGRESS.md).
