/* REV4 mono DSP voices, streaming pitch conversion and ordered submix routing. Each queued wave remains
 * backed by an attached guest pool until the decoder consumes it. Unsupported
 * formats, rates, effects or active surround output fail explicitly. Linear
 * interpolation is a local adapter, not bit-equivalent to the Tegra DSP filter. */
#include "audren_dsp.h"
#include "audren_profile.h"
#include "megaman_host_diagnostic.h"
#if MEGAMAN_HOST_DIAGNOSTIC
static void aud_host_null_voice_evidence(struct aud_renderer *r,unsigned index,const struct aud_voice_in *next,uint64_t pc) {
    static unsigned new_captures,null_captured;
    unsigned needed=next->format==6&&!next->extra;
    for(unsigned i=0;i<4;i++) {
        const struct aud_wave *w=&next->wave[i];
        if(!w->sent && w->size && (!w->address || (w->context_size&&!w->context)))needed=1;
    }
    if(aw_release_mode() || !current_guest_context || (!(needed&&!null_captured) && !(next->is_new&&new_captures<16)))return;
    if(needed)null_captured=1;
    if(next->is_new)new_captures++;
    const char *run=getenv("COTM_RUN_ID");
    if(!run || !*run || strlen(run)>12 || strspn(run,"0123456789")!=strlen(run))stop("AUDREN_RUN_ID",pc,0,24);
    printf("AUDREN_HOST_NULL_WIRE update=%"PRIu64" voice=%u state=%u new=%u waves=%u head=%u extra=0x%"PRIx64" extra-size=%"PRIu64" null-address=%u scope=host-original-input-no-acceptance\n",
        r->updates,index,next->state,next->is_new,next->wave_count,next->head,next->extra,next->extra_size,needed);
    for(unsigned i=0;i<4;i++)printf("AUDREN_HOST_NULL_WAVE slot=%u address=0x%"PRIx64" size=%"PRIu64" sent=%u context=0x%"PRIx64" context-size=%"PRIu64"\n",
        i,next->wave[i].address,next->wave[i].size,next->wave[i].sent,next->wave[i].context,next->wave[i].context_size);
    for(unsigned i=0;i<2;i++) {
        char path[128];snprintf(path,sizeof(path),"audren-null-%s-%u-%"PRIu64"-%s.bin",run,index,r->updates,i?"pools":"voice");
        const void *data=i?(const void *)r->pools:(const void *)next;
        size_t bytes=i?(r->p.voices*4+r->p.effects)*sizeof(*r->pools):sizeof(*next);
        int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(fd<0 || write(fd,data,bytes)!=(ssize_t)bytes)stop("AUDREN_HOST_NULL_CAPTURE_FAILED",pc,errno,21);
        close(fd);
    }
}
#endif
static void aud_voice_diagnostic(const struct aud_voice_in *v,uint64_t pc) {
    printf("AUDREN_REQUIRED_VOICE id=%u format=%u channels=%u rate=%u state=%u new=%u pitch=%g volume=%g extra=%"PRIu64" mix=%u splitter=%u waves=%u head=%u filters=%u/%u\n",
        v->id,v->format,v->channels,v->rate,v->state,v->is_new,(double)v->pitch,(double)v->volume,v->extra_size,v->mix,v->splitter,v->wave_count,v->head,v->filters[0].enabled,v->filters[1].enabled);
    for(unsigned i=0;i<4;i++)printf("AUDREN_REQUIRED_WAVE slot=%u bytes=%"PRIu64" start=%d end=%d loop=%u eos=%u sent=%u context=%"PRIu64"\n",i,v->wave[i].size,v->wave[i].start,v->wave[i].end,v->wave[i].loop,v->wave[i].eos,v->wave[i].sent,v->wave[i].context_size);
    if(!current_guest_context||aw_release_mode())return;
    const char *run=getenv("COTM_RUN_ID");
    if(!run||!*run||strlen(run)>12||strspn(run,"0123456789")!=strlen(run))stop("AUDREN_RUN_ID",pc,0,24);
    for(unsigned i=0;i<10;i++) {
        const void *data=v;uint64_t bytes=sizeof(*v);
        if(i==1){data=(void *)(uintptr_t)v->extra;bytes=v->extra_size;}
        else if(i>=2){unsigned k=(i-2)/2;data=(void *)(uintptr_t)(i&1?v->wave[k].context:v->wave[k].address);bytes=i&1?v->wave[k].context_size:v->wave[k].size;}
        if(!bytes)continue;
        if(bytes>32U*1024U*1024U || (i&&!accessible((uintptr_t)data,bytes,PROT_READ)))stop("AUDREN_CAPTURE_RANGE",pc,i,24);
        char path[112];snprintf(path,sizeof(path),"audren-required-%s-%u-%u.bin",run,v->id,i);
        int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(fd<0 || write(fd,data,bytes)!=(ssize_t)bytes)stop("AUDREN_CAPTURE_FAILED",pc,errno,21);
        close(fd);
    }
}
static void *aud_pool_pointer(struct aud_renderer *r,uint64_t address,uint64_t bytes,uint64_t pc) {
    unsigned count=r->p.voices*4+r->p.effects;
    for(unsigned i=0;i<count;i++) {
        struct aud_pool *p=&r->pools[i];
        if(p->state==5&&address>=p->address&&address-p->address<=p->size&&bytes<=p->size-(address-p->address)) {
            if(!accessible(address,bytes,PROT_READ))stop("AUDREN_POOL_POINTER",pc,address,24);
            return (void *)(uintptr_t)address;
        }
    }
    stop("AUDREN_UNATTACHED_DATA",pc,address,24);return NULL;
}
static unsigned aud_voice_mix_unset(const struct aud_renderer *r,uint32_t mix) {
    /* SDK6.5 AcquireVoiceSlot +84d50/+84d74 stores the adjacent pair
     * {INT32_MAX, UINT32_MAX}: mix and splitter have different sentinels.
     * Preserve the older adapter contract outside this port's REV6 path. */
    return mix==(aud_rev6_plus(r->p.revision)?0x7fffffffU:UINT32_MAX);
}
static void aud_voice_configure(struct aud_renderer *r,unsigned index,const struct aud_voice_in *next,uint64_t pc) {
    struct aud_voice *v=&r->voices[index];
    if(!next->used){
#ifdef AW_NO_ROMFS
        if(v->input.used)aw_voice_note(index,5,0,v,0);
#endif
        v->valid_mask=0;v->active=0;v->resample_active=0;v->input=*next;return;}
    if(aud_rev6_plus(r->p.revision)) {
        /* REV5/6 voice extension, independently identified from the protocol.
         * Never ignore flush requests or decode flags from the newer SDK. */
#ifdef ANIMAL_WELL_SHADERS
        if(next->pad2[0]>1 || next->pad2[1]>4)stop("ZERO_AUDIO_FLUSH_REQUIRED",pc,next->pad2[1],20);
        if(next->pad2[1]) {
            /* Flush count (SDK FlushWaveBuffers): the next N queued waves from the head are released as consumed. */
            for(unsigned n=0;n<next->pad2[1];n++){v->valid_mask&=~(1U<<v->head);v->head=(v->head+1)%4;v->consumed++;}
            v->active=0;v->pool_usage_dirty=1;aw_voice_note(index,6,next->pad2[1],v,0);
            static unsigned logged;if(logged<8){logged++;printf("AW_AUDIO_FLUSH voice=%u count=%u head=%u consumed=%u\n",index,next->pad2[1],v->head,v->consumed);}
        }
#else
        if(next->pad2[0]>1 || next->pad2[1])stop("ZERO_AUDIO_FLUSH_REQUIRED",pc,next->pad2[1],20);
#endif
        /* REV8+ (Animal Well, REV12): [2..3] decoding behaviour flags (bit0 played-count reset at loop, bit1 skip pitch/src
         * when rate and pitch already match), [4] sample-rate-conversion quality (0 medium, 1 high, 2 low): the host resampler
         * is linear in every case; the flags do not change what this renderer produces. */
        if(r->p.revision==AUD_REV12) {
            /* Measured on Animal Well: tail[4]=0x60 on every voice so far (a REV8+ field this renderer does not act on);
             * other tail bytes stay zero. Distinct values are logged once. */
            static uint8_t aw_tail4_seen[256];
            if(!aw_tail4_seen[next->pad2[4]]){aw_tail4_seen[next->pad2[4]]=1;struct line l={0};text(&l,"AW_VOICE_TAIL4 value=");hex(&l,next->pad2[4]);text(&l," flags=");hex(&l,next->pad2[2]);emit(&l);}
            if(next->pad2[2]>3 || next->pad2[3])stop("ZERO_AUDIO_VOICE_EXTENSION_REQUIRED",pc,next->pad2[2],20);
            for(unsigned i=5;i<24;i++)if(next->pad2[i])stop("ZERO_AUDIO_VOICE_EXTENSION_REQUIRED",pc,i,20);
        } else for(unsigned i=2;i<24;i++)if(next->pad2[i])stop("ZERO_AUDIO_VOICE_EXTENSION_REQUIRED",pc,i,20);
        if(next->pad2[0])v->drops=0;
    }
    unsigned pcm=aud_rev6_plus(r->p.revision)&&next->format==2;
    unsigned route_ok=aud_voice_mix_unset(r,next->mix)?
        (next->splitter==UINT32_MAX||next->splitter<r->p.splitters):(next->mix<=r->p.submixes&&next->splitter==UINT32_MAX);
    if(next->id!=index||next->state>2||(!pcm&&(next->format!=6||next->channels!=1))||
       !next->channels||next->channels>6 ||
       (next->rate!=32000&&next->rate!=44100&&next->rate!=48000)||!isfinite(next->pitch)||next->pitch<0.05f||next->pitch>8.0f||
       !isfinite(next->volume)||(pcm?(next->extra||next->extra_size):next->extra_size!=32)||next->wave_count>4||next->head>=4||
       !route_ok||next->filters[0].enabled||next->filters[1].enabled) {
        aud_voice_diagnostic(next,pc);
        stop("AUDREN_VOICE_FEATURE_REQUIRED",pc,index,20);
    }
#if MEGAMAN_HOST_DIAGNOSTIC
    aud_host_null_voice_evidence(r,index,next,pc);
#endif
    for(unsigned c=0;c<next->channels;c++) {
        if(next->channel_ids[c]>=r->p.voices)stop("ZERO_AUDIO_CHANNEL_ID",pc,c,24);
        for(unsigned j=0;j<c;j++)if(next->channel_ids[j]==next->channel_ids[c])stop("ZERO_AUDIO_CHANNEL_ALIAS",pc,c,24);
    }
    const int16_t *coeff=pcm?NULL:aud_pool_pointer(r,next->extra,32,pc);
    if(next->is_new) {
        memset(v,0,sizeof(*v));v->head=next->head;if(coeff)memcpy(v->coefficients,coeff,32);
#ifdef AW_NO_ROMFS
        aw_voice_note(index,1,0,v,0);
#endif
    } else if(v->input.used && (v->input.format!=next->format||v->input.channels!=next->channels))stop("ZERO_AUDIO_LIVE_FORMAT_CHANGE",pc,index,20);
    else if(coeff&&memcmp(v->coefficients,coeff,32))stop("AUDREN_LIVE_COEFFICIENT_CHANGE",pc,index,20);
    if(next->state==1&&v->input.state==0&&v->valid_mask) {
#ifdef ANIMAL_WELL_SHADERS
        /* Stop with queued waves = server VoiceInfo::UpdateParametersForCommandGeneration, state Stopping (sdk 0x223b00):
         * for k=0..3, every valid wave k is marked sent, the wave index advances by one and the consumed count
         * increments; then playback position, played count and decoder history restart. */
        unsigned flushed=0;
        for(unsigned k=0;k<4;k++)if(v->valid_mask&(1U<<k)){v->valid_mask&=~(1U<<k);v->head=(v->head+1)%4;v->consumed++;flushed++;}
        v->played=0;v->active=0;v->exhausted=0;v->resample_active=0;v->position=0;
        memset(v->history,0,sizeof(v->history));memset(v->loop_history,0,sizeof(v->loop_history));v->pool_usage_dirty=1;
        aw_voice_note(index,4,flushed,v,0);
        static unsigned logged;if(logged<8){logged++;printf("AW_AUDIO_STOPPING voice=%u flushed=%u head=%u consumed=%u appended=%u\n",index,flushed,v->head,v->consumed,v->aw_appended);}
#else
        stop("AUDREN_STOP_PENDING_WAVES_REQUIRED",pc,index,20);
#endif
    }
    for(unsigned k=0;k<4;k++) {
        const struct aud_wave *w=&next->wave[k];
        if(w->sent||!w->size)continue;
        uint64_t required=pcm?(uint64_t)(uint32_t)w->end*next->channels*2:
            aud_rev6_plus(r->p.revision)?zero_dsp_required_bytes((uint32_t)w->end):((uint64_t)(uint32_t)w->end+13)/14*8;
        /* REV9+ WaveBufferVersion2 (Animal Well): the last word pair is {loop first sample, loop last sample}; only a loop
         * region equal to the whole buffer (or unset) matches this renderer's loop, anything else stays fail-closed. */
        uint32_t loop_first=(uint32_t)w->reserved,loop_last=(uint32_t)(w->reserved>>32);
        unsigned reserved_ok=!w->reserved || (r->p.revision==AUD_REV12 && loop_first==(uint32_t)w->start && loop_last==(uint32_t)w->end);
        if(w->start<0||w->end<=w->start||w->size>32U*1024U*1024U||required>w->size||
           w->loop>1||w->eos>1||(pcm?(w->context||w->context_size):
               ((w->context_size!=0&&w->context_size!=6)||(w->context&&w->context_size!=6)))||!reserved_ok) {
            printf("AUDREN_WAVE_RANGE_INPUT voice=%u format=%u slot=%u size=%"PRIu64" required=%"PRIu64" start=%d end=%d loop=%u eos=%u context=%"PRIu64" context-size=%"PRIu64" reserved=%"PRIu64"\n",
                index,next->format,k,w->size,required,w->start,w->end,w->loop,w->eos,w->context,w->context_size,w->reserved);
            stop("AUDREN_WAVE_RANGE",pc,k,24);
        }
        aud_pool_pointer(r,w->address,w->size,pc);
        /* SDK 1a622c/30 makes ADPCM context optional by ADDRESS. LC2 sends
         * NULL with size6 for subsequent streaming buffers; the decoder
         * retains its preceding history (SDK 1a9518..54c). */
        if(w->context)aud_pool_pointer(r,w->context,w->context_size,pc);
        if(v->valid_mask&(1U<<k)) {
            const struct aud_wave *old=&v->input.wave[k];
            if(old->address!=w->address||old->size!=w->size||old->start!=w->start||old->end!=w->end)
                stop("AUDREN_WAVE_REPLACE_IN_USE",pc,k,24);
        } else {
            const struct aud_wave *old=&v->input.wave[k];
            if(!old->sent && old->address==w->address && old->size==w->size && old->start==w->start && old->end==w->end && !next->is_new) {
                static unsigned dup;if(dup<24){dup++;printf("AW_AUDIO_WAVE_RESENT voice=%u slot=%u address=0x%"PRIx64" consumed=%u appended=%u update=%"PRIu64"\n",index,k,w->address,v->consumed,v->aw_appended,r->updates);}
            }
            v->valid_mask|=1U<<k;v->exhausted=0;v->aw_appended++;
#ifdef AW_NO_ROMFS
            aw_voice_note(index,2,k,v,w->address);
#endif
        }
    }
    v->input=*next;
}
static void aud_refresh_pool_usage(struct aud_renderer *r) {
    unsigned count=r->p.voices*4+r->p.effects;
    for(unsigned p=0;p<count;p++)r->pools[p].used=0;
    for(unsigned i=0;i<r->p.voices;i++) {
        struct aud_voice *v=&r->voices[i];if(!v->input.used)continue;
        for(unsigned j=0;j<9;j++) {
            uint64_t address,size;
            if(j==0){address=v->input.extra;size=v->input.extra_size;}
            else {unsigned k=(j-1)/2;if(!(v->valid_mask&(1U<<k)))continue;
                struct aud_wave *w=&v->input.wave[k];address=j&1?w->address:w->context;size=j&1?w->size:w->context_size;}
            if(!size)continue;
            for(unsigned p=0;p<count;p++) {
                struct aud_pool *pool=&r->pools[p];
                if(pool->state==5&&address>=pool->address&&address-pool->address<=pool->size&&size<=pool->size-(address-pool->address))pool->used=1;
            }
        }
    }
    for(unsigned i=0;i<r->p.effects;i++) {
        const struct aud_effect_in *e=&r->effects[i].input;
        if(!e->type)continue;
        for(unsigned j=0;j<(e->type==2?4U:1U);j++) {
            uint64_t address=e->address,size=e->size;
            if(e->type==2){memcpy(&address,e->data+64+j*8,8);size=j&1?(uint64_t)read32(e->data+56)*4:128;}
            if(!size)continue;
            for(unsigned p=0;p<count;p++) {
                struct aud_pool *pool=&r->pools[p];
                if(pool->state==5 && address>=pool->address && address-pool->address<=pool->size && size<=pool->size-(address-pool->address))pool->used=1;
            }
        }
    }
}
/* Local NextOS/GCC tuning only: no fast-math, decoder or cadence change.
 * Differential PCM/arena/state tests retain the original O2 implementations. */
