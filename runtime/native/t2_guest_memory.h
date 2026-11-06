/* Frente J (25/09/2026): host "zram" for cold guest heap blocks. Off by default; T2_GUEST_ZRAM=1 turns it on.
 *
 * Why: on the 916 MiB device the host dies at the MemAvailable floor while the Unity 6 / IL2CPP heap grows
 * (svcMapPhysicalMemory 16 MiB chunks, ~476 MiB resident after boot on the PC bench). MEDIDO on the PC
 * (docs/MEMORIA-CONVIDADO.md §1): 98-99 % of those resident bytes are not touched in 15 s of frames, and they
 * compress to ~0.35 with LZ4 in 64 KiB blocks. Kernel 3.14 has no zram and swap is on the SD card (house rule: no).
 *
 * How (only blocks fully inside an "alias_physical" region with perm RW and attr 0, never inside a live NVN pool
 * or a normal_guard range):
 *   compactor thread (only blocks with resident pages per mincore() whose resident count did not change for
 *   T2_GZRAM_COLD_MS): mprotect(PROT_NONE) -> read through /proc/self/mem (FOLL_FORCE, no fault) -> all-zero block:
 *     MADV_DONTNEED + mprotect(RW) (exact: private anonymous memory reads back zero), else LZ4 into a malloc'd
 *     buffer + MADV_DONTNEED; the block stays PROT_NONE.
 *   any access (guest code, host code, GL driver in user space) -> SIGSEGV -> t2_gzram_fault: decompress, write the
 *     non-zero 4 KiB pages through /proc/self/mem while the block is still PROT_NONE (so no other thread can observe a
 *     half-restored block), then mprotect(RW). The faulting instruction is retried.
 *   kernel accesses (pread/pwrite into guest buffers) would return EFAULT instead of faulting: those host call sites
 *     call t2_gzram_touch(range) first; protection changes (region_protect_locked) and svcUnmapPhysicalMemory call
 *     t2_gzram_touch / t2_gzram_forget.
 * Lock order: regions_mutex -> t2_gz.mutex. The fault path takes only t2_gz.mutex. Code holding t2_gz.mutex never
 * dereferences guest memory directly.
 *
 * Settings: T2_GZRAM_BLOCK_KIB (64; 4..256, power of two), T2_GZRAM_COLD_MS (1500: a block is compressed only after
 * it has been seen this long without a restore; each restore doubles it, up to x64), T2_GZRAM_PERIOD_MS (50),
 * T2_GZRAM_BUDGET_MIB (8 per period), T2_GZRAM_TARGET_MIB (0 = always compact cold blocks; n = only while
 * MemAvailable < n MiB), T2_GZRAM_MAX_PERMILLE (700: worse ratio = left alone), T2_GZRAM_STATS_MS (5000),
 * T2_GZRAM_POOLS (0: live NVN pools excluded).
 */
#ifndef T2_GUEST_MEMORY_H
#define T2_GUEST_MEMORY_H

