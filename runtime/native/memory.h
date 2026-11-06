/* Stack remapping for old Linux kernels. Guest source is inaccessible while
 * mapped: private copy + source protection preserves the observable ownership
 * semantics without needing mremap(DONTUNMAP), unavailable on this kernel. */
struct stack_alias_record {uint64_t dest,source,size;unsigned original_perm,original_attr;};
static struct stack_alias_record aliases[256];   /* Unity 2021 aliases a stack per thread and keeps many (MEDIDO run momo-074) */
static unsigned naliases;
struct shared_region {int fd;uint64_t size;void *writer;unsigned permission;};
static struct shared_region shared_regions[16];
static unsigned nshared;
/* Protection splits do not create new heap backings. Restore their original
 * extent when adjacent pieces agree, so later aliases can cross old split
 * boundaries. Never merge stack aliases: unmap requires their exact identity. */
static void region_coalesce_main_heap(void) {
    size_t out=0;
    for(size_t i=0;i<nregions;i++) {
        struct region r=regions[i];
        if(out) {
            struct region *left=&regions[out-1];
            if(left->size && r.size && left->state==5 && r.state==5 && left->perm==r.perm && left->attr==r.attr &&
               left->name && left->name==r.name && !strcmp(left->name,"main_heap") &&
               left->base>=HEAP_BASE && r.base>=left->base &&
               left->base-HEAP_BASE<heap_size && r.base-HEAP_BASE<heap_size &&
               left->size==r.base-left->base && r.size<=heap_size-(r.base-HEAP_BASE)) {
                left->size+=r.size;continue;
            }
        }
        regions[out++]=r;
    }
    nregions=out;
}
/* Alias-region pages backed by svcMapPhysicalMemory are interchangeable: merging adjacent
 * pieces keeps the fixed region table from filling as the guest grows its virtual heap. */
static void region_coalesce_alias_physical(void) {
    size_t out=0;
    for(size_t i=0;i<nregions;i++) {
        struct region r=regions[i];
        if(out) {
            struct region *left=&regions[out-1];
            if(left->state==5 && r.state==5 && left->perm==r.perm && left->attr==r.attr &&
               left->name && r.name && !strcmp(left->name,"alias_physical") && !strcmp(r.name,"alias_physical") &&
               left->base+left->size==r.base) {
                left->size+=r.size;continue;
            }
        }
        regions[out++]=r;
    }
    nregions=out;
}
static void region_insert(struct region r,uint64_t pc) {
    if(nregions==MAX_REGIONS)stop("REGION_TABLE_FULL",pc,nregions,21);
    size_t at=nregions;
    while(at && regions[at-1].base>r.base){regions[at]=regions[at-1];at--;}
    regions[at]=r;nregions++;
}
/* Internal table/VM primitives require regions_mutex. Public wrappers below
 * own it across copying and restoration, not merely each table edit. */
