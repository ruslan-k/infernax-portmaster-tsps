/* LC2-only opt-in command trace sampling. No command, fence or video state is
 * read or changed here. Each caller supplies its own TLS channel/cache. */
#ifndef NVN_COMMAND_LOGS_H
#define NVN_COMMAND_LOGS_H
#include "frame_logs.h"
/* MM7/MM8 transitions exceed 64 exact keys (174 observed in run4101).
 * Keep bounded, collision-safe storage and the full-trace overflow policy. */
#define NVN_COMMAND_LOG_SLOTS 512U
struct nvn_command_log_cache {
    struct frame_log_counter slots[NVN_COMMAND_LOG_SLOTS];
    uint64_t calls,emitted,suppressed,overflow;
};
static pthread_once_t nvn_command_log_once=PTHREAD_ONCE_INIT;
static unsigned nvn_command_log_enabled;
static uint64_t nvn_command_log_next_lifetime;
static void nvn_command_log_configure(void) {
    const char *value=getenv("MEGAMAN_SAMPLE_COMMAND_LOGS");
    if(value&&strcmp(value,"0")&&strcmp(value,"1"))stop("NVN_LC2_COMMAND_LOG_OPTION",0,0,24);
    nvn_command_log_enabled=value&&!strcmp(value,"1");
}
static unsigned nvn_command_logs_sampled(void) {
    pthread_once(&nvn_command_log_once,nvn_command_log_configure);return nvn_command_log_enabled;
}
static uint64_t nvn_command_log_lifetime(void) {
    if(!nvn_command_logs_sampled())return 0;
    /* Logging is also usable without the async API mutex. Only this rare
     * diagnostic-ID allocation needs atomics; log counters remain TLS. */
    uint64_t previous=__atomic_load_n(&nvn_command_log_next_lifetime,__ATOMIC_RELAXED);
    while(previous!=UINT64_MAX) {
        uint64_t next=previous+1;
        if(__atomic_compare_exchange_n(&nvn_command_log_next_lifetime,&previous,next,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED))return next;
    }
    return UINT64_MAX; /* Saturation forces full logs. */
}
static void nvn_command_log_increment(uint64_t *value) {if(*value!=UINT64_MAX)(*value)++;}
static struct frame_log_event nvn_command_log_sample(struct nvn_command_log_cache *cache,
        struct frame_log_counter *legacy,uint64_t owner,uint64_t count,uint64_t lifetime,unsigned force) {
    if(!nvn_command_logs_sampled())return frame_log_sample(legacy,owner,count,0,force);
    nvn_command_log_increment(&cache->calls);
    /* Lifetime and count are exact keys; a collision never merges identities.
     * There is no eviction: a full table falls back to a complete trace. */
    uint64_t hash=(owner>>4)^(count*UINT64_C(0x9e3779b97f4a7c15))^(lifetime*UINT64_C(0xbf58476d1ce4e5b9));
    struct frame_log_counter *slot=NULL;
    for(unsigned i=0;i<NVN_COMMAND_LOG_SLOTS;i++) {
        struct frame_log_counter *p=&cache->slots[(hash+i)&(NVN_COMMAND_LOG_SLOTS-1)];
        if(!p->valid||(p->keys[0]==owner&&p->keys[1]==count&&p->keys[2]==lifetime)){slot=p;break;}
    }
    struct frame_log_event event;
    if(slot)event=frame_log_sample(slot,owner,count,lifetime,force||frame_log_full_commands()||lifetime==UINT64_MAX);
    else {nvn_command_log_increment(&cache->overflow);event=frame_log_sample(legacy,owner,count,lifetime,1);}
    nvn_command_log_increment(event.emit?&cache->emitted:&cache->suppressed);
    return event;
}
#endif
