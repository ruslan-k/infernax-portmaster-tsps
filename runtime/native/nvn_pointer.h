#include <execinfo.h>
static void aw_snapshot_dump_near(uint64_t address);
__attribute__((unused)) static _Thread_local char aw_trace_context_buffer[160];static _Thread_local const char *aw_trace_context;
/* A slot hint is not a cached authorization. All region metadata below is
 * reread under regions_mutex on every use. query/accessible and signal-path
 * callers remain unchanged; a hint miss uses their complete validation.
 */
static size_t nvn_pointer_hint=SIZE_MAX;
static unsigned nvn_pointer_valid_locked(uint64_t address,uint64_t size,unsigned perm,unsigned *hit) {
    *hit=0;
    if(normal_guard_overlap(address,size))return 0;
    if(size && address<AS_END && size<=AS_END-address && nvn_pointer_hint<nregions) {
        const struct region *r=&regions[nvn_pointer_hint];
        if(address>=r->base && address-r->base<r->size && size<=r->size-(address-r->base) && (r->perm&perm)==perm) {
            *hit=1;return 1;
        }
    }
    unsigned valid=accessible(address,size,perm);
    if(valid&&size)for(size_t i=0;i<nregions;i++) {
        const struct region *r=&regions[i];
        if(address>=r->base && address-r->base<r->size){nvn_pointer_hint=i;break;}
        if(address<r->base)break;
    }
    return valid;
}
/* Sparse, jittered sampling avoids a clock call on every pointer and a fixed
 * phase against draw or audio cadence. The interval is 64..191 calls, mean
 * 127.5; this is a statistical observation, not an exact accounting of waits.
 */
static _Thread_local struct {unsigned remaining,random;} nvn_pointer_sampler;
static struct {
    uint64_t calls,hits,samples,clock_failures,wait_ns,hold_ns,wait_max_ns,hold_max_ns;
    uint64_t wait_buckets[5];
} nvn_pointer_profile;
static unsigned nvn_pointer_after_frame;
static unsigned nvn_pointer_sample(void) {
    if(nvn_pointer_sampler.remaining){nvn_pointer_sampler.remaining--;return 0;}
    if(!nvn_pointer_sampler.random)nvn_pointer_sampler.random=0x9e3779b9U^(guest_thread_index*0x85ebca6bU);
    nvn_pointer_sampler.random=nvn_pointer_sampler.random*1664525U+1013904223U;
    nvn_pointer_sampler.remaining=63+((nvn_pointer_sampler.random>>16)&127U);
    return 1;
}
static unsigned nvn_pointer_clock(uint64_t *value) {
    struct timespec now;if(clock_gettime(CLOCK_MONOTONIC,&now))return 0;
    *value=(uint64_t)now.tv_sec*1000000000ULL+now.tv_nsec;return 1;
}
#include "nvn_pointer_fast.h"
static _Thread_local struct nvn_snapshot *nvn_redirect_current;
static void *nvn_redirect_lookup(uint64_t address,uint64_t size);
static void *nvn_pointer(uint64_t address,uint64_t size,unsigned perm) {
    if(nvn_redirect_current){void *p=nvn_redirect_lookup(address,size);
        if(!p){void *frames[24];int n=backtrace(frames,24);char **names=backtrace_symbols(frames,n);
            printf("NVN_SNAPSHOT_MISS_TRACE address=0x%"PRIx64" size=%"PRIu64" context=%s\n",address,size,aw_trace_context?aw_trace_context:"-");aw_snapshot_dump_near(address);
            for(int i=0;i<n;i++)printf("  host-frame %d %s\n",i,names&&names[i]?names[i]:"?");
            stop("NVN_SNAPSHOT_MISS",nvn_caller,address,24);}
        return p;}
    if(nvn_pointer_fast_try(address,size,perm))return (void *)(uintptr_t)address;
    unsigned sample=nvn_pointer_sample(),clock_valid=0,hit=0;
    uint64_t before=0,acquired=0,validated=0;
    if(sample)clock_valid=nvn_pointer_clock(&before);
    pthread_mutex_lock(&regions_mutex);
    if(sample)clock_valid=nvn_pointer_clock(&acquired)&&clock_valid;
    unsigned valid=nvn_pointer_valid_locked(address,size,perm,&hit);
    if(valid)nvn_pointer_fast_remember_locked(address,size);
    nvn_pointer_profile.calls++;nvn_pointer_profile.hits+=hit;
    if(sample) {
        clock_valid=nvn_pointer_clock(&validated)&&clock_valid&&acquired>=before&&validated>=acquired;
        if(clock_valid) {
            uint64_t wait=acquired-before,hold=validated-acquired;
            nvn_pointer_profile.samples++;nvn_pointer_profile.wait_ns+=wait;nvn_pointer_profile.hold_ns+=hold;
            if(wait>nvn_pointer_profile.wait_max_ns)nvn_pointer_profile.wait_max_ns=wait;
            if(hold>nvn_pointer_profile.hold_max_ns)nvn_pointer_profile.hold_max_ns=hold;
            unsigned bucket=wait<10000?0:wait<100000?1:wait<1000000?2:wait<5000000?3:4;
            nvn_pointer_profile.wait_buckets[bucket]++;
        } else nvn_pointer_profile.clock_failures++;
    }
    pthread_mutex_unlock(&regions_mutex);
    if(!valid) {
        /* short-cut: diagnostic-only backtrace, no address resolution elsewhere; remove with the investigation */
        if(getenv("INVEST_PTR_TRACE")){void *frames[32];int n=backtrace(frames,32);
            printf("NVN_GUEST_POINTER_TRACE address=0x%"PRIx64" size=%"PRIu64" perm=%u thread=%u caller=0x%"PRIx64"\n",address,size,perm,guest_thread_index,nvn_caller);
            backtrace_symbols_fd(frames,n,STDOUT_FILENO);}
        stop("NVN_GUEST_POINTER",nvn_caller,address,24);
    }
    return (void *)(uintptr_t)address;
}
/* Called after Submit, outside the producer/regions locks. A quick atomic
 * frame check avoids an extra regions lock except once per 60-frame report.
 * Copy/reset under the regions lock; never print while holding it.
 */
