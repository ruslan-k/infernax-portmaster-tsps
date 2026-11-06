/* SVC32/33: pause real guest execution and expose its actual AArch64 context.
 * A blocked SVC already has a stable guest frame under kernel_mutex; host
 * service completion may proceed, but the trap cannot return while paused.
 * Running guest code parks in a private signal using only atomics/futex.
 * Never suspend inside a driver, allocator or nested original NVN callback.
 */
#include <linux/futex.h>
_Static_assert(__atomic_always_lock_free(4,0),"thread activity atomics must be lock-free");
static void messenger_pause_capture(struct guest_thread *t,const ucontext_t *ctx,unsigned in_svc) {
    memcpy(&t->paused_context,ctx,sizeof(*ctx));
    if(in_svc) {
        /* Horizon does not expose volatile x0..x18 during a system call.
         * Its exception PC is the instruction following SVC. */
        memset(t->paused_context.uc_mcontext.regs,0,19*8);
        uint32_t op=bridge_instruction(ctx->uc_mcontext.pc);
        if((op&0xffe0001fU)==0xd4000001U)t->paused_context.uc_mcontext.pc+=4;
    }
}
static unsigned messenger_pause_guest_pc(uint64_t pc) {
    for(unsigned i=0;i<sizeof(images)/sizeof(images[0]);i++)
        if((images[i].perm&PROT_EXEC) && pc>=images[i].base && pc-images[i].base<images[i].map_size)return 1;
    return 0;
}
static void messenger_pause_signal(int sig,siginfo_t *info,void *opaque) {
    (void)sig;(void)info;int saved_errno=errno;
    struct guest_thread *t=&threads[guest_thread_index];ucontext_t *ctx=opaque;
    if(!__atomic_load_n(&t->pause_requested,__ATOMIC_ACQUIRE) || signal_handler_depth || nvn_bridge_depth ||
       !messenger_pause_guest_pc(ctx->uc_mcontext.pc))return;
    messenger_pause_capture(t,ctx,0);
    __atomic_store_n(&t->pause_ack,1,__ATOMIC_RELEASE);
    while(__atomic_load_n(&t->pause_requested,__ATOMIC_ACQUIRE))
        syscall(SYS_futex,&t->pause_requested,FUTEX_WAIT_PRIVATE,1,NULL,NULL,0);
    __atomic_store_n(&t->pause_ack,0,__ATOMIC_RELEASE);errno=saved_errno;
}
/* kernel_mutex held; called before dispatch and before releasing the user
 * register frame. An already acknowledged blocked SVC keeps its snapshot. */
