/* LC2-specific equivalent zero-fill for verified private anonymous mappings.
 * Include after image/region tables, memory.h, normal_guard.h, region_generation.h,
 * accessible(), stop(), ucontext_t and line logging. No pool size is changed.
 * Configure before entries are trapped. The dispatcher already owns kernel_mutex;
 * trylock(regions_mutex) avoids introducing a wait or reversing lock order.
 */
#ifndef MEGAMAN_MEMORY_H
#define MEGAMAN_MEMORY_H
#define LC2_MEMORY_SDK_SHA256 "b0bb3705056fed72c5a1fc6821eb28d6b52df07e0a883a2b5b206a403ae8b1f3"
#define LC2_MEMORY_RTLD_SHA256 "895fcab2971bfcb48987e5d3c522359de5a08e576662ae19c9d739f4f8524c0a"
#define LC2_MEMORY_SDK_MEMSET_OFFSET 0x50d690ULL
#define LC2_MEMORY_RTLD_MEMSET_OFFSET 0x1a20ULL
#define LC2_MEMORY_MEMSET_OPCODE 0xb4000642U
#define LC2_MEMORY_ZERO_MIN (1ULL<<20)
#ifndef MEGAMAN_MEMORY_MADVISE
#define MEGAMAN_MEMORY_MADVISE madvise
#endif
static uint64_t megaman_memory_sdk_base,megaman_memory_rtld_base;
static unsigned megaman_memory_enabled;
static uint64_t megaman_memory_zero_calls,megaman_memory_zero_bytes;
static struct {uint64_t base,size;const char *image_name;} megaman_memory_bss[16];
static unsigned megaman_memory_bss_count;

static void megaman_memory_configure(uint64_t sdk_base,const char *sdk_sha,
                                     uint64_t rtld_base,const char *rtld_sha) {
    const char *setting=getenv("MEGAMAN_LAZY_ZERO");
    megaman_memory_sdk_base=sdk_base;megaman_memory_rtld_base=rtld_base;
    megaman_memory_enabled=0;megaman_memory_bss_count=0;
    if(!setting || !strcmp(setting,"0"))return;
    if(strcmp(setting,"1"))stop("LAZY_ZERO_INVALID_SETTING",0,0,24);
    if(!sdk_sha || !rtld_sha || strcmp(sdk_sha,LC2_MEMORY_SDK_SHA256) || strcmp(rtld_sha,LC2_MEMORY_RTLD_SHA256))
        stop("LAZY_ZERO_NSO_IDENTITY",0,0,24);
    uint64_t entries[]={sdk_base+LC2_MEMORY_SDK_MEMSET_OFFSET,rtld_base+LC2_MEMORY_RTLD_MEMSET_OFFSET};
    static const struct {unsigned offset;uint32_t opcode;} guards[]={
        {0,LC2_MEMORY_MEMSET_OPCODE},{4,0x8b000048U},{8,0xf1000c5fU},
        {12,0x39000001U},{0xc8,0xd65f03c0U}
    };
    for(unsigned i=0;i<2;i++)for(unsigned j=0;j<sizeof(guards)/sizeof(guards[0]);j++){
        uint64_t at=entries[i]+guards[j].offset;uint32_t word=0;
        if(!accessible(at,4,PROT_READ))stop("LAZY_ZERO_CODE_ADDRESS",at,0,24);
        memcpy(&word,(const void*)(uintptr_t)at,4);
        if(word!=guards[j].opcode)stop("LAZY_ZERO_CODE_IDENTITY",at,word,24);
    }
    /* All images have already been allocated MAP_PRIVATE|MAP_ANONYMOUS by
     * map_region() and populated only through file_size. Derive exact BSS
     * bounds from each MOD0 and constrain them to the uninitialized data tail.
     * Parent image pins are enforced by prepare_native, not self-declared here. */
    for(unsigned i=2;i<sizeof(images)/sizeof(images[0]);i++){
        const struct image_segment *data=&images[i],*code=&images[i-2];
        const char *suffix=strstr(data->name,".data.seg");
        if(!suffix || strcmp(suffix,".data.seg"))continue;
        size_t prefix=(size_t)(suffix-data->name);
        if(strncmp(code->name,data->name,prefix) || strcmp(code->name+prefix,".text.seg") ||
           data->perm!=(PROT_READ|PROT_WRITE) || data->state!=4 || code->perm!=(PROT_READ|PROT_EXEC))
            stop("LAZY_ZERO_IMAGE_LAYOUT",data->base,i,24);
        uint32_t mod_offset=0;memcpy(&mod_offset,(const void*)(uintptr_t)(code->base+4),4);
        uint64_t mod=code->base+mod_offset;uint32_t magic=0;int32_t bounds[2];
        if(mod<code->base || mod>=AS_END || !accessible(mod,16,PROT_READ))stop("LAZY_ZERO_MOD0_ADDRESS",mod,i,24);
        memcpy(&magic,(const void*)(uintptr_t)mod,4);
        memcpy(bounds,(const void*)(uintptr_t)(mod+8),8);
        int64_t begin=(int64_t)mod+bounds[0],end=(int64_t)mod+bounds[1];
        if(magic!=0x30444f4d || begin<0 || end<begin ||
           (uint64_t)begin<data->base+data->file_size || (uint64_t)end>data->base+data->map_size ||
           megaman_memory_bss_count==16)stop("LAZY_ZERO_BSS_BOUNDS",mod,(uint64_t)begin,24);
        unsigned slot=megaman_memory_bss_count++;
        megaman_memory_bss[slot].base=(uint64_t)begin;
        megaman_memory_bss[slot].size=(uint64_t)(end-begin);
        megaman_memory_bss[slot].image_name=data->name;
    }
    megaman_memory_enabled=1;
    struct line log={0};text(&log,"LAZY_ZERO_POLICY enabled=1 scope=SDK-private-heap,rtld-exact-BSS min-bytes=");
    hex(&log,LC2_MEMORY_ZERO_MIN);text(&log," semantics=memset-zero pools=unchanged");emit(&log);
}

