/* Asynchronous frames (opt-in COTM_ASYNC_FRAME=1, requires COTM_GL_THREAD=1).
 * SubmitCommands on the guest thread only decodes the lists, applies the
 * guest-visible side effects (uniform buffer updates, counters) and copies
 * every byte the frame will read (command arenas, vertex/index ranges,
 * uniform bytes per draw, texture copy sources) into a private snapshot.
 * The GL worker executes the frame later from that snapshot while the game
 * thread runs the next frame's logic. Guest memory is never read by the
 * worker: every read is redirected to the snapshot or the run stops.
 * The guest fence is signaled when the snapshot exists, exactly like the
 * early-fence experiment, because the game may reuse its memory then. */
struct nvn_snap_entry {uint64_t start,bytes,offset,generation;unsigned draw_seq,bucket_next;};
#define NVN_SNAP_BUCKETS 1024U
#define NVN_SNAPSHOT_DRAWS 1024U
struct nvn_snapshot {
    struct nvn_snap_entry *entries;unsigned count,capacity,last_hit,draw_start[NVN_SNAPSHOT_DRAWS+2],draws;
    unsigned buckets[NVN_SNAP_BUCKETS];
    uint8_t *arena;size_t arena_used,arena_capacity;
    struct nvn_command_list lists[16];unsigned list_count;
    unsigned draw_cursor,live,started;
    uint64_t serial,queue_object;
    uint64_t lookups,misses;
};
static struct {
    int configured;unsigned enabled;
    struct nvn_snapshot pool[4];
    uint64_t submits,dropped,snapshot_ns,logic_ns,logic_start,bytes;unsigned after_submit,last_submit_skipped;
    uint64_t uniform_generation;
} nvn_async_frame;
static unsigned nvn_async_frame_enabled(void) {
    if(!nvn_async_frame.configured) {
        const char *o=getenv("COTM_ASYNC_FRAME");
#ifndef ANIMAL_WELL_SHADERS
        if(o && !strcmp(o,"1"))stop("NVN_LC2_ASYNC_STAGE_PROGRAMS_UNQUALIFIED",nvn_caller,0,20);
#else
        /* Animal Well: DrawArrays-only lists; every guest read of the replay (vertex ranges, uniform ranges incl. the
         * dynamic cbuffer array, copy sources) is captured per draw; descriptor pools are runtime copies. Misses stop. */
#endif
        nvn_async_frame.enabled=o&&!strcmp(o,"1")&&nvn_gl_worker_enabled();nvn_async_frame.configured=1;
        if(nvn_async_frame.enabled)printf("NVN_ASYNC_FRAME_EXPERIMENT enabled=1 execution=gl-worker-from-snapshot guest-reads=redirected fence=snapshot-complete drop=pending-frame-replaced\n");
    }
    return nvn_async_frame.enabled;
}
static void *nvn_redirect_lookup(uint64_t address,uint64_t size) {
    struct nvn_snapshot *s=nvn_redirect_current;s->lookups++;
    if(s->last_hit<s->count) {
        const struct nvn_snap_entry *e=&s->entries[s->last_hit];
        if(e->draw_seq<=s->draw_cursor && address>=e->start && address-e->start<=e->bytes && size<=e->bytes-(address-e->start))
            return s->arena+e->offset+(address-e->start);
    }
    /* Entries are appended in draw order: start just before the first entry
     * of the draw after the cursor, so a draw's own captures are found first. */
    unsigned from=s->count;
    if(s->draw_cursor<s->draws && s->draw_cursor+1<=NVN_SNAPSHOT_DRAWS)from=s->draw_start[s->draw_cursor+1];
    if(from>s->count)from=s->count;
    for(unsigned i=from;i-- >0;) {
        const struct nvn_snap_entry *e=&s->entries[i];
        if(e->draw_seq>s->draw_cursor)continue;
        if(address>=e->start && address-e->start<=e->bytes && size<=e->bytes-(address-e->start)) {
            s->last_hit=i;return s->arena+e->offset+(address-e->start);
        }
    }
    s->misses++;return NULL;
}
static void nvn_redirect_advance_draw(void){if(nvn_redirect_current)nvn_redirect_current->draw_cursor++;}
static void nvn_snapshot_reset(struct nvn_snapshot *s){s->count=0;s->arena_used=0;s->list_count=0;s->draw_cursor=0;s->lookups=s->misses=0;s->last_hit=UINT_MAX;s->draws=0;s->draw_start[0]=0;memset(s->buckets,0xff,sizeof(s->buckets));}
static struct nvn_snapshot *nvn_snapshot_alloc(void) {
    for(unsigned i=0;i<4;i++)if(!nvn_async_frame.pool[i].live){struct nvn_snapshot *s=&nvn_async_frame.pool[i];nvn_snapshot_reset(s);s->live=1;s->started=0;return s;}
    return NULL;
}
#ifdef ANIMAL_WELL_SHADERS
/* AW_PASS_STATS=1: per render pass signature (records + every captured guest byte + sampled texture ids), compared with
 * the previous frame; a pass is reusable when its signature matches and no texture it samples changed this frame. */
