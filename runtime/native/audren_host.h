/* Physical host output: system SDL2 + pinned V5 nxaudio SPSC PCM ring.
 * SDL operations run on a normal host worker, never in a guest signal frame.
 * The callback reads only the ring; it never touches guest memory/kernel locks.
 */
#include <SDL2/SDL.h>
#include "nxaudio.h"
#include "audren_affinity.h"
#include "megaman_host_audio_clock.h"
#define AUD_HOST_BLOCK 240U
#define AUD_HOST_CAPACITY 8192U
#define AUD_HOST_BURST 4U
#define AUD_HOST_PERIOD_NS 5000000ULL
struct aud_cadence {
    uint64_t next_ns,refill_blocks,late_ticks,max_late_ns,rebased_ticks;
    unsigned playing,low_water,target,high_water;
};
static uint64_t aud_host_now(void) {
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    return (uint64_t)now.tv_sec*1000000000ULL+now.tv_nsec;
}
static void aud_cadence_configure(struct aud_cadence *c,unsigned period) {
    *c=(struct aud_cadence){.low_water=3U*period};
    /* Native65 measured wake delays up to19.4ms while mixing costs ~1.6ms
     * per block. Three SDL periods at the low watermark cover that delay,
     * the 5ms poll and usual mix work; four periods establish the reserve.
     * Only real renderer PCM primes/refills it; no gain or clock changes. */
    c->target=((4U*period+AUD_HOST_BLOCK-1)/AUD_HOST_BLOCK)*AUD_HOST_BLOCK;
    c->high_water=c->target+AUD_HOST_BLOCK;
    if(!period || c->high_water>AUD_HOST_CAPACITY)stop("AUDREN_HOST_PERIOD_RANGE",0,period,20);
}
static void aud_cadence_started(struct aud_cadence *c,uint64_t now) {
    c->playing=1;c->next_ns=now+AUD_HOST_PERIOD_NS;
}
static unsigned aud_cadence_blocks(struct aud_cadence *c,uint64_t now,unsigned queued) {
    if(queued>AUD_HOST_CAPACITY)stop("AUDREN_HOST_RING_STATE",0,queued,24);
    uint64_t due=0,wanted=0;
    if(!c->playing) {
        if(queued<c->target)wanted=(c->target-queued+AUD_HOST_BLOCK-1)/AUD_HOST_BLOCK;
    } else {
        if(now>=c->next_ns) {
            uint64_t late=now-c->next_ns;due=1+late/AUD_HOST_PERIOD_NS;
            if(late>c->max_late_ns)c->max_late_ns=late;
            if(late>=AUD_HOST_PERIOD_NS)c->late_ticks++;
        }
        wanted=due;
        if(queued<c->low_water) {
            unsigned refill=(c->target-queued+AUD_HOST_BLOCK-1)/AUD_HOST_BLOCK;
            if(wanted<refill)wanted=refill;
        }
    }
    unsigned space=(AUD_HOST_CAPACITY-queued)/AUD_HOST_BLOCK;
    unsigned reserve=queued<c->high_water?(c->high_water-queued)/AUD_HOST_BLOCK:0;
    if(space>reserve)space=reserve;
    unsigned blocks=wanted>AUD_HOST_BURST?AUD_HOST_BURST:(unsigned)wanted;
    if(blocks>space)blocks=space;
    if(c->playing) {
        unsigned timed=due<blocks?(unsigned)due:blocks;
        c->next_ns+=(uint64_t)timed*AUD_HOST_PERIOD_NS;
        c->refill_blocks+=blocks-timed;
        /* A consumer that stops/pauses must not build unbounded deadline debt.
         * Rebase only when the real queued reserve prevents further output;
         * no PCM is skipped, replaced or overwritten. */
        if(due>blocks && blocks==space && queued+blocks*AUD_HOST_BLOCK>=c->target) {
            c->rebased_ticks+=due-blocks;c->next_ns=now+AUD_HOST_PERIOD_NS;
        }
    }
    return blocks;
}
static struct timespec aud_cadence_deadline(const struct aud_cadence *c,uint64_t now,unsigned active) {
    uint64_t next=active?(c->playing?c->next_ns:now+1000000ULL):now+AUD_HOST_PERIOD_NS;
    /* Bounded catch-up batches yield the kernel mutex between batches. */
    if(next<=now)next=now+1000000ULL;
    return (struct timespec){(time_t)(next/1000000000ULL),(long)(next%1000000000ULL)};
}
static struct {unsigned initialized,operation,done,error;SDL_AudioDeviceID device;
    nxaudio_stream *stream;pthread_t worker;unsigned callbacks,nonzero,underruns;
    uint64_t requested_frames;struct aud_cadence cadence;
    uint64_t profile_ns,mix_ns,mix_max_ns;unsigned queue_min,queue_max;
    nxaudio_stream_stats last_stats;unsigned last_callbacks;uint64_t last_requested;
    uint64_t last_refills,last_late,last_rebased;} aud_host;