/* ---- LZ4 block format (greedy, single hash), self-contained so the build needs no new library ---- */
#define T2LZ_HASH_BITS 13
static inline uint32_t t2lz_read32(const uint8_t *p){uint32_t v;memcpy(&v,p,4);return v;}
static inline unsigned t2lz_hash(uint32_t v){return (v*2654435761U)>>(32-T2LZ_HASH_BITS);}
static uint8_t *t2lz_length(uint8_t *op,const uint8_t *oend,size_t len) {
    while(len>=255){if(op>=oend)return NULL;*op++=255;len-=255;}
    if(op>=oend)return NULL;
    *op++=(uint8_t)len;
    return op;
}
/* Returns the compressed size, or 0 if it does not fit in cap. Offsets stay below 65536 (distance check). */
static size_t t2lz_compress(const uint8_t *src,size_t n,uint8_t *dst,size_t cap,uint32_t *table) {
    const uint8_t *ip=src,*anchor=src,*iend=src+n,*mflimit=n>12?iend-12:src,*matchlimit=n>5?iend-5:src;
    uint8_t *op=dst,*oend=dst+cap;
    memset(table,0,sizeof(uint32_t)<<T2LZ_HASH_BITS);
    if(n>=13) {
        ip++;
        while(ip<mflimit) {
            uint32_t seq=t2lz_read32(ip);unsigned h=t2lz_hash(seq);
            const uint8_t *ref=src+table[h];table[h]=(uint32_t)(ip-src);
            if(ip-ref>65535 || t2lz_read32(ref)!=seq){ip++;continue;}   /* table starts at 0: a stale 0 is just a miss */
            while(ip>anchor && ref>src && ip[-1]==ref[-1]){ip--;ref--;}
            const uint8_t *mp=ip+4,*mr=ref+4;
            while(mp<matchlimit && *mp==*mr){mp++;mr++;}
            size_t lit=(size_t)(ip-anchor),mlen=(size_t)(mp-ip)-4;
            if(op+1+lit+2+(lit/255)+(mlen/255)+2>oend)return 0;
            uint8_t *token=op++;
            *token=(uint8_t)((lit>=15?15:lit)<<4);
            if(lit>=15){op=t2lz_length(op,oend,lit-15);if(!op)return 0;}
            memcpy(op,anchor,lit);op+=lit;
            uint16_t off=(uint16_t)(ip-ref);memcpy(op,&off,2);op+=2;
            *token|=(uint8_t)(mlen>=15?15:mlen);
            if(mlen>=15){op=t2lz_length(op,oend,mlen-15);if(!op)return 0;}
            ip=mp;anchor=ip;
            if(ip>=mflimit)break;
            table[t2lz_hash(t2lz_read32(ip-2))]=(uint32_t)(ip-2-src);
        }
    }
    size_t lit=(size_t)(iend-anchor);
    if(op+1+lit+lit/255+1>oend)return 0;
    uint8_t *token=op++;*token=(uint8_t)((lit>=15?15:lit)<<4);
    if(lit>=15){op=t2lz_length(op,oend,lit-15);if(!op)return 0;}
    memcpy(op,anchor,lit);op+=lit;
    return (size_t)(op-dst);
}
/* Returns 1 only if exactly n bytes were produced without reading or writing out of bounds. */
static int t2lz_decompress(const uint8_t *src,size_t clen,uint8_t *dst,size_t n) {
    const uint8_t *ip=src,*iend=src+clen;uint8_t *op=dst,*oend=dst+n;
    while(ip<iend) {
        unsigned token=*ip++;size_t lit=token>>4;
        if(lit==15){unsigned b;do{if(ip>=iend)return 0;b=*ip++;lit+=b;}while(b==255);}
        if((size_t)(iend-ip)<lit || (size_t)(oend-op)<lit)return 0;
        memcpy(op,ip,lit);ip+=lit;op+=lit;
        if(ip==iend)break;
        if(iend-ip<2)return 0;
        uint16_t off;memcpy(&off,ip,2);ip+=2;
        if(!off || (size_t)(op-dst)<off)return 0;
        size_t mlen=(token&15);
        if(mlen==15){unsigned b;do{if(ip>=iend)return 0;b=*ip++;mlen+=b;}while(b==255);}
        mlen+=4;
        if((size_t)(oend-op)<mlen)return 0;
        const uint8_t *ref=op-off;
        for(size_t i=0;i<mlen;i++)op[i]=ref[i];   /* overlapping copy is the format's run-length case */
        op+=mlen;
    }
    return op==oend;
}

#ifndef T2_GZRAM_CODEC_ONLY   /* tools/mem/t2lz_test.c builds only the codec above */
/* ---- svcMapMemory stack aliases (memory.h stack_alias_locked), always on unless T2_STACK_SPARSE=0 ----
 * The destination is a fresh anonymous mapping (all zero), so copying only the non-zero 4 KiB pages of the source
 * is exact and leaves untouched stack pages non-resident (MEDIDO PC: 34 aliases = 27 MiB, 100 % resident with the
 * full memcpy). While aliased the source is inaccessible (PROT_NONE) and the unmap path overwrites it entirely from
 * the destination before it becomes accessible again, so its old pages can be dropped: exact as well. */
