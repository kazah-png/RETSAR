"""Opt-in Windows QEMU supervisor. Importing this module creates no processes.

Only the direct child created here is assigned to our unnamed, non-inheritable
Job Object. No PID lookup, service, registry, host driver or physical disk API.
The free-space floor is a guarded target, not a reservation against other apps.
"""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes as wt
import json
import ntpath
import os
from pathlib import Path
import queue
import re
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time

GIB = 1024 ** 3
STOP_FREE_BYTES = 26 * GIB
POLL_SECONDS = 0.20
HEARTBEAT_TIMEOUT = 2.0
WRITE_BPS = 16 * 1024 ** 2
MAX_MESSAGE = 16384
LOAD_LIBRARY_SEARCH_SYSTEM32 = 0x00000800


class WindowsHypervisorApi:
    """Read-only WHP presence query; never creates or configures a partition."""
    def __init__(self, *, loader=None):
        if os.name != 'nt' or ctypes.sizeof(ctypes.c_void_p) != 8:
            raise RuntimeError("WHPX requires a 64-bit Windows supervisor")
        # Restrict both the requested DLL and its dependencies to System32.
        loader = ctypes.WinDLL if loader is None else loader
        self.dll = loader('WinHvPlatform.dll', winmode=LOAD_LIBRARY_SEARCH_SYSTEM32)
        self.query = self.dll.WHvGetCapability
        self.query.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32,
                               ctypes.POINTER(ctypes.c_uint32)]
        self.query.restype = ctypes.c_int32  # HRESULT, not the platform's Python int size.

    def hypervisor_present(self) -> tuple[int, int, int]:
        present = ctypes.c_int32(0)  # WHV_CAPABILITY.HypervisorPresent is a four-byte BOOL.
        written = ctypes.c_uint32(0)
        result = self.query(0, ctypes.byref(present), ctypes.sizeof(present), ctypes.byref(written))
        return result, present.value, written.value


def require_accelerator(accelerator: str, *, whp_api=None) -> None:
    if accelerator == 'tcg':
        return
    if accelerator != 'whpx':
        raise ValueError("Only tcg or whpx can be selected; accelerator fallback is not allowed")
    try:
        api = WindowsHypervisorApi() if whp_api is None else whp_api
        result, present, written = api.hypervisor_present()
    except (OSError, AttributeError) as error:
        raise RuntimeError(f"WHPX capability query failed; no guest created and no fallback: {error}") from error
    if type(result) is not int or result != 0:
        code = f'0x{result & 0xffffffff:08X}' if type(result) is int else 'invalid HRESULT'
        raise RuntimeError(f"WHPX WHvGetCapability returned {code}; no guest created and no fallback")
    if type(written) is not int or written != 4 or type(present) is not int or present != 1:
        raise RuntimeError("WHPX requires HypervisorPresent=TRUE and exactly four returned bytes; no guest created and no fallback")


def allowed_drive(value: str) -> str:
    """Validate ordinary C:/D: path syntax without accessing any filesystem."""
    value = value.replace("/", "\\")
    if not re.match(r"^[CcDd]:\\", value) or ',' in value or any(ord(x) < 32 for x in value):
        raise ValueError("Only ordinary absolute C: or D: paths without comma/control characters are allowed")
    if ':' in value[2:] or any(p in ('.', '..', '') or p.endswith(('.', ' ')) for p in value[3:].split('\\')):
        raise ValueError("Path components must be explicit, with no traversal or alternate streams")
    return value[0].upper() + ':\\'


def ordinary_lab_path(value: str, *, directory: bool = False) -> Path:
    """Reject disallowed paths before filesystem access, then reject reparses."""
    allowed_drive(value)
    value = value.replace("/", "\\")
    path = Path(value)
    current = Path(value[:3])
    for component in value[3:].split('\\'):
        current = current / component
        st = os.lstat(current)
        if getattr(st, "st_file_attributes", 0) & 0x400:
            raise ValueError("Reparse points are not allowed in laboratory paths")
    if directory != path.is_dir() or (not directory and not path.is_file()):
        raise ValueError("Expected an existing ordinary directory or file")
    return path


