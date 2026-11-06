/* MRS guest TLS -> B; MRS host TP; LDR the current pthread's virtual TLS; B.
 * Only the original destination changes. In particular, no scratch register,
 * stack, flags, host TP write, or cached guest value is involved.
 * Installation is all-or-nothing while every image is still private RW.
 */
#define TLS_STUB_BYTES 16U
struct tls_stub_site {uint64_t pc,stub;uint32_t opcode;};
static struct tls_stub_site tls_stub_sites[MAX_TRAPS];
static size_t ntls_stubs;
static uint64_t tls_stub_low,tls_stub_high;
static uint64_t tls_stub_offsets[2];
static const char *tls_stub_fallback="not initialized";
static unsigned tls_stub_kind(uint32_t opcode) {
    uint32_t op=opcode&0xffffffe0U;
    return op==0xd53bd060U?1:op==0xd53bd040U?2:0;
}
static int tls_stub_offset_valid(uint64_t offset) {return !(offset&7) && offset<=32760;}
static void tls_stubs_initialize(void) {
    uintptr_t tp=(uintptr_t)__builtin_thread_pointer();
    tls_stub_offsets[0]=(uintptr_t)&guest_tls_base-tp;
    tls_stub_offsets[1]=(uintptr_t)&virtual_tpidr-tp;
}
static int tls_stub_branch(uint64_t pc,uint64_t destination,uint32_t *word) {
    if((pc|destination)&3)return 0;
    if(destination>=pc) {if(destination-pc>0x7fffffcULL)return 0;}
    else if(pc-destination>0x8000000ULL)return 0;
    *word=0x14000000U|((uint32_t)((destination-pc)>>2)&0x03ffffffU);return 1;
}
static int tls_stub_encode(uint32_t words[4],uint32_t *branch,uint64_t pc,uint64_t stub,
                           uint32_t opcode,uint64_t offset) {
    unsigned rt=opcode&31;
    uint32_t out[4],entry;
    if(!tls_stub_kind(opcode) || rt==31 || !tls_stub_offset_valid(offset) ||
       stub>UINT64_MAX-12 || pc>UINT64_MAX-4 ||
       !tls_stub_branch(pc,stub,&entry) || !tls_stub_branch(stub+8,pc+4,&out[2]))return 0;
    out[0]=0xd53bd040U|rt; /* TPIDR_EL0 remains the host pthread's real TP. */
    out[1]=0xf9400000U|((uint32_t)(offset/8)<<10)|(rt<<5)|rt;
    out[3]=0; /* Unreachable slot tail remains the anonymous mapping's zero. */
    memcpy(words,out,sizeof(out));*branch=entry;return 1;
}
static unsigned tls_stub_candidate(uint64_t pc,uint32_t opcode) {
    if(!tls_stub_kind(opcode) || (opcode&31)==31)return 0;
    for(size_t i=0;i<sizeof(graphics_entries)/sizeof(graphics_entries[0]);i++)
        if(pc==graphics_entries[i].pc)return 0;
    return 1;
}
/* MOMODORA: the sdk's own executable padding holds 0x5c0 bytes and this title needs 124 stubs
 * (1984 B), so the planner spends the padding of EVERY executable image, not only the segment that
 * owns the site. Nothing outside the images is mapped: adding a region of its own changed the guest
 * memory map and broke the rtld (MEDIDO run momo-073). Without the stubs every guest TLS read is a
 * SIGTRAP: 7.4 million of them in 90 s of loading (MEDIDO run momo-070). */
