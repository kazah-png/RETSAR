"""Unit tests use fake Win32 calls. No guest or child process is started."""
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import threading
from types import SimpleNamespace
import unittest
import xml.etree.ElementTree as ET
from unittest.mock import Mock, call, patch

spec = importlib.util.spec_from_file_location('supervise_qemu', Path(__file__).with_name('supervise_qemu.py'))
lab = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lab)


class FakeApi:
    def __init__(self, fail=None):
        self.events = []
        self.fail = fail

    def event(self, name, *args):
        self.events.append((name, *args))
        if name == self.fail:
            raise OSError('injected ' + name)

    def job(self):
        self.event('job')
        return 'own-job'

    def create_suspended(self, command, cwd):
        self.event('create-suspended', command, cwd)
        return 'own-process', 'own-thread', 'stdin', 'stdout', 'stderr'

    def assign(self, job, process):
        self.event('assign', job, process)

    def resume(self, thread):
        self.event('resume', thread)

    def close(self, handle):
        self.event('close', handle)

    def terminate_job(self, job):
        self.event('terminate-job', job)

    def terminate_process(self, process):
        self.event('terminate-process', process)


class FakeWhpApi:
    def __init__(self, result=(0, 1, 4)):
        self.result = result
        self.calls = 0

    def hypervisor_present(self):
        self.calls += 1
        return self.result