static unsigned megaman_memory_entry(uint64_t pc,uint32_t opcode){
    if(!megaman_memory_enabled)return 0;
    unsigned kind=pc==megaman_memory_sdk_base+LC2_MEMORY_SDK_MEMSET_OFFSET?1:
        pc==megaman_memory_rtld_base+LC2_MEMORY_RTLD_MEMSET_OFFSET?2:0;
    if(kind && opcode!=LC2_MEMORY_MEMSET_OPCODE)stop("LAZY_ZERO_ENTRY_IDENTITY",pc,opcode,24);
    return kind;
}

/* regions_mutex is held across validation, advice and edge writes. Only the
 * two proven backing classes are admitted, with no alias/transfer/normal GPU
 * protection. Other memset calls keep their complete original implementation. */
static unsigned megaman_memory_scope_locked(unsigned kind,uint64_t address,uint64_t size){
    const char *expected_name=NULL;unsigned expected_state=0;
    if(!size || address>=AS_END || size>AS_END-address)return 0;
    if(kind==1){
        if(address<HEAP_BASE || address-HEAP_BASE>=heap_size || size>heap_size-(address-HEAP_BASE))return 0;
        expected_name="main_heap";expected_state=5;
    } else {
        for(unsigned i=0;i<megaman_memory_bss_count;i++)
            if(address==megaman_memory_bss[i].base && size==megaman_memory_bss[i].size)
                expected_name=megaman_memory_bss[i].image_name;
        if(!expected_name)return 0;
        expected_state=4;
    }
    if(normal_guard_overlap(address,size))return 0;
    for(unsigned i=0;i<naliases;i++)
        if(normal_ranges_overlap(address,size,aliases[i].source,aliases[i].size) ||
           normal_ranges_overlap(address,size,aliases[i].dest,aliases[i].size))return 0;
    for(unsigned i=0;i<sizeof(transfer_regions)/sizeof(transfer_regions[0]);i++)
        if(transfer_regions[i].open && normal_ranges_overlap(address,size,transfer_regions[i].address,transfer_regions[i].size))return 0;
    for(uint64_t current=address,left=size;left;){
        struct region r=query(current);
        if(r.state!=expected_state || r.perm!=(PROT_READ|PROT_WRITE) || r.attr ||
           !r.name || strcmp(r.name,expected_name) || current<r.base || current-r.base>=r.size)return 0;
        uint64_t step=r.size-(current-r.base);if(step>left)step=left;
        current+=step;left-=step;
    }
    return 1;
}

static unsigned megaman_memory_handle(uint64_t pc,uint32_t opcode,ucontext_t *ctx){
    unsigned kind=megaman_memory_entry(pc,opcode);if(!kind)return 0;
    uint64_t address=ctx->uc_mcontext.regs[0],size=ctx->uc_mcontext.regs[2];
    /* CBZ x2, entry+0x134 is the exact first instruction of both versions.
     * It does not change NZCV or general registers. Replay it on every fallback. */
    /* The taken target comes from the pinned CBZ immediate (LC2 +0x134, Animal Well SDK 17 +0xc8). */
    {int64_t taken=(int64_t)((int32_t)(((opcode>>5)&0x7ffffU)<<13)>>13)*4;ctx->uc_mcontext.pc=pc+(size?4:(uint64_t)taken);}
    if((ctx->uc_mcontext.regs[1]&255) || size<LC2_MEMORY_ZERO_MIN ||
       address>=AS_END || size>AS_END-address)return 1;
    if(pthread_mutex_trylock(&regions_mutex))return 1;
    if(!megaman_memory_scope_locked(kind,address,size)){
        pthread_mutex_unlock(&regions_mutex);return 1;
    }
    uint64_t page_begin=(address+PAGE-1)&~(PAGE-1),end=address+size,page_end=end&~(PAGE-1);
    if(page_end<=page_begin){pthread_mutex_unlock(&regions_mutex);return 1;}
    region_write_begin(pc);
    int rc=MEGAMAN_MEMORY_MADVISE((void*)(uintptr_t)page_begin,page_end-page_begin,MADV_DONTNEED);
    if(!rc){
        if(page_begin>address)memset((void*)(uintptr_t)address,0,page_begin-address);
        if(end>page_end)memset((void*)(uintptr_t)page_end,0,end-page_end);
    }
    region_write_end(pc);pthread_mutex_unlock(&regions_mutex);
    if(rc)return 1; /* Original native memset will complete even if advice failed. */
    megaman_memory_zero_calls++;megaman_memory_zero_bytes+=size;
    struct line log={0};text(&log,"LAZY_ZERO_MEMSET scope=");text(&log,kind==1?"private-heap":"exact-BSS");
    text(&log," address=");hex(&log,address);text(&log," bytes=");hex(&log,size);
    text(&log," discarded-full-pages-bytes=");hex(&log,page_end-page_begin);
    text(&log," caller=");hex(&log,ctx->uc_mcontext.regs[30]);emit(&log);
    ctx->uc_mcontext.pc=ctx->uc_mcontext.regs[30]; /* x0 remains original destination. */
    return 1;
}
#endif
