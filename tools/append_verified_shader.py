#!/usr/bin/env python3
"""Append one translated shader; preserve every existing catalogue initializer.

The single-entry header must come from build_catalogue.py on verified owner blobs.
Owner blobs are never copied into this repository. This is not a translator.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


def array_body(text, symbol):
    match = re.search(r'\b' + re.escape(symbol) + r'\s*\[\s*\]\s*=\s*\{', text)
    if not match:
        raise ValueError('Missing array: ' + symbol)
    start = match.end()
    i, depth, quote, comment = start, 1, None, None
    while i < len(text):
        c = text[i]
        n = text[i:i+2]
        if comment == 'line':
            if c == '\n': comment = None
        elif comment == 'block':
            if n == '*/': comment = None; i += 1
        elif quote:
            if c == '\\': i += 1
            elif c == quote: quote = None
        elif n == '//': comment = 'line'; i += 1
        elif n == '/*': comment = 'block'; i += 1
        elif c in ('"', "'"): quote = c
        elif c == '{': depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return start, i, text[start:i]
        i += 1
    raise ValueError('Unterminated array: ' + symbol)


def append_one(original, single):
    count_match = re.search(r'#define ZERO_ZX_SHADER_PACKAGES (\d+)', original)
    if not count_match:
        raise ValueError('Missing package count')
    count = int(count_match[1])
    if not re.search(r'#define ZERO_ZX_SHADER_PACKAGES 1\b', single):
        raise ValueError('Exactly one generated shader is required')
    if re.search(r'\.error=(?!NULL)', single):
        raise ValueError('Refusing untranslated/error catalogue entry')
    identities = re.findall(r'\.code_hash=\{([^}]+)\},\.control_hash=\{([^}]+)\}', single)
    if len(identities) != 1:
        raise ValueError('Exactly one code/control identity is required')
    if identities[0] in re.findall(r'\.code_hash=\{([^}]+)\},\.control_hash=\{([^}]+)\}', original):
        raise ValueError('Already catalogued exact identity')
    # Pin both hashes for newly appended entries; preserve the legacy policy for
    # older entries so this arm cannot silently change their compatibility.
    strict = re.search(r'#define INFERNAX_STRICT_SHADER_FIRST (\d+)', original)
    replacements = [(count_match.start(1), count_match.end(1), str(count+1))]
    for symbol in ['nvn_shader_sources', 'nvn_megaman_shader_bindings']:
        _, end, old_body = array_body(original, symbol)
        _, _, new_body = array_body(single, symbol)
        if len(re.findall(r'/\*\s*\d+\s', old_body)) != count:
            raise ValueError('Unexpected existing entry count: ' + symbol)
        if len(re.findall(r'/\*\s*0\s', new_body)) != 1:
            raise ValueError('Unexpected generated entry: ' + symbol)
        new_body = re.sub(r'(/\*\s*)0(\s)', lambda m:m[1]+str(count)+m[2], new_body, count=1)
        replacements.append((end, end, new_body))
    result = original
    for start,end,value in sorted(replacements,reverse=True):
        result = result[:start] + value + result[end:]
    if strict is None:
        result = result.replace('#define INFERNAX_SEPARATE_SAMPLERS 1',
            '#define INFERNAX_SEPARATE_SAMPLERS 1\n#define INFERNAX_STRICT_SHADER_FIRST '+str(count), 1)
    for symbol in ['nvn_shader_sources', 'nvn_megaman_shader_bindings']:
        old = array_body(original, symbol)[2]
        new = array_body(result, symbol)[2]
        if not new.startswith(old):
            raise AssertionError('Existing initializer changed: ' + symbol)
    return result, count


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('existing', type=Path)
    ap.add_argument('single', type=Path)
    ap.add_argument('output', type=Path)
    args = ap.parse_args()
    if args.output.exists() or args.output.resolve() in (args.existing.resolve(), args.single.resolve()):
        raise SystemExit('Output must be a new, separate file')
    old, single = args.existing.read_text(), args.single.read_text()
    new, index = append_one(old, single)
    args.output.write_text(new)
    print(json.dumps({'index':index,'entries':index+1,'previous_sha256':hashlib.sha256(old.encode()).hexdigest(),
                      'new_sha256':hashlib.sha256(new.encode()).hexdigest(),
                      'legacy_initializers_byte_identical':True,'new_identity_policy':'code+control SHA256'},indent=2))


if __name__ == '__main__':
    main()