def under(path: Path, root: Path) -> bool:
    return ntpath.commonpath([str(path), str(root)]).casefold() == str(root).casefold()


def validate_qcow2(path: Path) -> None:
    with path.open('rb') as stream:
        header = stream.read(104)
    if len(header) < 104 or header[:4] != b'QFI\xfb':
        raise ValueError("The guest disk must be a qcow2 image")
    version = struct.unpack_from('>I', header, 4)[0]
    backing_offset = struct.unpack_from('>Q', header, 8)[0]
    backing_size = struct.unpack_from('>I', header, 16)[0]
    virtual_size = struct.unpack_from('>Q', header, 24)[0]
    encryption = struct.unpack_from('>I', header, 32)[0]
    snapshots = struct.unpack_from('>I', header, 60)[0]
    incompatible = struct.unpack_from('>Q', header, 72)[0] if version == 3 else 0
    if version not in (2, 3) or virtual_size != 64 * GIB:
        raise ValueError("The guest disk must have exactly 64 GiB virtual capacity")
    if backing_offset or backing_size or encryption or snapshots or incompatible & ~1:
        raise ValueError("Backing files, encryption, snapshots and unsupported qcow2 features are refused")


def build_command(paths: dict[str, Path], *, installed_guest: bool = False,
                  accelerator: str = 'tcg') -> list[str]:
    """No extra QEMU arguments are accepted from callers."""
    if accelerator == 'tcg':
        accelerator_args = ['-accel', 'tcg,thread=multi', '-cpu', 'max']
    elif accelerator == 'whpx':
        # QEMU 11.0 WHPX ignores -cpu; use its documented minimal configuration.
        # Do not add an unverified CPU model, IRQ-chip override, or fallback list.
        accelerator_args = ['-accel', 'whpx']
    else:
        raise ValueError("Only tcg or whpx can be selected; accelerator fallback is not allowed")
    command = [str(paths['qemu']), '-no-user-config', '-nodefaults', '-name', 'RETSAR-Lab',
               '-machine', 'q35,vmport=off', *accelerator_args,
               '-m', '4096', '-smp', '2', '-display', 'none',
               '-vga', 'std', '-vnc', '127.0.0.1:5', '-qmp', 'stdio',
               '-monitor', 'none', '-serial', f"file:{paths['serial']}", '-parallel', 'none', '-S',
               '-drive', f"if=pflash,format=raw,unit=0,readonly=on,file={paths['code']}",
               '-drive', f"if=pflash,format=raw,unit=1,file={paths['vars']}",
               '-drive', f"if=none,id=system,format=qcow2,file={paths['disk']},throttling.bps-write={WRITE_BPS}",
               '-device', 'nvme,drive=system,serial=RETSARLAB',
               '-netdev', 'user,id=labnet,restrict=on,hostfwd=tcp:127.0.0.1:13389-:3389',
               '-device', 'e1000e,netdev=labnet', '-device', 'qemu-xhci,id=xhci',
               '-device', 'usb-tablet,bus=xhci.0', '-boot',
               'order=c,menu=on' if installed_guest else 'order=dc,menu=on']
    if not installed_guest:
        command += ['-drive', f"if=ide,media=cdrom,readonly=on,format=raw,file={paths['iso']}"]
    if 'unattend' in paths:
        command += ['-drive', f"if=ide,media=cdrom,readonly=on,format=raw,file={paths['unattend']}"]
    return command


