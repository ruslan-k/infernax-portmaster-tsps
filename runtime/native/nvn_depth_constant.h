/* C14: discard only a known constant full physical 512x512 depth image.
 * Recreate that exact clear before any consumer or partial clear. Neither
 * unknown depth nor the larger allocation behind LOW Window is eligible. */
typedef void (GL_APIENTRY *nvn_depth_discard_fn)(GLenum,GLsizei,const GLenum *);
static pthread_once_t nvn_depth_once=PTHREAD_ONCE_INIT;
static struct {
    unsigned enabled,resolved,available,after_frame;
    EGLContext context;nvn_depth_discard_fn discard;
    uint64_t full_clears,unknown_clears,discards,restores,readers,writers,identity_skipped,binding_skipped,unavailable;
} nvn_depth;
static void nvn_depth_configure(void) {
    const char *option=getenv("COTM_DEPTH_CONSTANT_DISCARD");
    if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("NVN_DEPTH_CONSTANT_OPTION",nvn_caller,0,24);
    nvn_depth.enabled=option&&!strcmp(option,"1");
    if(nvn_depth.enabled)printf("NVN_DEPTH_CONSTANT_EXPERIMENT enabled=1 physical=512x512 window=excluded discard=constant-only recovery=real-clear\n");
}
static unsigned nvn_depth_enabled(void) {
    if(pthread_once(&nvn_depth_once,nvn_depth_configure))stop("NVN_DEPTH_CONSTANT_ONCE",nvn_caller,0,21);
    return nvn_depth.enabled;
}
static unsigned nvn_depth_eligible(const struct nvn_texture *color,const struct nvn_texture *depth) {
    return color && depth && color->live && color->name && depth->live && depth->id && depth->depth_buffer &&
        color->b.width==512 && color->b.height==512 && depth->b.width==512 && depth->b.height==512 &&
        depth->b.depth==1 && depth->b.levels==1 && depth->b.target==1 && depth->b.samples<=1 &&
        depth->b.format==0x34 && nvn_low_color(color)<0;
}
static unsigned nvn_depth_full_clear(const struct nvn_texture *color,const struct nvn_texture *depth) {
    return nvn_depth_eligible(color,depth) && nvn_low.scissor_known && !nvn_low.scissor[0] && !nvn_low.scissor[1] &&
        nvn_low.scissor[2]==512 && nvn_low.scissor[3]==512;
}
static unsigned nvn_depth_identity(const struct nvn_texture *depth) {
    return depth->depth_constant_id==depth->id && depth->depth_constant_name==depth->depth_buffer &&
        depth->depth_constant_context==nvn_device.context && nvn_device.context!=EGL_NO_CONTEXT;
}
static unsigned nvn_depth_attachment(const struct nvn_texture *color,const struct nvn_texture *depth,GLuint framebuffer) {
    if(!framebuffer || nvn_framebuffers.active!=framebuffer || nvn_framebuffers.retired ||
       nvn_device.context==EGL_NO_CONTEXT || nvn_framebuffers.context!=nvn_device.context)return 0;
    for(unsigned i=0;i<NVN_FRAMEBUFFER_CACHE_SIZE;i++) {
        const struct nvn_framebuffer_entry *e=&nvn_framebuffers.entries[i];
        if(e->framebuffer==framebuffer && e->color_id==color->id && e->color==color->name &&
           e->depth_id==depth->id && e->depth==depth->depth_buffer)return 1;
    }
    return 0;
}
static unsigned nvn_depth_resolve(void) {
    if(!nvn_depth.resolved) {
        const char *extensions=(const char *)glGetString(GL_EXTENSIONS),*name="GL_EXT_discard_framebuffer";
        size_t length=strlen(name);unsigned found=0;
        for(const char *p=extensions;p && (p=strstr(p,name));p+=length)
            if((p==extensions||p[-1]==' ')&&(p[length]==' '||!p[length])){found=1;break;}
        if(found)nvn_depth.discard=(nvn_depth_discard_fn)eglGetProcAddress("glDiscardFramebufferEXT");
        nvn_depth.available=nvn_depth.discard!=NULL;nvn_depth.context=nvn_device.context;nvn_depth.resolved=1;
        printf("NVN_DEPTH_CONSTANT_BACKEND extension=%u entrypoint=%u fallback=preserve\n",found,nvn_depth.available);
    }
    return nvn_depth.available && nvn_depth.context==nvn_device.context;
}
static void nvn_depth_restore(struct nvn_texture *color,struct nvn_texture *depth,GLuint framebuffer) {
    if(!depth || !depth->depth_discarded)return;
    if(!depth->depth_constant_known || !nvn_depth_identity(depth) || !nvn_depth_eligible(color,depth) || nvn_depth.context!=nvn_device.context ||
       !nvn_depth_attachment(color,depth,framebuffer))stop("NVN_DEPTH_CONSTANT_RESTORE_IDENTITY",nvn_caller,depth->id,24);
    GLint bound=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&bound);
    if(bound<=0 || (GLuint)bound!=framebuffer)stop("NVN_DEPTH_CONSTANT_RESTORE_BINDING",nvn_caller,framebuffer,24);
    GLboolean mask=GL_FALSE,scissor=glIsEnabled(GL_SCISSOR_TEST);GLfloat clear=0;
    glGetBooleanv(GL_DEPTH_WRITEMASK,&mask);glGetFloatv(GL_DEPTH_CLEAR_VALUE,&clear);
    if(scissor)glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);glClearDepthf(depth->depth_constant_value);glClear(GL_DEPTH_BUFFER_BIT);
    glDepthMask(mask);glClearDepthf(clear);if(scissor)glEnable(GL_SCISSOR_TEST);
    depth->depth_discarded=0;nvn_depth.restores++;
    /* Actual mask, clear value and scissor enable are restored, so the existing
     * cache stays accurate. No color, stencil, viewport, binding or GL error
     * is modified/consumed, and no GPU completion is claimed. */
}
static void nvn_depth_before_clear(struct nvn_texture *color,struct nvn_texture *depth,GLuint framebuffer) {
    if(!nvn_depth_enabled() || !depth)return;
    if(!nvn_depth_full_clear(color,depth))nvn_depth_restore(color,depth,framebuffer);
}
static void nvn_depth_after_clear(struct nvn_texture *color,struct nvn_texture *depth,GLfloat value) {
    if(!nvn_depth.enabled || !depth)return;
    depth->depth_discarded=0;
    depth->depth_constant_known=nvn_depth_full_clear(color,depth) && isfinite(value);
    if(depth->depth_constant_known){depth->depth_constant_value=value;depth->depth_constant_id=depth->id;
        depth->depth_constant_name=depth->depth_buffer;depth->depth_constant_context=nvn_device.context;nvn_depth.full_clears++;}
    else nvn_depth.unknown_clears++;
}
static void nvn_depth_before_draw(struct nvn_texture *color,struct nvn_texture *depth,GLuint framebuffer,unsigned state) {
    if(!nvn_depth_enabled() || !depth || !(state&1))return;
    nvn_depth_restore(color,depth,framebuffer);nvn_depth.readers++;
    if(state&2){depth->depth_constant_known=0;nvn_depth.writers++;}
}
static void nvn_depth_leave(struct nvn_texture *color,struct nvn_texture *depth,GLuint framebuffer,
                            const struct nvn_texture *next_color,const struct nvn_texture *next_depth) {
    if(!nvn_depth_enabled() || !nvn_depth_eligible(color,depth) || !depth->depth_constant_known || depth->depth_discarded ||
       (color==next_color && depth==next_depth))return;
    if(!nvn_depth_identity(depth) || !nvn_depth_attachment(color,depth,framebuffer)){nvn_depth.identity_skipped++;return;}
    if(!nvn_depth_resolve()){nvn_depth.unavailable++;return;}
    GLint bound=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&bound);
    if(bound<=0 || (GLuint)bound!=framebuffer){nvn_depth.binding_skipped++;return;}
    const GLenum attachment=GL_DEPTH_ATTACHMENT;nvn_depth.discard(GL_FRAMEBUFFER,1,&attachment);
    depth->depth_discarded=1;nvn_depth.discards++;
}
static void nvn_depth_report(unsigned frame) {
    if(!nvn_depth.enabled || frame<nvn_depth.after_frame || frame-nvn_depth.after_frame<60)return;
    printf("NVN_DEPTH_CONSTANT_PROFILE after-frame=%u full-clears=%"PRIu64" unknown-clears=%"PRIu64" discarded=%"PRIu64
        " restored=%"PRIu64" readers=%"PRIu64" writers=%"PRIu64" identity-skipped=%"PRIu64" binding-skipped=%"PRIu64
        " unavailable=%"PRIu64" scope=cumulative attachment=depth-only bandwidth=not-measured\n",frame,
        nvn_depth.full_clears,nvn_depth.unknown_clears,nvn_depth.discards,nvn_depth.restores,nvn_depth.readers,nvn_depth.writers,
        nvn_depth.identity_skipped,nvn_depth.binding_skipped,nvn_depth.unavailable);
    nvn_depth.after_frame=frame;
}