static struct {unsigned enabled,hashing,passes,frames;uint64_t hash;uint64_t cur[256],prev[256];uint64_t cur_target[256],prev_target[256];
    uint64_t sampled[64];unsigned nsampled,pass_draws;uint64_t changed[256];unsigned nchanged;uint64_t total,own_same,dep_same,draws,dep_draws;uint64_t pair_draws[64],pair_reuse[64];unsigned pair_last;} aw_pass;
static inline void aw_pass_mix(const void *data,size_t bytes){const uint8_t *p=data;uint64_t h=aw_pass.hash;for(size_t i=0;i<bytes;i++){h^=p[i];h*=0x100000001b3ULL;}aw_pass.hash=h;}
static void aw_pass_close(void) {
    if(!aw_pass.hashing)
        return;
    aw_pass.hashing=0;
    unsigned i=aw_pass.passes++;
    if(i>=256)
        return;
    aw_pass.cur[i]=aw_pass.hash;aw_pass.total++;
    unsigned own=aw_pass.prev[i]==aw_pass.hash&&aw_pass.prev_target[i]==aw_pass.cur_target[i],dep=own;
    for(unsigned k=0;k<aw_pass.nsampled&&dep;k++)for(unsigned c=0;c<aw_pass.nchanged;c++)if(aw_pass.changed[c]==aw_pass.sampled[k]){dep=0;break;}
    aw_pass.own_same+=own;aw_pass.dep_same+=dep;aw_pass.draws+=aw_pass.pass_draws;if(dep)aw_pass.dep_draws+=aw_pass.pass_draws;
    if(aw_pass.pass_draws){aw_pass.pair_draws[aw_pass.pair_last&63]+=aw_pass.pass_draws;if(dep)aw_pass.pair_reuse[aw_pass.pair_last&63]+=aw_pass.pass_draws;}
    if(!dep&&aw_pass.nchanged<256)aw_pass.changed[aw_pass.nchanged++]=aw_pass.cur_target[i];
}
#endif
static void nvn_snapshot_capture(struct nvn_snapshot *s,uint64_t start,uint64_t bytes,unsigned draw_seq,uint64_t generation,const void *source) {
    if(!bytes)return;
#ifdef ANIMAL_WELL_SHADERS
    if(aw_pass.hashing&&draw_seq)aw_pass_mix(source,(size_t)bytes);
#endif
    unsigned bucket=(unsigned)((start>>4)*2654435761U)%NVN_SNAP_BUCKETS;
    for(unsigned i=s->buckets[bucket];i!=UINT_MAX;i=s->entries[i].bucket_next) {
        const struct nvn_snap_entry *e=&s->entries[i];
        if(e->start==start && e->bytes==bytes && (!e->draw_seq || e->generation==generation))return;
    }
    if(s->count==s->capacity) {
        unsigned capacity=s->capacity?s->capacity*2:256;
        struct nvn_snap_entry *grown=realloc(s->entries,capacity*sizeof(*grown));
        if(!grown)stop("NVN_SNAPSHOT_MEMORY",nvn_caller,capacity,21);
        s->entries=grown;s->capacity=capacity;
    }
    size_t at=(s->arena_used+15)&~(size_t)15;
    if(at+bytes>s->arena_capacity) {
        size_t capacity=s->arena_capacity?s->arena_capacity:(2U<<20);while(capacity<at+bytes)capacity*=2;
        uint8_t *grown=realloc(s->arena,capacity);
        if(!grown)stop("NVN_SNAPSHOT_MEMORY",nvn_caller,capacity,21);
        s->arena=grown;s->arena_capacity=capacity;
    }
    memcpy(s->arena+at,source,bytes);
    s->entries[s->count]=(struct nvn_snap_entry){start,bytes,at,generation,draw_seq,s->buckets[bucket]};
    s->buckets[bucket]=s->count;s->count++;
    s->arena_used=at+bytes;nvn_async_frame.bytes+=bytes;
}
/* Guest-thread pass: decode, side effects, captures. Mirrors the binding
 * records consumed by nvn_render_draw without touching the worker's state. */
