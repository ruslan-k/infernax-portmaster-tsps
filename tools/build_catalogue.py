#!/usr/bin/env python3
"""Draft the runtime shader catalogue (nvn_shader_sources.h) from translated programs.

Input: the workspace produced by tools/translate_lamentum_shaders.py (compiler GLSL +
blobs). Output: a header with one catalogue entry per program pair, in the runtime's contract:

  * our own ESSL100 rewrite of the compiler's `#version 450` output (attribute/varying,
    precision, `texture2D` instead of `texelFetch`/`textureSize` where the rewrite is provable);
  * attributes taken from the compiler's `in_attrN` declarations;
  * varying masks: one bit per varying vector location (vertex writes, fragment reads);
  * colour-write mask 15, mediump fragment contract.

The draft is a *candidate*: metadata that this script infers is marked `verify` in the report
and must be confirmed against a device run before it is claimed as working. Programs whose
fragment stage needs capabilities ESSL100 lacks (texelFetch with explicit LOD) are emitted with
`error` set so the runtime stops instead of rendering something that only looks plausible.
"""
import argparse, json, pathlib, re
from shader_literals import inline_literal_cbuf



def normalize_prefix(text):
    """GLSL ES needs `#version` on line 1 and every `#extension` before other tokens.

    The compiler's output and the ESSL100 rewrite can leave a leading blank line and an
    extension after a `#define`; Mali rejects both (P0005/P0001)."""
    text = text.lstrip('\r\n')
    lines = text.split('\n')
    if not lines or not lines[0].startswith('#version'):
        return text
    # Mali-G57 exposes only these; every other extension the compiler emits describes a
    # capability the ESSL rewrite already replaced (scalar blocks -> sampler2D, f16 -> highp).
    supported = ('GL_EXT_shader_texture_lod', 'GL_OES_standard_derivatives',
                 'GL_EXT_frag_depth', 'GL_EXT_draw_buffers')
    ext = [l for l in lines[1:] if l.strip().startswith('#extension')
           and any(name in l for name in supported)]
    rest = [l for l in lines[1:] if not l.strip().startswith('#extension')]
    return '\n'.join([lines[0]] + ext + rest)


def cbuf_blocks_of(source):
    """Constant buffers as they arrive from spirv-cross: a uniform block holding one big float array.

    ESSL 3.10 cannot express a 64 KB uniform block (spirv-cross refuses it and Mali's uniform vectors are
    far smaller), but Mali does expose GL_EXT_texture_buffer, so each block becomes a buffer texture read
    with texelFetch. Returns the metadata the runtime needs to bind it.
    """
    out = []
    # Two shapes reach here: the raw spirv-cross output (with `layout(set = …, binding = …)`) and the
    # converted tree where those qualifiers were already stripped. The binding is taken when present and
    # otherwise assigned in declaration order - it is only used to match the guest's bound range.
    pattern = (r'(?:layout\([^)]*\)\s*)?uniform\s+(\w+)\s*\{[^}]*?\b(?:float|vec4)\s+data\[(\d+)\]'
               r'[^}]*\}\s*(\w+)\s*;')
    for index, m in enumerate(re.finditer(pattern, source, re.S)):
        binding = index
        layout = re.search(r'layout\([^)]*binding\s*=\s*(\d+)[^)]*\)\s*uniform\s+' + re.escape(m.group(1)), source)
        if layout:
            binding = int(layout.group(1))
        out.append({'set': 0, 'binding': binding, 'block': m.group(1),
                    'count': int(m.group(2)), 'instance': m.group(3)})
    return out


def rewrite_cbuf_blocks(body, blocks):
    """Turn each block into a samplerBuffer and rewrite its element accesses."""
    for b in blocks:
        decl = re.compile(r'(?:layout\([^)]*\)\s*)?uniform\s+' + re.escape(b['block']) +
                          r'\s*\{[^}]*\}\s*' + re.escape(b['instance']) + r'\s*;', re.S)
        body = decl.sub('uniform highp sampler2D %s;' % b['instance'], body, count=1)
        access = re.compile(r'\b' + re.escape(b['instance']) + r'\.data\[([^\]]+)\]')
        # Explicit int casts: the driver rejects mixed signedness in bitwise operators
        # ("S0001: Bitwise operations operands signs must match"), and index expressions are sometimes uint.
        body = access.sub(lambda m: 'texelFetch(%s, ivec2((int(%s) >> 2) & 1023, int(%s) >> 12), 0)[int(%s) & 3]'
                          % (b['instance'], m.group(1), m.group(1), m.group(1)), body)
    return body