static void messenger_pause_kernel_gate(ucontext_t *ctx) {
    if(nvn_bridge_depth)return;
    struct guest_thread *t=&threads[guest_thread_index];
    while(__atomic_load_n(&t->pause_requested,__ATOMIC_ACQUIRE)) {
        if(!__atomic_load_n(&t->pause_ack,__ATOMIC_ACQUIRE)) {
            messenger_pause_capture(t,ctx,1);__atomic_store_n(&t->pause_ack,2,__ATOMIC_RELEASE);
        }
        pthread_cond_wait(&kernel_changed,&kernel_mutex);
    }
}
static uint32_t messenger_thread_activity(uint64_t handle,uint64_t activity,uint64_t pc) {
    struct guest_thread *t=thread_get(handle,pc);
    if(activity>1)stop("MESSENGER_THREAD_ACTIVITY_VALUE",pc,activity,24);
    if(t==&threads[guest_thread_index])stop("MESSENGER_THREAD_ACTIVITY_SELF",pc,handle,24);
    if(t->state!=2)return 0xfa01;
    if(t->pause_busy)stop("MESSENGER_THREAD_ACTIVITY_CONCURRENT",pc,handle,20);
    if(__atomic_load_n(&t->pause_requested,__ATOMIC_ACQUIRE)==activity)return 0xfa01;
    t->pause_busy=1;
    __atomic_store_n(&t->pause_requested,(uint32_t)activity,__ATOMIC_RELEASE);
    if(!activity) {
        if(__atomic_load_n(&t->pause_ack,__ATOMIC_ACQUIRE)==2)__atomic_store_n(&t->pause_ack,0,__ATOMIC_RELEASE);
        syscall(SYS_futex,&t->pause_requested,FUTEX_WAKE_PRIVATE,1,NULL,NULL,0);
        pthread_cond_broadcast(&kernel_changed);
    }
    unsigned ready=0;
    for(unsigned retry=0;retry<2000;retry++) {
        unsigned ack=__atomic_load_n(&t->pause_ack,__ATOMIC_ACQUIRE);
        if((activity&&ack)||(!activity&&!ack)){ready=1;break;}
        if(activity && t->kernel_context) {
            messenger_pause_capture(t,t->kernel_context,1);
            __atomic_store_n(&t->pause_ack,2,__ATOMIC_RELEASE);ready=1;break;
        }
        if(activity) {
            int error=pthread_kill(t->native,SIGUSR2);
            if(error)stop("MESSENGER_THREAD_ACTIVITY_SIGNAL",pc,error,21);
        }
        pthread_mutex_unlock(&kernel_mutex);
        struct timespec delay={0,1000000};nanosleep(&delay,NULL);
        pthread_mutex_lock(&kernel_mutex);
        if(t->state!=2)stop("MESSENGER_THREAD_ACTIVITY_LIFETIME",pc,handle,24);
    }
    if(!ready)stop("MESSENGER_THREAD_ACTIVITY_TIMEOUT",pc,handle,20);
    t->pause_busy=0;
    static unsigned reports;
    if(++reports<=32 || !(reports&(reports-1))) {
        struct line b={0};text(&b,"MESSENGER_THREAD_ACTIVITY handle=");hex(&b,handle);text(&b," paused=");hex(&b,activity);
        text(&b," acknowledgement=");hex(&b,__atomic_load_n(&t->pause_ack,__ATOMIC_ACQUIRE));
        text(&b," guest-pc=");location(&b,t->paused_context.uc_mcontext.pc);emit(&b);
    }
    return 0;
}
static uint32_t messenger_thread_context(uint64_t address,uint64_t handle,uint64_t pc) {
    struct guest_thread *t=thread_get(handle,pc);
    if(!__atomic_load_n(&t->pause_requested,__ATOMIC_ACQUIRE) || !__atomic_load_n(&t->pause_ack,__ATOMIC_ACQUIRE))return 0xfa01;
    if(!accessible(address,0x320,PROT_WRITE))stop("MESSENGER_THREAD_CONTEXT_RANGE",pc,address,24);
    const ucontext_t *ctx=&t->paused_context;uint8_t out[0x320]={0};
    memcpy(out,ctx->uc_mcontext.regs,31*8);memcpy(out+0xf8,&ctx->uc_mcontext.sp,8);
    memcpy(out+0x100,&ctx->uc_mcontext.pc,8);uint32_t psr=ctx->uc_mcontext.pstate&0xff0ffe20U;memcpy(out+0x108,&psr,4);
    unsigned offset=0,found=0;
    while(offset+8<=sizeof(ctx->uc_mcontext.__reserved)) {
        const uint8_t *record=ctx->uc_mcontext.__reserved+offset;
        uint32_t magic=read32(record),size=read32(record+4);
        if(!magic && !size)break;
        if(size<16 || size%16 || size>sizeof(ctx->uc_mcontext.__reserved)-offset)stop("MESSENGER_THREAD_FPSIMD_RECORD",pc,size,24);
        if(magic==0x46508001 && size==0x210) {
            memcpy(out+0x110,record+16,512);memcpy(out+0x310,record+12,4);memcpy(out+0x314,record+8,4);found=1;
        }
        offset+=size;
    }
    if(!found)stop("MESSENGER_THREAD_FPSIMD_REQUIRED",pc,handle,20);
    /* Target SDK5.4 predates Horizon6: TPIDR_EL0 is not returned by SVC33. */
    memcpy((void *)(uintptr_t)address,out,sizeof(out));return 0;
}
