/* Zero-local producer FBOs for exact uniform D24S8 copies. No guest image,
 * completion, render-target binding cache or depth proof is changed here.
 * Texture storage is immutable; its last Finalize purges before deleting RBO.
 */
#ifndef ZERO_DEPTH_FRAMEBUFFER_H
#define ZERO_DEPTH_FRAMEBUFFER_H
#define ZERO_DEPTH_FRAMEBUFFER_CAPACITY 8U
struct zero_depth_framebuffer_entry {
    uint64_t storage_id;
    GLuint framebuffer,renderbuffer;
    unsigned logical_width,logical_height,width,height;
    EGLContext context;
};
static struct {
    unsigned configured,enabled,next;
    EGLContext context;
    struct zero_depth_framebuffer_entry entries[ZERO_DEPTH_FRAMEBUFFER_CAPACITY];
    uint64_t hits,misses,evictions,deleted;
} zero_depth_framebuffers;
static unsigned zero_depth_framebuffer_enabled(void) {
    if(!zero_depth_framebuffers.configured) {
        const char *option=getenv("ZERO_DEPTH_COPY_FBO_CACHE");
        if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("ZERO_DEPTH_FBO_OPTION",nvn_caller,0,24);
        zero_depth_framebuffers.enabled=option&&!strcmp(option,"1");zero_depth_framebuffers.configured=1;
        if(zero_depth_framebuffers.enabled)printf("ZERO_DEPTH_FBO_CACHE enabled=1 capacity=8 identity=storage-id-rbo-geometry-producer-context release=last-storage-view binding=restored\n");
    }
    return zero_depth_framebuffers.enabled;
}
static void zero_depth_framebuffer_context(void) {
    EGLContext current=eglGetCurrentContext();
    if(current==EGL_NO_CONTEXT || current!=nvn_device.context ||
       (zero_depth_framebuffers.context!=EGL_NO_CONTEXT && zero_depth_framebuffers.context!=current))
        stop("ZERO_DEPTH_FBO_CONTEXT",nvn_caller,0,24);
    zero_depth_framebuffers.context=current;
}
static GLuint zero_depth_framebuffer_bind(const struct nvn_texture *t,GLuint old_fbo) {
    zero_depth_framebuffer_context();
    unsigned width=zero_render_scale_width(t),height=zero_render_scale_height(t);
    if(!t->live || !t->id || !t->depth_buffer || t->b.format!=0x35 ||
       t->b.target!=1 || t->b.levels!=1 || !t->b.width || !t->b.height || !width || !height)
        stop("ZERO_DEPTH_FBO_STORAGE",nvn_caller,t->id,24);
    unsigned empty=ZERO_DEPTH_FRAMEBUFFER_CAPACITY;
    /* Reject any cached FBO already bound by the caller before any hit can
     * return, so a later entry equal to old_fbo cannot be skipped. */
    for(unsigned i=0;i<ZERO_DEPTH_FRAMEBUFFER_CAPACITY;i++) {
        const struct zero_depth_framebuffer_entry *e=&zero_depth_framebuffers.entries[i];
        if(e->framebuffer && e->framebuffer==old_fbo)stop("ZERO_DEPTH_FBO_ALREADY_BOUND",nvn_caller,e->storage_id,24);
    }
    for(unsigned i=0;i<ZERO_DEPTH_FRAMEBUFFER_CAPACITY;i++) {
        struct zero_depth_framebuffer_entry *e=&zero_depth_framebuffers.entries[i];
        if(!e->framebuffer){if(empty==ZERO_DEPTH_FRAMEBUFFER_CAPACITY)empty=i;continue;}
        if(e->storage_id!=t->id)continue;
        if(e->renderbuffer!=t->depth_buffer || e->logical_width!=t->b.width || e->logical_height!=t->b.height ||
           e->width!=width || e->height!=height || e->context!=nvn_device.context)
            stop("ZERO_DEPTH_FBO_STORAGE_CHANGED",nvn_caller,t->id,24);
        glBindFramebuffer(GL_FRAMEBUFFER,e->framebuffer);zero_depth_framebuffers.hits++;return e->framebuffer;
    }
    unsigned slot=empty;
    if(slot==ZERO_DEPTH_FRAMEBUFFER_CAPACITY) {
        slot=zero_depth_framebuffers.next;
        struct zero_depth_framebuffer_entry *old=&zero_depth_framebuffers.entries[slot];
        if(old->framebuffer==old_fbo || old->framebuffer==nvn_framebuffers.active)
            stop("ZERO_DEPTH_FBO_EVICT_BOUND",nvn_caller,old->storage_id,24);
        glDeleteFramebuffers(1,&old->framebuffer);*old=(struct zero_depth_framebuffer_entry){0};
        zero_depth_framebuffers.next=(slot+1)%ZERO_DEPTH_FRAMEBUFFER_CAPACITY;zero_depth_framebuffers.evictions++;
    }
    GLuint fbo=0;glGenFramebuffers(1,&fbo);if(!fbo)stop("ZERO_DEPTH_FBO_ALLOCATION",nvn_caller,t->id,21);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,t->depth_buffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_RENDERBUFFER,t->depth_buffer);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)stop("ZERO_DEPTH_COPY_FRAMEBUFFER",nvn_caller,t->id,21);
    zero_depth_framebuffers.entries[slot]=(struct zero_depth_framebuffer_entry){
        t->id,fbo,t->depth_buffer,t->b.width,t->b.height,width,height,nvn_device.context};
    zero_depth_framebuffers.misses++;return fbo;
}
static void zero_depth_framebuffer_forget(uint64_t storage_id) {
    if(!zero_depth_framebuffer_enabled() || zero_depth_framebuffers.context==EGL_NO_CONTEXT)return;
    zero_depth_framebuffer_context();GLint current=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&current);
    /* Validate the whole purge before any delete; a temporary copy FBO must
     * already have been restored by zero_depth_copy before Finalize enters. */
    for(unsigned i=0;i<ZERO_DEPTH_FRAMEBUFFER_CAPACITY;i++) {
        const struct zero_depth_framebuffer_entry *e=&zero_depth_framebuffers.entries[i];
        if(e->framebuffer && e->storage_id==storage_id &&
           (e->framebuffer==(GLuint)current || e->framebuffer==nvn_framebuffers.active))
            stop("ZERO_DEPTH_FBO_FINALIZE_BOUND",nvn_caller,storage_id,24);
    }
    for(unsigned i=0;i<ZERO_DEPTH_FRAMEBUFFER_CAPACITY;i++) {
        struct zero_depth_framebuffer_entry *e=&zero_depth_framebuffers.entries[i];
        if(e->framebuffer && e->storage_id==storage_id) {
            glDeleteFramebuffers(1,&e->framebuffer);*e=(struct zero_depth_framebuffer_entry){0};zero_depth_framebuffers.deleted++;
        }
    }
}
#endif
