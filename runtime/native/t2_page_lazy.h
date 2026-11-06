/* Port-local catalogue residency. Defaults retain every page after first use.
 * The separately opted-in DarkOS cache may release only a cold index plane. */
#ifndef T2_PAGE_LAZY_H
#define T2_PAGE_LAZY_H
static unsigned t2_page_lazy_enabled(void) {
    static int configured;static unsigned enabled;
    if(!configured) {
        const char *option=getenv("T2_PAGE_LAZY_UPLOAD");
        if(option && strcmp(option,"0") && strcmp(option,"1"))stop("T2_PAGE_LAZY_OPTION",nvn_caller,0,24);
        enabled=option&&!strcmp(option,"1");configured=1;
        if(enabled)printf("T2_PAGE_LAZY enabled=1 mode=idx8 default-eviction=never resolution=unchanged\n");
    }
    return enabled;
}
static void t2_page_lazy_materialize(struct nvn_texture *t) {
    const struct t2_page_entry *e=t->t2_page.pending_entry;
    if(!e)return;
    unsigned retained=t->t2_page.palette_name && t->t2_page.blockmap_name;
    size_t before=t->t2_budget_bytes;
    size_t auxiliary=e->block?(size_t)(e->w/e->block)*(e->h/e->block)+T2_IDX16_PALETTE_BYTES+1:0;
    if(!t->live || !t->name || (t->t2_page.mode!=T2_PAGE_MODE_IDX8 && t->t2_page.mode!=T2_PAGE_MODE_COMPACT) ||
       (retained && t->t2_page.mode==T2_PAGE_MODE_COMPACT) ||
       t->t2_page.deferred || (retained?before!=auxiliary:(before || t->t2_page.palette_name || t->t2_page.blockmap_name)) || e->w!=t->b.width || e->h!=t->b.height ||
       strcmp(e->name,t->t2_page.name))stop("T2_PAGE_LAZY_STATE",nvn_caller,t->id,24);
    GLint active,binding,alignment;size_t bytes=0;GLuint names[2]={0,0};
    glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
    glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    if(retained){names[0]=t->t2_page.blockmap_name;names[1]=t->t2_page.palette_name;}
    else glGenTextures(2,names);
    unsigned ok=names[0] && names[1] && (t->t2_page.mode==T2_PAGE_MODE_COMPACT?
        t2_compact_upload(t,e,names,&bytes):t2_page_upload_stream(t,e,T2_PAGE_MODE_IDX8,names,&bytes));
    glActiveTexture((GLenum)active);glBindTexture(GL_TEXTURE_2D,(GLuint)binding);
    glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);
    GLenum error=glGetError();
    if(!ok || error || bytes<before){if(!retained)glDeleteTextures(2,names);stop("T2_PAGE_LAZY_UPLOAD",nvn_caller,error,21);}
    t->parameter_known=0;t2_page_wrap_reset(&t->t2_page.blockmap_wrap);
    t->t2_page.gl_bytes=bytes;t->t2_budget_bytes=bytes;
    t->t2_page.pending_entry=NULL;t2_texture_budget_add(bytes-before,nvn_caller);
    printf("T2_PAGE_LAZY_RESIDENT page=%s id=%"PRIu64" bytes=%zu reload=%u\n",e->name,t->id,bytes,retained);
}
#endif
