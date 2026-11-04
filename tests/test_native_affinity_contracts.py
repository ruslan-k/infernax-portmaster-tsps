"""Compile the real affinity helper; error receipts must not bypass fail-closed."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

PREFIX = r'''
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#define INFERNAX_CBUF_SAMPLERS 1
struct guest_thread {pthread_t native;uint64_t host_tid,affinity;unsigned handle;int core,state;};
static struct guest_thread threads[2];static unsigned guest_thread_index;
static unsigned calls;static uint64_t requested;static int error_code;
static int record_affinity(pthread_t t,size_t size,const cpu_set_t*set){
 calls++;requested=0;for(unsigned i=0;i<64;i++)if(CPU_ISSET(i,set))requested|=UINT64_C(1)<<i;return error_code;
}
#define pthread_setaffinity_np record_affinity
static int host_affinity_log(pthread_t t,const char*role){printf("HOST_CPU_AFFINITY role=%s\n",role);return 0;}
static void stop(const char*s,uint64_t pc,uint64_t detail,int code){fprintf(stderr,"STOP %s %llu\n",s,(unsigned long long)detail);exit(code);}
'''
SUFFIX = r'''
int main(int argc,char**argv){
 struct guest_thread*t=&threads[1];t->native=pthread_self();t->host_tid=(uint64_t)getpid();t->state=2;t->core=0;t->affinity=1;t->handle=0x454;
 error_code=!strcmp(argv[1],"fail")?EINVAL:0;
 thread_affinity(t,2,!strcmp(argv[1],"invalid")?8:7,0x1234);
 printf("REQUESTED=%"PRIx64" CALLS=%u CORE=%d GUEST_MASK=%"PRIx64"\n",requested,calls,t->core,t->affinity);return 0;
}
'''


class NativeAffinityContracts(unittest.TestCase):
    def test_valid_mask_error_receipt_and_invalid_guest_guard(self):
        text=Path(os.environ['INFERNAX_NATIVE_HEADER']).with_name('threading.h').read_text()
        start=text.index('static void thread_affinity(');end=text.index('static void *thread_run(',start)
        with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR'],prefix='infernax-affinity-') as d:
            src=Path(d)/'probe.c';exe=Path(d)/'probe';src.write_text(PREFIX+text[start:end]+SUFFIX)
            build=subprocess.run(['cc','-std=c11','-O0','-pthread',str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(build.returncode,0,build.stderr)
            valid=subprocess.run([str(exe),'valid'],capture_output=True,text=True)
            self.assertEqual((valid.returncode,valid.stdout.strip()),(0,'REQUESTED=7 CALLS=1 CORE=2 GUEST_MASK=7'))
            fail=subprocess.run([str(exe),'fail'],capture_output=True,text=True)
            self.assertEqual(fail.returncode,21)
            self.assertIn('STOP HOST_THREAD_AFFINITY_FAILED 22',fail.stderr)
            self.assertIn('INFERNAX_AFFINITY_FAILURE',fail.stdout)
            self.assertIn('requested-host-mask=0x7',fail.stdout)
            self.assertIn('INFERNAX_AFFINITY_ONLINE',fail.stdout)
            mapped=subprocess.run([str(exe),'valid'],capture_output=True,text=True,
                                  env={**os.environ,'T2_HOST_CPU_BASE':'4'})
            self.assertEqual(mapped.returncode,0)
            self.assertIn('REQUESTED=70 CALLS=1 CORE=2 GUEST_MASK=7',mapped.stdout)
            self.assertIn('T2_HOST_CPU_MAP guest=0..2 host=4..6',mapped.stdout)
            bad_base=subprocess.run([str(exe),'valid'],capture_output=True,text=True,
                                    env={**os.environ,'T2_HOST_CPU_BASE':'4090'})
            self.assertEqual(bad_base.returncode,24)
            self.assertIn('INVALID_HOST_CPU_BASE',bad_base.stderr)
            invalid=subprocess.run([str(exe),'invalid'],capture_output=True,text=True)
            self.assertEqual(invalid.returncode,24)
            self.assertIn('INVALID_THREAD_AFFINITY',invalid.stderr)
            self.assertNotIn('INFERNAX_AFFINITY_FAILURE',invalid.stdout)


if __name__=='__main__':
    unittest.main()
