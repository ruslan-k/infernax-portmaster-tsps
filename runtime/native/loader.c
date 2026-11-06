#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <ucontext.h>
#include <time.h>
#include <pthread.h>
#include <dlfcn.h>
#include <setjmp.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "cond_clockwait_compat.h"

/* Local diagnostic experiment: no guest ABI forwarding to Linux syscalls. */
#define AS_END (1ULL << 39)
#define PAGE 4096ULL
#define MAX_REGIONS 1024   /* one region per aliased thread stack plus the alias-region pieces (MEDIDO run momo-074) */
#define MAX_TRAPS 8192
#define TRAP_OPCODE 0xd421a000U
#define MEMORY_BUDGET (1536ULL << 20)   /* Unity takes the whole declared budget (docs/RUNTIME.md 3.2); 1536 MiB is the Messenger value */
#define HOST_ADDRESS_LIMIT (8192ULL << 20)
#define HEAP_BASE 0x1000000000ULL
#define HEAP_LIMIT MEMORY_BUDGET
#include "t2_alias_region.h"
#include "t2_video.h"
#include "t2_runtime_health.h"
static uint64_t t2_alias_base=T2_ALIAS_BASE_FBDEV;
#define ALIAS_BASE t2_alias_base   /* same runtime base reported by svcGetInfo 2 */
#define ALIAS_SIZE T2_ALIAS_REGION_SIZE
struct image_segment { const char *name; uint64_t base, file_size, map_size; unsigned perm, state; };
#include "image_table.h"
#include "t2_image_heap_guard.h"
struct region { uint64_t base, size; unsigned perm, state; const char *name; unsigned attr; };
struct bridge { uint64_t pc; uint32_t opcode; };
static struct region regions[MAX_REGIONS];
static size_t nregions, nbridges;
static struct bridge bridges[MAX_TRAPS];
#include "trap_lookup.h"
static uint64_t svc_counts[256], entropy[4];
static _Thread_local uint64_t virtual_tpidr,guest_tls_base=0x70000000ULL;
static _Thread_local unsigned guest_thread_index;
static pthread_mutex_t kernel_mutex=PTHREAD_MUTEX_INITIALIZER;
/* Region readers in the graphics bridge do not share the audio/kernel lock.
 * Runtime mapping writers already own kernel and additionally own regions for
 * their whole VM transaction. Never acquire kernel while owning regions. */
static pthread_mutex_t regions_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t kernel_changed=PTHREAD_COND_INITIALIZER;
static unsigned tls_reads;
static uint64_t main_thread_id;
static uint64_t heap_size;
static uint64_t alias_mapped;
/* ROOT: the last guest maps of 64 MiB + 4 KiB = Unity 6's decoded-PNG buffers (t2_page_record_copy). */
static uint64_t t2_decode_maps[4];static unsigned t2_decode_next;
static unsigned momo_trace_waits;
static uint64_t momo_dump_address,momo_dump_size;
static _Thread_local ucontext_t *current_guest_context;
static _Thread_local volatile sig_atomic_t signal_handler_depth;
static void trace_frames(ucontext_t *ctx);
extern void enter_guest(uint64_t entry, uint64_t stack, uint64_t handle) __attribute__((noreturn));
extern void enter_thread(uint64_t entry,uint64_t stack,uint64_t argument,uint64_t handle) __attribute__((noreturn));

struct line { char data[2048]; size_t used; };
static void text(struct line *b, const char *s) {
    while (*s && b->used < sizeof(b->data)-1) b->data[b->used++] = *s++;
}
static void hex(struct line *b, uint64_t x) {
    char tmp[16]; unsigned n=0;
    do { tmp[n++]="0123456789abcdef"[x&15]; x>>=4; } while (x);
    text(b,"0x"); while (n && b->used<sizeof(b->data)-1) b->data[b->used++]=tmp[--n];
}
static void emit(struct line *b) { text(b,"\n"); (void)write(STDOUT_FILENO,b->data,b->used); }
static int host_affinity_log(pthread_t thread,const char *role) {
    cpu_set_t set;int rc=pthread_getaffinity_np(thread,sizeof(set),&set);
    if(rc){fprintf(stderr,"affinity query for %s: %s\n",role,strerror(rc));return rc;}
    struct line b={0};text(&b,"HOST_CPU_AFFINITY role=");text(&b,role);text(&b," cpus=");
    unsigned count=0;
    for(unsigned cpu=0;cpu<CPU_SETSIZE;cpu++)if(CPU_ISSET(cpu,&set)) {
        char number[16];snprintf(number,sizeof(number),"%u",cpu);
        if(count++)text(&b,",");
        text(&b,number);
    }
    emit(&b);return 0;
}
/* Bounded diagnostics for the observed background poll loop. Service calls
 * still execute every time; first occurrences and powers of two stay visible. */
static _Thread_local uint64_t svc_trace_counts[256];
#include "zero_hot_logs.h"
static void emit_svc(struct line *line,unsigned svc) {
    if(zero_hot_log_enabled && (svc==0x34 || svc==0x35)) {
        static _Thread_local struct zero_hot_log_cache address_sync_logs[2];
        struct zero_hot_log_event event=zero_hot_log_sample(&address_sync_logs[svc-0x34],line->data,line->used,0);
        if(!event.emit)return;
        text(line," occurrence=");hex(line,event.occurrence);text(line," suppressed=");hex(line,event.suppressed);
        text(line," log-scope=thread-exact-args changed=");hex(line,event.changed);
    }
    if(svc<256 &&
        (svc==0x21 || svc==0x1d || svc==0x17 || svc==0x18 || svc==0x12 || svc==0xb || svc==0x1c)) {
        uint64_t count=++svc_trace_counts[svc];
        if(count>16 && (count&(count-1)))return;
        text(line," occurrence=");hex(line,count);
    }
    emit(line);
}
static unsigned tls_stub_location(struct line *b,uint64_t pc);
static void location(struct line *b,uint64_t pc) {
    if(tls_stub_location(b,pc))return;
    for (size_t i=0;i<sizeof(images)/sizeof(images[0]);i++) {
        if (pc>=images[i].base && pc<images[i].base+images[i].map_size) {
            text(b,images[i].name);text(b,"+");hex(b,pc-images[i].base);return;
        }
    }
    hex(b,pc);
}
static void momo_thread_census(void);
static void stop(const char *why, uint64_t pc, uint64_t detail, int status) {
    struct line b={0};text(&b,"STOP status=");text(&b,why);text(&b," pc=");location(&b,pc);
    text(&b," detail=");hex(&b,detail);text(&b," query_memory_calls=");hex(&b,svc_counts[6]);
    text(&b," tls_trap_reads=");hex(&b,tls_reads);emit(&b);
    /* Emit the primary fail-closed reason before optional stack diagnostics. */
    momo_thread_census();
    if(current_guest_context)trace_frames(current_guest_context);
    /* Only async-signal-safe syscalls: this path also runs after guest faults. */
    int memory_fd=open("/proc/self/status",O_RDONLY|O_CLOEXEC);
    if(memory_fd>=0) {
        char memory_status[4096];ssize_t length=read(memory_fd,memory_status,sizeof(memory_status));close(memory_fd);
        if(length>0)for(ssize_t begin=0;begin<length;) {
            ssize_t end=begin;while(end<length && memory_status[end]!='\n')end++;
            if(end-begin>=2 && memory_status[begin]=='V' && memory_status[begin+1]=='m')
                (void)write(STDOUT_FILENO,memory_status+begin,end-begin+(end<length));
            begin=end+1;
        }
    }
    _exit(status);
}
#include "guest_profile.h"
#include "region_generation.h"
static struct region query(uint64_t address) {
    if(address>=AS_END) return (struct region){AS_END,0ULL-AS_END,0,16,"outside",0};
    uint64_t before=0;
    for(size_t i=0;i<nregions;i++) {
        struct region r=regions[i];
        if(address>=r.base && address<r.base+r.size) return r;
        if(address<r.base) return (struct region){before,r.base-before,0,0,"unmapped",0};
        before=r.base+r.size;
    }
    return (struct region){before,AS_END-before,0,0,"unmapped",0};
}
#include "normal_guard.h"
static int accessible(uint64_t address,uint64_t size,unsigned perm) {
    if(address>=AS_END || size>AS_END-address) return 0;
    if(normal_guard_overlap(address,size))return 0;
    while(size) {
        struct region r=query(address);
        if((r.perm&perm)!=perm) return 0;
        uint64_t count=r.base+r.size-address;if(count>size)count=size;
        address+=count;size-=count;
    }
    return 1;
}
static uint64_t counter_ticks(void) {
    struct timespec now;
    if(clock_gettime(CLOCK_MONOTONIC,&now))stop("CLOCK_UNAVAILABLE",0,errno,21);
    return (uint64_t)now.tv_sec*19200000ULL+(uint64_t)now.tv_nsec*192ULL/10000ULL;
}
static void trace_frames(ucontext_t *ctx) {
    struct line b={0};text(&b,"GUEST_FRAMES lr=");location(&b,ctx->uc_mcontext.regs[30]);
    uint64_t fp=ctx->uc_mcontext.regs[29];
    for(unsigned i=0;i<16 && fp && !(fp&15) && accessible(fp,16,PROT_READ);i++) {
        uint64_t pair[2];memcpy(pair,(void *)(uintptr_t)fp,16);
        text(&b," <- ");location(&b,pair[1]);if(pair[0]<=fp)break;fp=pair[0];
    }
    emit(&b);
}
static void t2_gzram_touch(uint64_t,uint64_t); /* Frente J: guest zram, t2_guest_memory.h */
static void t2_stack_alias_copy(void *,uint64_t,uint64_t);static void t2_stack_alias_release_source(uint64_t,uint64_t); /* Frente J */
#include "memory.h"
#include "t2_memwatch.h"
#include "normal_cpu.h"
#include "ipc.h"
#include "nvn_bridge.h"
#include "t2_guest_memory.h" /* Frente J */
#include "t2_lazy_mesh.h" /* Frente L */
#include "graphics_trace.h"
#include "megaman_movie.h"
#include "megaman_memory.h"
#include "megaman_heap_mapping.h"
#include "tls_stubs.h"
#include "aw_profiler.h"
#include "threading.h"
/* Stall census: what every live guest thread was doing at its last system call. Printed by stop(),
 * so a time limit or a fault always says who is waiting on what (MEDIDO need, run momo-076). */
