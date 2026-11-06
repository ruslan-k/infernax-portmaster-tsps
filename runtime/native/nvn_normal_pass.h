/* C17: postpone a previously qualified normal target until a real operation
 * needs its framebuffer. Clear constants are owned by the resource, not the
 * command arena. An omitted normal draw destroys that recoverable constant;
 * all existing UNMATERIALIZED and PROT_NONE consumer guards remain in force. */
static struct {
    unsigned configured,enabled,deferred,after_frame;
    struct nvn_texture *physical_color,*physical_depth;
    uint64_t color_id,depth_id,serial;
    GLuint color_name,depth_name,framebuffer;
    uint64_t targets,first_use,binds_avoided,color_clears,depth_clears,materialized,realized_targets;
} nvn_normal_pass;
static unsigned nvn_normal_pass_enabled(void) {
    if(!nvn_normal_pass.configured) {
        const char *option=getenv("COTM_NORMAL_PASS");
        if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("NVN_NORMAL_PASS_OPTION",nvn_caller,0,24);
        nvn_normal_pass.enabled=option&&!strcmp(option,"1");nvn_normal_pass.configured=1;
        if(nvn_normal_pass.enabled)printf("NVN_NORMAL_PASS_EXPERIMENT enabled=1 first-use=complete color=recoverable-constant depth=C14 logical-rects=deferred proof=physical-only\n");
    }
    return nvn_normal_pass.enabled;
}
static void nvn_normal_pass_guard(const struct nvn_texture *t) {
    /* State is validated separately at each draw; only storage eligibility is
     * relevant when selecting a target or recording a recoverable clear. */
    nvn_normal_validate_target(t,0,0,15);
    uint64_t base=t->pool->address+t->b.offset;
    if(!t->normal_guarded || !t->id || normal_guard_overlap(base,1)!=t->id ||
       normal_guard_overlap(base+t->size-1,1)!=t->id)
        stop("NVN_NORMAL_PASS_GUARD",nvn_caller,t->id,24);
}
static unsigned nvn_normal_pass_full(unsigned mask,const GLfloat color[4]) {
    if(mask!=15 || !nvn_low.scissor_known || nvn_low.scissor[0] || nvn_low.scissor[1] ||
       nvn_low.scissor[2]!=512 || nvn_low.scissor[3]!=512)return 0;
    for(unsigned i=0;i<4;i++)if(!isfinite(color[i]))return 0;
    return 1;
}
static void nvn_normal_pass_observe_bound(struct nvn_texture *color,struct nvn_texture *depth,GLuint framebuffer) {
    nvn_normal_pass.physical_color=color;nvn_normal_pass.physical_depth=depth;
    nvn_normal_pass.color_id=color?color->id:0;nvn_normal_pass.color_name=color?color->name:0;
    nvn_normal_pass.depth_id=depth?depth->id:0;nvn_normal_pass.depth_name=depth?depth->depth_buffer:0;
    nvn_normal_pass.framebuffer=framebuffer;nvn_normal_pass.serial=nvn_gl_completion.use_serial;
}
static void nvn_normal_pass_leave(struct nvn_texture *next,struct nvn_texture *depth) {
    struct nvn_texture *color=nvn_normal_pass.physical_color,*old_depth=nvn_normal_pass.physical_depth;
    /* Finalize/reuse can change a slot while a logical target is deferred.
     * Never apply an old attachment hint to that replacement identity. */
    if(!color || !color->live || color->id!=nvn_normal_pass.color_id || color->name!=nvn_normal_pass.color_name ||
       (old_depth && (!old_depth->live || old_depth->id!=nvn_normal_pass.depth_id || old_depth->depth_buffer!=nvn_normal_pass.depth_name)))return;
    nvn_depth_leave(color,old_depth,nvn_normal_pass.framebuffer,next,depth);
    nvn_normal_discard_target(color,nvn_normal_pass.framebuffer,next);
}
static GLuint nvn_normal_pass_bind(struct nvn_texture *color,struct nvn_texture *depth) {
    if(nvn_normal_pass.enabled && nvn_normal_pass.deferred && !nvn_direct_physical() && nvn_normal_pass.serial==nvn_gl_completion.use_serial &&
       color==nvn_normal_pass.physical_color && depth==nvn_normal_pass.physical_depth &&
       color->id==nvn_normal_pass.color_id && color->name==nvn_normal_pass.color_name &&
       (!depth || (depth->id==nvn_normal_pass.depth_id && depth->depth_buffer==nvn_normal_pass.depth_name)) &&
       nvn_framebuffers.active==nvn_normal_pass.framebuffer && !nvn_framebuffers.retired &&
       nvn_framebuffers.context==nvn_device.context && nvn_device.context!=EGL_NO_CONTEXT) {
        GLint bound=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&bound);
        if(bound>0 && (GLuint)bound==nvn_normal_pass.framebuffer) {
            nvn_normal_pass.binds_avoided++;return (GLuint)bound;
        }
    }
    return nvn_framebuffer_bind(color,depth);
}
static unsigned nvn_normal_pass_target(struct nvn_texture *color,struct nvn_texture *depth) {
    if(!nvn_normal_pass_enabled() || !nvn_normal_enabled() || !color->normal_guarded) {
        if(nvn_normal_pass.enabled)nvn_normal_pass.first_use++;
        return 0;
    }
    nvn_normal_pass_guard(color);
    nvn_normal_pass.deferred=nvn_low.defer_rects=1;
    nvn_render.target=color;nvn_render.depth=depth;nvn_render.framebuffer=0;
    nvn_low_target(color,depth);nvn_normal_pass.targets++;nvn_normal_pass.binds_avoided++;
    return 1;
}
static unsigned nvn_normal_pass_clear(unsigned mask,const GLfloat value[4]) {
    if(!nvn_normal_pass.deferred || !nvn_normal_pass_full(mask,value))return 0;
    struct nvn_texture *t=nvn_render.target;nvn_normal_pass_guard(t);
    t->normal_clear_id=t->id;t->normal_clear_name=t->name;t->normal_clear_context=nvn_device.context;
    if(t->normal_clear_context==EGL_NO_CONTEXT)stop("NVN_NORMAL_PASS_CONTEXT",nvn_caller,t->id,24);
    memcpy(t->normal_clear_value,value,sizeof(t->normal_clear_value));t->normal_pending_clear=1;
    /* This is a recoverable logical image. Consumers still materialize before
     * touching GL pixels; this is deliberately not a physical clear counter. */
    t->normal_unmaterialized=0;nvn_normal_pass.color_clears++;return 1;
}
static unsigned nvn_normal_pass_depth_clear(GLfloat value) {
    if(!nvn_normal_pass.deferred)return 0;
    nvn_normal_pass_guard(nvn_render.target);
    struct nvn_texture *depth=nvn_render.depth;
    if(!depth){nvn_normal_pass.depth_clears++;return 1;}
    if(!nvn_depth_enabled() || !nvn_depth_full_clear(nvn_render.target,depth) || !isfinite(value))return 0;
    nvn_depth_resolve();
    if(nvn_depth.context!=nvn_device.context || nvn_device.context==EGL_NO_CONTEXT)
        stop("NVN_NORMAL_PASS_DEPTH_CONTEXT",nvn_caller,depth->id,24);
    depth->depth_constant_id=depth->id;depth->depth_constant_name=depth->depth_buffer;
    depth->depth_constant_context=nvn_device.context;depth->depth_constant_value=value;
    depth->depth_constant_known=depth->depth_discarded=1;nvn_normal_pass.depth_clears++;return 1;
}
static void nvn_normal_pass_materialize(struct nvn_texture *t) {
    if(!t || !t->normal_pending_clear)return;
    nvn_normal_pass_guard(t);nvn_normal_require_materialized(t);
    if(t->normal_clear_id!=t->id || t->normal_clear_name!=t->name || t->normal_clear_context!=nvn_device.context ||
       nvn_device.context==EGL_NO_CONTEXT)stop("NVN_NORMAL_PASS_CLEAR_IDENTITY",nvn_caller,t->id,24);
    GLint previous=0;GLuint framebuffer=0;GLboolean mask[4],scissor=glIsEnabled(GL_SCISSOR_TEST);GLfloat clear[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&previous);glGetBooleanv(GL_COLOR_WRITEMASK,mask);glGetFloatv(GL_COLOR_CLEAR_VALUE,clear);
    /* A private temporary FBO cannot evict/reattach the saved producer cache
     * entry, even when every cache slot is occupied. No depth is attached. */
    glGenFramebuffers(1,&framebuffer);if(!framebuffer)stop("NVN_NORMAL_PASS_FBO_MEMORY",nvn_caller,t->id,21);
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t->name,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)stop("NVN_NORMAL_PASS_FBO_INCOMPLETE",nvn_caller,t->id,21);
    if(scissor)glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glClearColor(t->normal_clear_value[0],t->normal_clear_value[1],t->normal_clear_value[2],t->normal_clear_value[3]);glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(mask[0],mask[1],mask[2],mask[3]);glClearColor(clear[0],clear[1],clear[2],clear[3]);
    if(scissor)glEnable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)previous);glDeleteFramebuffers(1,&framebuffer);
    t->normal_pending_clear=0;nvn_normal_pass.materialized++;
}
static void nvn_normal_pass_real_target(unsigned color_consumer) {
    if(nvn_normal_pass.deferred) {
        struct nvn_texture *t=nvn_render.target,*depth=nvn_render.depth;nvn_normal_pass_guard(t);
        nvn_normal_pass_leave(t,depth);nvn_render.framebuffer=nvn_normal_pass_bind(t,depth);
        nvn_normal_pass_observe_bound(t,depth,nvn_render.framebuffer);
        nvn_normal_pass.deferred=nvn_low.defer_rects=0;nvn_low_target(t,depth);nvn_direct_target();nvn_normal_pass.realized_targets++;
    }
    if(color_consumer)nvn_normal_pass_materialize(nvn_render.target);
}
static void nvn_normal_pass_report(unsigned frame) {
    if(!nvn_normal_pass.enabled || frame<nvn_normal_pass.after_frame || frame-nvn_normal_pass.after_frame<60)return;
    printf("NVN_NORMAL_PASS_PROFILE after-frame=%u deferred-targets=%"PRIu64" first-use-fallback=%"PRIu64
        " binds-avoided=%"PRIu64" deferred-color-clears=%"PRIu64" deferred-depth-clears=%"PRIu64
        " materialized-colors=%"PRIu64" realized-targets=%"PRIu64" scope=cumulative proof=physical-only\n",frame,
        nvn_normal_pass.targets,nvn_normal_pass.first_use,nvn_normal_pass.binds_avoided,nvn_normal_pass.color_clears,
        nvn_normal_pass.depth_clears,nvn_normal_pass.materialized,nvn_normal_pass.realized_targets);
    nvn_normal_pass.after_frame=frame;
}
