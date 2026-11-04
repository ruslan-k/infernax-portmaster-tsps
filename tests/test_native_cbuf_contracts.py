"""Compile the actual kind-7 upload block with a stateful GL recorder."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

PREFIX = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/mman.h>
typedef unsigned GLuint,GLenum; typedef int GLint,GLsizei;
#define INFERNAX_CBUF_SAMPLERS 1
#define GL_TEXTURE0 1000
#define GL_TEXTURE_2D 1
#define GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS 2
#define GL_MAX_TEXTURE_IMAGE_UNITS 3
#define GL_TEXTURE_MIN_FILTER 4
#define GL_TEXTURE_MAG_FILTER 5
#define GL_TEXTURE_WRAP_S 6
#define GL_TEXTURE_WRAP_T 7
#define GL_NEAREST 8
#define GL_CLAMP_TO_EDGE 9
#define GL_RGBA32F 10
#define GL_RGBA 11
#define GL_FLOAT 12
static unsigned active,bindings[128],generated,copies,last_read,uploads;
static uint32_t live[16384],uploaded[16384];
static uint64_t nvn_caller;
struct nvn_range{uint64_t address,size;};
static struct{struct nvn_range uniform[2][16];}nvn_render;
struct unif{unsigned offset,size,binding,stage,kind;char name[64];};
static const void *nvn_cb_src[8];static unsigned nvn_cb_len[8];
static uint64_t nvn_cbf_addr[16];
static unsigned nvn_cbf_size[16],nvn_cbf_h[16];
static GLint nvn_cbf_unit[16];static GLuint nvn_cbf_tex[16];
static void stop(const char*s,uint64_t pc,uint64_t d,int c){fprintf(stderr,"STOP %s %llu\n",s,(unsigned long long)d);exit(c);}
static int nvn_flag_on(const char*s){return 0;}
static const void *nvn_gpu_pointer(uint64_t a,uint64_t n,unsigned prot){copies++;last_read=n;return (void*)(uintptr_t)a;}
static void glActiveTexture(GLenum t){active=t-GL_TEXTURE0;}
static void glBindTexture(GLenum t,GLuint n){bindings[active]=n;}
static void glGenTextures(GLsizei n,GLuint*p){*p=++generated+70;}
static void glTexParameteri(GLenum t,GLenum p,GLint v){}
static void glGetIntegerv(GLenum p,GLint*v){*v=128;}
static unsigned glGetError(void){return 0;}
static void glTexImage2D(GLenum t,int l,int f,int w,int h,int b,GLenum fmt,GLenum ty,const void*p){}
static void glTexSubImage2D(GLenum t,int l,int x,int y,int w,int h,GLenum fmt,GLenum ty,const void*p){memcpy(uploaded,p,(size_t)w*h*16);uploads++;}
static void glUniform1i(GLint l,GLint n){}
static void glGetUniformiv(GLuint p,GLint l,GLint*v){*v=31;}
static void glUniform4fv(GLint l,GLsizei c,const float*p){}
static void upload(void){
 struct unif value={.offset=0,.size=65536,.binding=0,.stage=0,.kind=7,.name="c3"};
 struct unif*u=&value;GLint location=0;unsigned samplers_total=1;GLuint program=1;
 for(unsigned once=0;once<1;once++){
'''
SUFFIX = r'''
 }
}
int main(void){
 bindings[0]=42;live[0]=1;live[1]=0x3f800000;
 live[2]=0x80000000;live[3]=0x00000001;live[4]=0x7fc00001;live[5]=0xff800000;
 nvn_render.uniform[0][0]=(struct nvn_range){(uint64_t)(uintptr_t)live,64};
 upload();unsigned first_bits=uploaded[0],ordinary_after_first=bindings[0];
 live[1]=0x40000000;upload();
 unsigned tail=0;for(unsigned i=16;i<16384;i++)tail|=uploaded[i];
 printf("{\"ordinary_after_first\":%u,\"cbuf_bound\":%u,\"first_bits\":%u,\"last_read\":%u,\"copies\":%u,\"uploads\":%u,\"updated_bits\":%u,\"zero_tail\":%u,\"special_bits\":[%u,%u,%u,%u]}\n",ordinary_after_first,bindings[31],first_bits,last_read,copies,uploads,uploaded[1],tail,uploaded[2],uploaded[3],uploaded[4],uploaded[5]);
 return 0;
}
'''


class NativeCbufContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        header = Path(os.environ['INFERNAX_NATIVE_HEADER'])
        cls.text = header.read_text()
        start = cls.text.index('        if(u->kind==7) {')
        # The source block ends before the ordinary scalar-uniform range path.
        end = cls.text.index('\n        {   /* MOMO_UNIFORM_DUMP', start)
        block = cls.text[start:end]
        scratch = Path(os.environ['TMPDIR'])
        with tempfile.TemporaryDirectory(prefix='infernax-cbuf-test-', dir=scratch) as d:
            source = Path(d)/'upload.c';exe=Path(d)/'upload'
            source.write_text(PREFIX+block+SUFFIX)
            c = subprocess.run(['cc','-std=c11','-O0',str(source),'-o',str(exe)],text=True,capture_output=True)
            if c.returncode:
                raise RuntimeError(c.stderr)
            r=subprocess.run([str(exe)],text=True,capture_output=True,check=True)
            cls.result=json.loads(r.stdout.splitlines()[-1])

    def test_first_bank_creation_does_not_clobber_ordinary_texture(self):
        self.assertEqual(self.result['ordinary_after_first'],42)
        self.assertNotEqual(self.result['cbuf_bound'],0)

    def test_integer_looking_words_are_not_numerically_rewritten_on_cpu(self):
        self.assertEqual(self.result['first_bits'],1)

    def test_special_float_words_preserve_cpu_payloads(self):
        self.assertEqual(self.result['special_bits'],[0x80000000,0x00000001,0x7fc00001,0xff800000])

    def test_read_is_bounded_and_live_for_each_draw(self):
        self.assertEqual(self.result['last_read'],64)
        self.assertEqual(self.result['copies'],2)
        self.assertEqual(self.result['uploads'],2)
        self.assertEqual(self.result['updated_bits'],0x40000000)
        self.assertEqual(self.result['zero_tail'],0)

    def test_no_unbounded_secondary_refresh_remains(self):
        self.assertNotRegex(self.text,r'if\(rf\)\s*nvn_cbuf_refresh\(\)')


if __name__=='__main__':
    unittest.main()