def validate_paths(args) -> dict[str, Path]:
    # Validate every supplied path before inspecting any of them on disk.
    for name in ('root', 'qemu', 'code', 'vars', 'disk', 'iso'):
        allowed_drive(getattr(args, name))
    if args.unattend_iso:
        allowed_drive(args.unattend_iso)
    root = ordinary_lab_path(args.root, directory=True)
    paths = {'root': root}
    for name in ('qemu', 'code', 'vars', 'disk', 'iso'):
        paths[name] = ordinary_lab_path(getattr(args, name))
    if args.unattend_iso:
        paths['unattend'] = ordinary_lab_path(args.unattend_iso)
    if paths['qemu'].name.casefold() != 'qemu-system-x86_64.exe':
        raise ValueError("Only the verified x86_64 QEMU system executable is allowed")
    for name in ('vars', 'disk', 'iso', 'unattend'):
        if name in paths and not under(paths[name], root):
            raise ValueError("Writable guest files and media must be inside the private lab root")
    if paths['iso'].suffix.lower() != '.iso' or ('unattend' in paths and paths['unattend'].suffix.lower() != '.iso'):
        raise ValueError("Installation media must be existing .iso files")
    if len({str(p).casefold() for key, p in paths.items() if key != 'root'}) != len(paths) - 1:
        raise ValueError("QEMU, disk, media and firmware paths must be distinct")
    paths['reports'] = ordinary_lab_path(str(root / 'reports'), directory=True)
    paths['logs'] = ordinary_lab_path(str(root / 'logs'), directory=True)
    paths['serial'] = paths['logs'] / 'guest-serial.log'
    if os.path.lexists(paths['serial']):
        raise ValueError("The guest serial log already exists; archive it before launching")
    validate_qcow2(paths['disk'])
    return paths


def ports_available() -> None:
    sockets = []
    try:
        for port in (5905, 13389):
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sockets.append(sock)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            sock.bind(('127.0.0.1', port))
    finally:
        for sock in sockets:
            sock.close()


def safe_request(message: dict, reports: Path) -> dict:
    if not isinstance(message, dict) or set(message) - {'execute', 'arguments'}:
        raise ValueError("Expected a QMP command without user-supplied IDs")
    command = message.get('execute')
    args = message.get('arguments', {})
    if not isinstance(args, dict):
        raise ValueError("QMP arguments must be an object")
    if command in {'query-status', 'query-version', 'query-block', 'system_powerdown', 'system_reset', 'quit'}:
        if args:
            raise ValueError("This command takes no arguments")
    elif command == 'screendump':
        if set(args) != {'filename'} or not isinstance(args['filename'], str):
            raise ValueError("A capture requires one simple .ppm filename")
        filename = args['filename']
        if not re.fullmatch(r'[A-Za-z0-9_-]{1,64}\.ppm', filename):
            raise ValueError("Captures must use a simple .ppm filename")
        destination = reports / filename
        if os.path.lexists(destination):
            # Never follow or overwrite an existing file, including a reparse.
            raise ValueError("Capture already exists; choose a new filename")
        args = {'filename': str(destination)}
    elif command == 'send-key':
        if set(args) - {'keys', 'hold-time'} or not 1 <= len(args.get('keys', [])) <= 8:
            raise ValueError("Use one to eight QMP qcode keys")
        if type(args.get('hold-time', 100)) is not int or not 1 <= args.get('hold-time', 100) <= 2000:
            raise ValueError("Key hold time must be between 1 and 2000 milliseconds")
        for key in args['keys']:
            if not isinstance(key, dict) or set(key) != {'type', 'data'} or key['type'] != 'qcode' or not re.fullmatch(r'[a-z0-9_-]{1,32}', str(key['data'])):
                raise ValueError("Only named guest qcode keys are accepted")
    elif command == 'input-send-event':
        events = args.get('events')
        if set(args) != {'events'} or not isinstance(events, list) or not 1 <= len(events) <= 16:
            raise ValueError("Use one to sixteen guest pointer events")
        for event in events:
            if not isinstance(event, dict) or set(event) != {'type', 'data'} or not isinstance(event['data'], dict):
                raise ValueError("Invalid guest pointer event")
            data = event['data']
            if event['type'] == 'abs':
                if set(data) != {'axis', 'value'} or data['axis'] not in ('x', 'y') or type(data['value']) is not int or not 0 <= data['value'] <= 32767:
                    raise ValueError("Invalid guest absolute coordinate")
            elif event['type'] == 'btn':
                if set(data) != {'button', 'down'} or data['button'] not in ('left', 'middle', 'right', 'wheel-up', 'wheel-down') or type(data['down']) is not bool:
                    raise ValueError("Invalid guest pointer button")
            else:
                raise ValueError("Only absolute pointer and button events are accepted")
    else:
        raise ValueError("QMP command is outside the laboratory allowlist")
    return {'execute': command, 'arguments': args}


class SECURITY_ATTRIBUTES(ctypes.Structure):
    _fields_ = [('nLength', wt.DWORD), ('lpSecurityDescriptor', wt.LPVOID), ('bInheritHandle', wt.BOOL)]