static void momo_thread_census(void) {
    static unsigned printed;
    if(printed++)return;
    for(unsigned i=0;i<nthreads && i<MAX_THREADS;i++) {
        const struct guest_thread *t=&threads[i];
        if(!t->state)continue;
        struct line b={0};text(&b,"MOMO_THREAD index=");hex(&b,i);text(&b," handle=");hex(&b,t->handle);
        text(&b," state=");hex(&b,(uint64_t)t->state);text(&b," core=");hex(&b,(uint64_t)t->core);
        text(&b," last-svc=");hex(&b,t->last_svc_id);text(&b," at=");location(&b,t->last_svc_pc);
        text(&b," waiting-key=");hex(&b,t->waiting_key);text(&b," waiting-mutex=");hex(&b,t->waiting_mutex);
        text(&b," entry=");location(&b,t->entry);emit(&b);
        /* A blocked thread is inside its system call, so its kernel frame is stable and says who it
         * is waiting for. This is the whole point of the census. */
        if(t->kernel_context)trace_frames(t->kernel_context);
    }
}
#include "zero_address_arbiter.h"
/* SDK 4.5 closes a CMIF session with an empty HIPC type-2 message.
 * The kernel handle stays valid until CloseHandle. Future sends report the
 * closed peer; aliases share that state. libnx cmifMakeCloseRequest contract. */
