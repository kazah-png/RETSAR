"""Unit tests use fake Win32 calls. No guest or child process is started."""
import importlib.util
import io
from pathlib import Path
import struct
import tempfile
import threading
import unittest
from unittest.mock import patch

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

    def test_non_c_path_refused_before_filesystem_access(self):
        with patch.object(lab.os, 'lstat', side_effect=AssertionError('must not access filesystem')):
            for value in ('E:/private', 'D:/private', '//server/share', 'C:/private/../target', 'C:/file:stream'):
                with self.subTest(value=value), self.assertRaises(ValueError):
                    lab.ordinary_c_path(value)

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
