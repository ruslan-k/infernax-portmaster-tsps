/* B9: GLES consumes the unchanged u16 stream directly. As with client vertex
 * arrays, guest storage remains borrowed through glDrawElements. A fresh scan
 * still validates the maximum fetched vertex on every draw; bytes are never
 * cached. Other types, unaligned storage and negative base vertex keep rebasing. */
static struct {unsigned enabled,after_frame;uint64_t direct,indices,fallback;} nvn_index_fast;
static pthread_once_t nvn_index_fast_once=PTHREAD_ONCE_INIT;
static void nvn_index_fast_configure(void) {
    const char *option=getenv("COTM_DIRECT_U16_INDICES");
    if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("NVN_DIRECT_INDEX_OPTION",nvn_caller,0,24);
    nvn_index_fast.enabled=option&&!strcmp(option,"1");
    if(nvn_index_fast.enabled)printf("NVN_DIRECT_INDEX_EXPERIMENT enabled=1 type=u16 scan=fresh-maximum storage=borrowed-through-draw fallback=other-type-unaligned-negative-base\n");
}
static unsigned nvn_index_direct_u16(unsigned type,uint64_t count,const uint8_t *src,int64_t vertex_base,
                                    uint64_t *base,uint64_t *maximum,const uint16_t **indices) {
    if(pthread_once(&nvn_index_fast_once,nvn_index_fast_configure))stop("NVN_DIRECT_INDEX_ONCE",nvn_caller,0,21);
    if(!nvn_index_fast.enabled)return 0;
    if(type!=1||!count||count>1048576||((uintptr_t)src&1)||vertex_base<0) {
        nvn_index_fast.fallback++;return 0;
    }
    uint32_t max=0;
    for(uint64_t i=0;i<count;i++){uint32_t value=(uint32_t)src[i*2]|((uint32_t)src[i*2+1]<<8);if(value>max)max=value;}
    *base=vertex_base;*maximum=max;*indices=(const uint16_t *)src;
    nvn_index_fast.direct++;nvn_index_fast.indices+=count;return 1;
}
static void nvn_index_fast_report(unsigned frame) {
    if(!nvn_index_fast.enabled||frame<nvn_index_fast.after_frame||frame-nvn_index_fast.after_frame<60)return;
    printf("NVN_DIRECT_INDEX_PROFILE after-frame=%u frames=%u direct-draws=%"PRIu64" direct-indices=%"PRIu64" fallback-draws=%"PRIu64" copied-index-bytes=0\n",
        frame,frame-nvn_index_fast.after_frame,nvn_index_fast.direct,nvn_index_fast.indices,nvn_index_fast.fallback);
    nvn_index_fast.after_frame=frame;nvn_index_fast.direct=nvn_index_fast.indices=nvn_index_fast.fallback=0;
}
