/* Terminator 2D: the two memory guards the house requires on a 916 MiB device.
 *
 * 1. MemAvailable floor, polled every 250 ms by a host thread. Mali-450 texture memory comes out of
 *    system RAM and is invisible in VmRSS; once the kernel starts paging to the SD card nothing
 *    supervises the device any more (Rivals lesson, rivals-switch-study/native/rivals_memory_floor.h
 *    @ 4d3601b, floor 120 MiB there; the root agent set 140 MiB for this title). The host exits
 *    with a readable STOP instead of taking the device down.
 *    T2_MEMORY_FLOOR_MIB=<n> overrides; 0 turns the floor off (PC runs under qemu).
 *
 * 2. Texture byte ceiling inside the host: every NVN texture storage accepted by TextureInitialize is
 *    added, every TextureFinalize subtracts, and crossing the budget stops the run. The budget is the
 *    number the device can actually give the GPU, not the RGBA8 total the game would like
 *    (12 pages x 64 MiB = 768 MiB; 384 MiB as idx16). T2_TEXTURE_BUDGET_MIB=<n> overrides; 0 disables.
 *    Default 420 MiB = the Rivals budget on the same device class (root agent, 25/09/2026).
 */
#ifndef T2_MEMWATCH_H
#define T2_MEMWATCH_H
#include <limits.h>
#include <malloc.h>

#define T2_MEMORY_FLOOR_DEFAULT_MIB 140U
#define T2_TEXTURE_BUDGET_DEFAULT_MIB 420U
#define T2_MEMWATCH_PERIOD_NS 250000000L

/* 3. Swap-aware pressure (Frente D, 27/09/2026). MEASURED on dArkOS .138 (630 MiB + 512 MiB zram):
 *    the kernel OOM killer took the game (status 137, "Free swap = 0kB") while MemAvailable never went
 *    under the 16 MiB floor, because once zram is full the anonymous pages cannot move and MemAvailable
 *    only reports file cache that the game itself is thrashing. The watcher now also reads SwapFree:
 *      level 1 (tight)    MemAvailable < floor+tight margin, or swap present and SwapFree < 25% of it
 *                         -> T2_MEMORY_PRESSURE, host heap trimmed, t2_memwatch_pressure() = 1 for
 *                            modules that can release caches (pages are never evicted by this file).
 *      level 2 (critical) swap present, SwapFree < swap floor AND MemAvailable < floor+tight margin
 *                         -> readable STOP T2_MEMORY_EXHAUSTED (status 26) instead of an anonymous SIGKILL.
 *    Decided only by measured counters (SwapTotal, SwapFree, MemAvailable): devices without swap
 *    (Mali-450 NextOS, SwapTotal 0) keep exactly the old floor behaviour.
 *    T2_SWAP_FLOOR_MIB=<n> (default 16; 0 = never stop on swap), T2_MEMORY_TIGHT_MIB=<n> (default 48).
 *    A T2_MEMWATCH_SAMPLE line every 10 s keeps the owner's log readable after a crash. */
#define T2_SWAP_FLOOR_DEFAULT_MIB 16U
#define T2_MEMORY_TIGHT_DEFAULT_MIB 48U
#define T2_MEMWATCH_SAMPLE_CHECKS 40U

struct t2_meminfo {unsigned long available_kib,swap_total_kib,swap_free_kib;};
static struct {unsigned configured,floor_mib,budget_mib,swap_floor_mib,tight_mib;unsigned long lowest_kib,lowest_swap_kib;
               uint64_t checks;int level;unsigned calm;} t2_memwatch;
static struct {uint64_t live_bytes,peak_bytes,count;} t2_texture_budget;
static pthread_mutex_t t2_texture_budget_mutex=PTHREAD_MUTEX_INITIALIZER;

