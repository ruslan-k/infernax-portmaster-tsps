/* Context-local GL state only. Every acquire changes use_serial, so a Submit
 * begins cold even if another thread, upload, or presentation ran in between.
 * Vertex entries store GL pointer state, never the bytes at those addresses.
 * The separate uniform cache snapshots each freshly validated draw. Texture
 * parameters belong to shared objects and remain outside this state cache.
 */
enum nvn_cached_state {
    NCS_PROGRAM,NCS_BLEND_ENABLE,NCS_BLEND_FUNC,NCS_BLEND_EQUATION,NCS_DEPTH_ENABLE,
    NCS_DEPTH_FUNC,NCS_DEPTH_MASK,NCS_STENCIL_ENABLE,NCS_FRONT_FACE,NCS_CULL_ENABLE,
    NCS_CULL_FACE,NCS_COLOR_MASK,NCS_COUNT
};
static struct {
    uint64_t serial,value[NCS_COUNT];GLuint program_name;
    unsigned valid,attrib_known,attrib_enabled,pointer_known,client_buffers_known;
    struct {const void *data;GLint size;GLenum type;GLboolean normalized;GLsizei stride;GLuint buffer;} pointers[16];
} nvn_render_state;
static struct {
    uint64_t sent[3],skipped[3],program_overflow,pointer_sent,pointer_skipped,buffer_sent,buffer_skipped;unsigned after_frame,program_count;
    struct {uint64_t id,draws,gpu_ns;GLuint name;unsigned sources[2],width,height;} programs[32];
    void *last;uint64_t pending_gpu_ns;
} nvn_state_profile;
static void nvn_render_state_epoch(void) {
    if(nvn_render_state.serial!=nvn_gl_completion.use_serial)
        nvn_render_state=(typeof(nvn_render_state)){.serial=nvn_gl_completion.use_serial};
    if(nvn_replay.last_frame>=nvn_state_profile.after_frame && nvn_replay.last_frame-nvn_state_profile.after_frame>=60) {
        printf("NVN_STATE_CACHE_PROFILE after-frame=%u frames=%u program-sent=%"PRIu64" program-skipped=%"PRIu64
               " fixed-sent=%"PRIu64" fixed-skipped=%"PRIu64" attrib-sent=%"PRIu64" attrib-skipped=%"PRIu64
               " draw-profile-overflow=%"PRIu64" scope=GL-calls-since-last-report epoch=each-acquire\n",
               nvn_replay.last_frame,nvn_replay.last_frame-nvn_state_profile.after_frame,
               nvn_state_profile.sent[0],nvn_state_profile.skipped[0],nvn_state_profile.sent[1],nvn_state_profile.skipped[1],
               nvn_state_profile.sent[2],nvn_state_profile.skipped[2],nvn_state_profile.program_overflow);
        printf("NVN_VERTEX_STATE_PROFILE after-frame=%u frames=%u pointer-sent=%"PRIu64" pointer-skipped=%"PRIu64" buffer-sent=%"PRIu64" buffer-skipped=%"PRIu64" scope=GL-state-calls bytes-cache=none epoch=each-acquire\n",
               nvn_replay.last_frame,nvn_replay.last_frame-nvn_state_profile.after_frame,nvn_state_profile.pointer_sent,nvn_state_profile.pointer_skipped,
               nvn_state_profile.buffer_sent,nvn_state_profile.buffer_skipped);
        for(unsigned i=0;i<nvn_state_profile.program_count;i++) {
            const typeof(nvn_state_profile.programs[0]) *p=&nvn_state_profile.programs[i];
            printf("NVN_DRAW_PROGRAM_PROFILE after-frame=%u program-id=0x%"PRIx64" program=%u sources=%u/%u target-size=%ux%u draws=%"PRIu64" finish-ms-per-frame=%.3f scope=since-last-report\n",
                   nvn_replay.last_frame,p->id,p->name,p->sources[0],p->sources[1],p->width,p->height,p->draws,p->gpu_ns*1e-6/60.0);
        }
        nvn_state_profile=(typeof(nvn_state_profile)){.after_frame=nvn_replay.last_frame};
    }
}
static void nvn_render_count_program(const struct nvn_program *p,GLuint name) {
    unsigned width=nvn_render.target->b.width,height=nvn_render.target->b.height;
    for(unsigned i=0;i<nvn_state_profile.program_count;i++) {
        typeof(nvn_state_profile.programs[0]) *entry=&nvn_state_profile.programs[i];
        if(entry->id==p->id && entry->name==name && entry->sources[0]==p->sources[0] &&
           entry->sources[1]==p->sources[1] && entry->width==width && entry->height==height) {entry->draws++;entry->gpu_ns+=nvn_state_profile.pending_gpu_ns;nvn_state_profile.pending_gpu_ns=0;nvn_state_profile.last=entry;return;}
    }
    nvn_state_profile.last=NULL;
    if(nvn_state_profile.program_count==32){nvn_state_profile.program_overflow++;return;}
    nvn_state_profile.programs[nvn_state_profile.program_count++]=(typeof(nvn_state_profile.programs[0])){
        .id=p->id,.draws=1,.name=name,.sources={p->sources[0],p->sources[1]},.width=width,.height=height};
    nvn_state_profile.last=&nvn_state_profile.programs[nvn_state_profile.program_count-1];
    nvn_state_profile.programs[nvn_state_profile.program_count-1].gpu_ns+=nvn_state_profile.pending_gpu_ns;nvn_state_profile.pending_gpu_ns=0;
}
/* Diagnostic only (ZERO_DRAW_FINISH_PROFILE=1): glFinish after every draw and
 * attribute the wall time to the program/target entry. Serializes the GPU. */
