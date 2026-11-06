/* Guest memory pools are borrowed address ranges, not eagerly copied GPU
 * allocations. GLES uploads are made for used resources at submission time.
 * Coherent host CPU access uses barriers plus dirty generations. No DMA or
 * unsupported GPU write is reported complete by this memory model.
 */
#define NVN_SHADER_SCRATCH_GRANULARITY (128U*1024U)
#define ZERO_SHADER_SCRATCH_ALIGNMENT 4096U
struct nvn_pool {uint64_t object,address,size,generation;uint32_t flags,references;unsigned live,retired,owned,zero_r8_target_seen;};
struct nvn_buffer {uint64_t object,offset,size,id;struct nvn_pool *pool;unsigned live;};
static struct nvn_pool nvn_pools[256];
static void zero_texture_pool_flushed(struct nvn_pool *,uint64_t,uint64_t);
static void zero_texture_require_cpu(const struct nvn_pool *,uint64_t,uint64_t);
/* NVN COMPRESSIBLE is 0x80, SHADER_CODE is 0x40; PHYSICAL/VIRTUAL are
 * 0x100/0x200 and remain unsupported. A null SetStorage address asks the
 * driver to allocate backing. Anonymous CPU/GPU shared backing is demand
 * paged by Linux; Map must return its real writable address.
 * Interface: open-ead/nnheaders 6bc6466d, include/nvn/nvn.h. */