static void region_protect_locked(uint64_t base,uint64_t size,unsigned perm,unsigned attr,uint64_t pc) {
    t2_gzram_touch(base,size); /* Frente J: no compressed guest block may change protection */
    normal_guard_require_clear(base,size,pc);
    struct region old=query(base);
    if(!old.state || size>old.base+old.size-base || nregions+2>MAX_REGIONS)
        stop("UNSUPPORTED_PROTECTION_RANGE",pc,base,20);
    if(mprotect((void *)(uintptr_t)base,size,perm))stop("PROTECTION_FAILED",pc,base,21);
    size_t at=0;while(at<nregions && regions[at].base!=old.base)at++;
    if(at==nregions)stop("REGION_MISSING",pc,base,24);
    memmove(regions+at,regions+at+1,(nregions-at-1)*sizeof(regions[0]));nregions--;
    if(old.base<base){struct region prefix=old;prefix.size=base-old.base;region_insert(prefix,pc);}
    struct region current=old;current.base=base;current.size=size;current.perm=perm;current.attr=attr;region_insert(current,pc);
    if(base+size<old.base+old.size){struct region suffix=old;suffix.base=base+size;suffix.size=old.base+old.size-suffix.base;region_insert(suffix,pc);}
    region_coalesce_main_heap();
}
static void stack_alias_locked(uint64_t dest,uint64_t source,uint64_t size,unsigned unmap,uint64_t pc) {
    normal_guard_require_clear(dest,size,pc);normal_guard_require_clear(source,size,pc);
    if(!size || (dest|source|size)&(PAGE-1) || source>=AS_END || size>AS_END-source ||
       dest<0x60000000 || dest>=0x70000000 || size>0x70000000-dest)
        stop("INVALID_STACK_ALIAS_RANGE",pc,dest,24);
    if(unmap) {
        unsigned i=0;while(i<naliases && !(aliases[i].dest==dest && aliases[i].source==source && aliases[i].size==size))i++;
        if(i==naliases)stop("UNKNOWN_OR_PARTIAL_STACK_UNMAP",pc,dest,20);
        struct stack_alias_record record=aliases[i];
        if(!accessible(dest,size,PROT_READ))stop("STACK_UNMAP_NOT_READABLE",pc,dest,24);
        region_protect_locked(source,size,3,0,pc);memcpy((void *)(uintptr_t)source,(void *)(uintptr_t)dest,size);
        region_protect_locked(source,size,record.original_perm,record.original_attr,pc);
        if(munmap((void *)(uintptr_t)dest,size))stop("STACK_UNMAP_FAILED",pc,dest,21);
        size_t at=0;while(at<nregions && regions[at].base!=dest)at++;
        if(at==nregions || regions[at].size!=size)stop("ALIAS_REGION_MISMATCH",pc,dest,24);
        memmove(regions+at,regions+at+1,(nregions-at-1)*sizeof(regions[0]));nregions--;
        aliases[i]=aliases[--naliases];return;
    }
    struct region original=query(source),target=query(dest);
    if(naliases==256 || nregions+3>MAX_REGIONS)stop("STACK_ALIAS_TABLE_FULL",pc,naliases,21);
    if(original.perm!=3 || original.attr || size>original.base+original.size-source ||
       target.state || size>target.base+target.size-dest)
        stop("STACK_ALIAS_CONFLICT",pc,dest,24);
    void *mapped=mmap((void *)(uintptr_t)dest,size,3,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(mapped==MAP_FAILED || (uintptr_t)mapped!=dest) {
        if(mapped!=MAP_FAILED)munmap(mapped,size);
        stop("STACK_ALIAS_MAPPING_FAILED",pc,dest,21);
    }
    t2_stack_alias_copy(mapped,source,size);region_protect_locked(source,size,0,1,pc); /* Frente J: sparse copy */
    t2_stack_alias_release_source(source,size); /* Frente J */
    region_insert((struct region){dest,size,3,11,"thread_stack_alias",0},pc);
    aliases[naliases++]=(struct stack_alias_record){dest,source,size,original.perm,original.attr};
}
static unsigned shared_create(uint64_t size,unsigned permission,uint64_t pc) {
    if(nshared==16 || !size || size%PAGE || size>(16U<<20))stop("INVALID_SHARED_REGION",pc,size,24);
    char name[]="/tmp/cotm-switch-shm-XXXXXX";int fd=mkstemp(name);
    if(fd<0)stop("SHARED_BACKING_FAILED",pc,errno,21);
    unlink(name);fcntl(fd,F_SETFD,FD_CLOEXEC);
    if(ftruncate(fd,size))stop("SHARED_RESIZE_FAILED",pc,errno,21);
    void *writer=mmap(NULL,size,3,MAP_SHARED,fd,0);
    if(writer==MAP_FAILED)stop("SHARED_WRITER_FAILED",pc,errno,21);
    shared_regions[nshared]=(struct shared_region){fd,size,writer,permission};return nshared++;
}
static void shared_map_locked(unsigned id,uint64_t address,uint64_t size,unsigned permission,uint64_t pc) {
    normal_guard_require_clear(address,size,pc);
    if(id>=nshared || address%PAGE || size!=shared_regions[id].size || permission!=shared_regions[id].permission)
        stop("INVALID_SHARED_MAP",pc,address,24);
    struct region target=query(address);
    if(target.state || address>=AS_END || size>target.base+target.size-address)stop("SHARED_MAP_CONFLICT",pc,address,24);
    void *p=mmap((void *)(uintptr_t)address,size,permission,MAP_SHARED,shared_regions[id].fd,0);
    if(p==MAP_FAILED || (uintptr_t)p!=address){if(p!=MAP_FAILED)munmap(p,size);stop("SHARED_MAP_FAILED",pc,address,21);}
    region_insert((struct region){address,size,permission,6,"service_shared_memory",0},pc);
}
struct transfer_region {uint64_t address,size;unsigned permission,original_perm,original_attr,open;void *buffer;};
static struct transfer_region transfer_regions[16];
static unsigned transfer_create_locked(uint64_t address,uint64_t size,unsigned permission,uint64_t pc) {
    normal_guard_require_clear(address,size,pc);
    if(permission)stop("TRANSFER_SIMULTANEOUS_ALIAS_UNIMPLEMENTED",pc,permission,20);
    struct region r=query(address);
    if(!size || (address|size)%PAGE || size>(64ULL<<20) || (permission!=0 && permission!=1 && permission!=3) ||
       r.perm!=3 || r.attr || size>r.base+r.size-address)stop("INVALID_TRANSFER_MEMORY",pc,address,24);
    unsigned index=0;while(index<16 && transfer_regions[index].open)index++;
    if(index==16)stop("TRANSFER_TABLE_FULL",pc,index,21);
    const char *option=getenv("T2_TRANSFER_SPARSE_COPY");
    if(option && strcmp(option,"0") && strcmp(option,"1"))stop("TRANSFER_SPARSE_OPTION",pc,0,24);
    unsigned sparse=option && !strcmp(option,"1");
    void *copy=mmap(NULL,size,3,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(copy==MAP_FAILED)stop("TRANSFER_BACKING_FAILED",pc,errno,21);
    uint64_t copied=size,skipped=0;
    if(sparse) {
        /* This destination is fresh anonymous zero-fill. Preserve every
         * nonzero page, leaving zero pages uncommitted. The caller hands off
         * the source exclusively, as required by the original memcpy path;
         * source protection, service ownership and full copyback stay intact. */
        static const uint8_t zero[PAGE];copied=0;
        for(uint64_t off=0;off<size;off+=PAGE) {
            const void *from=(const void *)(uintptr_t)(address+off);
            if(memcmp(from,zero,PAGE)){memcpy((uint8_t *)copy+off,from,PAGE);copied+=PAGE;}
            else skipped+=PAGE;
        }
    } else memcpy(copy,(void *)(uintptr_t)address,size);
    region_protect_locked(address,size,permission,1,pc);
    if(sparse)printf("T2_TRANSFER_SPARSE_COPY index=%u source=0x%" PRIx64 " bytes=%" PRIu64
                     " copied-bytes=%" PRIu64 " skipped-bytes=%" PRIu64 "\n",index,address,size,copied,skipped);
    transfer_regions[index]=(struct transfer_region){address,size,permission,r.perm,r.attr,1,copy};return index;
}
static void transfer_release_locked(unsigned index,uint64_t pc) {
    if(index>=16 || !transfer_regions[index].open)stop("INVALID_TRANSFER_RELEASE",pc,index,24);
    struct transfer_region *r=&transfer_regions[index];
    normal_guard_require_clear(r->address,r->size,pc);
    /* Simultaneous writable aliases need a shared backing implementation. */
    if(r->permission)stop("TRANSFER_ALIAS_COPYBACK_UNPROVED",pc,index,20);
    region_protect_locked(r->address,r->size,3,0,pc);memcpy((void *)(uintptr_t)r->address,r->buffer,r->size);
    region_protect_locked(r->address,r->size,r->original_perm,r->original_attr,pc);
    munmap(r->buffer,r->size);memset(r,0,sizeof(*r));
}
static inline void region_protect(uint64_t base,uint64_t size,unsigned perm,unsigned attr,uint64_t pc) {
    pthread_mutex_lock(&regions_mutex);region_write_begin(pc);region_protect_locked(base,size,perm,attr,pc);region_write_end(pc);pthread_mutex_unlock(&regions_mutex);
}
static void stack_alias(uint64_t dest,uint64_t source,uint64_t size,unsigned unmap,uint64_t pc) {
    pthread_mutex_lock(&regions_mutex);region_write_begin(pc);stack_alias_locked(dest,source,size,unmap,pc);region_write_end(pc);pthread_mutex_unlock(&regions_mutex);
}
static void shared_map(unsigned id,uint64_t address,uint64_t size,unsigned permission,uint64_t pc) {
    pthread_mutex_lock(&regions_mutex);region_write_begin(pc);shared_map_locked(id,address,size,permission,pc);region_write_end(pc);pthread_mutex_unlock(&regions_mutex);
}
static unsigned transfer_create(uint64_t address,uint64_t size,unsigned permission,uint64_t pc) {
    pthread_mutex_lock(&regions_mutex);region_write_begin(pc);unsigned result=transfer_create_locked(address,size,permission,pc);region_write_end(pc);pthread_mutex_unlock(&regions_mutex);return result;
}
static void transfer_release(unsigned index,uint64_t pc) {
    pthread_mutex_lock(&regions_mutex);region_write_begin(pc);transfer_release_locked(index,pc);region_write_end(pc);pthread_mutex_unlock(&regions_mutex);
}
