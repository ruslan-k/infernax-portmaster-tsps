"""Regression contracts from original shader declarations, not colour probes."""
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import build_catalogue as catalogue


def block(name, descriptor):
    return ('layout(set = 0, binding = %d, scalar) uniform block_%s '
            '{ float data[16384]; } %s;\n' % (descriptor, name, name))


class CatalogueContracts(unittest.TestCase):
    def test_sparse_banks_keep_raw_maxwell_indices(self):
        rows = catalogue.cbuf_blocks_of(block('c4', 0) + block('c5_1', 1))
        self.assertEqual([b['binding'] for b in rows], [1, 2])

    def test_reserved_bank_does_not_shift_application_slot(self):
        rows = catalogue.cbuf_blocks_of(block('c2', 0) + block('c3_1', 1))
        self.assertEqual(rows[1]['binding'], 0)

    def test_raw_and_spirv_indices_are_retained(self):
        row = catalogue.cbuf_blocks_of(block('c5_1', 1))[0]
        self.assertEqual(row.get('raw_bank'), 5)
        self.assertEqual(row.get('spirv_binding'), 1)

    def test_unknown_bank_name_is_not_guessed(self):
        with self.assertRaisesRegex(ValueError, 'bank'):
            catalogue.cbuf_blocks_of(block('arbitrary_constants', 1))

    def test_reserved_sampler_uses_native_handle_table(self):
        rows = catalogue.samplers_of('uniform sampler2D fs_tex2_168;')
        self.assertEqual(rows[0]['binding'], 14)
        self.assertEqual(rows[0]['offset'], 0x148)

    def test_adjacent_reserved_samplers_have_adjacent_native_slots(self):
        text = '\n'.join('uniform sampler2D fs_tex2_%x;' % off for off in (0x168, 0x170, 0x178))
        rows = catalogue.samplers_of(text)
        self.assertEqual([(b['offset']+0x20-0x168)//8 for b in rows], [0, 1, 2])

    def test_vertex_and_fragment_cbuf_uniforms_do_not_alias(self):
        raw = block('c3', 0) + 'void main(){ float x = c3.data[0u]; }'
        vs, _ = catalogue.rewrite_essl100(raw, 'vertex')
        fs, _ = catalogue.rewrite_essl100(raw, 'fragment')
        self.assertTrue('uniform highp sampler2D vs_c3;' in vs, 'VS declaration mismatch')
        self.assertTrue('uniform highp sampler2D fs_c3;' in fs, 'FS declaration mismatch')
        self.assertTrue('texelFetch(vs_c3,' in vs, 'VS fetch mismatch')
        self.assertTrue('texelFetch(fs_c3,' in fs, 'FS fetch mismatch')

    def test_dense_bank_layout_is_preserved(self):
        rows = catalogue.cbuf_blocks_of(''.join(block('c%d' % b, b-3) for b in (3, 4, 5)))
        self.assertEqual([r['binding'] for r in rows], [0, 1, 2])


if __name__ == '__main__':
    unittest.main()
