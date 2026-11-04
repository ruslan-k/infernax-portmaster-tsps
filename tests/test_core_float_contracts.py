"""Exercise the real CPU texture decoder and ES2/ES3 policy."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

HARNESS = r'''
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "HEADER"
int main(void){
 const uint32_t sw[4]={2,3,4,5};
 const uint16_t words[4]={0x8000,0x0001,0x7e01,0x3c00};
 uint16_t oes[4]={0},core[4]={0},r[4]={0};
 unsigned a=nvn_texture_decode_upload(0x29,sw,0x8d61,(uint8_t*)oes,(const uint8_t*)words,1);
 unsigned b=nvn_texture_decode_upload(0x29,sw,0x140b,(uint8_t*)core,(const uint8_t*)words,1);
 unsigned c=nvn_texture_decode_upload(5,sw,0x140b,(uint8_t*)r,(const uint8_t*)words,1);
 unsigned es3=0,es2=0,es2ext=0,token=0,internal=0;
#ifdef NVN_TEXTURE_CORE_FLOAT_POLICY
 es3=nvn_texture_float_backend_available(5,3,"");
 es2=nvn_texture_float_backend_available(5,2,"");
 es2ext=nvn_texture_float_backend_available(5,2,"GL_OES_texture_half_float");
 token=nvn_texture_upload_gl_type(0x8d61,3);
 internal=nvn_texture_upload_internal(token,3,0x1908);
#endif
 printf("{\"oes_ok\":%u,\"core_ok\":%u,\"core_bytes\":%u,\"exact_bits\":%u,\"r_ok\":%u,\"r_defaults\":%u,\"es3_available\":%u,\"es2_without_ext\":%u,\"es2_with_ext\":%u,\"type\":%u,\"internal\":%u}\n",a,b,nvn_texture_upload_bytes(0x140b),!memcmp(words,core,8)&&!memcmp(core,oes,8),c,r[0]==0x8000&&r[1]==0&&r[2]==0&&r[3]==0x3c00,es3,es2,es2ext,token,internal);
 return 0;
}
'''


class CoreFloatContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        header=Path(os.environ['INFERNAX_NATIVE_HEADER']).with_name('nvn_texture_formats.h')
        with tempfile.TemporaryDirectory(prefix='infernax-half-test-',dir=os.environ['TMPDIR']) as d:
            src=Path(d)/'half.c';exe=Path(d)/'half'
            src.write_text(HARNESS.replace('HEADER',str(header)))
            c=subprocess.run(['cc','-std=c11','-O0',str(src),'-o',str(exe)],capture_output=True,text=True)
            if c.returncode:raise RuntimeError(c.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True,check=True)
            cls.result=json.loads(r.stdout)

    def test_es3_core_half_token_decodes_exact_binary16(self):
        self.assertEqual(self.result['oes_ok'],1)
        self.assertEqual(self.result['core_ok'],1)
        self.assertEqual(self.result['core_bytes'],8)
        self.assertEqual(self.result['exact_bits'],1)

    def test_r16f_expands_only_missing_components(self):
        self.assertEqual(self.result['r_ok'],1)
        self.assertEqual(self.result['r_defaults'],1)

    def test_core_capability_does_not_require_es2_extension_name(self):
        self.assertEqual(self.result['es3_available'],1)
        self.assertEqual(self.result['es2_without_ext'],0)
        self.assertEqual(self.result['es2_with_ext'],1)
        self.assertEqual(self.result['type'],0x140b)
        self.assertEqual(self.result['internal'],0x881a)


if __name__=='__main__':unittest.main()
