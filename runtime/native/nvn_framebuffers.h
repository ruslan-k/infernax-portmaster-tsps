/* Producer-context FBOs with immutable attachment identities. Texture storage
 * is defined once in TextureInitialize; subimage uploads do not change it.
 * Any future in-place storage definition must invalidate this cache first.
 * Never use these names in the shared presentation context: ES2 permits either
 * shared or separate FBO namespaces. Every name comes from GenFramebuffers.
 */
#if defined(ANIMAL_WELL_SHADERS)
#define NVN_FRAMEBUFFER_CACHE_SIZE 64U /* ~45 distinct render targets per frame; 16 evicted 25 FBOs per frame (measured aw093) */
#elif defined(MOMO_SHADERS)
/* Momodora's title frame switches target 26 times and the sixteen-entry cache thrashes on it: 170 misses
 * and 170 evictions in sixty frames, so ~3 FBOs a frame are torn down and rebuilt for nothing
 * (MEDIDO run momo-214, NVN_FRAMEBUFFER_CACHE_PROFILE). */
#define NVN_FRAMEBUFFER_CACHE_SIZE 64U
#else
#define NVN_FRAMEBUFFER_CACHE_SIZE 16U
#endif
struct nvn_framebuffer_entry {
    uint64_t color_id,depth_id;
    GLuint framebuffer,color,depth;
};
static struct {
    EGLContext context;
    struct nvn_framebuffer_entry entries[NVN_FRAMEBUFFER_CACHE_SIZE];
    GLuint active,retired;
    unsigned next;
} nvn_framebuffers;
static struct {
    uint64_t hits,misses,evictions,deleted,retired,rebind_skipped;
    unsigned after_frame;
} nvn_framebuffer_profile;
static struct {int configured;unsigned enabled;} nvn_framebuffer_rebind;
static unsigned nvn_framebuffer_rebind_skip_enabled(void) {
    if(!nvn_framebuffer_rebind.configured){const char *o=getenv("COTM_FBO_REBIND_SKIP");nvn_framebuffer_rebind.enabled=o&&!strcmp(o,"1");nvn_framebuffer_rebind.configured=1;}
    return nvn_framebuffer_rebind.enabled;
}
static void nvn_framebuffer_context(void) {
    if(nvn_device.context==EGL_NO_CONTEXT ||
       (nvn_framebuffers.context!=EGL_NO_CONTEXT && nvn_framebuffers.context!=nvn_device.context))
        stop("NVN_FRAMEBUFFER_CONTEXT",nvn_caller,0,24);
    nvn_framebuffers.context=nvn_device.context;
}
/* All callers hold the producer context mutex with that context current.
 * Bind on every call: uploads/acquires and temporary diagnostic FBOs must not
 * turn attachment caching into an assumption about current binding state.
 */