static int t2_stack_sparse_enabled(void) {
    static int state=-1;
    if(state<0){const char *o=getenv("T2_STACK_SPARSE");state=!(o && !strcmp(o,"0"));}
    return state;
}
static uint64_t t2_stack_alias_copied,t2_stack_alias_skipped,t2_stack_alias_count;
static void t2_stack_alias_copy(void *mapped,uint64_t source,uint64_t size) {
    if(!t2_stack_sparse_enabled()){memcpy(mapped,(void *)(uintptr_t)source,size);return;}
    static const uint64_t zero[PAGE/8];
    for(uint64_t off=0;off<size;off+=PAGE) {
        const void *from=(const void *)(uintptr_t)(source+off);
        if(memcmp(from,zero,PAGE)){memcpy((uint8_t *)mapped+off,from,PAGE);t2_stack_alias_copied+=PAGE;}
        else t2_stack_alias_skipped+=PAGE;
    }
    if(++t2_stack_alias_count<=4 || !(t2_stack_alias_count&(t2_stack_alias_count-1)))
        printf("T2_STACK_SPARSE aliases=%" PRIu64 " copied-KiB=%" PRIu64 " skipped-KiB=%" PRIu64 "\n",
               t2_stack_alias_count,t2_stack_alias_copied>>10,t2_stack_alias_skipped>>10);
}
static void t2_stack_alias_release_source(uint64_t source,uint64_t size) {
    if(t2_stack_sparse_enabled())madvise((void *)(uintptr_t)source,size,MADV_DONTNEED);
}
/* ---- block table ---- */
struct t2_gz_block {uint8_t *data;uint32_t clen,ready_ms;uint8_t state,restores,resident;};
enum {T2_GZ_NORMAL=0,T2_GZ_COMPRESSED=1};
static struct {
    unsigned enabled,shift,cold_ms,period_ms,max_permille,stats_ms,pools,trace_left;uint64_t block,nblocks,budget,target_kib;
    int memfd;struct t2_gz_block *blocks;pthread_mutex_t mutex;pthread_t worker;
    uint8_t *raw,*packed,*vec;uint32_t *table;void *deferred[1024];unsigned ndeferred;
    uint64_t live,stored,peak_live,compressions,faults,restores,zero_reclaimed,incompressible,protect_fail,read_fail,
             touched,forgotten,stale_retries,scans,growing;
    struct timespec start;
} t2_gz={.mutex=PTHREAD_MUTEX_INITIALIZER,.memfd=-1};
static _Thread_local uint64_t t2_gz_last_fault;
static _Thread_local unsigned t2_gz_repeat;

