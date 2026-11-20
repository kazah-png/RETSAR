# RETSAR Desktop developer preview

An open-source project for general-purpose Windows desktop environments. This artifact is built from the source revision shown by its GitHub Actions run; it is a development snapshot, not a stable installer.

## Open the management console

Extract the complete archive, preserving the `lab` subdirectory, and open `RETSAR-Desktop.exe`. The normal console reads desktop/display state. It changes no session setting until an explicit Open/Resume action launches the viewer and Windows approves any required elevation.

`RETSAR.exe` is the separate read-only core CLI. `lab/RETSAR-child-session-client.exe` is the experimental native desktop viewer. `SHA256SUMS.txt` identifies the files supplied by this build.

## Before opening an independent desktop

Read the [Desktop guide](https://github.com/kazah-png/RETSAR/blob/main/docs/DESKTOP.md). The current viewer still requires the existing development receipt directory `D:\RETSAR-Lab\RETSAR\private`; it is not a general installer or an unattended service. Windows identity confirmation remains interactive.

The preview has demonstrated native child-desktop creation/resumption and a supervised application pilot on the tested host. Physical display/input exclusivity, crash recovery, broad application compatibility and prolonged stability remain in development. Closing the console leaves open desktops alone; closing the viewer disconnects, rather than logs off, its child.

For an eligible disconnected child, **Resume desktop** chooses the mode matching the observed original setting. Ordinary resumption attempts to restore a disabled baseline on close; enabled-baseline resumption preserves the enabled setting without changing it. A completed cleanup receipt and readback are required to verify either outcome. This is not a guarantee of crash recovery or completion of an earlier interrupted cleanup.

A supervised enabled-baseline trial resumed the existing Notepad desktop with Fit to window visibly active. The user confirmed the sizing buttons and an additional close/resume cycle retaining Notepad; a later receipt inspection corroborated the first resumed viewer closing while preserving its enabled baseline without a setting mutation or restore attempt. No sizing-toggle or second-resumption event was present; those manual actions remain user-reported. This does not establish prolonged stability or compatibility on other hosts.

For a clearly labelled interface demonstration without session or device inventory, run `RETSAR-Desktop.exe --preview`.

[Source and verified progress](https://github.com/kazah-png/RETSAR) · [MIT license](https://github.com/kazah-png/RETSAR/blob/main/LICENSE)

## Physical-resource observations

The console includes a **Devices** page with anonymous keyboard/mouse group counts. It does not assign or intercept devices. The package also includes `lab/RETSAR-physical-display-probe.exe`; `--status` reads exclusive-output eligibility without acquiring a display. Normal monitors are ineligible for that specialized-display route. Kernel input-adapter binaries are deliberately excluded from this package: the source-only laboratory component is not installed or enabled by opening the preview. [Physical-seat scope](https://github.com/kazah-png/RETSAR/blob/main/docs/PHYSICAL-SEAT.md).
