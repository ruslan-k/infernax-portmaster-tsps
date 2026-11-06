/* Zero-only opt-in for the two measured native GPU ordering barriers.
 * GLES commands on the serialized producer context retain their order.
 * This never publishes completion: counters, real fences and readbacks keep
 * their existing waits. See docs/BARRIER-COST-CONTRACT.md. */
#ifndef ZERO_GLES_BARRIER_H
#define ZERO_GLES_BARRIER_H
static struct {
    unsigned configured,enabled;
    uint64_t primitives,fragments,fallback;
} zero_gles_barrier;
static unsigned zero_gles_barrier_enabled(void) {
    if(!zero_gles_barrier.configured) {
        const char *option=getenv("ZERO_GLES_ORDER_BARRIERS");
        if(option&&strcmp(option,"0")&&strcmp(option,"1"))
            stop("ZERO_GLES_BARRIER_OPTION",nvn_caller,0,24);
        unsigned enabled=option&&!strcmp(option,"1");
        if(enabled&&!nvn_lc2_async_enabled())
            stop("ZERO_GLES_BARRIER_ASYNC_REQUIRED",nvn_caller,0,24);
        zero_gles_barrier.enabled=enabled;zero_gles_barrier.configured=1;
        if(enabled)printf("ZERO_GLES_ORDER_BARRIERS enabled=1 overrides-legacy-barrier=finish qualified-bits=0x1,0x16 operation=glFlush other-bits=glFinish completion=unchanged context=serialized-producer feedback=storage-owner-guarded\n");
    }
    return zero_gles_barrier.enabled;
}
static unsigned zero_gles_barrier_try(uint64_t bits) {
    if(!zero_gles_barrier_enabled())return 0;
    /* Native NVNbarrierBits: 1=ORDER_PRIMITIVES; 0x16 is exactly
     * ORDER_FRAGMENTS|ORDER_FRAGMENTS_TILED|INVALIDATE_TEXTURE.
     * Do not broaden this to arbitrary subsets or combinations. */
    uint64_t *count;
    if(bits==UINT64_C(0x1))count=&zero_gles_barrier.primitives;
    else if(bits==UINT64_C(0x16))count=&zero_gles_barrier.fragments;
    else count=&zero_gles_barrier.fallback;
    if(count==&zero_gles_barrier.fallback)glFinish();else glFlush();
    ++*count;
    if(*count==1||(*count%1024)==0)
        printf("ZERO_GLES_BARRIER_COUNT bits=0x%"PRIx64" action=%s primitives=%"PRIu64" fragments=%"PRIu64" fallback=%"PRIu64" completion=unchanged\n",
            bits,count==&zero_gles_barrier.fallback?"finish":"flush",
            zero_gles_barrier.primitives,zero_gles_barrier.fragments,zero_gles_barrier.fallback);
    return 1;
}
#endif