static unsigned nvn_draw_finish_profile_enabled(void) {
    static int configured;static unsigned enabled;
    if(!configured){const char *o=getenv("ZERO_DRAW_FINISH_PROFILE");enabled=o&&!strcmp(o,"1");configured=1;
        if(enabled)printf("ZERO_DRAW_FINISH_PROFILE enabled=1 scope=diagnostic-glFinish-per-draw\n");}
    return enabled;
}
static void nvn_draw_finish_profile(void) {
    if(!nvn_draw_finish_profile_enabled())return;
    /* The program entry is counted after the draw; attribute on that count. */
    uint64_t t0=nvn_nanoseconds();glFinish();nvn_state_profile.pending_gpu_ns+=nvn_nanoseconds()-t0;
}
static unsigned nvn_render_state_send(unsigned slot,uint64_t value) {
    unsigned bit=1U<<slot,group=slot==NCS_PROGRAM?0:1;
    if((nvn_render_state.valid&bit) && nvn_render_state.value[slot]==value) {
        nvn_state_profile.skipped[group]++;return 0;
    }
    nvn_render_state.valid|=bit;nvn_render_state.value[slot]=value;nvn_state_profile.sent[group]++;return 1;
}
static void nvn_render_use_program(const struct nvn_program *p,GLuint name) {
    if(nvn_render_state.program_name!=name)nvn_render_state.valid&=~(1U<<NCS_PROGRAM);
    nvn_render_state.program_name=name;
    if(nvn_render_state_send(NCS_PROGRAM,p->id))glUseProgram(name);
}
static void nvn_render_cap(unsigned slot,GLenum cap,unsigned enabled) {
    if(nvn_render_state_send(slot,enabled)){if(enabled)glEnable(cap);else glDisable(cap);}
}
static void nvn_render_color_mask(unsigned mask) {
    mask&=15;
    if(nvn_render.target) {
        unsigned channels=zero_texture_half_channels(nvn_render.target->b.format)|zero_texture_float_channels(nvn_render.target->b.format)|zero_texture_unorm16_channels(nvn_render.target->b.format);
        if(nvn_render.target->b.format==0x0d)channels=2;
#if ZERO_ZX_GAME
        if(nvn_render.target->b.format==1)channels=1;
#endif
        if(channels)mask&=(1U<<channels)-1U;
    }
    if(nvn_render_state_send(NCS_COLOR_MASK,mask))glColorMask(mask&1,(mask>>1)&1,(mask>>2)&1,(mask>>3)&1);
}
static void nvn_render_depth_mask(unsigned enabled) {
    if(nvn_render_state_send(NCS_DEPTH_MASK,enabled))glDepthMask(enabled);
}
static void nvn_render_attrib_enable(unsigned index,unsigned enabled) {
    unsigned bit=1U<<index;
    if((nvn_render_state.attrib_known&bit) && !!(nvn_render_state.attrib_enabled&bit)==enabled) {
        nvn_state_profile.skipped[2]++;return;
    }
    nvn_render_state.attrib_known|=bit;
    if(enabled){nvn_render_state.attrib_enabled|=bit;glEnableVertexAttribArray(index);}
    else{nvn_render_state.attrib_enabled&=~bit;glDisableVertexAttribArray(index);}
    nvn_state_profile.sent[2]++;
}
/* Client and streaming-buffer paths share this pointer state. Presentation
 * starts another epoch even when its context is the producer context.
 * Client memory is still validated on every draw and GL fetches current bytes
 * when drawing, independently of whether its pointer state has changed. */
static void nvn_render_client_buffers(void) {
    if(nvn_render_state.client_buffers_known){nvn_state_profile.buffer_skipped+=2;return;}
    glBindBuffer(GL_ARRAY_BUFFER,0);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    nvn_render_state.client_buffers_known=1;nvn_state_profile.buffer_sent+=2;
}
static void nvn_render_vertex_pointer_buffer(unsigned index,GLint size,GLenum type,GLboolean normalized,GLsizei stride,const void *data,GLuint buffer) {
    if(index>=16)stop("NVN_VERTEX_POINTER_STATE",nvn_caller,index,24);
    unsigned bit=1U<<index;typeof(nvn_render_state.pointers[0]) *p=&nvn_render_state.pointers[index];
    if((nvn_render_state.pointer_known&bit) && p->data==data && p->size==size && p->type==type && p->normalized==normalized && p->stride==stride && p->buffer==buffer) {
        nvn_state_profile.pointer_skipped++;return;
    }
    glVertexAttribPointer(index,size,type,normalized,stride,data);
    *p=(typeof(*p)){.data=data,.size=size,.type=type,.normalized=normalized,.stride=stride,.buffer=buffer};
    nvn_render_state.pointer_known|=bit;nvn_state_profile.pointer_sent++;
}
static void nvn_render_vertex_pointer(unsigned index,GLint size,GLenum type,GLboolean normalized,GLsizei stride,const void *data) {
    if(!nvn_render_state.client_buffers_known)stop("NVN_VERTEX_POINTER_STATE",nvn_caller,index,24);
    nvn_render_vertex_pointer_buffer(index,size,type,normalized,stride,data,0);
}
