"""Exercise the real reserved-sampler guard, including its failure evidence."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

PREFIX = r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#define INFERNAX_CBUF_SAMPLERS 1
static uint64_t nvn_caller;
static unsigned infernax_sampler_seq,infernax_sampler_writer[2][128];
static int nvn_flag_on(const char *name){(void)name;return 1;}
static struct {uint64_t texture_known[2][2],texture_handles[2][128];} nvn_render;
static void stop(const char *why,uint64_t pc,uint64_t detail,int status){
 (void)pc;fprintf(stderr,"STOP %s 0x%"PRIx64"\n",why,detail);exit(status);
}
'''
MAIN = r'''
int main(int argc,char **argv){
 (void)argv;
 struct {unsigned stage,binding,offset;int location,size_location;char name[64];} value={1,14,0x150,4,-1,"fs_tex2_170"},*u=&value;
 struct {unsigned sources[2];} source={{14,1}},*p=&source;
 unsigned program=7,specialized=0;int location=4;
 nvn_render.texture_known[1][0]=argc>1?3:1;
 nvn_render.texture_handles[1][0]=0xabc;
 nvn_render.texture_handles[1][1]=0xdef;
'''
END = r'''
 printf("SNAPSHOT=%"PRIu64"\n",nvn_render.texture_handles[u->stage][slot]);
 return 0;
}
'''


class NativeSamplerContracts(unittest.TestCase):
    def test_guard_preserves_known_handle_and_explains_missing_slot(self):
        text=Path(os.environ['INFERNAX_NATIVE_HEADER']).read_text()
        start=text.index('static unsigned momo_texture_unit(')
        helper=text[start:text.index('\n}',start)+2]
        start=text.index('#ifdef MOMO_SHADERS',text.index('if(u->kind==3 && u->binding==14)'))
        guard=text[start+len('#ifdef MOMO_SHADERS'):text.index('#else',start)]
        with tempfile.TemporaryDirectory(prefix='infernax-sampler-',dir=os.environ['TMPDIR']) as d:
            src=Path(d)/'guard.c';exe=Path(d)/'guard'
            src.write_text(PREFIX+helper+MAIN+guard+END)
            c=subprocess.run(['cc','-std=c11',str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(c.returncode,0,c.stderr)
            known=subprocess.run([str(exe),'known'],capture_output=True,text=True)
            self.assertEqual((known.returncode,known.stdout),(0,'SNAPSHOT=3567\n'))
            missing=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(missing.returncode,20)
            self.assertIn('NVN_LC2_TEXTURE_BINDING_UNDEFINED 0x150',missing.stderr)
            for field in ('INFERNAX_SAMPLER_MISSING','name=fs_tex2_170','pair=14/1','slot=1','known0=0x1'):
                self.assertIn(field,missing.stdout)


if __name__=='__main__':unittest.main()
