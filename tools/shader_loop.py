#!/usr/bin/env python3
"""Unattended shader-catalogue loop for the Lamentum port.

One iteration: build the runtime with the current catalogue -> deploy -> run the port through
the Spruce principal flow with the shader dump enabled -> if the run stopped on a program the
catalogue does not carry, fetch that program's blobs, translate them, fold them into the
catalogue (TEST-ONLY substitutes where ESSL cannot express the program) -> next iteration.

Stops when a run ends on anything other than ZERO_SHADER_NOT_PINNED (a new frontier), when the
iteration budget is spent, or when a step fails. Owner blobs stay in the local workspace; only
our GLSL and hashes reach the repository.

  python3 tools/shader_loop.py --iterations 30 --budget 45
"""
import argparse, json, os, pathlib, re, subprocess, sys, time

HOST = 'root@192.168.50.135'
PORT = '/mnt/SDCARD/Roms/PORTS/lamentum-switch'
WORK = pathlib.Path('/home/ruslan/lamentum-work')
REPO = WORK / 'repo'
COMPILER = pathlib.Path('/mnt/external/Hermes/spikes/wol-switch/tools/hades-eval/'
                        'shader-compiler-rs/target/release/shader-compiler-cli')
DOCKER_IMAGE = 'pmjs-arm64-spruce-local:20261002-gcc10'
IMAGE = WORK / 'image-09'


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def ssh(cmd, timeout=120):
    return run(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10', HOST, cmd], timeout=timeout)


def build(tag):
    out = WORK / tag
    if out.exists():
        return None
    r = run(['docker', 'run', '--rm', '--security-opt', 'label=disable',
             '-e', 'COTM_CC=/work/tools/cc-lamentum.sh',
             '-v', f'{WORK}/runtime-src:/reference:ro',
             '-v', f'{IMAGE}:/image:ro',
             '-v', f'{REPO}:/work',
             '--entrypoint', '/bin/sh', DOCKER_IMAGE,
             '-c', f'cd /work && bash build_lamentum.sh /reference /image /work/{tag}'],
            timeout=900)
    log = work_log = WORK / (tag + '.log')
    log.write_text(r.stdout + r.stderr)
    binary = REPO / tag / 'lamentum-nextos'
    if not binary.exists():
        raise SystemExit(f'build failed: see {log}')
    return binary


def deploy(binary):
    data = binary.read_bytes()
    r = subprocess.run(['ssh', '-o', 'BatchMode=yes', HOST,
                        f'cat > {PORT}/lamentum-nextos'], input=data, capture_output=True)
    if r.returncode != 0:
        return False
    ssh(f'chmod 755 {PORT}/lamentum-nextos; sync')
    return True


def run_device(budget, evidence):
    return run([sys.executable, str(REPO / 'tools/device_principal_test.py'),
                '--budget', str(budget), '--wait', '150',
                '--env', 'LC2_SHADER_DUMP=1', '--env', 'AW_RELEASE=0',
                '--evidence', str(evidence)], timeout=400)


def fetch_new_programs(run_id):
    """Pull the dumped unrecognised programs and return [(name, stage_hint)] for new ones."""
    listing = ssh(f'ls {PORT}/nvn-lc2-shader-code-*.bin 2>/dev/null').stdout.split()
    got = []
    for remote in listing:
        m = re.search(r'code-(\d+)-(\d+)\.bin$', remote)
        if not m:
            continue
        name = 'dump-%s-%s' % (m.group(1), m.group(2))
        # The device clock does not advance between runs, so the dump file names repeat: always
        # fetch and key the pair by the blob's own hash instead of its name.
        code_dst = WORK / 'shader-work' / (name + '.code.bin')
        if code_dst.exists():
            code_dst.unlink()
        ctl_old = WORK / 'shader-work' / (name + '.control.bin')
        if ctl_old.exists():
            ctl_old.unlink()
        control_remote = remote.replace('code-', 'control-')
        for remote_path, dst in ((remote, code_dst),
                                 (control_remote, WORK / 'shader-work' / (name + '.control.bin'))):
            r = subprocess.run(['ssh', '-o', 'BatchMode=yes', HOST, f'cat {remote_path}'],
                               capture_output=True)
            if r.returncode != 0 or not r.stdout:
                break
            dst.write_bytes(r.stdout)
        if code_dst.exists():
            # Name the pair by its own hash so repeated runs of the same program dedupe naturally.
            import hashlib
            digest = hashlib.sha256(code_dst.read_bytes()).hexdigest()[:8]
            hashed = WORK / 'shader-work' / ('dump-%s.code.bin' % digest)
            hashed_control = WORK / 'shader-work' / ('dump-%s.control.bin' % digest)
            if not hashed.exists():
                code_dst.rename(hashed)
                ctl = WORK / 'shader-work' / (name + '.control.bin')
                if ctl.exists():
                    ctl.rename(hashed_control)
                got.append('dump-%s' % digest)
    return got


def translate(name):
    code = WORK / 'shader-work' / (name + '.code.bin')
    out = WORK / 'shader-work' / (name + '.glsl')
    if out.exists():
        return True
    r = run([str(COMPILER), str(code), str(out)], timeout=120)
    return out.exists()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--iterations', type=int, default=25)
    ap.add_argument('--budget', type=int, default=45)
    ap.add_argument('--start', type=int, default=31)
    a = ap.parse_args()

    summary = []
    for i in range(a.iterations):
        tag = 'build-%d' % (a.start + i)
        print('== iteration %d (%s)' % (i + 1, tag), flush=True)
        binary = build(tag)
        if binary is None:
            print('   build dir exists, reusing', flush=True)
            binary = REPO / tag / 'lamentum-nextos'
        if not deploy(binary):
            summary.append({'iteration': i, 'result': 'deploy-failed'})
            break
        evidence = REPO / 'evidence' / ('loop-%02d' % (i + 1))
        run_device(a.budget, evidence)
        log = (evidence / 'latest.log')
        text = log.read_text(errors='replace') if log.exists() else ''
        stop = next((l for l in text.splitlines() if l.startswith('STOP status=')), '')
        dumped = [l for l in text.splitlines() if l.startswith('LC2_SHADER_DUMPED')]
        print('   stop:', stop[:120], '| dumps:', len(dumped), flush=True)
        summary.append({'iteration': i, 'stop': stop[:160], 'dumps': len(dumped)})
        if 'ZERO_SHADER_NOT_PINNED' not in stop:
            print('   new frontier, stopping the loop', flush=True)
            break
        run_id = re.search(r'COTM_RUN_ID', text)
        new = fetch_new_programs(None)
        progressed = False
        for name in new:
            if translate(name):
                progressed = True
                print('   translated', name, flush=True)
        if not progressed and not new:
            print('   nothing new to translate, stopping', flush=True)
            break
        r = run([sys.executable, str(REPO / 'tools/build_lamentum_catalogue.py'),
                 str(WORK / 'shader-work'), str(REPO / 'runtime/lamentum-shaders-draft.h'),
                 '--placeholder'], timeout=300)
        print('   catalogue:', (r.stdout or r.stderr).strip().splitlines()[-1:], flush=True)
    (WORK / 'shader-loop-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print('summary written', flush=True)


if __name__ == '__main__':
    main()
