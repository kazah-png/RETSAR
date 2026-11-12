# Opt-in remote IDD compilation

Supply an existing, legally obtained x64 Windows toolchain and review its licenses. The reference package versions are `Microsoft.Windows.WDK.x64`, `Microsoft.Windows.SDK.CPP` and `Microsoft.Windows.SDK.CPP.x64` **10.0.26100.6584**, with MSVC **14.44.35207**. The WDK package includes pre-release license terms. Those licenses are independent of MS-PL and MIT. No download or tool installation is automated.

The builder requires Python 3 on Windows. Supply these roots explicitly:

| Argument | Directory |
| --- | --- |
| `--msvc-root` | Versioned MSVC directory containing `bin/Hostx64/x64/cl.exe`, `include` and `lib/x64`. |
| `--wdk-root` | Extracted WDK package's `c` directory. |
| `--sdk-root` | Extracted SDK.CPP package's `c` directory, containing SDK headers. |
| `--sdk-x64-root` | Extracted SDK.CPP.x64 package's `c` directory, containing `um/x64` and `ucrt/x64` libraries. |
| `--out` | New output directory outside this source directory, with an existing parent. Existing destinations are refused. |
| `--driver-version` | Four version components, each 0 through 65535. |

The package directory version is `10.0.26100.0`; the selected frameworks are UMDF 2.25 and IddCx 1.4. The script intentionally supports that reviewed layout. It changes build-process environment variables only.

```text
python build.py --msvc-root <MSVC_ROOT> --wdk-root <WDK_ROOT> --sdk-root <SDK_ROOT> --sdk-x64-root <SDK_X64_ROOT> --out <NEW_PRIVATE_OUTPUT> --driver-version 0.0.1.0
```

This compiles `Driver.cpp` with C++17, `/W4`, `/MT`, `/O2`, `/guard:cf`, `/sdl` and the UMDF/IddCx stubs. It generates `RETSARRemoteIddLab.inf` using the current UTC build date and requested version, then runs WDK `InfVerif /v /u`. The source template contains no manually maintained date. Logs and `build-report.json` remain in the output directory. The compiler may report C4324 alignment warnings from the WDF header; the source is not compiled with `/WX`.

The reviewed source's private build completed compilation and universal INF validation with exit code 0. It emitted two C4324 warnings from WDF 2.25 `wdfrequest.h` and InfVerif warning 2084 for the inbox WUDFRd service binary. No driver or catalog was signed, and the DLL was not loaded. This establishes one build with the supplied reference toolchain, not an independently reproduced or runtime-validated package.

The build never executes the compiled DLL. It does not create a catalog or trust material, enable test signing, register a driver/service or stage a package. A successful compile/INF check establishes neither signing nor runtime behavior, and no reproducible-binary claim is made.

If a separate reviewed signing workflow later uses Inf2Cat, retain `WindowsProtectedFiles.xml` and its four `Microsoft.UniversalStore.HardwareWorkflow.*.dll` dependencies beside the official x86 tool. Those are licensed WDK inputs, not package contents. Driver DLL signing must precede catalog generation. Do not reuse the console package's catalog, receipt, certificate ownership or hardware ID.