static pthread_cond_t aud_host_requested=PTHREAD_COND_INITIALIZER;
/* The shared kernel mutex protects the operation predicate, but unrelated
 * guest kernel broadcasts must not wake this timed host wait. Responses and
 * renderer events continue to notify kernel_changed as before. */
static unsigned aud_host_wait(const struct timespec *deadline) {
    int rc=0;unsigned wakes=0;
    while(rc!=ETIMEDOUT && !(aud_host.operation&&!aud_host.done)) {
        rc=pthread_cond_clockwait(&aud_host_requested,&kernel_mutex,CLOCK_MONOTONIC,deadline);
        if(!rc)wakes++;
        else if(rc!=ETIMEDOUT)stop("AUDREN_HOST_WAIT_FAILED",0,rc,21);
    }
    return wakes;
}
static void aud_callback(void *unused,Uint8 *data,int bytes) {
    (void)unused;aud_affinity_callback();nxaudio_pull_result result;
#if MEGAMAN_HOST_DIAGNOSTIC
    if(bytes<=0 || bytes%4 || megaman_host_audio_clock_wait((unsigned)bytes/4)) {
        if(bytes>0)memset(data,0,(unsigned)bytes);
        __atomic_store_n(&aud_host.error,1,__ATOMIC_RELEASE);return;
    }
#endif
    if(bytes<0 || bytes%4 || nxaudio_realtime_pull(aud_host.stream,data,(unsigned)bytes/4,&result)!=NXAUDIO_OK) {
        if(bytes>0)memset(data,0,(unsigned)bytes);
        __atomic_store_n(&aud_host.error,1,__ATOMIC_RELEASE);return;
    }
    unsigned nz=0;for(int i=0;i<bytes/2;i++)if(((int16_t *)data)[i])nz++;
    __atomic_add_fetch(&aud_host.callbacks,1,__ATOMIC_RELAXED);
    __atomic_add_fetch(&aud_host.requested_frames,result.frames_requested,__ATOMIC_RELAXED);
    __atomic_add_fetch(&aud_host.nonzero,nz,__ATOMIC_RELAXED);
    if(result.silence_frames)__atomic_add_fetch(&aud_host.underruns,1,__ATOMIC_RELAXED);
}
static unsigned aud_device_open(void) {
    if(!aud_host.device) {
        if(SDL_InitSubSystem(SDL_INIT_AUDIO))return 1;
        const char *driver=SDL_GetCurrentAudioDriver();
        if(!driver || !strcmp(driver,"dummy") || !strcmp(driver,"disk"))return 1;
        SDL_AudioSpec want={.freq=48000,.format=AUDIO_S16SYS,.channels=2,.samples=512,.callback=aud_callback},have={0};
        aud_host.device=SDL_OpenAudioDevice(NULL,0,&want,&have,0);
        if(!aud_host.device)return 1;
        nxaudio_format requested={48000,NXAUDIO_SAMPLE_S16LE,2,512,40000},granted=requested;
        granted.frequency=have.freq;granted.channels=have.channels;granted.period_frames=have.samples;
        if(have.format!=AUDIO_S16SYS)return 1;
        nxaudio_reason reason;
        if(nxaudio_classify_granted_format(&requested,&granted,&reason)!=NXAUDIO_OK)return 1;
        nxaudio_stream_options options={NXAUDIO_API_VERSION,sizeof(options),granted,AUD_HOST_CAPACITY};
        if(nxaudio_stream_create(&options,&aud_host.stream)!=NXAUDIO_OK)return 1;
        printf("AUDREN_HOST_OPEN backend=%s rate=%d channels=%u sample-bits=16 period=%u provider=nxaudio-0.4.1 system-SDL-only\n",driver,have.freq,have.channels,have.samples);
#if MEGAMAN_HOST_DIAGNOSTIC
        printf("MEGAMAN_HOST_AUDIO_NULL_CLOCK rate=48000 clock=monotonic sink=null audible=false physical=false\n");
#endif
    }
    nxaudio_stream_stats stats;nxaudio_stream_get_stats(aud_host.stream,&stats);
    nxaudio_result rc=stats.state==NXAUDIO_STREAM_PAUSED?nxaudio_stream_resume(aud_host.stream):nxaudio_stream_start(aud_host.stream);
    if(rc!=NXAUDIO_OK)return 1;
    aud_cadence_configure(&aud_host.cadence,stats.format.period_frames);
    aud_host.profile_ns=aud_host_now();aud_host.queue_min=stats.queued_frames;aud_host.queue_max=stats.queued_frames;
    aud_host.last_stats=stats;aud_host.last_callbacks=__atomic_load_n(&aud_host.callbacks,__ATOMIC_RELAXED);
    aud_host.last_requested=__atomic_load_n(&aud_host.requested_frames,__ATOMIC_RELAXED);
    aud_host.last_refills=aud_host.last_late=aud_host.last_rebased=0;aud_host.mix_ns=aud_host.mix_max_ns=0;
    /* Open/resume succeeds in the native order. SDL remains paused until the
     * guest has marked its configured renderer started and real PCM is ready. */
    return 0;
}
static void aud_render_pcm(struct aud_renderer *r,int16_t pcm[480]) {
    if(!aud_snapshot_enabled()){aud_mix_pcm(r,pcm);return;}
    uint64_t begin=aud_host_now();aud_snapshot_capture_locked(r);
    aud_snapshot.capture_ns+=aud_host_now()-begin;
    pthread_mutex_unlock(&kernel_mutex);
    begin=aud_host_now();aud_snapshot_reading=1;aud_mix_pcm(&aud_snapshot.renderer,pcm);aud_snapshot_reading=0;
    aud_snapshot.unlocked_ns+=aud_host_now()-begin;
    pthread_mutex_lock(&kernel_mutex);
    begin=aud_host_now();aud_snapshot_commit_locked(r);aud_snapshot.commit_ns+=aud_host_now()-begin;
}
static void aud_host_profile(struct aud_renderer *r,uint64_t now,const nxaudio_stream_stats *stats) {
    if(stats->queued_frames<aud_host.queue_min)aud_host.queue_min=stats->queued_frames;
    if(stats->queued_frames>aud_host.queue_max)aud_host.queue_max=stats->queued_frames;
    if(!aud_host.profile_ns)aud_host.profile_ns=now;
    if(now-aud_host.profile_ns<1000000000ULL)return;
    unsigned callbacks=__atomic_load_n(&aud_host.callbacks,__ATOMIC_RELAXED);
    uint64_t requested=__atomic_load_n(&aud_host.requested_frames,__ATOMIC_RELAXED);
    struct aud_cadence *c=&aud_host.cadence;
    printf("AUDREN_HOST_PROFILE elapsed-ms=%.3f submitted=%u consumed=%u requested=%"PRIu64" callbacks=%u underrun-frames=%u silence-frames=%u queue=%u queue-min=%u queue-max=%u low-water=%u target=%u high-water=%u refill-blocks=%"PRIu64" late-wakes=%"PRIu64" max-late-ms=%.3f rebased-ticks=%"PRIu64" mix-ms=%.3f mix-max-ms=%.3f clipped-samples=%"PRIu64" output-samples=%"PRIu64" peak-preclip=%.3f scope=interval queue-scope=worker-observed\n",
        (now-aud_host.profile_ns)/1e6,(uint32_t)(stats->submitted_frames-aud_host.last_stats.submitted_frames),
        (uint32_t)(stats->consumed_frames-aud_host.last_stats.consumed_frames),requested-aud_host.last_requested,
        callbacks-aud_host.last_callbacks,(uint32_t)(stats->underrun_frames-aud_host.last_stats.underrun_frames),
        (uint32_t)(stats->silence_frames-aud_host.last_stats.silence_frames),stats->queued_frames,aud_host.queue_min,aud_host.queue_max,
        c->low_water,c->target,c->high_water,c->refill_blocks-aud_host.last_refills,c->late_ticks-aud_host.last_late,
        c->max_late_ns/1e6,c->rebased_ticks-aud_host.last_rebased,aud_host.mix_ns/1e6,aud_host.mix_max_ns/1e6,
        r->clipped_samples,r->output_samples,(double)r->peak_before_clip);
    aud_mix_profile_report();
    if(aud_snapshot.enabled) {
        printf("AUDREN_SNAPSHOT_PROFILE blocks=%"PRIu64" capture-ms=%.3f mix-unlocked-ms=%.3f commit-ms=%.3f copied-bytes=%"PRIu64" snapshot-valid=1 mix-kernel-lock=0 mix-regions-lock=0 scope=interval\n",
            aud_snapshot.blocks,aud_snapshot.capture_ns/1e6,aud_snapshot.unlocked_ns/1e6,aud_snapshot.commit_ns/1e6,aud_snapshot.copied_bytes);
        aud_snapshot.blocks=aud_snapshot.capture_ns=aud_snapshot.unlocked_ns=aud_snapshot.commit_ns=aud_snapshot.copied_bytes=0;
    }
    aud_host.profile_ns=now;aud_host.last_stats=*stats;aud_host.last_callbacks=callbacks;aud_host.last_requested=requested;
    aud_host.last_refills=c->refill_blocks;aud_host.last_late=c->late_ticks;aud_host.last_rebased=c->rebased_ticks;
    aud_host.mix_ns=aud_host.mix_max_ns=0;aud_host.queue_min=stats->queued_frames;aud_host.queue_max=stats->queued_frames;
    c->max_late_ns=0;r->clipped_samples=r->output_samples=0;r->peak_before_clip=0;
}
static void *aud_worker(void *unused) {
    (void)unused;sigset_t blocked;sigemptyset(&blocked);sigaddset(&blocked,SIGALRM);pthread_sigmask(SIG_BLOCK,&blocked,NULL);
    aud_affinity_worker();
    pthread_mutex_lock(&kernel_mutex);
    for(;;) {
        aud_affinity_check();
        if(aud_host.operation && !aud_host.done) {
            unsigned op=aud_host.operation;pthread_mutex_unlock(&kernel_mutex);unsigned error=0;
            if(op==1)error=aud_device_open();
            else if(op==2 && aud_host.device){SDL_PauseAudioDevice(aud_host.device,1);error=nxaudio_stream_pause(aud_host.stream)!=NXAUDIO_OK;aud_host.cadence.playing=0;aud_host.cadence.next_ns=0;}
            else error=1;
            if(error)fprintf(stderr,"AUDREN_HOST_ERROR %s\n",SDL_GetError());
            pthread_mutex_lock(&kernel_mutex);
            if(error)__atomic_store_n(&aud_host.error,1,__ATOMIC_RELEASE);
            aud_host.done=1;pthread_cond_broadcast(&kernel_changed);
        }
        unsigned active=0;
        for(unsigned i=0;i<2;i++)if(aud_renderers[i].open && aud_renderers[i].started) {
            active=1;struct aud_renderer *r=&aud_renderers[i];nxaudio_stream_stats stats;
            if(nxaudio_stream_get_stats(aud_host.stream,&stats)!=NXAUDIO_OK)stop("AUDREN_HOST_STATS_FAILED",0,0,21);
            if(stats.queued_frames<aud_host.queue_min)aud_host.queue_min=stats.queued_frames;
            unsigned blocks=aud_cadence_blocks(&aud_host.cadence,aud_host_now(),stats.queued_frames);
            for(unsigned block=0;block<blocks;block++) {
            int16_t pcm[480];uint64_t begin=aud_host_now();aud_render_pcm(r,pcm);
            uint64_t cost=aud_host_now()-begin;aud_host.mix_ns+=cost;if(cost>aud_host.mix_max_ns)aud_host.mix_max_ns=cost;
            uint32_t written=0;
            if(nxaudio_worker_submit(aud_host.stream,pcm,240,&written)!=NXAUDIO_OK || written!=240)
                stop("AUDREN_HOST_RING_OVERFLOW",0,written,21);
            if(r->event)session_get(r->event,0)->initialized=1;
            pthread_cond_broadcast(&kernel_changed);
            if(r->frames<=4 || !(r->frames&(r->frames-1))) {
                struct line log={0};text(&log,"AUDREN_RENDER frames=");hex(&log,r->frames);
                text(&log," callbacks=");hex(&log,__atomic_load_n(&aud_host.callbacks,__ATOMIC_RELAXED));
                text(&log," nonzero-samples=");hex(&log,__atomic_load_n(&aud_host.nonzero,__ATOMIC_RELAXED));emit(&log);
            }
            }
            if(nxaudio_stream_get_stats(aud_host.stream,&stats)!=NXAUDIO_OK)stop("AUDREN_HOST_STATS_FAILED",0,0,21);
            if(!aud_host.cadence.playing && stats.queued_frames>=aud_host.cadence.target) {
                aud_cadence_started(&aud_host.cadence,aud_host_now());
                printf("AUDREN_HOST_PRIMED queued=%u target=%u period=%u cadence-ns=%llu max-burst=%u pcm=renderer\n",
                    stats.queued_frames,aud_host.cadence.target,stats.format.period_frames,AUD_HOST_PERIOD_NS,AUD_HOST_BURST);
                pthread_mutex_unlock(&kernel_mutex);SDL_PauseAudioDevice(aud_host.device,0);pthread_mutex_lock(&kernel_mutex);
            }
            aud_host_profile(r,aud_host_now(),&stats);
        }
        struct timespec deadline=aud_cadence_deadline(&aud_host.cadence,aud_host_now(),active);
        (void)aud_host_wait(&deadline);
    }
    return NULL;
}
static void aud_host_initialize(void) {
    pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,1U<<20);
    if(pthread_create(&aud_host.worker,&attr,aud_worker,NULL))stop("AUDREN_HOST_THREAD_FAILED",0,errno,21);
    pthread_attr_destroy(&attr);aud_host.initialized=1;
}
static void aud_host_request(unsigned operation,uint64_t pc) {
    if(!aud_host.initialized || aud_host.operation)stop("AUDREN_HOST_REQUEST_INVALID",pc,operation,24);
    aud_host.operation=operation;aud_host.done=0;pthread_cond_broadcast(&kernel_changed);
    pthread_cond_signal(&aud_host_requested);
    while(!aud_host.done)pthread_cond_wait(&kernel_changed,&kernel_mutex);
    aud_host.operation=0;
    if(__atomic_load_n(&aud_host.error,__ATOMIC_ACQUIRE))stop("AUDREN_HOST_FAILED",pc,operation,21);
    if(operation==1)aud_output_ready=1;
}
