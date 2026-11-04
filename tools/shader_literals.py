#!/usr/bin/env python3
"""Specialize static float reads from captured shader-literal bank 1.

Inputs are owner captures, never committed. Code/control hashes must match the
catalogue. Preserve application cbuf metadata; do not infer its API slot here.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


def inline_literal_cbuf(source, candidates, code, control):
    names = {n for n in candidates if re.fullmatch(r'c(?:_?\d+)+', n)
             and re.search(r'\buniform\s+(?:highp\s+)?(?:sampler2D|float)\s+' + re.escape(n) +
                          r'\s*(?:\[[^\]]*\])?\s*;', source)}
    if not names:
        return source, set()
    if len(control) < 0x708:
        raise ValueError('truncated shader control')
    offset, end = struct.unpack_from('<II', control, 0x700)
    if end != len(code) or not (0 < offset < end) or offset % 0x100:
        raise ValueError('captured literal range disagrees with code size/alignment')
    for name in sorted(names):
        pattern = (r'texelFetch\(' + re.escape(name) +
                   r', ivec2\(\(int\((\d+[uU]?)\) >> 2\) & 1023, int\(\1\) >> 12\), 0\)\[int\(\1\) & 3\]')
        def replace(m):
            index = int(m[1].rstrip('uU'))
            pos = offset + 4 * index
            if pos + 4 > end:
                raise ValueError('literal read outside captured range: ' + m[0])
            word = struct.unpack_from('<I', code, pos)[0]
            return 'uintBitsToFloat(0x%08xu)' % word
        source = re.sub(pattern, replace, source)
        # ESSL310 leaves the block as a uniform float array (`c4.data[9u]`); a constant index
        # is the same literal read, so it inlines just like the texelFetch form above.
        def replace_array(m):
            index = int(m[1].rstrip('uU'))
            pos = offset + 4 * index
            if pos + 4 > end:
                raise ValueError('literal read outside captured range: ' + m[0])
            return 'uintBitsToFloat(0x%08xu)' % struct.unpack_from('<I', code, pos)[0]
        source = re.sub(re.escape(name) + r'\.data\[(\d+[uU]?)\]', replace_array, source)
        source = re.sub(r'\buniform\s+highp\s+sampler2D\s+' + re.escape(name) + r'\s*;', '', source)
        if re.search(r'\b' + re.escape(name) + r'\b', source):
            raise ValueError('unhandled/dynamic literal-bank access: ' + name)
    return source, names


def specialize_catalogue(text, workspace):
    marker = 'static const struct mm_shader_binding nvn_megaman_shader_bindings[]='
    sources, metadata = text.split(marker, 1)
    sections = re.compile(r'(/\* (\d+) ([\w-]+) (vertex|fragment) \*/)(.*?)(?=/\* \d+ |\Z)', re.S)
    meta = {m[3]: m[5] for m in sections.finditer(metadata)}
    changes = {}
    entries = re.compile(r'(^\t/\* (\d+) ([\w-]+) (vertex|fragment) \*/\n)(\t\{"",.*?\t "", [01]\},\n)', re.M | re.S)
    def source_entry(m):
        name = m[3]
        b = meta[name]
        uniform = re.search(r'\.uniform_count=(\d+),\.uniforms=\{(.*?)\},\n', b, re.S)
        candidates = set(re.findall(r'\.name="(c1(?:_\d+)?)"', uniform[2]))
        if not candidates:
            return m[0]
        glsl = ''.join(json.loads(s) for s in re.findall(r'"(?:[^"\\]|\\.)*"', m[5])[1:])
        code = (workspace / (name + '.code.bin')).read_bytes()
        control = (workspace / (name + '.control.bin')).read_bytes()
        for kind, data in [('code', code), ('control', control)]:
            h = re.search(r'\.' + kind + r'_hash=\{([^}]+)\}', b)[1]
            expected = bytes(int(v, 16) for v in h.split(','))
            if hashlib.sha256(data).digest() != expected:
                raise ValueError('catalogue/capture identity mismatch: ' + name + ' ' + kind)
        updated, removed = inline_literal_cbuf(glsl, candidates, code, control)
        if not removed:
            return m[0]
        changes[name] = sorted(removed)
        body = '\t{"", "\\n"\n' + ''.join('\t ' + json.dumps(line + '\n') + '\n' for line in updated.strip('\n').splitlines())
        return m[1] + body + '\t "", %d},\n' % (m[4] == 'fragment')
    sources = entries.sub(source_entry, sources)
    def metadata_entry(m):
        if m[3] not in changes:
            return m[0]
        def uniforms(u):
            entries = re.findall(r'\{\.name="([^"]+)"[^{}]*\}', u[4])
            if len(entries) != int(u[2]):
                raise ValueError('uniform count mismatch: ' + m[3])
            kept = [e[0] for e in re.finditer(r'\{\.name="([^"]+)"[^{}]*\}', u[4]) if e[1] not in changes[m[3]]]
            return u[1] + str(len(kept)) + u[3] + ','.join(kept) + u[5]
        body = re.sub(r'(\.uniform_count=)(\d+)(,\.uniforms=\{)(.*?)(\},\n)', uniforms, m[5], flags=re.S)
        return m[1] + body
    metadata = sections.sub(metadata_entry, metadata)
    return sources + marker + metadata, changes


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('workspace', type=Path)
    ap.add_argument('catalogue', type=Path)
    ap.add_argument('output', type=Path)
    a = ap.parse_args()
    text, changes = specialize_catalogue(a.catalogue.read_text(), a.workspace)
    a.output.write_text(text)
    print(json.dumps(changes, indent=2))


if __name__ == '__main__':
    main()