class STARTUPINFO(ctypes.Structure):
    _fields_ = [('cb', wt.DWORD), ('lpReserved', wt.LPWSTR), ('lpDesktop', wt.LPWSTR), ('lpTitle', wt.LPWSTR),
                ('dwX', wt.DWORD), ('dwY', wt.DWORD), ('dwXSize', wt.DWORD), ('dwYSize', wt.DWORD),
                ('dwXCountChars', wt.DWORD), ('dwYCountChars', wt.DWORD), ('dwFillAttribute', wt.DWORD),
                ('dwFlags', wt.DWORD), ('wShowWindow', wt.WORD), ('cbReserved2', wt.WORD),
                ('lpReserved2', ctypes.POINTER(wt.BYTE)), ('hStdInput', wt.HANDLE),
                ('hStdOutput', wt.HANDLE), ('hStdError', wt.HANDLE)]


class STARTUPINFOEX(ctypes.Structure):
    _fields_ = [('StartupInfo', STARTUPINFO), ('lpAttributeList', wt.LPVOID)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [('hProcess', wt.HANDLE), ('hThread', wt.HANDLE), ('dwProcessId', wt.DWORD), ('dwThreadId', wt.DWORD)]


class BASIC_LIMIT(ctypes.Structure):
    _fields_ = [('PerProcessUserTimeLimit', ctypes.c_int64), ('PerJobUserTimeLimit', ctypes.c_int64),
                ('LimitFlags', wt.DWORD), ('MinimumWorkingSetSize', ctypes.c_size_t),
                ('MaximumWorkingSetSize', ctypes.c_size_t), ('ActiveProcessLimit', wt.DWORD),
                ('Affinity', ctypes.c_size_t), ('PriorityClass', wt.DWORD), ('SchedulingClass', wt.DWORD)]


class IO_COUNTERS(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint64) for name in ('ReadOperationCount', 'WriteOperationCount', 'OtherOperationCount', 'ReadTransferCount', 'WriteTransferCount', 'OtherTransferCount')]


class EXTENDED_LIMIT(ctypes.Structure):
    _fields_ = [('BasicLimitInformation', BASIC_LIMIT), ('IoInfo', IO_COUNTERS),
                ('ProcessMemoryLimit', ctypes.c_size_t), ('JobMemoryLimit', ctypes.c_size_t),
                ('PeakProcessMemoryUsed', ctypes.c_size_t), ('PeakJobMemoryUsed', ctypes.c_size_t)]


