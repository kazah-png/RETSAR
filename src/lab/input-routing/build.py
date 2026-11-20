"""Opt-in synthetic tests and unsigned, inert KMDF source build. Never installs or loads anything."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys


def existing_root(value):
    try:
        path = Path(value).resolve(strict=True)
    except OSError as error:
        raise argparse.ArgumentTypeError(str(error)) from error
    if not path.is_dir():
        raise argparse.ArgumentTypeError("Toolchain root must be an existing directory")
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("msvc-root", "wdk-root", "sdk-root", "sdk-x64-root"):
        parser.add_argument("--" + name, required=True, type=existing_root)
    parser.add_argument("--out", required=True, type=Path, help="New output directory; parent must exist")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("This source build requires the supplied Windows toolchain")
    source = Path(__file__).resolve().parent
    repo = source.parents[2]
    output = args.out.resolve()
    if output.exists() or output == source or source in output.parents or not output.parent.is_dir():
        parser.error("Output must be a new directory outside this source directory, with an existing parent")
    msvc, wdk, sdk, sdk64 = args.msvc_root, args.wdk_root, args.sdk_root, args.sdk_x64_root
    version = "10.0.26100.0"
    compiler = msvc / "bin/Hostx64/x64/cl.exe"
    linker = compiler.with_name("link.exe")
    kernel_include = [wdk / "Include" / version / "km", wdk / "Include" / version / "km/crt",
                      wdk / "Include/wdf/kmdf/1.35", sdk / "Include" / version / "shared"]
    user_include = [msvc / "include"] + [sdk / "Include" / version / part for part in ("ucrt", "shared", "um")]
    kernel_lib = [wdk / "Lib" / version / "km/x64", wdk / "Lib/wdf/kmdf/x64/1.35"]
    user_lib = [msvc / "lib/x64", sdk64 / "ucrt/x64", sdk64 / "um/x64"]
    test_source = repo / "tests/physical_input_router_tests.cpp"
    inputs = [source / "physical_input_router.hpp", source / "kmdf_passthrough.cpp", test_source, Path(__file__).resolve()]
    for path in [compiler, linker] + inputs:
        if not path.is_file():
            parser.error("Required build input is missing: " + str(path))
    for path in kernel_include + user_include + kernel_lib + user_lib:
        if not path.is_dir():
            parser.error("Required toolchain directory is missing: " + str(path))
    output.mkdir()  # All admission and source/toolchain checks precede mutation.
    temporary = output / "temp"
    temporary.mkdir()
    env = os.environ.copy()
    env["TEMP"] = env["TMP"] = str(temporary)
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    report = {"schema": "retsar.physical-input-lab-build.v1", "completed": False,
              "signed": False, "installed": False, "loaded": False, "runtimeTested": False,
              "routingEnabled": False, "physicalExclusivityProven": False, "checks": [],
              "sourceSha256": {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}}

    def run(label, command, include, libraries, expected_failure=False):
        step_env = env.copy()
        step_env["INCLUDE"] = os.pathsep.join(map(str, include))
        step_env["LIB"] = os.pathsep.join(map(str, libraries))
        result = subprocess.run(command, cwd=output, env=step_env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, creationflags=0x08000000, check=False)
        (output / (label + ".log")).write_bytes(result.stdout)
        passed = (result.returncode != 0 and b"Physical routing is unavailable" in result.stdout
                  if expected_failure else result.returncode == 0)
        report["checks"].append({"name": label, "exitCode": result.returncode,
                                 "expectedFailure": expected_failure, "passed": passed})
        print(label + ": " + ("passed" if passed else "failed"))
        if not passed:
            raise RuntimeError("Build step failed: " + label + "; inspect its local log")

    try:
        run("policy-compile", [str(compiler), "/nologo", "/std:c++17", "/W4", "/WX", "/EHsc", "/MT", "/O2",
            str(test_source), "/Fo" + str(output / "policy-tests.obj"),
            "/Fe" + str(output / "physical-input-router-tests.exe")], user_include, user_lib)
        run("policy-tests", [str(output / "physical-input-router-tests.exe")], user_include, user_lib)
        kernel_command = [str(compiler), "/nologo", "/c", "/std:c++17", "/kernel", "/W4", "/WX", "/O2",
            "/GS", "/Zl", "/GR-", "/EHs-c-", "/external:env:INCLUDE", "/external:W0", "/D_AMD64_", "/DAMD64", "/D_WIN64",
            "/DNTDDI_VERSION=0x0A00000C", "/D_WIN32_WINNT=0x0A00", "/DWINVER=0x0A00",
            "/DKMDF_VERSION_MAJOR=1", "/DKMDF_VERSION_MINOR=35", "/DDBG=0", "/DUNICODE", "/D_UNICODE",
            str(source / "kmdf_passthrough.cpp")]
        run("kernel-compile", kernel_command + ["/DRETSAR_ENABLE_PHYSICAL_ROUTING=0",
            "/Fo" + str(output / "passthrough.obj")], kernel_include, kernel_lib)
        run("routing-gate", kernel_command + ["/DRETSAR_ENABLE_PHYSICAL_ROUTING=1",
            "/Fo" + str(output / "forbidden-routing.obj")], kernel_include, kernel_lib, expected_failure=True)
        driver = output / "RETSARInputPassThroughLab.sys"
        run("kernel-link", [str(linker), "/NOLOGO", "/DRIVER", "/SUBSYSTEM:NATIVE,10.00", "/ENTRY:FxDriverEntry",
            "/NODEFAULTLIB", "/INCREMENTAL:NO", "/MACHINE:X64", "/DYNAMICBASE", "/NXCOMPAT", "/MANIFEST:NO",
            "/OPT:REF", "/OPT:ICF", "/OUT:" + str(driver), str(output / "passthrough.obj"),
            "WdfDriverEntry.lib", "WdfLdr.lib", "ntoskrnl.lib", "hal.lib", "BufferOverflowFastFailK.lib"],
            kernel_include, kernel_lib)
        binary = driver.read_bytes()
        pe = struct.unpack_from("<I", binary, 0x3C)[0]
        optional = pe + 24
        if binary[pe:pe + 4] != b"PE\0\0" or struct.unpack_from("<H", binary, optional)[0] != 0x20B:
            raise RuntimeError("Expected x64 PE driver")
        certificate_offset, certificate_size = struct.unpack_from("<II", binary, optional + 112 + 4 * 8)
        if certificate_offset or certificate_size:
            raise RuntimeError("Unexpected embedded signature; this build must remain unsigned")
        report["driverSha256"] = hashlib.sha256(binary).hexdigest()
        report["completed"] = True
        return 0
    except (OSError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        return 1
    finally:
        (output / "build-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())