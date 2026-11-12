"""Opt-in source build with existing licensed tools; never installs or loads a driver."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def existing_root(value):
    path = Path(value).resolve(strict=True)
    if not path.is_dir():
        raise argparse.ArgumentTypeError("A toolchain root must be a directory")
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--msvc-root", required=True, type=existing_root)
    parser.add_argument("--wdk-root", required=True, type=existing_root)
    parser.add_argument("--sdk-root", required=True, type=existing_root)
    parser.add_argument("--sdk-x64-root", required=True, type=existing_root)
    parser.add_argument("--out", required=True, type=Path,
                        help="New output directory outside this source directory")
    parser.add_argument("--driver-version", required=True,
                        help="Four decimal components, each between 0 and 65535")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("The supplied compiler and verifier require Windows")
    if not re.fullmatch(r"\d{1,5}(\.\d{1,5}){3}", args.driver_version) or any(
            int(value) > 65535 for value in args.driver_version.split(".")):
        parser.error("Invalid driver version")
    source = Path(__file__).resolve().parent
    output = args.out.resolve()
    if output == source or source in output.parents or output.exists():
        parser.error("Output must be a new directory outside the source directory")
    if not output.parent.is_dir():
        parser.error("Output parent must already exist")
    msvc, wdk, sdk, sdk64 = args.msvc_root, args.wdk_root, args.sdk_root, args.sdk_x64_root
    version = "10.0.26100.0"
    compiler = msvc / "bin/Hostx64/x64/cl.exe"
    verifier = wdk / "tools" / version / "x64/infverif.exe"
    for path in (compiler, verifier, source / "Driver.cpp", source / "Driver.h",
                 source / "RETSARRemoteIddLab.inf.template"):
        if not path.is_file():
            parser.error("Required build input is missing: " + str(path))
    include = [msvc / "include", wdk / "Include/wdf/umdf/2.25",
               wdk / "Include" / version / "um/iddcx/1.4",
               wdk / "Include" / version / "shared"]
    include += [sdk / "Include" / version / part for part in ("shared", "um", "winrt", "ucrt")]
    libraries = [msvc / "lib/x64", sdk64 / "um/x64", sdk64 / "ucrt/x64",
                 wdk / "Lib/wdf/umdf/x64/2.25", wdk / "Lib" / version / "um/x64/iddcx/1.4"]
    for path in include + libraries:
        if not path.is_dir():
            parser.error("Required toolchain directory is missing: " + str(path))
    # All admission checks precede filesystem output or tool execution.
    output.mkdir()
    temporary = output / "temp"
    temporary.mkdir()
    env = os.environ.copy()
    env["TEMP"] = env["TMP"] = str(temporary)
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    env["INCLUDE"] = os.pathsep.join(map(str, include))
    env["LIB"] = os.pathsep.join(map(str, libraries))
    commands = [
        ("compile", [str(compiler), "/nologo", "/std:c++17", "/W4", "/EHsc", "/MT", "/O2",
                     "/LD", "/guard:cf", "/sdl", "/D_WIN32_WINNT=0x0A00", "/DNTDDI_VERSION=0x0A000008",
                     "/D_WIN64", "/D_AMD64_", "/DAMD64", "/DUNICODE", "/D_UNICODE",
                     "/DUMDF_DRIVER", "/DUMDF_VERSION_MAJOR=2", "/DUMDF_VERSION_MINOR=25",
                     "/DUMDF_USING_NTSTATUS", "/DIDDCX_VERSION_MAJOR=1", "/DIDDCX_VERSION_MINOR=4",
                     "/DIDDCX_MINIMUM_VERSION_REQUIRED=4", str(source / "Driver.cpp"),
                     "/Fo" + str(output / "Driver.obj"), "/Fe" + str(output / "RETSARRemoteIddLab.dll"),
                     "/link", "/SUBSYSTEM:WINDOWS,10.00", "/DYNAMICBASE", "/NXCOMPAT", "/guard:cf",
                     "WdfDriverStubUm.lib", "IddCxStub.lib", "OneCoreUAP.lib", "ntdll.lib", "avrt.lib",
                     "d3d11.lib", "dxgi.lib", "advapi32.lib"]),
        ("infverif", [str(verifier), "/v", "/u", str(output / "RETSARRemoteIddLab.inf")]),
    ]
    template = (source / "RETSARRemoteIddLab.inf.template").read_text(encoding="utf-8")
    inf = template.replace("@DRIVER_DATE@", datetime.now(timezone.utc).strftime("%m/%d/%Y"))
    inf = inf.replace("@DRIVER_VERSION@", args.driver_version)
    if "@DRIVER_" in inf:
        raise RuntimeError("Unresolved INF build metadata")
    (output / "RETSARRemoteIddLab.inf").write_bytes(inf.encode("utf-8"))
    report = {"schema": "retsar.remote-idd-build.v1", "signed": False, "installed": False,
              "runtimeTested": False, "sourceOnlyExperiment": True, "checks": [],
              "sourceSha256": {name: sha256(source / name) for name in
                               ("Driver.cpp", "Driver.h", "RETSARRemoteIddLab.inf.template", "build.py")}}
    exit_code = 1
    try:
        for label, command in commands:
            result = subprocess.run(command, cwd=output, env=env, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, creationflags=0x08000000, check=False)
            (output / (label + ".log")).write_bytes(result.stdout)
            report["checks"].append({"name": label, "exitCode": result.returncode})
            print(label + ": " + str(result.returncode))
            if result.returncode:
                return result.returncode
        report["dllSha256"] = sha256(output / "RETSARRemoteIddLab.dll")
        exit_code = 0
        return 0
    finally:
        report["completed"] = exit_code == 0
        (output / "build-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())
