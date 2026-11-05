#!/usr/bin/env python3
"""Bounded VM-only run: isolated saves, exact child cleanup, real X11 captures."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time

def content_frame(line):
    sample = re.match(r'NVN_PRESENT_PIXELS frame=([0-9]+) nonblack=([0-9]+) .*texture-draws=([0-9]+)', line)
    return bool(sample and int(sample[2]) > 0 and int(sample[3]) > 1)

# Ignore the first undefined surface and actual black presents.
assert not content_frame('NVN_PRESENT_PIXELS frame=1 nonblack=921600 max=188 texture-draws=1')
assert not content_frame('NVN_PRESENT_PIXELS frame=144 nonblack=0 max=0 texture-draws=48')
assert content_frame('NVN_PRESENT_PIXELS frame=600 nonblack=42841 max=32 texture-draws=200')

source = Path(sys.argv[1]).resolve()
tag = sys.argv[2]
assert source.parent == Path('/mnt/host') and source.name in ('baseline', 'candidate0170')
assert re.fullmatch(r'0170[ab]', tag), tag
runtime = Path('/mnt/host/baseline/infernax-nextos')
assert hashlib.sha256(runtime.read_bytes()).hexdigest() == '258a53591bdb77d7a41a92be2a7453fb4e4b8ee1282754a5aa5607fa383cbe86'
runid = str(int(time.time()))
work = Path('/opt/infernax-runs') / (tag + '-' + runid)
work.mkdir(mode=0o700, parents=True, exist_ok=False)
evidence = Path('/mnt/host/evidence') / work.name
evidence.mkdir(parents=True, exist_ok=False)
for name in [p.name for p in source.glob('*.seg')]:
    assert (source / name).is_file(), name
    (work / name).symlink_to(source / name)
# The production RomFS verifier deliberately rejects a symlink (O_NOFOLLOW).
romfs = work / 'infernax-romfs.section'
shutil.copyfile(source / romfs.name, romfs)
with romfs.open('rb') as stream:
    assert hashlib.file_digest(stream, 'sha256').hexdigest() == json.loads(Path('/mnt/host/baseline/manifest.json').read_text())[romfs.name]['sha256']
(work / 'save-laboratory').mkdir()
log = (evidence / 'game.log').open('xb', buffering=0)
events = (evidence / 'events.jsonl').open('x', buffering=1)

def event(kind, **fields):
    events.write(json.dumps(dict(event=kind, mono=time.monotonic(), **fields)) + '\n')

def capture(name):
    p = evidence / (name + '.png')
    r = subprocess.run(['import', '-display', ':0', '-window', 'root', str(p)], timeout=20, capture_output=True)
    event('CAPTURE', path=str(p), rc=r.returncode, stderr=r.stderr.decode(errors='replace'))
    assert r.returncode == 0, 'capture failed'

env = dict(os.environ, DISPLAY=':0', SDL_VIDEODRIVER='x11',
           SDL_VIDEO_X11_FORCE_EGL='1', T2_DISPLAY_BACKEND='sdl',
           SDL_AUDIODRIVER='alsa', AUDIODEV='plughw:0,0',
           COTM_GL_THREAD='0', COTM_SINGLE_CONTEXT='0', COTM_ASYNC_FRAME='0',
           T2_GPU_PIPELINE='0', MEGAMAN_PROBE_SECONDS='900',
           COTM_BLACK_LIMIT='3000', T2_MEMORY_FLOOR_MIB='8', T2_HOST_CPU_BASE='4',
           COTM_GL_ERROR_PER_LIST='1', NX_PRESENT_CLEAR='0', NX_PROBE_NO_DEPTH='0',
           NX_PROBE_QUAD='0', NX_SHADER_DUMP='1', COTM_RUN_ID=runid,
           NX_SCENE_AUDIT='1', NX_PROBE_FULL='0', NX_PROBE_DUMP='1', NX_TGT_FIRST='1',
           MOMO_TRACE_UNIFORM_BINDS='0')
assert 'NX_NVDEC_OPEN_DENY' not in env
padtool = '/mnt/host/tools/uinput_pad.py'
env['SDL_GAMECONTROLLERCONFIG'] = subprocess.check_output([sys.executable, padtool, '--mapping'], text=True).strip()
padenv = dict(env, UINPUT_FIFO=str(work / 'pad.fifo'))
padlog = (evidence / 'pad.log').open('xb', buffering=0)
pad = subprocess.Popen([sys.executable, padtool, 'wait:1:0'], env=padenv, stdout=padlog, stderr=subprocess.STDOUT, start_new_session=True)
game = None
try:
    for _ in range(50):
        assert pad.poll() is None, 'uinput pad stopped'
        if (work / 'pad.fifo').exists():
            break
        time.sleep(0.1)
    assert (work / 'pad.fifo').is_fifo(), 'uinput pad not ready'
    capture('desktop-before')
    game = subprocess.Popen([str(runtime)], cwd=work, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    event('BEGIN', pid=game.pid, source=str(source), work=str(work), runid=runid,
          limit_seconds=900, scope='VM-only; not TSPS memory/performance', environment={k:v for k,v in env.items() if k.startswith(('NX_', 'COTM_', 'T2_', 'SDL_', 'MEGAMAN_'))})
    reader = (evidence / 'game.log').open('r', errors='replace')
    start = time.monotonic()
    first = None
    captured = 0
    while game.poll() is None and time.monotonic() - start < 900:
        text = reader.read()
        for line in text.splitlines():
            if line.startswith(('STOP ', 'NV_OPEN ', 'HID_PHYSICAL_OPEN ', 'NVN_PRESENT_PIXELS ', 'NVN_SYSTEM_WINDOW ', 'NVN_PROVIDER_MEASURED ')):
                event('FRONTIER', line=line)
            if first is None and content_frame(line):
                first = time.monotonic()
        if first is not None and captured < 3 and time.monotonic() - first >= captured * 5:
            capture('content-' + str(captured))
            captured += 1
        time.sleep(0.25)
    event('END', rc=game.poll(), elapsed=time.monotonic()-start, timed_out=game.poll() is None)
    capture('screen-at-end')
finally:
    for child in (game, pad):
        if child is not None and child.poll() is None:
            os.killpg(child.pid, signal.SIGTERM)
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait(timeout=5)
    event('CLEANUP', game_rc=game.returncode if game else None, pad_rc=pad.returncode)
    log.close()
    padlog.close()
    events.close()
print(evidence, flush=True)
