/* GLES2 depth-only attachments and exact copies of measured uniform depth /
 * stencil images. A draw invalidates the constant proof before it writes;
 * unknown/nonuniform data is never replaced by an invented clear value. */
/* ZERO_UNIFORM_DEPTH_DISCARD=1: a D24S8 image whose depth AND stencil are
 * proven uniform (zero_depth_valid==3) is discarded when its pass is left,
 * so the Mali tiler never writes those tiles back, and is recreated by a real
 * full clear with the proven values before anything can read or partially
 * write it again. Nonuniform or unknown depth is never discarded. Measured
 * need: physical 1012/1015 spend ~40-55 ms/frame of GPU on 12 target
 * switches per frame at 1280x720/1920x1080 (write-back + reload traffic). */
typedef void (GL_APIENTRY *zero_udd_discard_fn)(GLenum,GLsizei,const GLenum *);
static struct {unsigned configured,enabled,resolved;zero_udd_discard_fn discard;EGLContext context;uint64_t discards,restores,skipped,leaves,lazy_copies;} zero_udd;
static unsigned zero_udd_enabled(void) {
    if(!zero_udd.configured) {
        const char *option=getenv("ZERO_UNIFORM_DEPTH_DISCARD");
        if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("ZERO_UNIFORM_DEPTH_DISCARD_OPTION",nvn_caller,0,24);
        zero_udd.enabled=option&&!strcmp(option,"1");zero_udd.configured=1;
        if(zero_udd.enabled)printf("ZERO_UNIFORM_DEPTH_DISCARD enabled=1 eligibility=depth-and-stencil-proven-uniform attachments=depth+stencil recovery=real-full-clear-on-rebind\n");
    }
    return zero_udd.enabled;
}
static unsigned zero_udd_resolve(void) {
    if(!zero_udd.resolved) {
        const char *extensions=(const char *)glGetString(GL_EXTENSIONS),*name="GL_EXT_discard_framebuffer";
        size_t length=strlen(name);unsigned found=0;
        for(const char *p=extensions;p && (p=strstr(p,name));p+=length)
            if((p==extensions||p[-1]==' ')&&(p[length]==' '||!p[length])){found=1;break;}
        if(found)zero_udd.discard=(zero_udd_discard_fn)eglGetProcAddress("glDiscardFramebufferEXT");
        zero_udd.context=nvn_device.context;zero_udd.resolved=1;
        printf("ZERO_UNIFORM_DEPTH_DISCARD_BACKEND extension=%u entrypoint=%u fallback=preserve\n",found,zero_udd.discard!=NULL);
    }
    return zero_udd.discard!=NULL && zero_udd.context==nvn_device.context;
}
/* Called while the pass framebuffer that attaches `depth` is still bound. */
static void zero_udd_leave(struct nvn_texture *depth,GLuint framebuffer) {
    if(!zero_udd_enabled() || !depth || !framebuffer)return;
    struct nvn_texture *st=zero_texture_storage_state(depth);zero_udd.leaves++;
    if(st->zero_depth_discarded || st->zero_depth_valid!=3 || depth->b.format!=0x35 || !st->depth_buffer || !zero_udd_resolve()){zero_udd.skipped++;return;}
    GLint bound=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&bound);
    if(bound<=0 || (GLuint)bound!=framebuffer){zero_udd.skipped++;return;}
    const GLenum attachments[2]={GL_DEPTH_ATTACHMENT,GL_STENCIL_ATTACHMENT};
    zero_udd.discard(GL_FRAMEBUFFER,2,attachments);st->zero_depth_discarded=1;zero_udd.discards++;
    if(zero_udd.discards<=2 || zero_udd.discards%3000==0)
        printf("ZERO_UNIFORM_DEPTH_DISCARD_PROFILE discards=%"PRIu64" restores=%"PRIu64" leaves=%"PRIu64" skipped=%"PRIu64" lazy-copies=%"PRIu64"\n",zero_udd.discards,zero_udd.restores,zero_udd.leaves,zero_udd.skipped,zero_udd.lazy_copies);
}
/* Called with a framebuffer attaching `depth` bound; recreates the proven
 * uniform image with a real full clear and leaves every GL state as found. */
