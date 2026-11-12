# RETSAR Desktop developer preview

An open-source project for general-purpose Windows desktop environments. This artifact is built from the source revision shown by its GitHub Actions run; it is a development snapshot, not a stable installer.

## Open the management console

Extract the complete archive, preserving the `lab` subdirectory, and open `RETSAR-Desktop.exe`. The normal console reads desktop/display state. It changes no session setting until an explicit Open/Resume action launches the viewer and Windows approves any required elevation.

`RETSAR.exe` is the separate read-only core CLI. `lab/RETSAR-child-session-client.exe` is the experimental native desktop viewer. `SHA256SUMS.txt` identifies the files supplied by this build.

## Before opening an independent desktop

Read the [Desktop guide](https://github.com/kazah-png/RETSAR/blob/main/docs/DESKTOP.md). The current viewer still requires the existing development receipt directory `D:\RETSAR-Lab\RETSAR\private`; it is not a general installer or an unattended service. Windows identity confirmation remains interactive.

The preview has demonstrated native child-desktop creation/resumption and a supervised application pilot on the tested host. Physical display/input exclusivity, crash recovery, broad application compatibility and prolonged stability remain in development. Closing the console leaves open desktops alone; closing the viewer disconnects, rather than logs off, its child.

For a clearly labelled interface demonstration without session or device inventory, run `RETSAR-Desktop.exe --preview`.

[Source and verified progress](https://github.com/kazah-png/RETSAR) · [MIT license](https://github.com/kazah-png/RETSAR/blob/main/LICENSE)