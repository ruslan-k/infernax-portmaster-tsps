#!/usr/bin/env python3
"""Read-only NSO/UnityFS feasibility inspection. Does not execute guest code."""
import argparse, collections, hashlib, json, pathlib, struct
import lz4.block


def inspect_nso(path):
    raw = path.read_bytes()
    if len(raw) < 256 or raw[:4] != b'NSO0':
        raise ValueError('not NSO0')
    flags = struct.unpack_from('<I', raw, 12)[0]
    segments = []
    for index, name in enumerate(('text', 'rodata', 'data')):
        offset, address, size = struct.unpack_from('<III', raw, 16 + index * 16)
        packed = struct.unpack_from('<I', raw, 96 + index * 4)[0]
        length = packed if flags & (1 << index) else size
        if offset + length > len(raw) or size > 512 * 1024 * 1024:
            raise ValueError('segment out of bounds')
        data = raw[offset:offset+length]
        if flags & (1 << index):
            data = lz4.block.decompress(data, uncompressed_size=size)
        if len(data) != size:
            raise ValueError('segment size mismatch')
        digest = hashlib.sha256(data).digest()
        if flags & (1 << (index + 3)) and digest != raw[160+index*32:192+index*32]:
            raise ValueError('NSO segment hash mismatch')
        segments.append(dict(name=name, address=address, size=size, sha256=digest.hex(), data=data))
    def read(address, size):
        for s in segments:
            delta = address - s['address']
            if 0 <= delta and delta + size <= s['size']:
                return s['data'][delta:delta+size]
        raise ValueError('unmapped NSO read: ' + hex(address))
    mod = struct.unpack('<I', read(4,4))[0]
    if read(mod,4) != b'MOD0':
        raise ValueError('missing MOD0')
    dynamic = mod + struct.unpack('<i', read(mod+4,4))[0]
    tags = {}
    for index in range(4096):
        tag, value = struct.unpack('<qQ',read(dynamic+index*16,16))
        if tag == 0:
            break
        tags[tag] = value
    else:
        raise ValueError('unterminated dynamic table')
    symbols = []
    if all(x in tags for x in (4,5,6,10)):
        count = struct.unpack('<II',read(tags[4],8))[1]
        strings = read(tags[5],tags[10])
        if count > 500000:
            raise ValueError('symbol count too large')
        for index in range(count):
            name,info,other,section,value,size = struct.unpack('<IBBHQQ',read(tags[6]+index*tags.get(11,24),24))
            end = strings.find(b'\0',name)
            if name >= len(strings) or end < 0:
                raise ValueError('invalid symbol string')
            text = strings[name:end].decode('utf8','replace')
            if text:
                symbols.append(dict(name=text, defined=section != 0, value=value, size=size, binding=info >> 4))
    relocations = []
    for address_tag, size_tag in ((7,8),(23,2)):
        if address_tag not in tags or size_tag not in tags:
            continue
        if tags[size_tag] % 24:
            raise ValueError('invalid RELA size')
        for offset in range(0,tags[size_tag],24):
            target, info, addend = struct.unpack('<QQq',read(tags[address_tag]+offset,24))
            read(target,8)
            relocations.append(dict(target=target, type=info & 0xffffffff, symbol_index=info >> 32, addend=addend))
    # Preserve exact dynsym indices; unnamed symbol zero still occupies a slot.
    symbol_slots = []
    if all(x in tags for x in (4,5,6,10)):
        count = struct.unpack('<II',read(tags[4],8))[1]
        for index in range(count):
            name,info,other,section,value,size = struct.unpack('<IBBHQQ',read(tags[6]+index*tags.get(11,24),24))
            end = strings.find(b'\0',name)
            symbol_slots.append(dict(name=strings[name:end].decode('utf8','replace'), defined=section != 0, value=value, binding=info >> 4, section=section))
    instructions = segments[0]['data']
    svc = collections.Counter()
    tls = []
    for offset in range(0,len(instructions)-3,4):
        word = struct.unpack_from('<I',instructions,offset)[0]
        if word & 0xffe0001f == 0xd4000001:
            svc[hex((word >> 5) & 65535)] += 1
        # MRS/MSR TPIDR_EL0 and TPIDRRO_EL0, independent of Rt.
        if word & 0xffffffe0 in (0xd53bd040,0xd51bd040,0xd53bd060):
            tls.append(hex(segments[0]['address']+offset))
    bss = struct.unpack_from('<I',raw,60)[0]
    return dict(module=path.name, build_id=raw[64:96].hex(), sha256=hashlib.sha256(raw).hexdigest(), segments=[{k:v for k,v in s.items() if k!='data'} for s in segments], bss_bytes=bss, mapped_bytes=max(s['address']+s['size'] for s in segments)+bss, dynamic_tags={hex(k):v for k,v in tags.items()}, symbols=symbols, symbol_slots=symbol_slots, relocations=relocations, svc_sites=dict(svc), tls_sites=tls)