class WindowsApi:
    """Small typed Win32 adapter, also replaceable by a fake in unit tests."""
    def __init__(self):
        if os.name != 'nt':
            raise OSError("The process guardian is Windows-only")
        self.dll = ctypes.WinDLL('kernel32', use_last_error=True)
        definitions = {
            'CreateJobObjectW': (wt.HANDLE, [wt.LPVOID, wt.LPCWSTR]),
            'SetInformationJobObject': (wt.BOOL, [wt.HANDLE, ctypes.c_int, wt.LPVOID, wt.DWORD]),
            'AssignProcessToJobObject': (wt.BOOL, [wt.HANDLE, wt.HANDLE]),
            'TerminateJobObject': (wt.BOOL, [wt.HANDLE, wt.UINT]),
            'TerminateProcess': (wt.BOOL, [wt.HANDLE, wt.UINT]),
            'CloseHandle': (wt.BOOL, [wt.HANDLE]),
            'SetHandleInformation': (wt.BOOL, [wt.HANDLE, wt.DWORD, wt.DWORD]),
            'GetHandleInformation': (wt.BOOL, [wt.HANDLE, ctypes.POINTER(wt.DWORD)]),
            'CreatePipe': (wt.BOOL, [ctypes.POINTER(wt.HANDLE), ctypes.POINTER(wt.HANDLE), ctypes.POINTER(SECURITY_ATTRIBUTES), wt.DWORD]),
            'InitializeProcThreadAttributeList': (wt.BOOL, [wt.LPVOID, wt.DWORD, wt.DWORD, ctypes.POINTER(ctypes.c_size_t)]),
            'UpdateProcThreadAttribute': (wt.BOOL, [wt.LPVOID, wt.DWORD, ctypes.c_size_t, wt.LPVOID, ctypes.c_size_t, wt.LPVOID, wt.LPVOID]),
            'DeleteProcThreadAttributeList': (None, [wt.LPVOID]),
            'CreateProcessW': (wt.BOOL, [wt.LPCWSTR, wt.LPWSTR, wt.LPVOID, wt.LPVOID, wt.BOOL, wt.DWORD, wt.LPVOID, wt.LPCWSTR, wt.LPVOID, ctypes.POINTER(PROCESS_INFORMATION)]),
            'ResumeThread': (wt.DWORD, [wt.HANDLE]),
            'WaitForSingleObject': (wt.DWORD, [wt.HANDLE, wt.DWORD]),
            'GetExitCodeProcess': (wt.BOOL, [wt.HANDLE, ctypes.POINTER(wt.DWORD)]),
            'PeekNamedPipe': (wt.BOOL, [wt.HANDLE, wt.LPVOID, wt.DWORD, wt.LPVOID, ctypes.POINTER(wt.DWORD), wt.LPVOID]),
            'ReadFile': (wt.BOOL, [wt.HANDLE, wt.LPVOID, wt.DWORD, ctypes.POINTER(wt.DWORD), wt.LPVOID]),
            'WriteFile': (wt.BOOL, [wt.HANDLE, wt.LPVOID, wt.DWORD, ctypes.POINTER(wt.DWORD), wt.LPVOID]),
        }
        for name, (restype, argtypes) in definitions.items():
            function = getattr(self.dll, name)
            function.restype, function.argtypes = restype, argtypes

    @staticmethod
    def check(ok):
        if not ok:
            raise ctypes.WinError(ctypes.get_last_error())
        return ok

    def close(self, handle):
        if handle:
            self.check(self.dll.CloseHandle(handle))

    def job(self):
        job = self.check(self.dll.CreateJobObjectW(None, None))
        try:
            self.check(self.dll.SetHandleInformation(job, 1, 0))
            flags = wt.DWORD()
            self.check(self.dll.GetHandleInformation(job, ctypes.byref(flags)))
            if flags.value & 1:
                raise RuntimeError("The job handle must not be inheritable")
            limits = EXTENDED_LIMIT()
            limits.BasicLimitInformation.LimitFlags = 0x2000 | 0x20  # KILL_ON_JOB_CLOSE | PRIORITY_CLASS
            limits.BasicLimitInformation.PriorityClass = 0x4000  # BELOW_NORMAL_PRIORITY_CLASS
            self.check(self.dll.SetInformationJobObject(job, 9, ctypes.byref(limits), ctypes.sizeof(limits)))
            return job
        except BaseException:
            self.close(job)
            raise

    def pipe(self):
        read, write = wt.HANDLE(), wt.HANDLE()
        attrs = SECURITY_ATTRIBUTES(ctypes.sizeof(SECURITY_ATTRIBUTES), None, True)
        self.check(self.dll.CreatePipe(ctypes.byref(read), ctypes.byref(write), ctypes.byref(attrs), 65536))
        return read.value, write.value

    def create_suspended(self, command, cwd):
        handles = []
        attributes = None
        process = PROCESS_INFORMATION()
        try:
            stdin_read, stdin_write = self.pipe(); handles += [stdin_read, stdin_write]
            stdout_read, stdout_write = self.pipe(); handles += [stdout_read, stdout_write]
            stderr_read, stderr_write = self.pipe(); handles += [stderr_read, stderr_write]
            for handle in (stdin_write, stdout_read, stderr_read):
                self.check(self.dll.SetHandleInformation(handle, 1, 0))
            size = ctypes.c_size_t()
            self.dll.InitializeProcThreadAttributeList(None, 1, 0, ctypes.byref(size))
            if not size.value:
                raise ctypes.WinError(ctypes.get_last_error())
            storage = ctypes.create_string_buffer(size.value)
            self.check(self.dll.InitializeProcThreadAttributeList(storage, 1, 0, ctypes.byref(size)))
            attributes = storage
            inherited = (wt.HANDLE * 3)(stdin_read, stdout_write, stderr_write)
            self.check(self.dll.UpdateProcThreadAttribute(storage, 0, 0x20002, inherited, ctypes.sizeof(inherited), None, None))
            startup = STARTUPINFOEX()
            startup.StartupInfo.cb = ctypes.sizeof(startup)
            startup.StartupInfo.dwFlags = 0x100 | 1  # USESTDHANDLES | USESHOWWINDOW
            startup.StartupInfo.wShowWindow = 0
            startup.StartupInfo.hStdInput, startup.StartupInfo.hStdOutput, startup.StartupInfo.hStdError = stdin_read, stdout_write, stderr_write
            startup.lpAttributeList = ctypes.cast(storage, wt.LPVOID)
            flags = 0x4 | 0x4000 | 0x80000 | 0x08000000  # SUSPENDED | BELOW_NORMAL | EXTENDED_STARTUPINFO | NO_WINDOW
            self.check(self.dll.CreateProcessW(command[0], ctypes.create_unicode_buffer(subprocess.list2cmdline(command)),
                       None, None, True, flags, None, str(cwd), ctypes.byref(startup), ctypes.byref(process)))
            # Only these three pipe handles are inherited; never the job handle.
            for handle in (stdin_read, stdout_write, stderr_write):
                self.close(handle); handles.remove(handle)
            self.dll.DeleteProcThreadAttributeList(attributes)
            attributes = None
            handles.clear()
            return process.hProcess, process.hThread, stdin_write, stdout_read, stderr_read
        except BaseException:
            cleanup_errors = []
            if process.hProcess:
                # Even an error closing a child pipe cannot orphan this suspended child.
                try:
                    self.terminate_process(process.hProcess)
                except BaseException as error:
                    cleanup_errors.append(error)
                handles += [process.hThread, process.hProcess]
            if attributes is not None:
                try:
                    self.dll.DeleteProcThreadAttributeList(attributes)
                except BaseException as error:
                    cleanup_errors.append(error)
            for handle in handles:
                try:
                    self.close(handle)
                except BaseException as error:
                    cleanup_errors.append(error)
            if cleanup_errors:
                raise RuntimeError("Suspended-child failure cleanup could not be confirmed") from cleanup_errors[0]
            raise

    def assign(self, job, process):
        self.check(self.dll.AssignProcessToJobObject(job, process))

    def resume(self, thread):
        result = self.dll.ResumeThread(thread)
        if result == 0xffffffff:
            raise ctypes.WinError(ctypes.get_last_error())
        if result != 1:
            raise RuntimeError("Unexpected primary-thread suspend count")

    def terminate_job(self, job):
        self.check(self.dll.TerminateJobObject(job, 125))

    def terminate_process(self, process):
        self.check(self.dll.TerminateProcess(process, 125))

    def poll(self, process):
        result = self.dll.WaitForSingleObject(process, 0)
        if result == 258:
            return None
        if result != 0:
            raise ctypes.WinError(ctypes.get_last_error())
        exit_code = wt.DWORD()
        self.check(self.dll.GetExitCodeProcess(process, ctypes.byref(exit_code)))
        return exit_code.value

    def read_available(self, handle):
        available = wt.DWORD()
        if not self.dll.PeekNamedPipe(handle, None, 0, None, ctypes.byref(available), None):
            if ctypes.get_last_error() == 109:
                return b''
            raise ctypes.WinError(ctypes.get_last_error())
        if not available.value:
            return b''
        buffer = ctypes.create_string_buffer(min(available.value, 65536))
        actual = wt.DWORD()
        self.check(self.dll.ReadFile(handle, buffer, len(buffer), ctypes.byref(actual), None))
        return buffer.raw[:actual.value]

    def write(self, handle, data):
        actual = wt.DWORD()
        self.check(self.dll.WriteFile(handle, data, len(data), ctypes.byref(actual), None))
        if actual.value != len(data):
            raise OSError("Incomplete QMP pipe write")