static uint32_t ipc_send(uint64_t handle,uint64_t pc) {
    struct session *s=session_get(handle,pc);
    if(s->peer_closed)return 0xf601;
    uint8_t *buffer=(uint8_t *)(uintptr_t)guest_tls_base;
    if(!accessible((uintptr_t)buffer,IPC_BYTES,3))stop("INVALID_IPC_TLS",pc,0,24);
    uint32_t h0=read32(buffer),h1=read32(buffer+4);
    if((h0&65535)==2) {
        if(h0!=2 || h1)stop("INVALID_SESSION_CLOSE_MESSAGE",pc,h0,24);
        s->peer_closed=1;
        struct line b={0};text(&b,"IPC_SESSION_PEER_CLOSED handle=");hex(&b,handle);emit(&b);
        return 0xf601;
    }
    ipc_dispatch(handle,pc);return 0;
}
static unsigned local_register_trap(ucontext_t *ctx,uint32_t instruction) {
    if((instruction&0xffffffe0U)==0xd53be020U || (instruction&0xffffffe0U)==0xd53be040U ||
       (instruction&0xffffffe0U)==0xd53be000U) {
        unsigned rt=instruction&31;
        guest_profile_register((instruction&0xffffffe0U)==0xd53be000U?1:0);
        uint64_t value=(instruction&0xffffffe0U)==0xd53be000U?19200000ULL:counter_ticks();
        if(rt<31)ctx->uc_mcontext.regs[rt]=value;
        ctx->uc_mcontext.pc+=4;return 1;
    }
    if((instruction&0xffffffe0U)==0xd53bd060U || (instruction&0xffffffe0U)==0xd53bd040U) {
        unsigned rt=instruction&31;
        guest_profile_register(2);
        uint64_t value=(instruction&0xffffffe0U)==0xd53bd060U?guest_tls_base:virtual_tpidr;
        if(rt<31)ctx->uc_mcontext.regs[rt]=value;
        __atomic_add_fetch(&tls_reads,1,__ATOMIC_RELAXED);ctx->uc_mcontext.pc+=4;return 1;
    }
    if((instruction&0xffffffe0U)==0xd51bd040U) {
        guest_profile_register(3);
        unsigned rt=instruction&31;virtual_tpidr=rt<31?ctx->uc_mcontext.regs[rt]:0;
        ctx->uc_mcontext.pc+=4;return 1;
    }
    return 0;
}
static void signal_dispatch(int sig,siginfo_t *info,void *opaque) {
    ucontext_t *ctx=opaque;current_guest_context=ctx;uint64_t pc=ctx->uc_mcontext.pc;
    if(sig!=SIGTRAP) {
        struct line fault={0};text(&fault,"FAULT signal=");hex(&fault,sig);text(&fault," code=");hex(&fault,info->si_code);
        text(&fault," lr=");location(&fault,ctx->uc_mcontext.regs[30]);text(&fault," sp=");hex(&fault,ctx->uc_mcontext.sp);emit(&fault);
        for(unsigned i=0;i<31;i++){struct line r={0};text(&r,"FAULT_REG x");hex(&r,i);text(&r,"=");hex(&r,ctx->uc_mcontext.regs[i]);text(&r," ");location(&r,ctx->uc_mcontext.regs[i]);emit(&r);}
#ifdef AW_NO_ROMFS
        if(pc==LC2_SDK_BASE+0x1dfe90) {
            /* nn::audio::VoiceInfoManager::UpdateVoiceRelatedParameters list renumber: walk the list from x21+0x18 and dump
             * the node whose next pointer is bad, plus the neighbours, to identify the writer. */
            aw_voice_compare(ctx->uc_mcontext.regs[21]+0x10);
            uint64_t head=ctx->uc_mcontext.regs[21]+0x10,node=0;memcpy(&node,(void *)(uintptr_t)(ctx->uc_mcontext.regs[21]+0x18),8);
            uint64_t prev=head,pprev=head;
            for(unsigned n=0;n<400&&node!=head;n++) {
                if(!accessible(node,0x60,PROT_READ)){uint64_t base=0;memcpy(&base,(void *)(uintptr_t)(ctx->uc_mcontext.regs[21]+8),8);
                    {for(int nb=-1;nb<=1;nb++){uint64_t cand=pprev+(uint64_t)(int64_t)(nb*0x210);if(!accessible(cand,0x1c0,PROT_READ))continue;uint32_t cid=0,crel=0,ccon=0;int32_t ccount=0;int16_t chead=0;uint64_t cnext=0;memcpy(&cid,(void *)(uintptr_t)(cand+64),4);memcpy(&crel,(void *)(uintptr_t)(cand+48),4);memcpy(&ccon,(void *)(uintptr_t)(cand+440),4);memcpy(&ccount,(void *)(uintptr_t)(cand+124),4);memcpy(&chead,(void *)(uintptr_t)(cand+128),2);memcpy(&cnext,(void *)(uintptr_t)(cand+8),8);
                     struct line c={0};text(&c,"AW_AUDIO_NEIGHBOUR node=");hex(&c,cand);text(&c," id=");hex(&c,cid);text(&c," released=");hex(&c,crel);text(&c," consumed=");hex(&c,ccon);text(&c," count=");hex(&c,(uint32_t)ccount);text(&c," head=");hex(&c,(uint16_t)chead);text(&c," next=");hex(&c,cnext);emit(&c);if(cid<512)aw_voice_dump(cid);}
                     uint64_t bad=pprev;uint32_t id=0,released=0,consumed=0;int32_t count=0;int16_t wh=0;(void)base;memcpy(&id,(void *)(uintptr_t)(bad+64),4);memcpy(&released,(void *)(uintptr_t)(bad+48),4);memcpy(&consumed,(void *)(uintptr_t)(bad+440),4);memcpy(&count,(void *)(uintptr_t)(bad+124),4);memcpy(&wh,(void *)(uintptr_t)(bad+128),2);uint64_t vi=id;
                     {struct line c={0};text(&c,"AW_AUDIO_BAD_CLIENT node=");hex(&c,bad);text(&c," id=");hex(&c,id);text(&c," released=");hex(&c,released);text(&c," consumed=");hex(&c,consumed);text(&c," count=");hex(&c,(uint32_t)count);text(&c," head=");hex(&c,(uint16_t)wh);emit(&c);}struct line q={0};text(&q,"AW_AUDIO_BAD_VOICE index=");hex(&q,vi);text(&q," base=");hex(&q,base);emit(&q);aw_voice_dump((unsigned)vi);}
                    struct line b={0};text(&b,"AW_AUDIO_LIST_BAD index=");hex(&b,n);text(&b," node=");hex(&b,node);text(&b," prev=");location(&b,prev);emit(&b);
                    for(uint64_t at=(prev&~7ULL)-0x80;at<(prev&~7ULL)+0xc0;at+=8){uint64_t v=0;if(!accessible(at,8,PROT_READ))continue;memcpy(&v,(void *)(uintptr_t)at,8);struct line w={0};text(&w,"AW_AUDIO_NODE ");location(&w,at);text(&w,"=");hex(&w,v);emit(&w);}
                    break;}
                {struct line b={0};text(&b,"AW_AUDIO_LIST_NODE i=");hex(&b,n);text(&b," at=");location(&b,node);uint64_t nx=0;memcpy(&nx,(void *)(uintptr_t)(node+8),8);text(&b," next=");location(&b,nx);
                 uint32_t idx=0;memcpy(&idx,(void *)(uintptr_t)(node+84),4);text(&b," idx=");hex(&b,idx);emit(&b);}
                if(n>=0x55){for(uint64_t at=node-0x10;at<node+0x78;at+=8){uint64_t v=0;memcpy(&v,(void *)(uintptr_t)at,8);struct line w={0};text(&w,"AW_AUDIO_GOOD ");location(&w,at);text(&w,"=");hex(&w,v);emit(&w);}}
                pprev=prev;prev=node;uint64_t next=0;memcpy(&next,(void *)(uintptr_t)(node+8),8);node=next;
            }
        }
#endif
        for(unsigned i=0;i<24;i++){uint64_t at=ctx->uc_mcontext.sp+i*8,v=0;if(!accessible(at,8,PROT_READ))break;memcpy(&v,(void *)(uintptr_t)at,8);struct line w={0};text(&w,"FAULT_STACK +");hex(&w,i*8);text(&w,"=");hex(&w,v);text(&w," ");location(&w,v);emit(&w);}
        if(momo_dump_address) {
            /* Bounded stall diagnostic: hexdump of a guest range chosen by MOMO_DUMP=address:bytes. */
            const unsigned char *at=(const unsigned char *)(uintptr_t)momo_dump_address;
            for(uint64_t off=0;off<momo_dump_size;off+=16) {
                struct line d={0};text(&d,"MOMO_DUMP ");hex(&d,momo_dump_address+off);text(&d,":");
                for(unsigned i=0;i<16 && off+i<momo_dump_size;i++){text(&d," ");hex(&d,at[off+i]);}
                emit(&d);
            }
        }
        stop(sig==SIGALRM?"TIME_LIMIT":sig==SIGSEGV&&normal_guard_overlap((uintptr_t)info->si_addr,1)?"NVN_NORMAL_CPU_ACCESS":"CPU_FAULT",pc,(uintptr_t)info->si_addr,22);
    }
    uint32_t instruction=bridge_instruction(pc);
    if(!instruction || *(const uint32_t *)(uintptr_t)pc!=TRAP_OPCODE)stop("UNKNOWN_TRAP",pc,0,23);
    if(graphics_trace_entry(pc,instruction,ctx))return;
    if(megaman_movie_handle(pc,instruction,ctx))return;
    if(megaman_memory_handle(pc,instruction,ctx))return;
    if(local_register_trap(ctx,instruction))return;
    if((instruction&0xffe0001fU)!=0xd4000001U)stop("BAD_BRIDGE",pc,instruction,23);
    unsigned svc=(instruction>>5)&0xffff;__typeof__(ctx->uc_mcontext.regs[0]) *r=ctx->uc_mcontext.regs;
    if(svc<256)svc_counts[svc]++;
    if(guest_thread_index<MAX_THREADS){threads[guest_thread_index].last_svc_pc=pc;threads[guest_thread_index].last_svc_id=svc;}
    struct line b={0};text(&b,"SVC id=");hex(&b,svc);text(&b," thread=");hex(&b,guest_thread_index);text(&b," pc=");location(&b,pc);
    for(unsigned i=0;i<(svc==8?6U:4U);i++){text(&b," arg");hex(&b,i);text(&b,"=");hex(&b,r[i]);}
    if(svc==6) {
        struct region out=query(r[2]);
        if(!accessible(r[0],40,PROT_WRITE))stop("INVALID_OUTPUT_POINTER",pc,r[0],24);
        struct meminfo {uint64_t base,size;uint32_t state,attr,perm,ipc,device,pad;};
        struct meminfo value={out.base,out.size,out.state,out.attr,out.perm,0,0,0};
        memcpy((void *)(uintptr_t)r[0],&value,sizeof(value));
        text(&b," region=");text(&b,out.name);text(&b," base=");hex(&b,out.base);text(&b," size=");hex(&b,out.size);
        if(svc_counts[svc]<=40)emit_svc(&b,svc);
        r[0]=0;r[1]=0;ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==0x27) {
        if(r[1]>4096 || !accessible(r[0],r[1],PROT_READ))stop("INVALID_DEBUG_STRING",pc,r[0],24);
        emit_svc(&b,svc);(void)write(STDOUT_FILENO,(void *)(uintptr_t)r[0],r[1]);(void)write(STDOUT_FILENO,"\n",1);
        r[0]=0;ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==0x29) {
        uint64_t type=r[1],handle=r[2],sub=r[3],value=0;int supported=1;
        if(type==11 && handle==0 && sub<4) {
            value=entropy[sub];
        } else if(type==8 && handle==0 && sub==0) {
            value=0; /* No Horizon debugger is attached. */
        } else if(handle==0xffff8001U && sub==0) {
            switch(type) {
            case 0:value=LC2_ALLOWED_CORE_MASK;break;
            case 1:value=LC2_ALLOWED_PRIORITY_MASK;break;
            case 2:value=ALIAS_BASE;break; /* guest alias virtual region */
            case 3:value=ALIAS_SIZE;break;
            case 4:value=HEAP_BASE;break;
            case 5:value=HEAP_LIMIT;break;
            case 6:
            case 21:value=MEMORY_BUDGET;break; /* explicit laboratory process quota */
            case 7:
            case 22:
                value=LC2_MAIN_STACK_SIZE+heap_size; /* logical guest allocations, not host RSS */
                for(size_t i=0;i<sizeof(images)/sizeof(images[0]);i++)value+=images[i].map_size;
                break;
            case 12:value=0x08000000;break;
            case 13:value=AS_END-0x08000000;break;
            case 14:value=0x60000000;break;
            case 15:value=0x10000000;break;
            /* MOMODORA (MEDIDO, run momo-002): Unity 2021 calls nn::os::AllocateAddressRegion
             * unconditionally, so VammManager::InitializeIfEnabled must enable itself. It enables on
             * InfoType 16 (SystemResourceSizeTotal) != 0; the NPDM of this title declares 16 MiB. */
            case 16:value=LC2_SYSTEM_RESOURCE_SIZE;break;
            case 17:value=0;break; /* SystemResourceSizeUsed: the host keeps its own page tables; none of the pool is spent. */
            case 28:value=0;break; /* AliasRegionExtraSize (kernel 18.0.0, asked by SDK 18.3.2): NPDM flag bit 6 clear. */
            case 18:value=LC2_PROGRAM_ID;break;
            /* TERMINATOR (MEDIDO, run pc-002): Unity 6 calls nn::os::GetFreeThreadCount at boot ->
             * InfoType 24 (FreeThreadCount, kernel 6.0.0+): threads still creatable under the limit. */
            case 24:value=nthreads<MAX_THREADS?(uint64_t)(MAX_THREADS-nthreads):0;break;
            default:supported=0;break;
            }
        } else supported=0;
        if(!supported){emit_svc(&b,svc);stop("UNIMPLEMENTED_GETINFO",pc,type,20);}
        /* The entropy result is never printed. */
        emit_svc(&b,svc);r[0]=0;r[1]=value;ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==0x0c || svc==0x0e || svc==0x25) {
        uint64_t handle=svc==0x0e?r[2]:r[1];
        struct guest_thread *t=thread_get(handle,pc);
        emit_svc(&b,svc);r[0]=0;
        if(svc==0x0c)r[1]=t->priority;
        else if(svc==0x0e){r[1]=t->core;r[2]=t->affinity;}
        else r[1]=t==threads?main_thread_id:t->id;
        ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==2) {
        /* SetMemoryPermission: nn::ro::ProtectRelro on the sdk data image (SDK 17). Only R/RW/none on an
         * existing private image or heap range; the split region keeps its state and attributes. */
        trace_frames(ctx);
        uint64_t address=r[0],size=r[1];unsigned perm=(unsigned)r[2];
        if((address|size)&(PAGE-1) || !size || perm>3 || perm==2)stop("INVALID_MEMORY_PERMISSION_REQUEST",pc,address,24);
        struct region old=query(address);
        if(!old.state || old.state==16 || size>old.base+old.size-address || old.perm&PROT_EXEC)stop("MEMORY_PERMISSION_RANGE",pc,address,20);
        region_protect(address,size,perm,old.attr,pc);
        struct line b={0};text(&b,"SET_MEMORY_PERMISSION address=");hex(&b,address);text(&b," size=");hex(&b,size);text(&b," perm=");hex(&b,perm);text(&b," region=");text(&b,old.name);emit(&b);
        r[0]=0;ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==3) {
        /* SetMemoryAttribute: SDK 17 ProtectRelro locks the permission (attribute 0x10) after making it read-only. */
        trace_frames(ctx);
        uint64_t address=r[0],size=r[1];unsigned mask=(unsigned)r[2],attr=(unsigned)r[3];
        /* SDK 17 also tags decoder buffers device-shared (mask 0x8). Host memory stays coherent, so the
         * attribute is recorded only; the range itself is validated below. */
        {struct line a={0};text(&a,"SET_MEMORY_ATTRIBUTE_ARG address=");hex(&a,address);text(&a," size=");hex(&a,size);text(&a," mask=");hex(&a,mask);text(&a," attr=");hex(&a,attr);emit(&a);}
        if((address|size)&(PAGE-1) || !size || (mask!=0x10 && mask!=0x8) || (attr&~mask))stop("UNSUPPORTED_MEMORY_ATTRIBUTE",pc,mask,20);
        struct region old=query(address);
        if(!old.state || old.state==16 || size>old.base+old.size-address || old.perm&PROT_EXEC)stop("MEMORY_ATTRIBUTE_RANGE",pc,address,20);
        region_protect(address,size,old.perm,old.attr|attr,pc);
        struct line b={0};text(&b,"SET_MEMORY_ATTRIBUTE address=");hex(&b,address);text(&b," size=");hex(&b,size);text(&b," attr=");hex(&b,attr);emit(&b);
        r[0]=0;ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==1) {
        trace_frames(ctx);
        uint64_t requested=r[1];
        if(requested%(2ULL<<20) || requested>HEAP_LIMIT)stop("HEAP_REQUEST_OUTSIDE_EXPERIMENT_LIMIT",pc,requested,20);
        /* The first allocation and same-size queries are supported. Growing or
           shrinking an existing heap requires a separately tested implementation. */
        if(heap_size && requested!=heap_size)stop("HEAP_RESIZE_NOT_IMPLEMENTED",pc,requested,20);
        if(requested && !heap_size) {
            pthread_mutex_lock(&regions_mutex);
            region_write_begin(pc);
            /* Zero/ZX requests0xbac00000 virtual bytes. Native1001 hit the
             * Linux3.14 commit heuristic with plain anonymous mmap. The .26
             * isolated heap probe proves NORESERVE retains zero-fill and
             * exact hint semantics. Native clears/writes remain unmodified;
             * this does not enable any LC2 lazy-zero address hook. */
            int mapping_flags=megaman_heap_mapping_flags(1);
            void *p=mmap((void *)(uintptr_t)HEAP_BASE,requested,3,mapping_flags,-1,0);
            int mapping_errno=p==MAP_FAILED?errno:0;
            if(p==MAP_FAILED || (uintptr_t)p!=HEAP_BASE) {
                struct line diagnostic={0};text(&diagnostic,"HEAP_MAPPING_DIAGNOSTIC requested=");hex(&diagnostic,requested);
                text(&diagnostic," flags=");hex(&diagnostic,(unsigned)mapping_flags);text(&diagnostic," errno=");hex(&diagnostic,(unsigned)mapping_errno);
                text(&diagnostic," returned=");hex(&diagnostic,(uintptr_t)p);emit(&diagnostic);
                if(p!=MAP_FAILED)munmap(p,requested);
                stop("HEAP_MAPPING_FAILED",pc,requested,21);
            }
            struct line policy={0};text(&policy,"HEAP_MAPPING_POLICY address=");hex(&policy,HEAP_BASE);text(&policy," bytes=");hex(&policy,requested);
            text(&policy," flags=");hex(&policy,(unsigned)mapping_flags);text(&policy," verified-lazy-zero=");hex(&policy,megaman_memory_enabled);
            text(&policy," physical-pages=on-access");emit(&policy);
            if(nregions==MAX_REGIONS)stop("REGION_TABLE_FULL",pc,nregions,21);
            size_t at=nregions;
            while(at && regions[at-1].base>HEAP_BASE){regions[at]=regions[at-1];at--;}
            regions[at]=(struct region){HEAP_BASE,requested,3,5,"main_heap",0};nregions++;heap_size=requested;
            region_write_end(pc);
            pthread_mutex_unlock(&regions_mutex);
        }
        emit_svc(&b,svc);r[0]=0;r[1]=HEAP_BASE;ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==0x2c || svc==0x2d) {
        /* Alias-region physical mapping used by nn::os VammManager (Unity 2021 virtual memory).
         * Horizon semantics (KPageTable::Map/UnmapPhysicalMemory): the range may already be partly
         * mapped; only the free pages are backed, and only the pages this call owns are released. */
        uint64_t address=r[0],size=r[1];
        /* ROOT (MEDIDO device runs 113/115/118): Unity 6 sometimes decodes the next 4096x4096 page while the
         * previous page's 64 MiB + 4 KiB decoded buffer is still alive; the load peak then varies by 64 MiB
         * and the 916 MiB device crosses the floor. Pace it: hold THIS request (no lock held) until the other
         * decoded buffer is unmapped, at most T2_DECODE_PACE_MS (default 2000; 0 disables). */
        if(svc==0x2c && size==0x4001000) {
            static long pace_ms=-1;if(pace_ms<0){const char *o=getenv("T2_DECODE_PACE_MS");pace_ms=o?strtol(o,NULL,10):0; /* MEDIDO runs 122/123: the older buffer is freed only at frame end, pacing never helped: off */}
            unsigned waited=0,live=0;
            for(;;) {
                live=0;for(unsigned q=0;q<4;q++)if(__atomic_load_n(&t2_decode_maps[q],__ATOMIC_ACQUIRE))live++;
                if(!live || (long)waited>=pace_ms)break;
                struct timespec ts={0,5*1000*1000};nanosleep(&ts,NULL);waited+=5;
            }
            if(waited)printf("T2_DECODE_PACE waited-ms=%u still-live=%u\n",waited,live);
        }
        if((address|size)&(PAGE-1) || !size || address<ALIAS_BASE || size>ALIAS_SIZE ||
           address-ALIAS_BASE>ALIAS_SIZE-size)stop("INVALID_PHYSICAL_MEMORY_RANGE",pc,address,24);
        pthread_mutex_lock(&regions_mutex);
        region_write_begin(pc);
        uint64_t cursor=address,changed=0;
        while(cursor<address+size) {
            struct region piece=query(cursor);
            uint64_t extent=piece.base+piece.size-cursor;
            if(extent>address+size-cursor)extent=address+size-cursor;
            int owned=piece.state==5 && piece.name && !strcmp(piece.name,"alias_physical");
            if(svc==0x2c && !piece.state) {
                void *p=mmap((void *)(uintptr_t)cursor,extent,3,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE|MAP_NORESERVE,-1,0);
                if(p==MAP_FAILED || (uintptr_t)p!=cursor) {
                    int mapping_errno=p==MAP_FAILED?errno:0;
                    struct line diagnostic={0};text(&diagnostic,"PHYSICAL_MEMORY_MAP_DIAGNOSTIC address=");hex(&diagnostic,cursor);
                    text(&diagnostic," size=");hex(&diagnostic,extent);
                    text(&diagnostic," errno=");hex(&diagnostic,(unsigned)mapping_errno);
                    text(&diagnostic," returned=");hex(&diagnostic,(uintptr_t)p);emit(&diagnostic);
                    if(p!=MAP_FAILED)munmap(p,extent);
                    region_write_end(pc);pthread_mutex_unlock(&regions_mutex);
                    stop("PHYSICAL_MEMORY_MAP_FAILED",pc,cursor,21);
                }
                region_insert((struct region){cursor,extent,3,5,"alias_physical",0},pc);
                alias_mapped+=extent;changed+=extent;
            } else if(svc==0x2c && !owned) {
                region_write_end(pc);pthread_mutex_unlock(&regions_mutex);
                stop("PHYSICAL_MEMORY_CONFLICT",pc,cursor,24);
            } else if(svc==0x2d && owned) {
                t2_gzram_forget(cursor,extent); /* Frente J */
                if(munmap((void *)(uintptr_t)cursor,extent)) {
                    region_write_end(pc);pthread_mutex_unlock(&regions_mutex);
                    stop("PHYSICAL_MEMORY_UNMAP_FAILED",pc,cursor,21);
                }
                size_t at=0;while(at<nregions && regions[at].base!=piece.base)at++;
                if(at==nregions) {
                    region_write_end(pc);pthread_mutex_unlock(&regions_mutex);
                    stop("PHYSICAL_MEMORY_UNMAP_MISMATCH",pc,cursor,24);
                }
                struct region head=regions[at];
                memmove(regions+at,regions+at+1,(nregions-at-1)*sizeof(regions[0]));nregions--;
                if(head.base<cursor)region_insert((struct region){head.base,cursor-head.base,3,5,"alias_physical",0},pc);
                if(cursor+extent<head.base+head.size)
                    region_insert((struct region){cursor+extent,head.base+head.size-cursor-extent,3,5,"alias_physical",0},pc);
                alias_mapped-=extent;changed+=extent;
            }
            cursor+=extent;
        }
        if(svc==0x2c && size==0x4001000){__atomic_store_n(&t2_decode_maps[t2_decode_next%4],address,__ATOMIC_RELEASE);t2_decode_next++;} /* ROOT */
        if(svc==0x2d)for(unsigned q=0;q<4;q++)if(t2_decode_maps[q]>=address && t2_decode_maps[q]<address+size)__atomic_store_n(&t2_decode_maps[q],0,__ATOMIC_RELEASE); /* ROOT: forget unmapped */
        region_coalesce_alias_physical();
        region_write_end(pc);
        pthread_mutex_unlock(&regions_mutex);
        struct line m={0};text(&m,svc==0x2c?"MAP_PHYSICAL_MEMORY address=":"UNMAP_PHYSICAL_MEMORY address=");
        hex(&m,address);text(&m," size=");hex(&m,size);text(&m," changed=");hex(&m,changed);
        text(&m," alias-mapped=");hex(&m,alias_mapped);text(&m," regions=");hex(&m,nregions);emit(&m);
        emit_svc(&b,svc);r[0]=0;ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==0x1f) {
        emit_svc(&b,svc);uint32_t handle=connect_port(r[1],pc);
        r[0]=0;r[1]=handle;ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==0x21) {
        emit_svc(&b,svc);r[0]=ipc_send(r[0],pc);ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==0x16) {
        emit_svc(&b,svc);session_close(r[0],pc);
        r[0]=0;ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==0x1d) {
        if((r[0]&3) || !accessible(r[0],4,PROT_WRITE))stop("INVALID_PROCESS_WIDE_KEY",pc,r[0],24);
        emit_svc(&b,svc);key_signal(r[0],(int32_t)r[1]);r[0]=0;ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==0x1e){r[0]=counter_ticks();ctx->uc_mcontext.pc+=4;return;}
    if(svc==0x34 || svc==0x35) {
        emit_svc(&b,svc);
        r[0]=svc==0x34?zero_address_wait(r[0],r[1],(int32_t)r[2],(int64_t)r[3],pc):
            zero_address_signal(r[0],r[1],(int32_t)r[2],(int32_t)r[3],pc);
        ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==4 || svc==5) {
        emit_svc(&b,svc);stack_alias(r[0],r[1],r[2],svc==5,pc);r[0]=0;ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==0x13) {
        emit_svc(&b,svc);struct session *s=session_get(r[0],pc);
        if(s->endpoint!=EP_SHARED)stop("INVALID_SHARED_MEMORY_HANDLE",pc,r[0],24);
        shared_map(s->initialized,r[1],r[2],r[3],pc);r[0]=0;ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==8) {
        emit_svc(&b,svc);uint32_t handle=thread_create(r[1],r[2],r[3],(int32_t)r[4],(int32_t)r[5],pc);
        r[0]=0;r[1]=handle;ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==9) {emit_svc(&b,svc);thread_start(r[0],pc);r[0]=0;ctx->uc_mcontext.pc+=4;return;}
    if(svc==0x15) {
        emit_svc(&b,svc);unsigned index=transfer_create(r[1],r[2],r[3],pc);
        uint32_t handle=session_new(EP_TRANSFER,pc);session_get(handle,pc)->initialized=index;
        r[0]=0;r[1]=handle;ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==0xa) {emit_svc(&b,svc);thread_exit(pc);}
    if(svc==0x18) {
        emit_svc(&b,svc);uint32_t index=0;
        r[0]=wait_synchronization(r[1],(int32_t)r[2],(int64_t)r[3],&index,pc);
        if(!r[0])r[1]=index;
        ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==0x1a) {emit_svc(&b,svc);mutex_wait(r[1],r[0],r[2],pc);r[0]=0;ctx->uc_mcontext.pc+=4;return;}
    if(svc==0x1b) {emit_svc(&b,svc);mutex_release(r[0],pc);r[0]=0;ctx->uc_mcontext.pc+=4;return;}
    if(svc==0x1c) {
        emit_svc(&b,svc);
        /* Bounded diagnostic for a stalled boot: who is waiting on which process-wide key. */
        static _Thread_local unsigned traced_waits;
        if(momo_trace_waits && traced_waits<momo_trace_waits) {
            traced_waits++;
            struct line w={0};text(&w,"MOMO_WAIT_KEY thread=");hex(&w,guest_thread_index);
            text(&w," key=");hex(&w,r[0]);text(&w," mutex=");hex(&w,r[1]);text(&w," tag=");hex(&w,r[2]);
            text(&w," timeout=");hex(&w,r[3]);emit(&w);
            trace_frames(ctx);
        }
        r[0]=key_wait(r[0],r[1],r[2],(int64_t)r[3],pc);ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==0xb) {
        emit_svc(&b,svc);int64_t ns=(int64_t)r[0];pthread_mutex_unlock(&kernel_mutex);
        if(ns>0){struct timespec delay={ns/1000000000,ns%1000000000};while(nanosleep(&delay,&delay)&&errno==EINTR){}}
        else sched_yield();
        pthread_mutex_lock(&kernel_mutex);ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==0xf) {
        emit_svc(&b,svc);thread_affinity(thread_get(r[0],pc),(int32_t)r[1],r[2],pc);
        r[0]=0;ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==0x19) {
        emit_svc(&b,svc);thread_get(r[0],pc)->cancel=1;r[0]=0;ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==0xd) {
        emit_svc(&b,svc);int priority=(int32_t)r[1];if(priority<28 || priority>59)stop("INVALID_THREAD_PRIORITY",pc,r[1],24);
        thread_get(r[0],pc)->priority=priority;r[0]=0;ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==0x32) {
        emit_svc(&b,svc);r[0]=messenger_thread_activity(r[0],r[1],pc);ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==0x33) {
        emit_svc(&b,svc);r[0]=messenger_thread_context(r[0],r[1],pc);ctx->uc_mcontext.pc+=4;return;
    }
    if(svc==0x10){r[0]=sched_getcpu();ctx->uc_mcontext.pc+=4;return;}
    if(svc==0x26) {
        emit_svc(&b,svc);stop("GUEST_SVC_BREAK",pc,guest_terminate_result?guest_terminate_result:r[0],25);
    }
    if(svc==0x17 || svc==0x12) {
        emit_svc(&b,svc);struct session *event=session_get(r[0],pc);
        if(event->endpoint!=EP_EVENT)stop("INVALID_EVENT_HANDLE",pc,r[0],24);
        r[0]=svc==0x17 && !event->initialized?0xfa01:0;event->initialized=0;ctx->uc_mcontext.pc+=4;return;
    }
    emit_svc(&b,svc);stop("UNIMPLEMENTED_SVC",pc,svc,20);
}
/* MOMO_STACK_SAMPLE_MS=<ms>: a bounded profile of the guest main thread. Each SIGPROF prints the
 * guest frame chain at the interrupted PC, which is the only way to see where the game spins when it
 * is not inside a system call (MEDIDO need: the boot stalls inside guest code, run momo-080). */