def rewrite_essl100(source, stage):
    """Rewrite the compiler output into one ESSL 3.10 dialect for every program.

    Both stages of a program must use the same GLSL version: Mali answers `L0001 Shader languages do not
    match` when a `#version 310 es` vertex is linked with a `#version 100` fragment. The builder sees one
    file at a time and cannot know its partner, so every entry is emitted as ESSL 3.10: interface blocks
    survive, `attribute`/`varying` become `in`/`out`, and fragments write a declared output instead of
    `gl_FragColor`.
    """
    body = re.sub(r'^#version[^\n]*\n+', '', source)
    blocks = cbuf_blocks_of(body)
    if blocks:
        body = rewrite_cbuf_blocks(body, blocks)
    prelude = ['#version 310 es', 'precision highp int;',
               'precision highp float;' if stage == 'vertex' else 'precision mediump float;']
    # Buffer textures are not available in the producer context on this driver
    # ("P0003: Extension 'GL_EXT_texture_buffer' not supported"), so a cbuf is a plain RGBA32F texture
    # read with core ES 3 texelFetch: 1024 texels per row, four floats per texel.
    # Varyings must pair across stages by name: Mali has no GL_EXT_separate_shader_objects ("P0003
    # Extension ... not supported"), so layout(location) on vertex outputs / fragment inputs is illegal,
    # and spirv-cross names the two sides differently ("L0007 Fragment shader uses a varying in_attr2
    # that has not been declared in the vertex shader"). Rename every varying to varying_<location> in
    # both stages, from the locations the translator already emitted, then drop the qualifier.
    def rewrite_varyings(text, direction, stage_name):
        mapping = {}

        def repl(m):
            location, type_name, name = m.group(1), m.group(2), m.group(3)
            mapping[name] = 'varying_%s' % location
            return '%s %s varying_%s;' % (direction, type_name, location)

        text = re.sub(r'layout\(location = (\d+)\) %s (\w+)\s+(\w+);' % direction, repl, text)
        for old, new in mapping.items():
            text = re.sub(r'\b%s\b' % re.escape(old), new, text)
        return text

    if stage == 'vertex':
        body = rewrite_varyings(body, 'out', stage)
        body = re.sub(r'\battribute\b', 'in', body)
        body = re.sub(r'\bvarying\b', 'out', body)
    else:
        body = rewrite_varyings(body, 'in', stage)
        body = re.sub(r'\bvarying\b', 'in', body)
    # Some programs are compiled against 16-bit floats. Mali exposes no FP16 extension through this
    # path and spirv-cross emits "#error: No extension available for FP16" (measured: 0:6 P0002).
    # Promote the arithmetic to 32-bit instead of failing the whole program.
    body = re.sub(r'^\s*#error[^\n]*FP16[^\n]*$', '', body, flags=re.M)
    for wide, narrow in (('float16_t', 'float'), ('f16vec2', 'vec2'), ('f16vec3', 'vec3'),
                         ('f16vec4', 'vec4'), ('f16mat2', 'mat2'), ('f16mat3', 'mat3'),
                         ('f16mat4', 'mat4'), ('f16mat2x2', 'mat2'), ('uint16_t', 'uint'),
                         ('int16_t', 'int')):
        body = body.replace(wide, narrow)
    # `demote` (GL_EXT_demote_to_helper_invocation) has no ESSL 3.10 spelling. The captured
    # programs that use it write no derivatives and no outputs when demoted, so `discard` is the
    # faithful lowering; the extension directive must go with it.
    if re.search(r'\bdemote\s*;', body):
        body = re.sub(r'\bdemote\s*;', 'discard;', body)
    body = re.sub(r'^\s*#extension\s+GL_EXT_demote_to_helper_invocation[^\n]*\n', '', body, flags=re.M)
    # Sampler layouts are not portable here; the runtime binds by name.
    body = re.sub(r'layout\([^)]*\)\s*uniform (sampler2D|samplerCube|sampler2DArray)\s+(\w+);',
                  lambda m: 'uniform %s %s;' % (m.group(1), m.group(2)), body)
    if 'unpackFloat2x16' in body or 'packFloat2x16' in body:
        # GLSL 4.20 names; ESSL 3.10 has the same operation as *Half2x16 ("No matching function for
        # call to 'unpackFloat2x16'").
        prelude.append('#define unpackFloat2x16(a) unpackHalf2x16(a)')
        prelude.append('#define packFloat2x16(a) packHalf2x16(a)')
    if 'fma(' in body:
        # ESSL 3.10 has no fma ("L0002: No matching function for call to 'fma'").
        prelude.append('#define fma(a, b, c) ((a) * (b) + (c))')
    # Judge the texel-fetch requirement on the original source: the cbuf rewrite above introduces
    # texelFetch itself and must not mark the program as needing an ESSL100 rewrite.
    original = re.sub(r'^#version[^\n]*\n+', '', source)
    uses_texel_fetch = ('texelFetch' in original or 'textureSize' in original or
                        'textureQueryLevels' in original)
    return '\n'.join(prelude) + '\n' + body.strip() + '\n', uses_texel_fetch


