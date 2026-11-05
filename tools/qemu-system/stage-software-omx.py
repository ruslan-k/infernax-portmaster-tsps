#!/usr/bin/env python3
"""Stage a VM-only copy that registers real software OMX, without NVIDIA OMX."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import sys

ORIGINAL_SHA256 = '32a88871a9436535b972f1dcf0db41fac0a4f6aca09723bbd1123451faec3f26'
OFFSET = 0x272c34
CONTEXT = (0x52800800, 0xf9003668, 0x9402fa67, 0xaa0003f4, 0x9403203d,
           0xaa1303e0, 0xaa1403e1, 0x9403203e, 0x52800100, 0x9402fa60,
           0xaa0003f4, 0x9403203e, 0xaa1303e0, 0xaa1403e1, 0xa9414ff4,
           0xa8c27bfd, 0x14032035)

def patched_text(original):
    if hashlib.sha256(original).hexdigest() != ORIGINAL_SHA256:
        raise ValueError('Wrong subsdk0.text.seg SHA-256; no patch applied')
    if struct.unpack_from('<17I', original, OFFSET - 8) != CONTEXT:
        raise ValueError('OMX constructor context mismatch; no patch applied')
    old, = struct.unpack_from('<I', original, OFFSET)
    assert old == 0x9402fa67 and OFFSET + ((old & 0x3ffffff) << 2) == 0x3315d0
    replacement = 0x14000006
    assert replacement & 0xfc000000 == 0x14000000
    assert OFFSET + ((replacement & 0x3ffffff) << 2) == 0x272c4c
    result = original[:OFFSET] + struct.pack('<I', replacement) + original[OFFSET+4:]
    assert len(result) == len(original)
    assert [i for i, (a, b) in enumerate(zip(original, result)) if a != b] == list(range(OFFSET, OFFSET + 4))
    return result

if __name__ == '__main__':
    source, destination = (Path(a).resolve() for a in sys.argv[1:])
    if source.name != 'baseline' or destination.name != 'candidate0170' or source.parent != destination.parent:
        raise ValueError('Only sibling baseline -> candidate0170 staging is allowed')
    original = (source / 'subsdk0.text.seg').read_bytes()
    candidate = patched_text(original)
    manifest = json.loads((source / 'manifest.json').read_text())
    names = [n for n in manifest if n.endswith('.seg') or n == 'infernax-romfs.section']
    for name in names:
        if hashlib.sha256((source / name).read_bytes()).hexdigest() != manifest[name]['sha256']:
            raise ValueError('Baseline changed: ' + name)
    destination.mkdir(mode=0o700, exist_ok=False)
    for name in names:
        if name != 'subsdk0.text.seg':
            shutil.copyfile(source / name, destination / name)
    (destination / 'subsdk0.text.seg').write_bytes(candidate)
    actual = (destination / 'subsdk0.text.seg').read_bytes()
    assert actual == candidate
    receipt = dict(scope='VM-only; no TSPS deployment', original_sha256=ORIGINAL_SHA256,
                   candidate_sha256=hashlib.sha256(actual).hexdigest(),
                   offset=hex(OFFSET), old='67fa0294', new='06000014', changed_bytes=4)
    (destination / 'patch-receipt.json').write_text(json.dumps(receipt, indent=2))
    print(json.dumps(receipt, indent=2))
