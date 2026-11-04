"""Reflection, not a sampler name, determines the independent sampler operand."""
import copy
import pathlib
import sys
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import build_catalogue as catalogue


def reflected(offset=0x568):
    return [{'name':'fs_tex2_170','binding':3,'descriptor':{
        'raw_stage':5,'has_secondary':1,'cbuf_index':2,'cbuf_offset':0x170,
        'shift_left':0,'secondary_cbuf_index':2,'secondary_cbuf_offset':offset,
        'secondary_shift_left':0,'count':1,'size_shift':3}}]


class SeparateMetadataContracts(unittest.TestCase):
    def test_actual_secondary_operand_is_retained(self):
        source='layout(set = 0, binding = 3) uniform sampler2D fs_tex2_170;'
        row=catalogue.reflected_samplers(source,reflected())[0]
        self.assertEqual((row['binding'],row['offset'],row['separate_sampler'],row['sampler_offset']),(14,0x150,1,0x568))
        self.assertEqual(catalogue.reflected_samplers(source,reflected(0x570))[0]['sampler_offset'],0x570)

    def test_invalid_descriptor_subset_is_not_guessed(self):
        source='uniform sampler2D fs_tex2_170;'
        for key,value in [('has_secondary',0),('cbuf_index',3),('secondary_cbuf_index',3),('shift_left',1),('secondary_shift_left',1),('count',2),('size_shift',2),('raw_stage',1),('cbuf_offset',0x178),('secondary_cbuf_offset',0x569),('secondary_cbuf_offset',0x968)]:
            with self.subTest(key=key,value=value):
                rows=reflected();rows[0]['descriptor'][key]=value
                with self.assertRaises(ValueError):catalogue.reflected_samplers(source,rows)

    def test_missing_duplicate_extra_and_binding_mismatch_fail(self):
        source='layout(set = 0, binding = 3) uniform sampler2D fs_tex2_170;'
        for rows in ([],reflected()*2,reflected()+[dict(reflected()[0],name='fs_tex2_178')],[dict(reflected()[0],binding=2)]):
            with self.subTest(rows=rows):
                with self.assertRaises(ValueError):catalogue.reflected_samplers(source,rows)

if __name__=='__main__':unittest.main()
