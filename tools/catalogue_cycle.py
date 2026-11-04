#!/usr/bin/env python3
"""One shader-catalogue cycle: capture the program the run stops on, append it, rebuild, rerun.

Unlike tools/shader_loop.py this NEVER regenerates the catalogue (a regeneration
reverts hand-validated entries), and it runs the port with the measured memory
configuration that actually reaches the scene, so the stop it captures is the real
frontier. Owner blobs stay in the local workspace.
"""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys

HOST = 'root@192.168.50.135'
PORT = '/mnt/SDCARD/Roms/PORTS/lamentum-switch'
WORK = pathlib.Path('/home/ruslan/lamentum-work')
REPO = WORK / 'repo'
SHADERS = WORK / 'shader-work'
COMPILER = pathlib.Path('/mnt/external/Hermes/spikes/wol-switch/tools/hades-eval/'
                        'shader-compiler-rs/target/release/shader-compiler-cli')
IMAGE = 'pmjs-arm64-spruce-local:20261002-gcc10'
BASE_ENV = ['LC2_GUEST_QUOTA_MIB=3072', 'LC2_BC_LAZY_ALL=1', 'LC2_BC_LAZY_UPLOAD=1',
            'LC2_BC_FILE_BACKING=/mnt/SDCARD', 'LC2_BC_FILE_BACKING_EXTRA=/mnt/UDISK',
            'LC2_BC_FILE_CAP_MIB=1280', 'T2_MEMORY_FLOOR_MIB=32', 'T2_GUEST_ZRAM=1',
            'T2_GZRAM_BLOCK_KIB=16', 'T2_GZRAM_COLD_MS=10', 'T2_GZRAM_TARGET_MIB=512',
            'T2_GZRAM_PERIOD_MS=20', 'T2_GZRAM_BUDGET_MIB=32']


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def ssh(cmd, timeout=120, binary=False):
    args = ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10', HOST, cmd]
    if binary:
        return subprocess.run(args, capture_output=True, timeout=timeout)
    return run(args, timeout=timeout)


def build(tag):
    out = REPO / tag
    if (out / 'lamentum-nextos').exists():
        return out / 'lamentum-nextos'
    r = subprocess.run(['docker', 'run', '--rm', '--security-opt', 'label=disable',
                        '-e', 'COTM_CC=/work/tools/cc-lamentum.sh',
                        '-v', f'{WORK}/runtime-src:/reference:ro', '-v', f'{WORK}/image-09:/image:ro',
                        '-v', f'{REPO}:/work', '--entrypoint', '/bin/sh', IMAGE,
                        '-c', f'cd /work && bash build_lamentum.sh /reference /image /work/{tag}'],
                       capture_output=True, text=True, timeout=1200)
    (WORK / (tag + '.log')).write_text(r.stdout + r.stderr)
    binary = out / 'lamentum-nextos'
    if not binary.exists():
        raise SystemExit('build failed: ' + str(WORK / (tag + '.log')))
    return binary


def deploy(binary, backup_tag):
    data = binary.read_bytes()
    dst = PORT + '/lamentum-nextos'
    r = subprocess.run(['ssh', '-o', 'BatchMode=yes', HOST,
                        f'cp -p {dst} {dst}.before-{backup_tag}; cat > {dst}; chmod 755 {dst}; sync'],
                       input=data, capture_output=True, timeout=60)
    if r.returncode:
        raise SystemExit('deploy failed')
    got = ssh(f'sha256sum {dst}').stdout.split()[0]
    if got != hashlib.sha256(data).hexdigest():
        raise SystemExit('deployed hash mismatch')
    return got


def run_port(evidence, budget, dump=True):
    ssh(f'rm -f {PORT}/nvn-lc2-shader-*.bin')
    env = list(BASE_ENV) + ['COTM_RUN_ID=1']
    if dump:
        env += ['AW_RELEASE=0', 'LC2_SHADER_DUMP=1']
    cmd = [sys.executable, str(REPO / 'tools/device_principal_test.py'), '--budget', str(budget),
           '--wait', str(budget + 40), '--evidence', str(evidence)]
    for e in env:
        cmd += ['--env', e]
    subprocess.run(cmd, capture_output=True, text=True, timeout=budget + 180)
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence / 'env.json').write_text(json.dumps(env, indent=2) + '\n')
    log = evidence / 'latest.log'
    text = log.read_text(errors='replace') if log.exists() else ''
    return text


def fetch_programs():
    listing = ssh(f'ls {PORT}/nvn-lc2-shader-code-*.bin 2>/dev/null').stdout.split()
    got = []
    for remote in listing:
        code = ssh(f'cat {remote}', binary=True).stdout
        control = ssh(f'cat {remote.replace("code-", "control-")}', binary=True).stdout
        if not code or not control:
            continue
        digest = hashlib.sha256(code).hexdigest()[:8]
        name = 'dump-' + digest
        (SHADERS / (name + '.code.bin')).write_bytes(code)
        (SHADERS / (name + '.control.bin')).write_bytes(control)
        if not (SHADERS / (name + '.glsl')).exists():
            run([str(COMPILER), str(SHADERS / (name + '.code.bin')), str(SHADERS / (name + '.glsl'))], timeout=180)
        if (SHADERS / (name + '.glsl')).exists():
            got.append(name)
    return sorted(set(got))


def append(name):
    tmp = WORK / 'catalogue-next.h'
    r = run([sys.executable, str(REPO / 'tools/append_program.py'), str(SHADERS),
             str(REPO / 'runtime/lamentum-shaders-draft.h'), str(tmp), name], timeout=300)
    if r.returncode:
        if 'already catalogued' in (r.stderr or ''):
            return None
        raise SystemExit('append failed: ' + r.stderr[-400:])
    (REPO / 'runtime/lamentum-shaders-draft.h').write_text(tmp.read_text())
    return json.loads(r.stdout)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--iterations', type=int, default=1)
    ap.add_argument('--budget', type=int, default=290)
    ap.add_argument('--start', type=int, default=190)
    ap.add_argument('--evidence-prefix', default='cycle')
    a = ap.parse_args()
    summary = []
    for i in range(a.iterations):
        tag = 'build-%d' % (a.start + i)
        evidence = REPO / 'evidence' / ('%s-%02d' % (a.evidence_prefix, i + 1))
        binary = build(tag)
        print('built', tag, flush=True)
        print('deployed', deploy(binary, tag), flush=True)
        text = run_port(evidence, a.budget)
        stop = next((l for l in text.splitlines() if l.startswith('STOP status=')), '')
        frames = re.findall(r'NVN_PRESENT_PIXELS frame=(\d+)', text)
        textures = sum(l.startswith('NVN_TEXTURE_REAL') for l in text.splitlines())
        print('stop:', stop[:140], '| textures:', textures,
              '| frames:', max(map(int, frames)) if frames else 0, flush=True)
        entry = {'iteration': i + 1, 'tag': tag, 'stop': stop[:200], 'textures': textures,
                 'frame_ceiling': max(map(int, frames)) if frames else 0}
        if 'ZERO_SHADER_NOT_PINNED' not in stop:
            entry['result'] = 'new frontier'
            summary.append(entry)
            break
        added = [x for x in (append(n) for n in fetch_programs()) if x]
        entry['added'] = added
        print('added:', added, flush=True)
        summary.append(entry)
        if not added:
            print('no new program could be added; stopping', flush=True)
            break
    (WORK / 'catalogue-cycle-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print('done')


if __name__ == '__main__':
    main()
