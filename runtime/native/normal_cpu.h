/* Called with the producer context owned; take regions only in that order.
 * Protection persists across clears, until the texture is actually deleted.
 * A full GL clear cannot make a raw guest memory alias represent GPU pixels. */
static void normal_cpu_backing(uint64_t base,uint64_t size,uint64_t pc) {
    struct region r=query(base);
    if(!size || (base|size)%PAGE || base<HEAP_BASE || base-HEAP_BASE>=heap_size || size>heap_size-(base-HEAP_BASE) ||
       r.state!=5 || r.perm!=3 || r.attr || !r.name || strcmp(r.name,"main_heap") || base-r.base>r.size || size>r.size-(base-r.base))
        stop("NVN_NORMAL_CPU_BACKING",pc,base,24);
    for(unsigned i=0;i<naliases;i++)if(normal_ranges_overlap(base,size,aliases[i].source,aliases[i].size) ||
        normal_ranges_overlap(base,size,aliases[i].dest,aliases[i].size))stop("NVN_NORMAL_EXISTING_CPU_ALIAS",pc,base,24);
    for(unsigned i=0;i<16;i++)if(transfer_regions[i].open && normal_ranges_overlap(base,size,transfer_regions[i].address,transfer_regions[i].size))
        stop("NVN_NORMAL_EXISTING_TRANSFER",pc,base,24);
}
static void normal_cpu_protect_locked(uint64_t base,uint64_t size,uint64_t id,uint64_t pc) {
    normal_cpu_backing(base,size,pc);normal_guard_require_clear(base,size,pc);
    unsigned count=__atomic_load_n(&normal_guard_count,__ATOMIC_RELAXED);
    if(!id || count==NORMAL_GUARD_MAX)stop("NVN_NORMAL_GUARD_CAPACITY",pc,count,21);
    for(unsigned i=0;i<count;i++)if(normal_guards[i].id==id)stop("NVN_NORMAL_GUARD_ID_REUSED",pc,id,24);
    region_write_begin(pc);
    struct normal_guard *g=&normal_guards[count];g->base=base;g->size=size;g->id=id;
    __atomic_store_n(&g->active,1,__ATOMIC_RELEASE);__atomic_store_n(&normal_guard_count,count+1,__ATOMIC_RELEASE);
    if(mprotect((void *)(uintptr_t)base,size,PROT_NONE))stop("NVN_NORMAL_CPU_PROTECT",pc,errno,21);
    region_write_end(pc);
}
static inline void normal_cpu_protect(uint64_t base,uint64_t size,uint64_t id,uint64_t pc) {
    pthread_mutex_lock(&regions_mutex);normal_cpu_protect_locked(base,size,id,pc);pthread_mutex_unlock(&regions_mutex);
}
static void normal_cpu_restore(uint64_t id,uint64_t pc) {
    pthread_mutex_lock(&regions_mutex);
    unsigned count=__atomic_load_n(&normal_guard_count,__ATOMIC_RELAXED),i=0;
    while(i<count && !(normal_guards[i].id==id && __atomic_load_n(&normal_guards[i].active,__ATOMIC_RELAXED)))i++;
    if(i==count)stop("NVN_NORMAL_GUARD_MISSING",pc,id,24);
    struct normal_guard *g=&normal_guards[i];normal_cpu_backing(g->base,g->size,pc);
    region_write_begin(pc);
    if(mprotect((void *)(uintptr_t)g->base,g->size,PROT_READ|PROT_WRITE))stop("NVN_NORMAL_CPU_RESTORE",pc,errno,21);
    __atomic_store_n(&g->active,0,__ATOMIC_RELEASE);region_write_end(pc);pthread_mutex_unlock(&regions_mutex);
}
