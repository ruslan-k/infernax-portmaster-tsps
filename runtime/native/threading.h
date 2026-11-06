/* Bounded guest threads backed by pthreads. Each has its own guest TLR,
 * virtual TPIDR, host stack and signal stack. Kernel state is serialized by
 * kernel_mutex; blocking operations release it through condition waits. */
#define MAX_THREADS 96U   /* Unity 2021 keeps growing its worker and loading pools (MEDIDO run momo-070) */
struct guest_thread {
    uint64_t entry,argument,stack,tls,id,affinity,host_tid;
    uint32_t handle;int priority,core,state;
    pthread_t native;sigjmp_buf host_return;
    uint64_t waiting_mutex,waiting_key;uint32_t waiting_tag;unsigned key_ready,cancel,starting;
    uint64_t last_svc_pc,last_svc_id;   /* stall census, printed by stop() */
    /* SVC32/33 pause state; copied with the Messenger implementation (90cbe57). */
    uint32_t pause_requested,pause_ack;unsigned pause_busy;
    ucontext_t *kernel_context;ucontext_t paused_context;
};
static struct guest_thread threads[MAX_THREADS]={{.handle=0x100,.priority=LC2_MAIN_PRIORITY,.core=LC2_MAIN_CORE,.affinity=1ULL<<LC2_MAIN_CORE,.state=2}};
static unsigned nthreads=1;
static uint64_t next_thread_id=0x100001;
static struct guest_thread *thread_get(uint64_t handle,uint64_t pc) {
    if(handle==0xffff8000U)return &threads[guest_thread_index];
    if(handle==0x100)return &threads[0];
    struct session *s=session_get(handle,pc);
    if(s->endpoint!=EP_THREAD || s->initialized>=nthreads || !threads[s->initialized].state)
        stop("UNIMPLEMENTED_THREAD_HANDLE",pc,handle,20);
    return &threads[s->initialized];
}
#include "thread_activity.h"
static void thread_affinity(struct guest_thread *t,int core,uint64_t mask,uint64_t pc) {
    if(core==-2)core=0;
    if(core==-3)core=t->core;
    if(!mask || (mask&~7ULL) || core < -1 || core>2 || (core>=0 && !(mask&(1ULL<<core))))
        stop("INVALID_THREAD_AFFINITY",pc,mask,24);
    if(t->state==2) {
        /* T2_HOST_CPU_BASE=<n>: the kernel cpuset may exclude CPU0..2 (measured: cpuset.cpus=4-7
         * while guest threads ask for cores 0..2 -> pthread_setaffinity_np EINVAL). Shifting only the
         * host mask keeps guest-visible core/mask semantics unchanged; default 0 is identity. */
        static int base_configured;static unsigned host_base;
        if(!base_configured){
            const char *o=getenv("T2_HOST_CPU_BASE");
            unsigned long b=o?strtoul(o,NULL,0):0;
            if(b>CPU_SETSIZE-3)stop("INVALID_HOST_CPU_BASE",pc,b,24);
            host_base=(unsigned)b;base_configured=1;
            if(host_base)printf("T2_HOST_CPU_MAP guest=0..2 host=%u..%u\n",host_base,host_base+2);
        }
        cpu_set_t set;CPU_ZERO(&set);for(unsigned i=0;i<3;i++)if(mask&(1ULL<<i))CPU_SET(host_base+i,&set);
#ifdef AW_NO_ROMFS
        {const char *o=getenv("AW_GUEST_ONE_CORE");if(o&&!strcmp(o,"1")){CPU_ZERO(&set);CPU_SET(0,&set);}} /* race bisection: every guest thread on CPU0 */
        {const char *o=getenv("AW_CPU0_EXCLUSIVE");if(o&&!strcmp(o,"1")&&t!=&threads[0]){CPU_CLR(0,&set);if(!CPU_COUNT(&set)){CPU_SET(1,&set);CPU_SET(2,&set);}
            printf("AW_GUEST_AFFINITY thread=%u mask=0x%"PRIx64" cpu0=excluded\n",(unsigned)(t-threads),mask);}} /* CPU0 reserved for the game loop */
#endif
        int rc=pthread_setaffinity_np(t->native,sizeof(set),&set);
        if(rc) {
#ifdef INFERNAX_CBUF_SAMPLERS
            uint64_t requested=0;for(unsigned i=0;i<64;i++)if(CPU_ISSET(i,&set))requested|=UINT64_C(1)<<i;
            static unsigned failure_sequence;struct timespec observed={0};clock_gettime(CLOCK_MONOTONIC,&observed);
            printf("INFERNAX_AFFINITY_EVENT sequence=%u monotonic-ns=%"PRIu64" current-tid=%ld pthread-self=0x%"PRIxPTR"\n",++failure_sequence,(uint64_t)observed.tv_sec*UINT64_C(1000000000)+(uint64_t)observed.tv_nsec,(long)syscall(SYS_gettid),(uintptr_t)pthread_self());
            printf("INFERNAX_AFFINITY_FAILURE index=%u handle=0x%x core=%d guest-mask=0x%"PRIx64" requested-host-mask=0x%"PRIx64" state=%d native=0x%"PRIxPTR" host-tid=%"PRIu64" self-target=%d rc=%d\n",
                (unsigned)(t-threads),t->handle,core,mask,requested,t->state,(uintptr_t)t->native,t->host_tid,pthread_equal(pthread_self(),t->native),rc);
            char online[128]="unread";FILE *fp=fopen("/sys/devices/system/cpu/online","r");
            if(fp){if(!fgets(online,sizeof(online),fp))strcpy(online,"unread");fclose(fp);}
            online[strcspn(online,"\r\n")]=0;printf("INFERNAX_AFFINITY_ONLINE value=%s\n",online);
            int current_rc=host_affinity_log(pthread_self(),"affinity-failure-current");
            cpu_set_t target;CPU_ZERO(&target);int target_rc=-1,target_error=EINVAL;
            if(t->host_tid && t->host_tid<=INT32_MAX){target_rc=sched_getaffinity((pid_t)t->host_tid,sizeof(target),&target);target_error=target_rc?errno:0;}
            uint64_t allowed=0;if(!target_rc)for(unsigned i=0;i<64;i++)if(CPU_ISSET(i,&target))allowed|=UINT64_C(1)<<i;
            printf("INFERNAX_AFFINITY_QUERIES current-rc=%d target-tid-rc=%d target-errno=%d target-low64-mask=0x%"PRIx64"\n",current_rc,target_rc,target_error,allowed);
            char path[128];snprintf(path,sizeof(path),"/proc/self/task/%"PRIu64"/status",t->host_tid);fp=fopen(path,"r");
            if(fp){char line[256];while(fgets(line,sizeof(line),fp))if(!strncmp(line,"Name:",5)||!strncmp(line,"State:",6)||!strncmp(line,"Pid:",4)||!strncmp(line,"Cpus_allowed_list:",18))printf("INFERNAX_AFFINITY_TASK %s",line);fclose(fp);}
            else printf("INFERNAX_AFFINITY_TASK_UNREAD errno=%d\n",errno);
            snprintf(path,sizeof(path),"/proc/self/task/%"PRIu64"/cgroup",t->host_tid);fp=fopen(path,"r");
            if(fp){char line[256];unsigned n=0;while(n++<16 && fgets(line,sizeof(line),fp))printf("INFERNAX_AFFINITY_CGROUP %s",line);fclose(fp);}
            const char *cpusets[]={"/sys/fs/cgroup/cpuset/cpuset.effective_cpus","/sys/fs/cgroup/cpuset/cpuset.cpus","/sys/fs/cgroup/cpuset.cpus.effective"};
            for(unsigned i=0;i<3;i++){fp=fopen(cpusets[i],"r");if(fp){char line[256];if(fgets(line,sizeof(line),fp))printf("INFERNAX_AFFINITY_ROOT_CPUSET path=%s value=%s",cpusets[i],line);fclose(fp);}}
            fflush(stdout);
#endif
            stop("HOST_THREAD_AFFINITY_FAILED",pc,rc,21);
        }
    }
    t->core=core;t->affinity=mask;
}
static void *thread_run(void *opaque) {
    struct guest_thread *t=opaque;guest_thread_index=t-threads;guest_tls_base=t->tls;virtual_tpidr=0;
#ifdef AW_NO_ROMFS
    aw_prof_thread=(int)guest_thread_index;
#endif
    tls_stubs_validate_thread(t->entry);
    stack_t alt={0};alt.ss_size=1U<<20;alt.ss_sp=mmap(NULL,alt.ss_size,3,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(alt.ss_sp==MAP_FAILED || sigaltstack(&alt,NULL))stop("THREAD_ALTSTACK_FAILED",t->entry,errno,21);
    /* pthread_create runs inside a SVC trap; never inherit blocked SIGTRAP. */
    sigset_t unblocked;sigemptyset(&unblocked);sigaddset(&unblocked,SIGTRAP);pthread_sigmask(SIG_UNBLOCK,&unblocked,NULL);
    pthread_mutex_lock(&kernel_mutex);t->host_tid=(uint64_t)syscall(SYS_gettid);
    thread_affinity(t,t->core,t->affinity,t->entry);
    struct line b={0};text(&b,"THREAD_ENTER handle=");hex(&b,t->handle);text(&b," entry=");location(&b,t->entry);
    text(&b," argument=");hex(&b,t->argument);text(&b," tls=");hex(&b,t->tls);
    text(&b," guest_id=");hex(&b,t->id);text(&b," host_tid=");hex(&b,t->host_tid);emit(&b);
    pthread_cond_broadcast(&kernel_changed);pthread_mutex_unlock(&kernel_mutex);
    if(!sigsetjmp(t->host_return,1))enter_thread(t->entry,t->stack,t->argument,t->handle);
    stack_t disabled={.ss_flags=SS_DISABLE};sigaltstack(&disabled,NULL);munmap(alt.ss_sp,alt.ss_size);
    return NULL;
}
/* Called with kernel_mutex held. Exit publishes state3 before leaving its
 * signal stack; joining proves host teardown finished before a slot/TLR is
 * reused. An open alias keeps the thread object alive after CloseHandle. */
static void thread_reap(uint64_t pc) {
    for(unsigned i=1;i<nthreads;i++) {
        struct guest_thread *t=&threads[i];if(t->starting || (t->state!=1 && t->state!=3))continue;
        unsigned referenced=0;
        for(unsigned j=0;j<next_handle;j++)if(handles[j].endpoint==EP_THREAD && handles[j].references && handles[j].initialized==i)referenced=1;
        if(referenced)continue;
        if(t->state==3) {int rc=pthread_join(t->native,NULL);if(rc)stop("HOST_THREAD_JOIN_FAILED",pc,rc,21);}
        uint64_t tls=t->tls;*t=(struct guest_thread){.tls=tls};
        /* SVC diagnostic counters belong to the native thread's TLS. A new
         * pthread starts with zero counters; never index the old 32-row
         * global table with one of the 96 admitted guest thread slots. */
    }
}
static uint32_t thread_create(uint64_t entry,uint64_t argument,uint64_t stack,int priority,int core,uint64_t pc) {
    if(!accessible(entry,4,PROT_EXEC) || (entry&3) || (stack&15) ||
       stack<16 || !accessible(stack-16,16,3) || priority<28 || priority>59 || (core!=-2 && (core<0 || core>2)))
        stop("INVALID_THREAD_CREATE",pc,entry,24);
    if(core==-2)core=0;
    thread_reap(pc);unsigned index=1;while(index<nthreads&&threads[index].state)index++;
    if(index==MAX_THREADS)stop("THREAD_TABLE_LIVE_CAPACITY",pc,index,21);
    uint64_t tls=0x70000000ULL+index*PAGE;
    if(!threads[index].tls) {
        pthread_mutex_lock(&regions_mutex);
        region_write_begin(pc);
        void *mapped=mmap((void *)(uintptr_t)tls,PAGE,3,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(mapped==MAP_FAILED || (uintptr_t)mapped!=tls)stop("THREAD_TLS_MAPPING_FAILED",pc,tls,21);
        region_insert((struct region){tls,PAGE,3,12,"guest_thread_local_region",0},pc);
        region_write_end(pc);
        pthread_mutex_unlock(&regions_mutex);
    } else memset((void *)(uintptr_t)tls,0,PAGE);
    uint32_t handle=session_new(EP_THREAD,pc);session_get(handle,pc)->initialized=index;
    threads[index]=(struct guest_thread){.entry=entry,.argument=argument,.stack=stack,.tls=tls,
        .id=next_thread_id++,.affinity=1ULL<<core,.handle=handle,.priority=priority,.core=core,.state=1};
    if(index==nthreads)nthreads++;
    return handle;
}
static void thread_start(uint32_t handle,uint64_t pc) {
    struct guest_thread *t=thread_get(handle,pc);if(t->state!=1)stop("THREAD_NOT_CREATED",pc,handle,24);
    pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,256U<<10);t->state=2;t->starting=1;
    int rc=pthread_create(&t->native,&attr,thread_run,t);pthread_attr_destroy(&attr);
    if(rc)stop("HOST_THREAD_CREATE_FAILED",pc,rc,21);
    /* Guest ID is stable from creation; host TID is separate and diagnostic. */
    while(!t->host_tid)pthread_cond_wait(&kernel_changed,&kernel_mutex);
    t->starting=0;
}
static void thread_exit(uint64_t pc) __attribute__((noreturn));
static void thread_exit(uint64_t pc) {
    if(!guest_thread_index)stop("MAIN_THREAD_EXIT",pc,0,0);
    struct guest_thread *t=&threads[guest_thread_index];t->state=3;
    pthread_cond_broadcast(&kernel_changed);pthread_mutex_unlock(&kernel_mutex);
    siglongjmp(t->host_return,1);
}
static struct timespec timeout_deadline(int64_t ns) {
    struct timespec at;clock_gettime(CLOCK_MONOTONIC,&at);
    if(ns>0){at.tv_sec+=ns/1000000000;at.tv_nsec+=ns%1000000000;if(at.tv_nsec>=1000000000){at.tv_sec++;at.tv_nsec-=1000000000;}}
    return at;
}
static int kernel_wait(int64_t timeout,const struct timespec *deadline) {
    if(timeout<0)return pthread_cond_wait(&kernel_changed,&kernel_mutex);
    if(!timeout)return ETIMEDOUT;
    return pthread_cond_clockwait(&kernel_changed,&kernel_mutex,CLOCK_MONOTONIC,deadline);
}
static uint32_t wait_synchronization(uint64_t address,int count,int64_t timeout,uint32_t *index,uint64_t pc) {
    if(count<0 || count>64 || !accessible(address,(unsigned)count*4,PROT_READ))stop("INVALID_WAIT_HANDLES",pc,address,24);
    uint32_t wait_handles[64];memcpy(wait_handles,(void *)(uintptr_t)address,count*4);
    struct timespec deadline=timeout_deadline(timeout);struct guest_thread *current=&threads[guest_thread_index];
    for(;;) {
        if(current->cancel){current->cancel=0;return 0xec01;}
        for(int i=0;i<count;i++) {
            struct session *s=session_get(wait_handles[i],pc);unsigned ready;
            if(s->endpoint==EP_EVENT)ready=s->initialized;
            else if(s->endpoint==EP_THREAD)ready=threads[s->initialized].state==3;
            else stop("UNSUPPORTED_WAIT_OBJECT",pc,wait_handles[i],20);
            if(ready){*index=i;return 0;}
        }
        int rc=kernel_wait(timeout,&deadline);if(rc==ETIMEDOUT)return 0xea01;
        if(rc)stop("HOST_WAIT_FAILED",pc,rc,21);
    }
}
static uint32_t *mutex_address(uint64_t address,uint64_t pc) {
    if((address&3) || !accessible(address,4,3))stop("INVALID_MUTEX_ADDRESS",pc,address,24);
    return (uint32_t *)(uintptr_t)address;
}
static void mutex_release(uint64_t address,uint64_t pc) {
    uint32_t *word=mutex_address(address,pc);struct guest_thread *next=NULL;unsigned count=0;
    for(unsigned i=0;i<nthreads;i++)if(threads[i].waiting_mutex==address) {
        count++;if(!next || threads[i].priority<next->priority)next=&threads[i];
    }
    __atomic_store_n(word,next?next->waiting_tag|(count>1?0x40000000U:0):0,__ATOMIC_RELEASE);
    if(next)next->waiting_mutex=0;
    pthread_cond_broadcast(&kernel_changed);
}
static void mutex_wait(uint64_t address,uint32_t owner,uint32_t tag,uint64_t pc) {
    uint32_t *word=mutex_address(address,pc);struct guest_thread *current=&threads[guest_thread_index];
    /* SDK 0x96f04..0x96f28 publishes this exact value using LDAXR/STLXR.
     * A fresh acquisition by the same owner without the waiter bit must
     * return: its guest fast unlock will not enter ArbitrateUnlock. */
    if(__atomic_load_n(word,__ATOMIC_ACQUIRE)!=(owner|0x40000000U))return;
    if(tag!=current->handle)stop("INVALID_MUTEX_TAG",pc,tag,24);
    current->waiting_mutex=address;current->waiting_tag=tag;
    while(current->waiting_mutex)pthread_cond_wait(&kernel_changed,&kernel_mutex);
}
/* Horizon publishes waiter presence in the condition-variable word itself: Wait stores 1 there and
 * Signal clears it once the last waiter leaves. nn::os::detail::InternalConditionVariableImplByHorizon
 * ::Signal (sdk+0x13ee7c, MEDIDO) returns WITHOUT calling svcSignalProcessWideKey when that word is
 * zero, so a host that never writes it loses every wake-up (Momodora deadlock, run momo-006). */
static void key_publish(uint64_t key,uint64_t pc) {
    __atomic_store_n(mutex_address(key,pc),1U,__ATOMIC_RELEASE);
}
static void key_retire(uint64_t key,uint64_t pc) {
    for(unsigned i=0;i<nthreads;i++)if(threads[i].waiting_key==key && !threads[i].key_ready)return;
    __atomic_store_n(mutex_address(key,pc),0U,__ATOMIC_RELEASE);
}
static uint32_t key_wait(uint64_t mutex,uint64_t key,uint32_t tag,int64_t timeout,uint64_t pc) {
    mutex_address(key,pc);struct guest_thread *current=&threads[guest_thread_index];
    if(tag!=current->handle)stop("INVALID_CONDITION_TAG",pc,tag,24);
    current->waiting_key=key;current->key_ready=0;key_publish(key,pc);mutex_release(mutex,pc);
    struct timespec deadline=timeout_deadline(timeout);uint32_t result=0;
    while(!current->key_ready) {
        int rc=kernel_wait(timeout,&deadline);
        if(rc==ETIMEDOUT && !current->key_ready){result=0xea01;break;}
        if(rc && rc!=ETIMEDOUT)stop("HOST_CONDITION_WAIT_FAILED",pc,rc,21);
    }
    /* SDK TimedWait 0x96a78..0x96a7c calls Enter itself after 0xea01.
     * Reacquiring here on timeout makes that original Enter self-deadlock. */
    current->waiting_key=0;key_retire(key,pc);if(result)return result;
    uint32_t *word=mutex_address(mutex,pc);
    for(;;) {
        uint32_t expected=0;
        if(__atomic_compare_exchange_n(word,&expected,tag,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))break;
        uint32_t owner=expected&0xbfffffffU;
        /* Guest fast unlocks run outside kernel_mutex. Publish the waiter
         * bit only if the observed owner is still present; fetch_or could
         * otherwise change a concurrently released zero into OWNER=0. */
        if(!__atomic_compare_exchange_n(word,&expected,expected|0x40000000U,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))continue;
        mutex_wait(mutex,owner,tag,pc);
        if((__atomic_load_n(word,__ATOMIC_ACQUIRE)&0xbfffffffU)==tag)break;
    }
    return result;
}
static void key_signal(uint64_t key,int count) {
    for(unsigned i=0;i<nthreads && count!=0;i++)if(threads[i].waiting_key==key && !threads[i].key_ready) {
        threads[i].key_ready=1;if(count>0)count--;
    }
    key_retire(key,0);
    pthread_cond_broadcast(&kernel_changed);
}