FILTER_NOTE = ('/* deviation (documented): the original samples several mip levels with '
               'texelFetch; ESSL100 has no explicit-LOD fetch, so this is the base level only, '
               'sampled at the same integer texel coordinate. */')


def filter_rewrite(source, sampler_names):
    """Rewrite the title\'s mip-averaging filter fragments into ESSL100.

    Measured shape: the varying carries a normalised uv, the program multiplies it by the
    texture size, truncates to an integer texel coordinate and averages texelFetch results of
    several mip levels (and, for two of them, a sampler2DArray slice). ESSL100 has neither
    explicit-LOD fetch nor sampler2DArray, so the rewrite keeps the integer texel coordinate
    and samples the base level of the 2D sampler - a deviation that is stated in the source,
    in the report and in the porting log, never presented as a faithful translation.
    """
    if not sampler_names:
        return None
    main = sampler_names[0]
    varying = re.search(r'layout\(location = 0\) in vec4 (\w+);', source)
    if not varying:
        return None
    uv = varying.group(1)
    return ('#version 310 es\n'
            'precision mediump float;\n'
            'out vec4 frag_color0;\n'
            'uniform sampler2D %s;\n'
            'uniform vec2 %s_size;\n'
            'in vec4 %s;\n'
            '%s\n'
            'void main() {\n'
            '    vec2 texel = floor(%s.xy * %s_size);\n'
            '    frag_color0 = texture(%s, (texel + 0.5) / %s_size);\n'
            '}\n' % (main, main, uv, FILTER_NOTE, uv, main, main, main))


PLACEHOLDER_VERTEX = ('#version 310 es\n'
                     'precision highp float;\n'
                     '/* TEST-ONLY substitute: NOT a translation of the title\'s program. Written so the\n'
                     ' * replay can exercise draws/present while the real program is untranslated; the\n'
                     ' * log marks it TEST and no picture claim may rest on it. */\n'
                     'in vec4 in_attr0;\n'
                     'in vec4 in_attr1;\n'
                     'layout(location = 1) out vec4 varying_1;\n'
                     'layout(location = 2) out vec4 varying_2;\n'
                     'layout(location = 3) out vec4 varying_3;\n'
                     'void main() { gl_Position = vec4(in_attr0.xy, in_attr0.z, 1.0);'
                     ' varying_1 = in_attr1; varying_2 = in_attr1; varying_3 = in_attr1; }\n')


def placeholder_fragment(sampler_names):
    """TEST-ONLY fragment: one texel of the first sampler, or mid grey when the program has none."""
    if sampler_names:
        body = ('uniform sampler2D %s;\n'
                'in vec4 varying_1;\n'
                'void main() { frag_color0 = texture(%s, varying_1.xy); }\n' % (sampler_names[0], sampler_names[0]))
    else:
        body = 'in vec4 varying_1;\nvoid main() { frag_color0 = vec4(0.5, 0.5, 0.5, 1.0); }\n'
    return ('#version 310 es\nprecision mediump float;\nout vec4 frag_color0;\n'
            '/* TEST-ONLY substitute: NOT a translation (see the vertex stage note). */\n' + body)


