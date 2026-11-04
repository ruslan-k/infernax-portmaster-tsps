#!/usr/bin/env python3
"""Generate the owner-only Lamentum base image contracts for the recovered Horizon runtime.

Output contains proprietary owner data (game code segments, RomFS section, derived
addresses). It is never published. Shader translation is NOT supplied here.

Derivation rules (fail-closed, no borrowed offsets):
  * four modules rtld/main/subsdk0/sdk must be valid NSO0 with verified segment hashes;
  * the NVN bootstrap lookup thunk is decoded from the title's own sdk module;
  * every derived value is checked against the instruction pattern it came from.
"""
import argparse, hashlib, json, os, pathlib, shutil, struct, subprocess, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from inspect_nso import inspect_nso

TITLE_ID = 0x010039A016792000
MODULE_ORDER = ('rtld', 'main', 'subsdk0', 'sdk')


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('exefs', type=pathlib.Path)
    p.add_argument('npdm_json', type=pathlib.Path)
    p.add_argument('romfs_json', type=pathlib.Path)
    p.add_argument('output', type=pathlib.Path)
    p.add_argument('--title-id', default=hex(TITLE_ID))
    a = p.parse_args()

    npdm = json.loads(a.npdm_json.read_text())
    rom = json.loads(a.romfs_json.read_text())
    title_id = int(a.title_id, 16)
    if int(npdm['title_id'], 16) != title_id or not npdm['is_64_bit']:
        raise ValueError('unsupported title/ABI')
    section = pathlib.Path(rom['section'])
    if section.stat().st_size != rom['section_size'] or digest(section) != rom['sha256']:
        raise ValueError('RomFS identity mismatch')
    if rom['romfs_offset'] + rom['romfs_size'] > rom['section_size']:
        raise ValueError('RomFS bounds')

    mods = {n: inspect_nso(a.exefs / n) for n in MODULE_ORDER}
    if a.output.exists():
        raise ValueError('output must not exist')
    # The SDK enables virtual address memory (VAMM) only when svcGetInfo(16) reports a
    # non-zero system resource size, so this value must come from the title's own NPDM
    # instead of a borrowed constant.
    npdm_raw = (a.exefs / 'main.npdm').read_bytes()
    if npdm_raw[:4] != b'META' or len(npdm_raw) < 0x20:
        raise ValueError('main.npdm is not an NPDM')
    system_resource_size = struct.unpack_from('<I', npdm_raw, 0x14)[0]
    if system_resource_size == 0:
        raise ValueError('NPDM declares no system resource size; VAMM cannot be enabled')

    subprocess.run([sys.executable, str(pathlib.Path(__file__).with_name('prepare_image.py')),
                    str(a.exefs), str(a.output)], check=True)
    for f in (a.output / 'segments').iterdir():
        shutil.move(str(f), str(a.output / f.name))
    (a.output / 'segments').rmdir()

    bases, base, entries = {}, 0x08000000, []
    for name in MODULE_ORDER:
        m = mods[name]
        bases[name] = base
        for s in m['segments']:
            size = s['size'] + (m['bss_bytes'] if s['name'] == 'data' else 0)
            aligned = (size + 4095) & ~4095
            perm = 5 if s['name'] == 'text' else 3 if s['name'] == 'data' else 1
            state = 4 if s['name'] == 'data' else 3
            entries.append('    {"%s.%s.seg",0x%xULL,%dULL,%dULL,%d,%d},' %
                           (name, s['name'], base + s['address'], s['size'], aligned, perm, state))
        base += ((m['mapped_bytes'] + 4095) & ~4095) + 4096

    sdk = mods['sdk']
    text = (a.output / 'sdk.text.seg').read_bytes()
    symbols = {s['name']: s for s in sdk['symbols'] if s['defined']}
    for required in ('nvnBootstrapLoader', 'nvnBootstrapLoaderInternal'):
        if required not in symbols:
            raise ValueError('missing sdk symbol ' + required)
    bootstrap = symbols['nvnBootstrapLoader']['value']
    internal = symbols['nvnBootstrapLoaderInternal']['value']
    # nvnBootstrapLoaderInternal forwards to the NVN lookup thunk with an unconditional
    # branch; its slot inside the stub differs per SDK, so locate the branch instead of
    # assuming an offset.
    branch, branch_at = None, None
    for slot_index in range(16):
        word = struct.unpack_from('<I', text, internal + 4 * slot_index)[0]
        if word & 0xfc000000 == 0x14000000:
            branch, branch_at = word, internal + 4 * slot_index
            break
    if branch is None:
        raise ValueError('no branch in nvnBootstrapLoaderInternal; do not assume the thunk layout')
    delta = branch & 0x3ffffff
    if delta & 0x2000000:
        delta -= 0x4000000
    lookup = branch_at + delta * 4
    adrp, ldr, indirect, jump = struct.unpack_from('<4I', text, lookup)
    if (adrp & 0x9f00001f) != 0x90000008:
        raise ValueError('lookup thunk does not load the object page into x8')
    for word, rt in ((ldr, 8), (indirect, 2)):
        if (word & 0xffc00000) != 0xf9400000 or (word & 0x1f) != rt or ((word >> 5) & 0x1f) != 8:
            raise ValueError('lookup thunk is not "ldr x%d,[x8]" chain' % rt)
    if jump != 0xd61f0040:
        raise ValueError('lookup thunk does not branch through x2')
    imm = ((adrp >> 29) & 3) | (((adrp >> 5) & 0x7ffff) << 2)
    if imm & 0x100000:
        imm -= 0x200000
    slot = (lookup & ~4095) + imm * 4096 + ((ldr >> 10) & 0xfff) * 8
    reloc = next((r for r in sdk['relocations'] if r['target'] == slot and r['type'] == 1027), None)
    if not reloc:
        raise ValueError('lookup object relocation not proven')
    pointer = bases['sdk'] + reloc['addend']

    g = ['/* Generated owner-specific graphics identity. No translated shaders. */',
         '#define LC2_GRAPHICS_INIT_PC 0ULL', '#define LC2_GRAPHICS_INIT_OPCODE 0U',
         '#define LC2_GRAPHICS_BOOTSTRAP_PC 0x%xULL' % (bases['sdk'] + bootstrap),
         '#define LC2_GRAPHICS_BOOTSTRAP_OPCODE 0x%xU' % struct.unpack_from('<I', text, bootstrap)[0],
         '#define LC2_GRAPHICS_LOOKUP_PC 0x%xULL' % (bases['sdk'] + lookup),
         '#define LC2_GRAPHICS_LOOKUP_OPCODE 0x%xU' % adrp,
         '#define LC2_NVN_LOOKUP_POINTER 0x%xULL' % pointer]
    (a.output / 'megaman_graphics_contract.h').write_text('\n'.join(g) + '\n')

    h = ['/* Generated local owner image; not redistributable game payload. */',
         '#define LC2_PROGRAM_ID 0x%xULL' % title_id,
         '#define LC2_SDK_VERSION_STRING "%s"' % npdm.get('sdk_version', 'unknown'),
         '#define LC2_MAIN_STACK_SIZE %dULL' % int(npdm['main_thread_stack_size'], 16),
         '#define LC2_MAIN_PRIORITY %d' % npdm['main_thread_priority'],
         '#define LC2_MAIN_CORE %d' % npdm['default_cpu_id'],
         '#define LC2_ALLOWED_CORE_MASK 7ULL',
         '#define LC2_ALLOWED_PRIORITY_MASK 0x%xULL' % sum(1 << i for i in range(28, 60)),
         '#define LC2_SYSTEM_RESOURCE_SIZE 0x%xULL' % system_resource_size,
         '/* This SDK generation uses the unprefixed sampler builder (measured from the title\'s',
         ' * own nvnSamplerBuilder* setters); see runtime/infernax-adapter.patch. */',
         '#define LC2_SAMPLER_UNPACKED 1',
         '/* This title binds a depth-stencil render target with every colour target. */',
         '#define LC2_RENDER_TARGET_DEPTH 1',
         '/* 36-byte NVNtextureView (measured from this SDK 10.9 generation). */',
         '#define LC2_TEXTURE_VIEW_36 1',
         '#define ROMFS_SECTION_NAME "infernax-romfs.section"',
         '#define ROMFS_SECTION_SIZE %dULL' % rom['section_size'],
         '#define ROMFS_OFFSET %dULL' % rom['romfs_offset'],
         '#define ROMFS_SIZE %dULL' % rom['romfs_size'],
         '#define FILESYSTEM_ROMFS_READ_LIMIT (256ULL<<20)',
         '#define LC2_ROMFS_SHA256 "%s"' % rom['sha256']]
    for name in MODULE_ORDER:
        m, prefix = mods[name], 'LC2_' + name.upper()
        h += ['#define %s_BASE 0x%xULL' % (prefix, bases[name]),
              '#define %s_IMAGE_SIZE %dULL' % (prefix, m['mapped_bytes']),
              '#define %s_SHA256 "%s"' % (prefix, m['sha256'])]
    h += ['static const char *const allowed_services[]={' +
          ','.join(json.dumps(n) for n in npdm['service_access']) + '};',
          'static const struct image_segment images[]={', *entries, '};']
    (a.output / 'image_table.h').write_text('\n'.join(h) + '\n')
    (a.output / 'megaman_lc2_contract.h').write_text(
        '/* No title-specific overrides: the Lamentum owner image_table.h carries the contract. */\n')
    try:
        os.link(section, a.output / 'infernax-romfs.section')
    except OSError:
        shutil.copy2(section, a.output / 'infernax-romfs.section')
    print('Owner contracts generated; shader translation absent.')
    print('image module bases:', {k: hex(v) for k, v in bases.items()})
    print('NVN lookup thunk:', hex(bases['sdk'] + lookup), 'object:', hex(pointer))


if __name__ == '__main__':
    main()
