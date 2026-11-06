/* Native Zero/ZX main+1bce720 acquires interleaved PCM16; +1bcef9c
 * acquires separate mono DSP voices. Splitter channel-stride routing matches
 * Ryujinx1df6c07 CommandGenerator.GenerateVoice. As in the inherited adapter,
 * pitch conversion is linear interpolation, not the Tegra DSP FIR. */
static unsigned zero_pcm_frame(struct aud_voice *v,int16_t sample[6]) {
    if(!(v->valid_mask&(1U<<v->head)))return 0;
    struct aud_wave *w=&v->input.wave[v->head];
    if(!v->active){v->cursor=w->start;v->active=1;}
    memcpy(sample,(const uint8_t *)(uintptr_t)w->address+(size_t)v->cursor*v->input.channels*2,v->input.channels*2);
    v->cursor++;v->played++;
    if(v->cursor>=(uint32_t)w->end) {
        if(w->loop)v->cursor=w->start;
        else {
            unsigned eos=w->eos;v->valid_mask&=~(1U<<v->head);v->consumed++;v->pool_usage_dirty=1;
#ifdef AW_NO_ROMFS
            if(aw_voices_base)aw_voice_note((unsigned)(v-aw_voices_base),3,v->head,v,w->address);
#endif
            v->head=(v->head+1)%4;v->active=0;if(eos)v->played=0;
        }
    }
    return 1;
}
static unsigned zero_pcm_sample(struct aud_renderer *r,struct aud_voice *v,float sample[6]) {
    double step=(double)v->input.pitch*v->input.rate/r->p.rate;
    if(!v->resample_active && step==1.0) {
        int16_t input[6]={0};unsigned have=zero_pcm_frame(v,input);
        for(unsigned c=0;c<v->input.channels;c++)sample[c]=input[c];
        return have;
    }
    if(!v->resample_active) {
        v->resample_have[0]=zero_pcm_frame(v,v->zero_pcm_value[0]);if(!v->resample_have[0])return 0;
        memset(v->zero_pcm_value[1],0,sizeof(v->zero_pcm_value[1]));v->resample_have[1]=zero_pcm_frame(v,v->zero_pcm_value[1]);
        v->position=0;v->resample_active=1;
    }
    for(unsigned c=0;c<v->input.channels;c++)sample[c]=(float)(v->zero_pcm_value[0][c]+(v->zero_pcm_value[1][c]-v->zero_pcm_value[0][c])*v->position);
    v->position+=step;
    while(v->position>=1 && v->resample_active) {
        v->position-=1;memcpy(v->zero_pcm_value[0],v->zero_pcm_value[1],sizeof(v->zero_pcm_value[0]));
        v->resample_have[0]=v->resample_have[1];if(!v->resample_have[0]){v->resample_active=0;break;}
        memset(v->zero_pcm_value[1],0,sizeof(v->zero_pcm_value[1]));v->resample_have[1]=zero_pcm_frame(v,v->zero_pcm_value[1]);
    }
    return 1;
}
struct zero_voice_route {unsigned channel,mix;const float *gain;};
static unsigned zero_voice_routes(struct aud_renderer *r,const struct aud_voice_in *in,struct zero_voice_route routes[24]) {
    unsigned n=0;
    for(unsigned c=0;c<in->channels;c++) {
        if(in->channel_ids[c]>=r->p.voices || !r->channels[in->channel_ids[c]].input.used)
            stop("AUDREN_UNUSED_CHANNEL",0,in->id,24);
        if(!aud_voice_mix_unset(r,in->mix))routes[n++]=(struct zero_voice_route){c,in->mix,r->channels[in->channel_ids[c]].input.gain};
        else if(in->splitter!=UINT32_MAX) {
            if(in->splitter>=r->p.splitters)stop("ZERO_AUDIO_VOICE_SPLITTER",0,in->splitter,24);
            struct zero_aud_splitter *s=&r->splitters[in->splitter];
            if(s->count>24 || (s->count&&s->rate!=r->p.rate))stop("ZERO_AUDIO_SPLITTER_RATE",0,s->rate,20);
            for(unsigned i=c;i<s->count;i+=in->channels) {
                if(s->ids[i]>=r->p.splitter_destinations)stop("ZERO_AUDIO_SPLITTER_DESTINATION_ID",0,s->ids[i],24);
                struct zero_aud_destination *d=&r->destinations[s->ids[i]];
                if(d->used && d->mix!=UINT32_MAX)routes[n++]=(struct zero_voice_route){c,d->mix,d->gain};
            }
        }
    }
    for(unsigned i=0;i<n;i++) {
        if(routes[i].mix>r->p.submixes || !r->mixes[routes[i].mix].input.used)stop("AUDREN_VOICE_ROUTE",0,in->id,24);
        for(unsigned b=0;b<r->mixes[routes[i].mix].input.buffers;b++)if(!isfinite(routes[i].gain[b]))stop("AUDREN_CHANNEL_GAIN",0,b,24);
    }
    return n;
}