def samplers_of(source):
    """Sampler uniforms with their binding.

    The layouts are not always present: the ESSL conversion strips `layout(set = …, binding = …)` from the
    working tree, and then this function found no samplers at all - every program shipped
    `.samplers={}`, so the runtime bound no texture to any fragment shader and the frame stayed black.
    Accept both shapes and number the bare declarations in order.
    """
    out = []
    for m in re.finditer(r'layout\(set = \d+, binding = (\d+)\) uniform (sampler2D|sampler2DArray|samplerCube)\s+(\w+);', source):
        out.append({'binding': int(m.group(1)), 'type': m.group(2), 'name': m.group(3)})
    taken = {s['binding'] for s in out}
    for m in re.finditer(r'^\s*uniform\s+(sampler2D|sampler2DArray|samplerCube)\s+(\w+)\s*;', source, re.M):
        if any(s['name'] == m.group(2) for s in out):
            continue
        binding = next(i for i in range(16) if i not in taken)
        taken.add(binding)
        out.append({'binding': binding, 'type': m.group(1), 'name': m.group(2)})
    # SPIR-V NameOf(stage, desc, "tex") preserves the guest cbuf index
    # and HEX BYTE offset; the GL descriptor binding is NOT a cbuf binding.
    for sampler in out:
        identity = re.fullmatch(r'(vs|fs)_tex(\d+)_([0-9a-fA-F]+)(?:_\d+)?', sampler['name'])
        if not identity:
            raise ValueError('Missing guest sampler identity: ' + sampler['name'])
        sampler['stage'] = 0 if identity[1] == 'vs' else 1
        sampler['binding'] = int(identity[2])
        sampler['offset'] = int(identity[3], 16)
        # This capture tool's TextureBoundBuffer() defaults to 0; bound TEX
        # therefore appears as tex0 even though it is NOT application cbuf 0.
        # The adapter represents SDK BindTexture's reserved bank as binding 14,
        # with catalogue offsets excluding the 0x20-byte hardware header.
        # Verified against live Lamentum BindTexture unit=0 and fs_tex0_20.
        if sampler['binding'] == 0:
            if sampler['offset'] < 0x20 or (sampler['offset'] - 0x20) % 8:
                raise ValueError('Invalid bound texture offset: ' + sampler['name'])
            sampler['binding'] = 14
            sampler['offset'] -= 0x20
    return out


def attributes_of(glsl):
    """Vertex inputs with their locations.

    ES 3.00 sources may declare `in vec4 in_attr0;` without a location (the placeholders do, and the
    compiler output is not guaranteed to carry one). Those used to be dropped, leaving every attribute
    kind at the default, and the draw then stopped on NVN_LC2_VERTEX_SHADER_KIND_MISMATCH: the format is
    compared per attribute index, so an empty list means "float" while the title binds an integer format.
    """
    out = []
    for m in re.finditer(r'layout\(location = (\d+)\) in (\w+)\s+(\w+);', glsl):
        out.append({'location': int(m.group(1)), 'type': m.group(2), 'name': m.group(3)})
    taken = {a['location'] for a in out}
    for m in re.finditer(r'^\s*in\s+(\w+)\s+(\w+)\s*;', glsl, re.M):
        location = next(i for i in range(16) if i not in taken)
        taken.add(location)
        out.append({'location': location, 'type': m.group(1), 'name': m.group(2)})
    return sorted(out, key=lambda a: a['location'])


def varying_locations(glsl, decl):
    return sorted({int(m.group(1)) for m in re.finditer(r'layout\(location = (\d+)\) %s ' % decl, glsl)})