class AcceleratorTests(unittest.TestCase):
    def test_tcg_default_does_not_load_or_query_whp(self):
        with patch.object(lab, 'WindowsHypervisorApi', side_effect=AssertionError('must not load WHP')):
            lab.require_accelerator('tcg')
            fake = FakeWhpApi()
            lab.require_accelerator('tcg', whp_api=fake)
            self.assertEqual(fake.calls, 0)

    def test_whp_loader_is_system32_only_and_capability_abi_is_four_bytes(self):
        class Capability:
            def __call__(self, code, buffer, size, written):
                self.args = code, size
                self.initial = lab.ctypes.cast(buffer, lab.ctypes.POINTER(lab.ctypes.c_int32))[0]
                lab.ctypes.cast(buffer, lab.ctypes.POINTER(lab.ctypes.c_int32))[0] = 1
                lab.ctypes.cast(written, lab.ctypes.POINTER(lab.ctypes.c_uint32))[0] = 4
                return 0

        capability = Capability()
        loader = Mock(return_value=SimpleNamespace(WHvGetCapability=capability))
        with patch.object(lab.ctypes, 'WinDLL', side_effect=AssertionError('must not load a real DLL')):
            api = lab.WindowsHypervisorApi(loader=loader)
            self.assertEqual(api.hypervisor_present(), (0, 1, 4))
        loader.assert_called_once_with('WinHvPlatform.dll', winmode=0x00000800)
        self.assertEqual(capability.args, (0, 4))
        self.assertEqual(capability.initial, 0)
        self.assertIs(capability.restype, lab.ctypes.c_int32)
        self.assertEqual(capability.argtypes, [lab.ctypes.c_uint32, lab.ctypes.c_void_p,
                                              lab.ctypes.c_uint32, lab.ctypes.POINTER(lab.ctypes.c_uint32)])

    def test_whpx_accepts_only_successful_present_capability(self):
        fake = FakeWhpApi()
        lab.require_accelerator('whpx', whp_api=fake)
        self.assertEqual(fake.calls, 1)

    def test_whpx_refuses_failed_or_malformed_hresult_without_fallback(self):
        for result in (-2147024809, 0x80070057, 1, None, True, '0'):
            with self.subTest(result=result), self.assertRaisesRegex(RuntimeError, 'no fallback'):
                lab.require_accelerator('whpx', whp_api=FakeWhpApi((result, 1, 4)))

    def test_whpx_refuses_absent_or_malformed_bool_and_returned_size(self):
        for present, written in ((0, 4), (2, 4), (True, 4), (None, 4), (1, 0),
                                 (1, 8), (1, None), (1, True)):
            with self.subTest(present=present, written=written), self.assertRaisesRegex(RuntimeError, 'no fallback'):
                lab.require_accelerator('whpx', whp_api=FakeWhpApi((0, present, written)))

    def test_missing_whp_dll_or_export_fails_closed(self):
        for error in (OSError('DLL unavailable'), AttributeError('export unavailable')):
            with self.subTest(error=type(error).__name__), patch.object(lab, 'WindowsHypervisorApi', side_effect=error):
                with self.assertRaisesRegex(RuntimeError, 'no guest created and no fallback'):
                    lab.require_accelerator('whpx')

    def test_whpx_unavailable_stops_before_child_ports_or_storage(self):
        api = FakeApi()
        with patch.object(lab, 'OwnedChild') as child, patch.object(lab, 'ports_available') as ports, \
                patch.object(lab, 'check_storage') as storage:
            with self.assertRaises(RuntimeError):
                lab.supervise({}, api=api, accelerator='whpx', whp_api=FakeWhpApi((0, 0, 4)))
            child.assert_not_called()
            ports.assert_not_called()
            storage.assert_not_called()
        self.assertEqual(api.events, [])

    def test_whpx_success_reaches_only_owned_fake_child_with_selected_command(self):
        class ExitedApi(FakeApi):
            def poll(self, process): return 0
            def read_available(self, handle): return b''

        api, whp = ExitedApi(), FakeWhpApi()
        with tempfile.TemporaryDirectory(prefix='retsar-whpx-test-') as temp:
            root = Path(temp)
            paths = {name: root / name for name in ('qemu', 'code', 'vars', 'disk', 'iso', 'serial')}
            paths.update(root=root, logs=root, reports=root)
            with patch.object(lab, 'check_storage'), patch.object(lab, 'ports_available'), \
                    patch.object(lab.threading, 'Thread'), patch.object(lab.sys, 'stdout', io.StringIO()):
                self.assertEqual(lab.supervise(paths, api=api, accelerator='whpx', whp_api=whp), 0)
        self.assertEqual(whp.calls, 1)
        command = next(event[1] for event in api.events if event[0] == 'create-suspended')
        self.assertEqual(command[command.index('-accel') + 1], 'whpx')
        self.assertNotIn('-cpu', command)
        self.assertIn(('terminate-job', 'own-job'), api.events)

    def test_unknown_accelerators_and_fallback_lists_are_refused(self):
        for value in ('whpx:tcg', 'kvm', 'whpx,kernel-irqchip=off', '', None):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    lab.require_accelerator(value)
                with self.assertRaises(ValueError):
                    lab.build_command({}, accelerator=value)

    def test_whpx_changes_only_accelerator_and_omits_cpu_override(self):
        paths = {name: Path('C:/private/lab') / name for name in ('qemu', 'code', 'vars', 'disk', 'iso', 'serial')}
        tcg = lab.build_command(paths)
        whpx = lab.build_command(paths, accelerator='whpx')
        index = tcg.index('-accel')
        self.assertEqual(whpx, tcg[:index] + ['-accel', 'whpx'] + tcg[index + 4:])
        self.assertNotIn('-cpu', whpx)
        self.assertNotIn('kernel-irqchip', ' '.join(whpx))
        self.assertEqual(whpx.count('-accel'), 1)

    def test_cli_defaults_to_tcg_and_forwards_explicit_whpx(self):
        required = ['supervise_qemu.py', '--launch-approved', '--root', 'private-root']
        for name in ('qemu', 'code', 'vars', 'disk', 'iso'):
            required += ['--' + name, name]
        for extra, expected in (([], 'tcg'), (['--accelerator', 'whpx'], 'whpx')):
            with self.subTest(expected=expected), patch.object(lab.sys, 'argv', required + extra), \
                    patch.object(lab, 'validate_paths', return_value={}), patch.object(lab, 'supervise', return_value=0) as supervisor:
                self.assertEqual(lab.main(), 0)
                supervisor.assert_called_once_with({}, installed_guest=False, accelerator=expected, guest_management=False)


