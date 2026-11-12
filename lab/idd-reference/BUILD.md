# Private compilation reference

This records the toolchain layout and options used to compile the source snapshot privately. The public C++/header/manifest files contain the same source with UTF-8 without BOM and LF normalization. There is no public build script or independently validated rebuild here. Reconstructing the commands below is an opt-in developer task; the repository does not download or install tools, generate trust material, or execute the resulting helper.

## Toolchain inputs

Supply your own legally obtained, licensed x64 Windows toolchain. The recorded versions are:

| Input | Recorded version | Root used below |
| --- | --- | --- |
| MSVC C++ build tools | 14.44.35207 | `MSVC_ROOT`: the versioned MSVC directory containing `bin/Hostx64/x64/cl.exe`, `include` and `lib/x64`. |
| `Microsoft.Windows.WDK.x64` | 10.0.26100.6584 | `WDK_ROOT`: the package's extracted `c` directory. |
| `Microsoft.Windows.SDK.CPP` | 10.0.26100.6584 | `SDK_ROOT`: the package's extracted `c` directory containing SDK headers. |
| `Microsoft.Windows.SDK.CPP.x64` | 10.0.26100.6584 | `SDK_X64_ROOT`: the package's extracted `c` directory containing x64 SDK libraries. |

The recorded WDK package is accompanied by Microsoft pre-release software license terms. The SDK and compiler have their own terms. Review the terms accompanying the exact packages you obtain; the MS-PL source license does not license those tools. None of the packages, headers, libraries or license-acceptance steps is redistributed or automated here.

The package version is `10.0.26100.6584`, but its include/library/tool directory version is `10.0.26100.0`. The private build used UMDF `2.25` and IddCx `1.4`. It did not use Spectre-mitigated libraries.

Choose a separate writable output directory `OUT` and temporary directory `TEMP_ROOT`; `SOURCE` denotes this source directory. Quote every expanded path. Set the following environment only in the build process, without changing machine-wide settings:

- Prepend `MSVC_ROOT/bin/Hostx64/x64` to `PATH`; the helper manifest build also used `SDK_ROOT/bin/10.0.26100.0/x64`.
- Set `TEMP` and `TMP` to `TEMP_ROOT`.
- Set `INCLUDE` and `LIB` to the semicolon-separated paths below, in their listed order.
- Use `OUT` as the compiler working directory so intermediate link outputs do not enter the source directory.

## Driver compilation

`INCLUDE`:

```text
MSVC_ROOT/include
WDK_ROOT/Include/wdf/umdf/2.25
WDK_ROOT/Include/10.0.26100.0/um/iddcx/1.4
WDK_ROOT/Include/10.0.26100.0/shared
SDK_ROOT/Include/10.0.26100.0/shared
SDK_ROOT/Include/10.0.26100.0/um
SDK_ROOT/Include/10.0.26100.0/winrt
SDK_ROOT/Include/10.0.26100.0/ucrt
```

`LIB`:

```text
MSVC_ROOT/lib/x64
SDK_X64_ROOT/um/x64
SDK_X64_ROOT/ucrt/x64
WDK_ROOT/Lib/wdf/umdf/x64/2.25
WDK_ROOT/Lib/10.0.26100.0/um/x64/iddcx/1.4
```

Invoke `MSVC_ROOT/bin/Hostx64/x64/cl.exe` with these options, replacing and quoting the symbolic paths:

```text
/nologo /std:c++17 /W4 /EHsc /MT /O2 /LD /guard:cf /sdl
/D_WIN32_WINNT=0x0A00 /DNTDDI_VERSION=0x0A000008
/D_WIN64 /D_AMD64_ /DAMD64 /DUNICODE /D_UNICODE
/DUMDF_DRIVER /DUMDF_VERSION_MAJOR=2 /DUMDF_VERSION_MINOR=25 /DUMDF_USING_NTSTATUS
/DIDDCX_VERSION_MAJOR=1 /DIDDCX_VERSION_MINOR=4 /DIDDCX_MINIMUM_VERSION_REQUIRED=4
SOURCE/Driver.cpp
/FoOUT/Driver.obj /FeOUT/RETSARIddLab.dll
/link /SUBSYSTEM:WINDOWS,10.00 /DYNAMICBASE /NXCOMPAT /guard:cf
WdfDriverStubUm.lib IddCxStub.lib OneCoreUAP.lib ntdll.lib avrt.lib
d3d11.lib dxgi.lib advapi32.lib
```

The private compiler returned success and emitted two C4324 warnings from the WDF 2.25 `wdfrequest.h` header. Driver warnings were not treated as errors. This is compilation evidence, not a production qualification or runtime result.

## Holder compilation

Use these `INCLUDE` paths:

```text
MSVC_ROOT/include
SDK_ROOT/Include/10.0.26100.0/shared
SDK_ROOT/Include/10.0.26100.0/um
SDK_ROOT/Include/10.0.26100.0/ucrt
```

Use `MSVC_ROOT/lib/x64`, `SDK_X64_ROOT/um/x64` and `SDK_X64_ROOT/ucrt/x64` for `LIB`. Compile with:

```text
/nologo /std:c++17 /W4 /WX /EHsc /MT /O2 /guard:cf /sdl
/D_WIN32_WINNT=0x0A00 /DNTDDI_VERSION=0x0A000008 /DUNICODE /D_UNICODE
SOURCE/SwDeviceHolder.cpp
/FoOUT/SwDeviceHolder.obj /FeOUT/RETSARIddHolder.exe
/link /SUBSYSTEM:CONSOLE,10.00 /DYNAMICBASE /NXCOMPAT /guard:cf
/MANIFEST:EMBED /MANIFESTINPUT:SOURCE/SwDeviceHolder.manifest
Cfgmgr32.lib Swdevice.lib Advapi32.lib Ole32.lib Kernel32.lib
```

The private helper build succeeded with warnings treated as errors. Nine private command-line checks exercised help and refusal paths only. None of those checks requested software-device creation.

## INF and further validation

Copy `RETSARIddLab.inf.template` to an output-only `RETSARIddLab.inf` and replace `@DRIVER_DATE@` and `@DRIVER_VERSION@` using explicitly selected build metadata. Generate the date with the packaging process in INF `MM/DD/YYYY` format; do not commit a manually maintained date into this template. The privately checked version was `0.0.1.0`. An unresolved placeholder must prevent packaging.

The private packaging checks used the WDK tools at `tools/10.0.26100.0/x64/infverif.exe` with `/v /u` and `bin/10.0.26100.0/x86/Inf2Cat.exe` with `/os:ServerFE_X64`. Keep `WindowsProtectedFiles.xml` beside Inf2Cat, together with the four `Microsoft.UniversalStore.HardwareWorkflow.*.dll` files supplied in that directory. Inf2Cat depends on this XML at runtime; copying only the executable and DLLs is insufficient. The recorded x86 tool directory contains no additional `.config` or XML files. These are licensed WDK inputs, not files distributed in this source directory.

Both private packaging tools returned success for the private populated INF when run from the supplied WDK layout. InfVerif reported warning 2084 because the inbox `WUDFRd` service binary is not a package CopyFiles destination. Those checks do not apply automatically to a newly populated INF or rebuilt DLL.

No catalog, signing operation, driver installation or execution procedure is supplied here. Those require a separately reviewed guest experiment and are outside this source snapshot. A different SDK/WDK/compiler combination or guest build needs its own compilation, package and runtime validation.
