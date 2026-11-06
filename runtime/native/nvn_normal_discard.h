/* Optional producer-context hint for pixels already made unobservable by the
 * normal-pass experiment. Keep taint/CPU guards and all real clears unchanged.
 * The old attachment must be identified before any destination bind/reattach. */
typedef void (GL_APIENTRY *nvn_discard_framebuffer_fn)(GLenum,GLsizei,const GLenum *);
static struct {
    unsigned configured,enabled,resolved,available,after_frame;
    EGLContext context;nvn_discard_framebuffer_fn discard;
    uint64_t eligible,discarded,identity_skipped,binding_skipped,unavailable;
} nvn_normal_discard_state;
static unsigned nvn_normal_discard_enabled(void) {
    if(!nvn_normal_discard_state.configured) {
        const char *option=getenv("COTM_NORMAL_DISCARD");
        if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("NVN_NORMAL_DISCARD_OPTION",nvn_caller,0,24);
        nvn_normal_discard_state.enabled=option&&!strcmp(option,"1");nvn_normal_discard_state.configured=1;
        if(nvn_normal_discard_state.enabled)printf("NVN_NORMAL_DISCARD_EXPERIMENT enabled=1 attachment=color-only guards=normal-unmaterialized binding=queried\n");
    }
    return nvn_normal_discard_state.enabled;
}
static unsigned nvn_normal_discard_resolve(void) {
    if(!nvn_normal_discard_state.resolved) {
        const char *extensions=(const char *)glGetString(GL_EXTENSIONS),*name="GL_EXT_discard_framebuffer";
        size_t length=strlen(name);unsigned found=0;
        for(const char *p=extensions;p && (p=strstr(p,name));p+=length)
            if((p==extensions||p[-1]==' ')&&(p[length]==' '||!p[length])){found=1;break;}
        nvn_normal_discard_state.context=nvn_device.context;
        if(found)nvn_normal_discard_state.discard=(nvn_discard_framebuffer_fn)eglGetProcAddress("glDiscardFramebufferEXT");
        nvn_normal_discard_state.available=nvn_normal_discard_state.discard!=NULL;nvn_normal_discard_state.resolved=1;
        printf("NVN_NORMAL_DISCARD_BACKEND context=producer extension=%u entrypoint=%u fallback=preserve\n",found,nvn_normal_discard_state.available);
    }
    return nvn_normal_discard_state.available && nvn_normal_discard_state.context==nvn_device.context;
}
static void nvn_normal_discard_target(const struct nvn_texture *old,GLuint framebuffer,const struct nvn_texture *next) {
    if(!nvn_normal_discard_enabled() || !nvn_normal.enabled || !nvn_low.enabled || !old || !next || old==next ||
       !old->live || !old->name || old->name==next->name || !old->normal_unmaterialized || !old->normal_guarded ||
       old->depth_buffer || old->b.width!=512 || old->b.height!=512 || old->b.depth!=1 || old->b.levels!=1 ||
       old->b.target!=1 || old->b.samples>1 || (old->b.format!=0x25&&old->b.format!=0x9a) || old->size!=512U*512U*4U ||
       !old->pool || !old->pool->live || (old->pool->flags&7)!=1 || nvn_low_color(old)>=0)return;
    nvn_normal_discard_state.eligible++;
    unsigned identified=0;
    if(framebuffer && nvn_framebuffers.active==framebuffer && !nvn_framebuffers.retired &&
       nvn_framebuffers.context==nvn_device.context && nvn_device.context!=EGL_NO_CONTEXT)
        for(unsigned i=0;i<NVN_FRAMEBUFFER_CACHE_SIZE;i++) {
            const struct nvn_framebuffer_entry *entry=&nvn_framebuffers.entries[i];
            if(entry->framebuffer==framebuffer && entry->color_id==old->id && entry->color==old->name){identified=1;break;}
        }
    if(!identified){nvn_normal_discard_state.identity_skipped++;return;}
    if(!nvn_normal_discard_resolve()){nvn_normal_discard_state.unavailable++;return;}
    GLint bound=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&bound);
    if(bound<=0 || (GLuint)bound!=framebuffer){nvn_normal_discard_state.binding_skipped++;return;}
    const GLenum attachment=GL_COLOR_ATTACHMENT0;
    nvn_normal_discard_state.discard(GL_FRAMEBUFFER,1,&attachment);nvn_normal_discard_state.discarded++;
    /* No taint removal, CPU unprotection, GL error consumption or completion
     * assertion here. Replay retains its existing error/completion checks. */
}
static void nvn_normal_discard_report(unsigned frame) {
    if(!nvn_normal_discard_state.enabled || frame<nvn_normal_discard_state.after_frame || frame-nvn_normal_discard_state.after_frame<60)return;
    printf("NVN_NORMAL_DISCARD_PROFILE after-frame=%u frames=%u eligible=%"PRIu64" discarded=%"PRIu64" identity-skipped=%"PRIu64
           " binding-skipped=%"PRIu64" unavailable=%"PRIu64" hinted-bytes=%"PRIu64" attachment=color-only scope=hint-calls-not-measured-bandwidth\n",
           frame,frame-nvn_normal_discard_state.after_frame,nvn_normal_discard_state.eligible,nvn_normal_discard_state.discarded,
           nvn_normal_discard_state.identity_skipped,nvn_normal_discard_state.binding_skipped,nvn_normal_discard_state.unavailable,
           nvn_normal_discard_state.discarded*UINT64_C(512)*512*4);
    nvn_normal_discard_state.after_frame=frame;nvn_normal_discard_state.eligible=nvn_normal_discard_state.discarded=0;
    nvn_normal_discard_state.identity_skipped=nvn_normal_discard_state.binding_skipped=nvn_normal_discard_state.unavailable=0;
}