static void nvn_snapshot_build(struct nvn_snapshot *s,struct nvn_queue *q,const uint64_t *handles,unsigned count) {
#ifdef ANIMAL_WELL_SHADERS
    /* CPU-written textures (texel writes / copies) are refreshed from guest memory at their next sampling, which now
     * happens on the worker: capture every dirty texture image so that refresh read hits the snapshot. */
    for(unsigned i=0;i<4096;i++) {
        struct nvn_texture *t=&nvn_textures[i];
        if(!t->live || !t->pool || !__atomic_load_n(&t->zero_cpu_dirty,__ATOMIC_ACQUIRE) || !t->size)continue;
        nvn_snapshot_capture(s,t->pool->address+t->b.offset,t->size,0,0,nvn_pointer(t->pool->address+t->b.offset,t->size,PROT_READ));
    }
#endif
    struct {uint32_t attrib[16],streams[16][2],attrib_count,stream_count;struct nvn_range vertex[16],uniform[2][16];struct nvn_program *program;struct megaman_program_stage bound[2];uint64_t row_stride,texture_pool;} st;
    memset(&st,0,sizeof(st));unsigned draw_seq=0;
#ifdef ANIMAL_WELL_SHADERS
    {static int c;if(!c){const char *o=getenv("AW_PASS_STATS");aw_pass.enabled=o&&!strcmp(o,"1");c=1;}}
    if(aw_pass.enabled){st.texture_pool=nvn_replay.texture_pool;aw_pass.passes=0;aw_pass.nchanged=0;aw_pass.hashing=0;}
#endif
    (void)q;
    for(unsigned i=0;i<count;i++) {
        struct nvn_command_list *list=nvn_command_list_require(handles[i]);
        if(s->list_count==16)stop("NVN_SNAPSHOT_LISTS",nvn_caller,count,24);
        s->lists[s->list_count++]=*list;
        struct nvn_command_buffer *c=nvn_command_find(list->owner);
        for(unsigned k=0;k<c->command_count;k++) {
            struct nvn_command_arena *a=&c->command[k];
            if(a->used)nvn_snapshot_capture(s,a->address,a->used,0,0,nvn_pointer(a->address,a->used,PROT_READ));
        }
        uint64_t next=list->first;
        for(uint64_t j=0;j<list->count;j++) {
            struct nvn_record r;nvn_command_decode(list,next,&r);uint64_t *a=r.args;
#ifdef ANIMAL_WELL_SHADERS
            if(aw_pass.enabled) {
                if(r.op==NI_nvnCommandBufferSetRenderTargets) {
                    aw_pass_close();aw_pass.hashing=1;aw_pass.hash=0xcbf29ce484222325ULL;aw_pass.nsampled=0;aw_pass.pass_draws=0;
                    uint64_t targets[2];memcpy(targets,r.payload,16);struct nvn_texture *t=targets[0]?nvn_texture_find(targets[0]):NULL;
                    if(aw_pass.passes<256)aw_pass.cur_target[aw_pass.passes]=t?t->id:0;
                }
                if(aw_pass.hashing) {
                    aw_pass_mix(&r.op,sizeof(r.op));aw_pass_mix(r.args,sizeof(r.args));
                    if(r.op==NI_nvnCommandBufferClearColor)aw_pass_mix(r.payload,16);
                    if(r.op==NI_nvnCommandBufferBindTexture&&aw_pass.nsampled<64&&st.texture_pool&&(uint32_t)a[2]) {
                        struct nvn_descriptor *td=nvn_render_descriptor(st.texture_pool,1,(uint32_t)a[2]);
                        aw_pass.sampled[aw_pass.nsampled++]=td?td->object:0;
                    }
                }
            }
#endif
            if(r.op==NI_nvnCommandBufferBindTexture&&nvd_decoded&&st.texture_pool&&(uint32_t)a[2]){
                static uint64_t seen[64];static unsigned ns;
                struct nvn_descriptor *td=nvn_render_descriptor(st.texture_pool,1,(uint32_t)a[2]);
                struct nvn_texture *bt=td?nvn_texture_find(td->object):NULL;
                if(bt){unsigned i,dup=0;for(i=0;i<ns;i++)if(seen[i]==bt->id){dup=1;break;}
                    if(!dup&&ns<64){seen[ns++]=bt->id;
                        printf("NVD_CENSUS id=%"PRIu64" fmt=0x%x %ux%u tgt=%d slot=%u serial=%"PRIu64"\n",
                            bt->id,bt->b.format,bt->b.width,bt->b.height,(int)bt->b.target,(unsigned)a[1],nvd_serial);}}
            }
            switch(r.op) {
            case NI_nvnCommandBufferSetTexturePool:st.texture_pool=a[0];break;
            case NI_nvnCommandBufferBindProgram:st.program=nvn_program_find(a[0]);
#ifdef ANIMAL_WELL_SHADERS
                {   /* vertex and fragment come from separate program objects bound with stage masks */
                    struct nvn_program *bp=st.program;unsigned empty[2]={0};
                    if(!megaman_program_bind(st.bound,a[0],bp?bp->id:0,bp?bp->stage_mask:0,bp?bp->sources:empty,(unsigned)a[1]))stop("NVN_PROGRAM_BIND_STATE",nvn_caller,a[0],24);
                }
#endif
                break;
            case NI_nvnCommandBufferBindVertexAttribState:st.attrib_count=a[0];memcpy(st.attrib,r.payload,a[0]*4);break;
            case NI_nvnCommandBufferBindVertexStreamState:st.stream_count=a[0];memcpy(st.streams,r.payload,a[0]*8);break;
            case NI_nvnCommandBufferBindVertexBuffer:if(a[0]<16)st.vertex[a[0]]=(struct nvn_range){a[1],a[2]};break;
            case NI_nvnCommandBufferBindVertexBuffers:memcpy(st.vertex+a[0],r.payload,a[1]*16);break;
            case NI_nvnCommandBufferBindUniformBuffer:if(a[0]<2&&a[1]<16)st.uniform[a[0]][a[1]]=(struct nvn_range){a[2],a[3]};break;
            case NI_nvnCommandBufferBindUniformBuffers:memcpy(st.uniform[a[0]]+a[1],r.payload,a[2]*16);break;
            case NI_nvnCommandBufferBindUniformBuffersDeferred:memcpy(st.uniform[a[0]]+a[1],nvn_pointer(a[3],a[2]*16,PROT_READ),a[2]*16);break;
            case NI_nvnCommandBufferSetCopyRowStride:st.row_stride=a[0];break;
            case NI_nvnCommandBufferUpdateUniformBuffer: {
                if(a[2]>a[1] || a[3]>a[1]-a[2] || a[3]>sizeof(r.payload))stop("NVN_UNIFORM_REPLAY_UPDATE_RANGE",nvn_caller,a[3],24);
                uint8_t *destination=nvn_gpu_pointer(a[0],a[1],PROT_READ|PROT_WRITE);
                memcpy(destination+a[2],r.payload,a[3]);nvn_async_frame.uniform_generation++;break;
            }
            case NI_nvnCommandBufferReportCounter: {
                if(a[0])stop("NVN_COUNTER_TYPE_REQUIRED",nvn_caller,a[0],20);
                if(a[1]%16)stop("NVN_COUNTER_ALIGNMENT",nvn_caller,a[1],24);
                uint64_t counter[2]={0,nvn_nanoseconds()};memcpy(nvn_gpu_pointer(a[1],16,PROT_WRITE),counter,16);break;
            }
            case NI_nvnCommandBufferCopyBufferToTexture: {
                uint32_t box[6];memcpy(box,r.payload,sizeof(box));
                if(!box[3]||!box[4])break;
                uint64_t row=(uint64_t)box[3]*4,stride=st.row_stride?st.row_stride:row;
                if(stride<row || stride>64U*1024U*1024U)stop("NVN_COPY_ROW_STRIDE",nvn_caller,stride,24);
                uint64_t bytes=stride*(box[4]-1)+row;
                nvn_snapshot_capture(s,a[0],bytes,0,0,nvn_gpu_pointer(a[0],bytes,PROT_READ));break;
            }
            case NI_nvnCommandBufferDrawArrays:case NI_nvnCommandBufferDrawElements:case NI_nvnCommandBufferDrawElementsBaseVertex: {
                draw_seq++;
#ifdef ANIMAL_WELL_SHADERS
                if(aw_pass.hashing){aw_pass.pass_draws++;aw_pass.pair_last=st.bound[1].source;}
#endif
                if(draw_seq<=NVN_SNAPSHOT_DRAWS){s->draw_start[draw_seq]=s->count;s->draws=draw_seq;}
                uint64_t base=0,maximum=0,cnt;
                if(r.op==NI_nvnCommandBufferDrawArrays){base=a[1];cnt=a[2];maximum=cnt?cnt-1:0;}
                else {
                    unsigned type=a[1];cnt=a[2];if(type>2||cnt>1048576)stop("NVN_INDEX_RANGE",nvn_caller,cnt,24);
                    if(!cnt)break;
                    uint64_t ibytes=cnt*(1U<<type);const uint8_t *src=nvn_gpu_pointer(a[3],ibytes,PROT_READ);
                    nvn_snapshot_capture(s,a[3],ibytes,0,0,src);
                    uint32_t min=UINT32_MAX,max=0;
                    for(uint64_t k=0;k<cnt;k++){uint32_t v=type==0?src[k]:type==1?(uint32_t)src[k*2]|((uint32_t)src[k*2+1]<<8):read32(src+k*4);if(v<min)min=v;if(v>max)max=v;}
                    int64_t vb=r.op==NI_nvnCommandBufferDrawElementsBaseVertex?(int32_t)a[4]:0;
                    if(type==1 && vb>=0){base=vb;maximum=max;}
                    else {int64_t sb=vb+(int64_t)min;if(sb<0)stop("NVN_INDEX_NEGATIVE_VERTEX",nvn_caller,sb,24);base=sb;maximum=max-min;}
                }
                if(!cnt)break;
                for(unsigned k=0;k<(unsigned)nvn_device.attributes&&k<st.attrib_count;k++) {
                    uint32_t attr=st.attrib[k];if(!(attr&64))continue;
                    unsigned stream=attr&31,format=(attr>>21)&1023,offset=(attr>>7)&16383;
                    if(stream>=st.stream_count||stream>=16)stop("NVN_VERTEX_STREAM_REQUIRED",nvn_caller,stream,20);
                    unsigned components=format==0xa?1:format==0x16?2:format==0x22?3:format==0x2e||format==0x25?4:0;
                    if(!components)stop("NVN_VERTEX_FORMAT_REQUIRED",nvn_caller,format,20);
                    unsigned element=components*(format==0x25?1:4),stride=st.streams[stream][0];struct nvn_range range=st.vertex[stream];
                    uint64_t start=base*stride+offset,extent=maximum*stride+element;
                    if(!stride||start>range.size||extent>range.size-start)stop("NVN_VERTEX_FETCH_RANGE",nvn_caller,start+extent,24);
                    uint64_t stream_start=base*stride,span=maximum*stride+stride;
                    if(offset+extent>span)span=offset+extent;
                    if(span>range.size-stream_start)span=range.size-stream_start;
                    nvn_snapshot_capture(s,range.address+stream_start,span,0,0,nvn_gpu_pointer(range.address+stream_start,span,PROT_READ));
                }
                struct nvn_program *p=st.program;
#ifdef ANIMAL_WELL_SHADERS
                /* Native (guest) program objects carry no uniform table here: it lives in the linked pair. Capture from
                 * the static binding tables of both stages instead (every constant-buffer range a stage reads). */
                if(p&&p->live)for(unsigned stage=0;stage<2;stage++) {
                    const struct mm_shader_binding *mb=&nvn_megaman_shader_bindings[st.bound[stage].source];
                    for(unsigned u=0;u<mb->uniform_count;u++) {
                        const struct mm_shader_uniform *v=&mb->uniforms[u];
                        if(v->stage>1||v->binding>=16)continue;
                        struct nvn_range range=st.uniform[v->stage][v->binding];
                        if(v->offset>=range.size)continue; /* the draw reads zero for this slot (AW_UNIFORM_RANGE path) */
                        unsigned avail=(unsigned)(range.size-v->offset);if(avail>v->size)avail=v->size;
                        nvn_snapshot_capture(s,range.address+v->offset,avail,draw_seq,nvn_async_frame.uniform_generation,nvn_gpu_pointer(range.address+v->offset,avail,PROT_READ));
                        static unsigned logged;if(logged<40){logged++;printf("AW_SNAPSHOT_CAPTURE draw=%u pair=%u/%u uniform=%s start=0x%"PRIx64" bytes=%u entries=%u\n",draw_seq,st.bound[0].source,st.bound[1].source,v->name,range.address+v->offset,avail,s->count);}
                    }
                }
                if(0)
#endif
                if(p&&p->live)for(unsigned u=0;u<p->uniform_count;u++) {
                    struct nvn_uniform *un=&p->uniforms[u];if(un->location<0&&!(p->alpha5.name&&p->alpha5.locations[u]>=0))continue;
                    if(un->stage>1||un->binding>=16)continue;
                    struct nvn_range range=st.uniform[un->stage][un->binding];
                    /* A uniform that lies past the bound range has nothing to capture; LC2 titles bind
                     * per-draw sub-ranges, so this is a legal layout, not a bad binding. */
                    if(un->offset>=range.size) {
                        static unsigned told;
                        if(told++<8)printf("AW_UNIFORM_RANGE_SKIP program=%"PRIu64" name=%s stage=%u binding=%u offset=%u bound-size=%"PRIu64"\n",
                            p->id,un->name,un->stage,un->binding,un->offset,range.size);
                        continue;
                    }
                    unsigned aw_avail=(unsigned)(range.size-un->offset);if(aw_avail>un->size)aw_avail=un->size;
                    if(un->offset>range.size||un->size>range.size-un->offset){printf("AW_UNIFORM_RANGE program=%"PRIu64" name=%s stage=%u binding=%u offset=%u size=%u bound-size=%"PRIu64"\n",p->id,un->name,un->stage,un->binding,un->offset,un->size,range.size);for(unsigned sx=0;sx<2;sx++)for(unsigned bx=0;bx<16;bx++)if(st.uniform[sx][bx].size)printf("AW_UNIFORM_BOUND stage=%u index=%u size=%"PRIu64"\n",sx,bx,st.uniform[sx][bx].size);}
                    nvn_snapshot_capture(s,range.address+un->offset,aw_avail,draw_seq,nvn_async_frame.uniform_generation,nvn_gpu_pointer(range.address+un->offset,aw_avail,PROT_READ));
                }
                break;
            }
            default:break;
            }
            next=r.next;
        }
    }
    if(s->draws+1<=NVN_SNAPSHOT_DRAWS+1)s->draw_start[s->draws+1]=s->count;
#ifdef ANIMAL_WELL_SHADERS
    if(aw_pass.enabled) {
        aw_pass_close();
        memcpy(aw_pass.prev,aw_pass.cur,sizeof(aw_pass.prev));memcpy(aw_pass.prev_target,aw_pass.cur_target,sizeof(aw_pass.prev_target));
        if(++aw_pass.frames%60==0){printf("AW_PASS_STATS frames=60 passes-per-frame=%.1f same-inputs=%.1f reusable=%.1f draws-per-frame=%.1f reusable-draws=%.1f\n",aw_pass.total/60.0,aw_pass.own_same/60.0,aw_pass.dep_same/60.0,aw_pass.draws/60.0,aw_pass.dep_draws/60.0);
            if(aw_pass.frames%600==0)for(unsigned k=0;k<64;k++)if(aw_pass.pair_draws[k])printf("AW_PASS_PAIR fragment=%u draws=%"PRIu64" reusable=%"PRIu64"\n",k,aw_pass.pair_draws[k],aw_pass.pair_reuse[k]);
            aw_pass.total=aw_pass.own_same=aw_pass.dep_same=aw_pass.draws=aw_pass.dep_draws=0;}
    }
#endif
}

