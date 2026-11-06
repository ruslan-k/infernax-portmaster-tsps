/* REV6 effect dispatch: exact guest buffer topology, ordered per submix.
 * Aux exchange uses signed PCM32 in the guest's shared rings; the native
 * client remains responsible for its callback and returned audio samples.
 * Protocol reference/provenance: vendor/ryujinx-audio/NOTICE.md. */
static uint64_t zero_aud_u64(const uint8_t *p) {uint64_t x;memcpy(&x,p,8);return x;}
static float zero_aud_float(const uint8_t *p) {float x;memcpy(&x,p,4);return x;}
static void *zero_aud_pool_write(struct aud_renderer *r,uint64_t address,uint64_t size,uint64_t pc) {
    void *p=aud_pool_pointer(r,address,size,pc);
    if(!accessible(address,size,PROT_WRITE))stop("ZERO_AUDIO_WRITE_RANGE",pc,address,24);
    return p;
}
static void zero_aud_effect_configure(struct aud_renderer *r,unsigned id,const struct aud_effect_in *next,uint64_t pc) {
    struct aud_effect *e=&r->effects[id];const uint8_t *d=next->data;
    if(next->type!=0 && next->type!=1 && next->type!=2 && next->type!=5 && next->type!=7 && next->type!=8)stop("AUDREN_EFFECT_REQUIRED",pc,next->type,20);
    if(next->is_new>1 || next->enabled>1 || (next->type && (next->mix>r->p.submixes || next->order>=r->p.effects)))
        stop("ZERO_AUDIO_EFFECT_RANGE",pc,id,24);
    if(e->input.type!=next->type || next->is_new){zero_reverb_free(e->reverb);e->reverb=NULL;e->state=next->type?1:0;}
    unsigned count=0;
    if(next->type==1) {
        count=read32(d+144);if(count>24)stop("ZERO_AUDIO_BUFFER_MIXER_COUNT",pc,count,24);
        for(unsigned i=0;i<count;i++)if(d[i]>=24 || d[24+i]>=24 || !isfinite(zero_aud_float(d+48+4*i)))
            stop("ZERO_AUDIO_BUFFER_MIXER_PARAMETER",pc,i,24);
    } else if(next->type==2) {
        count=read32(d+48);unsigned samples=read32(d+56),max_channels=read32(d+60);
        if(!count || count>24 || max_channels<count || max_channels>24 || read32(d+52)!=r->p.rate ||
           samples<count*240 || samples>(8U<<20))stop("ZERO_AUDIO_AUX_PARAMETER",pc,id,24);
        for(unsigned i=0;i<count;i++)if(d[i]>=24 || d[24+i]>=24)stop("ZERO_AUDIO_AUX_CHANNEL",pc,i,24);
        for(unsigned i=0;i<2;i++) {
            uint64_t header=zero_aud_u64(d+64+i*16),buffer=zero_aud_u64(d+72+i*16);
            zero_aud_pool_write(r,header,128,pc);zero_aud_pool_write(r,buffer,(uint64_t)samples*4,pc);
        }
    } else if(next->type==7) {
        /* LightLimiter (measured on Animal Well): input[6] +0, output[6] +6, max channels u16 +12, channels u16 +14, rate +16.
         * A pass-through keeps the mix audible; statistics stay zero. */
        count=d[14]|(d[15]<<8);
        if(!count || count>6 || (unsigned)(d[12]|(d[13]<<8))<count || read32(d+16)!=r->p.rate)stop("ZERO_AUDIO_LIMITER_PARAMETER",pc,id,24);
        for(unsigned i=0;i<count;i++)if(d[i]>=24 || d[6+i]>=24)stop("ZERO_AUDIO_LIMITER_CHANNEL",pc,i,24);
    } else if(next->type==8) {
        /* CaptureBuffer: AuxiliaryBufferParameter layout, send side only (the game reads the ring for rumble). */
        count=read32(d+48);unsigned samples=read32(d+56),max_channels=read32(d+60);
        if(!count || count>24 || max_channels<count || max_channels>24 || read32(d+52)!=r->p.rate ||
           samples<count*240 || samples>(8U<<20))stop("ZERO_AUDIO_CAPTURE_PARAMETER",pc,id,24);
        for(unsigned i=0;i<count;i++)if(d[i]>=24 || d[24+i]>=24)stop("ZERO_AUDIO_CAPTURE_CHANNEL",pc,i,24);
        uint64_t header=zero_aud_u64(d+64),buffer=zero_aud_u64(d+72);
        zero_aud_pool_write(r,header,128,pc);zero_aud_pool_write(r,buffer,(uint64_t)samples*4,pc);
    } else if(next->type==5) {
        struct zero_reverb_parameter p={0};memcpy(&p,d,73);
        if(!zero_reverb_valid(&p) || p.input[0]>=24 || p.output[0]>=24 || (p.channels==2 && (p.input[1]>=24 || p.output[1]>=24)))stop("ZERO_AUDIO_REVERB_PARAMETER",pc,id,20);
        aud_pool_pointer(r,next->address,next->size,pc);
        if(next->enabled) {
            if(!e->reverb || p.status==0){zero_reverb_free(e->reverb);e->reverb=zero_reverb_new(&p);}
            else if(p.status==1)zero_reverb_configure(e->reverb,&p);
            if(!e->reverb)stop("ZERO_AUDIO_REVERB_MEMORY",pc,id,21);
        }
    }
    if(!r->updates && next->type)printf("ZERO_AUDIO_EFFECT id=%u type=%u mix=%u order=%u enabled=%u channels=%u\n",
        id,next->type,next->mix,next->order,next->enabled,next->type==5?1:count);
    e->input=*next;
}
static void zero_aud_aux_process(struct aud_renderer *r,const struct aud_effect_in *e,float *mix,unsigned buffers) {
    const uint8_t *d=e->data;unsigned count=read32(d+48),samples=read32(d+56);
    uint8_t *send_header=zero_aud_pool_write(r,zero_aud_u64(d+64),128,0);
    uint8_t *return_header=zero_aud_pool_write(r,zero_aud_u64(d+80),128,0);
    int32_t *send=zero_aud_pool_write(r,zero_aud_u64(d+72),(uint64_t)samples*4,0);
    int32_t *receive=aud_pool_pointer(r,zero_aud_u64(d+88),(uint64_t)samples*4,0);
    unsigned write=read32(send_header+68),read=read32(return_header+64);
    if(write>=samples || read>=samples)stop("ZERO_AUDIO_AUX_CURSOR",0,write,24);
    if(!e->enabled){memset(send_header+64,0,64);memset(return_header+64,0,64);}
    for(unsigned c=0;c<count;c++) {
        if(d[c]>=buffers || d[24+c]>=buffers)stop("ZERO_AUDIO_AUX_MIX_RANGE",0,c,24);
        float *in=mix+d[c]*240,*out=mix+d[24+c]*240;
        if(!e->enabled){if(in!=out)memcpy(out,in,240*sizeof(float));continue;}
        unsigned send_at=(write+c*240)%samples,receive_at=(read+c*240)%samples;
        /* Capture a complete planar channel before replacing an aliased output. */
        for(unsigned n=0;n<240;n++) {
            float value=in[n];
            if(!isfinite(value) || (double)value<INT32_MIN || (double)value>INT32_MAX)stop("ZERO_AUDIO_AUX_PCM32_RANGE",0,n,24);
            send[(send_at+n)%samples]=(int32_t)value;
        }
        for(unsigned n=0;n<240;n++)out[n]=(float)receive[(receive_at+n)%samples];
    }
    if(e->enabled) {
        put32(send_header+68,(write+count*240)%samples);put32(return_header+64,(read+count*240)%samples);
    }
}
static void zero_aud_capture_process(struct aud_renderer *r,const struct aud_effect_in *e,float *mix,unsigned buffers) {
    const uint8_t *d=e->data;unsigned count=read32(d+48),samples=read32(d+56);
    uint8_t *send_header=zero_aud_pool_write(r,zero_aud_u64(d+64),128,0);
    int32_t *send=zero_aud_pool_write(r,zero_aud_u64(d+72),(uint64_t)samples*4,0);
    unsigned write=read32(send_header+68);
    if(write>=samples)stop("ZERO_AUDIO_CAPTURE_CURSOR",0,write,24);
    if(!e->enabled)memset(send_header+64,0,64);
    for(unsigned c=0;c<count;c++) {
        if(d[c]>=buffers || d[24+c]>=buffers)stop("ZERO_AUDIO_CAPTURE_MIX_RANGE",0,c,24);
        float *in=mix+d[c]*240,*out=mix+d[24+c]*240;
        if(e->enabled) {
            unsigned send_at=(write+c*240)%samples;
            for(unsigned n=0;n<240;n++) {
                float value=in[n];
                if(!isfinite(value))value=0;
                if((double)value<INT32_MIN)value=(float)INT32_MIN;
                if((double)value>INT32_MAX)value=(float)INT32_MAX;
                send[(send_at+n)%samples]=(int32_t)value;
            }
        }
        if(in!=out)memcpy(out,in,240*sizeof(float));
    }
    if(e->enabled)put32(send_header+68,(write+count*240)%samples);
}
static void zero_aud_effects_mix(struct aud_renderer *r,unsigned id,const unsigned *offsets) {
    struct aud_mix_in *m=&r->mixes[id].input;float *mix=r->mix_pcm+offsets[id]*240;
    /* This stack index exists for one mix call only. Keep every DSP call
     * and its order; a duplicate fails when its original order is reached,
     * after exactly the same preceding effects have updated their state. */
    struct aud_effect *ordered[256]={0};uint8_t duplicate[256]={0};
    for(unsigned i=0;i<r->p.effects;i++) {
        struct aud_effect *e=&r->effects[i];unsigned order=e->input.order;
        if(e->input.type && e->input.mix==id && order<r->p.effects) {
            if(ordered[order])duplicate[order]=1;else ordered[order]=e;
        }
    }
    for(unsigned order=0;order<r->p.effects;order++) {
        if(duplicate[order])stop("ZERO_AUDIO_EFFECT_DUPLICATE_ORDER",0,id,24);
        struct aud_effect *chosen=ordered[order];
        if(!chosen)continue;
        struct aud_effect_in *e=&chosen->input;const uint8_t *d=e->data;
        if(e->type==1) {
            unsigned count=read32(d+144);
            for(unsigned c=0;c<count;c++) {
                if(d[c]>=m->buffers || d[24+c]>=m->buffers)stop("ZERO_AUDIO_BUFFER_MIXER_RANGE",0,c,24);
                float gain=zero_aud_float(d+48+c*4),*in=mix+d[c]*240,*out=mix+d[24+c]*240;
                if(e->enabled)for(unsigned n=0;n<240;n++)out[n]+=in[n]*gain;
            }
        } else if(e->type==2)zero_aud_aux_process(r,e,mix,m->buffers);
        else if(e->type==8)zero_aud_capture_process(r,e,mix,m->buffers);
        else if(e->type==7) {
            unsigned count=d[14]|(d[15]<<8);
            for(unsigned c=0;c<count;c++) {
                if(d[c]>=m->buffers || d[6+c]>=m->buffers)stop("ZERO_AUDIO_LIMITER_MIX_RANGE",0,c,24);
                float *in=mix+d[c]*240,*out=mix+d[6+c]*240;
                if(in!=out)memcpy(out,in,240*sizeof(float));
            }
        }
        else if(e->type==5) {
            uint16_t channels;memcpy(&channels,d+14,2);
            if(d[0]>=m->buffers || d[6]>=m->buffers || (channels==2 && (d[1]>=m->buffers || d[7]>=m->buffers)))stop("ZERO_AUDIO_REVERB_MIX_RANGE",0,id,24);
            float *in=mix+d[0]*240,*out=mix+d[6]*240;
            if(channels==2) {
                float *in1=mix+d[1]*240,*out1=mix+d[7]*240;
                if(e->enabled)zero_reverb_process_stereo(chosen->reverb,in,in1,out,out1,240);
                else {if(in!=out)memcpy(out,in,240*sizeof(float));if(in1!=out1)memcpy(out1,in1,240*sizeof(float));}
            } else if(e->enabled)zero_reverb_process(chosen->reverb,in,out,240);
            else if(in!=out)memcpy(out,in,240*sizeof(float));
        }
        chosen->state=2;
    }
}
