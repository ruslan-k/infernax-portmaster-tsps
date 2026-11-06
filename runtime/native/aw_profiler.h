/* AW_PROFILE=1: process-wide SIGPROF sampler (1 ms of CPU). Each sample records the interrupted pc, bucketed by
 * 256-byte function-ish ranges, split by thread kind (guest thread index or host thread). Every 10 s the top
 * buckets are printed with their module offset (main/sdk/rtld) or host binary offset (resolve with addr2line). */
#ifndef AW_PROFILER_H
#define AW_PROFILER_H
#include <sys/time.h>
#include <dlfcn.h>
#define AW_PROF_SLOTS 4096U
static struct {uint64_t key[AW_PROF_SLOTS];uint32_t hits[AW_PROF_SLOTS];uint64_t total,host_self_base;} aw_prof;
static _Thread_local int aw_prof_thread=-2;
static void aw_prof_handler(int sig,siginfo_t *info,void *opaque) {
    (void)sig;(void)info;ucontext_t *ctx=opaque;uint64_t pc=ctx->uc_mcontext.pc;
    int kind=aw_prof_thread; /* -2 host unknown, >=0 guest thread index */
    uint64_t key=((uint64_t)(uint8_t)(kind+2)<<56)|(pc>>8<<8&0x00ffffffffffffffULL);
    unsigned slot=(unsigned)((key>>8)*2654435761U)&(AW_PROF_SLOTS-1);
    for(unsigned i=0;i<16;i++){unsigned k=(slot+i)&(AW_PROF_SLOTS-1);if(aw_prof.key[k]==key||!aw_prof.key[k]){aw_prof.key[k]=key;aw_prof.hits[k]++;break;}}
    aw_prof.total++;
}
static void aw_prof_where(uint64_t pc,char *out,size_t n) {
    if(pc>=LC2_MAIN_BASE&&pc<LC2_MAIN_BASE+LC2_MAIN_IMAGE_SIZE)snprintf(out,n,"main+0x%"PRIx64,(uint64_t)(pc-LC2_MAIN_BASE));
    else if(pc>=LC2_SDK_BASE&&pc<LC2_SDK_BASE+LC2_SDK_IMAGE_SIZE)snprintf(out,n,"sdk+0x%"PRIx64,(uint64_t)(pc-LC2_SDK_BASE));
    else if(pc>=LC2_SUBSDK0_BASE&&pc<LC2_SUBSDK0_BASE+LC2_SUBSDK0_IMAGE_SIZE)snprintf(out,n,"subsdk0+0x%"PRIx64,(uint64_t)(pc-LC2_SUBSDK0_BASE));
    else if(pc>=LC2_RTLD_BASE&&pc<LC2_RTLD_BASE+LC2_RTLD_IMAGE_SIZE)snprintf(out,n,"rtld+0x%"PRIx64,(uint64_t)(pc-LC2_RTLD_BASE));
    else {Dl_info d;if(dladdr((void *)(uintptr_t)pc,&d)&&d.dli_fname){const char *b=strrchr(d.dli_fname,'/');snprintf(out,n,"%s%s+0x%"PRIx64,b?b+1:d.dli_fname,d.dli_sname?"":"",pc-(uint64_t)(uintptr_t)d.dli_saddr);
            if(d.dli_sname)snprintf(out,n,"%s:%s",b?b+1:d.dli_fname,d.dli_sname);}
        else snprintf(out,n,"host+0x%"PRIx64,pc);}
}
static void *aw_prof_reporter(void *unused) {
    (void)unused;
    for(;;) {
        sleep(10);
        uint64_t total=aw_prof.total;if(!total)continue;
        uint64_t keys[AW_PROF_SLOTS];uint32_t hits[AW_PROF_SLOTS];memcpy(keys,aw_prof.key,sizeof(keys));memcpy(hits,aw_prof.hits,sizeof(hits));
        memset(aw_prof.key,0,sizeof(aw_prof.key));memset(aw_prof.hits,0,sizeof(aw_prof.hits));aw_prof.total=0;
        uint64_t per_kind[8]={0};for(unsigned i=0;i<AW_PROF_SLOTS;i++)if(keys[i])per_kind[(keys[i]>>56)&7]+=hits[i];
        printf("AW_PROFILE samples=%"PRIu64" host-unnamed=%"PRIu64" guest0=%"PRIu64" guest1=%"PRIu64" guest2=%"PRIu64" guest3=%"PRIu64"\n",total,per_kind[0],per_kind[2],per_kind[3],per_kind[4],per_kind[5]);
        for(unsigned rank=0;rank<40;rank++) {
            unsigned best=AW_PROF_SLOTS;for(unsigned i=0;i<AW_PROF_SLOTS;i++)if(keys[i]&&(best==AW_PROF_SLOTS||hits[i]>hits[best]))best=i;
            if(best==AW_PROF_SLOTS||!hits[best])break;
            char where[160];aw_prof_where(keys[best]&0x00ffffffffffffffULL,where,sizeof(where));
            int kind=(int)((keys[best]>>56)&0xff)-2;
            printf("AW_PROFILE_TOP rank=%u thread=%d pct=%.1f at=%s\n",rank,kind,100.0*hits[best]/total,where);keys[best]=0;
        }
    }
    return NULL;
}
static void aw_prof_start(void) {
    const char *o=getenv("AW_PROFILE");if(!o||strcmp(o,"1"))return;
    struct sigaction sa={0};sa.sa_sigaction=aw_prof_handler;sa.sa_flags=SA_SIGINFO|SA_RESTART|SA_ONSTACK;sigemptyset(&sa.sa_mask);sigaction(SIGPROF,&sa,NULL);
    struct itimerval it={{0,1000},{0,1000}};setitimer(ITIMER_PROF,&it,NULL);
    pthread_t t;pthread_create(&t,NULL,aw_prof_reporter,NULL);pthread_detach(t);
    printf("AW_PROFILE enabled=1 period=1ms-cpu scope=process\n");
}
#endif
