# Indirect display laboratory reference

This directory contains reviewable source for a RETSAR console virtual-display experiment in an isolated QEMU Windows Server 2022 guest. It is derived from Microsoft's IddSample at commit `d49bb7d72f3651952b2b6e4ad28d14fe2247eb69`.

The driver and helper sources were compiled privately with MSVC 14.44.35207, WDK/SDK 10.0.26100.6584, UMDF 2.25 and IddCx 1.4. That compilation is the evidence available for this source snapshot. This directory does not establish successful guest driver startup, monitor arrival, frame acquisition or cleanup. It does not provide a validated public build pipeline or claim reproducible binaries.

## Contents and provenance

| File | Purpose |
| --- | --- |
| `Driver.cpp`, `Driver.h` | IddSample adaptation with one EDID-less 1280 × 720 at 60 Hz monitor and lifecycle/frame-counter instrumentation. |
| `SwDeviceHolder.cpp`, `SwDeviceHolder.manifest` | Original RETSAR helper that creates one software device and holds its handle for at most 120 seconds before requesting removal. |
| `RETSARIddLab.inf.template` | Server build 20348 package metadata with explicit DriverVer placeholders. It is not an installable INF. |
| `source-manifest.json` | Pinned upstream revision, original hashes, adapted hashes and per-file license attribution. |
| `upstream-diff.patch` | Text diff of the adapted driver/header/INF against the pinned upstream originals; line endings are normalized for comparison. |
| `LICENSE-MS-PL.txt`, `NOTICE.txt` | Retained Microsoft license and attribution, including the boundary with the original MIT-licensed helper. |
| [BUILD.md](BUILD.md) | Toolchain roots, compiler/linker settings and limitations of the private compilation reference. |

The four C++/header/manifest files contain the same source used for the private compilation, normalized to UTF-8 without a BOM and LF line endings. The INF differs from the privately checked package in its introductory comment and replacement of DriverVer with placeholders, with the same text normalization. The manifest's `sha256` and `bytes` fields describe the normalized public files; `upstreamSha256` retains the hashes of the original acquired upstream bytes. Unused upstream sample EDID tables and production TODO comments remain to make the adaptation easy to compare.

## Intended experiment

The driver advertises a single virtual monitor. TraceLogging records lifecycle events, swapchain assignments and acquired-frame counts under provider `RETSAR.Lab.IddReference`, GUID `38e4154b-e31d-4edc-a3b7-7a139b63423c`. It does not export frame pixels or record input.

The helper requires an elevated process, computer name `RETSARLAB`, manufacturer `QEMU`, and Windows Server build `20348`. Device creation requires the explicit `--create-approved` argument. Session 0 additionally requires `--guest-management`; this records caller and active-console session IDs without asserting session ownership. The software device uses handle lifetime, and the helper observes presence/start state and waits up to another 15 seconds after closing the handle to check removal. A supervisor is still needed to bound a hung process.

These are implementation constraints, not evidence of a working seat. Even a successful helper lifecycle would not prove acquired frames, physical monitor ownership, remote-session display support, an independent login session, keyboard/mouse assignment or multiseat operation. Frame evidence would require separately reviewed driver events.

## Build and distribution boundary

Read [BUILD.md](BUILD.md) and the applicable toolchain licenses before attempting a build. The reference WDK package carries Microsoft pre-release terms. Those toolchain terms are separate from the source licenses in this directory. Toolchain acquisition and license acceptance are manual. No download, installer or automatic toolchain setup is included.

This source directory contains no compiled driver/helper, catalog, certificate, private key, toolchain binary, machine log or guest deployment script. It neither installs nor signs anything. Signing, package staging, guest boot settings, supervised execution and exact cleanup remain separate laboratory operations. Keep generated outputs outside tracked source files.

The Microsoft-derived driver, header and INF are licensed under [MS-PL](LICENSE-MS-PL.txt). The original RETSAR helper and manifest use the repository's [MIT license](../../LICENSE). See [NOTICE.txt](NOTICE.txt) for attribution.