static unsigned momo_stack_samples_left;
static void signal_handler(int sig,siginfo_t *info,void *opaque) {
    if(sig==SIGPROF) {
        ucontext_t *ctx=opaque;
        if(momo_stack_samples_left) {
            momo_stack_samples_left--;
            struct line b={0};text(&b,"MOMO_SAMPLE thread=");hex(&b,guest_thread_index);
            text(&b," in-kernel=");hex(&b,signal_handler_depth);text(&b," pc=");
            location(&b,ctx->uc_mcontext.pc);emit(&b);
            if(!signal_handler_depth)trace_frames(ctx);
        }
        return;
    }
    if(sig==SIGSEGV && t2_gzram_fault(info,opaque))return; /* Frente J: compressed guest block, restored; retry */
    ucontext_t *previous=current_guest_context;
    signal_handler_depth++;
    if(sig!=SIGTRAP){signal_dispatch(sig,info,opaque);return;}
    ucontext_t *ctx=opaque;uint32_t instruction=bridge_instruction(ctx->uc_mcontext.pc);
    /* These instructions access only the interrupted thread's virtual register
     * state and the monotonic clock. They cannot wake a waiter or modify kernel
     * objects, so they neither lock nor broadcast the global kernel condition. */
    if(instruction && *(const uint32_t *)(uintptr_t)ctx->uc_mcontext.pc==TRAP_OPCODE) {
        current_guest_context=ctx;
        if(local_register_trap(ctx,instruction)) {
            current_guest_context=previous;signal_handler_depth--;return;
        }
    }
    struct guest_profile_scope profile;guest_profile_begin(&profile,instruction);
    pthread_mutex_lock(&kernel_mutex);guest_profile_locked(&profile);
    /* SVC32 pause gate, with the Messenger implementation: a blocked system call keeps a stable
     * guest frame and must not return into guest code while the thread is paused. */
    struct guest_thread *thread=&threads[guest_thread_index];
    ucontext_t *previous_kernel=thread->kernel_context;
    if(!nvn_bridge_depth)thread->kernel_context=ctx;
    messenger_pause_kernel_gate(ctx);
    signal_dispatch(sig,info,opaque);
    guest_profile_end(&profile);
    messenger_pause_kernel_gate(ctx);
    thread->kernel_context=previous_kernel;
    pthread_cond_broadcast(&kernel_changed);pthread_mutex_unlock(&kernel_mutex);
    current_guest_context=previous;
    signal_handler_depth--;
}
void *momo_stack_sampler(void *argument) {
    unsigned ms=*(unsigned *)argument;
    struct timespec pause={ms/1000,(long)(ms%1000)*1000000L};
    /* Round-robin over every live guest thread: the Unity main thread spends most of its life blocked
     * inside a system call, so sampling only thread 0 returned nothing at all (run momo-133). A sample
     * taken inside the kernel gate is still reported, marked in-kernel, without walking the frames. */
    unsigned next=0;
    {   /* MOMO_STACK_SAMPLE_AFTER=<seconds>: skip the boot and profile the steady state only. */
        const char *after=getenv("MOMO_STACK_SAMPLE_AFTER");
        if(after){struct timespec w={(time_t)strtoul(after,NULL,0),0};nanosleep(&w,NULL);}
    }
    while(momo_stack_samples_left) {
        nanosleep(&pause,NULL);
        unsigned live=nthreads<MAX_THREADS?nthreads:MAX_THREADS,tried=0;
        while(tried++<live) {
            unsigned i=next++%live;
            if(threads[i].state==2 && threads[i].native){pthread_kill(threads[i].native,SIGPROF);break;}
        }
    }
    return NULL;
}
static void *map_region_fd(uint64_t base,uint64_t size,unsigned perm,unsigned state,const char *name,int fd) {
    pthread_mutex_lock(&regions_mutex);region_write_begin(0);
    if(nregions==MAX_REGIONS || !size || base%PAGE || size%PAGE) {fprintf(stderr,"bad mapping\n");exit(1);}
    /* fd>=0: private (copy-on-write) file mapping of a read-only image segment; see t2_image_file_map.h. */
    void *p=mmap((void *)(uintptr_t)base,size,PROT_READ|PROT_WRITE,fd>=0?MAP_PRIVATE:MAP_PRIVATE|MAP_ANONYMOUS,fd,0);
    /* Old NextOS kernels lack MAP_FIXED_NOREPLACE; a hint plus exact-address check never clobbers existing mappings. */
    if(p==MAP_FAILED || (uint64_t)(uintptr_t)p!=base) {fprintf(stderr,"mapping unavailable %s: %s\n",name,strerror(errno));exit(1);}
    regions[nregions++]=(struct region){base,size,perm,state,name,0};
    region_write_end(0);pthread_mutex_unlock(&regions_mutex);return p;
}
static void *map_region(uint64_t base,uint64_t size,unsigned perm,unsigned state,const char *name) {
    return map_region_fd(base,size,perm,state,name,-1);
}
#include "t2_image_file_map.h"
static int region_cmp(const void *a,const void *b) {
    const struct region *x=a,*y=b;return x->base>y->base?1:x->base<y->base?-1:0;
}
int main(int argc,char **argv) {
    (void)argv;if(argc!=1){fprintf(stderr,"Run from the verified native image directory, without arguments.\n");return 1;}
    setvbuf(stdout,NULL,_IONBF,0);prctl(PR_SET_NAME,"terminator2d-nextos",0,0,0);main_thread_id=(uint64_t)getpid();
    nvn_platform_argv=argv;
    t2_runtime_health_capture();
    if(t2_video_init())return 24;
    unsigned drm_present=access("/dev/dri/card0",F_OK)==0 ||
        access("/dev/dri/renderD128",F_OK)==0;
    unsigned modern_gpu_node=access("/dev/mali0",F_OK)==0;
    t2_alias_base=t2_alias_region_select(MEGAMAN_HOST_DIAGNOSTIC,t2_pc_bench(),
        getenv("T2_DISPLAY_BACKEND"),drm_present,modern_gpu_node);
    printf("T2_ALIAS_REGION base=0x%"PRIx64" size=0x%"PRIx64" end=0x%"PRIx64" drm=%u modern-gpu-node=%u host-reservation=none\n",
        ALIAS_BASE,ALIAS_SIZE,ALIAS_BASE+ALIAS_SIZE,drm_present,modern_gpu_node);
    const char *dump=getenv("MOMO_DUMP");
    if(dump){char *rest=NULL;momo_dump_address=strtoull(dump,&rest,0);momo_dump_size=rest&&*rest==':'?strtoull(rest+1,NULL,0):64;
             if(momo_dump_size>4096)momo_dump_size=4096;}
    const char *trace_waits=getenv("MOMO_TRACE_WAITS");
    if(trace_waits)momo_trace_waits=(unsigned)strtoul(trace_waits,NULL,0);
    const char *hot_log_verbose=getenv("COTM_VERBOSE_FRAME_LOGS");
    zero_hot_log_verbose=hot_log_verbose&&!strcmp(hot_log_verbose,"1");
#if ZERO_ZX_GAME
    zero_hot_log_enabled=1;
#endif
    threads[0].native=pthread_self();threads[0].id=main_thread_id;
    {   /* The sampler prints raw host addresses; without the load base they cannot be turned back into
         * functions after the run (the base is randomized per run, MEDIDO run momo-149). */
        Dl_info info;extern int main(int,char **);
        if(dladdr((void *)(uintptr_t)main,&info) && info.dli_fbase)
            printf("HOST_BASE address=%p file=%s\n",info.dli_fbase,info.dli_fname?info.dli_fname:"");
    }
    struct rlimit no_core={0,0};setrlimit(RLIMIT_CORE,&no_core);
    struct rlimit address_limit={HOST_ADDRESS_LIMIT,HOST_ADDRESS_LIMIT};
    if(setrlimit(RLIMIT_AS,&address_limit)){perror("diagnostic memory quota");return 1;}
    printf("START native-aarch64 program-id=0x%llx sdk=%s rtld-entry=0x8000000 shims=Memory,Threads,IPC guest-quota=%lluMiB host-as-limit=%lluMiB heap-limit=%lluMiB; virtual limits are not resident-RAM measurements\n",(unsigned long long)LC2_PROGRAM_ID,LC2_SDK_VERSION_STRING,(unsigned long long)(MEMORY_BUDGET>>20),(unsigned long long)(HOST_ADDRESS_LIMIT>>20),(unsigned long long)(HEAP_LIMIT>>20));
    FILE *random=fopen("/dev/urandom","rb");
    if(!random || fread(entropy,1,sizeof(entropy),random)!=sizeof(entropy)){fprintf(stderr,"entropy unavailable\n");return 1;}
    fclose(random);
    if(t2_image_heap_guard(ALIAS_BASE==T2_ALIAS_BASE_DRM,argv))return 1;
    for(size_t i=0;i<sizeof(images)/sizeof(images[0]);i++) {
        const struct image_segment *s=&images[i];
        if(t2_image_file_map(s))continue; /* read-only segment backed by its file: clean, reclaimable pages */
        void *p=map_region(s->base,s->map_size,s->perm,s->state,s->name);
        FILE *f=fopen(s->name,"rb");
        if(!f || fread(p,1,s->file_size,f)!=s->file_size || fgetc(f)!=EOF){fprintf(stderr,"segment read failed %s\n",s->name);return 1;}
        fclose(f);
    }
    t2_lazy_mesh_apply(images,sizeof(images)/sizeof(images[0])); /* Frente L: lazy quad meshes, T2_LAZY_MESH=0 disables */
    t2_image_file_map_report();
    /* Build and staging pin the original main NSO and segment hashes. The
     * optional movie handler also checks each relevant native instruction. */
    /* Historical LC2 hooks stay off: their code and object layouts are not
     * interchangeable with Zero/ZX. Native missing-MP4 behavior is observed
     * before adding a separately proven completion handler. */
    /* Animal Well: the lazy BSS zero is re-pinned to this game's rtld/sdk memset (megaman_memory.h);
     * the LC2 movie hook stays off (its identity check disables it). */
    if(getenv("MEGAMAN_OMIT_VIDEOS"))
        stop("ZERO_ZX_REJECT_LC2_ADDRESS_HOOKS",0,0,24);
    megaman_movie_configure(LC2_MAIN_BASE,NULL);
    /* Terminator 2D: the lazy-zero hook is pinned to the Momodora sdk/rtld memset; the VERIFIED identities
     * of this title's modules are passed, so MEGAMAN_LAZY_ZERO=1 fails closed (LAZY_ZERO_NSO_IDENTITY)
     * until the hook is re-pinned for 19.3.9. Off by default. */
    megaman_memory_configure(LC2_SDK_BASE,LC2_SDK_SHA256,LC2_RTLD_BASE,LC2_RTLD_SHA256);
    tls_stubs_initialize();
    aw_prof_thread=0;aw_prof_start();
    int tls_fast=tls_stubs_install(images,sizeof(images)/sizeof(images[0]));
    printf("TLS_READ_STUBS sites=%zu bytes=%zu offsets=0x%"PRIx64",0x%"PRIx64" fallback=%s; tls_trap_reads counts only remaining traps\n",
           ntls_stubs,ntls_stubs*TLS_STUB_BYTES,tls_stub_offsets[0],tls_stub_offsets[1],tls_fast?"none":tls_stub_fallback);
    for(size_t i=0;i<sizeof(images)/sizeof(images[0]);i++) {
        const struct image_segment *s=&images[i];void *p=(void *)(uintptr_t)s->base;
        if(s->perm&PROT_EXEC) for(uint64_t off=0;off+4<=s->file_size;off+=4) {
            uint32_t *w=(uint32_t *)((uint8_t *)p+off),v=*w;
            unsigned graphics_entry=0;
            for(unsigned j=0;j<sizeof(graphics_entries)/sizeof(graphics_entries[0]);j++)if(s->base+off==graphics_entries[j].pc) {
                if(v!=graphics_entries[j].opcode)stop("GRAPHICS_IMAGE_IDENTITY",s->base+off,v,24);
                graphics_entry=1;
            }
            if(graphics_entry || megaman_movie_entry(s->base+off,v) ||
               megaman_memory_entry(s->base+off,v) ||
               (v&0xffe0001fU)==0xd4000001U || (v&0xffffffe0U)==0xd53bd060U ||
               (v&0xffffffe0U)==0xd53bd040U || (v&0xffffffe0U)==0xd51bd040U ||
               (v&0xffffffe0U)==0xd53be020U || (v&0xffffffe0U)==0xd53be040U || (v&0xffffffe0U)==0xd53be000U) {
                if(nbridges==MAX_TRAPS){fprintf(stderr,"too many bridges\n");return 1;}
                bridges[nbridges++]=(struct bridge){s->base+off,v};*w=TRAP_OPCODE;
            }
        }
        if(s->perm&PROT_EXEC)__builtin___clear_cache(p,(char *)p+s->map_size);
        if(mprotect(p,s->map_size,s->perm)){perror("mprotect");return 1;}
        printf("MAP %s base=0x%"PRIx64" size=0x%"PRIx64" perm=%u\n",s->name,s->base,s->map_size,s->perm);
    }
    map_region(0x60000000,LC2_MAIN_STACK_SIZE,3,11,"main_stack");
    map_region(0x70000000,0x1000,3,12,"main_thread_local_region");
    qsort(regions,nregions,sizeof(regions[0]),region_cmp);
    qsort(bridges,nbridges,sizeof(bridges[0]),bridge_compare);
    for(size_t i=1;i<nbridges;i++)if(bridges[i-1].pc==bridges[i].pc)stop("DUPLICATE_TRAP_ADDRESS",bridges[i].pc,0,24);
    stack_t alt={0};alt.ss_size=1024*1024;alt.ss_sp=mmap(NULL,alt.ss_size,3,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(alt.ss_sp==MAP_FAILED || sigaltstack(&alt,NULL)){perror("sigaltstack");return 1;}
    struct sigaction sa={0};sa.sa_sigaction=signal_handler;sa.sa_flags=SA_SIGINFO|SA_ONSTACK;sigemptyset(&sa.sa_mask);
    int signals[]={SIGTRAP,SIGSEGV,SIGILL,SIGBUS,SIGFPE,SIGALRM};
    for(size_t i=0;i<sizeof(signals)/sizeof(signals[0]);i++)if(sigaction(signals[i],&sa,NULL)){perror("sigaction");return 1;}
    {
        const char *interval=getenv("MOMO_STACK_SAMPLE_MS");
        if(interval) {
            struct sigaction profile={0};profile.sa_sigaction=signal_handler;
            profile.sa_flags=SA_SIGINFO|SA_ONSTACK|SA_RESTART;sigemptyset(&profile.sa_mask);
            if(sigaction(SIGPROF,&profile,NULL)){perror("sigaction SIGPROF");return 1;}
            momo_stack_samples_left=(unsigned)(getenv("MOMO_STACK_SAMPLES")?strtoul(getenv("MOMO_STACK_SAMPLES"),NULL,0):64);
            static struct {unsigned ms;} sampler;sampler.ms=(unsigned)strtoul(interval,NULL,0);
            if(sampler.ms<50)sampler.ms=50;
            pthread_t thread;
            extern void *momo_stack_sampler(void *);
            if(pthread_create(&thread,NULL,momo_stack_sampler,&sampler.ms))perror("stack sampler");
            else printf("MOMO_STACK_SAMPLER interval-ms=%u samples=%u target=guest-main\n",sampler.ms,momo_stack_samples_left);
        }
    }
    struct sigaction pause_action={0};pause_action.sa_sigaction=messenger_pause_signal;
    pause_action.sa_flags=SA_SIGINFO|SA_ONSTACK|SA_RESTART;sigemptyset(&pause_action.sa_mask);
    if(sigaction(SIGUSR2,&pause_action,NULL)){perror("sigaction SIGUSR2");return 1;}
    printf("ENTER original rtld; bridges=%zu; no manual relocations; guest TLS virtualized\n",nbridges);
    unsigned probe_seconds=20;
    const char *budget=getenv("MEGAMAN_PROBE_SECONDS");
    if(budget) {
        char *end=NULL;unsigned long seconds=strtoul(budget,&end,10);
        if(!*budget || *end || (seconds<1 && strcmp(budget,"0")) || seconds>900)stop("INVALID_PROBE_TIME_BUDGET",0,seconds,24);
        probe_seconds=(unsigned)seconds;
    }
    printf("NATIVE_PROBE_TIME_BUDGET seconds=%u\n",probe_seconds);
    /* Host workers (and SDL children they create later) retain the caller's
     * original allowed CPUs. Pin only the guest main, before its first opcode;
     * guest-created threads still receive their native masks in thread_run. */
    if(host_affinity_log(pthread_self(),"host-initial"))return 1;
    guest_profile_configure();
    hid_host_initialize();aud_host_initialize();t2_memwatch_start();
    t2_gzram_start(); /* Frente J */
    unsigned host_base=0;{const char *o=getenv("T2_HOST_CPU_BASE");unsigned long b=o?strtoul(o,NULL,0):0;
        if(b>CPU_SETSIZE-1){fprintf(stderr,"invalid T2_HOST_CPU_BASE %lu\n",b);return 1;}host_base=(unsigned)b;
        if(host_base)printf("T2_HOST_CPU_MAP guest-main host=%u\n",host_base);}
    cpu_set_t affinity;CPU_ZERO(&affinity);CPU_SET(host_base,&affinity);
    if(sched_setaffinity(0,sizeof(affinity),&affinity)){perror("main-thread core 0 affinity");return 1;}
    if(host_affinity_log(pthread_self(),"guest-main") || host_affinity_log(hid_host.worker,"host-hid") ||
       host_affinity_log(aud_host.worker,"host-audio"))return 1;
    tls_stubs_validate_thread(0x08000000);
    /* Explicit zero cancels the diagnostic alarm for the packaged runtime. */
    alarm(probe_seconds);enter_guest(0x08000000,0x60000000+LC2_MAIN_STACK_SIZE,0x100);
}
