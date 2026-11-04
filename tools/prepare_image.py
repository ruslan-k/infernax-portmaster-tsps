#!/usr/bin/env python3
"""Prepare local owner NSO segments/layout; output contains game data, do not publish."""
import argparse, json, pathlib, struct
import lz4.block
from inspect_nso import inspect_nso

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('exefs',type=pathlib.Path)
    ap.add_argument('output',type=pathlib.Path)
    args=ap.parse_args()
    if args.output.exists(): ap.error('output must not exist; never overwrite an owner image')
    names=('rtld','main','subsdk0','sdk')
    modules=[inspect_nso(args.exefs/name) for name in names]
    # All module structures/hashes validate before the first output is created.
    (args.output/'segments').mkdir(parents=True)
    base=0x08000000
    lines=[]
    for name,module in zip(names,modules):
        raw=(args.exefs/name).read_bytes()
        flags=struct.unpack_from('<I',raw,12)[0]
        for index,segment in enumerate(module['segments']):
            offset=struct.unpack_from('<I',raw,16+index*16)[0]
            compressed=struct.unpack_from('<I',raw,96+index*4)[0]
            size=segment['size']
            data=raw[offset:offset+(compressed if flags&(1<<index) else size)]
            if flags&(1<<index): data=lz4.block.decompress(data,uncompressed_size=size)
            (args.output/'segments'/(name+'.'+segment['name']+'.seg')).write_bytes(data)
        s={x['name']:x for x in module['segments']}
        values=[base,s['text']['size'],s['rodata']['address'],s['rodata']['size'],s['data']['address'],s['data']['size'],module['bss_bytes']]
        lines.append(' '.join([name]+[format(x,'x') for x in values]))
        base+=((module['mapped_bytes']+4095)&~4095)+4096
    (args.output/'native-layout.txt').write_text('\n'.join(lines)+'\n')
    (args.output/'module-identities.json').write_text(json.dumps([{k:m[k] for k in ('module','sha256','build_id','segments','bss_bytes')} for m in modules],indent=2)+'\n')
    print('Prepared four verified modules; owner bytes remain local:',args.output)

if __name__=='__main__': main()