def inspect_bundle(path):
    with path.open('rb') as stream:
        def cstr():
            value = bytearray()
            while len(value) < 4096:
                c = stream.read(1)
                if not c: raise ValueError('truncated string')
                if c == b'\0': return value.decode('ascii')
                value.extend(c)
            raise ValueError('oversized string')
        if cstr() != 'UnityFS': raise ValueError('not UnityFS')
        version = struct.unpack('>I',stream.read(4))[0]
        player, revision = cstr(),cstr()
        size, packed, unpacked, flags = struct.unpack('>QIII',stream.read(20))
        if size != path.stat().st_size: raise ValueError('bundle size mismatch')
        if version >= 7: stream.seek((stream.tell()+15)&~15)
        if flags & 128: stream.seek(size-packed)
        metadata = stream.read(packed)
        codec = flags & 63
        if codec in (2,3): metadata = lz4.block.decompress(metadata,uncompressed_size=unpacked)
        elif codec != 0: raise ValueError('unsupported metadata codec')
        if len(metadata) != unpacked: raise ValueError('bundle metadata mismatch')
        count = struct.unpack_from('>I',metadata,16)[0]
        offset = 20
        blocks = []
        for _ in range(count):
            a,b,c = struct.unpack_from('>IIH',metadata,offset); offset += 10
            blocks.append((a,b,c))
        entries = struct.unpack_from('>I',metadata,offset)[0]; offset += 4
        files = []
        for _ in range(entries):
            start,length,status = struct.unpack_from('>QQI',metadata,offset); offset += 20
            end = metadata.index(0,offset)
            name = metadata[offset:end].decode('utf8'); offset=end+1
            files.append(dict(name=name,offset=start,size=length,flags=status))
        return dict(path=str(path), unity_revision=revision, format_version=version, stored_bytes=size, block_count=count, unpacked_block_bytes=sum(x[0] for x in blocks), packed_block_bytes=sum(x[1] for x in blocks), files=files, note='Uncompressed bundle size is not resident memory or peak RSS.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root',type=pathlib.Path)
    parser.add_argument('--output',type=pathlib.Path,required=True)
    args = parser.parse_args()
    modules = [inspect_nso(p) for p in sorted((args.root/'exefs').iterdir()) if p.read_bytes()[:4]==b'NSO0']
    definitions = {s['name'] for m in modules for s in m['symbols'] if s['defined']}
    undefined = {s['name'] for m in modules for s in m['symbols'] if not s['defined'] and s['binding'] != 2}
    report = dict(modules=modules, unresolved_strong_imports=sorted(undefined-definitions), bundles=[inspect_bundle(p) for p in (args.root/'romfs').rglob('*') if p.is_file() and p.open('rb').read(8)==b'UnityFS\0'])
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    for m in modules:
        print(m['module'], 'mapped_bytes='+str(m['mapped_bytes']), 'symbols='+str(len(m['symbols'])), 'svc_sites='+str(sum(m['svc_sites'].values())), 'tls_sites='+str(len(m['tls_sites'])))
    print('Unresolved strong imports:',len(report['unresolved_strong_imports']))
    for b in report['bundles']:
        print(pathlib.Path(b['path']).name, b['unity_revision'], 'stored='+str(b['stored_bytes']), 'unpacked='+str(b['unpacked_block_bytes']))

if __name__ == '__main__':
    main()
