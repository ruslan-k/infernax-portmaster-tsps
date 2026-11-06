/* B6 is an authorization snapshot, not a memory pin. Eligibility is restricted
 * to an unchanged private RW main-heap region. Pool/texture/buffer checks still
 * run in their callers. Normal guards are consulted fresh on every hit. */
static pthread_once_t nvn_pointer_fast_once=PTHREAD_ONCE_INIT;
static unsigned nvn_pointer_fast_enabled;
#define NVN_POINTER_FAST_WAYS 16U
static _Thread_local struct {
    uint64_t base,size,generation,hits,fallbacks,invalidations;
    unsigned valid;
    /* Additional authorized main-heap regions (same contract), round-robin. */
    struct {uint64_t base,size;} ways[NVN_POINTER_FAST_WAYS];unsigned way_count,way_next;
} nvn_pointer_fast;
static void nvn_pointer_fast_configure(void) {
    const char *option=getenv("COTM_POINTER_FAST");
    if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("NVN_POINTER_FAST_OPTION",nvn_caller,0,24);
    nvn_pointer_fast_enabled=option&&!strcmp(option,"1");
    if(nvn_pointer_fast_enabled)printf("NVN_POINTER_FAST_EXPERIMENT enabled=1 scope=private-RW-main-heap validation=immutable-snapshot-generation guard=fresh lifetime=authorization-only\n");
}
static unsigned nvn_pointer_fast_configured(void) {
    if(signal_handler_depth)return 0;
    if(pthread_once(&nvn_pointer_fast_once,nvn_pointer_fast_configure))stop("NVN_POINTER_FAST_ONCE",nvn_caller,0,21);
    return nvn_pointer_fast_enabled;
}
static unsigned nvn_pointer_fast_try(uint64_t address,uint64_t size,unsigned perm) {
    if(!nvn_pointer_fast_configured())return 0;
    uint64_t generation=region_generation_read();
    if(nvn_pointer_fast.valid && generation!=nvn_pointer_fast.generation) {
        nvn_pointer_fast.valid=0;nvn_pointer_fast.way_count=nvn_pointer_fast.way_next=0;nvn_pointer_fast.invalidations++;
    }
    if(nvn_pointer_fast.valid && !(generation&1) && size && address<AS_END && size<=AS_END-address && !(perm&~(PROT_READ|PROT_WRITE))) {
        for(unsigned w=0;w<nvn_pointer_fast.way_count;w++) {
            uint64_t base=nvn_pointer_fast.ways[w].base,span=nvn_pointer_fast.ways[w].size;
            if(address>=base && address-base<span && size<=span-(address-base) && !normal_guard_overlap(address,size) &&
               region_generation_read()==generation){nvn_pointer_fast.hits++;return 1;}
        }
    }
    nvn_pointer_fast.fallbacks++;return 0;
}
static void nvn_pointer_fast_remember_locked(uint64_t address,uint64_t size) {
    if(signal_handler_depth || !nvn_pointer_fast_enabled || !size)return;
    uint64_t generation=region_generation_read();if(generation&1)return;
    struct region r=query(address);
    if(r.state!=5 || r.perm!=(PROT_READ|PROT_WRITE) || r.attr || !r.name || strcmp(r.name,"main_heap") ||
       r.base<HEAP_BASE || r.base-HEAP_BASE>=heap_size || r.size>heap_size-(r.base-HEAP_BASE) ||
       address<r.base || address-r.base>=r.size || size>r.size-(address-r.base))return;
    if(!nvn_pointer_fast.valid||nvn_pointer_fast.generation!=generation){nvn_pointer_fast.way_count=nvn_pointer_fast.way_next=0;}
    nvn_pointer_fast.base=r.base;nvn_pointer_fast.size=r.size;nvn_pointer_fast.generation=generation;nvn_pointer_fast.valid=1;
    for(unsigned w=0;w<nvn_pointer_fast.way_count;w++)if(nvn_pointer_fast.ways[w].base==r.base&&nvn_pointer_fast.ways[w].size==r.size)return;
    unsigned slot=nvn_pointer_fast.way_count<NVN_POINTER_FAST_WAYS?nvn_pointer_fast.way_count++:nvn_pointer_fast.way_next;
    nvn_pointer_fast.way_next=(slot+1)%NVN_POINTER_FAST_WAYS;
    nvn_pointer_fast.ways[slot].base=r.base;nvn_pointer_fast.ways[slot].size=r.size;
}
static void nvn_pointer_fast_report(unsigned frame,unsigned frames) {
    if(!nvn_pointer_fast_enabled)return;
    printf("NVN_POINTER_FAST_PROFILE after-frame=%u frames=%u thread=%u hits=%"PRIu64" fallbacks=%"PRIu64" invalidations=%"PRIu64
        " generation=%"PRIu64" ways=%u scope=calling-thread lifetime=authorization-only\n",frame,frames,guest_thread_index,
        nvn_pointer_fast.hits,nvn_pointer_fast.fallbacks,nvn_pointer_fast.invalidations,region_generation_read(),nvn_pointer_fast.way_count);
    nvn_pointer_fast.hits=nvn_pointer_fast.fallbacks=nvn_pointer_fast.invalidations=0;
}
