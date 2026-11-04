"""Matched captures are opt-in, bounded and do not replace shader matching."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

PREFIX = r'''
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
static unsigned nvn_megaman_shader_bindings[15],writes;
static unsigned char head[2176],code_bytes[5120],code_hash[32],control_hash[32];
static const void *nvn_gpu_pointer(uint64_t address,unsigned size,unsigned prot){(void)size;(void)prot;return (void*)(uintptr_t)address;}
static void nvn_private_dump(const char *kind,unsigned sequence,const void *data,size_t size){(void)kind;(void)sequence;(void)data;(void)size;writes++;}
#define PROT_READ 1
static void capture(unsigned i){
 uint64_t code=(uint64_t)(uintptr_t)code_bytes;
 unsigned code_size=sizeof(code_bytes),control_size=sizeof(head);
'''
SUFFIX = r'''
}
int main(int argc,char **argv){
 if(argc>1)setenv("NX_SHADER_DUMP_ALL",argv[1],1);else unsetenv("NX_SHADER_DUMP_ALL");
 for(unsigned n=0;n<2;n++)for(unsigned i=0;i<15;i++)capture(i);
 printf("WRITES=%u\n",writes);
 return 0;
}
'''

class MatchedCaptureContracts(unittest.TestCase):
    def test_exact_opt_in_and_one_pair_per_pinned_index(self):
        text=Path(os.environ['INFERNAX_NATIVE_HEADER']).with_name('nvn_shaders.h').read_text()
        start=text.index('            const char *capture_all=getenv("NX_SHADER_DUMP_ALL");')
        end=text.index('            const char *want=getenv("MOMO_CONTROL_DUMP");',start)
        with tempfile.TemporaryDirectory(prefix='infernax-matched-capture-',dir=os.environ['TMPDIR']) as d:
            src=Path(d)/'capture.c';exe=Path(d)/'capture'
            src.write_text(PREFIX+text[start:end]+SUFFIX)
            p=subprocess.run(['cc','-std=c11',str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(p.returncode,0,p.stderr)
            for flag in (None,'0','true','1'):
                p=subprocess.run([str(exe)]+([]if flag is None else[flag]),capture_output=True,text=True)
                self.assertEqual(p.returncode,0,p.stderr)
                self.assertEqual(p.stdout.splitlines()[-1],'WRITES=%d'%(30 if flag=='1'else 0))

if __name__=='__main__':unittest.main()