class OwnedChild:
    """No PID-based operation; cleanup handles only resources created here."""
    def __init__(self, api):
        self.api, self.job, self.process, self.thread = api, None, None, None
        self.stdin = self.stdout = self.stderr = None
        self.assigned = False
        self.lock = threading.Lock()

    def start(self, command, cwd):
        try:
            self.job = self.api.job()
            self.process, self.thread, self.stdin, self.stdout, self.stderr = self.api.create_suspended(command, cwd)
            self.api.assign(self.job, self.process)
            self.assigned = True
            self.api.resume(self.thread)
            self.api.close(self.thread); self.thread = None
        except BaseException:
            self.close()
            raise

    def terminate(self):
        with self.lock:
            if self.job and self.assigned:
                self.api.terminate_job(self.job)
            elif self.process and (not self.assigned or self.api.poll(self.process) is None):
                # Assignment failure: this exact child is still suspended.
                self.api.terminate_process(self.process)

    def close(self):
        errors = []
        try:
            self.terminate()
        except BaseException as error:
            errors.append(error)
        with self.lock:
            # Closing our non-inherited job is a second kill-on-close path.
            for name in ('job', 'thread', 'stdin', 'stdout', 'stderr', 'process'):
                handle = getattr(self, name)
                if handle:
                    try:
                        self.api.close(handle)
                    except BaseException as error:
                        errors.append(error)
                    setattr(self, name, None)
        if errors:
            raise RuntimeError("Owned-child cleanup failed") from errors[0]