static GLuint nvn_framebuffer_bind(const struct nvn_texture *color,const struct nvn_texture *depth) {
    nvn_framebuffer_context();
    if(color->b.format==6 || (color->b.format==1 && !ZERO_ZX_GAME)) {
        printf("ZERO_PACKED_TARGET_REJECT object=0x%"PRIx64" id=%"PRIu64" format=0x%x size=%ux%u levels=%u target=%u flags=0x%x pool-flags=0x%x swizzle=%u/%u/%u/%u\n",
               color->object,color->id,color->b.format,color->b.width,color->b.height,color->b.levels,color->b.target,color->b.flags,color->pool?color->pool->flags:0,
               color->b.swizzle[0],color->b.swizzle[1],color->b.swizzle[2],color->b.swizzle[3]);
        stop("NVN_LC2_PACKED_RENDER_TARGET_COHERENCE_REQUIRED",nvn_caller,color->b.format,20);
    }
#if ZERO_ZX_GAME
    if(color->b.format==1) {
        if(!color->pool || !(color->pool->flags&4) || (color->pool->flags&3) || color->b.target!=1 ||
           color->b.depth!=1 || color->b.levels!=1 || color->b.samples || (color->b.flags&~8U) ||
           color->host_half || color->host_scaled || color->packaged_lazy || color->b.packaged_layout)
            stop("ZERO_R8_RENDER_TARGET_CONTRACT",nvn_caller,color->id,20);
        for(unsigned i=0;i<4096;i++) {
            const struct nvn_texture *other=&nvn_textures[i];
            if(other!=color && other->live && other->pool==color->pool &&
               color->b.offset<other->b.offset+other->size && other->b.offset<color->b.offset+color->size)
                stop("ZERO_R8_RENDER_TARGET_ALIAS_REQUIRED",nvn_caller,other->id,20);
        }
        __atomic_store_n(&color->pool->zero_r8_target_seen,1,__ATOMIC_RELEASE);
    }
#endif
    uint64_t depth_id=depth?depth->id:0;GLuint depth_name=depth?depth->depth_buffer:0;
    unsigned slot=NVN_FRAMEBUFFER_CACHE_SIZE,empty=NVN_FRAMEBUFFER_CACHE_SIZE;
    for(unsigned i=0;i<NVN_FRAMEBUFFER_CACHE_SIZE;i++) {
        const struct nvn_framebuffer_entry *entry=&nvn_framebuffers.entries[i];
        if(!entry->framebuffer){if(empty==NVN_FRAMEBUFFER_CACHE_SIZE)empty=i;continue;}
        if(entry->color_id==color->id && entry->color==color->name && entry->depth_id==depth_id && entry->depth==depth_name) {
            slot=i;break;
        }
    }
    if(slot<NVN_FRAMEBUFFER_CACHE_SIZE) {
        nvn_framebuffer_profile.hits++;
        if(nvn_framebuffers.entries[slot].framebuffer==nvn_framebuffers.active && !nvn_framebuffers.retired && nvn_framebuffer_rebind_skip_enabled())
            nvn_framebuffer_profile.rebind_skipped++;
        else glBindFramebuffer(GL_FRAMEBUFFER,nvn_framebuffers.entries[slot].framebuffer);
    } else {
        nvn_framebuffer_profile.misses++;
        slot=empty;
        if(slot==NVN_FRAMEBUFFER_CACHE_SIZE) {
            slot=nvn_framebuffers.next;nvn_framebuffers.next=(slot+1)%NVN_FRAMEBUFFER_CACHE_SIZE;
            nvn_framebuffer_profile.evictions++;
        }
        GLuint framebuffer=nvn_framebuffers.entries[slot].framebuffer;
        if(!framebuffer)glGenFramebuffers(1,&framebuffer);
        if(!framebuffer)stop("NVN_FRAMEBUFFER_MEMORY",nvn_caller,0,21);
        glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color->name,0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth_name);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_RENDERBUFFER,
                                  depth && depth->b.format==0x35?depth_name:0);
        GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if(status!=GL_FRAMEBUFFER_COMPLETE)stop("NVN_FRAMEBUFFER_INCOMPLETE",nvn_caller,status,21);
        nvn_framebuffers.entries[slot]=(struct nvn_framebuffer_entry){color->id,depth_id,framebuffer,color->name,depth_name};
    }
    nvn_framebuffers.active=nvn_framebuffers.entries[slot].framebuffer;
    if(nvn_framebuffers.retired) {
        glDeleteFramebuffers(1,&nvn_framebuffers.retired);nvn_framebuffers.retired=0;
    }
    return nvn_framebuffers.active;
}
static void nvn_framebuffer_forget(uint64_t id) {
    if(nvn_framebuffers.context==EGL_NO_CONTEXT)return;
    nvn_framebuffer_context();
    for(unsigned i=0;i<NVN_FRAMEBUFFER_CACHE_SIZE;i++) {
        struct nvn_framebuffer_entry *entry=&nvn_framebuffers.entries[i];
        if(!entry->framebuffer || (entry->color_id!=id && entry->depth_id!=id))continue;
        if(entry->framebuffer==nvn_framebuffers.active) {
            /* Preserve the old bound FBO through DeleteTexture/Renderbuffer.
             * GL detaches only the deleted image, leaving the other attachment
             * and current binding intact. Delete this single uncached FBO once
             * the next valid target is bound, never draw accidentally to FBO0.
             */
            if(nvn_framebuffers.retired)stop("NVN_FRAMEBUFFER_RETIRE_STATE",nvn_caller,id,24);
            nvn_framebuffers.retired=entry->framebuffer;nvn_framebuffer_profile.retired++;
        } else {
            glDeleteFramebuffers(1,&entry->framebuffer);nvn_framebuffer_profile.deleted++;
        }
        *entry=(struct nvn_framebuffer_entry){0};
    }
}
static void nvn_framebuffer_report(unsigned frame) {
    if(frame<nvn_framebuffer_profile.after_frame || frame-nvn_framebuffer_profile.after_frame<60)return;
    printf("NVN_TEXTURE_HINT_PROFILE hits=%" PRIu64 " misses=%" PRIu64 " stamp-hits=%" PRIu64
           " slots=%u scope=cumulative key=object hit=live-and-object-or-rechecked-stamp\n",
        nvn_texture_hint_hits,nvn_texture_hint_misses,nvn_texture_hint_stamp_hits,NVN_TEXTURE_HINT_SLOTS);
    printf("NVN_FRAMEBUFFER_CACHE_PROFILE after-frame=%u frames=%u hits=%"PRIu64" misses=%"PRIu64
           " evictions=%"PRIu64" finalize-deleted=%"PRIu64" finalize-retired=%"PRIu64" rebind-skipped=%"PRIu64
           " capacity=%u scope=since-last-report bind=every-target attach-check=miss-only\n",
           frame,frame-nvn_framebuffer_profile.after_frame,nvn_framebuffer_profile.hits,nvn_framebuffer_profile.misses,
           nvn_framebuffer_profile.evictions,nvn_framebuffer_profile.deleted,nvn_framebuffer_profile.retired,nvn_framebuffer_profile.rebind_skipped,NVN_FRAMEBUFFER_CACHE_SIZE);
    nvn_framebuffer_profile=(typeof(nvn_framebuffer_profile)){.after_frame=frame};
}