static unsigned t2_memwatch_mib(const char *name,unsigned fallback) {
    const char *v=getenv(name);
    if(!v)return fallback;
    char *end=NULL;unsigned long n=strtoul(v,&end,10);
    if(!*v || (end && *end) || n>4096)stop("T2_MEMWATCH_INVALID_SETTING",0,n,24);
    return (unsigned)n;
}
static void t2_memwatch_configure(void) {
    if(t2_memwatch.configured)return;
    t2_memwatch.configured=1;
    t2_memwatch.floor_mib=t2_memwatch_mib("T2_MEMORY_FLOOR_MIB",T2_MEMORY_FLOOR_DEFAULT_MIB);
    t2_memwatch.budget_mib=t2_memwatch_mib("T2_TEXTURE_BUDGET_MIB",T2_TEXTURE_BUDGET_DEFAULT_MIB);
    t2_memwatch.swap_floor_mib=t2_memwatch_mib("T2_SWAP_FLOOR_MIB",T2_SWAP_FLOOR_DEFAULT_MIB);
    t2_memwatch.tight_mib=t2_memwatch_mib("T2_MEMORY_TIGHT_MIB",T2_MEMORY_TIGHT_DEFAULT_MIB);
    t2_memwatch.lowest_kib=ULONG_MAX;t2_memwatch.lowest_swap_kib=ULONG_MAX;
    printf("T2_MEMWATCH floor-MiB=%u swap-floor-MiB=%u tight-MiB=%u period-ms=250 source=/proc/meminfo:MemAvailable+SwapFree texture-budget-MiB=%u reason=gpu-memory-is-invisible-to-vmrss\n",
           t2_memwatch.floor_mib,t2_memwatch.swap_floor_mib,t2_memwatch.tight_mib,t2_memwatch.budget_mib);
}
static unsigned long t2_meminfo_field(const char *buffer,const char *key) {
    const char *at=strstr(buffer,key);
    return at?strtoul(at+strlen(key),NULL,10):0;
}
#ifndef T2_MEMWATCH_MEMINFO_PATH
#define T2_MEMWATCH_MEMINFO_PATH "/proc/meminfo"
#endif
static int t2_meminfo_read(struct t2_meminfo *m) {
    int fd=open(T2_MEMWATCH_MEMINFO_PATH,O_RDONLY|O_CLOEXEC);
    if(fd<0)return -1;
    char buffer[4096];ssize_t n=read(fd,buffer,sizeof(buffer)-1);close(fd);
    if(n<=0)return -1;
    buffer[n]=0;
    m->available_kib=t2_meminfo_field(buffer,"MemAvailable:");
    m->swap_total_kib=t2_meminfo_field(buffer,"SwapTotal:");
    m->swap_free_kib=t2_meminfo_field(buffer,"SwapFree:");
    return m->available_kib?0:-1;
}
static unsigned long t2_mem_available_kib(void) {
    struct t2_meminfo m;return t2_meminfo_read(&m)?0:m.available_kib;
}
/* Pure classification: 0 normal, 1 tight, 2 critical (swap exhausted), 3 below the MemAvailable floor. */
static int t2_memwatch_classify(const struct t2_meminfo *m,unsigned floor_mib,unsigned swap_floor_mib,unsigned tight_mib) {
    unsigned long available=m->available_kib>>10,swap_free=m->swap_free_kib>>10,swap_total=m->swap_total_kib>>10;
    if(floor_mib && available<floor_mib)return 3;
    int low=available<(unsigned long)floor_mib+tight_mib;
    if(swap_total) {
        if(swap_floor_mib && swap_free<swap_floor_mib && low)return 2;
        if(swap_free*4<swap_total)return 1;
    }
    return low?1:0;
}
static volatile int t2_memwatch_pressure_level;
__attribute__((unused)) static int t2_memwatch_pressure(void){return __atomic_load_n(&t2_memwatch_pressure_level,__ATOMIC_RELAXED);}
static void t2_memwatch_self_status(unsigned long *rss_kib,unsigned long *swap_kib) {
    *rss_kib=*swap_kib=0;
    int fd=open("/proc/self/status",O_RDONLY|O_CLOEXEC);
    if(fd<0)return;
    char buffer[4096];ssize_t n=read(fd,buffer,sizeof(buffer)-1);close(fd);
    if(n<=0)return;
    buffer[n]=0;*rss_kib=t2_meminfo_field(buffer,"VmRSS:");*swap_kib=t2_meminfo_field(buffer,"VmSwap:");
}
static void t2_memwatch_step(const struct t2_meminfo *m) {
    unsigned long available=m->available_kib;
    t2_memwatch.checks++;
    if(available<t2_memwatch.lowest_kib) {
        t2_memwatch.lowest_kib=available;
        if((available>>10)<=t2_memwatch.floor_mib+64)
            printf("T2_MEMORY_LOW available-MiB=%lu floor-MiB=%u swap-free-MiB=%lu checks=%llu\n",available>>10,t2_memwatch.floor_mib,
                   m->swap_free_kib>>10,(unsigned long long)t2_memwatch.checks);
    }
    if(m->swap_total_kib && m->swap_free_kib<t2_memwatch.lowest_swap_kib)t2_memwatch.lowest_swap_kib=m->swap_free_kib;
    if(!(t2_memwatch.checks%T2_MEMWATCH_SAMPLE_CHECKS)) {
        unsigned long rss,swap;t2_memwatch_self_status(&rss,&swap);
        printf("T2_MEMWATCH_SAMPLE checks=%llu available-MiB=%lu swap-free-MiB=%lu swap-total-MiB=%lu vmrss-MiB=%lu vmswap-MiB=%lu level=%d\n",
               (unsigned long long)t2_memwatch.checks,available>>10,m->swap_free_kib>>10,m->swap_total_kib>>10,rss>>10,swap>>10,t2_memwatch.level);
    }
    int level=t2_memwatch_classify(m,t2_memwatch.floor_mib,t2_memwatch.swap_floor_mib,t2_memwatch.tight_mib);
    if(level==3) {
        printf("T2_MEMORY_FLOOR_HIT available-MiB=%lu floor-MiB=%u swap-free-MiB=%lu action=exit\n",available>>10,t2_memwatch.floor_mib,m->swap_free_kib>>10);
        fflush(NULL);
        stop("T2_MEMORY_FLOOR",0,available>>10,26);
    }
    if(level==2) {
        printf("T2_MEMORY_EXHAUSTED available-MiB=%lu swap-free-MiB=%lu swap-total-MiB=%lu swap-floor-MiB=%u action=exit reason=swap-full-oom-imminent\n",
               available>>10,m->swap_free_kib>>10,m->swap_total_kib>>10,t2_memwatch.swap_floor_mib);
        fflush(NULL);
        stop("T2_MEMORY_EXHAUSTED",0,m->swap_free_kib>>10,26);
    }
    /* Rise at once; fall only after 10 s (40 checks) below the current level, so a boundary does not spam. */
    if(level<t2_memwatch.level) {
        if(++t2_memwatch.calm<T2_MEMWATCH_SAMPLE_CHECKS)level=t2_memwatch.level;
    } else t2_memwatch.calm=0;
    if(level!=t2_memwatch.level) {
        t2_memwatch.calm=0;
        unsigned long rss,swap;t2_memwatch_self_status(&rss,&swap);
        printf("T2_MEMORY_PRESSURE level=%d previous=%d available-MiB=%lu swap-free-MiB=%lu swap-total-MiB=%lu vmrss-MiB=%lu vmswap-MiB=%lu\n",
               level,t2_memwatch.level,available>>10,m->swap_free_kib>>10,m->swap_total_kib>>10,rss>>10,swap>>10);
        if(level>t2_memwatch.level)malloc_trim(0);
        t2_memwatch.level=level;
        __atomic_store_n(&t2_memwatch_pressure_level,level,__ATOMIC_RELAXED);
    }
}
/* Frente A (27/09/2026): host malloc arenas keep freed pages. Transient buffers of the replay/texture paths
 * (0.1-17 MiB) land in thread arenas once glibc raises its dynamic mmap threshold, and freed space below the
 * arena top is never returned: on the dArkOS device a 41 MiB arena mapping sat 38 MiB in zram in gameplay
 * (MEDIDO smaps, resume-fast/darkos-gameplay.smaps). malloc_trim(0) madvises every whole free page of every
 * arena back to the kernel; it changes no live allocation. T2_HOST_TRIM_MS=<n> (default 2000, 0 = off). */