static void aw_snapshot_dump_near(uint64_t address) {
    struct nvn_snapshot *s=nvn_redirect_current;if(!s)return;
    printf("AW_SNAPSHOT_STATE count=%u draws=%u cursor=%u\n",s->count,s->draws,s->draw_cursor);
    for(unsigned i=0;i<s->count;i++){const struct nvn_snap_entry *e=&s->entries[i];
        if(address+64>=e->start && e->start+e->bytes+64>=address)printf("  entry %u start=0x%"PRIx64" bytes=%"PRIu64" draw_seq=%u\n",i,e->start,(uint64_t)e->bytes,e->draw_seq);}
}
/* Worker side: execute a snapshot with every guest read redirected. */
static void nvn_async_frame_execute(struct nvn_snapshot *s) {
    nvn_redirect_current=s;s->draw_cursor=0;
    uint64_t started=nvn_nanoseconds();uint64_t cpu_start=0,cpu_end=0;unsigned cpu_valid=nvn_cpu_nanoseconds(&cpu_start);
    nvn_gl_acquire();
    unsigned batch_errors=nvn_replay_errors_batched();
    nvn_stream_vbo_begin_submit();nvn_skip_begin_submit();if(!nvn_skip.active)nvn_stable_vbo_begin_submit();
    for(unsigned i=0;i<s->list_count;i++) {
        struct nvn_command_list *list=&s->lists[i];list->live=1;
        nvn_counter_begin_list();
        nvn_replay_run_list(list,batch_errors,1);
#ifdef ANIMAL_WELL_SHADERS
        aw_clear_flush_all();
#endif
    }
#ifdef ANIMAL_WELL_SHADERS
    glFinish(); /* bound the driver's in-flight jobs to one frame: GL_OUT_OF_MEMORY on a 6-vertex draw after ~400 frames with glFlush (aw175) */
#else
    glFlush();
#endif
    nvn_gl_release();nvn_profile.replay_ns+=nvn_nanoseconds()-started;nvn_profile.replay_submits++;
    if(nvn_cpu_nanoseconds(&cpu_end) && cpu_valid && cpu_end>=cpu_start){nvn_profile.replay_cpu_ns+=cpu_end-cpu_start;nvn_profile.replay_cpu_samples++;}
    if(s->misses)stop("NVN_SNAPSHOT_MISS_COUNT",nvn_caller,s->misses,24);
    nvn_redirect_current=NULL;
}
/* Guest-thread side: ids that never touch GL in async mode, plus submit and
 * present which become snapshot/post operations. */