static void nvn_pointer_report(unsigned frame) {
    unsigned previous=__atomic_load_n(&nvn_pointer_after_frame,__ATOMIC_RELAXED);
    if(frame<previous || frame-previous<60)return;
    pthread_mutex_lock(&regions_mutex);
    previous=__atomic_load_n(&nvn_pointer_after_frame,__ATOMIC_RELAXED);
    if(frame<previous || frame-previous<60){pthread_mutex_unlock(&regions_mutex);return;}
    typeof(nvn_pointer_profile) p=nvn_pointer_profile;
    nvn_pointer_profile=(typeof(nvn_pointer_profile)){0};
    __atomic_store_n(&nvn_pointer_after_frame,frame,__ATOMIC_RELAXED);pthread_mutex_unlock(&regions_mutex);
    nvn_pointer_fast_report(frame,frame-previous);
    double estimate=p.samples?(double)p.wait_ns*p.calls/p.samples/(frame-previous)*1e-6:-1.0;
    printf("NVN_POINTER_PROFILE after-frame=%u frames=%u calls=%"PRIu64" hint-hits=%"PRIu64" fallbacks=%"PRIu64
           " samples=%"PRIu64" clock-failures=%"PRIu64" sample-wait-ms=%.3f sample-hold-ms=%.3f max-wait-ms=%.3f max-hold-ms=%.3f"
           " estimated-wait-ms-per-frame=%.3f wait-buckets=%"PRIu64"/%"PRIu64"/%"PRIu64"/%"PRIu64"/%"PRIu64
           " scope=locked-NVN-pointer-calls sampling=jitter64-191 hold=acquired-through-validation attribution=regions-mutex-estimate\n",
           frame,frame-previous,p.calls,p.hits,p.calls-p.hits,p.samples,p.clock_failures,p.wait_ns*1e-6,p.hold_ns*1e-6,
           p.wait_max_ns*1e-6,p.hold_max_ns*1e-6,estimate,p.wait_buckets[0],p.wait_buckets[1],p.wait_buckets[2],p.wait_buckets[3],p.wait_buckets[4]);
}
