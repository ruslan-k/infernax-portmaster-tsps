/* D18: one block has an immutable parameter/ADPCM snapshot. Audio IPC waits
 * for its commit, releasing kernel_mutex; unrelated IPC/SVC keeps progressing.
 * The transfer backing remains retained by the renderer until aud_destroy.
 * Only capture holds kernel->regions. No guest memory is read while mixing. */
#define AUD_SNAPSHOT_SOURCE_LIMIT 962U /* two lookahead samples + 240 * max pitch4 */
struct aud_snapshot_wave {
    uint64_t address,context;
    uint32_t first[2],blocks[2];size_t offset[2];
    uint8_t context_bytes[6];unsigned has_context;
};
static struct {
    struct aud_renderer renderer,*owner;
    uint8_t *arena,*data;struct aud_snapshot_wave *waves;
    size_t arena_capacity,data_capacity,wave_capacity,arena_bytes,data_used;
    unsigned configured,enabled,busy;
    uint64_t blocks,capture_ns,unlocked_ns,commit_ns,copied_bytes;
} aud_snapshot;
static _Thread_local unsigned aud_snapshot_reading;
static pthread_cond_t aud_snapshot_finished=PTHREAD_COND_INITIALIZER;
static void *aud_pool_pointer(struct aud_renderer *,uint64_t,uint64_t,uint64_t);
static unsigned aud_snapshot_enabled(void) {
    /* Only the mixer calls this, with kernel_mutex held. Callback and IPC do
     * not read configuration or scratch storage. Environment is immutable. */
    if(!aud_snapshot.configured) {
        const char *option=getenv("COTM_AUDIO_SNAPSHOT");
        if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("AUDREN_SNAPSHOT_OPTION",0,0,24);
        aud_snapshot.enabled=option&&!strcmp(option,"1");aud_snapshot.configured=1;
        if(aud_snapshot.enabled)printf("AUDREN_SNAPSHOT enabled=1 block=240 source-limit=962 pcm=unchanged mix-mutex=none capture=kernel-regions commit=kernel\n");
    }
    return aud_snapshot.enabled;
}
static unsigned aud_snapshot_wait_locked(void) {
    unsigned waited=0;
    while(aud_snapshot.busy) {
        waited=1;
        int error=pthread_cond_wait(&aud_snapshot_finished,&kernel_mutex);
        if(error)stop("AUDREN_SNAPSHOT_WAIT",0,error,21);
    }
    return waited;
}
static void aud_snapshot_reserve(struct aud_renderer *r) {
    size_t arena=(size_t)aud_layout(NULL,NULL,&r->p),waves=(size_t)r->p.voices*4;
    size_t bytes=(size_t)r->p.voices*1024;
    if(!r->p.voices||r->p.voices>1024||arena>(4U<<20))stop("AUDREN_SNAPSHOT_LAYOUT",0,arena,24);
    if(arena>aud_snapshot.arena_capacity) {
        uint8_t *p=realloc(aud_snapshot.arena,arena);if(!p)stop("AUDREN_SNAPSHOT_MEMORY",0,arena,21);
        aud_snapshot.arena=p;aud_snapshot.arena_capacity=arena;
    }
    if(bytes>aud_snapshot.data_capacity) {
        uint8_t *p=realloc(aud_snapshot.data,bytes);if(!p)stop("AUDREN_SNAPSHOT_MEMORY",0,bytes,21);
        aud_snapshot.data=p;aud_snapshot.data_capacity=bytes;
    }
    if(waves>aud_snapshot.wave_capacity) {
        struct aud_snapshot_wave *p=realloc(aud_snapshot.waves,waves*sizeof(*p));
        if(!p)stop("AUDREN_SNAPSHOT_MEMORY",0,waves,21);
        aud_snapshot.waves=p;aud_snapshot.wave_capacity=waves;
    }
    aud_snapshot.arena_bytes=arena;aud_snapshot.data_used=0;
    memset(aud_snapshot.waves,0,waves*sizeof(*aud_snapshot.waves));
}
static void aud_snapshot_segment(struct aud_renderer *r,const struct aud_wave *wave,
                                 struct aud_snapshot_wave *copy,unsigned segment,uint32_t first,unsigned samples) {
    if(!samples)return;
    uint64_t begin=(uint64_t)(first/14)*8,end=((uint64_t)first+samples+13)/14*8;
    if(segment>1||end<begin||end>wave->size||wave->address>UINT64_MAX-begin)
        stop("AUDREN_SNAPSHOT_WAVE_RANGE",0,first,24);
    size_t bytes=(size_t)(end-begin);
    if(bytes>aud_snapshot.data_capacity-aud_snapshot.data_used)stop("AUDREN_SNAPSHOT_DATA_CAPACITY",0,bytes,24);
    const void *source=aud_pool_pointer(r,wave->address+begin,bytes,0);
    copy->address=wave->address;copy->first[segment]=first/14;copy->blocks[segment]=bytes/8;
    copy->offset[segment]=aud_snapshot.data_used;
    memcpy(aud_snapshot.data+aud_snapshot.data_used,source,bytes);aud_snapshot.data_used+=bytes;
}
static void aud_snapshot_capture_locked(struct aud_renderer *r) {
    if(aud_rev6_plus(r->p.revision))stop("ZERO_AUDIO_REV6_SNAPSHOT_UNSUPPORTED",0,0,20);
    if(aud_snapshot.busy||aud_snapshot_reading)stop("AUDREN_SNAPSHOT_REENTRY",0,0,24);
    aud_snapshot_reserve(r);
    pthread_mutex_lock(&regions_mutex);
    memcpy(aud_snapshot.arena,r->pools,aud_snapshot.arena_bytes);
    aud_snapshot.renderer=*r;aud_layout(&aud_snapshot.renderer,aud_snapshot.arena,&r->p);
    for(unsigned i=0;i<r->p.voices;i++) {
        const struct aud_voice *v=&aud_snapshot.renderer.voices[i];
        if(!v->input.used||v->input.state!=0)continue;
        unsigned head=v->head,mask=v->valid_mask,remaining=AUD_SNAPSHOT_SOURCE_LIMIT,active=v->active;
        if(head>=4)stop("AUDREN_SNAPSHOT_HEAD",0,head,24);
        uint32_t cursor=v->cursor;
        for(unsigned visited=0;visited<4&&remaining&&(mask&(1U<<head));visited++) {
            const struct aud_wave *w=&v->input.wave[head];struct aud_snapshot_wave *copy=&aud_snapshot.waves[i*4+head];
            uint32_t first=active?cursor:(uint32_t)w->start;
            if(w->start<0||w->end<=w->start||first<(uint32_t)w->start||first>=(uint32_t)w->end||w->loop>1)
                stop("AUDREN_SNAPSHOT_CURSOR",0,first,24);
            if(!active&&w->context) {
                if(w->context_size!=6)stop("AUDREN_SNAPSHOT_CONTEXT",0,w->context_size,24);
                memcpy(copy->context_bytes,aud_pool_pointer(r,w->context,6,0),6);copy->context=w->context;copy->has_context=1;
            }
            unsigned take=(uint32_t)w->end-first;if(take>remaining)take=remaining;
            aud_snapshot_segment(r,w,copy,0,first,take);remaining-=take;
            if(w->loop) {
                unsigned loop=(uint32_t)(w->end-w->start);if(loop>remaining)loop=remaining;
                aud_snapshot_segment(r,w,copy,1,(uint32_t)w->start,loop);break;
            }
            mask&=~(1U<<head);head=(head+1)%4;active=0;
        }
    }
    pthread_mutex_unlock(&regions_mutex);
    aud_snapshot.owner=r;aud_snapshot.busy=1;
    aud_snapshot.copied_bytes+=aud_snapshot.arena_bytes+aud_snapshot.data_used;
}
static const uint8_t *aud_snapshot_block(struct aud_renderer *r,struct aud_voice *v,const struct aud_wave *w,uint32_t cursor) {
    if(!aud_snapshot_reading)return NULL;
    if(r!=&aud_snapshot.renderer||v<r->voices||v>=r->voices+r->p.voices||v->head>=4)
        stop("AUDREN_SNAPSHOT_READER",0,cursor,24);
    struct aud_snapshot_wave *copy=&aud_snapshot.waves[(size_t)(v-r->voices)*4+v->head];unsigned block=cursor/14;
    if(copy->address!=w->address)stop("AUDREN_SNAPSHOT_ADDRESS",0,w->address,24);
    for(unsigned i=0;i<2;i++)if(block>=copy->first[i]&&block-copy->first[i]<copy->blocks[i])
        return aud_snapshot.data+copy->offset[i]+(size_t)(block-copy->first[i])*8;
    stop("AUDREN_SNAPSHOT_BLOCK_MISS",0,cursor,24);return NULL;
}
static const uint8_t *aud_snapshot_context(struct aud_renderer *r,struct aud_voice *v,const struct aud_wave *w) {
    if(!aud_snapshot_reading)return NULL;
    if(r!=&aud_snapshot.renderer||v<r->voices||v>=r->voices+r->p.voices||v->head>=4)
        stop("AUDREN_SNAPSHOT_READER",0,v->head,24);
    struct aud_snapshot_wave *copy=&aud_snapshot.waves[(size_t)(v-r->voices)*4+v->head];
    if(!copy->has_context||copy->context!=w->context)stop("AUDREN_SNAPSHOT_CONTEXT_MISS",0,w->context,24);
    return copy->context_bytes;
}
static void aud_snapshot_commit_locked(struct aud_renderer *r) {
    if(!aud_snapshot.busy||aud_snapshot.owner!=r||aud_snapshot_reading||(void *)r->pools==(void *)aud_snapshot.arena)
        stop("AUDREN_SNAPSHOT_COMMIT",0,0,24);
    memcpy(r->pools,aud_snapshot.arena,aud_snapshot.arena_bytes);
    r->frames=aud_snapshot.renderer.frames;r->output_samples=aud_snapshot.renderer.output_samples;
    r->clipped_samples=aud_snapshot.renderer.clipped_samples;r->peak_before_clip=aud_snapshot.renderer.peak_before_clip;
    aud_snapshot.blocks++;aud_snapshot.owner=NULL;aud_snapshot.busy=0;
    pthread_cond_broadcast(&aud_snapshot_finished);
}