def check_space(free_bytes, volume='C:\\'):
    if type(free_bytes) is not int or free_bytes <= STOP_FREE_BYTES:
        raise RuntimeError(f"{volume} free-space monitor reached the 26 GiB stop threshold or returned invalid data")


def monitored_volumes(root: Path | str) -> tuple[str, ...]:
    """Select only the system volume and the approved lab volume; never enumerate drives."""
    drive = allowed_drive(str(root))
    return ('C:\\',) if drive == 'C:\\' else ('C:\\', 'D:\\')


def check_storage(root: Path | str) -> None:
    # C: remains guarded for Windows/pagefile overhead when guest assets are on D:.
    for volume in monitored_volumes(root):
        check_space(shutil.disk_usage(volume).free, volume)


def check_serial(path: Path):
    try:
        st = os.lstat(path)
    except FileNotFoundError:
        return
    if getattr(st, 'st_file_attributes', 0) & 0x400 or st.st_size >= 16 * 1024 ** 2:
        raise RuntimeError("The guest serial log reached its 16 MiB stop threshold or is not an ordinary file")


class Heartbeat:
    def __init__(self):
        self.last = time.monotonic()
        self.failed = threading.Event()
        self.stop = threading.Event()

    def watch(self, child):
        try:
            while not self.stop.wait(POLL_SECONDS):
                if time.monotonic() - self.last > HEARTBEAT_TIMEOUT:
                    raise RuntimeError("Supervisor heartbeat lost")
        except BaseException:
            self.failed.set()
            try:
                child.terminate()
            except BaseException:
                # Job closure at process exit remains the final safety path.
                os._exit(125)


def input_reader(commands, ended):
    try:
        while True:
            line = sys.stdin.readline(MAX_MESSAGE + 1)
            if not line:
                ended.set()
                return
            if len(line) > MAX_MESSAGE:
                raise ValueError("Supervisor input command exceeds its bound")
            commands.put_nowait(json.loads(line))
    except BaseException:
        ended.set()


