#!/usr/bin/env python3
"""Translate Lamentum's NVN shader programs into GLSL with the Hades-lineage compiler.

Owner data stays local: this script reads the owner image segments, writes the per-program
blobs and the produced GLSL into a workspace directory, and prints a report. Only our own
GLSL may be committed; the stage blobs are game data.

  python3 tools/translate_lamentum_shaders.py <image-dir> <workspace> [--compiler PATH]

Layout facts measured on this title (SDK 10.9.0.0):
  * control section: magic 0x98761234, version 1, control size at +0x20, code size at +0x704;
  * the code section follows the control section and carries the compiler's own 0x30-byte
    header plus an SPH whose first word (offset +0x30) carries the stage in bits 10-13
    (1 = vertex, 5 = fragment); the SASS starts at +0x80;
  * the CLI wants exactly `[0x30 header][parameters][code]`, so the code blob is fed as-is.
"""
import argparse, hashlib, json, pathlib, struct, subprocess, sys

MAGIC = 0x98761234
DEFAULT_COMPILER = ('/mnt/external/Hermes/spikes/wol-switch/tools/hades-eval/'
                    'shader-compiler-rs/target/release/shader-compiler-cli')
SEGMENTS = ('main.text.seg', 'main.rodata.seg', 'subsdk0.text.seg', 'subsdk0.rodata.seg',
            'sdk.text.seg', 'sdk.rodata.seg')


def scan(path):
    data = path.read_bytes()
    for off in range(0, max(0, len(data) - 0x1000), 4):
        if struct.unpack_from('<I', data, off)[0] != MAGIC:
            continue
        control_size = struct.unpack_from('<I', data, off + 0x20)[0]
        code_size = struct.unpack_from('<I', data, off + 0x704)[0]
        if not (0x80 <= control_size <= 0x4000) or not (0x20 <= code_size <= 0x100000):
            continue
        control = data[off:off + control_size]
        code = data[off + control_size:off + control_size + code_size]
        if len(code) != code_size:
            continue
        yield off, control, code


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('image', type=pathlib.Path)
    ap.add_argument('workspace', type=pathlib.Path)
    ap.add_argument('--compiler', default=DEFAULT_COMPILER)
    a = ap.parse_args()
    a.workspace.mkdir(parents=True, exist_ok=True)
    compiler = pathlib.Path(a.compiler)
    if not compiler.exists():
        sys.exit('compiler not found: %s' % compiler)

    report = []
    index = 0
    for seg in SEGMENTS:
        path = a.image / seg
        if not path.exists():
            continue
        for off, control, code in scan(path):
            index += 1
            stage = (struct.unpack_from('<I', code, 0x30)[0] >> 10) & 0xF if len(code) > 0x34 else 0
            name = '%s-%04x' % (seg.split('.')[0], off)
            (a.workspace / (name + '.control.bin')).write_bytes(control)
            (a.workspace / (name + '.code.bin')).write_bytes(code)
            out = a.workspace / (name + '.glsl')
            proc = subprocess.run([str(compiler), str(a.workspace / (name + '.code.bin')), str(out)],
                                  capture_output=True, text=True, timeout=120)
            entry = {
                'name': name, 'segment': seg, 'offset': hex(off),
                'control_size': len(control), 'code_size': len(code),
                'code_sha256': hashlib.sha256(code).hexdigest(),
                'control_sha256': hashlib.sha256(control).hexdigest(),
                'sph_stage_field': stage,
                'vertex' if stage == 1 else 'fragment' if stage == 5 else 'unknown': True,
                'glsl': out.exists(),
                'glsl_endianness_void': bool(out.exists() and out.stat().st_size < 200),
                'stderr': proc.stderr.strip()[-200:],
            }
            report.append(entry)
            print(json.dumps(entry))
    (a.workspace / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('programs:', len(report))


if __name__ == '__main__':
    main()