#include <malloc.h>
static struct {unsigned configured,period_checks;uint64_t runs,ns_total,ns_max,released;} t2_host_trim;
static void t2_host_trim_tick(uint64_t checks) {
    if(!t2_host_trim.configured) {
        t2_host_trim.configured=1;
        unsigned ms=t2_memwatch_mib("T2_HOST_TRIM_MS",2000);
        t2_host_trim.period_checks=ms?(ms+249)/250:0;
        printf("T2_HOST_TRIM period-ms=%u scope=glibc-arenas-free-pages\n",ms);
    }
    if(!t2_host_trim.period_checks || checks%t2_host_trim.period_checks)return;
    struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);
    int released=malloc_trim(0);
    clock_gettime(CLOCK_MONOTONIC,&b);
    uint64_t ns=(uint64_t)(b.tv_sec-a.tv_sec)*1000000000ull+(uint64_t)(b.tv_nsec-a.tv_nsec);
    t2_host_trim.runs++;t2_host_trim.ns_total+=ns;if(ns>t2_host_trim.ns_max)t2_host_trim.ns_max=ns;
    t2_host_trim.released+=released>0;
    if(t2_host_trim.runs%30==0)printf("T2_HOST_TRIM runs=%llu released-runs=%llu avg-us=%llu max-us=%llu\n",
        (unsigned long long)t2_host_trim.runs,(unsigned long long)t2_host_trim.released,
        (unsigned long long)(t2_host_trim.ns_total/t2_host_trim.runs/1000),(unsigned long long)(t2_host_trim.ns_max/1000));
}
static void *t2_memwatch_thread(void *argument) {
    (void)argument;
    struct timespec pause={0,T2_MEMWATCH_PERIOD_NS};
    for(uint64_t tick=1;;tick++) {
        nanosleep(&pause,NULL);
        t2_host_trim_tick(tick);
        struct t2_meminfo m;
        if(t2_meminfo_read(&m))continue;
        t2_memwatch_step(&m);
    }
    return NULL;
}
static void t2_memwatch_start(void) {
    t2_memwatch_configure();
    if(!t2_memwatch.floor_mib){printf("T2_MEMWATCH floor=off\n");return;}
    pthread_t thread;pthread_attr_t attr;
    if(pthread_attr_init(&attr) || pthread_attr_setstacksize(&attr,256*1024) ||
       pthread_create(&thread,&attr,t2_memwatch_thread,NULL))stop("T2_MEMWATCH_THREAD",0,errno,21);
    pthread_attr_destroy(&attr);
}
/* Called by the NVN texture path with the bridge lock held; the mutex only protects the counters. */
static void t2_texture_budget_add(uint64_t bytes,uint64_t caller) {
    t2_memwatch_configure();
    pthread_mutex_lock(&t2_texture_budget_mutex);
    t2_texture_budget.live_bytes+=bytes;t2_texture_budget.count++;
    if(t2_texture_budget.live_bytes>t2_texture_budget.peak_bytes)t2_texture_budget.peak_bytes=t2_texture_budget.live_bytes;
    uint64_t live=t2_texture_budget.live_bytes,count=t2_texture_budget.count;
    pthread_mutex_unlock(&t2_texture_budget_mutex);
    if(count<=8 || !(count&(count-1)) || bytes>=(16ULL<<20))
        printf("T2_TEXTURE_BUDGET add-bytes=%llu live-MiB=%llu count=%llu budget-MiB=%u\n",
               (unsigned long long)bytes,(unsigned long long)(live>>20),(unsigned long long)count,t2_memwatch.budget_mib);
    if(t2_memwatch.budget_mib && live>((uint64_t)t2_memwatch.budget_mib<<20)) {
        printf("T2_TEXTURE_BUDGET_EXCEEDED live-MiB=%llu budget-MiB=%u last-bytes=%llu action=exit\n",
               (unsigned long long)(live>>20),t2_memwatch.budget_mib,(unsigned long long)bytes);
        fflush(NULL);
        stop("T2_TEXTURE_BUDGET",caller,live>>20,26);
    }
}
static void t2_texture_budget_sub(uint64_t bytes,uint64_t caller) {
    pthread_mutex_lock(&t2_texture_budget_mutex);
    if(t2_texture_budget.live_bytes<bytes){pthread_mutex_unlock(&t2_texture_budget_mutex);stop("T2_TEXTURE_BUDGET_UNDERFLOW",caller,bytes,24);}
    t2_texture_budget.live_bytes-=bytes;
    pthread_mutex_unlock(&t2_texture_budget_mutex);
}
#endif
