"""Explicit native guardian test: starts only two sleeping Python helper children.

This is not a VM test. No QEMU, Windows session, service, device or registry work.
Run only after reviewing supervise_qemu.py and with --run-owned-child-test.
"""
import argparse
import ctypes
from ctypes import wintypes as wt
from pathlib import Path
import sys

from supervise_qemu import OwnedChild, WindowsApi


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-owned-child-test', action='store_true')
    args = parser.parse_args()
    if not args.run_owned_child_test:
        parser.error('No process started; explicit --run-owned-child-test is required')
    api = WindowsApi()
    api.dll.GetPriorityClass.restype = wt.DWORD
    api.dll.GetPriorityClass.argtypes = [wt.HANDLE]
    command = [sys.executable, '-I', '-c', 'import time; time.sleep(60)']
    for mode in ('terminate-job', 'close-job'):
        child = OwnedChild(api)
        try:
            child.start(command, Path(__file__).parent)
            if api.poll(child.process) is not None:
                raise RuntimeError('Owned helper exited before the test')
            flags = wt.DWORD()
            api.check(api.dll.GetHandleInformation(child.job, ctypes.byref(flags)))
            if flags.value & 1 or api.dll.GetPriorityClass(child.process) != 0x4000:
                raise RuntimeError('Job inheritance or child priority invariant failed')
            if mode == 'terminate-job':
                child.terminate()
            else:
                api.close(child.job)
                child.job = None
            if api.dll.WaitForSingleObject(child.process, 5000) != 0 or api.poll(child.process) is None:
                raise RuntimeError('Owned helper did not terminate within five seconds')
            print('PASS native guardian ' + mode + '; only the newly created sleeping helper was affected')
        finally:
            child.close()
    print('No VM or real RDS session was exercised.')


if __name__ == '__main__':
    main()
