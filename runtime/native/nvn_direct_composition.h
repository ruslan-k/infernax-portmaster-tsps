/* C15: Window composition may own the physical backbuffer only after a new
 * original full clear. Logical textures become stale until a real copyback or
 * full logical clear. Swap consumes ownership, never proves GPU completion. */
static struct {
    unsigned enabled,active,slot,first_captured,depth_temporary,bound_direct;
    uint64_t next_epoch,epochs[3],ids[3],epoch,window,id;
    unsigned stale[3];GLuint name,depth_name;uint64_t depth_id;
    struct nvn_texture *depth;EGLContext context;EGLSurface surface;
    struct nvn_queue *current_queue,*queue;
    uint64_t queue_object,queue_address;
    uint64_t direct_frames,canonical_frames,clears,copies,depth_clears,alpha_clears;
} nvn_direct;
static pthread_once_t nvn_direct_once=PTHREAD_ONCE_INIT;
static void nvn_direct_configure(void) {
    const char *option=getenv("COTM_DIRECT_COMPOSITION");
    if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("NVN_DIRECT_OPTION",nvn_caller,0,24);
    nvn_direct.enabled=option&&!strcmp(option,"1");
    if(nvn_direct.enabled)printf("NVN_DIRECT_COMPOSITION enabled=1 admission=original-full-clear frame=acquire-epoch physical=640x360 texture-stale=guarded alpha=present-only completion=independent\n");
}
static unsigned nvn_direct_enabled(void) {
    if(pthread_once(&nvn_direct_once,nvn_direct_configure))stop("NVN_DIRECT_ONCE",nvn_caller,0,21);
    return nvn_direct.enabled;
}
static int nvn_direct_slot(const struct nvn_texture *t) {
    if(!t)return -1;
    for(unsigned i=0;i<nvn_window.count;i++)if(nvn_window.textures[i]==t)return (int)i;
    return -1;
}
static void nvn_direct_environment(void) {
    const char *surface=getenv("COTM_FBDEV_640");
    if(!nvn_single_enabled() || nvn_window.game_pbuffer || !nvn_window.live || !nvn_low.enabled ||
       !surface || strcmp(surface,"1") || nvn_window.fb.width!=640 || nvn_window.fb.height!=360 ||
       nvn_device.context==EGL_NO_CONTEXT || nvn_window.surface==EGL_NO_SURFACE || nvn_device.surface!=nvn_window.surface)
        stop("NVN_DIRECT_ENVIRONMENT",nvn_caller,0,24);
    nvn_single_require_owner();
}
static void nvn_direct_texture(unsigned slot) {
    if(slot>=nvn_window.count || slot>=3)stop("NVN_DIRECT_SLOT",nvn_caller,slot,24);
    struct nvn_texture *t=nvn_window.textures[slot];
    if(!t || !t->live || !t->id || !t->name || nvn_low_color(t)!=(int)slot ||
       t->b.width!=1280 || t->b.height!=720 || t->b.levels!=1 ||
       (nvn_direct.ids[slot] && nvn_direct.ids[slot]!=t->id))stop("NVN_DIRECT_TEXTURE_IDENTITY",nvn_caller,slot,24);
    nvn_direct.ids[slot]=t->id;
}
static void nvn_direct_identity(void) {
    if(!nvn_direct.active)stop("NVN_DIRECT_NO_OWNER",nvn_caller,0,24);
    nvn_direct_environment();nvn_direct_texture(nvn_direct.slot);
    struct nvn_texture *t=nvn_window.textures[nvn_direct.slot];struct nvn_queue *q=nvn_direct.queue;
    if(!nvn_window.acquired[nvn_direct.slot] || nvn_direct.epoch!=nvn_direct.epochs[nvn_direct.slot] ||
       nvn_direct.id!=t->id || nvn_direct.name!=t->name || nvn_direct.window!=nvn_window.object ||
       nvn_direct.context!=nvn_device.context || nvn_direct.surface!=nvn_window.surface ||
       !q || !q->live || q->object!=nvn_direct.queue_object || q->address!=nvn_direct.queue_address)
        stop("NVN_DIRECT_OWNER_IDENTITY",nvn_caller,nvn_direct.epoch,24);
    if(nvn_direct.depth && (!nvn_direct.depth->live || nvn_direct.depth->id!=nvn_direct.depth_id ||
       nvn_direct.depth->depth_buffer!=nvn_direct.depth_name))stop("NVN_DIRECT_DEPTH_IDENTITY",nvn_caller,nvn_direct.depth_id,24);
    if(nvn_render.target==t && nvn_render.depth!=nvn_direct.depth)stop("NVN_DIRECT_DEPTH_CHANGED",nvn_caller,t->id,24);
}
static void nvn_direct_acquire(unsigned slot) {
    if(!nvn_direct_enabled())return;
    nvn_direct_environment();nvn_direct_texture(slot);
    if(nvn_direct.active)stop("NVN_DIRECT_ACQUIRE_PENDING",nvn_caller,slot,24);
    if(!++nvn_direct.next_epoch)stop("NVN_DIRECT_EPOCH_WRAP",nvn_caller,0,24);
    nvn_direct.epochs[slot]=nvn_direct.next_epoch;
}
static void nvn_direct_submit(struct nvn_queue *q) {
    if(nvn_direct_enabled())nvn_direct.current_queue=q;
}
static unsigned nvn_direct_target_active(void) {
    return nvn_direct.enabled && nvn_direct.bound_direct && nvn_render.target && nvn_direct_slot(nvn_render.target)>=0;
}
static unsigned nvn_direct_physical(void) {return nvn_direct.enabled && nvn_direct.bound_direct;}
static void nvn_direct_target(void) {
    nvn_direct.bound_direct=0;
    if(!nvn_direct_enabled() || !nvn_direct.active || nvn_direct_slot(nvn_render.target)!=(int)nvn_direct.slot)return;
    nvn_direct_identity();glBindFramebuffer(GL_FRAMEBUFFER,0);nvn_direct.bound_direct=1;
}
static unsigned nvn_direct_full(unsigned mask,const float *color) {
    if((mask&15)!=15 || !nvn_low.scissor_known || nvn_low.scissor[0] || nvn_low.scissor[1] ||
       nvn_low.scissor[2]!=1280 || nvn_low.scissor[3]!=720)return 0;
    for(unsigned i=0;i<4;i++)if(!isfinite(color[i]))return 0;
    GLint scissor[4];glGetIntegerv(GL_SCISSOR_BOX,scissor);
    return !glIsEnabled(GL_SCISSOR_TEST) || (!scissor[0]&&!scissor[1]&&scissor[2]==640&&scissor[3]==360);
}
static void nvn_direct_before_color(unsigned mask,const float *color) {
    if(!nvn_direct_enabled())return;
    int slot=nvn_direct_slot(nvn_render.target);if(slot<0)return;
    nvn_direct_environment();nvn_direct_texture(slot);
    unsigned full=nvn_direct_full(mask,color),acquired=0;
    for(unsigned i=0;i<nvn_window.count;i++)acquired+=!!nvn_window.acquired[i];
    if(nvn_direct.active && slot==(int)nvn_direct.slot) {
        nvn_direct_identity();
        if(nvn_direct.current_queue!=nvn_direct.queue)stop("NVN_DIRECT_QUEUE_CHANGED",nvn_caller,0,24);
        glBindFramebuffer(GL_FRAMEBUFFER,0);nvn_direct.bound_direct=1;return;
    }
    if(full && !nvn_direct.active && acquired==1 && nvn_window.acquired[slot] && nvn_direct.epochs[slot]) {
        struct nvn_queue *q=nvn_direct.current_queue;if(!q||!q->live)stop("NVN_DIRECT_CLEAR_QUEUE",nvn_caller,0,24);
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        GLint bits[5];const GLenum fields[5]={GL_RED_BITS,GL_GREEN_BITS,GL_BLUE_BITS,GL_ALPHA_BITS,GL_SAMPLE_BUFFERS};
        for(unsigned i=0;i<5;i++)glGetIntegerv(fields[i],&bits[i]);
        if(bits[0]!=8||bits[1]!=8||bits[2]!=8||bits[3]!=8||bits[4])stop("NVN_DIRECT_DEFAULT_FORMAT",nvn_caller,bits[4],24);
        struct nvn_texture *t=nvn_render.target;
        nvn_direct.active=1;nvn_direct.slot=slot;nvn_direct.epoch=nvn_direct.epochs[slot];nvn_direct.window=nvn_window.object;
        nvn_direct.id=t->id;nvn_direct.name=t->name;nvn_direct.context=nvn_device.context;nvn_direct.surface=nvn_window.surface;
        nvn_direct.depth=nvn_render.depth;nvn_direct.depth_id=nvn_render.depth?nvn_render.depth->id:0;
        nvn_direct.depth_name=nvn_render.depth?nvn_render.depth->depth_buffer:0;
        nvn_direct.queue=q;nvn_direct.queue_object=q->object;nvn_direct.queue_address=q->address;
        nvn_direct.stale[slot]=1;nvn_direct.clears++;nvn_direct.bound_direct=1;return;
    }
    if(nvn_direct.stale[slot]&&!full)stop("NVN_DIRECT_STALE_PARTIAL_CLEAR",nvn_caller,nvn_render.target->id,20);
    if(nvn_direct.bound_direct){glBindFramebuffer(GL_FRAMEBUFFER,nvn_render.framebuffer);nvn_direct.bound_direct=0;}
    if(full)nvn_direct.stale[slot]=0; /* original full clear executes next */
}
static void nvn_direct_depth_begin(void) {
    if(!nvn_direct_target_active())return;
    if(nvn_direct.depth_temporary)stop("NVN_DIRECT_DEPTH_REENTRY",nvn_caller,0,24);
    glBindFramebuffer(GL_FRAMEBUFFER,nvn_render.framebuffer);nvn_direct.bound_direct=0;nvn_direct.depth_temporary=1;
}
static void nvn_direct_depth_end(void) {
    if(!nvn_direct.depth_temporary)return;
    glBindFramebuffer(GL_FRAMEBUFFER,0);nvn_direct.bound_direct=1;nvn_direct.depth_temporary=0;nvn_direct.depth_clears++;
}
static void nvn_direct_materialize(void) {
    nvn_direct_identity();struct nvn_texture *t=nvn_window.textures[nvn_direct.slot];
    GLint framebuffer=0,texture=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&framebuffer);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
    glBindFramebuffer(GL_FRAMEBUFFER,0);glBindTexture(GL_TEXTURE_2D,t->name);
    glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,640,360);
    glBindTexture(GL_TEXTURE_2D,(GLuint)texture);glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)framebuffer);
    GLenum error=glGetError();if(error)stop("NVN_DIRECT_COPYBACK",nvn_caller,error,21);
    nvn_direct.stale[nvn_direct.slot]=0;nvn_direct.active=0;nvn_direct.queue=NULL;nvn_direct.copies++;
}
static void nvn_direct_before_draw(unsigned depth_state) {
    if(!nvn_direct_enabled())return;
    int slot=nvn_direct_slot(nvn_render.target);if(slot<0)return;
    nvn_direct_texture(slot);
    if(nvn_direct.active && slot==(int)nvn_direct.slot) {
        nvn_direct_identity();
        if(nvn_direct.current_queue!=nvn_direct.queue)stop("NVN_DIRECT_QUEUE_CHANGED",nvn_caller,0,24);
        if(depth_state&1){nvn_direct_materialize();glBindFramebuffer(GL_FRAMEBUFFER,nvn_render.framebuffer);nvn_direct.bound_direct=0;}
        else {glBindFramebuffer(GL_FRAMEBUFFER,0);nvn_direct.bound_direct=1;}
    } else if(nvn_direct.stale[slot])stop("NVN_DIRECT_STALE_DRAW",nvn_caller,nvn_render.target->id,20);
}
static unsigned nvn_direct_present(struct nvn_queue *q,struct nvn_texture *t) {
    if(!nvn_direct_enabled())return 0;
    int slot=nvn_direct_slot(t);if(slot<0)stop("NVN_DIRECT_PRESENT_SLOT",nvn_caller,t?t->id:0,24);
    nvn_direct_environment();nvn_direct_texture(slot);
    if(nvn_direct.active) {
        nvn_direct_identity();
        if(slot!=(int)nvn_direct.slot || q!=nvn_direct.queue)stop("NVN_DIRECT_PRESENT_OWNER",nvn_caller,slot,24);
        return 1;
    }
    if(nvn_direct.stale[slot])stop("NVN_DIRECT_STALE_PRESENT",nvn_caller,t->id,20);
    nvn_direct.canonical_frames++;return 0;
}
static void nvn_direct_alpha(void) {
    nvn_direct_identity();glBindFramebuffer(GL_FRAMEBUFFER,0);
    GLboolean mask[4],scissor=glIsEnabled(GL_SCISSOR_TEST);GLfloat clear[4];
    glGetBooleanv(GL_COLOR_WRITEMASK,mask);glGetFloatv(GL_COLOR_CLEAR_VALUE,clear);
    glDisable(GL_SCISSOR_TEST);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_TRUE);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(mask[0],mask[1],mask[2],mask[3]);glClearColor(clear[0],clear[1],clear[2],clear[3]);if(scissor)glEnable(GL_SCISSOR_TEST);
    nvn_direct.alpha_clears++;
}
static void nvn_direct_sampled(const uint8_t *pixels,unsigned width,unsigned height,unsigned frame) {
    if(nvn_direct.first_captured)return;
    nvn_direct_identity();if(!pixels||width!=640||height!=360)stop("NVN_DIRECT_CAPTURE_SIZE",nvn_caller,width,24);
    size_t bytes=(size_t)width*height*4;
    for(size_t i=3;i<bytes;i+=4)if(pixels[i]!=255)stop("NVN_DIRECT_CAPTURE_ALPHA",nvn_caller,pixels[i],22);
    nvn_private_dump("direct-rgba",frame,pixels,bytes);nvn_direct.first_captured=1;
    printf("NVN_DIRECT_FIRST_RGBA frame=%u size=%ux%u alpha255=%u rgb-proof=separate boundary=physical-backbuffer-before-present\n",frame,width,height,width*height);
}
static void nvn_direct_swapped(void) {
    if(!nvn_direct.active)return;
    nvn_direct.active=0;nvn_direct.queue=NULL;nvn_direct.direct_frames++;
}
static void nvn_direct_forget(struct nvn_texture *t) {
    if(!nvn_direct_enabled())return;
    int slot=nvn_direct_slot(t);
    if(nvn_direct.active && (slot==(int)nvn_direct.slot || t==nvn_direct.depth))stop("NVN_DIRECT_FINALIZE_PENDING",nvn_caller,t->id,24);
    /* C17 may have a deferred logical normal target while the last Window
     * FBO remains cache.active and the physical binding is still zero. Use
     * that real cached attachment tuple, not the virtual logical target. */
    if(nvn_direct.bound_direct && nvn_framebuffers.active && !nvn_framebuffers.retired)
        for(unsigned i=0;i<NVN_FRAMEBUFFER_CACHE_SIZE;i++) {
            const struct nvn_framebuffer_entry *e=&nvn_framebuffers.entries[i];
            if(e->framebuffer==nvn_framebuffers.active && (e->color_id==t->id || e->depth_id==t->id)) {
                glBindFramebuffer(GL_FRAMEBUFFER,e->framebuffer);nvn_direct.bound_direct=0;break;
            }
        }
    if(slot<0)return;
    nvn_direct.stale[slot]=0;nvn_direct.ids[slot]=0;nvn_direct.epochs[slot]=0;
}
static void nvn_direct_queue_forget(struct nvn_queue *q) {
    if(!nvn_direct_enabled())return;
    if(nvn_direct.active && nvn_direct.queue==q)stop("NVN_DIRECT_QUEUE_FINALIZE_PENDING",nvn_caller,q->object,24);
    if(nvn_direct.current_queue==q)nvn_direct.current_queue=NULL;
}
static void nvn_direct_report(unsigned frame) {
    if(nvn_direct.enabled)printf("NVN_DIRECT_PROFILE frame=%u direct=%"PRIu64" canonical=%"PRIu64" clears=%"PRIu64" copybacks=%"PRIu64
        " depth-clears=%"PRIu64" alpha-clears=%"PRIu64" scope=cumulative completion-proof=none\n",frame,
        nvn_direct.direct_frames,nvn_direct.canonical_frames,nvn_direct.clears,nvn_direct.copies,nvn_direct.depth_clears,nvn_direct.alpha_clears);
}