static uint32_t t2_gz_now_ms(void) {
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    return (uint32_t)((now.tv_sec-t2_gz.start.tv_sec)*1000+(now.tv_nsec-t2_gz.start.tv_nsec)/1000000);
}
static unsigned long t2_gz_setting(const char *name,unsigned long fallback,unsigned long low,unsigned long high) {
    const char *v=getenv(name);if(!v)return fallback;
    char *end=NULL;unsigned long n=strtoul(v,&end,10);
    if(!*v || *end || n<low || n>high)stop("T2_GZRAM_INVALID_SETTING",0,n,24);
    return n;
}
static int t2_gz_io(int write_mode,void *buffer,size_t size,uint64_t address) {
    size_t done=0;
    while(done<size) {
        ssize_t n=write_mode?pwrite(t2_gz.memfd,(uint8_t *)buffer+done,size-done,(off_t)(address+done)):
                             pread(t2_gz.memfd,(uint8_t *)buffer+done,size-done,(off_t)(address+done));
        if(n<0 && errno==EINTR)continue;
        if(n<=0)return 0;
        done+=(size_t)n;
    }
    return 1;
}
static int t2_gz_all_zero(const uint8_t *p,size_t n) {
    const uint64_t *w=(const uint64_t *)(const void *)p;
    for(size_t i=0;i<n/8;i++)if(w[i])return 0;
    return 1;
}
/* t2_gz.mutex held. Restores one compressed block; 1 on success. */
static int t2_gz_restore_locked(uint64_t index) {
    struct t2_gz_block *b=&t2_gz.blocks[index];
    if(b->state!=T2_GZ_COMPRESSED)return 1;
    uint64_t address=ALIAS_BASE+(index<<t2_gz.shift);
    if(!t2lz_decompress(b->data,b->clen,t2_gz.raw,t2_gz.block))stop("T2_GZRAM_CORRUPT",address,b->clen,24);
    /* The block was dropped with MADV_DONTNEED: zero pages are already correct and stay non-resident. */
    for(uint64_t off=0;off<t2_gz.block;off+=PAGE) {
        if(t2_gz_all_zero(t2_gz.raw+off,PAGE))continue;
        uint64_t end=off+PAGE;
        while(end<t2_gz.block && !t2_gz_all_zero(t2_gz.raw+end,PAGE))end+=PAGE;
        if(!t2_gz_io(1,t2_gz.raw+off,end-off,address+off))stop("T2_GZRAM_RESTORE_WRITE",address+off,(uint64_t)errno,21);
        off=end-PAGE;
    }
    if(mprotect((void *)(uintptr_t)address,t2_gz.block,PROT_READ|PROT_WRITE))stop("T2_GZRAM_RESTORE_PROTECT",address,(uint64_t)errno,21);
    if(t2_gz.ndeferred<sizeof(t2_gz.deferred)/sizeof(t2_gz.deferred[0]))t2_gz.deferred[t2_gz.ndeferred++]=b->data;
    else free(b->data);
    t2_gz.stored-=b->clen;t2_gz.live-=t2_gz.block;t2_gz.restores++;
    b->data=NULL;b->clen=0;b->state=T2_GZ_NORMAL;if(b->restores<255)b->restores++;
    unsigned shift=b->restores>6?6:b->restores;
    b->ready_ms=t2_gz_now_ms()+(t2_gz.cold_ms<<shift);
    return 1;
}
/* Called first thing for SIGSEGV. 1 = handled (retry the access), 0 = not ours. */
static int t2_gzram_fault(const siginfo_t *info,const void *opaque) {
    if(!t2_gz.enabled)return 0;
    uint64_t address=(uint64_t)(uintptr_t)info->si_addr;
    if(address<ALIAS_BASE || address>=ALIAS_BASE+ALIAS_SIZE)return 0;
    uint64_t index=(address-ALIAS_BASE)>>t2_gz.shift;
    pthread_mutex_lock(&t2_gz.mutex);
    int ours=t2_gz.blocks[index].state==T2_GZ_COMPRESSED;
    unsigned trace=0;
    if(ours){t2_gz.faults++;t2_gz_restore_locked(index);trace=t2_gz.trace_left?t2_gz.trace_left--:0;}
    pthread_mutex_unlock(&t2_gz.mutex);
    if(trace) {   /* T2_GZRAM_TRACE=<n>: who touches cold blocks (first n restores) */
        const ucontext_t *ctx=opaque;struct line b={0};
        text(&b,"T2_GZRAM_FAULT address=");hex(&b,address);text(&b," restores=");hex(&b,t2_gz.blocks[index].restores);
        text(&b," thread=");hex(&b,guest_thread_index);text(&b," pc=");location(&b,ctx->uc_mcontext.pc);
        text(&b," lr=");location(&b,ctx->uc_mcontext.regs[30]);emit(&b);
    }
    if(ours){t2_gz_repeat=0;return 1;}
    /* Another thread restored it between our fault and the lock: retry a few times, then let the normal path see it. */
    if(address==t2_gz_last_fault && ++t2_gz_repeat>3){t2_gz_repeat=0;return 0;}
    if(address!=t2_gz_last_fault){t2_gz_last_fault=address;t2_gz_repeat=1;}
    __atomic_add_fetch(&t2_gz.stale_retries,1,__ATOMIC_RELAXED);
    return 1;
}
/* Restore every compressed block overlapping [address,address+size) (before a kernel access or a protection change). */
static void t2_gzram_touch(uint64_t address,uint64_t size) {
    if(!t2_gz.enabled || !size || address>=ALIAS_BASE+ALIAS_SIZE || address+size<=ALIAS_BASE)return;
    uint64_t first=(address<ALIAS_BASE?0:address-ALIAS_BASE)>>t2_gz.shift;
    uint64_t end=address+size>ALIAS_BASE+ALIAS_SIZE?ALIAS_SIZE:address+size-ALIAS_BASE;
    uint64_t last=(end-1)>>t2_gz.shift;
    pthread_mutex_lock(&t2_gz.mutex);
    uint32_t hold=t2_gz_now_ms()+(t2_gz.cold_ms?t2_gz.cold_ms:1000);
    for(uint64_t i=first;i<=last;i++) {
        struct t2_gz_block *b=&t2_gz.blocks[i];
        if(b->state==T2_GZ_COMPRESSED){t2_gz_restore_locked(i);t2_gz.touched++;}
        if(!b->ready_ms || (int32_t)(hold-b->ready_ms)>0)b->ready_ms=hold;   /* no re-compression while the caller uses it */
    }
    pthread_mutex_unlock(&t2_gz.mutex);
}
/* Before svcUnmapPhysicalMemory: blocks fully inside the range are dropped, partial ones restored. */
static void t2_gzram_forget(uint64_t address,uint64_t size) {
    if(!t2_gz.enabled || !size)return;
    if(address<ALIAS_BASE || address+size>ALIAS_BASE+ALIAS_SIZE){t2_gzram_touch(address,size);return;}
    uint64_t first=(address-ALIAS_BASE)>>t2_gz.shift,last=(address+size-1-ALIAS_BASE)>>t2_gz.shift;
    pthread_mutex_lock(&t2_gz.mutex);
    for(uint64_t i=first;i<=last;i++) {
        struct t2_gz_block *b=&t2_gz.blocks[i];
        uint64_t base=ALIAS_BASE+(i<<t2_gz.shift);
        if(b->state!=T2_GZ_COMPRESSED){b->ready_ms=0;b->restores=0;continue;}
        if(base>=address && base+t2_gz.block<=address+size) {
            free(b->data);t2_gz.stored-=b->clen;t2_gz.live-=t2_gz.block;t2_gz.forgotten++;
            b->data=NULL;b->clen=0;b->state=T2_GZ_NORMAL;b->ready_ms=0;b->restores=0;
        } else t2_gz_restore_locked(i);
    }
    pthread_mutex_unlock(&t2_gz.mutex);
}
static int t2_gz_pool_overlap(uint64_t address,uint64_t size) {
    if(t2_gz.pools)return 0;
    for(unsigned i=0;i<256;i++) {
        const struct nvn_pool *p=&nvn_pools[i];
        if(p->live && p->size && address<p->address+p->size && p->address<address+size)return 1;
    }
    return 0;
}
/* regions_mutex and t2_gz.mutex held; the block is eligible. */
static void t2_gz_compact_locked(uint64_t index,uint32_t now) {
    struct t2_gz_block *b=&t2_gz.blocks[index];
    uint64_t address=ALIAS_BASE+(index<<t2_gz.shift),size=t2_gz.block;
    if(mprotect((void *)(uintptr_t)address,size,PROT_NONE)){t2_gz.protect_fail++;b->ready_ms=now+t2_gz.cold_ms*8;return;}
    if(!t2_gz_io(0,t2_gz.raw,size,address)) {
        t2_gz.read_fail++;
        if(mprotect((void *)(uintptr_t)address,size,PROT_READ|PROT_WRITE))stop("T2_GZRAM_UNDO_PROTECT",address,(uint64_t)errno,21);
        b->ready_ms=now+t2_gz.cold_ms*8;return;
    }
    if(t2_gz_all_zero(t2_gz.raw,size)) {
        madvise((void *)(uintptr_t)address,size,MADV_DONTNEED);
        if(mprotect((void *)(uintptr_t)address,size,PROT_READ|PROT_WRITE))stop("T2_GZRAM_UNDO_PROTECT",address,(uint64_t)errno,21);
        t2_gz.zero_reclaimed++;b->ready_ms=now+t2_gz.cold_ms*4;return;
    }
    size_t clen=t2lz_compress(t2_gz.raw,size,t2_gz.packed,size*t2_gz.max_permille/1000,t2_gz.table);
    uint8_t *data=clen?malloc(clen):NULL;
    if(!data) {
        if(mprotect((void *)(uintptr_t)address,size,PROT_READ|PROT_WRITE))stop("T2_GZRAM_UNDO_PROTECT",address,(uint64_t)errno,21);
        t2_gz.incompressible++;b->ready_ms=now+t2_gz.cold_ms*16;
        if(t2_gz.trace_left){t2_gz.trace_left--;printf("T2_GZRAM_INCOMPRESSIBLE address=0x%" PRIx64 " packed=%zu block=%" PRIu64 " first=%016" PRIx64 "\n",
            address,clen?clen:t2lz_compress(t2_gz.raw,size,t2_gz.packed,size,t2_gz.table),size,*(const uint64_t *)(const void *)t2_gz.raw);}
        return;
    }
    memcpy(data,t2_gz.packed,clen);
    madvise((void *)(uintptr_t)address,size,MADV_DONTNEED);
    b->data=data;b->clen=(uint32_t)clen;b->state=T2_GZ_COMPRESSED;
    t2_gz.stored+=clen;t2_gz.live+=size;t2_gz.compressions++;
    if(t2_gz.live>t2_gz.peak_live)t2_gz.peak_live=t2_gz.live;
}
static void t2_gz_stats(const char *why) {
    printf("T2_GZRAM %s compressed-MiB=%.1f stored-MiB=%.1f saved-MiB=%.1f peak-compressed-MiB=%.1f compressions=%" PRIu64
           " faults=%" PRIu64 " restores=%" PRIu64 " touched=%" PRIu64 " forgotten=%" PRIu64 " zero-reclaimed=%" PRIu64
           " incompressible=%" PRIu64 " protect-fail=%" PRIu64 " read-fail=%" PRIu64 " stale-retries=%" PRIu64
           " scans=%" PRIu64 " growing=%" PRIu64 " memavail-MiB=%lu\n",why,
           t2_gz.live/1048576.0,t2_gz.stored/1048576.0,(t2_gz.live-t2_gz.stored)/1048576.0,t2_gz.peak_live/1048576.0,
           t2_gz.compressions,t2_gz.faults,t2_gz.restores,t2_gz.touched,t2_gz.forgotten,t2_gz.zero_reclaimed,
           t2_gz.incompressible,t2_gz.protect_fail,t2_gz.read_fail,t2_gz.stale_retries,t2_gz.scans,t2_gz.growing,
           t2_mem_available_kib()/1024);
    fflush(stdout);
}
static void *t2_gz_thread(void *argument) {
    (void)argument;
    uint64_t cursor=ALIAS_BASE;uint32_t last_stats=0;
    for(;;) {
        struct timespec pause={t2_gz.period_ms/1000,(long)(t2_gz.period_ms%1000)*1000000L};nanosleep(&pause,NULL);
        uint32_t now=t2_gz_now_ms();
        if(now-last_stats>=t2_gz.stats_ms){last_stats=now;t2_gz_stats("periodic");}
        pthread_mutex_lock(&t2_gz.mutex);
        while(t2_gz.ndeferred)free(t2_gz.deferred[--t2_gz.ndeferred]);
        pthread_mutex_unlock(&t2_gz.mutex);
        if(t2_gz.target_kib && t2_mem_available_kib()>=t2_gz.target_kib)continue;
        t2_gz.scans++;
        uint64_t examined=0,steps=0,limit=ALIAS_SIZE>>t2_gz.shift;
        while(examined<t2_gz.budget && steps<limit) {
            steps++;
            if(cursor>=ALIAS_BASE+ALIAS_SIZE)cursor=ALIAS_BASE;
            uint64_t index=(cursor-ALIAS_BASE)>>t2_gz.shift,address=cursor;
            cursor+=t2_gz.block;
            if(t2_gz.blocks[index].state!=T2_GZ_NORMAL)continue;
            pthread_mutex_lock(&regions_mutex);
            struct region r=query(address);
            if(!r.state){  /* skip the hole in one step */
                uint64_t next=(r.base+r.size+t2_gz.block-1)&~(t2_gz.block-1);
                if(next>cursor)cursor=next;
                pthread_mutex_unlock(&regions_mutex);continue;
            }
            int eligible=r.name && !strcmp(r.name,"alias_physical") && r.perm==3 && !r.attr &&
                         address>=r.base && address+t2_gz.block<=r.base+r.size &&
                         !normal_guard_overlap(address,t2_gz.block) && !t2_gz_pool_overlap(address,t2_gz.block);
            if(eligible) {
                pthread_mutex_lock(&t2_gz.mutex);
                struct t2_gz_block *b=&t2_gz.blocks[index];
                now=t2_gz_now_ms();
                /* mincore: 0 resident pages = nothing to gain; a resident count that changed since the last sighting
                 * = the guest is still filling fresh pages here (boot allocator), so the clock restarts. */
                unsigned resident=0;
                if(b->state==T2_GZ_NORMAL && !mincore((void *)(uintptr_t)address,t2_gz.block,t2_gz.vec))
                    for(uint64_t k=0;k<t2_gz.block/PAGE;k++)resident+=t2_gz.vec[k]&1;
                else resident=255;
                if(resident>255)resident=255;
                if(b->state!=T2_GZ_NORMAL || !resident){}
                else if(!b->ready_ms || resident!=b->resident) {
                    if(b->ready_ms)t2_gz.growing++;
                    b->resident=(uint8_t)resident;
                    uint32_t wait=now+t2_gz.cold_ms;if(!b->ready_ms || (int32_t)(wait-b->ready_ms)>0)b->ready_ms=wait;
                } else if((int32_t)(now-b->ready_ms)>=0){t2_gz_compact_locked(index,now);b->resident=0;examined+=t2_gz.block;}
                pthread_mutex_unlock(&t2_gz.mutex);
            }
            pthread_mutex_unlock(&regions_mutex);
        }
    }
    return NULL;
}
static void t2_gzram_start(void) {
    const char *on=getenv("T2_GUEST_ZRAM");
    if(!on || strcmp(on,"1")){printf("T2_GZRAM off (T2_GUEST_ZRAM=1 enables)\n");return;}
    unsigned long kib=t2_gz_setting("T2_GZRAM_BLOCK_KIB",64,4,256);
    if(kib&(kib-1))stop("T2_GZRAM_INVALID_SETTING",0,kib,24);
    t2_gz.block=kib*1024;t2_gz.shift=(unsigned)__builtin_ctzll(t2_gz.block);
    t2_gz.cold_ms=(unsigned)t2_gz_setting("T2_GZRAM_COLD_MS",1500,0,600000);
    t2_gz.period_ms=(unsigned)t2_gz_setting("T2_GZRAM_PERIOD_MS",50,1,10000);
    t2_gz.budget=(uint64_t)t2_gz_setting("T2_GZRAM_BUDGET_MIB",8,1,4096)<<20;
    t2_gz.target_kib=(uint64_t)t2_gz_setting("T2_GZRAM_TARGET_MIB",0,0,65536)*1024;
    t2_gz.max_permille=(unsigned)t2_gz_setting("T2_GZRAM_MAX_PERMILLE",700,50,1000);
    t2_gz.stats_ms=(unsigned)t2_gz_setting("T2_GZRAM_STATS_MS",5000,100,3600000);
    t2_gz.pools=(unsigned)t2_gz_setting("T2_GZRAM_POOLS",0,0,1);
    t2_gz.trace_left=(unsigned)t2_gz_setting("T2_GZRAM_TRACE",0,0,1000000);
    t2_gz.nblocks=ALIAS_SIZE>>t2_gz.shift;
    t2_gz.blocks=calloc(t2_gz.nblocks,sizeof(*t2_gz.blocks));
    t2_gz.raw=malloc(t2_gz.block);t2_gz.vec=malloc(t2_gz.block/PAGE);t2_gz.packed=malloc(t2_gz.block);t2_gz.table=malloc(sizeof(uint32_t)<<T2LZ_HASH_BITS);
    if(!t2_gz.blocks || !t2_gz.raw || !t2_gz.vec || !t2_gz.packed || !t2_gz.table)stop("T2_GZRAM_ALLOCATION",0,t2_gz.nblocks,21);
    t2_gz.memfd=open("/proc/self/mem",O_RDWR|O_CLOEXEC);
    if(t2_gz.memfd<0)stop("T2_GZRAM_PROC_SELF_MEM",0,(uint64_t)errno,21);
    clock_gettime(CLOCK_MONOTONIC,&t2_gz.start);
    t2_gz.enabled=1;
    pthread_attr_t attr;
    if(pthread_attr_init(&attr) || pthread_attr_setstacksize(&attr,256*1024) ||
       pthread_create(&t2_gz.worker,&attr,t2_gz_thread,NULL))stop("T2_GZRAM_THREAD",0,0,21);
    pthread_attr_destroy(&attr);
    printf("T2_GZRAM on block-KiB=%lu cold-ms=%u period-ms=%u budget-MiB=%" PRIu64 " target-MiB=%" PRIu64
           " max-permille=%u pools=%s codec=lz4-block scope=alias_physical\n",kib,t2_gz.cold_ms,t2_gz.period_ms,
           t2_gz.budget>>20,t2_gz.target_kib/1024,t2_gz.max_permille,t2_gz.pools?"included":"excluded");
}
#endif /* T2_GZRAM_CODEC_ONLY */
#endif