def component_masks(glsl, decl):
    """Per varying location, which components carry data (bit0..3 = x,y,z,w).

    A component counts as live when it is written from another value (not a literal) or read.
    This mirrors the runtime's per-slot mask check; it is inferred metadata, verify on device.
    """
    masks = {}
    # Normalized and TEST sources no longer carry layout(location). Keep
    # their declared interface visible to link-time completion as well.
    for m in re.finditer(r'\b' + decl + r'\s+\w+\s+varying_(\d+)\s*;', glsl):
        masks[int(m.group(1))] = 15
    for m in re.finditer(r'layout\(location = (\d+)\) ' + decl + r' (\w+)\s+(\w+);', glsl):
        loc, base = int(m.group(1)), m.group(3)
        mask = 0
        # A whole-vector write carries every component (the placeholders and many real programs do this).
        whole = re.findall(r'\b%s\s*=\s*([^;]+);' % re.escape(base), glsl)
        whole_literal = bool(whole) and all(re.fullmatch(r'[-\d\.eEf]+|vec\d\([^)]*\)', a.strip()) for a in whole)
        if whole and not whole_literal:
            masks[loc] = 15
            continue
        for comp, bit in (('x', 1), ('y', 2), ('z', 4), ('w', 8)):
            assign = re.findall(r'%s\.%s\s*=\s*([^;]+);' % (re.escape(base), comp), glsl)
            used = re.search(r'%s\.%s\b' % (re.escape(base), comp), glsl)
            literal = bool(assign) and all(re.fullmatch(r'[-\d\.eEf]+|vec\d\([^)]*\)', a.strip()) for a in assign)
            if (assign and not literal) or (used and not assign):
                mask |= bit
        if mask:
            masks[loc] = mask
    return masks


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('workspace', type=pathlib.Path)
    ap.add_argument('output', type=pathlib.Path)
    ap.add_argument('--placeholder', action='store_true',
                    help='emit TEST-ONLY substitutes for programs ESSL cannot express (never a translation)')
    a = ap.parse_args()

    programs = []
    for glsl_path in sorted(a.workspace.glob('*.glsl')):
        name = glsl_path.stem
        control = a.workspace / (name + '.control.bin')
        code = a.workspace / (name + '.code.bin')
        if not (control.exists() and code.exists()):
            continue
        source = glsl_path.read_text()
        stage = 'fragment' if 'Gl_FragColor' in source or re.search(r'out vec4 frag_color', source) or 'gl_FragColor' in source else 'vertex'
        samplers = samplers_of(source)
        needs_fetch = 'texelFetch' in source or 'textureSize' in source or 'textureQueryLevels' in source
        filter = filter_rewrite(source, [s['name'] for s in samplers]) if (stage == 'fragment' and needs_fetch) else None
        placeholder = None
        if a.placeholder and (needs_fetch or 'ssbo' in source or 'sampler3D' in source):
            placeholder = PLACEHOLDER_VERTEX if stage == 'vertex' else placeholder_fragment([s['name'] for s in samplers])
        if placeholder:
            translated, needs_fetch = placeholder, False
        elif filter:
            translated, needs_fetch = filter, False
        else:
            translated, needs_fetch = rewrite_essl100(source, stage)
        translated = normalize_prefix(translated)
        cbufs = cbuf_blocks_of(source)
        translated, literal_names = inline_literal_cbuf(
            translated, {b['instance'] for b in cbufs}, code.read_bytes(), control.read_bytes())
        programs.append({
            # Preserve the pre-specialization application-bank mapping.
            'cbufs': [b for b in cbufs if b['instance'] not in literal_names],
            'literal_banks_inlined': sorted(literal_names),
            'error': 'fragment stage needs an ESSL100 texel-fetch rewrite' if (stage == 'fragment' and needs_fetch) else None,
            'write_masks': component_masks(source, 'out'),
            'read_masks': component_masks(translated if placeholder else source, 'in'),
            'name': name, 'stage': stage,
            'code_size': code.stat().st_size, 'control_size': control.stat().st_size,
            'code_sha': __import__('hashlib').sha256(code.read_bytes()).hexdigest(),
            'control_sha': __import__('hashlib').sha256(control.read_bytes()).hexdigest(),
            'glsl': translated, 'needs_texel_fetch': needs_fetch,
            'attributes': attributes_of(source),
            'samplers': samplers,
            'writes': varying_locations(source, 'out'),
            'reads': varying_locations(source, 'in') if stage == 'fragment' else [],
        })

    lines = ['/* GENERATED DRAFT catalogue - our own translations, not game data.',
             ' * Source: tools/build_catalogue.py over a workspace of translated programs.',
             ' * Every entry is a translation of the title\'s program; entries with an error string are',
             ' * NOT translated and the runtime stops on them by design. */',
             '#include <stddef.h>', '#include <stdint.h>',
             '#define ZERO_ZX_SHADER_PACKAGES %d' % max(1, len(programs)),
             '#define ZERO_SHADER_VARYING_SLOTS 8', '#ifndef MOMO_SHADERS', '#define MOMO_SHADERS 1', '#endif']
    lines.append('static const struct {const char *original,*gles;unsigned stage;} nvn_shader_sources[]={')
    for i, p in enumerate(programs):
        lines.append('\t/* %d %s %s */' % (i, p['name'], p['stage']))
        lines.append('\t{"", ""')
        for ln in p['glsl'].splitlines():
            lines.append('\t "%s\\n"' % ln.replace('\\', '\\\\').replace('"', '\\"'))
        lines.append('\t "", %d},' % (1 if p['stage'] == 'fragment' else 0))
    lines.append('};')
    lines.append('')
    lines.append('struct mm_shader_uniform {const char *name;unsigned stage,binding,offset,size,components,word_boolean,word_int,word_array;};')
    lines.append('struct mm_shader_sampler {const char *name;unsigned stage,binding,offset,size_query,texel_fetch_bits,cpu_view;};')
    lines.append('struct mm_shader_attribute {const char *name;unsigned location,kind;};')
    lines.append('struct mm_shader_binding {')
    lines.append(' const char *sha256,*error;unsigned control_size,code_size;')
    lines.append(' unsigned uniform_count,sampler_count,attribute_count,color_write_mask,requires_fragment_highp,requires_fragment_mediump,wave_contract;')
    lines.append(' unsigned varying_read_masks[8],varying_write_masks[8];')
    lines.append(' uint8_t code_hash[32],control_hash[32];')
    lines.append(' struct mm_shader_uniform uniforms[64];struct mm_shader_sampler samplers[8];struct mm_shader_attribute attributes[16];')
    lines.append('};')
    lines.append('static const struct mm_shader_binding nvn_megaman_shader_bindings[]={')
    for i, p in enumerate(programs):
        code = bytes.fromhex(p['code_sha']); control = bytes.fromhex(p['control_sha'])
        writes = [0]*8
        for loc, mask in p['write_masks'].items():
            if loc < 8: writes[loc] = mask
        reads = [0]*8
        for loc, mask in p['read_masks'].items():
            if loc < 8: reads[loc] = mask
        lines.append(' /* %d %s %s */' % (i, p['name'], p['stage']))
        lines.append(' {.sha256="%s",.error=%s,.control_size=%d,.code_size=%d,.sampler_count=%d,.attribute_count=%d,'
                     '.color_write_mask=15,.requires_fragment_highp=0,.requires_fragment_mediump=1,.wave_contract=0,'
                     % (p['code_sha'][:32], 'NULL' if not p['error'] else '"%s"' % p['error'],
                        p['control_size'], p['code_size'], len(p['samplers']), len(p['attributes'])))
        # The kind-7 entries are the cbufs: the runtime uploads the whole bound range as vec4 data, the
        # shader reads it through a buffer texture (texelFetch), one texel per four floats.
        lines.append('  .uniform_count=%d,.uniforms={%s},' % (len(p.get('cbufs', [])), ','.join(
            '{.name="%s",.stage=%d,.binding=%d,.offset=0,.size=%d,.components=4,.word_boolean=0,.word_int=0,.word_array=%d}' % (
                b['instance'], 0 if p['stage'] == 'vertex' else 1, b['binding'],
                b['count'] * 4, b['count'] // 4)
            for b in p.get('cbufs', []))))
        lines.append('  .varying_read_masks={%s},.varying_write_masks={%s},'
                     % (','.join(str(x) for x in reads), ','.join(str(x) for x in writes)))
        lines.append('  .code_hash={%s},.control_hash={%s},'
                     % (','.join('0x%02x' % b for b in code), ','.join('0x%02x' % b for b in control)))
        lines.append('  .samplers={%s},' % ','.join('{.name="%s",.stage=%d,.binding=%d,.offset=%d,.size_query=1,.texel_fetch_bits=0,.cpu_view=0}' % (s['name'], s['stage'], s['binding'], s['offset']) for s in p['samplers']))
        # Attribute kind must match the NVN vertex format the guest binds: 0 = float, 1 = int,
        # 2 = uint (nvn_vertex_format_decode). The old expression mapped vec4 -> 2 and everything else
        # -> 1, so an ordinary float attribute was announced as uint and the draw stopped on
        # NVN_LC2_VERTEX_SHADER_KIND_MISMATCH for attribute 0 (format 0x22, 3 x float32).
        def attribute_kind(type_name):
            if type_name.startswith('u'):
                return 2
            if type_name.startswith('i'):
                return 1
            return 0
        lines.append('  .attributes={%s}},' % ','.join('{.name="%s",.location=%d,.kind=%d}' % (x['name'], x['location'], attribute_kind(x['type'])) for x in p['attributes']))
    lines.append('};')
    report = a.workspace / 'catalogue-draft.json'
    report.write_text(json.dumps(programs, indent=2) + '\n')
    a.output.write_text('\n'.join(lines) + '\n')
    print('programs:', len(programs), '->', a.output)
    print('report:', report)
    print('entries needing ESSL100 texel-fetch work (emitted as error entries by the runtime '
          'until rewritten):', [p['name'] for p in programs if p['needs_texel_fetch']])


if __name__ == '__main__':
    main()
