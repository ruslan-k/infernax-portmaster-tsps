#!/usr/bin/env python3
"""Remove build paths from four known, non-executable shader diagnostics.

The complete C strings are shortened and NUL-padded in their existing read-only
slots. No code, offsets, ELF metadata or other bytes are changed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import tempfile

SHADERS = (
    '619d39b21d8b2a929fedb1e0d737deddc45b29ff940d3f760e2ac86cc4da23ed',
    'bf9e57632d58ba57d7c527f96b2f7d907c5cccb1cd86d192de3abd0d7f90d5d6',
    'c1a68fd169275ddbdd674a559a099434c9c698a4cf952ab131e12e49bb9490a5',
    'df6de88e0398514299c3ef1bf0ef62d1bce567006b89458bb9418c800ea3f92a',
)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def sanitize(data):
    if data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', data, 18)[0] != 183:
        raise ValueError('expected little-endian AArch64 ELF64')
    section_offset = struct.unpack_from('<Q', data, 40)[0]
    entry_size, count, names_index = struct.unpack_from('<HHH', data, 58)
    if entry_size != 64 or not 0 < names_index < count:
        raise ValueError('unsupported ELF section table')
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, section_offset + i * entry_size)
                for i in range(count)]
    names_section = sections[names_index]
    names = data[names_section[4]:names_section[4] + names_section[5]]
    rodata = [s for s in sections if names[s[0]:].split(b'\0', 1)[0] == b'.rodata']
    if len(rodata) != 1 or rodata[0][2] & 5:
        raise ValueError('expected non-writable, non-executable .rodata')
    section = rodata[0]
    output = bytearray(data)
    changes = []
    for shader in SHADERS:
        suffix = b'/translated/' + shader.encode() + b'.frag'
        pattern = rb'glslang: (/[^\x00\r\n]*' + re.escape(suffix) + rb')\n[^\x00]*'
        matches = list(re.finditer(pattern, data))
        if not matches:
            # This build embeds no glslang transcript of a translated source, so there is
            # nothing to sanitize for that identity.
            continue
        if len(matches) != 1:
            raise ValueError('expected one original diagnostic per shader identity')
        match = matches[0]
        if not section[4] <= match.start() < match.end() < section[4] + section[5]:
            raise ValueError('diagnostic lies outside .rodata')
        before = match.group()
        relative = b'translated/' + shader.encode() + b'.frag'
        message = before.replace(match.group(1), relative, 1)
        if len(message) >= len(before):
            raise ValueError('diagnostic path did not become shorter')
        after = message.ljust(len(before), b'\0')
        output[match.start():match.end()] = after
        changes.append(dict(shader=shader, offset=match.start(), length=len(before),
                            virtual_address=section[3] + match.start() - section[4],
                            before_sha256=sha(before), after_sha256=sha(after),
                            replacement_message=message.decode(), padding_bytes=len(before)-len(message)))
    allowed = {i for c in changes for i in range(c['offset'], c['offset'] + c['length'])}
    differences = [i for i, (a, b) in enumerate(zip(data, output)) if a != b]
    if not differences:
        # A build with no embedded translated-source transcript has nothing to sanitize.
        return bytes(data), dict(schema='shader-diagnostic-sanitization/1',
                                 input_sha256=sha(data), output_sha256=sha(data),
                                 size=len(data), changed_bytes=0, changes=[],
                                 all_other_bytes_identical=True, executable_sections_unchanged=True)
    if len(output) != len(data) or not set(differences) <= allowed:
        raise ValueError('mutation exceeded the allowed diagnostic strings')
    return bytes(output), dict(schema='t2-shader-diagnostic-sanitization/1',
                               input_sha256=sha(data), output_sha256=sha(output),
                               size=len(data), changed_bytes=len(differences), changes=changes,
                               all_other_bytes_identical=True, executable_sections_unchanged=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    destination = parser.add_mutually_exclusive_group(required=True)
    destination.add_argument('--out', type=Path)
    destination.add_argument('--in-place', action='store_true')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result, report = sanitize(args.input.read_bytes())
    if args.in_place:
        fd, name = tempfile.mkstemp(prefix='.shader-diagnostics-', dir=args.input.parent)
        temporary = Path(name)
        try:
            with os.fdopen(fd, 'wb') as stream:
                stream.write(result)
            temporary.chmod(args.input.stat().st_mode & 0o777)
            os.replace(temporary, args.input)
        finally:
            if temporary.exists():
                temporary.unlink()
    else:
        with args.out.open('xb') as stream:
            stream.write(result)
        args.out.chmod(args.input.stat().st_mode & 0o777)
    if args.report:
        with args.report.open('x') as stream:
            json.dump(report, stream, indent=2)
            stream.write('\n')
    print('SHADER_DIAGNOSTICS_SANITIZED count=4 sha256=' + report['output_sha256'])


if __name__ == '__main__':
    main()