def supervise(paths, *, api=None, installed_guest=False, accelerator='tcg', whp_api=None):
    require_accelerator(accelerator, whp_api=whp_api)
    api = api or WindowsApi()
    check_storage(paths['root'])
    ports_available()
    child = OwnedChild(api)
    heartbeat = Heartbeat()
    commands, input_failed = queue.Queue(maxsize=16), threading.Event()
    watchdog = None
    buffer = b''
    pending = {}
    phase = 'greeting'
    counter = 0
    stderr_remaining = 4 * 1024 ** 2
    # Exclusive creation avoids overwriting or following a pre-existing log.
    log_path = paths['logs'] / f'qemu-{time.time_ns()}.stderr.log'
    with log_path.open('xb') as log:
        try:
            child.start(build_command(paths, installed_guest=installed_guest, accelerator=accelerator), paths['root'])
            watchdog = threading.Thread(target=heartbeat.watch, args=(child,), daemon=True)
            watchdog.start()
            threading.Thread(target=input_reader, args=(commands, input_failed), daemon=True).start()
            deadline = time.monotonic() + 30
            while True:
                check_storage(paths['root'])
                check_serial(paths['serial'])
                heartbeat.last = time.monotonic()
                if heartbeat.failed.is_set() or input_failed.is_set():
                    raise RuntimeError("Supervisor monitor or input channel failed")
                exit_code = api.poll(child.process)
                errors = api.read_available(child.stderr)
                if errors and stderr_remaining:
                    written = errors[:stderr_remaining]
                    log.write(written); log.flush(); stderr_remaining -= len(written)
                if exit_code is not None:
                    # Preserve early startup errors before closing the child pipes.
                    print(json.dumps({'supervisor': 'guest-exited', 'exitCode': exit_code}), flush=True)
                    return exit_code
                buffer += api.read_available(child.stdout)
                if len(buffer) > 1024 * 1024:
                    raise RuntimeError("QMP output exceeded its bounded buffer")
                while b'\n' in buffer:
                    line, buffer = buffer.split(b'\n', 1)
                    if not line.strip():
                        continue
                    reply = json.loads(line)
                    if phase == 'greeting':
                        if 'QMP' not in reply:
                            raise RuntimeError("QEMU did not return the expected QMP greeting")
                        api.write(child.stdin, b'{"execute":"qmp_capabilities","id":"setup"}\n')
                        phase = 'capabilities'
                    elif reply.get('id') == 'setup':
                        if 'error' in reply or 'return' not in reply:
                            raise RuntimeError("QMP capability negotiation failed")
                        check_storage(paths['root'])
                        api.write(child.stdin, b'{"execute":"cont","id":"start"}\n')
                        phase = 'starting'
                    elif reply.get('id') == 'start':
                        if 'error' in reply or 'return' not in reply:
                            raise RuntimeError("QMP refused to start the guest")
                        phase = 'running'
                        print(json.dumps({'supervisor': 'guest-started', 'realSessionEvidence': False}), flush=True)
                    else:
                        pending.pop(reply.get('id'), None)
                        print(json.dumps(reply), flush=True)
                if phase != 'running' and time.monotonic() > deadline:
                    raise RuntimeError("QMP startup timed out")
                if phase == 'running' and not commands.empty():
                    request = safe_request(commands.get_nowait(), paths['reports'])
                    counter += 1
                    request['id'] = f'user-{counter}'
                    if len(pending) >= 16:
                        raise RuntimeError("Too many unanswered QMP requests")
                    pending[request['id']] = time.monotonic()
                    api.write(child.stdin, (json.dumps(request) + '\n').encode('utf-8'))
                if any(time.monotonic() - started > 30 for started in pending.values()):
                    raise RuntimeError("A QMP request timed out")
                time.sleep(POLL_SECONDS)
        finally:
            heartbeat.stop.set()
            if watchdog:
                watchdog.join(timeout=1)
            child.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, help='Existing private lab directory on C: or D:; both C: and the lab volume are monitored')
    for name in ('qemu', 'code', 'vars', 'disk', 'iso'):
        parser.add_argument('--' + name, required=True, help='Existing ordinary C: or D: file; guest disk, writable firmware and media must be inside --root')
    parser.add_argument('--unattend-iso', help='Optional existing read-only installation-answer ISO inside the C: or D: lab root')
    parser.add_argument('--launch-approved', action='store_true', help='Explicit opt-in to the already approved lab launch')
    parser.add_argument('--installed-guest', action='store_true', help='Boot the existing disk without attaching the Windows installation ISO')
    parser.add_argument('--accelerator', choices=('tcg', 'whpx'), default='tcg',
                        help='TCG by default; WHPX explicitly requires an already available Windows hypervisor, with no fallback or host configuration changes')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('This supervisor is Windows-only')
    if not args.launch_approved:
        parser.error('No child was created: --launch-approved is required')
    paths = validate_paths(args)
    return supervise(paths, installed_guest=args.installed_guest, accelerator=args.accelerator)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
    except Exception as error:
        print(f'Laboratory stopped: {error}', file=sys.stderr)
        sys.exit(125)