class GuestManagementTests(unittest.TestCase):
    def test_opt_in_adds_only_fixed_loopback_forward_to_existing_network(self):
        paths = {name: Path('D:/private/lab') / name for name in ('qemu', 'code', 'vars', 'disk', 'iso', 'serial')}
        normal = lab.build_command(paths)
        enabled = lab.build_command(paths, guest_management=True)
        index = normal.index('-netdev') + 1
        expected = normal.copy()
        expected[index] += ',hostfwd=tcp:127.0.0.1:15985-:5985'
        self.assertEqual(enabled, expected)
        self.assertNotIn('15985', ' '.join(normal))
        self.assertEqual(enabled.count('-netdev'), 1)

    def test_management_cli_default_and_explicit_opt_in(self):
        required = ['supervise_qemu.py', '--launch-approved', '--root', 'private-root']
        for name in ('qemu', 'code', 'vars', 'disk', 'iso'):
            required += ['--' + name, name]
        for extra, expected in (([], False), (['--guest-management'], True)):
            with self.subTest(enabled=expected), patch.object(lab.sys, 'argv', required + extra), \
                    patch.object(lab, 'validate_paths', return_value={}), patch.object(lab, 'supervise', return_value=0) as supervisor:
                self.assertEqual(lab.main(), 0)
                supervisor.assert_called_once_with({}, installed_guest=False, accelerator='tcg', guest_management=expected)

    def test_non_boolean_opt_in_is_refused_before_network_access(self):
        with patch.object(lab.socket, 'socket', side_effect=AssertionError('must not access network')):
            for invalid in (1, 0, None, 'true', '127.0.0.1:15985', []):
                with self.subTest(value=invalid):
                    with self.assertRaises(ValueError): lab.build_command({}, guest_management=invalid)
                    with self.assertRaises(ValueError): lab.ports_available(guest_management=invalid)

    def test_port_preflight_binds_only_expected_loopback_ports_exclusively(self):
        for enabled, ports in ((False, (5905, 13389)), (True, (5905, 13389, 15985))):
            sockets = [Mock() for _ in ports]
            with self.subTest(enabled=enabled), patch.object(lab.socket, 'socket', side_effect=sockets) as factory:
                lab.ports_available(guest_management=enabled)
            self.assertEqual(factory.call_args_list, [call(lab.socket.AF_INET, lab.socket.SOCK_STREAM)] * len(ports))
            for sock, port in zip(sockets, ports):
                sock.setsockopt.assert_called_once_with(lab.socket.SOL_SOCKET, lab.socket.SO_EXCLUSIVEADDRUSE, 1)
                sock.bind.assert_called_once_with(('127.0.0.1', port))
                sock.close.assert_called_once_with()

    def test_busy_management_port_closes_all_preflight_sockets(self):
        sockets = [Mock(), Mock(), Mock()]
        sockets[-1].bind.side_effect = OSError('management port is occupied')
        with patch.object(lab.socket, 'socket', side_effect=sockets):
            with self.assertRaisesRegex(OSError, 'occupied'):
                lab.ports_available(guest_management=True)
        for sock in sockets:
            sock.close.assert_called_once_with()

    def test_busy_management_port_stops_before_owned_child_or_log_creation(self):
        api = FakeApi()
        paths = {'root': Path('D:/private/lab')}
        with patch.object(lab, 'check_storage'), \
                patch.object(lab, 'ports_available', side_effect=OSError('management port is occupied')) as ports, \
                patch.object(lab, 'OwnedChild') as child, \
                patch.object(lab.Path, 'open', side_effect=AssertionError('must not create a log')):
            with self.assertRaisesRegex(OSError, 'occupied'):
                lab.supervise(paths, api=api, guest_management=True)
            ports.assert_called_once_with(guest_management=True)
            child.assert_not_called()
        self.assertEqual(api.events, [])

    def test_started_json_reports_forwarding_without_claiming_service_readiness(self):
        class ConnectedApi(FakeApi):
            def __init__(self):
                super().__init__()
                self.polls = 0

            def poll(self, process):
                self.polls += 1
                return None if self.polls == 1 else 0

            def read_available(self, handle):
                if handle == 'stdout':
                    return b'{"QMP":{}}\n{"return":{},"id":"setup"}\n{"return":{},"id":"start"}\n'
                return b''

            def write(self, handle, data): self.event('write', handle, data)

        for enabled in (False, True):
            api = ConnectedApi()
            output = io.StringIO()
            with self.subTest(enabled=enabled), tempfile.TemporaryDirectory(prefix='retsar-management-test-') as temp:
                root = Path(temp)
                paths = {name: root / name for name in ('qemu', 'code', 'vars', 'disk', 'iso', 'serial')}
                paths.update(root=root, logs=root, reports=root)
                with patch.object(lab, 'check_storage'), patch.object(lab, 'ports_available') as ports, \
                        patch.object(lab.threading, 'Thread'), patch.object(lab.time, 'sleep'), \
                        patch.object(lab.sys, 'stdout', output):
                    self.assertEqual(lab.supervise(paths, api=api, guest_management=enabled), 0)
                ports.assert_called_once_with(guest_management=enabled)
            records = [json.loads(line) for line in output.getvalue().splitlines()]
            started = next(record for record in records if record.get('supervisor') == 'guest-started')
            self.assertIs(started['guestManagementForwarding'], enabled)
            self.assertIs(started['guestManagementServiceVerified'], False)
            self.assertIs(started['realSessionEvidence'], False)
            command = next(event[1] for event in api.events if event[0] == 'create-suspended')
            self.assertEqual('15985' in ' '.join(command), enabled)


