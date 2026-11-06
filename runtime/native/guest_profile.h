#ifndef COTM_GUEST_PROFILE_H
#define COTM_GUEST_PROFILE_H
/* D19: calling-thread counters and sampled timing, never shared snapshots.
 * Configure in main before workers/signals can enter guest code. The signal
 * path has no option lookup, allocation, printing or additional mutex. */
#define GUEST_PROFILE_IPC_SLOTS 32U
#define GUEST_OTHER_SLOTS 32U
struct guest_other_bucket {
    uint64_t pc,lr,calls,returns,wall_ns,cpu_ns,lock_ns,samples,cpu_samples,lock_samples;
    uint32_t instruction;unsigned used,size_class,byte_zero;
};
static unsigned guest_other_enabled;
static uint64_t guest_other_clock_cost[2];
static _Thread_local struct {
    struct guest_other_bucket buckets[GUEST_OTHER_SLOTS];
    uint64_t calls,returns,overflow,missing_context,clock_calls,clock_errors,samples,wall_ns,cpu_ns,lock_ns,cpu_samples,lock_samples;
    unsigned remaining,rng,ready;
} guest_other;
struct guest_profile_timing {
    uint64_t lock_ns,lock_max_ns,dispatch_ns,dispatch_max_ns,cpu_ns;
    uint64_t lock_samples,dispatch_samples,cpu_samples;
};
struct guest_profile_svc {
    uint64_t calls,returns;struct guest_profile_timing timing;unsigned remaining;
};
struct guest_profile_ipc {
    unsigned used,endpoint,command,close,type;const char *category;
    uint64_t calls;struct guest_profile_timing timing;
};
struct guest_profile_scope {
    unsigned active,sampled,svc,wall_valid,locked_valid,cpu_valid;
    uint64_t before_ns,locked_ns,cpu_ns;
    struct guest_profile_scope *previous;struct guest_profile_ipc *ipc;
    struct guest_other_bucket *other;
};
static unsigned guest_profile_enabled;
static _Thread_local struct {
    struct guest_profile_svc svc[256];struct guest_profile_ipc ipc[GUEST_PROFILE_IPC_SLOTS];
    uint64_t cnt_ticks,cnt_frequency,tls_reads,tls_writes,other_traps,ipc_calls,ipc_overflow,clock_errors,reentrant;
    uint64_t interval_cpu_ns;unsigned interval_cpu_valid,interval_ready,rng;
    struct guest_profile_scope *current;
} guest_profile;
static void guest_profile_configure(void) {
    const char *option=getenv("COTM_GUEST_PROFILE");
    if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("GUEST_PROFILE_OPTION",0,0,24);
    guest_profile_enabled=option&&!strcmp(option,"1");
    if(guest_profile_enabled)printf("GUEST_PROFILE_CONFIG enabled=1 counters=calling-thread timing=sampled-SVC jitter=64..191 clocks=monotonic-and-thread-cpu ipc-slots=32 extra-mutex=0 hid-cache=0\n");
    option=getenv("MEGAMAN_OTHER_TRAP_PROFILE");
    if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("GUEST_OTHER_PROFILE_OPTION",0,0,24);
    guest_other_enabled=option&&!strcmp(option,"1");
    if(guest_other_enabled) {
        /* Startup-only estimate of clock-call wall cost. No correction is
         * subtracted from samples; no clock calibration runs in the signal. */
        for(unsigned kind=0;kind<2;kind++) {
            struct timespec before,after,sample;int error=clock_gettime(CLOCK_MONOTONIC,&before);
            for(unsigned i=0;i<64;i++)error|=clock_gettime(kind?CLOCK_THREAD_CPUTIME_ID:CLOCK_MONOTONIC,&sample);
            error|=clock_gettime(CLOCK_MONOTONIC,&after);
            if(error || after.tv_sec<before.tv_sec)guest_other_clock_cost[kind]=UINT64_MAX;
            else {uint64_t begin=(uint64_t)before.tv_sec*1000000000ULL+before.tv_nsec;
                uint64_t end=(uint64_t)after.tv_sec*1000000000ULL+after.tv_nsec;
                guest_other_clock_cost[kind]=end>=begin?(end-begin)/64:UINT64_MAX;}
        }
        printf("GUEST_OTHER_PROFILE_CONFIG enabled=1 slots=32 key=pc-lr-arg2class-arg1bytezero jitter=16..47 clocks-per-sample=5 startup-clock-monotonic-ns=%"PRIu64" startup-clock-thread-cpu-ns=%"PRIu64" overhead=subtracted-never extra-mutex=0 signal-stdio=0 scope=calling-thread\n",
            guest_other_clock_cost[0],guest_other_clock_cost[1]);
    }
}
static unsigned guest_profile_clock(clockid_t clock,uint64_t *out) {
    int saved_errno=errno;struct timespec time;int rc=clock_gettime(clock,&time);errno=saved_errno;
    if(rc||time.tv_sec<0||time.tv_nsec<0||time.tv_nsec>=1000000000L||
       (uint64_t)time.tv_sec>(UINT64_MAX-(uint64_t)time.tv_nsec)/1000000000ULL) {
        guest_profile.clock_errors++;return 0;
    }
    *out=(uint64_t)time.tv_sec*1000000000ULL+(uint64_t)time.tv_nsec;return 1;
}
static unsigned guest_profile_next(void) {
    unsigned x=guest_profile.rng;if(!x)x=0x9e3779b9U^(guest_thread_index+1U)*0x85ebca6bU;
    x^=x<<13;x^=x>>17;x^=x<<5;guest_profile.rng=x;return 64U+((x>>8)&127U);
}
static void guest_profile_register(unsigned kind) {
    if(!guest_profile_enabled)return;
    if(kind==0)guest_profile.cnt_ticks++;
    else if(kind==1)guest_profile.cnt_frequency++;
    else if(kind==2)guest_profile.tls_reads++;
    else if(kind==3)guest_profile.tls_writes++;
}
static unsigned guest_other_size_class(uint64_t size) {
    return !size?0:size<64?1:size<4096?2:size<(1U<<20)?3:4;
}
static unsigned guest_other_clock(clockid_t clock,uint64_t *out) {
    int saved_errno=errno;struct timespec time;guest_other.clock_calls++;
    int rc=clock_gettime(clock,&time);errno=saved_errno;
    if(rc||time.tv_sec<0||time.tv_nsec<0||time.tv_nsec>=1000000000L||
       (uint64_t)time.tv_sec>(UINT64_MAX-(uint64_t)time.tv_nsec)/1000000000ULL) {
        guest_other.clock_errors++;return 0;
    }
    *out=(uint64_t)time.tv_sec*1000000000ULL+time.tv_nsec;return 1;
}
static void guest_other_begin(struct guest_profile_scope *scope,uint32_t instruction) {
    if(!guest_other_enabled)return;
    guest_other.calls++;
    *scope=(struct guest_profile_scope){.active=2,.previous=guest_profile.current};
    guest_profile.current=scope;
    /* signal_handler installs this pointer before guest_profile_begin.
     * Read register values only; never dereference an argument or change it. */
    if(current_guest_context) {
        uint64_t pc=current_guest_context->uc_mcontext.pc,lr=current_guest_context->uc_mcontext.regs[30];
        unsigned size_class=guest_other_size_class(current_guest_context->uc_mcontext.regs[2]);
        unsigned byte_zero=!(current_guest_context->uc_mcontext.regs[1]&255U);
        unsigned hash=(unsigned)((pc>>2)^(lr>>4)^(size_class*7U)^byte_zero)%GUEST_OTHER_SLOTS;
        for(unsigned i=0;i<GUEST_OTHER_SLOTS;i++) {
            struct guest_other_bucket *b=&guest_other.buckets[(hash+i)%GUEST_OTHER_SLOTS];
            if(!b->used)*b=(struct guest_other_bucket){.used=1,.pc=pc,.lr=lr,.instruction=instruction,.size_class=size_class,.byte_zero=byte_zero};
            if(b->pc==pc&&b->lr==lr&&b->size_class==size_class&&b->byte_zero==byte_zero){b->calls++;scope->other=b;break;}
        }
        if(!scope->other)guest_other.overflow++;
    } else guest_other.missing_context++;
    if(guest_other.remaining){guest_other.remaining--;return;}
    unsigned rng=guest_other.rng;if(!rng)rng=0x7f4a7c15U^(guest_thread_index+1U)*0x85ebca6bU;
    rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;guest_other.rng=rng;
    guest_other.remaining=15U+((rng>>8)&31U);scope->sampled=1;
    scope->wall_valid=guest_other_clock(CLOCK_MONOTONIC,&scope->before_ns);
    scope->cpu_valid=guest_other_clock(CLOCK_THREAD_CPUTIME_ID,&scope->cpu_ns);
}
static void guest_profile_begin(struct guest_profile_scope *scope,uint32_t instruction) {
    scope->active=0;if(!guest_profile_enabled&&!guest_other_enabled)return;
    if((instruction&0xffe0001fU)!=0xd4000001U || ((instruction>>5)&0xffffU)>=256) {
        if(guest_profile_enabled)guest_profile.other_traps++;
        guest_other_begin(scope,instruction);return;
    }
    if(!guest_profile_enabled)return;
    unsigned svc=(instruction>>5)&255U;struct guest_profile_svc *bucket=&guest_profile.svc[svc];bucket->calls++;
    *scope=(struct guest_profile_scope){.active=1,.svc=svc,.previous=guest_profile.current};
    if(guest_profile.current)guest_profile.reentrant++;
    guest_profile.current=scope;
    if(bucket->remaining){bucket->remaining--;return;}
    bucket->remaining=guest_profile_next()-1;scope->sampled=1;
    scope->wall_valid=guest_profile_clock(CLOCK_MONOTONIC,&scope->before_ns);
}
static void guest_profile_locked(struct guest_profile_scope *scope) {
    if(!scope->active||!scope->sampled)return;
    if(scope->active==2){scope->locked_valid=guest_other_clock(CLOCK_MONOTONIC,&scope->locked_ns);return;}
    scope->locked_valid=guest_profile_clock(CLOCK_MONOTONIC,&scope->locked_ns);
    scope->cpu_valid=guest_profile_clock(CLOCK_THREAD_CPUTIME_ID,&scope->cpu_ns);
}
static void guest_profile_ipc_call(unsigned endpoint,unsigned command,unsigned close,unsigned type,const char *category) {
    if(!guest_profile_enabled)return;
    guest_profile.ipc_calls++;unsigned hash=(endpoint*131U+command*17U+close*7U+type)%GUEST_PROFILE_IPC_SLOTS;
    for(unsigned i=0;i<GUEST_PROFILE_IPC_SLOTS;i++) {
        struct guest_profile_ipc *bucket=&guest_profile.ipc[(hash+i)%GUEST_PROFILE_IPC_SLOTS];
        if(!bucket->used){bucket->used=1;bucket->endpoint=endpoint;bucket->command=command;bucket->close=close;bucket->type=type;bucket->category=category;}
        if(bucket->endpoint==endpoint&&bucket->command==command&&bucket->close==close&&bucket->type==type) {
            bucket->calls++;if(guest_profile.current)guest_profile.current->ipc=bucket;return;
        }
    }
    guest_profile.ipc_overflow++;if(guest_profile.current)guest_profile.current->ipc=NULL;
}
static void guest_profile_accumulate(struct guest_profile_timing *timing,const struct guest_profile_timing *sample) {
    timing->lock_ns+=sample->lock_ns;timing->dispatch_ns+=sample->dispatch_ns;timing->cpu_ns+=sample->cpu_ns;
    timing->lock_samples+=sample->lock_samples;timing->dispatch_samples+=sample->dispatch_samples;timing->cpu_samples+=sample->cpu_samples;
    if(sample->lock_max_ns>timing->lock_max_ns)timing->lock_max_ns=sample->lock_max_ns;
    if(sample->dispatch_max_ns>timing->dispatch_max_ns)timing->dispatch_max_ns=sample->dispatch_max_ns;
}
static void guest_profile_end(struct guest_profile_scope *scope) {
    if(!scope->active)return;
    if(scope->active==2) {
        struct guest_other_bucket *bucket=scope->other;guest_other.returns++;if(bucket)bucket->returns++;
        if(scope->sampled) {
            uint64_t cpu=0,wall=0;unsigned cpu_ok=guest_other_clock(CLOCK_THREAD_CPUTIME_ID,&cpu);
            unsigned wall_ok=guest_other_clock(CLOCK_MONOTONIC,&wall);
            if(scope->wall_valid&&wall_ok&&wall>=scope->before_ns) {
                uint64_t elapsed=wall-scope->before_ns;guest_other.wall_ns+=elapsed;guest_other.samples++;
                if(bucket){bucket->wall_ns+=elapsed;bucket->samples++;}
            } else if(scope->wall_valid&&wall_ok)guest_other.clock_errors++;
            if(scope->cpu_valid&&cpu_ok&&cpu>=scope->cpu_ns) {
                uint64_t elapsed=cpu-scope->cpu_ns;guest_other.cpu_ns+=elapsed;guest_other.cpu_samples++;
                if(bucket){bucket->cpu_ns+=elapsed;bucket->cpu_samples++;}
            } else if(scope->cpu_valid&&cpu_ok)guest_other.clock_errors++;
            if(scope->wall_valid&&scope->locked_valid&&scope->locked_ns>=scope->before_ns) {
                uint64_t elapsed=scope->locked_ns-scope->before_ns;guest_other.lock_ns+=elapsed;guest_other.lock_samples++;
                if(bucket){bucket->lock_ns+=elapsed;bucket->lock_samples++;}
            } else if(scope->wall_valid&&scope->locked_valid)guest_other.clock_errors++;
        }
        guest_profile.current=scope->previous;scope->active=0;return;
    }
    struct guest_profile_svc *bucket=&guest_profile.svc[scope->svc];bucket->returns++;
    if(scope->sampled) {
        uint64_t cpu=0,wall=0;unsigned cpu_valid=guest_profile_clock(CLOCK_THREAD_CPUTIME_ID,&cpu);
        unsigned wall_valid=guest_profile_clock(CLOCK_MONOTONIC,&wall);struct guest_profile_timing sample={0};
        if(scope->wall_valid&&scope->locked_valid) {
            if(scope->locked_ns>=scope->before_ns){sample.lock_samples=1;sample.lock_ns=sample.lock_max_ns=scope->locked_ns-scope->before_ns;}
            else guest_profile.clock_errors++;
        }
        if(scope->locked_valid&&wall_valid) {
            if(wall>=scope->locked_ns){sample.dispatch_samples=1;sample.dispatch_ns=sample.dispatch_max_ns=wall-scope->locked_ns;}
            else guest_profile.clock_errors++;
        }
        if(scope->cpu_valid&&cpu_valid) {
            if(cpu>=scope->cpu_ns){sample.cpu_samples=1;sample.cpu_ns=cpu-scope->cpu_ns;}
            else guest_profile.clock_errors++;
        }
        guest_profile_accumulate(&bucket->timing,&sample);
        if(scope->ipc)guest_profile_accumulate(&scope->ipc->timing,&sample);
    }
    guest_profile.current=scope->previous;scope->active=0;
}
static void guest_profile_timing_print(const struct guest_profile_timing *t) {
    printf(" lock-wall-us=%.3f lock-samples=%"PRIu64" lock-max-us=%.3f dispatch-wall-us=%.3f dispatch-samples=%"PRIu64
        " dispatch-max-us=%.3f dispatch-cpu-us=%.3f cpu-samples=%"PRIu64" timing-scope=sampled-SVC kernel-hold-measured=0\n",
        t->lock_samples?(double)t->lock_ns/t->lock_samples*1e-3:-1.0,t->lock_samples,t->lock_max_ns*1e-3,
        t->dispatch_samples?(double)t->dispatch_ns/t->dispatch_samples*1e-3:-1.0,t->dispatch_samples,t->dispatch_max_ns*1e-3,
        t->cpu_samples?(double)t->cpu_ns/t->cpu_samples*1e-3:-1.0,t->cpu_samples);
}
static void guest_other_boundary(unsigned frame,unsigned frames,unsigned report) {
    if(!guest_other_enabled)return;
    if(report&&frames&&guest_other.ready) {
        printf("GUEST_OTHER_PROFILE frame=%u frames=%u thread=%u calls=%"PRIu64" returns=%"PRIu64" overflow=%"PRIu64" missing-context=%"PRIu64" samples=%"PRIu64" cpu-samples=%"PRIu64" lock-samples=%"PRIu64" clock-calls=%"PRIu64" clock-errors=%"PRIu64" sampled-wall-total-ns=%"PRIu64" sampled-cpu-total-ns=%"PRIu64" sampled-lock-total-ns=%"PRIu64" scope=presenting-thread timing=clock-start-through-dispatch-end kernel-broadcast-unlock=excluded signal-entry-return-histogram=excluded lock-includes=initial-cpu-clock overhead=subtracted-never\n",
            frame,frames,guest_thread_index,guest_other.calls,guest_other.returns,guest_other.overflow,guest_other.missing_context,
            guest_other.samples,guest_other.cpu_samples,guest_other.lock_samples,guest_other.clock_calls,guest_other.clock_errors,
            guest_other.wall_ns,guest_other.cpu_ns,guest_other.lock_ns);
        for(unsigned i=0;i<GUEST_OTHER_SLOTS;i++) {
            const struct guest_other_bucket *b=&guest_other.buckets[i];if(!b->used)continue;
            printf("GUEST_OTHER_SITE frame=%u thread=%u pc=0x%"PRIx64" lr=0x%"PRIx64" opcode=0x%x arg2-class=%u arg1-byte-zero=%u calls=%"PRIu64" returns=%"PRIu64" samples=%"PRIu64" cpu-samples=%"PRIu64" lock-samples=%"PRIu64" wall-ns=%"PRIu64" cpu-ns=%"PRIu64" lock-ns=%"PRIu64" args=dereferenced-never\n",
                frame,guest_thread_index,b->pc,b->lr,b->instruction,b->size_class,b->byte_zero,b->calls,b->returns,
                b->samples,b->cpu_samples,b->lock_samples,b->wall_ns,b->cpu_ns,b->lock_ns);
        }
    }
    unsigned remaining=guest_other.remaining,rng=guest_other.rng;
    guest_other=(typeof(guest_other)){.ready=1,.remaining=remaining,.rng=rng};
}
static void guest_profile_boundary(unsigned frame,unsigned frames,unsigned report) {
    guest_other_boundary(frame,frames,report);
    if(!guest_profile_enabled)return;
    /* Only the presenting thread reads/resets its own TLS. Startup is dropped
     * at the first present; background-thread counters are not aggregated. */
    uint64_t cpu=0;unsigned valid=guest_profile_clock(CLOCK_THREAD_CPUTIME_ID,&cpu);
    if(report&&frames&&guest_profile.interval_ready) {
        uint64_t calls=0,returns=0;for(unsigned i=0;i<256;i++){calls+=guest_profile.svc[i].calls;returns+=guest_profile.svc[i].returns;}
        unsigned cpu_ok=valid&&guest_profile.interval_cpu_valid&&cpu>=guest_profile.interval_cpu_ns;
        printf("GUEST_PROFILE frame=%u frames=%u thread=%u svc=%"PRIu64" svc-returns=%"PRIu64" ipc=%"PRIu64
            " cnt-ticks=%"PRIu64" cnt-frequency=%"PRIu64" tls-read-traps=%"PRIu64" tls-write-traps=%"PRIu64
            " other-traps=%"PRIu64" ipc-overflow=%"PRIu64" clock-errors=%"PRIu64" reentrant=%"PRIu64
            " thread-cpu-ms-per-frame=%.3f scope=presenting-thread interval=since-previous-profile cpu-includes=guest-runtime-GL-logs tls-stubs-counted=0\n",
            frame,frames,guest_thread_index,calls,returns,guest_profile.ipc_calls,guest_profile.cnt_ticks,guest_profile.cnt_frequency,
            guest_profile.tls_reads,guest_profile.tls_writes,guest_profile.other_traps,guest_profile.ipc_overflow,guest_profile.clock_errors,guest_profile.reentrant,
            cpu_ok?(double)(cpu-guest_profile.interval_cpu_ns)*1e-6/frames:-1.0);
        for(unsigned i=0;i<256;i++)if(guest_profile.svc[i].calls) {
            const struct guest_profile_svc *s=&guest_profile.svc[i];printf("GUEST_SVC_PROFILE frame=%u thread=%u svc=0x%x calls=%"PRIu64" returns=%"PRIu64" calls-per-frame=%.3f",
                frame,guest_thread_index,i,s->calls,s->returns,(double)s->calls/frames);guest_profile_timing_print(&s->timing);
        }
        for(unsigned i=0;i<GUEST_PROFILE_IPC_SLOTS;i++)if(guest_profile.ipc[i].used) {
            const struct guest_profile_ipc *s=&guest_profile.ipc[i];printf("GUEST_IPC_PROFILE frame=%u thread=%u endpoint=%u command=%u close=%u type=%u category=%s calls=%"PRIu64" calls-per-frame=%.3f",
                frame,guest_thread_index,s->endpoint,s->command,s->close,s->type,s->category,s->calls,(double)s->calls/frames);guest_profile_timing_print(&s->timing);
        }
    }
    /* Keep jitter phase between intervals to avoid repeated deterministic
     * sampling at a particular frame operation. No timing sum is extrapolated. */
    for(unsigned i=0;i<256;i++){unsigned remaining=guest_profile.svc[i].remaining;guest_profile.svc[i]=(struct guest_profile_svc){.remaining=remaining};}
    memset(guest_profile.ipc,0,sizeof(guest_profile.ipc));
    guest_profile.cnt_ticks=guest_profile.cnt_frequency=guest_profile.tls_reads=guest_profile.tls_writes=guest_profile.other_traps=0;
    guest_profile.ipc_calls=guest_profile.ipc_overflow=guest_profile.clock_errors=guest_profile.reentrant=0;
    guest_profile.interval_cpu_ns=cpu;guest_profile.interval_cpu_valid=valid;guest_profile.interval_ready=1;
}
#endif