static uint64_t zero_owned_pool_next=0x4000000000ULL;
static void zero_pool_collect(void) {
    for(unsigned i=0;i<256;i++) {
        struct nvn_pool *p=&nvn_pools[i];
        if(!p->owned || p->live || p->references)continue;
        pthread_mutex_lock(&regions_mutex);region_write_begin(nvn_caller);
        size_t at=0;while(at<nregions && regions[at].base!=p->address)at++;
        if(at==nregions || regions[at].size!=p->size || strcmp(regions[at].name,"zero_nvn_pool"))
            stop("ZERO_OWNED_POOL_REGION",nvn_caller,p->address,24);
        if(munmap((void *)(uintptr_t)p->address,p->size))stop("ZERO_OWNED_POOL_UNMAP",nvn_caller,errno,21);
        memmove(regions+at,regions+at+1,(nregions-at-1)*sizeof(regions[0]));nregions--;
        region_write_end(nvn_caller);pthread_mutex_unlock(&regions_mutex);
        p->owned=0;p->retired=0;
    }
}
static uint64_t zero_pool_allocate(uint64_t size) {
    pthread_mutex_lock(&regions_mutex);region_write_begin(nvn_caller);
    uint64_t address=zero_owned_pool_next;
    if(!size || size%4096 || nregions==MAX_REGIONS)stop("ZERO_OWNED_POOL_CAPACITY",nvn_caller,size,21);
    for(;;) {
        if(address>=AS_END-4096 || size>AS_END-address-4096)stop("ZERO_OWNED_POOL_CAPACITY",nvn_caller,size,21);
        struct region available=query(address);uint64_t end=available.base+available.size;
        if(available.state || size>end-address) {
            if(end<=address || end>=AS_END)stop("ZERO_OWNED_POOL_CAPACITY",nvn_caller,size,21);
            address=(end+4095)&~4095ULL;continue;
        }
        /* A hint never replaces an existing host mapping. Region tracking and
         * address selection stay under the same lock, including concurrent
         * native pool creation. Skip unrelated host mappings if Linux moves it. */
        void *memory=mmap((void *)(uintptr_t)address,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(memory==MAP_FAILED)stop("ZERO_OWNED_POOL_MAP",nvn_caller,errno,21);
        if((uintptr_t)memory==address)break;
        if(munmap(memory,size))stop("ZERO_OWNED_POOL_HINT_UNMAP",nvn_caller,errno,21);
        address+=0x40000000ULL;
    }
    region_insert((struct region){address,size,3,5,"zero_nvn_pool",0},nvn_caller);
    zero_owned_pool_next=address+size+4096;
    region_write_end(nvn_caller);pthread_mutex_unlock(&regions_mutex);
    return address;
}
/* Terminator run 514 reaches 4096 live buffer descriptors after the full intro.
 * These are borrowed pool slices, not copied mesh/GPU payloads. Reserve 768 KiB
 * of metadata (48 bytes each on AArch64); lazy mesh creation stays unchanged.
 * Only scan slots actually used, and reuse finalized slots before growing. */
#define NVN_BUFFER_CAPACITY 16384U
static struct nvn_buffer nvn_buffers[NVN_BUFFER_CAPACITY];
static struct {unsigned slots,live,high_water;uint64_t initialized,finalized,reinitialized;} nvn_buffer_stats;
static void nvn_buffer_report(const char *reason) {
    printf("T2_BUFFER_TABLE reason=%s live=%u high-water=%u slots=%u capacity=%u metadata-bytes=%zu initialized=%"PRIu64" finalized=%"PRIu64" reinitialized=%"PRIu64"\n",
        reason,nvn_buffer_stats.live,nvn_buffer_stats.high_water,nvn_buffer_stats.slots,NVN_BUFFER_CAPACITY,
        sizeof(nvn_buffers),nvn_buffer_stats.initialized,nvn_buffer_stats.finalized,nvn_buffer_stats.reinitialized);
}
static struct nvn_buffer *nvn_buffer_find(uint64_t object) {
    for(unsigned i=0;i<nvn_buffer_stats.slots;i++)if(nvn_buffers[i].object==object&&nvn_buffers[i].live)return &nvn_buffers[i];
    stop("NVN_BUFFER_INVALID",nvn_caller,object,24);return NULL;
}
static uint64_t nvn_next_object_id;
static uint64_t zero_next_object_id(void) {
    uint64_t previous=__atomic_fetch_add(&nvn_next_object_id,1,__ATOMIC_RELAXED);
    if(previous==UINT64_MAX)stop("NVN_OBJECT_ID_CAPACITY",nvn_caller,previous,24);
    return previous+1;
}
static struct nvn_pool *nvn_pool_find(uint64_t object) {
    for(unsigned i=0;i<256;i++)if(nvn_pools[i].object==object && nvn_pools[i].live)return &nvn_pools[i];
    stop("NVN_MEMORY_POOL_INVALID",nvn_caller,object,24);return NULL;
}
static uint64_t nvn_builder_get(unsigned id,uint64_t builder,struct nvn_frame *caller) {
    struct nvn_frame f={.args={builder},.platform=caller->platform,.lr=caller->lr};return nvn_original_helper(id,&f);
}
static void nvn_debug_label(uint64_t address) {
    if(!address)return;
    for(unsigned i=0;i<256;i++)if(!*(char *)nvn_pointer(address+i,1,PROT_READ))return;
    stop("NVN_DEBUG_LABEL_LENGTH",nvn_caller,address,24);
}
static void nvn_buffer_initialize(struct nvn_frame *f,uint64_t *result) {
    uint64_t *a=f->args,device;
    void *object=nvn_pointer(a[0],48,PROT_READ|PROT_WRITE);
    memcpy(&device,nvn_pointer(a[1]+8,8,PROT_READ),8); /* SDK 6.5 builder prefix */
    if(!nvn_device.live || device!=nvn_device.address)stop("NVN_BUFFER_DEVICE",nvn_caller,device,24);
    struct nvn_pool *p=nvn_pool_find(nvn_builder_get(NI_nvnBufferBuilderGetMemoryPool,a[1],f));
    uint64_t offset=nvn_builder_get(NI_nvnBufferBuilderGetMemoryOffset,a[1],f),size=nvn_builder_get(NI_nvnBufferBuilderGetSize,a[1],f);
    if(offset>p->size || size>p->size-offset)stop("NVN_BUFFER_RANGE",nvn_caller,size,24);
    pthread_mutex_lock(&kernel_mutex);
    normal_guard_require_clear(p->address+offset,size,nvn_caller);
    unsigned slot=NVN_BUFFER_CAPACITY;struct nvn_buffer *previous=NULL;
    for(unsigned i=0;i<nvn_buffer_stats.slots;i++) {
        if(nvn_buffers[i].live && nvn_buffers[i].object==a[0]){previous=&nvn_buffers[i];slot=i;break;}
        if(!nvn_buffers[i].live && slot==NVN_BUFFER_CAPACITY)slot=i;
    }
    if(slot==NVN_BUFFER_CAPACITY)slot=nvn_buffer_stats.slots;
    if(slot==NVN_BUFFER_CAPACITY){nvn_buffer_report("capacity");stop("NVN_BUFFER_CAPACITY",nvn_caller,slot,21);}
    struct nvn_pool *old=previous?previous->pool:NULL;
    if(old && !old->references)stop("NVN_BUFFER_REFERENCE_STATE",nvn_caller,a[0],24);
    if(old!=p && p->references==UINT32_MAX)stop("NVN_BUFFER_REFERENCE_CAPACITY",nvn_caller,a[0],24);
    /* LC2 main378c00 -> main378c18 deliberately initializes one buffer twice.
     * SDK2d5d08 replaces its 48-byte value; SDK2d5d50 Finalize is empty.
     * All checks precede this transaction. Other users retain their pool refs. */
    const uint64_t value[6]={0,device,p->object,offset,size,0};
    if(old!=p) {
        if(old && !--old->references)old->retired=0;
        p->references++;
    }
    nvn_buffers[slot]=(struct nvn_buffer){a[0],offset,size,zero_next_object_id(),p,1};
    memcpy(object,value,sizeof(value));
    if(slot==nvn_buffer_stats.slots)nvn_buffer_stats.slots++;
    nvn_buffer_stats.initialized++;
    if(previous)nvn_buffer_stats.reinitialized++;
    else if(++nvn_buffer_stats.live>nvn_buffer_stats.high_water) {
        unsigned peak=nvn_buffer_stats.high_water=nvn_buffer_stats.live;
        if(peak==4097 || !(peak&(peak-1)))nvn_buffer_report("high-water");
    }
    pthread_mutex_unlock(&kernel_mutex);*result=1;
}
static void nvn_buffer_finalize(struct nvn_buffer *b) {
    pthread_mutex_lock(&kernel_mutex);
    struct nvn_pool *p=b->pool;
    b->live=0;if(!--p->references)p->retired=0;
    nvn_buffer_stats.live--;nvn_buffer_stats.finalized++;
    pthread_mutex_unlock(&kernel_mutex);
}
static unsigned nvn_memory_dispatch(unsigned id,struct nvn_frame *f,uint64_t *result) {
    uint64_t *a=f->args;*result=0;
    if(id==NI_nvnMemoryPoolInitialize) {
        nvn_pointer(a[0],8,PROT_READ|PROT_WRITE);uint64_t device;memcpy(&device,nvn_pointer(a[1]+8,8,PROT_READ),8);
        if(!nvn_device.live || device!=nvn_device.address)stop("NVN_MEMORY_POOL_DEVICE",nvn_caller,device,24);
        uint64_t address=nvn_builder_get(NI_nvnMemoryPoolBuilderGetMemory,a[1],f),size=nvn_builder_get(NI_nvnMemoryPoolBuilderGetSize,a[1],f);
        uint32_t flags=nvn_builder_get(NI_nvnMemoryPoolBuilderGetFlags,a[1],f);
        unsigned cpu=flags&7,gpu=flags&0x38;
        if(flags&~0xffU || (cpu!=1&&cpu!=2&&cpu!=4) || (gpu!=8&&gpu!=16&&gpu!=32))stop("NVN_MEMORY_POOL_FLAGS",nvn_caller,flags,20);
        if(!size || size>MEMORY_BUDGET || (address|size)%4096)stop("NVN_MEMORY_POOL_RANGE",nvn_caller,size,24);
        zero_pool_collect();
        unsigned owned=!address;
        if(owned)address=zero_pool_allocate(size);
        else nvn_pointer(address,size,PROT_READ|PROT_WRITE);
        unsigned slot=256;
        for(unsigned i=0;i<256;i++) {
            struct nvn_pool *p=&nvn_pools[i];
            if(!p->live && !p->retired){if(slot==256)slot=i;continue;}
            if(p->object==a[0] || (address<p->address+p->size && p->address<address+size))stop("NVN_MEMORY_POOL_OVERLAP",nvn_caller,address,24);
        }
        if(slot==256)stop("NVN_MEMORY_POOL_CAPACITY",nvn_caller,slot,21);
        nvn_pools[slot]=(struct nvn_pool){.object=a[0],.address=address,.size=size,.flags=flags,.live=1,.generation=1,.owned=owned};
        printf("NVN_MEMORY_POOL_BOUND address=0x%"PRIx64" size=0x%"PRIx64" flags=0x%x storage=%s lazy-gpu-upload\n",address,size,flags,
               owned?"driver-owned-demand-paged":"borrowed-guest");
        *result=1;return 1;
    }
    if(id==NI_nvnMemoryPoolMap || id==NI_nvnMemoryPoolGetBufferAddress || id==NI_nvnMemoryPoolGetSize || id==NI_nvnMemoryPoolGetFlags ||
       id==NI_nvnMemoryPoolFlushMappedRange || id==NI_nvnMemoryPoolInvalidateMappedRange || id==NI_nvnMemoryPoolSetDebugLabel || id==NI_nvnMemoryPoolFinalize) {
        struct nvn_pool *p=nvn_pool_find(a[0]);
        if(id==NI_nvnMemoryPoolMap){if(!(p->flags&1))zero_texture_require_cpu(p,0,p->size);*result=(p->flags&1)?0:p->address;}
        if(id==NI_nvnMemoryPoolGetBufferAddress){normal_guard_require_clear(p->address,p->size,nvn_caller);*result=(p->flags&8)?0:p->address;}
        if(id==NI_nvnMemoryPoolGetSize)*result=p->size;
        if(id==NI_nvnMemoryPoolGetFlags)*result=p->flags;
        if(id==NI_nvnMemoryPoolFlushMappedRange || id==NI_nvnMemoryPoolInvalidateMappedRange) {
            if(a[1]>p->size || a[2]>p->size-a[1] || (p->flags&1))stop("NVN_MEMORY_POOL_CACHE_RANGE",nvn_caller,a[1],24);
            zero_texture_require_cpu(p,a[1],a[2]);
            __atomic_thread_fence(__ATOMIC_SEQ_CST);if(id==NI_nvnMemoryPoolFlushMappedRange){p->generation++;zero_texture_pool_flushed(p,a[1],a[2]);}
        }
        if(id==NI_nvnMemoryPoolSetDebugLabel)nvn_debug_label(a[1]);
        if(id==NI_nvnMemoryPoolFinalize){p->live=0;p->retired=p->references!=0;zero_pool_collect();}
        return 1;
    }
    if(id==NI_nvnBufferInitialize) {
        nvn_buffer_initialize(f,result);return 1;
    }
    if(id==NI_nvnBufferMap || id==NI_nvnBufferGetAddress || id==NI_nvnBufferGetSize || id==NI_nvnBufferGetMemoryOffset || id==NI_nvnBufferGetMemoryPool ||
       id==NI_nvnBufferGetDebugID || id==NI_nvnBufferFlushMappedRange || id==NI_nvnBufferInvalidateMappedRange || id==NI_nvnBufferSetDebugLabel || id==NI_nvnBufferFinalize) {
        struct nvn_buffer *b=nvn_buffer_find(a[0]);struct nvn_pool *p=b->pool;
        if(id==NI_nvnBufferMap){if(!(p->flags&1))zero_texture_require_cpu(p,b->offset,b->size);*result=(p->flags&1)?0:p->address+b->offset;}
        if(id==NI_nvnBufferGetAddress){normal_guard_require_clear(p->address+b->offset,b->size,nvn_caller);*result=(p->flags&8)?0:p->address+b->offset;}
        if(id==NI_nvnBufferGetSize)*result=b->size;
        if(id==NI_nvnBufferGetMemoryOffset)*result=b->offset;
        if(id==NI_nvnBufferGetMemoryPool)*result=p->object;
        if(id==NI_nvnBufferGetDebugID)*result=b->id;
        if(id==NI_nvnBufferFlushMappedRange || id==NI_nvnBufferInvalidateMappedRange) {
            if(a[1]>b->size || a[2]>b->size-a[1] || (p->flags&1))stop("NVN_BUFFER_CACHE_RANGE",nvn_caller,a[1],24);
            zero_texture_require_cpu(p,b->offset+a[1],a[2]);
            __atomic_thread_fence(__ATOMIC_SEQ_CST);if(id==NI_nvnBufferFlushMappedRange){p->generation++;zero_texture_pool_flushed(p,b->offset+a[1],a[2]);}
        }
        if(id==NI_nvnBufferSetDebugLabel)nvn_debug_label(a[1]);
        if(id==NI_nvnBufferFinalize)nvn_buffer_finalize(b);
        return 1;
    }
    return 0;
}
