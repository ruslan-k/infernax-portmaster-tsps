import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location('append_verified_shader', Path(__file__).resolve().parents[1] / 'tools/append_verified_shader.py')
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def header(code='0x01', control='0x02', error='NULL'):
    return ('#define ZERO_ZX_SHADER_PACKAGES 1\n#define INFERNAX_SEPARATE_SAMPLERS 1\n'
        'static const struct x nvn_shader_sources[]={\n/* 0 fixture vertex */\n'
        '{"", "void main(){ /* } */ }", 0},\n};\n'
        'static const struct y nvn_megaman_shader_bindings[]={\n/* 0 fixture vertex */\n'
        '{.error='+error+',.code_hash={'+code+'},.control_hash={'+control+'}},\n};\n')


class AppendVerifiedShaderTests(unittest.TestCase):
    def test_append_preserves_bodies_and_adds_strict_boundary(self):
        old, one = header(), header('0x03', '0x04')
        result, index = MODULE.append_one(old, one)
        self.assertEqual(index, 1)
        self.assertIn('#define ZERO_ZX_SHADER_PACKAGES 2', result)
        self.assertIn('#define INFERNAX_STRICT_SHADER_FIRST 1', result)
        for symbol in ['nvn_shader_sources', 'nvn_megaman_shader_bindings']:
            self.assertTrue(MODULE.array_body(result, symbol)[2].startswith(MODULE.array_body(old, symbol)[2]))

    def test_repeat_append_keeps_first_strict_boundary(self):
        first, _ = MODULE.append_one(header(), header('0x03', '0x04'))
        second, index = MODULE.append_one(first, header('0x05', '0x06'))
        self.assertEqual(index, 2)
        self.assertEqual(second.count('#define INFERNAX_STRICT_SHADER_FIRST 1'), 1)
        self.assertIn('#define ZERO_ZX_SHADER_PACKAGES 3', second)

    def test_exact_duplicate_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Already catalogued'):
            MODULE.append_one(header(), header())

    def test_same_code_different_control_is_distinct(self):
        result, index = MODULE.append_one(header(), header(control='0x09'))
        self.assertEqual(index, 1)
        self.assertIn('.control_hash={0x09}', result)

    def test_error_entry_rejected(self):
        with self.assertRaisesRegex(ValueError, 'untranslated'):
            MODULE.append_one(header(), header('0x03', error='"unsupported"'))

    def test_braces_inside_strings_and_comments_do_not_close_array(self):
        original = header()
        start, end, body = MODULE.array_body(original, 'nvn_shader_sources')
        self.assertEqual(original[start:end], body)
        self.assertIn('void main(){ /* } */ }', body)
        self.assertEqual(original[end:end+2], '};')


if __name__ == '__main__':
    unittest.main()