static __attribute__((optimize("O3"))) unsigned aud_voice_source_sample(struct aud_renderer *r,struct aud_voice *v,int16_t *sample) {
    if(!(v->valid_mask&(1U<<v->head)))return 0;
    struct aud_wave *w=&v->input.wave[v->head];
    if(!v->active) {
        v->cursor=w->start;
        if(w->context) {
            const uint8_t *ctx=aud_snapshot_context(r,v,w);
            if(!ctx)ctx=aud_pool_pointer(r,w->context,6,0);
            memcpy(v->history[0],ctx+2,4);
        } else if(w->start)stop("AUDREN_OFFSET_CONTEXT_REQUIRED",0,w->start,20);
        memcpy(v->loop_history,v->history[0],4);v->active=1;
    }
    const uint8_t *encoded=aud_snapshot_block(r,v,w,v->cursor);
    unsigned decoded=encoded?cotm_dsp_sample(encoded,8,v->cursor%14,v->coefficients,v->history[0],sample):
        cotm_dsp_sample((const void *)(uintptr_t)w->address,w->size,v->cursor,v->coefficients,v->history[0],sample);
    if(!decoded)stop("AUDREN_ADPCM_BLOCK",0,v->cursor,24);
    v->cursor++;v->played++;
    if(v->cursor>=(uint32_t)w->end) {
        if(w->loop){v->cursor=w->start;memcpy(v->history[0],v->loop_history,4);}
        else {
            unsigned eos=w->eos;v->valid_mask&=~(1U<<v->head);v->consumed++;v->pool_usage_dirty=1;
#ifdef AW_NO_ROMFS
            aw_voice_note((unsigned)(v-r->voices),3,v->head,v,w->address);
#endif
            v->head=(v->head+1)%4;v->active=0;
            /* EOS resets the stream's sample counter, not the voice queue.
             * A following queued wave may already be valid (CotM's intro to
             * loop transition). Empty queues stop at the valid-mask check. */
            if(eos)v->played=0;
        }
    }
    return 1;
}
static __attribute__((optimize("O3"))) unsigned aud_voice_sample(struct aud_renderer *r,struct aud_voice *v,float *sample) {
    /* LC2 contains native DSP sources at 32/44.1/48 kHz. Pitch scales the
     * source clock; the renderer still produces its requested 48 kHz output.
     * At 48 kHz the ratio is exactly one, preserving the previous arithmetic. */
    double step=(double)v->input.pitch*((double)v->input.rate/(double)r->p.rate);
    if(!v->resample_active && step==1.0) {
        int16_t source=0;unsigned have=aud_voice_source_sample(r,v,&source);*sample=source;return have;
    }
    if(!v->resample_active) {
        v->resample_have[0]=aud_voice_source_sample(r,v,&v->resample_value[0]);
        if(!v->resample_have[0])return 0;
        v->resample_value[1]=0;v->resample_have[1]=aud_voice_source_sample(r,v,&v->resample_value[1]);
        v->position=0;v->resample_active=1;
    }
    *sample=(float)(v->resample_value[0]+(v->resample_value[1]-v->resample_value[0])*v->position);
    v->position+=step;
    while(v->position>=1 && v->resample_active) {
        v->position-=1;
        v->resample_value[0]=v->resample_value[1];v->resample_have[0]=v->resample_have[1];
        if(!v->resample_have[0]){v->resample_active=0;break;}
        v->resample_value[1]=0;v->resample_have[1]=aud_voice_source_sample(r,v,&v->resample_value[1]);
    }
    return 1;
}
#include "zero_pcm_voice.h"
static __attribute__((optimize("O3"))) void aud_mix_pcm(struct aud_renderer *r,int16_t pcm[480]) {
    unsigned profile_sample=(aud_mix_profile.sequence++&15U)==0;
    uint64_t phase[6]={0};aud_mix_profile.blocks++;
    if(profile_sample)phase[0]=aud_mix_profile_now();
    unsigned offsets[256]={0},total=0,count=r->p.submixes+1;
    for(unsigned i=0;i<count;i++){offsets[i]=total;if(r->mixes[i].input.used)total+=r->mixes[i].input.buffers;}
    if(total>r->p.mix_buffers)stop("AUDREN_MIX_ARENA_RANGE",0,total,24);
    memset(r->mix_pcm,0,240*(r->p.mix_buffers+6)*sizeof(float));memset(pcm,0,960);
    if(profile_sample)phase[1]=aud_mix_profile_now();
    for(unsigned i=0;i<r->p.voices;i++) {
        struct aud_voice *v=&r->voices[i];struct aud_voice_in *in=&v->input;
        if(!in->used||in->state!=0)continue;
        struct zero_voice_route routes[24];unsigned nroutes=zero_voice_routes(r,in,routes);
        for(unsigned n=0;n<240;n++) {
            float sample[6]={0};if(!(in->format==2?zero_pcm_sample(r,v,sample):aud_voice_sample(r,v,sample)))break;
            for(unsigned route=0;route<nroutes;route++) {
                struct zero_voice_route *d=&routes[route];float value=sample[d->channel]*in->volume;
                for(unsigned b=0;b<r->mixes[d->mix].input.buffers;b++)
                    r->mix_pcm[(offsets[d->mix]+b)*240+n]+=value*d->gain[b];
            }
        }
    }
    /* Process a submix only after every source feeding it was processed. */
    if(profile_sample)phase[2]=aud_mix_profile_now();
    unsigned done[256]={0};done[0]=1;
    for(unsigned pass=1;pass<count;pass++) {
        unsigned chosen=count;
        for(unsigned i=1;i<count;i++)if(!done[i]) {
            if(!r->mixes[i].input.used){chosen=i;break;}
            unsigned waiting=0;
            for(unsigned j=1;j<count;j++)if(j!=i&&!done[j]&&r->mixes[j].input.used&&r->mixes[j].input.destination==i)waiting=1;
            if(!waiting){chosen=i;break;}
        }
        if(chosen==count)stop("AUDREN_MIX_CYCLE",0,pass,24);
        done[chosen]=1;struct aud_mix_in *mix=&r->mixes[chosen].input;if(!mix->used)continue;
        zero_aud_effects_mix(r,chosen,offsets);
        unsigned dest=mix->destination;
        if(dest>=count||dest==chosen||!r->mixes[dest].input.used)stop("AUDREN_MIX_DESTINATION",0,dest,24);
        for(unsigned src=0;src<mix->buffers;src++)for(unsigned dst=0;dst<r->mixes[dest].input.buffers;dst++) {
            float gain=mix->matrix[src][dst]*mix->volume;if(!isfinite(gain))stop("AUDREN_MIX_GAIN",0,src,24);if(!gain)continue;
            float *in=r->mix_pcm+(offsets[chosen]+src)*240,*out=r->mix_pcm+(offsets[dest]+dst)*240;
            for(unsigned n=0;n<240;n++)out[n]+=in[n]*gain;
        }
    }
    zero_aud_effects_mix(r,0,offsets);
    if(profile_sample)phase[3]=aud_mix_profile_now();
    for(unsigned i=0;i<r->p.sinks;i++) {
        struct aud_sink_in *sink=&r->sinks[i].input;if(!sink->used)continue;
        unsigned channels=read32(sink->data+256);
        if(sink->type!=1||(channels!=2&&channels!=6)||sink->data[267]>1)stop("AUDREN_SINK_MIX_REQUIRED",0,channels,20);
        for(unsigned c=0;c<channels;c++)if(sink->data[260+c]>=r->mixes[0].input.buffers)stop("AUDREN_SINK_BUFFER",0,c,24);
        float downmix[4]={0};if(sink->data[267])memcpy(downmix,sink->data+268,16);
        for(unsigned n=0;n<240;n++) {
            for(unsigned c=2;!sink->data[267] && c<channels;c++)if(r->mix_pcm[sink->data[260+c]*240+n]!=0)stop("AUDREN_SURROUND_DOWNMIX_REQUIRED",0,c,20);
            for(unsigned c=0;c<2;c++) {
                float sample=r->mix_pcm[sink->data[260+c]*240+n];
                if(channels==6 && sink->data[267]) {
                    /* Device sink order (including REV6): FL, FR, FC, LFE, BL, BR.
                     * Reverb's legacy effect-channel mapping is independent. */
                    sample=nearbyintf(r->mix_pcm[sink->data[264+c]*240+n]*downmix[3]+
                        r->mix_pcm[sink->data[263]*240+n]*downmix[2]+r->mix_pcm[sink->data[262]*240+n]*downmix[1]+sample*downmix[0]);
                }
                float value=sample*r->mixes[0].input.volume*aud_output_volume+pcm[n*2+c];
                if(!isfinite(value))stop("AUDREN_OUTPUT_NONFINITE",0,c,24);
                r->output_samples++;
                if(fabsf(value)>r->peak_before_clip)r->peak_before_clip=fabsf(value);
                if(value>32767 || value<-32768)r->clipped_samples++;
                if(value>32767)value=32767;
                if(value<-32768)value=-32768;
                pcm[n*2+c]=(int16_t)lrintf(value);
            }
        }
    }
    if(profile_sample)phase[4]=aud_mix_profile_now();
    /* Pool usage only changes with voice/effect inputs or wave exhaustion. */
    for(unsigned i=0;i<r->p.voices;i++)if(r->voices[i].pool_usage_dirty){r->voices[i].pool_usage_dirty=0;r->pool_usage_dirty=1;}
    if(r->pool_usage_dirty){aud_refresh_pool_usage(r);r->pool_usage_dirty=0;}r->frames++;
    if(profile_sample) {
        phase[5]=aud_mix_profile_now();aud_mix_profile.samples++;
        for(unsigned i=0;i<5;i++)aud_mix_profile.phase_ns[i]+=phase[i+1]-phase[i];
    }
}
