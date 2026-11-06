/* CotM renderer adapter, REV4. Wire layouts: pinned switchbrew/libnx audren.h.
 * The work-buffer ABI reports this CPU renderer's actual arena requirement,
 * not the unrelated Tegra DSP implementation's allocation size. The arena is
 * backed by the caller's transfer memory and retained until renderer close.
 * Unsupported rendering commands stop; no successful silent audio substitute.
 */
#define AUD_REV4 0x34564552U
#define AUD_REV6 0x36564552U
#define AUD_REV12 0x3c564552U /* Animal Well SDK 17.5.3; wire layouts of REV6 are retained for this game's feature set */
static unsigned aud_revision_supported(uint32_t revision) {return revision==AUD_REV4 || revision==AUD_REV6 || revision==AUD_REV12;}
static unsigned aud_rev6_plus(uint32_t revision) {return revision==AUD_REV6 || revision==AUD_REV12;}
#include "audren_wire.h"
#include "zero_reverb.h"
struct aud_parameter { uint32_t rate,samples,mix_buffers,submixes,voices,sinks,effects,perf_frames;
    uint32_t flags,splitters,splitter_destinations,reserved,revision; };
_Static_assert(sizeof(struct aud_parameter)==52,"audren parameter ABI");
struct aud_pool { uint64_t address,size;uint32_t state,used; };
struct aud_voice { struct aud_voice_in input;uint64_t played;uint32_t consumed,drops,head,active;unsigned pool_usage_dirty;
    double position;int16_t history[6][2];float filter_history[6][2][2];
    uint32_t valid_mask,cursor,exhausted,aw_appended;int16_t coefficients[16],loop_history[2];
    unsigned resample_active,resample_have[2];int16_t resample_value[2],zero_pcm_value[2][6]; };
struct aud_channel { struct aud_channel_in input; };
struct aud_mix { struct aud_mix_in input; };
struct aud_effect { struct aud_effect_in input;uint32_t state;struct zero_reverb *reverb; };
struct aud_sink { struct aud_sink_in input;uint32_t written; };
struct zero_aud_splitter {uint32_t rate,count,ids[24];};
struct zero_aud_destination {uint32_t mix,used;float gain[24];};
struct aud_renderer { unsigned open,started;uint32_t transfer,event,time_limit;
    struct aud_parameter p;struct aud_pool *pools;struct aud_voice *voices;
    struct aud_channel *channels;struct aud_mix *mixes;struct aud_effect *effects;
    struct aud_sink *sinks;struct zero_aud_splitter *splitters;struct zero_aud_destination *destinations;
    float *mix_pcm;uint64_t frames,updates;unsigned pool_usage_dirty;
    uint64_t output_samples,clipped_samples;float peak_before_clip; };
static struct aud_renderer aud_renderers[2];
static void zero_aud_effects_mix(struct aud_renderer *,unsigned,const unsigned *);
static float aud_output_volume=1.0f;
static float aud_inactive_volumes[3]={1.0f,1.0f,1.0f};
static uint32_t aud_device_events[3];
static unsigned aud_output_ready;
static void aud_host_request(unsigned operation,uint64_t pc);
static void aud_parameter_validate(const struct aud_parameter *p,uint64_t pc) {
    printf("ZERO_AUDIO_PARAMETERS revision=0x%x rate=%u samples=%u buffers=%u submixes=%u voices=%u sinks=%u effects=%u perf=%u flags=0x%x splitters=%u destinations=%u context=%u\n",
           p->revision,p->rate,p->samples,p->mix_buffers,p->submixes,p->voices,p->sinks,p->effects,p->perf_frames,p->flags,p->splitters,p->splitter_destinations,p->reserved);
    if(!aud_revision_supported(p->revision))stop("AUDREN_REVISION_UNSUPPORTED",pc,p->revision,20);
    if(p->rate!=48000 || p->samples!=240 || !p->voices || p->voices>1024 ||
       !p->sinks || p->sinks>16 || p->submixes>255 || !p->mix_buffers || p->mix_buffers>256 ||
       p->effects>256)stop("AUDREN_PARAMETER_UNSUPPORTED",pc,p->voices,20);
    if(p->perf_frames || p->flags>1 || p->reserved || p->splitters>1024 || p->splitter_destinations>6144 ||
       (!!p->splitters!=!!p->splitter_destinations) || (!aud_rev6_plus(p->revision) && p->splitters))
        stop("AUDREN_PARAMETER_FEATURE_UNSUPPORTED",pc,p->flags,20);
}
static uint64_t aud_layout(struct aud_renderer *r,void *arena,const struct aud_parameter *p) {
    uint64_t offset=0;
#define AUD_ALLOC(member,type,count) do {offset=(offset+63)&~63ULL; \
    if(r){r->member=(type *)((uint8_t *)arena+offset);}offset+=(uint64_t)(count)*sizeof(type);}while(0)
    AUD_ALLOC(pools,struct aud_pool,p->voices*4+p->effects);
    AUD_ALLOC(voices,struct aud_voice,p->voices);
    AUD_ALLOC(channels,struct aud_channel,p->voices);
    AUD_ALLOC(mixes,struct aud_mix,p->submixes+1);
    AUD_ALLOC(effects,struct aud_effect,p->effects);
    AUD_ALLOC(sinks,struct aud_sink,p->sinks);
    AUD_ALLOC(splitters,struct zero_aud_splitter,p->splitters);
    AUD_ALLOC(destinations,struct zero_aud_destination,p->splitter_destinations);
    AUD_ALLOC(mix_pcm,float,p->samples*(p->mix_buffers+6));
