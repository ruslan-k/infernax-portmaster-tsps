"""Run actual replay cases and separate-handle resolver in a small C recorder."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

PREFIX=r'''
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define INFERNAX_CBUF_SAMPLERS 1
static uint64_t nvn_caller;
static int nvn_flag_on(const char *s){(void)s;return 0;}
static struct {
 uint64_t texture_handles[2][128],texture_known[2][2];
 uint32_t separate_textures[2][128],separate_samplers[2][128];
 uint64_t separate_texture_known[2][2],separate_sampler_known[2][2];
} nvn_render;
struct nvn_uniform {unsigned stage,offset,sampler_offset;char name[64];};
static void stop(const char *s,uint64_t pc,uint64_t detail,int status){(void)pc;fprintf(stderr,"STOP %s 0x%"PRIx64"\n",s,detail);exit(status);}
static void infernax_sampler_record(const char *op,unsigned stage,unsigned unit,uint64_t before,uint64_t known){(void)op;(void)stage;(void)unit;(void)before;(void)known;}
enum {NI_nvnCommandBufferBindTexture=1,NI_nvnCommandBufferBindSeparateTexture,NI_nvnCommandBufferBindSeparateSampler};
'''
BIND=r'''
static int bind(unsigned op,unsigned stage,unsigned unit,uint64_t handle){
 struct {unsigned op;} value={op},*r=&value;
 uint64_t a[3]={stage,unit,handle};
 switch(op){
'''
MAIN=r'''
 }
}
int main(int argc,char **argv){
 const char *mode=argc>1?argv[1]:"normal";
 struct nvn_uniform u={1,0x150,0x568,"fs_tex2_170"};
 if(strcmp(mode,"missing-texture"))bind(NI_nvnCommandBufferBindSeparateTexture,1,1,!strcmp(mode,"zero")?0:0xa40);
 if(strcmp(mode,"missing-sampler"))bind(NI_nvnCommandBufferBindSeparateSampler,!strcmp(mode,"stage")?0:1,0,UINT64_C(0x160)<<32);
 if(!strcmp(mode,"layout"))u.sampler_offset=0x569;
 if(!strcmp(mode,"combined"))bind(NI_nvnCommandBufferBindTexture,1,1,UINT64_C(0x17a00000b00));
 printf("HANDLE=%"PRIx64"\n",infernax_separate_sampler_handle(&u));
 if(!strcmp(mode,"normal")){
  bind(NI_nvnCommandBufferBindSeparateSampler,1,0,UINT64_C(0x161)<<32);
  printf("HANDLE=%"PRIx64"\n",infernax_separate_sampler_handle(&u));
  bind(NI_nvnCommandBufferBindSeparateSampler,1,1,UINT64_C(0x162)<<32);u.sampler_offset=0x570;
  printf("HANDLE=%"PRIx64"\n",infernax_separate_sampler_handle(&u));
 }
 return 0;
}
'''

class NativeSeparateSamplerContracts(unittest.TestCase):
    def test_independent_operands_updates_and_fail_closed_guards(self):
        text=Path(os.environ['INFERNAX_NATIVE_HEADER']).read_text()
        start=text.index('static uint64_t infernax_separate_sampler_handle(')
        helper=text[start:text.index('\n}',start)+2]
        start=text.index('    case NI_nvnCommandBufferBindTexture:')
        end=text.index('    case NI_nvnCommandBufferSetRenderTargets:',start)
        with tempfile.TemporaryDirectory(prefix='infernax-separate-',dir=os.environ['TMPDIR']) as d:
            src=Path(d)/'separate.c';exe=Path(d)/'separate'
            src.write_text(PREFIX+helper+BIND+text[start:end]+MAIN)
            p=subprocess.run(['cc','-std=c11',str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(p.returncode,0,p.stderr)
            cases={'normal':(0,['16000000a40','16100000a40','16200000a40']),
                   'combined':(0,['16000000a40']),'zero':(0,['16000000000']),
                   'missing-texture':(20,[]),'missing-sampler':(20,[]),'stage':(20,[]),'layout':(24,[])}
            for mode,(status,handles) in cases.items():
                with self.subTest(mode=mode):
                    p=subprocess.run([str(exe),mode],capture_output=True,text=True)
                    self.assertEqual(p.returncode,status,p.stderr)
                    self.assertEqual([l.removeprefix('HANDLE=')for l in p.stdout.splitlines()if l.startswith('HANDLE=')],handles)

if __name__=='__main__':unittest.main()