class GuardianTests(unittest.TestCase):
    def test_child_suspended_then_assigned_then_resumed(self):
        api = FakeApi()
        child = lab.OwnedChild(api)
        child.start(['fixed-qemu'], 'private-root')
        self.assertEqual([e[0] for e in api.events], ['job', 'create-suspended', 'assign', 'resume', 'close'])
        child.close()
        self.assertIn(('terminate-job', 'own-job'), api.events)
        self.assertNotIn(('terminate-process', 'own-process'), api.events)
        before = len(api.events)
        child.close()
        self.assertEqual(len(api.events), before)

    def test_failed_assignment_never_resumes_and_kills_exact_handle(self):
        api = FakeApi('assign')
        child = lab.OwnedChild(api)
        with self.assertRaises(OSError):
            child.start(['fixed-qemu'], 'private-root')
        self.assertFalse(any(e[0] == 'resume' for e in api.events))
        self.assertIn(('terminate-process', 'own-process'), api.events)
        self.assertIn(('close', 'own-job'), api.events)

    def test_failed_resume_kills_job_and_closes_all_owned_handles(self):
        api = FakeApi('resume')
        with self.assertRaises(OSError):
            lab.OwnedChild(api).start(['fixed-qemu'], 'private-root')
        self.assertIn(('terminate-job', 'own-job'), api.events)
        for handle in ('own-job', 'own-thread', 'stdin', 'stdout', 'stderr', 'own-process'):
            self.assertIn(('close', handle), api.events)

    def test_create_failure_closes_empty_job(self):
        api = FakeApi('create-suspended')
        with self.assertRaises(OSError):
            lab.OwnedChild(api).start(['fixed-qemu'], 'private-root')
        self.assertEqual(api.events[-1], ('close', 'own-job'))
        self.assertFalse(any(e[0].startswith('terminate') for e in api.events))

    def test_termination_failure_still_closes_kill_on_close_job(self):
        api = FakeApi('terminate-job')
        child = lab.OwnedChild(api)
        child.start(['fixed-qemu'], 'private-root')
        with self.assertRaises(RuntimeError):
            child.close()
        self.assertIn(('close', 'own-job'), api.events)

    def test_26_gib_is_inclusive_stop_threshold(self):
        for value in (25 * lab.GIB, 26 * lab.GIB, None, True, -1):
            with self.subTest(value=value), self.assertRaises(RuntimeError):
                lab.check_space(value)
        lab.check_space(26 * lab.GIB + 1)

    def test_storage_guard_queries_only_system_and_selected_lab_volume(self):
        free = SimpleNamespace(free=40 * lab.GIB)
        for root, expected in (('C:/private/lab', ['C:\\']),
                               ('d:/private/lab', ['C:\\', 'D:\\'])):
            with self.subTest(root=root), patch.object(lab.shutil, 'disk_usage', return_value=free) as usage:
                lab.check_storage(root)
                self.assertEqual(usage.call_args_list, [call(volume) for volume in expected])

    def test_storage_guard_stops_on_either_volume_threshold(self):
        for constrained in ('C:\\', 'D:\\'):
            def usage(volume):
                self.assertIn(volume, ('C:\\', 'D:\\'))
                return SimpleNamespace(free=(26 if volume == constrained else 40) * lab.GIB)
            with self.subTest(volume=constrained), patch.object(lab.shutil, 'disk_usage', side_effect=usage):
                with self.assertRaises(RuntimeError) as stopped:
                    lab.check_storage('D:/private/lab')
                self.assertIn(constrained, str(stopped.exception))

    def test_storage_guard_refuses_other_roots_without_querying_a_volume(self):
        with patch.object(lab.shutil, 'disk_usage', side_effect=AssertionError('must not query a volume')):
            for root in ('E:/private/lab', 'Z:/lab', '//server/share', 'D:/private/../lab'):
                with self.subTest(root=root), self.assertRaises(ValueError):
                    lab.check_storage(root)

    def test_storage_guard_fails_closed_when_lab_volume_cannot_be_read(self):
        with patch.object(lab.shutil, 'disk_usage', side_effect=[SimpleNamespace(free=40 * lab.GIB), OSError('unavailable')]) as usage:
            with self.assertRaises(OSError):
                lab.check_storage('D:/private/lab')
            self.assertEqual(usage.call_args_list, [call('C:\\'), call('D:\\')])

    def test_running_storage_failure_closes_only_owned_child(self):
        api = FakeApi()
        with tempfile.TemporaryDirectory(prefix='retsar-storage-test-') as temp:
            root = Path(temp)
            paths = {name: root / name for name in ('qemu', 'code', 'vars', 'disk', 'iso', 'serial')}
            paths.update(root=Path('D:/private/lab'), logs=root, reports=root)
            with patch.object(lab, 'check_storage', side_effect=[None, RuntimeError('storage stop')]) as guard, \
                    patch.object(lab, 'ports_available'), patch.object(lab.threading, 'Thread'):
                with self.assertRaisesRegex(RuntimeError, 'storage stop'):
                    lab.supervise(paths, api=api)
                self.assertEqual(guard.call_args_list, [call(paths['root']), call(paths['root'])])
        self.assertIn(('terminate-job', 'own-job'), api.events)
        self.assertIn(('close', 'own-process'), api.events)
        self.assertNotIn(('terminate-process', 'own-process'), api.events)

    def test_lost_heartbeat_terminates_owned_job(self):
        api = FakeApi()
        child = lab.OwnedChild(api)
        child.start(['fixed-qemu'], 'private-root')
        heartbeat = lab.Heartbeat()
        heartbeat.last = 0
        with patch.object(lab.time, 'monotonic', return_value=100), patch.object(heartbeat.stop, 'wait', return_value=False):
            heartbeat.watch(child)
        self.assertTrue(heartbeat.failed.is_set())
        self.assertIn(('terminate-job', 'own-job'), api.events)
        child.close()

    def test_eof_and_invalid_input_fail_closed(self):
        for content in ('', '{invalid json}\n', 'x' * (lab.MAX_MESSAGE + 1)):
            ended = threading.Event()
            with patch.object(lab.sys, 'stdin', io.StringIO(content)):
                lab.input_reader(lab.queue.Queue(maxsize=16), ended)
            self.assertTrue(ended.is_set())

    def test_early_child_exit_preserves_stderr_before_cleanup(self):
        class ExitedApi(FakeApi):
            def poll(self, process):
                self.event('poll', process)
                return 1

            def read_available(self, handle):
                self.event('read', handle)
                return b'QEMU startup rejected a test option\n' if handle == 'stderr' else b''

        api = ExitedApi()
        with tempfile.TemporaryDirectory(prefix='retsar-exit-test-') as temp:
            root = Path(temp)
            paths = {name: root / name for name in ('qemu', 'code', 'vars', 'disk', 'iso', 'serial')}
            paths.update(root=root, logs=root, reports=root)
            free = type('DiskUsage', (), {'free': 40 * lab.GIB})()
            with patch.object(lab.shutil, 'disk_usage', return_value=free), patch.object(lab, 'ports_available'), \
                    patch.object(lab.threading, 'Thread'), patch.object(lab.sys, 'stdout', io.StringIO()):
                self.assertEqual(lab.supervise(paths, api=api), 1)
            logs = list(root.glob('*.stderr.log'))
            self.assertEqual(len(logs), 1)
            self.assertIn(b'QEMU startup rejected', logs[0].read_bytes())
        self.assertLess(api.events.index(('read', 'stderr')), api.events.index(('close', 'stderr')))


