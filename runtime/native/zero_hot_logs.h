/* Zero-only emission sampling. Exact byte signatures never merge on a hash
 * collision; first/new cases and powers of two remain visible. The owner is
 * TLS or already serialized. No guest/SDL/synchronization operation lives here. */
#ifndef ZERO_HOT_LOGS_H
#define ZERO_HOT_LOGS_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define ZERO_HOT_LOG_SLOTS 16U
#define ZERO_HOT_LOG_BYTES 512U
struct zero_hot_log_slot {uint64_t occurrence,suppressed;size_t size;unsigned used;unsigned char bytes[ZERO_HOT_LOG_BYTES];};
struct zero_hot_log_cache {struct zero_hot_log_slot slots[ZERO_HOT_LOG_SLOTS];uint64_t calls,emitted,suppressed,overflow;};
struct zero_hot_log_event {uint64_t occurrence,suppressed;unsigned emit,changed;};
static unsigned zero_hot_log_verbose,zero_hot_log_enabled;
static void zero_hot_log_increment(uint64_t *value) {if(*value!=UINT64_MAX)(*value)++;}
static struct zero_hot_log_event zero_hot_log_sample(struct zero_hot_log_cache *cache,const void *bytes,size_t size,unsigned force) {
    zero_hot_log_increment(&cache->calls);
    struct zero_hot_log_slot *slot=NULL;unsigned changed=0;
    if(size<=ZERO_HOT_LOG_BYTES)for(unsigned i=0;i<ZERO_HOT_LOG_SLOTS;i++) {
        struct zero_hot_log_slot *candidate=&cache->slots[i];
        if(candidate->used && (candidate->size!=size || memcmp(candidate->bytes,bytes,size)))continue;
        slot=candidate;
        if(!slot->used){slot->used=1;slot->size=size;memcpy(slot->bytes,bytes,size);changed=1;}
        break;
    }
    if(!slot) {
        zero_hot_log_increment(&cache->overflow);zero_hot_log_increment(&cache->emitted);
        return (struct zero_hot_log_event){cache->calls,cache->suppressed,1,1};
    }
    zero_hot_log_increment(&slot->occurrence);
    unsigned emit=force||zero_hot_log_verbose||changed||slot->occurrence<=4||
        !(slot->occurrence&(slot->occurrence-1))||slot->occurrence==UINT64_MAX;
    if(!emit)zero_hot_log_increment(&slot->suppressed);
    zero_hot_log_increment(emit?&cache->emitted:&cache->suppressed);
    return (struct zero_hot_log_event){slot->occurrence,slot->suppressed,emit,changed};
}
#endif
