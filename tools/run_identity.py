#!/usr/bin/env python3
"""Owned-run bootstrap. Install beside lamentum-nextos as run_identity.py.

Only UUID/process/executable/launcher identity is qualified here. Private image
and compiled shader bindings are explicitly unknown until a verified build/deploy
manifest is integrated. No GL/game/page-policy changes and no proc-name guessing.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys


def validate_uuid(value):
    import re
    if not isinstance(value, str) or not re.fullmatch(
            r'[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}', value):
        raise ValueError('invalid canonical kernel run UUID')
    return value


def new_run_uuid():
    from pathlib import Path
    return validate_uuid(Path('/proc/sys/kernel/random/uuid').read_text().strip())


def pinned_file(path):
    """Hash one opened regular file; preserve its FD for identity-safe execve."""
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    try:
        first = os.fstat(fd)
        if not stat.S_ISREG(first.st_mode):
            raise ValueError('identity input is not a regular file')
        digest = hashlib.sha256()
        for data in iter(lambda: os.read(fd, 131072), b''):
            digest.update(data)
        last = os.fstat(fd)
        fields = ('st_dev', 'st_ino', 'st_size', 'st_mtime_ns', 'st_ctime_ns')
        if any(getattr(first, key) != getattr(last, key) for key in fields):
            raise RuntimeError('identity input changed during hash')
        os.lseek(fd, 0, os.SEEK_SET)
        return fd, {'dev': first.st_dev, 'ino': first.st_ino, 'bytes': first.st_size,
                    'mtime_ns': first.st_mtime_ns, 'sha256': digest.hexdigest()}
    except BaseException:
        os.close(fd)
        raise


def exec_owned(runtime, launcher, run_uuid):
    run_uuid = validate_uuid(run_uuid)
    if os.execve not in os.supports_fd:
        raise RuntimeError('Python execve(fd) is unavailable; pathname fallback is forbidden')
    fd, identity = pinned_file(runtime)
    try:
        launcher_fd, launcher_identity = pinned_file(launcher)
        os.close(launcher_fd)
        helper_fd, helper_identity = pinned_file(__file__)
        os.close(helper_fd)
        text = Path('/proc/self/stat').read_text()
        starttime = int(text[text.rindex(')') + 1:].split()[19])
        header = {'schema': 1, 'run_uuid': run_uuid, 'launch_nonce': new_run_uuid(),
                  'boot_id': Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
                  'pid': os.getpid(), 'starttime': starttime,
                  'elf_dev': identity['dev'], 'elf_ino': identity['ino'],
                  'elf_bytes': identity['bytes'], 'elf_sha256': identity['sha256'],
                  'launcher_sha256': launcher_identity['sha256'],
                  'helper_sha256': helper_identity['sha256'],
                  'image_sha256': None, 'catalogue_sha256': None,
                  'content_bindings_complete': False,
                  'execution_contract': 'execve-open-fd; observer must verify /proc/PID/exe after exec'}
        env = dict(os.environ, LAMENTUM_RUN_UUID=run_uuid)
        # Several recovered capture/audio helpers require <=12 DECIMAL characters.
        # Preserve that ABI as a legacy label, NEVER as the run ownership key.
        env['COTM_RUN_ID'] = str(int(run_uuid.replace('-', '')[:9], 16))
        print('LAMENTUM_RUN_HEADER ' + json.dumps(header, sort_keys=True, separators=(',', ':')), flush=True)
        print('LAMENTUM_BUILD_ID sha256=' + identity['sha256'], flush=True)
        print('LAMENTUM_LAUNCH native-horizon budget=%ss shader-catalogue=draft-with-TEST-substitutes'
              % env.get('LAMENTUM_PROBE_SECONDS', '120'), flush=True)
        os.set_inheritable(fd, True)  # also permits explicitly synthetic shebang fixtures
        os.execve(fd, [str(runtime)], env)
    finally:
        os.close(fd)


def terminate_owned(owner):
    """TERM through a pidfd only after boot/starttime/executable checks; no PID fallback."""
    import ctypes
    import errno
    import os
    import platform
    import signal
    from pathlib import Path
    keys = ('boot_id', 'pid', 'starttime', 'elf_dev', 'elf_ino')
    if not owner or any(key not in owner for key in keys):
        raise ValueError('complete owned process identity required')
    libc = None
    try:
        if hasattr(os, 'pidfd_open'):
            fd = os.pidfd_open(owner['pid'], 0)
        else:
            # Linux AArch64 syscall numbers were checked against asm/unistd.h
            # in the pinned cross-toolchain; no unsupported-architecture guess.
            if platform.machine() != 'aarch64':
                return {'outcome': 'pidfd_unavailable', 'reason': 'unsupported syscall ABI'}
            libc = ctypes.CDLL(None, use_errno=True)
            libc.syscall.restype = ctypes.c_long
            fd = libc.syscall(ctypes.c_long(434), ctypes.c_int(owner['pid']), ctypes.c_uint(0))
            if fd < 0:
                raise OSError(ctypes.get_errno(), 'pidfd_open')
    except OSError as error:
        return {'outcome': 'already_exited' if error.errno == errno.ESRCH else 'pidfd_unavailable',
                'errno': error.errno}
    try:
        base = Path('/proc') / str(owner['pid'])
        text = (base / 'stat').read_text()
        executable = os.stat(base / 'exe')
        seen = {'boot_id': Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
                'pid': owner['pid'], 'starttime': int(text[text.rindex(')')+1:].split()[19]),
                'elf_dev': executable.st_dev, 'elf_ino': executable.st_ino}
        if any(seen[key] != owner[key] for key in keys):
            return {'outcome': 'identity_mismatch', 'observed': seen}
        if hasattr(signal, 'pidfd_send_signal'):
            signal.pidfd_send_signal(fd, signal.SIGTERM)
        else:
            if platform.machine() != 'aarch64':
                return {'outcome': 'pidfd_unavailable', 'reason': 'unsupported signal syscall ABI'}
            if libc is None:
                libc = ctypes.CDLL(None, use_errno=True)
                libc.syscall.restype = ctypes.c_long
            result = libc.syscall(ctypes.c_long(424), ctypes.c_int(fd), ctypes.c_int(signal.SIGTERM),
                                  ctypes.c_void_p(0), ctypes.c_uint(0))
            if result:
                raise OSError(ctypes.get_errno(), 'pidfd_send_signal')
        return {'outcome': 'pidfd_TERM', 'owner': owner}
    except OSError as error:
        return {'outcome': 'already_exited' if error.errno in (errno.ESRCH, errno.ENOENT) else 'pidfd_error',
                'errno': error.errno}
    finally:
        os.close(fd)


def uuid_request():
    import inspect
    return ('import json\n' + inspect.getsource(validate_uuid) + '\n' + inspect.getsource(new_run_uuid)
            + '\nprint(json.dumps({"run_uuid":new_run_uuid()}))\n')


def terminate_request(owner):
    import inspect
    return ('import json\n' + inspect.getsource(terminate_owned)
            + '\nprint(json.dumps(terminate_owned(%r)))\n' % owner)


def preflight_request(runtime, launcher, helper):
    import inspect
    return ('import os,stat,hashlib,json\n' + inspect.getsource(pinned_file)
            + '\nfrom pathlib import Path\nresult={}\n'
            + 'for key,path in %r.items():\n fd,data=pinned_file(path);os.close(fd);result[key]=data\n'
            % {'runtime': str(runtime), 'launcher': str(launcher), 'helper': str(helper)}
            + 'result["boot_id"]=Path("/proc/sys/kernel/random/boot_id").read_text().strip()\n'
            + 'print(json.dumps(result))\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('new', 'exec'))
    parser.add_argument('--runtime', type=Path)
    parser.add_argument('--launcher', type=Path)
    parser.add_argument('--run-uuid')
    args = parser.parse_args()
    if args.command == 'new':
        print(new_run_uuid())
    else:
        if args.runtime is None or args.launcher is None:
            parser.error('exec requires --runtime and --launcher')
        exec_owned(args.runtime, args.launcher, args.run_uuid if args.run_uuid is not None else new_run_uuid())


if __name__ == '__main__':
    main()