class ConfigurationTests(unittest.TestCase):
    def test_first_logon_command_respects_windows_setup_length_limit(self):
        template = Path(__file__).with_name('autounattend.xml.template')
        root = ET.parse(template).getroot()
        namespace = {'u': 'urn:schemas-microsoft-com:unattend'}
        command = root.find('.//u:FirstLogonCommands/u:SynchronousCommand/u:CommandLine', namespace).text
        self.assertTrue(command)
        self.assertLessEqual(len(command), 1024)
        self.assertNotIn('@@', command)

    def test_fixed_command_has_no_host_integration_or_unrestricted_network(self):
        paths = {name: Path('C:/private/lab') / name for name in ('qemu', 'code', 'vars', 'disk', 'iso', 'serial')}
        command = lab.build_command(paths)
        self.assertEqual(command[command.index('-m') + 1], '4096')
        self.assertEqual(command[command.index('-smp') + 1], '2')
        self.assertEqual(command[command.index('-accel') + 1], 'tcg,thread=multi')
        self.assertEqual(command[command.index('-display') + 1], 'none')
        self.assertIn('127.0.0.1:5', command)
        self.assertIn('user,id=labnet,restrict=on,hostfwd=tcp:127.0.0.1:13389-:3389', command)
        self.assertIn('-S', command)
        self.assertEqual(lab.WRITE_BPS, 16 * 1024 ** 2)
        self.assertNotIn('dump-guest-core=', ' '.join(command))
        self.assertIn('usb-tablet,bus=xhci.0', command)
        self.assertIn(f'throttling.bps-write={lab.WRITE_BPS}', ' '.join(command))
        for unsupported in ('-virtfs', '-fsdev', 'usb-host', 'vfio', 'whpx', 'host_device', '-loadvm', '-readconfig'):
            self.assertNotIn(unsupported, ' '.join(command))

    def test_optional_answer_media_is_readonly_second_cd(self):
        paths = {name: Path('C:/private/lab') / name for name in ('qemu', 'code', 'vars', 'disk', 'iso', 'serial', 'unattend')}
        command = lab.build_command(paths)
        self.assertEqual(len([arg for arg in command if 'media=cdrom,readonly=on,format=raw' in arg]), 2)

    def test_installed_guest_omits_bootable_installer_but_keeps_bootstrap_media(self):
        paths = {name: Path('C:/private/lab') / name for name in ('qemu', 'code', 'vars', 'disk', 'iso', 'serial', 'unattend')}
        command = lab.build_command(paths, installed_guest=True)
        self.assertEqual(command[command.index('-boot') + 1], 'order=c,menu=on')
        optical = [arg for arg in command if 'media=cdrom' in arg]
        self.assertEqual(optical, [f"if=ide,media=cdrom,readonly=on,format=raw,file={paths['unattend']}"])
        self.assertTrue(any(f"file={paths['disk']}," in arg for arg in command))

    def test_allowed_drive_is_pure_and_accepts_only_c_and_d(self):
        with patch.object(lab.os, 'lstat', side_effect=AssertionError('must not access filesystem')), \
                patch.object(lab.shutil, 'disk_usage', side_effect=AssertionError('must not query a volume')):
            for drive in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ':
                with self.subTest(drive=drive):
                    if drive in ('C', 'D'):
                        self.assertEqual(lab.allowed_drive(drive.lower() + ':/private/lab'), drive + ':\\')
                    else:
                        with self.assertRaises(ValueError):
                            lab.allowed_drive(drive + ':/private/lab')

    def test_disallowed_path_refused_before_filesystem_access(self):
        with patch.object(lab.os, 'lstat', side_effect=AssertionError('must not access filesystem')):
            for value in ('E:/private', '//server/share', '//?/D:/private', '//./C:/private',
                          'C:/private/../target', 'D:/private/./target', 'D:/private/.. /target',
                          'C:/file:stream', 'D:/file:stream', 'D:relative', 'C:/private//file',
                          'D:/private,extra', 'C:/private\tfile'):
                with self.subTest(value=value), self.assertRaises(ValueError):
                    lab.ordinary_lab_path(value)

    def test_ordinary_c_and_d_files_are_accepted_with_mocked_filesystem(self):
        with patch.object(lab.os, 'lstat', return_value=SimpleNamespace(st_file_attributes=0)) as stat, \
                patch.object(lab.Path, 'is_dir', return_value=False), patch.object(lab.Path, 'is_file', return_value=True):
            for drive in ('C', 'D'):
                value = drive + ':/private/file'
                self.assertEqual(lab.ordinary_lab_path(value), Path(value))
            self.assertEqual({path.args[0].drive.upper() for path in stat.call_args_list}, {'C:', 'D:'})

    def test_reparse_on_either_allowed_drive_is_refused(self):
        with patch.object(lab.os, 'lstat', return_value=SimpleNamespace(st_file_attributes=0x400)):
            for drive in ('C', 'D'):
                with self.subTest(drive=drive), self.assertRaisesRegex(ValueError, 'Reparse'):
                    lab.ordinary_lab_path(drive + ':/private/file')

    def test_late_forbidden_media_path_is_refused_before_any_filesystem_access(self):
        args = SimpleNamespace(root='D:/private/lab', qemu='C:/tools/qemu-system-x86_64.exe',
                               code='C:/tools/firmware.fd', vars='D:/private/lab/vars.fd',
                               disk='D:/private/lab/disk.qcow2', iso='E:/private/media.iso', unattend_iso=None)
        with patch.object(lab.os, 'lstat', side_effect=AssertionError('must not access filesystem')):
            with self.assertRaises(ValueError):
                lab.validate_paths(args)

    def test_d_lab_keeps_guest_assets_contained_and_allows_c_qemu(self):
        args = SimpleNamespace(root='D:/private/lab', qemu='C:/tools/qemu-system-x86_64.exe',
                               code='C:/tools/firmware.fd', vars='D:/private/lab/vars.fd',
                               disk='D:/private/lab/disk.qcow2', iso='D:/private/lab/media.iso',
                               unattend_iso='D:/private/lab/answer.iso')
        with patch.object(lab, 'ordinary_lab_path', side_effect=lambda value, **kwargs: Path(value)), \
                patch.object(lab.os.path, 'lexists', return_value=False), patch.object(lab, 'validate_qcow2'):
            paths = lab.validate_paths(args)
            self.assertEqual(paths['qemu'].drive, 'C:')
            for name in ('vars', 'disk', 'iso', 'unattend', 'logs', 'reports', 'serial'):
                self.assertTrue(lab.under(paths[name], paths['root']))
            for outside in ('D:/elsewhere/disk.qcow2', 'C:/private/lab/disk.qcow2'):
                args.disk = outside
                with self.subTest(outside=outside), self.assertRaises(ValueError):
                    lab.validate_paths(args)

    def test_qcow2_rejects_references_and_wrong_capacity(self):
        header = bytearray(104)
        struct.pack_into('>4sI', header, 0, b'QFI\xfb', 3)
        struct.pack_into('>Q', header, 24, 64 * lab.GIB)
        with tempfile.TemporaryDirectory(prefix='retsar-header-test-') as temp:
            path = Path(temp) / 'disk.qcow2'
            path.write_bytes(header)
            lab.validate_qcow2(path)
            for offset, fmt, value in ((8, '>Q', 104), (16, '>I', 5), (24, '>Q', 32 * lab.GIB),
                                       (32, '>I', 1), (60, '>I', 1), (72, '>Q', 4)):
                modified = bytearray(header)
                struct.pack_into(fmt, modified, offset, value)
                path.write_bytes(modified)
                with self.subTest(offset=offset), self.assertRaises(ValueError):
                    lab.validate_qcow2(path)

    def test_qmp_rejects_hotplug_hmp_host_paths_and_arbitrary_ids(self):
        for message in ({'execute': 'human-monitor-command'}, {'execute': 'blockdev-add'},
                        {'execute': 'device_add'}, {'execute': 'query-status', 'id': 'setup'},
                        {'execute': 'screendump', 'arguments': {'filename': 'E:/capture.ppm'}},
                        {'execute': 'screendump', 'arguments': {'filename': '../capture.ppm'}}):
            with self.subTest(message=message), self.assertRaises(ValueError):
                lab.safe_request(message, Path('C:/private/reports'))

    def test_qmp_accepts_bounded_guest_keys_and_pointer(self):
        message = {'execute': 'send-key', 'arguments': {'keys': [{'type': 'qcode', 'data': 'ret'}]}}
        self.assertEqual(lab.safe_request(message, Path('C:/private/reports')), message)
        pointer = {'execute': 'input-send-event', 'arguments': {'events': [{'type': 'abs', 'data': {'axis': 'x', 'value': 32767}}]}}
        self.assertEqual(lab.safe_request(pointer, Path('C:/private/reports')), pointer)
        pointer['arguments']['events'][0]['data']['value'] = 32768
        with self.assertRaises(ValueError):
            lab.safe_request(pointer, Path('C:/private/reports'))

    def test_capture_does_not_overwrite(self):
        with tempfile.TemporaryDirectory(prefix='retsar-capture-test-') as temp:
            reports = Path(temp)
            (reports / 'old.ppm').write_bytes(b'existing')
            with self.assertRaises(ValueError):
                lab.safe_request({'execute': 'screendump', 'arguments': {'filename': 'old.ppm'}}, reports)
            result = lab.safe_request({'execute': 'screendump', 'arguments': {'filename': 'new.ppm'}}, reports)
            self.assertEqual(result['arguments']['filename'], str(reports / 'new.ppm'))


if __name__ == '__main__':
    unittest.main()