static unsigned nvn_async_frame_local(unsigned id,struct nvn_frame *f,uint64_t *result) {
    uint64_t *a=f->args;*result=0;
    switch(id) {
    case NI_nvnQueueFenceSync:case NI_nvnQueueWaitSync:case NI_nvnSyncInitialize:case NI_nvnSyncFinalize:case NI_nvnSyncSetDebugLabel:case NI_nvnSyncWait:
        return nvn_sync_dispatch(id,f,result);
    case NI_nvnDeviceGetCurrentTimestampInNanoseconds:case NI_nvnDeviceGetTimestampInNanoseconds:
        return nvn_replay_dispatch(id,f,result);
#ifdef ANIMAL_WELL_SHADERS
    /* CPU-only texture/view getters and view setters: Animal Well calls ~75 per frame. As SYNC worker jobs each one
     * waited for the frame being rendered, which serialized the game logic with the GPU (logic-ms == render-ms). */
    case NI_nvnTextureViewSetDefaults:case NI_nvnTextureViewSetLevels:case NI_nvnTextureViewSetLayers:case NI_nvnTextureViewSetFormat:
    case NI_nvnTextureViewSetSwizzle:case NI_nvnTextureViewSetTarget:case NI_nvnTextureViewSetDepthStencilMode:
    case NI_nvnTextureViewGetLevels:case NI_nvnTextureViewGetLayers:case NI_nvnTextureViewGetTarget:
    case NI_nvnTextureGetWidth:case NI_nvnTextureGetHeight:case NI_nvnTextureGetDepth:case NI_nvnTextureGetFormat:case NI_nvnTextureGetLevels:
    case NI_nvnTextureGetFlags:case NI_nvnTextureGetTarget:case NI_nvnTextureGetSamples:case NI_nvnTextureGetDepthStencilMode:
    case NI_nvnTextureGetMemoryPool:case NI_nvnTextureGetMemoryOffset:case NI_nvnTextureGetStorageSize:case NI_nvnTextureGetDebugID:
        return nvn_texture_dispatch(id,f,result);
    case NI_nvnDeviceGetTextureHandle:case NI_nvnDeviceGetSeparateTextureHandle:
    case NI_nvnDeviceGetSeparateSamplerHandle:return nvn_texture_dispatch(id,f,result);
    case NI_nvnWindowGetPresentInterval:case NI_nvnWindowGetNativeWindow:case NI_nvnWindowSetDebugLabel:
        nvn_window_require(f->args[0]);
        if(id==NI_nvnWindowGetPresentInterval)*result=nvn_window.interval;
        if(id==NI_nvnWindowGetNativeWindow)*result=nvn_window.native;
        return 1;
#endif
    case NI_nvnQueueAcquireTexture:case NI_nvnWindowAcquireTexture: {
        uint64_t output;struct nvn_sync *sync=NULL;
        if(id==NI_nvnQueueAcquireTexture){nvn_queue_find(a[0]);nvn_window_require(a[1]);output=a[2];}
        else{nvn_window_require(a[0]);sync=nvn_sync_find(a[1]);output=a[2];}
        int32_t slot=-1;
        for(unsigned i=0;i<nvn_window.count;i++){unsigned candidate=(nvn_window.next+i)%nvn_window.count;if(!nvn_window.acquired[candidate]){slot=candidate;break;}}
        if(slot<0)stop("NVN_WINDOW_NO_FREE_TEXTURE",nvn_caller,nvn_window.count,20);
        if(sync){sync->generation++;sync->completed=sync->generation;sync->signaled=1;}
        memcpy(nvn_pointer(output,4,PROT_WRITE),&slot,4);nvn_window.acquired[slot]=1;nvn_window.next=(slot+1)%nvn_window.count;
        return 1;
    }
    case NI_nvnQueueFlush:*result=0;{struct nvn_queue *q=nvn_queue_find(a[0]);(void)q;}return 1;
    case NI_nvnQueuePresentTexture: {
        struct nvn_queue *q=nvn_queue_find(a[0]);nvn_window_require(a[1]);uint64_t index=a[2];(void)q;
        if(index>=nvn_window.count || !nvn_window.acquired[index])stop("NVN_PRESENT_OWNERSHIP",nvn_caller,index,24);
        nvn_window.acquired[index]=0;
        if(nvn_async_frame.last_submit_skipped)nvn_async_frame.last_submit_skipped=0; /* keep the last rendered image on screen */
        else nvn_gl_post_present(a[0],a[1],index,nvn_caller);
        nvn_async_frame.submits++;
        {   /* Pace the game logic at the original 60 Hz: without a real vsync
             * the guest would otherwise run faster than real time in light scenes. */
            static uint64_t next_deadline;static unsigned period_ns;
            if(!period_ns){const char *o=getenv("COTM_PACE_HZ");unsigned hz=o?(unsigned)strtoul(o,NULL,10):60;if(hz<10||hz>240)hz=60;period_ns=1000000000U/hz;}
            uint64_t now=nvn_nanoseconds();
            if(next_deadline && now<next_deadline && next_deadline-now<200000000ULL) {
                struct timespec delay={0,(long)(next_deadline-now)};while(nanosleep(&delay,&delay)&&errno==EINTR){}
                now=next_deadline;
            }
            if(!next_deadline || now>next_deadline+period_ns)next_deadline=now;
            next_deadline+=period_ns;
        }
        if(nvn_async_frame.submits-nvn_async_frame.after_submit>=60) {
            uint64_t now=nvn_nanoseconds();double per=nvn_async_frame.logic_start?(now-nvn_async_frame.logic_start)*1e-6/60:0.0;
            printf("NVN_LOGIC_PROFILE submit=%"PRIu64" logic-ms-per-frame=%.3f snapshot-ms-per-frame=%.3f snapshot-kb-per-frame=%.1f frames-posted=%"PRIu64" dropped=%"PRIu64" scope=guest-thread-wall\n",
                nvn_async_frame.submits,per,nvn_async_frame.snapshot_ns*1e-6/60,nvn_async_frame.bytes/1024.0/60,nvn_gl_frames_posted(),nvn_gl_frames_dropped());
            nvn_async_frame.logic_start=now;nvn_async_frame.snapshot_ns=0;nvn_async_frame.bytes=0;nvn_async_frame.after_submit=nvn_async_frame.submits;
            nvn_gl_sync_report();
        }
        return 1;
    }
    case NI_nvnQueueSubmitCommands: {
        uint64_t started=nvn_nanoseconds();
        struct nvn_queue *q=nvn_queue_find(a[0]);
        if(a[1]>256)stop("NVN_QUEUE_SUBMIT_COUNT",nvn_caller,a[1],24);
        const uint64_t *handles=nvn_pointer(a[2],a[1]*8,PROT_READ);
        /* Opt-in COTM_RENDER_CAP_HZ: frames posted faster than the cap are not
         * even snapshotted (cheaper for the game thread, GPU gets idle time). */
        static int cap_configured;static uint64_t cap_period_ns,cap_last;static uint64_t cap_skipped;static unsigned skip_busy;
        if(!cap_configured){const char *o=getenv("COTM_RENDER_CAP_HZ");unsigned hz=o?(unsigned)strtoul(o,NULL,10):0;cap_period_ns=hz?1000000000ULL/hz:0;
            const char *b=getenv("COTM_SKIP_WHEN_BUSY");skip_busy=b&&!strcmp(b,"1");cap_configured=1;
            if(hz||skip_busy)printf("NVN_RENDER_CAP_EXPERIMENT enabled=1 hz=%u skip-when-busy=%u after-proof-only=1\n",hz,skip_busy);}
        uint64_t period=cap_period_ns;
        {   /* Opt-in COTM_THERMAL_CAP_HZ: when the SoC passes 79 C the render
             * rate is capped so the GPU idles and the governor keeps its cores. */
            static int th_configured;static uint64_t th_period_ns,th_last_check;static unsigned th_active;
            if(!th_configured){const char *o=getenv("COTM_THERMAL_CAP_HZ");unsigned hz=o?(unsigned)strtoul(o,NULL,10):0;th_period_ns=hz?1000000000ULL/hz:0;th_configured=1;if(hz)printf("NVN_THERMAL_CAP_EXPERIMENT enabled=1 hz=%u above=79C below=76C\n",hz);}
            if(th_period_ns && started-th_last_check>1000000000ULL) {
                th_last_check=started;FILE *f=fopen("/sys/class/thermal/thermal_zone0/temp","r");
                if(f){long t=0;if(fscanf(f,"%ld",&t)==1){if(t>=79000&&!th_active){th_active=1;printf("NVN_THERMAL_CAP state=on temp=%ld\n",t);}else if(t<=76000&&th_active){th_active=0;printf("NVN_THERMAL_CAP state=off temp=%ld\n",t);}}fclose(f);}
            }
            if(th_active&&(!period||th_period_ns>period))period=th_period_ns;
        }
        unsigned over_cap=period && cap_last && started-cap_last<period;
        unsigned busy=skip_busy && nvn_gl_frame_pending();
        if((over_cap||busy) && a[1]==1 && nvn_present.proved && !nvn_present.black_streak) {
            for(unsigned i=0;i<a[1];i++) {
                struct nvn_command_list *list=nvn_command_list_require(handles[i]);
                /* Side effects the guest can observe must still happen. */
                uint64_t next=list->first;
                for(uint64_t j=0;j<list->count;j++) {
                    struct nvn_record r;nvn_command_decode(list,next,&r);uint64_t *ra=r.args;
                    if(r.op==NI_nvnCommandBufferUpdateUniformBuffer){uint8_t *d=nvn_gpu_pointer(ra[0],ra[1],PROT_READ|PROT_WRITE);memcpy(d+ra[2],r.payload,ra[3]);nvn_async_frame.uniform_generation++;}
                    else if(r.op==NI_nvnCommandBufferReportCounter){uint64_t counter[2]={0,nvn_nanoseconds()};memcpy(nvn_gpu_pointer(ra[1],16,PROT_WRITE),counter,16);}
                    else if(r.op==NI_nvnCommandBufferCopyBufferToTexture){cap_skipped=cap_skipped;/* texture copies must not be lost: fall through to a full snapshot below */goto cap_full;}
                    next=r.next;
                }
                q->handles[q->arena->head%(q->command_size/8)]=list->handle;q->arena->head++;q->arena->submitted++;
                list->submitted=1;list->completed=1;q->arena->tail++;q->arena->completed=q->arena->submitted;
                q->command_used+=list->recorded_bytes;q->control_used+=32;
            }
            cap_skipped++;nvn_async_frame.dropped++;nvn_async_frame.last_submit_skipped=1;
            return 1;
        }
        cap_full:
        cap_last=started;nvn_async_frame.last_submit_skipped=0;
        struct nvn_snapshot *s=nvn_snapshot_alloc();
        if(!s){nvn_gl_drain();s=nvn_snapshot_alloc();if(!s)stop("NVN_SNAPSHOT_POOL",nvn_caller,0,24);}
        s->queue_object=a[0];
        nvn_snapshot_build(s,q,handles,(unsigned)a[1]);
        for(unsigned i=0;i<a[1];i++) {
            struct nvn_command_list *list=nvn_command_list_require(handles[i]);
            q->handles[q->arena->head%(q->command_size/8)]=list->handle;q->arena->head++;q->arena->submitted++;
            list->submitted=1;list->completed=1;q->arena->tail++;q->arena->completed=q->arena->submitted;
            q->command_used+=list->recorded_bytes;q->control_used+=32;
        }
        nvn_gl_post_frame(s,nvn_caller);
        nvn_async_frame.snapshot_ns+=nvn_nanoseconds()-started;
        return 1;
    }
    default:return 0;
    }
}