#undef AUD_ALLOC
    return (offset+4095)&~4095ULL;
}
#include "audren_snapshot.h"
static void aud_destroy(unsigned state) {
    aud_snapshot_wait_locked();
    if(!state || state>2 || !aud_renderers[state-1].open)stop("AUDREN_CLOSE_INVALID",0,state,24);
    struct aud_renderer *r=&aud_renderers[state-1];
    r->started=0;if(r->event)session_close(r->event,0);
    for(unsigned i=0;i<r->p.effects;i++)zero_reverb_free(r->effects[i].reverb);
    session_close(r->transfer,0);memset(r,0,sizeof(*r));
}
#ifdef AW_NO_ROMFS
struct aw_voice_event {uint64_t update;uint8_t kind,slot,head,valid;uint16_t consumed,appended;uint32_t address;};
static struct aw_voice_event aw_voice_history[512][16];static uint8_t aw_voice_history_next[512];
static uint64_t aw_audio_updates;static struct aud_voice *aw_voices_base;static uint32_t aw_reported_consumed[512];static uint8_t aw_reported_used[512];
static void aw_voice_note(unsigned voice,unsigned kind,unsigned slot,const struct aud_voice *v,uint64_t address){
    if(voice>=512)
        return;
    struct aw_voice_event *e=&aw_voice_history[voice][aw_voice_history_next[voice]++%16];
    e->update=aw_audio_updates;e->kind=(uint8_t)kind;e->slot=(uint8_t)slot;e->head=(uint8_t)v->head;e->valid=(uint8_t)v->valid_mask;e->consumed=(uint16_t)v->consumed;e->appended=(uint16_t)v->aw_appended;e->address=(uint32_t)address;
}
static void aw_voice_compare(uint64_t head){
    uint64_t node=0;memcpy(&node,(void *)(uintptr_t)(head+8),8);
    for(unsigned n=0;n<400&&node!=head;n++){
        if(!accessible(node,0x1c0,PROT_READ))break;
        uint32_t id=0,rel=0,con=0;int32_t cnt=0;uint8_t used=0;memcpy(&id,(void *)(uintptr_t)(node+64),4);memcpy(&rel,(void *)(uintptr_t)(node+48),4);memcpy(&con,(void *)(uintptr_t)(node+440),4);memcpy(&cnt,(void *)(uintptr_t)(node+124),4);memcpy(&used,(void *)(uintptr_t)(node+73),1);
        if(id<512&&(con||rel||aw_reported_consumed[id]))printf("AW_AUDIO_COMPARE id=%u node=0x%"PRIx64" client-consumed=%u client-released=%u client-count=%d in-use=%u ours-reported=%u\n",id,node,con,rel,cnt,used,aw_reported_consumed[id]);
        uint64_t next=0;memcpy(&next,(void *)(uintptr_t)(node+8),8);node=next;
    }
}
static void aw_voice_dump(unsigned voice){
    if(voice>=512)
        return;
    for(unsigned n=0;n<16;n++){const struct aw_voice_event *e=&aw_voice_history[voice][(aw_voice_history_next[voice]+n)%16];if(!e->kind)continue;
        printf("AW_VOICE_HISTORY voice=%u update=%"PRIu64" kind=%s slot=%u head=%u valid=0x%x consumed=%u appended=%u address=0x%x\n",voice,e->update,
            e->kind==1?"new":e->kind==2?"append":e->kind==3?"consume":e->kind==4?"stopping":e->kind==5?"unused":e->kind==6?"flush":e->kind==7?"reported":"?",e->slot,e->head,e->valid,e->consumed,e->appended,e->address);}
}
#endif
#ifdef AW_NO_ROMFS
static void *aw_audio_stale_guard(void *opaque) {
    unsigned voices=(unsigned)(uintptr_t)opaque;uint64_t manager=0xa151000ULL,fixes=0;
    for(;;) {
        struct timespec pause={0,1000000};nanosleep(&pause,NULL);
        uint64_t base=0;int32_t count=-1;
        memcpy(&count,(void *)(uintptr_t)manager,4);memcpy(&base,(void *)(uintptr_t)(manager+8),8);
        if(count!=(int32_t)voices||!base)continue;
        for(unsigned i=0;i<voices;i++) {
            uint8_t *v=(uint8_t *)(uintptr_t)(base+(uint64_t)i*0x210);
            if(!v[73])continue;
            uint32_t released,consumed;int32_t pending;
            memcpy(&released,v+48,4);memcpy(&pending,v+124,4);memcpy(&consumed,v+440,4);
            if(pending<0||pending>4)continue;
            uint32_t limit=released+(uint32_t)pending;
            if(consumed>limit) {
                memcpy(v+440,&limit,4);
                if(fixes++<16)printf("AW_AUDIO_STALE_CONSUMED voice=%u consumed=%u released=%u pending=%d lowered-to=%u\n",i,consumed,released,pending,limit);
            }
        }
    }
    return NULL;
}
#endif
#include "aw_release.h"
#include "audren_voice.h"
#include "zero_audio_splitters.h"
#include "zero_audio_effects.h"
static void aud_update(struct aud_renderer *r,struct ipc_request *req,uint64_t pc) {
    if(req->num_inputs!=1 || !req->num_outputs || req->num_outputs>2 || req->inputs[0].size<64 || req->inputs[0].size>(1U<<20))
        stop("AUDREN_UPDATE_BUFFERS",pc,req->num_inputs,24);
    const uint8_t *in=(const void *)(uintptr_t)req->inputs[0].address;
    struct aud_header h;memcpy(&h,in,64);
    if(!r->updates)printf("ZERO_AUDIO_UPDATE revision=0x%x behavior=%u pools=%u voices=%u channels=%u effects=%u mixes=%u sinks=%u perf=%u extensions=%u,%u,%u,%u,%u,%u total=%u output=%"PRIu64"\n",
        h.revision,h.behavior,h.pools,h.voices,h.channels,h.effects,h.mixes,h.sinks,h.performance,h.reserved[0],h.reserved[1],h.reserved[2],h.reserved[3],h.reserved[4],h.reserved[5],h.total,req->outputs[0].size);
    unsigned pools=r->p.voices*4+r->p.effects;
    if(h.reserved[0]>(1U<<20) || (!r->p.splitters && h.reserved[0]))stop("ZERO_AUDIO_SPLITTER_SIZE",pc,h.reserved[0],24);
    /* REV7+ sends the mix section as a 32-byte dirty-only header (count) followed by only the dirty mix parameters. */
    unsigned mix_dirty_only=r->p.revision==AUD_REV12;
    uint32_t mixes_expected=mix_dirty_only?h.mixes:(r->p.submixes+1)*0x930;
    if(mix_dirty_only && (h.mixes<32 || (h.mixes-32)%0x930 || (h.mixes-32)/0x930>r->p.submixes+1U))stop("AUDREN_MIX_DIRTY_SECTION",pc,h.mixes,24);
    uint32_t input_size=64+16+pools*32+r->p.voices*(0x170+0x70)+r->p.effects*0xc0+mixes_expected+r->p.sinks*0x140+16+h.reserved[0];
    unsigned renderer_info=aud_rev6_plus(r->p.revision)?16:0;
    /* REV10+: EffectOutStatusVersion2 = state + EffectResultState (0x80) = 0x90 per effect. */
    unsigned effect_out=r->p.revision==AUD_REV12?0x90:16;
    uint32_t output_size=64+176+pools*16+r->p.voices*16+r->p.effects*effect_out+r->p.sinks*32+16+renderer_info;
    if(h.revision!=r->p.revision || h.behavior!=16 || h.pools!=pools*32 || h.voices!=r->p.voices*0x170 ||
       h.channels!=r->p.voices*0x70 || h.effects!=r->p.effects*0xc0 || h.mixes!=mixes_expected ||
       h.sinks!=r->p.sinks*0x140 || h.performance!=16 || h.total!=input_size || req->inputs[0].size<input_size ||
       req->outputs[0].size<output_size)stop("AUDREN_UPDATE_LAYOUT",pc,h.total,24);
    for(unsigned i=1;i<6;i++)if(h.reserved[i])stop("AUDREN_UPDATE_EXTENSION",pc,h.reserved[i],20);
    if(read32(in+64)!=r->p.revision || read32(in+72) || read32(in+76))stop("AUDREN_BEHAVIOR_FLAGS",pc,read32(in+72),20);
    /* Keep one private protocol snapshot per diagnostic run, outside source. */
    if(!r->updates && current_guest_context && !aw_release_mode()) {
        const char *run=getenv("COTM_RUN_ID");char path[96];
        if(!run || strspn(run,"0123456789")!=strlen(run) || strlen(run)>12)stop("AUDREN_RUN_ID",pc,0,24);
        snprintf(path,sizeof(path),"audren-update-%s.bin",run);
        int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(fd<0 || write(fd,in,input_size)!=input_size)stop("AUDREN_SNAPSHOT_FAILED",pc,errno,21);
        close(fd);
    }
    uint8_t *out=(void *)(uintptr_t)req->outputs[0].address;memset(out,0,output_size);
    struct aud_header oh={.revision=r->p.revision,.behavior=176,.pools=pools*16,.voices=r->p.voices*16,
        .effects=r->p.effects*effect_out,.sinks=r->p.sinks*32,.performance=16,.total=output_size};
    oh.reserved[1]=renderer_info;memcpy(out,&oh,64);
    if(renderer_info)memcpy(out+output_size-renderer_info,&r->frames,8);
    /* SDK 17 reads the 176-byte behaviour error section right after the output header (10 x {result,extra} + count);
     * it stays zero (no errors) and the pool/voice/effect/sink status follow it. Older revisions keep the proven layout. */
#ifdef AW_NO_ROMFS
    {   /* AW_AUDIO_CLIENT_CHECK: walk the SDK client VoiceInfoManager list (main.data+0x61000, list head +0x10, next at node+8)
         * and report the first voice whose appended-wave count (+124) leaves 0..4 (sdk GetReleasedWaveBuffer 0x1df1d0). */
        static int cfg;static unsigned on;if(!cfg){const char *o=getenv("AW_AUDIO_CLIENT_CHECK");on=o&&!strcmp(o,"1");cfg=1;}
        if(on) {
            uint64_t head=0xa151010ULL,node=0;
            if(accessible(head,16,PROT_READ)){memcpy(&node,(void *)(uintptr_t)(head+8),8);
                static unsigned reported;
                for(unsigned n=0;n<512&&node!=head&&accessible(node,0x1c0,PROT_READ);n++) {
                    int32_t count;int16_t whead;uint32_t released,consumed;uint64_t slots[5];
                    memcpy(&count,(void *)(uintptr_t)(node+124),4);memcpy(&whead,(void *)(uintptr_t)(node+128),2);
                    memcpy(&released,(void *)(uintptr_t)(node+48),4);memcpy(&consumed,(void *)(uintptr_t)(node+440),4);
                    memcpy(slots,(void *)(uintptr_t)(node+8),40);
                    if((count<0||count>4||whead<0||whead>3)&&reported<6){reported++;
                        printf("AW_AUDIO_CLIENT_BAD node=0x%"PRIx64" count=%d head=%d released=%u consumed=%u next=0x%"PRIx64" slots=0x%"PRIx64",0x%"PRIx64",0x%"PRIx64",0x%"PRIx64" update=%"PRIu64"\n",
                            node,count,whead,released,consumed,slots[0],slots[1],slots[2],slots[3],slots[4],r->updates);
                        for(unsigned vi=0;vi<r->p.voices;vi++)if(r->voices[vi].consumed==consumed&&r->voices[vi].input.used)
                            printf("AW_AUDIO_CLIENT_MATCH voice=%u consumed=%u appended=%u head=%u valid=0x%x state=%u\n",vi,r->voices[vi].consumed,r->voices[vi].aw_appended,r->voices[vi].head,r->voices[vi].valid_mask,r->voices[vi].input.state);
                    }
                    uint64_t next;memcpy(&next,(void *)(uintptr_t)(node+8),8);node=next;
                }
            }
        }
    }
#endif
#ifdef AW_NO_ROMFS
    aw_audio_updates=r->updates;aw_voices_base=r->voices;
#endif
#ifdef AW_NO_ROMFS
    {   /* Stale-consumed guard. nn::audio::AcquireVoiceInfo (sdk 0x1df850) resets released(+48)/count(+124)/head(+128)
         * but keeps the OutStatus last copied into VoiceInfo+440 (consumed). GetReleasedWaveBuffer (0x1df1d0) pops while
         * consumed != released without checking the count; a voice re-acquired and polled before the next renderer
         * update releases phantom wave buffers and the next AppendWaveBuffer writes slot -1 (VoiceInfo+8, the used-list
         * link) -> CPU fault in UpdateVoiceRelatedParameters (measured, Animal Well, async logic). The console runs ~3
         * renderer updates per game frame; here logic can outrun the guest audio thread. A host thread lowers +440 to
         * released+count (the most the client can legitimately release) every millisecond. */
        static int started;
        if(!started){started=1;pthread_t guard;if(pthread_create(&guard,NULL,aw_audio_stale_guard,(void *)(uintptr_t)r->p.voices)==0)pthread_detach(guard);}
    }
#endif
    uint32_t ip=80,op=64+(r->p.revision==AUD_REV12?176:0),attached=0,used_voices=0,used_mixes=0,used_sinks=0;
    for(unsigned i=0;i<pools;i++,ip+=32,op+=16) {
        struct aud_pool *p=&r->pools[i];uint64_t address,size;memcpy(&address,in+ip,8);memcpy(&size,in+ip+8,8);
        uint32_t state=read32(in+ip+16);
        if(state==4) {
            if(!address || !size || (address|size)%4096 || !accessible(address,size,PROT_READ))stop("AUDREN_POOL_INVALID",pc,address,24);
            if(p->state==5 && (p->address!=address || p->size!=size))stop("AUDREN_POOL_REPLACE_ATTACHED",pc,i,24);
            *p=(struct aud_pool){address,size,5,p->used};put32(out+op,5);
        } else if(state==2) {
            if(aud_rev6_plus(r->p.revision) && (!address||!size||(address|size)%4096||p->state!=5||p->address!=address||p->size!=size))
                stop("ZERO_AUDIO_POOL_DETACH_IDENTITY",pc,i,24);
            if(p->used) {
                if(!aud_rev6_plus(r->p.revision))stop("AUDREN_POOL_DETACH_IN_USE",pc,i,24);
                /* Native PoolMapper leaves OutStatus unchanged when unmap
                 * is still in use. Keep the mapping and zero/no-change reply;
                 * the SDK retries after these voices release their backing. */
                static unsigned waits;if(waits++<8)printf("ZERO_AUDIO_POOL_DETACH_PENDING pool=%u backing-retained=1 reply-state=0\n",i);
            } else {*p=(struct aud_pool){.state=3};put32(out+op,3);}
        } else if(state>6)stop("AUDREN_POOL_STATE",pc,state,24);
        if(p->state==5)attached++;
    }
    for(unsigned i=0;i<r->p.voices;i++,ip+=0x70) {
        memcpy(&r->channels[i].input,in+ip,0x70);
        if(r->channels[i].input.used && r->channels[i].input.id!=i)stop("AUDREN_CHANNEL_ID",pc,i,24);
    }
    for(unsigned i=0;i<r->p.voices;i++,ip+=0x170,op+=16) {
        struct aud_voice *v=&r->voices[i];struct aud_voice_in next;memcpy(&next,in+ip,sizeof(next));
        if(next.used)used_voices++;
        aud_voice_configure(r,i,&next,pc);
        v->input=next;memcpy(out+op,&v->played,8);put32(out+op+8,v->consumed);put32(out+op+12,v->drops);
#ifdef AW_NO_ROMFS
        if(i<512&&(aw_reported_consumed[i]!=v->consumed||aw_reported_used[i]!=next.used)){aw_reported_consumed[i]=v->consumed;aw_reported_used[i]=next.used;aw_voice_note(i,7,next.used,v,next.is_new);}
#endif
        if(v->consumed>v->aw_appended){static unsigned bad;if(bad<16){bad++;printf("AW_AUDIO_CONSUMED_OVER voice=%u consumed=%u appended=%u head=%u valid=0x%x used=%u new=%u state=%u\n",i,v->consumed,v->aw_appended,v->head,v->valid_mask,next.used,next.is_new,next.state);}}
    }
    for(unsigned i=0;i<r->p.effects;i++,ip+=0xc0,op+=effect_out) {
        struct aud_effect_in next;memcpy(&next,in+ip,sizeof(next));
        zero_aud_effect_configure(r,i,&next,pc);
        out[op]=(!r->started && r->effects[i].state!=1) || !next.type?4:3;
    }
    if(r->p.splitters){zero_aud_splitter_update(r,in+ip,h.reserved[0],pc);ip+=h.reserved[0];}
    if(mix_dirty_only) {
        /* MixInParameterDirtyOnlyUpdate: magic 0xcafebabe, then the count of mix parameters that follow. */
        if(read32(in+ip)!=0xcafebabeU)stop("AUDREN_MIX_DIRTY_MAGIC",pc,read32(in+ip),24);
        uint32_t dirty=read32(in+ip+4);ip+=32;
        if(dirty!=(h.mixes-32)/0x930)stop("AUDREN_MIX_DIRTY_COUNT",pc,dirty,24);
        for(unsigned k=0;k<dirty;k++,ip+=0x930) {
            struct aud_mix_in next;memcpy(&next,in+ip,sizeof(next));
            unsigned i=next.id==0x7fffffffU?r->p.submixes:next.id;  /* final mix id is the last slot */
            if(i>r->p.submixes)stop("AUDREN_MIX_ID",pc,next.id,24);
            if(next.used) {
                if(next.buffers>24 || !next.buffers || next.rate!=48000 || !isfinite(next.volume) || next.splitter!=UINT32_MAX)stop("AUDREN_MIX_INVALID",pc,i,24);
            }
            r->mixes[i].input=next;
        }
        for(unsigned i=0;i<=r->p.submixes;i++)if(r->mixes[i].input.used)used_mixes++;
    } else for(unsigned i=0;i<=r->p.submixes;i++,ip+=0x930) {
        struct aud_mix_in next;memcpy(&next,in+ip,sizeof(next));
        if(next.used) {
            if(next.id!=i || next.buffers>24 || !next.buffers || next.rate!=48000 || !isfinite(next.volume) ||
               next.splitter!=UINT32_MAX)stop("AUDREN_MIX_INVALID",pc,i,24);
            used_mixes++;
        }
        r->mixes[i].input=next;
    }
    for(unsigned i=0;i<r->p.sinks;i++,ip+=0x140,op+=32) {
        struct aud_sink_in next;memcpy(&next,in+ip,sizeof(next));
        if(next.used) {
            used_sinks++;
            if(next.type!=1 || memcmp(next.data,"MainAudioOut\0",13) || read32(next.data+256)>6 || next.data[267]>1 ||
               (next.data[267] && !aud_rev6_plus(r->p.revision)))
                stop("AUDREN_SINK_REQUIRED",pc,next.type,20);
            for(unsigned c=0;next.data[267] && c<4;c++)if(!isfinite(zero_aud_float(next.data+268+c*4)))
                stop("ZERO_AUDIO_SINK_COEFFICIENT",pc,c,24);
        }
        r->sinks[i].input=next;put32(out+op,r->sinks[i].written);
    }
    /* SDK6.5 +8287c writes only the target-node word; remaining12 bytes
     * are padding, not a request. +a1f00 returns HistorySize=0 whenever
     * the renderer has no performance manager, without inspecting input.
     * This adapter accepts only perf_frames=0 and already zeroed output. */
    if(r->p.perf_frames)stop("AUDREN_PERFORMANCE_REQUEST",pc,r->p.perf_frames,20);
    aud_refresh_pool_usage(r);r->pool_usage_dirty=0;
    r->updates++;
    if(r->updates<=4 || !(r->updates&(r->updates-1))) {
        struct line log={0};text(&log,"AUDREN_UPDATE count=");hex(&log,r->updates);text(&log," attached-pools=");hex(&log,attached);
        text(&log," used-voices=");hex(&log,used_voices);text(&log," mixes=");hex(&log,used_mixes);text(&log," sinks=");hex(&log,used_sinks);emit(&log);
    }
}
static int aud_dispatch(uint8_t *buffer,struct session *s,struct ipc_request *req,enum endpoint endpoint,unsigned *state,uint64_t pc) {
    if(endpoint!=EP_AUDREN_MANAGER && endpoint!=EP_AUDREN && endpoint!=EP_AUDIO_DEVICE)return 0;
    aud_snapshot_wait_locked();
    if(endpoint==EP_AUDREN_MANAGER && (req->command==2 || req->command==4)) {
        unsigned offset=req->command==4?8:0;uint64_t aruid;
        if(req->data_size<offset+8 || (offset && !aud_revision_supported(read32(req->data))))stop("AUDREN_DEVICE_PARAMETER",pc,req->data_size,24);
        memcpy(&aruid,req->data+offset,8);
        if(aruid!=main_thread_id)stop("AUDREN_DEVICE_ARUID",pc,aruid,24);
        ipc_child(buffer,s,req,EP_AUDIO_DEVICE,pc);return 1;
    }
    if(endpoint==EP_AUDIO_DEVICE) {
        /* The device service exists independently of renderer start. This
         * port's configured endpoint is the strict stereo NextOS provider;
         * names, gains and initially unsignalled change events are available
         * before the first stream. Opening a stream still verifies the actual
         * obtained channel/rate contract in audren_host.h. */
        static const char name[256]="AudioTvOutput";
        if(req->command==0 || req->command==6 || req->command==3 || req->command==10) {
            if(req->num_outputs!=1 || req->outputs[0].size<256)stop("AUDREN_DEVICE_NAME_BUFFER",pc,req->num_outputs,24);
            memcpy((void *)(uintptr_t)req->outputs[0].address,name,256);
            uint32_t count=1;unsigned list=req->command==0 || req->command==6;
            ipc_reply(buffer,req->token,0,&count,list?4:0,0,req->domain);return 1;
        }
        if(req->command==5){uint32_t channels=2;ipc_reply(buffer,req->token,0,&channels,4,0,req->domain);return 1;}
        if(req->command==4 || req->command==11 || req->command==12) {
            unsigned i=req->command==4?0:req->command==11?1:2;
            if(!aud_device_events[i])aud_device_events[i]=session_new(EP_EVENT,pc);
            ipc_copy_events(buffer,req,&aud_device_events[i],1,pc);return 1;
        }
        if(req->command==1 || req->command==2 || req->command==7 || req->command==8) {
            if(req->num_inputs!=1 || req->inputs[0].size>256 || !req->inputs[0].size)stop("AUDREN_DEVICE_NAME_BUFFER",pc,req->num_inputs,24);
            const char *input_name=(void *)(uintptr_t)req->inputs[0].address;
            if(!memchr(input_name,0,req->inputs[0].size))stop("AUDREN_DEVICE_NAME_TERMINATOR",pc,req->inputs[0].size,24);
            float *stored=NULL;
            if(!strcmp(input_name,name))stored=&aud_output_volume;
            if(!strcmp(input_name,"AudioStereoJackOutput"))stored=&aud_inactive_volumes[0];
            if(!strcmp(input_name,"AudioBuiltInSpeakerOutput"))stored=&aud_inactive_volumes[1];
            if(!strcmp(input_name,"AudioUsbDeviceOutput"))stored=&aud_inactive_volumes[2];
            if(!stored){struct line log={0};text(&log,"AUDREN_UNKNOWN_DEVICE name=");text(&log,input_name);emit(&log);stop("AUDREN_DEVICE_NAME_UNSUPPORTED",pc,req->command,20);}
            unsigned set=req->command==1 || req->command==7;
            if(set){float volume;if(req->data_size<4)stop("AUDREN_DEVICE_VOLUME_LENGTH",pc,req->data_size,24);
                /* CotM sets 2.5 for the inactive built-in/jack outputs. This
                 * is a linear gain, not a normalized slider value. */
                memcpy(&volume,req->data,4);if(!isfinite(volume) || volume<0)stop("AUDREN_DEVICE_VOLUME_INVALID",pc,0,24);
                *stored=volume;
            }
            ipc_reply(buffer,req->token,0,stored,set?0:4,0,req->domain);return 1;
        }
    }
    if(endpoint==EP_AUDREN_MANAGER && (req->command==0 || req->command==1)) {
        struct aud_parameter p;
        if(req->data_size<sizeof(p))stop("AUDREN_PARAMETER_TRUNCATED",pc,req->data_size,24);
        memcpy(&p,req->data,sizeof(p));aud_parameter_validate(&p,pc);
        uint64_t size=aud_layout(NULL,NULL,&p);
        if(req->command==1){ipc_reply(buffer,req->token,0,&size,8,0,req->domain);return 1;}
        if(req->data_size<72 || !req->send_pid || req->copies!=2 || req->copy_handles[1]!=0xffff8001U)
            stop("AUDREN_OPEN_INVALID",pc,req->data_size,24);
        uint64_t supplied,aruid;memcpy(&supplied,req->data+56,8);memcpy(&aruid,req->data+64,8);
        struct session *transfer=session_get(req->copy_handles[0],pc);
        if(transfer->endpoint!=EP_TRANSFER || transfer->initialized>=16)stop("AUDREN_WORKBUF_HANDLE",pc,transfer->endpoint,24);
        struct transfer_region *t=&transfer_regions[transfer->initialized];
        if(!t->open || t->permission || supplied>t->size || supplied<size || aruid!=main_thread_id)
            stop("AUDREN_WORKBUF_CONTRACT",pc,supplied,24);
        unsigned i=0;while(i<2&&aud_renderers[i].open)i++;
        if(i==2)stop("AUDREN_SESSIONS_FULL",pc,i,21);
        for(unsigned j=0;j<2;j++)if(aud_renderers[j].open && session_get(aud_renderers[j].transfer,pc)==transfer)
            stop("AUDREN_WORKBUF_ALREADY_OWNED",pc,req->copy_handles[0],24);
        struct aud_renderer *r=&aud_renderers[i];memset(t->buffer,0,size);
        *r=(struct aud_renderer){.open=1,.p=p,.time_limit=100,.transfer=session_clone(req->copy_handles[0],pc)};
        aud_layout(r,t->buffer,&p);*ipc_child(buffer,s,req,EP_AUDREN,pc)=i+1;
        struct line log={0};text(&log,"AUDREN_OPEN revision=");hex(&log,p.revision);text(&log," arena-bytes=");hex(&log,size);
        text(&log," voices-capacity=");hex(&log,p.voices);text(&log," effects-capacity=");hex(&log,p.effects);emit(&log);
        return 1;
    }
    if(endpoint==EP_AUDREN) {
        if(!*state || *state>2 || !aud_renderers[*state-1].open)stop("AUDREN_SESSION_INVALID",pc,*state,24);
        struct aud_renderer *r=&aud_renderers[*state-1];uint32_t value;
        if(req->command==5 || req->command==6) {
            if(req->command==5 && !r->started) {
                if(!r->updates)stop("AUDREN_START_WITHOUT_CONFIG",pc,0,24);
                for(unsigned i=0;i<2;i++)if(aud_renderers[i].started)stop("AUDREN_MULTIPLE_ACTIVE_UNSUPPORTED",pc,i,20);
                aud_host_request(1,pc);r->clipped_samples=r->output_samples=0;r->peak_before_clip=0;r->started=1;
            }
            if(req->command==6 && r->started){r->started=0;aud_host_request(2,pc);}
            ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
        }
        if(req->command==4 || req->command==10) {
            aud_update(r,req,pc);ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
        }
        if(req->command<=3 || req->command==9) {
            value=req->command==0?r->p.rate:req->command==1?r->p.samples:req->command==2?r->p.mix_buffers:
                req->command==3?!r->started:r->time_limit;
            ipc_reply(buffer,req->token,0,&value,4,0,req->domain);return 1;
        }
        if(req->command==7) {
            if(!r->event)r->event=session_new(EP_EVENT,pc);
            ipc_copy_events(buffer,req,&r->event,1,pc);return 1;
        }
        if(req->command==8) {
            if(req->data_size<4 || read32(req->data)>100)stop("AUDREN_TIME_LIMIT_INVALID",pc,req->data_size,24);
            r->time_limit=read32(req->data);ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
        }
    }
    struct line detail={0};text(&detail,"AUDREN_UNIMPLEMENTED endpoint=");hex(&detail,endpoint);
    text(&detail," command=");hex(&detail,req->command);text(&detail," data-bytes=");hex(&detail,req->data_size);
    for(unsigned i=0;i+4<=req->data_size&&i<80;i+=4){text(&detail," u32=");hex(&detail,read32(req->data+i));}
    emit(&detail);
    for(unsigned i=0;i<req->num_inputs;i++) {
        detail=(struct line){0};text(&detail,"AUDREN_INPUT size=");hex(&detail,req->inputs[i].size);
        for(unsigned j=0;j+4<=req->inputs[i].size&&j<64;j+=4){text(&detail," u32=");hex(&detail,read32((void *)(uintptr_t)(req->inputs[i].address+j)));}emit(&detail);
    }
    for(unsigned i=0;i<req->num_outputs;i++) {detail=(struct line){0};text(&detail,"AUDREN_OUTPUT size=");hex(&detail,req->outputs[i].size);emit(&detail);}
    stop("AUDREN_BACKEND_REQUIRED",pc,req->command,20);return 1;
}
#include "audren_host.h"
