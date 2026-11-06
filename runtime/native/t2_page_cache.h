/* DarkOS opt-in: retain eight index planes; never evict an atlas sampled in
 * the preceding 120 real frames. Auxiliary textures and all GL names survive. */
#ifndef T2_PAGE_CACHE_H
#define T2_PAGE_CACHE_H
static struct {uint64_t id;unsigned frame;} t2_page_cache_seen[4096];
static unsigned t2_page_cache_enabled(void) {
    static int configured;static unsigned enabled;
    if(!configured) {
        const char *o=getenv("T2_PAGE_CACHE");
        if(o && strcmp(o,"0") && strcmp(o,"8"))stop("T2_PAGE_CACHE_OPTION",nvn_caller,0,24);
        enabled=o&&!strcmp(o,"8");configured=1;
        if(enabled && (!t2_page_lazy_enabled() || !nvn_single_enabled()))stop("T2_PAGE_CACHE_PREREQUISITE",nvn_caller,0,24);
        if(enabled)printf("T2_PAGE_CACHE enabled=1 capacity=8 idle-frames=120 names=preserved auxiliary=retained\n");
    }
    return enabled;
}
static void t2_page_cache_touch(struct nvn_texture *t,unsigned frame) {
    if(t->t2_page.mode!=T2_PAGE_MODE_IDX8 || !t2_page_cache_enabled())return;
    size_t i=(size_t)(t-nvn_textures);
    if(i>=nvn_textures_high || i>=4096)stop("T2_PAGE_CACHE_SLOT",nvn_caller,t->id,24);
    t2_page_cache_seen[i].id=t->id;t2_page_cache_seen[i].frame=frame;
}
static unsigned t2_page_cache_resident(const struct nvn_texture *t) {
    return t->live && t->name && t->t2_page.mode==T2_PAGE_MODE_IDX8 && !t->t2_page.pending_entry &&
        !t->t2_page.deferred && !t->draws && !t->host_scaled_bound && !t->composition_target_seen &&
        t->t2_page.palette_name && t->t2_page.blockmap_name && t->t2_budget_bytes>(size_t)t->b.width*t->b.height;
}
static void t2_page_cache_after_present(unsigned frame) {
    if(!t2_page_cache_enabled() || frame%30)return;
    unsigned resident=0;
    for(unsigned i=0;i<nvn_textures_high;i++)if(t2_page_cache_resident(&nvn_textures[i])) {
        resident++;
        if(t2_page_cache_seen[i].id!=nvn_textures[i].id) {
            t2_page_cache_seen[i].id=nvn_textures[i].id;t2_page_cache_seen[i].frame=frame;
        }
    }
    unsigned finished=0;GLint active=0,binding=0;
    while(resident>8) {
        struct nvn_texture *cold=NULL;unsigned oldest=119;
        for(unsigned i=0;i<nvn_textures_high;i++)if(t2_page_cache_resident(&nvn_textures[i])) {
            unsigned age=frame-t2_page_cache_seen[i].frame;
            if(age>oldest){oldest=age;cold=&nvn_textures[i];}
        }
        if(!cold)break; /* More than eight recently used atlases: no forced eviction. */
        const struct t2_page_entry *entry=NULL;
        for(unsigned i=0;i<t2_pages.manifest.count;i++) {
            const struct t2_page_entry *e=&t2_pages.manifest.entries[i];
            if(e->w==cold->b.width && e->h==cold->b.height && e->block==cold->t2_page.block && e->flipped==cold->t2_page.flipped &&
               e->palettes==cold->t2_page.palettes && e->entries==cold->t2_page.entries && !strcmp(e->name,cold->t2_page.name)){entry=e;break;}
        }
        if(!entry)stop("T2_PAGE_CACHE_CATALOGUE",nvn_caller,cold->id,24);
        if(!finished) {
            glFinish();GLenum error=glGetError();if(error)stop("T2_PAGE_CACHE_FINISH",nvn_caller,error,21);
            glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);finished=1;
        }
        glBindTexture(GL_TEXTURE_2D,cold->name);
        glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE,1,1,0,GL_LUMINANCE,GL_UNSIGNED_BYTE,NULL);
        GLenum error=glGetError();if(error)stop("T2_PAGE_CACHE_RELEASE",nvn_caller,error,21);
        size_t freed=(size_t)entry->w*entry->h-1;
        cold->t2_budget_bytes-=freed;cold->t2_page.gl_bytes-=freed;
        cold->t2_page.pending_entry=entry;cold->parameter_known=0;
        t2_texture_budget_sub(freed,nvn_caller);resident--;
        printf("T2_PAGE_CACHE_EVICT page=%s frame=%u idle=%u freed-bytes=%zu resident=%u\n",entry->name,frame,oldest,freed,resident);
    }
    if(finished){glActiveTexture((GLenum)active);glBindTexture(GL_TEXTURE_2D,(GLuint)binding);}
}
#endif
