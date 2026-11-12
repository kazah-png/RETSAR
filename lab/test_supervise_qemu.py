"""Unit tests use fake Win32 calls. No guest or child process is started."""
import importlib.util
import io
from pathlib import Path
import struct
import tempfile
import threading
from types import SimpleNamespace
import unittest
import xml.etree.ElementTree as ET
from unittest.mock import call, patch

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
