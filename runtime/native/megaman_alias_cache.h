#ifndef MEGAMAN_ALIAS_CACHE_H
#define MEGAMAN_ALIAS_CACHE_H
#include <inttypes.h>
#include <stdio.h>
/* Included after the complete admission alias check. Cadence and CPU-image
 * sampling share this cache; live/layout/pool/pending checks remain outside it. All
 * readers and texture membership transitions hold nvn_gl_completion.mutex. */
struct nvn_mfc_alias_hint {
    uint64_t id,epoch,offset,size;
    const struct nvn_pool *pool;
};
static struct {
    uint64_t epoch,hits,misses,completed_scan_slots;
    unsigned reported;
    struct nvn_mfc_alias_hint hints[4096];
} nvn_mfc_alias_cache={.epoch=1};

/* Call immediately before publishing/removing live membership, under the GL
 * mutex. A Flush changes bytes, not membership, and must not advance this
 * epoch. A retired pool remains valid while texture references retain it. */
static void nvn_texture_alias_epoch_advance(void) {
    if(nvn_mfc_alias_cache.epoch==UINT64_MAX)
        stop("NVN_LC2_ALIAS_EPOCH_WRAP",nvn_caller,0,24);
    nvn_mfc_alias_cache.epoch++;
}
static unsigned nvn_texture_aliases_cached(const struct nvn_texture *t,const char *reason,unsigned code) {
    uintptr_t address=(uintptr_t)t,first=(uintptr_t)nvn_textures;
    if(!t||address<first||address-first>=sizeof(nvn_textures)||
       (address-first)%sizeof(*t)||!t->live||!t->id||!t->pool)
        stop("NVN_LC2_ALIAS_CACHE_ID",nvn_caller,0,24);
    unsigned slot=(unsigned)(t-nvn_textures);
    struct nvn_mfc_alias_hint *hint=&nvn_mfc_alias_cache.hints[slot];
    if(hint->epoch==nvn_mfc_alias_cache.epoch&&hint->id==t->id&&
       hint->pool==t->pool&&hint->offset==t->b.offset&&hint->size==t->size) {
        nvn_mfc_alias_cache.hits++;
        if(!nvn_mfc_alias_cache.reported) {
            nvn_mfc_alias_cache.reported=1;
            printf("NVN_LC2_ALIAS_CACHE first-hit=1 id=%"PRIu64" epoch=%"PRIu64" hits=%"PRIu64" misses=%"PRIu64" completed-scan-slots=%"PRIu64" validation-only=1 pending=unchanged\n",
                t->id,nvn_mfc_alias_cache.epoch,nvn_mfc_alias_cache.hits,
                nvn_mfc_alias_cache.misses,nvn_mfc_alias_cache.completed_scan_slots);
        }
        return 1;
    }
    nvn_mfc_alias_cache.misses++;
    for(unsigned i=0;i<4096;i++) {
        const struct nvn_texture *other=&nvn_textures[i];
        if(other!=t&&other->live&&other->pool==t->pool&&
           t->b.offset<other->b.offset+other->size&&other->b.offset<t->b.offset+t->size)
            stop(reason,nvn_caller,other->id,code);
    }
    /* Only completed scans are counted; a rejected scan publishes no hint. */
    nvn_mfc_alias_cache.completed_scan_slots+=4096;
    *hint=(struct nvn_mfc_alias_hint){.id=t->id,.epoch=nvn_mfc_alias_cache.epoch,
        .offset=t->b.offset,.size=t->size,.pool=t->pool};
    return 0;
}
static void nvn_mfc_check_aliases_cached(const struct nvn_texture *t) {
    (void)nvn_texture_aliases_cached(t,"NVN_LOW_EXISTING_ALIAS",24);
}
#endif