static void zero_udd_restore(struct nvn_texture *depth) {
    if(!depth)return;
    struct nvn_texture *st=zero_texture_storage_state(depth);
    if(!st->zero_depth_discarded)return;
    if(st->zero_depth_valid!=3)stop("ZERO_UNIFORM_DEPTH_DISCARD_PROOF_LOST",nvn_caller,st->id,24);
    GLint front,back,stencil_value;GLfloat depth_value;GLboolean depth_mask;GLboolean scissor=glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_STENCIL_WRITEMASK,&front);glGetIntegerv(GL_STENCIL_BACK_WRITEMASK,&back);
    glGetIntegerv(GL_STENCIL_CLEAR_VALUE,&stencil_value);glGetFloatv(GL_DEPTH_CLEAR_VALUE,&depth_value);glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_mask);
    if(scissor)glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);glStencilMask(255);glClearDepthf(st->zero_depth_value);glClearStencil((GLint)st->zero_stencil_value);
    glClear(GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    glDepthMask(depth_mask);glStencilMaskSeparate(GL_FRONT,(GLuint)front);glStencilMaskSeparate(GL_BACK,(GLuint)back);
    glClearDepthf(depth_value);glClearStencil(stencil_value);if(scissor)glEnable(GL_SCISSOR_TEST);
    st->zero_depth_discarded=0;zero_udd.restores++;
}
static struct nvn_texture zero_no_color;
static struct nvn_texture *zero_depth_only_target(const struct nvn_texture *depth) {
    zero_no_color=(struct nvn_texture){.b={.target=1,.depth=1,.levels=1,.format=0x25,
        .width=depth->b.width,.height=depth->b.height},.live=1,
        .host_scaled=depth->host_scaled,.host_width=depth->host_width,.host_height=depth->host_height};
    return &zero_no_color; /* name=0: no GL color image, no guest texture handle */
}
static unsigned zero_depth_full_rect(const struct nvn_texture *t) {
    if(!glIsEnabled(GL_SCISSOR_TEST))return 1;
    GLint r[4];glGetIntegerv(GL_SCISSOR_BOX,r);
    return r[0]<=0 && r[1]<=0 && (int64_t)r[0]+r[2]>=zero_render_scale_width(t) &&
        (int64_t)r[1]+r[3]>=zero_render_scale_height(t);
}
static void zero_depth_after_clear(struct nvn_texture *t,unsigned depth,float value,unsigned stencil,unsigned mask) {
    if(!t)return;
    t=zero_texture_storage_state(t);
    unsigned full=zero_depth_full_rect(t);mask&=255;stencil&=255;
    if(depth) {
        value=fmaxf(0,fminf(1,value));
        if(full){t->zero_depth_value=value;t->zero_depth_valid|=1;}
        else if(!(t->zero_depth_valid&1) || t->zero_depth_value!=value)t->zero_depth_valid&=~1U;
    }
    if(mask) {
        unsigned next=(t->zero_stencil_value&~mask)|(stencil&mask);
        if(full && (mask==255 || (t->zero_depth_valid&2))){t->zero_stencil_value=next;t->zero_depth_valid|=2;}
        else if(!(t->zero_depth_valid&2) || next!=t->zero_stencil_value)t->zero_depth_valid&=~2U;
    }
}
static void zero_depth_copy(const struct nvn_record *r) {
    const uint64_t *a=r->args;struct nvn_texture *src=nvn_texture_find(a[0]),*dst=nvn_texture_find(a[3]);
    src=zero_texture_storage_state(src);dst=zero_texture_storage_state(dst);
    uint32_t from[6],to[6];memcpy(from,r->payload,24);memcpy(to,r->payload+24,24);
    unsigned at=48;
    if(a[1]){if(!megaman_texture_view_identity(r->payload+at,src->b.levels))stop("ZERO_DEPTH_COPY_SOURCE_VIEW",nvn_caller,a[1],20);at+=MEGAMAN_TEXTURE_VIEW_BYTES;}
    if(a[4] && !megaman_texture_view_identity(r->payload+at,dst->b.levels))stop("ZERO_DEPTH_COPY_TARGET_VIEW",nvn_caller,a[4],20);
    if(src->b.format!=0x35 || dst->b.format!=0x35 || !src->depth_buffer || !dst->depth_buffer ||
       ((src->host_scaled || dst->host_scaled) && !zero_render_scale_enabled()) ||
       src->b.target!=1 || dst->b.target!=1 || src->b.levels!=1 || dst->b.levels!=1 ||
       (a[6]&~0x1eU))stop("ZERO_TEXTURE_COPY_FORMAT_REQUIRED",nvn_caller,src->b.format,20);
    nvn_rt_storage_require(src);nvn_rt_storage_require(dst);
    const struct nvn_texture *textures[2]={src,dst};const uint32_t *boxes[2]={from,to};
    for(unsigned i=0;i<2;i++) {
        const uint32_t *b=boxes[i];const struct nvn_texture *t=textures[i];
        if(b[2] || b[5]!=1 || b[0]>t->b.width || b[3]>t->b.width-b[0] || b[1]>t->b.height || b[4]>t->b.height-b[1])
            stop("ZERO_TEXTURE_COPY_BOX",nvn_caller,t->id,24);
        if(!b[3] || !b[4])return;
    }
    if(src->zero_depth_valid!=3) {
        printf("ZERO_DEPTH_COPY_SOURCE id=%"PRIu64" target=%"PRIu64" valid=%u from=%u,%u,%u,%u to=%u,%u,%u,%u flags=0x%"PRIx64"\n",
            src->id,dst->id,src->zero_depth_valid,from[0],from[1],from[3],from[4],to[0],to[1],to[3],to[4],a[6]);
        stop("ZERO_NONUNIFORM_DEPTH_COPY_REQUIRED",nvn_caller,src->id,20);
    }
    if(zero_udd_enabled() && !to[0] && !to[1] && to[3]==dst->b.width && to[4]==dst->b.height) {
        /* Physical 1019: each CopyTextureToTexture costs ~1.2 ms of CPU plus a
         * render-target switch on the Mali tiler. A full-image copy of a proven
         * uniform depth/stencil only needs the proof moved to the destination;
         * the real GL clear happens when the destination is next bound. */
        dst->zero_depth_value=src->zero_depth_value;dst->zero_stencil_value=src->zero_stencil_value;
        dst->zero_depth_valid=3;dst->zero_depth_discarded=1;zero_udd.lazy_copies++;
        static unsigned lazy_count;if(lazy_count++<2)printf("ZERO_DEPTH_COPY id=%"PRIu64" target=%"PRIu64" source=known-uniform depth=%g stencil=%u operation=deferred-to-next-bind\n",src->id,dst->id,src->zero_depth_value,src->zero_stencil_value);
        return;
    }
    GLint old_fbo,scissor[4],front_mask,back_mask,stencil_value;GLfloat depth_value;GLboolean depth_mask;
    GLboolean scissor_enabled=glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&old_fbo);glGetIntegerv(GL_SCISSOR_BOX,scissor);
    glGetIntegerv(GL_STENCIL_WRITEMASK,&front_mask);glGetIntegerv(GL_STENCIL_BACK_WRITEMASK,&back_mask);
    glGetIntegerv(GL_STENCIL_CLEAR_VALUE,&stencil_value);glGetFloatv(GL_DEPTH_CLEAR_VALUE,&depth_value);glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_mask);
    unsigned cached=zero_depth_framebuffer_enabled();GLuint fbo=0;
    if(cached)fbo=zero_depth_framebuffer_bind(dst,(GLuint)old_fbo);
    else {
        glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,dst->depth_buffer);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_RENDERBUFFER,dst->depth_buffer);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)stop("ZERO_DEPTH_COPY_FRAMEBUFFER",nvn_caller,dst->id,21);
    }
    zero_udd_restore(dst);
    GLint logical_to[4]={(GLint)to[0],(GLint)to[1],(GLint)to[3],(GLint)to[4]},actual_to[4];
    if(!zero_render_scale_rect(dst,logical_to,actual_to))stop("ZERO_DEPTH_COPY_SCALED_RECT",nvn_caller,dst->id,24);
    glEnable(GL_SCISSOR_TEST);glScissor(actual_to[0],actual_to[1],actual_to[2],actual_to[3]);glDepthMask(GL_TRUE);glStencilMask(255);
    glClearDepthf(src->zero_depth_value);glClearStencil(src->zero_stencil_value);glClear(GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    zero_depth_after_clear(dst,1,src->zero_depth_value,src->zero_stencil_value,255);
    glDepthMask(depth_mask);glStencilMaskSeparate(GL_FRONT,front_mask);glStencilMaskSeparate(GL_BACK,back_mask);
    glClearDepthf(depth_value);glClearStencil(stencil_value);glScissor(scissor[0],scissor[1],scissor[2],scissor[3]);
    if(!scissor_enabled)glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER,old_fbo);if(!cached)glDeleteFramebuffers(1,&fbo);
    GLenum error=glGetError();if(error)stop("ZERO_DEPTH_COPY_GL_ERROR",nvn_caller,error,21);
    static unsigned count;if(count++<4)printf("ZERO_DEPTH_COPY id=%"PRIu64" target=%"PRIu64" source=known-uniform depth=%g stencil=%u operation=real-GLES-clear\n",src->id,dst->id,src->zero_depth_value,src->zero_stencil_value);
}