struct tls_stub_pool {uint64_t base,size,used;};
static struct tls_stub_pool tls_stub_pools[8];
static unsigned ntls_stub_pools;
static int tls_stubs_install(const struct image_segment *segments,size_t count) {
    size_t planned=0;
    if(ntls_stubs)stop("TLS_STUBS_ALREADY_INSTALLED",0,ntls_stubs,24);
    tls_stub_fallback="TLS offset cannot use LDR unsigned immediate";
    if(!tls_stub_offset_valid(tls_stub_offsets[0]) || !tls_stub_offset_valid(tls_stub_offsets[1]))return 0;
    for(size_t i=0;i<count;i++) {
        const struct image_segment *s=&segments[i];if(!(s->perm&PROT_EXEC))continue;
        tls_stub_fallback="invalid executable padding geometry";
        if(s->perm!=(PROT_READ|PROT_EXEC) || (s->base&(PAGE-1)) ||
           !s->file_size || (s->file_size&3) || s->file_size>UINT64_MAX-(PAGE-1) ||
           s->map_size!=((s->file_size+PAGE-1)&~(PAGE-1)) || s->base>UINT64_MAX-s->map_size)return 0;
        uint64_t tail=(s->file_size+15)&~15ULL;
        const uint8_t *base=(const uint8_t *)(uintptr_t)s->base;
        tls_stub_fallback="executable padding is not zero";
        for(uint64_t off=s->file_size;off<s->map_size;off++)if(base[off])return 0;
        if(tail<s->map_size && ntls_stub_pools<8)
            tls_stub_pools[ntls_stub_pools++]=(struct tls_stub_pool){s->base+tail,s->map_size-tail,0};
        for(uint64_t off=0;off<s->file_size;off+=4) {
            uint32_t opcode;memcpy(&opcode,base+off,4);
            if(!tls_stub_candidate(s->base+off,opcode))continue;
            tls_stub_fallback="executable padding or metadata capacity";
            if(planned==MAX_TRAPS)return 0;
            uint64_t stub=0;
            for(unsigned pool=0;pool<ntls_stub_pools;pool++) {
                struct tls_stub_pool *slot=&tls_stub_pools[pool];
                if(TLS_STUB_BYTES>slot->size-slot->used)continue;
                uint64_t candidate=slot->base+slot->used;uint32_t probe[4],entry;
                if(!tls_stub_encode(probe,&entry,s->base+off,candidate,opcode,
                                    tls_stub_offsets[tls_stub_kind(opcode)-1]))continue;
                stub=candidate;slot->used+=TLS_STUB_BYTES;break;
            }
            if(!stub)return 0;
            uint32_t words[4],branch;
            tls_stub_fallback="TLS stub branch range";
            if(!tls_stub_encode(words,&branch,s->base+off,stub,opcode,tls_stub_offsets[tls_stub_kind(opcode)-1]))return 0;
            tls_stub_sites[planned++]=(struct tls_stub_site){s->base+off,stub,opcode};
        }
    }
    /* No image byte was changed on any rejected plan. All remaining operations
     * are infallible stores into mappings created RW before any guest enters. */
    for(size_t i=0;i<planned;i++) {
        const struct tls_stub_site *s=&tls_stub_sites[i];uint32_t words[4],branch;
        if(!tls_stub_encode(words,&branch,s->pc,s->stub,s->opcode,tls_stub_offsets[tls_stub_kind(s->opcode)-1]))
            stop("TLS_STUB_PLAN_CHANGED",s->pc,s->stub,24);
        memcpy((void *)(uintptr_t)s->stub,words,sizeof(words));
        memcpy((void *)(uintptr_t)s->pc,&branch,4);
    }
    tls_stub_low=UINT64_MAX;tls_stub_high=0;
    for(size_t i=0;i<planned;i++) {
        if(tls_stub_sites[i].stub<tls_stub_low)tls_stub_low=tls_stub_sites[i].stub;
        if(tls_stub_sites[i].stub+TLS_STUB_BYTES>tls_stub_high)tls_stub_high=tls_stub_sites[i].stub+TLS_STUB_BYTES;
    }
    ntls_stubs=planned;tls_stub_fallback=NULL;return 1;
}
/* Main-executable static TLS has a constant TP displacement across pthreads.
 * Validate that contract before every guest entry. A mismatch after patching
 * cannot fall back per thread: the executable sites are process-wide. */
static void tls_stubs_validate_thread(uint64_t pc) {
    if(!ntls_stubs)return;
    uintptr_t tp=(uintptr_t)__builtin_thread_pointer();
    if((uintptr_t)&guest_tls_base-tp!=tls_stub_offsets[0] ||
       (uintptr_t)&virtual_tpidr-tp!=tls_stub_offsets[1])stop("TLS_STUB_THREAD_LAYOUT",pc,guest_thread_index,24);
}
static unsigned tls_stub_location(struct line *b,uint64_t pc) {
    if(!ntls_stubs || pc<tls_stub_low || pc>=tls_stub_high)return 0;
    for(size_t i=0;i<ntls_stubs;i++) {
        const struct tls_stub_site *s=&tls_stub_sites[i];
        if(pc>=s->stub && pc-s->stub<TLS_STUB_BYTES) {
            text(b,"TLS-stub@");hex(b,pc);text(b," guest=");location(b,s->pc);
            text(b," phase=");hex(b,pc-s->stub);return 1;
        }
    }
    return 0;
}
