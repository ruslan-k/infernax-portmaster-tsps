/* D20, local Mali-450 experiment. The callback applies its own mask once;
 * it performs no logging, allocation, guest access or kernel-mutex operation. */
#include <stdatomic.h>
static struct {
    unsigned enabled,configured;
    _Atomic unsigned callback_error;
    _Atomic uint64_t callback_tid;
    uint64_t reported_tid;
} aud_affinity;
static unsigned aud_affinity_apply(unsigned cpu) {
    cpu_set_t wanted,actual;CPU_ZERO(&wanted);CPU_SET(cpu,&wanted);
    int rc=pthread_setaffinity_np(pthread_self(),sizeof(wanted),&wanted);if(rc)return (unsigned)rc;
    rc=pthread_getaffinity_np(pthread_self(),sizeof(actual),&actual);if(rc)return (unsigned)rc;
    return CPU_EQUAL(&wanted,&actual)?0:(unsigned)EINVAL;
}
static void aud_affinity_worker(void) {
    const char *option=getenv("COTM_AUDIO_AFFINITY");
    if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("AUDREN_AFFINITY_OPTION",0,0,24);
    aud_affinity.enabled=option&&!strcmp(option,"1");aud_affinity.configured=1;
    if(!aud_affinity.enabled)return;
    unsigned error=aud_affinity_apply(2);if(error)stop("AUDREN_MIXER_AFFINITY",0,error,21);
    printf("AUDREN_AFFINITY role=mixer cpu=2 tid=%ld verified=1 scope=audio-only\n",(long)syscall(SYS_gettid));
}
static void aud_affinity_callback(void) {
    if(!aud_affinity.enabled)return;
    static _Thread_local unsigned configured;
    if(configured)return;
    configured=1;unsigned error=aud_affinity_apply(3);
    if(error)atomic_store_explicit(&aud_affinity.callback_error,error,memory_order_release);
    else atomic_store_explicit(&aud_affinity.callback_tid,(uint64_t)syscall(SYS_gettid),memory_order_release);
}
static void aud_affinity_check(void) {
    if(!aud_affinity.enabled)return;
    unsigned error=atomic_load_explicit(&aud_affinity.callback_error,memory_order_acquire);
    if(error)stop("AUDREN_CALLBACK_AFFINITY",0,error,21);
    uint64_t tid=atomic_load_explicit(&aud_affinity.callback_tid,memory_order_acquire);
    if(tid&&tid!=aud_affinity.reported_tid) {
        aud_affinity.reported_tid=tid;
        printf("AUDREN_AFFINITY role=callback cpu=3 tid=%"PRIu64" verified=1 scope=audio-only\n",tid);
    }
}
