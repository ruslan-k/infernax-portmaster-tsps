/* AudioOut IPC, backed by the original game's PCM and system SDL2/nxaudio.
 * Copied from the-messenger-switch-study/native/audout.h @ 90cbe57 (FMOD on this title plays
 * through AudioOut, docs/AUDIO.md); the only change is the device-volume symbol of this trunk.
 * Imported exact reference: provenance/cotm2-audout-source.json.
 * All SDL calls occur on a normal worker; realtime callback never reads guest memory.
 */
#include "audout_queue.h"
static struct {unsigned initialized,operation,done,error,open,started,playing,generation;
    uint32_t rate,event,volume_bits;pthread_t worker;SDL_AudioDeviceID device;
    nxaudio_stream *stream;struct aout_queue queue;uint64_t consumed,underrun,nonzero;
    uint64_t profile_ns,last_consumed,last_underrun;unsigned last_callbacks,callbacks;
} aout={.volume_bits=0x3f800000U};
static pthread_cond_t aout_changed=PTHREAD_COND_INITIALIZER;
static void aout_callback(void *unused,Uint8 *pcm,int bytes) {
    (void)unused;nxaudio_pull_result result;
    if(bytes<0 || bytes%4 || nxaudio_realtime_pull(aout.stream,pcm,(unsigned)bytes/4,&result)!=NXAUDIO_OK) {
        if(bytes>0)memset(pcm,0,(size_t)bytes);
        __atomic_store_n(&aout.error,1,__ATOMIC_RELEASE);return;
    }
    uint32_t bits=__atomic_load_n(&aout.volume_bits,__ATOMIC_RELAXED);float volume;memcpy(&volume,&bits,4);
    float device_volume=aud_output_volume; /* trunk keeps the device volume as a plain float */
    volume*=device_volume;
    unsigned nonzero=0;
    for(int i=0;i<bytes/2;i++) {
        int16_t *v=(int16_t *)pcm+i;
        if(volume!=1.0f){float scaled=(float)*v*volume;*v=scaled>32767?32767:scaled< -32768?-32768:(int16_t)scaled;}
        nonzero+=*v!=0;
    }
    __atomic_add_fetch(&aout.consumed,result.frames_copied,__ATOMIC_RELEASE);
    __atomic_add_fetch(&aout.underrun,result.silence_frames,__ATOMIC_RELAXED);
    __atomic_add_fetch(&aout.nonzero,nonzero,__ATOMIC_RELAXED);
    __atomic_add_fetch(&aout.callbacks,1,__ATOMIC_RELAXED);
}
static unsigned aout_open_device(void) {
    if(SDL_InitSubSystem(SDL_INIT_AUDIO)){printf("AUDOUT_HOST_OPEN_FAIL stage=sdl-init error=%s\n",SDL_GetError());return 1;}
    const char *driver=SDL_GetCurrentAudioDriver();
    if(!driver || !strcmp(driver,"dummy") || !strcmp(driver,"disk")){printf("AUDOUT_HOST_OPEN_FAIL stage=driver driver=%s\n",driver?driver:"(null)");return 1;}
    /* CotM2 recycles three 240-frame PCM buffers only after consumption.
     * A 512-frame pull exhausts their partial tail every eighth callback,
     * even with immediate refill. A 256-frame pull leaves refill headroom. */
    SDL_AudioSpec wanted={.freq=(int)aout.rate,.format=AUDIO_S16SYS,.channels=2,.samples=256,.callback=aout_callback},got={0};
    aout.device=SDL_OpenAudioDevice(NULL,0,&wanted,&got,0);
    if(!aout.device || got.freq!=wanted.freq || got.format!=wanted.format || got.channels!=2 || !got.samples || got.samples>256)
        {printf("AUDOUT_HOST_OPEN_FAIL stage=open driver=%s device=%u freq=%d/%d format=%u/%u channels=%u samples=%u error=%s\n",
            driver,aout.device,got.freq,wanted.freq,got.format,wanted.format,got.channels,got.samples,SDL_GetError());return 1;}
    nxaudio_stream_options options={NXAUDIO_API_VERSION,sizeof(options),{aout.rate,NXAUDIO_SAMPLE_S16LE,2,got.samples,40000},8192};
    if(nxaudio_stream_create(&options,&aout.stream)!=NXAUDIO_OK)return 1;
    __atomic_store_n(&aout.consumed,0,__ATOMIC_RELEASE);aout.queue.consumed=0;
    printf("AUDOUT_HOST_OPEN backend=%s rate=%u channels=2 format=S16 period=%u provider=nxaudio-0.4.1 pcm=guest\n",driver,aout.rate,got.samples);
    return 0;
}
static void aout_refresh(void) {
    aout.queue.consumed=__atomic_load_n(&aout.consumed,__ATOMIC_ACQUIRE);
    if(aout.event && aout_released(&aout.queue)) {
        session_get(aout.event,0)->initialized=1;pthread_cond_broadcast(&kernel_changed);
    }
}
static void *aout_worker(void *unused) {
    (void)unused;sigset_t blocked;sigemptyset(&blocked);sigaddset(&blocked,SIGALRM);pthread_sigmask(SIG_BLOCK,&blocked,NULL);
    const char *affinity=getenv("COTM_AUDIO_AFFINITY");
    if(affinity&&strcmp(affinity,"0")&&strcmp(affinity,"1"))stop("AUDOUT_AFFINITY_OPTION",0,0,24);
    if(affinity&&!strcmp(affinity,"1")) {
        cpu_set_t set,actual;CPU_ZERO(&set);CPU_SET(2,&set);
        int error=pthread_setaffinity_np(pthread_self(),sizeof(set),&set);
        if(!error)error=pthread_getaffinity_np(pthread_self(),sizeof(actual),&actual);
        if(error||!CPU_EQUAL(&set,&actual))stop("AUDOUT_AFFINITY",0,error,21);
        /* SDL/PulseAudio threads created by this worker inherit CPU2. Keep
         * their callbacks/mainloops off the guest main/control core0. */
        printf("AUDOUT_AFFINITY worker-tid=%ld cpu=2 SDL-PulseAudio=inherit verified=1\n",(long)syscall(SYS_gettid));
    }
    pthread_mutex_lock(&kernel_mutex);
    for(;;) {
        if(aout.operation&&!aout.done) {
            unsigned operation=aout.operation,error=0;pthread_mutex_unlock(&kernel_mutex);
            if(operation==1)error=aout_open_device();
            else if(operation==2) {
                nxaudio_stream_stats stats;
                if(nxaudio_stream_get_stats(aout.stream,&stats)!=NXAUDIO_OK)error=1;
                else error=(stats.state==NXAUDIO_STREAM_PAUSED?nxaudio_stream_resume(aout.stream):nxaudio_stream_start(aout.stream))!=NXAUDIO_OK;
            }
            else if(operation==3){SDL_PauseAudioDevice(aout.device,1);error=nxaudio_stream_pause(aout.stream)!=NXAUDIO_OK;}
            else if(operation==4){SDL_PauseAudioDevice(aout.device,1);SDL_CloseAudioDevice(aout.device);aout.device=0;nxaudio_stream_close(&aout.stream);}
            else error=1;
            pthread_mutex_lock(&kernel_mutex);aout.done=1;
            if(error)__atomic_store_n(&aout.error,1,__ATOMIC_RELEASE);
            pthread_cond_broadcast(&kernel_changed);
        }
        if(__atomic_load_n(&aout.error,__ATOMIC_ACQUIRE))stop("AUDOUT_HOST_FAILURE",0,0,21);
        if(aout.open && aout.started && !aout.operation) {
            aout_refresh();
            for(unsigned i=0;i<aout.queue.count;i++) {
                struct aout_buffer *b=&aout.queue.buffers[(aout.queue.head+i)%AOUT_SLOTS];
                if(b->sent==b->bytes)continue;
                nxaudio_stream_stats stats;if(nxaudio_stream_get_stats(aout.stream,&stats)!=NXAUDIO_OK)stop("AUDOUT_RING_STATE",0,0,21);
                uint32_t frames=(b->bytes-b->sent)/4,space=8192-stats.queued_frames,written=0;
                if(frames>space)frames=space;
                if(!frames)break;
                if(nxaudio_worker_submit(aout.stream,b->pcm+b->sent,frames,&written)!=NXAUDIO_OK || written!=frames)
                    stop("AUDOUT_RING_SUBMIT",0,written,21);
                b->sent+=written*4;
            }
            nxaudio_stream_stats stats;if(nxaudio_stream_get_stats(aout.stream,&stats)!=NXAUDIO_OK)stop("AUDOUT_RING_STATE",0,0,21);
            if(!aout.playing && stats.queued_frames) {
                aout.playing=1;pthread_mutex_unlock(&kernel_mutex);SDL_PauseAudioDevice(aout.device,0);pthread_mutex_lock(&kernel_mutex);
            }
            uint64_t now=aud_host_now();
            if(now-aout.profile_ns>=1000000000ULL) {
                uint64_t consumed=__atomic_load_n(&aout.consumed,__ATOMIC_RELAXED),underrun=__atomic_load_n(&aout.underrun,__ATOMIC_RELAXED);
                printf("AUDOUT_HOST_PROFILE consumed-frames=%"PRIu64" underrun-frames=%"PRIu64" queue=%u buffers=%u nonzero-total=%"PRIu64" scope=interval\n",
                    consumed-aout.last_consumed,underrun-aout.last_underrun,stats.queued_frames,aout.queue.count,__atomic_load_n(&aout.nonzero,__ATOMIC_RELAXED));
                aout.profile_ns=now;aout.last_consumed=consumed;aout.last_underrun=underrun;
            }
        }
        struct timespec deadline;clock_gettime(CLOCK_MONOTONIC,&deadline);deadline.tv_nsec+=2000000;
        if(deadline.tv_nsec>=1000000000){deadline.tv_nsec-=1000000000;deadline.tv_sec++;}
        pthread_cond_clockwait(&aout_changed,&kernel_mutex,CLOCK_MONOTONIC,&deadline);
    }
    return NULL;
}
static unsigned aout_wait_operation(void) {
    unsigned waited=0;while(aout.operation){waited=1;pthread_cond_wait(&kernel_changed,&kernel_mutex);}return waited;
}
static void aout_request(unsigned operation,uint64_t pc) {
    if(!aout.initialized) {
        if(pthread_create(&aout.worker,NULL,aout_worker,NULL))stop("AUDOUT_WORKER_CREATE",pc,errno,21);
        aout.initialized=1;
    }
    if(aout.operation)stop("AUDOUT_OPERATION_PENDING",pc,operation,24);
    aout.operation=operation;aout.done=0;pthread_cond_signal(&aout_changed);
    while(!aout.done)pthread_cond_wait(&kernel_changed,&kernel_mutex);
    aout.operation=0;pthread_cond_broadcast(&kernel_changed);
    if(__atomic_load_n(&aout.error,__ATOMIC_ACQUIRE))stop("AUDOUT_HOST_FAILURE",pc,operation,21);
}
static void aout_destroy(unsigned state) {
    aout_wait_operation();
    if(!state)return;
    if(!aout.open || state!=aout.generation)stop("AUDOUT_CLOSE_IDENTITY",0,state,24);
    aout.started=0;aout_request(4,0);aout_clear(&aout.queue);aout.open=aout.playing=0;
    if(aout.event){session_close(aout.event,0);aout.event=0;}
}
static int aout_dispatch(uint8_t *buffer,struct session *s,struct ipc_request *req,enum endpoint endpoint,unsigned *state,uint64_t pc) {
    if(endpoint!=EP_AUDOUT_MANAGER && endpoint!=EP_AUDOUT)return 0;
    if(req->domain)stop("AUDOUT_DOMAIN_UNSUPPORTED",pc,req->command,20);
    if(endpoint==EP_AUDOUT_MANAGER) {
        if(req->command==0 || req->command==2) {
            if(req->num_outputs!=1 || req->outputs[0].size<256)stop("AUDOUT_LIST_BUFFER",pc,0,24);
            memset((void *)(uintptr_t)req->outputs[0].address,0,256);memcpy((void *)(uintptr_t)req->outputs[0].address,"DeviceOut",9);
            uint32_t count=1;ipc_reply(buffer,req->token,0,&count,4,0,0);return 1;
        }
        if(req->command==1 || req->command==3) {
            if(aout.open || req->data_size<16 || !req->send_pid || req->copies!=1 || req->copy_handles[0]!=0xffff8001U ||
               req->num_inputs!=1 || req->num_outputs!=1 || req->inputs[0].size<1 || req->inputs[0].size>256 || req->outputs[0].size<256)
                stop("AUDOUT_OPEN_CONTRACT",pc,req->data_size,24);
            /* SDK AudioOutParameter is u32 rate, u16 channels, u16 reserved.
             * InitializeAudioOutParameter fills reserved with 0xcafe. */
            uint32_t rate=read32(req->data),channels=read32(req->data+4)&0xffffU;uint64_t aruid;memcpy(&aruid,req->data+8,8);
            if(!rate)rate=48000;
            if(!channels)channels=2;
            if((rate!=48000&&rate!=44100) || channels!=2 || aruid!=main_thread_id)stop("AUDOUT_FORMAT_REQUIRED",pc,channels,20);
            const char *name=(const char *)(uintptr_t)req->inputs[0].address;
            if(!memchr(name,0,req->inputs[0].size) || (*name && strcmp(name,"DeviceOut")))stop("AUDOUT_DEVICE_REQUIRED",pc,0,20);
            uint64_t output=req->outputs[0].address;unsigned token=req->token;
            aout.rate=rate;aout_request(1,pc);aout.open=1;aout.started=aout.playing=0;
            if(!++aout.generation)stop("AUDOUT_GENERATION_WRAP",pc,0,24);
            if(!accessible(output,256,PROT_WRITE))stop("AUDOUT_OUTPUT_INVALIDATED",pc,output,24);
            memset((void *)(uintptr_t)output,0,256);memcpy((void *)(uintptr_t)output,"DeviceOut",9);
            unsigned *child=ipc_child(buffer,s,req,EP_AUDOUT,pc);*child=aout.generation;uint32_t moved=read32(buffer+12);
            uint32_t result[4]={rate,2,2,1};ipc_reply(buffer,token,0,result,sizeof(result),moved,0);
            aud_output_ready=1;printf("AUDOUT_OPEN rate=%u channels=2 PCM=S16 state=stopped\n",rate);return 1;
        }
        return 0;
    }
    if(!aout.open || *state!=aout.generation)stop("AUDOUT_HANDLE_IDENTITY",pc,*state,24);
    aout_refresh();
    if(req->command==0){uint32_t value=aout.started?0:1;ipc_reply(buffer,req->token,0,&value,4,0,0);return 1;}
    if(req->command==1 || req->command==2) {
        unsigned token=req->token,started=req->command==1;
        if(started!=aout.started){aout.started=0;aout_request(started?2:3,pc);aout.started=started;aout.playing=0;}
        ipc_reply(buffer,token,0,NULL,0,0,0);return 1;
    }
    if(req->command==3 || req->command==7) {
        if(req->data_size<8 || req->num_inputs!=1 || req->inputs[0].size!=40)stop("AUDOUT_APPEND_DESCRIPTOR",pc,req->data_size,24);
        uint64_t values[5],tag;memcpy(&tag,req->data,8);memcpy(values,(void *)(uintptr_t)req->inputs[0].address,40);
        if(values[0] || !values[1] || values[4]>values[2] || values[3]>values[2]-values[4] || values[3]>AOUT_BYTES_LIMIT ||
           values[1]>UINT64_MAX-values[4] || !accessible(values[1]+values[4],values[3],PROT_READ))stop("AUDOUT_PCM_RANGE",pc,values[3],24);
        if(!aout_push(&aout.queue,tag,(void *)(uintptr_t)(values[1]+values[4]),(uint32_t)values[3]))stop("AUDOUT_QUEUE_REJECTED",pc,tag,24);
        {static unsigned logged;if(logged<3){logged++;printf("AUDOUT_PCM_APPEND initial=%u frames=%"PRIu64" buffers=%u\n",logged,values[3]/4,aout.queue.count);}}
        pthread_cond_signal(&aout_changed);ipc_reply(buffer,req->token,0,NULL,0,0,0);return 1;
    }
    if(req->command==4){if(!aout.event)aout.event=session_new(EP_EVENT,pc);ipc_copy_events(buffer,req,&aout.event,1,pc);return 1;}
    if(req->command==5 || req->command==8) {
        if(req->num_outputs!=1 || req->outputs[0].size%8)stop("AUDOUT_RELEASE_BUFFER",pc,0,24);
        uint32_t count=aout_release(&aout.queue,(uint64_t *)(uintptr_t)req->outputs[0].address,req->outputs[0].size/8);
        ipc_reply(buffer,req->token,0,&count,4,0,0);return 1;
    }
    if(req->command==6){if(req->data_size<8)stop("AUDOUT_CONTAINS_TAG",pc,0,24);uint64_t tag;memcpy(&tag,req->data,8);uint8_t found=0;
        for(unsigned i=0;i<aout.queue.count;i++)found|=aout.queue.buffers[(aout.queue.head+i)%AOUT_SLOTS].tag==tag;
        ipc_reply(buffer,req->token,0,&found,1,0,0);return 1;}
    if(req->command==9){uint32_t count=aout.queue.count;ipc_reply(buffer,req->token,0,&count,4,0,0);return 1;}
    if(req->command==10){ipc_reply(buffer,req->token,0,&aout.queue.consumed,8,0,0);return 1;}
    if(req->command==12){float v;if(req->data_size<4)stop("AUDOUT_VOLUME_SIZE",pc,0,24);memcpy(&v,req->data,4);
        if(!isfinite(v)||v<0||v>1)stop("AUDOUT_VOLUME_RANGE",pc,0,24);
        uint32_t bits;memcpy(&bits,&v,4);__atomic_store_n(&aout.volume_bits,bits,__ATOMIC_RELAXED);
        ipc_reply(buffer,req->token,0,NULL,0,0,0);return 1;}
    if(req->command==13){uint32_t bits=__atomic_load_n(&aout.volume_bits,__ATOMIC_RELAXED);ipc_reply(buffer,req->token,0,&bits,4,0,0);return 1;}
    return 0;
}
