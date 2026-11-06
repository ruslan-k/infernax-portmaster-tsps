/* Frente B (27/09/2026): measured page working set and optional idle residency.
 *
 * T2_PAGE_USE_LOG=<frames>  (0/absent = off) prints, every <frames> presents, which catalogue
 *   pages were really sampled in that window, which are resident, the texture budget, and the
 *   system/process memory counters. Pure observation: no GL call, one /proc read per report.
 *
 * T2_PAGE_RESIDENCY=1 (default off) deletes the GL textures (index, map, palette) of a compact/idx8 catalogue page that
 *   has not been sampled for T2_PAGE_IDLE_FRAMES presents (default 900) and only while
 *   MemAvailable is below T2_PAGE_PRESSURE_MIB (default 160; 0 = always). The guest texture object and
 *   the catalogue identity stay (the index plane gets a fresh storage-less GL name); the next real sample re-materializes the page
 *   through the existing verified lazy path (same files, same hashes). At most one page is released
 *   per report so a transition never frees and reloads in a burst. Decision is by measured memory,
 *   never by firmware name. */
#ifndef T2_PAGE_RESIDENCY_H
#define T2_PAGE_RESIDENCY_H
static struct {
    unsigned configured,frame,log_frames,enabled,idle_frames,pressure_mib;
    unsigned last_sample[4096];uint8_t evicted[4096];uint16_t window_used;
    uint64_t evictions,reloads,evicted_bytes;
} t2_residency;
static unsigned t2_residency_env(const char *name,unsigned fallback,unsigned max) {
    const char *v=getenv(name);if(!v)return fallback;
    char *end=NULL;unsigned long n=strtoul(v,&end,10);
    if(!*v || (end && *end) || n>max)stop("T2_PAGE_RESIDENCY_OPTION",nvn_caller,n,24);
    return (unsigned)n;
}
static void t2_residency_configure(void) {
    if(t2_residency.configured)return;
    t2_residency.configured=1;
    t2_residency.log_frames=t2_residency_env("T2_PAGE_USE_LOG",0,100000);
    t2_residency.enabled=t2_residency_env("T2_PAGE_RESIDENCY",0,1);
    t2_residency.idle_frames=t2_residency_env("T2_PAGE_IDLE_FRAMES",900,1000000);
    t2_residency.pressure_mib=t2_residency_env("T2_PAGE_PRESSURE_MIB",160,4096);
    if(t2_residency.enabled && (!t2_page_lazy_enabled() || t2_page_cache_enabled() || t2_residency.idle_frames<60))
        stop("T2_PAGE_RESIDENCY_PREREQUISITE",nvn_caller,0,24);
    if(t2_residency.log_frames || t2_residency.enabled)
        printf("T2_PAGE_RESIDENCY enabled=%u idle-frames=%u pressure-MiB=%u use-log-frames=%u index-name=fresh-on-evict reload=verified-lazy\n",
            t2_residency.enabled,t2_residency.idle_frames,t2_residency.pressure_mib,t2_residency.log_frames);
}
static int t2_residency_page_number(const struct nvn_texture *t) {
    unsigned n=0;
    if(!t->t2_page.name[0] || sscanf(t->t2_page.name,"TexturePack%u",&n)!=1 || n>=16)return -1;
    return (int)n;
}
/* Real sampled bind of a catalogue page (nvn_render.h), before lazy materialization. */
static void t2_residency_touch(struct nvn_texture *t) {
    unsigned frame=t2_residency.frame+1;
    t2_residency_configure();
    if(!t2_residency.log_frames && !t2_residency.enabled)return;
    if(t->t2_page.mode!=T2_PAGE_MODE_IDX8 && t->t2_page.mode!=T2_PAGE_MODE_COMPACT)return;
    size_t i=(size_t)(t-nvn_textures);if(i>=4096)stop("T2_PAGE_RESIDENCY_SLOT",nvn_caller,t->id,24);
    t2_residency.last_sample[i]=frame?frame:1;
    int n=t2_residency_page_number(t);if(n>=0)t2_residency.window_used|=(uint16_t)(1u<<n);
}
/* Replaces the direct lazy call at the sampled bind: times a reload of an evicted page. */
static void t2_residency_materialize(struct nvn_texture *t) {
    size_t i=(size_t)(t-nvn_textures);
    if(i>=4096 || !t2_residency.evicted[i] || !t->t2_page.pending_entry){t2_page_lazy_materialize(t);return;}
    struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);
    t2_page_lazy_materialize(t);
    clock_gettime(CLOCK_MONOTONIC,&b);t2_residency.evicted[i]=0;t2_residency.reloads++;
    printf("T2_PAGE_RESIDENCY_RELOAD page=%s bytes=%zu ms=%.1f reloads=%"PRIu64"\n",t->t2_page.name,t->t2_budget_bytes,
        (double)(b.tv_sec-a.tv_sec)*1e3+(double)(b.tv_nsec-a.tv_nsec)*1e-6,t2_residency.reloads);
}
static void t2_residency_proc(unsigned long *rss,unsigned long *swap) {
    *rss=*swap=0;int fd=open("/proc/self/status",O_RDONLY|O_CLOEXEC);if(fd<0)return;
    char b[4096];ssize_t n=read(fd,b,sizeof b-1);close(fd);if(n<=0)return;b[n]=0;
    const char *p=strstr(b,"VmRSS:");if(p)*rss=strtoul(p+6,NULL,10);
    p=strstr(b,"VmSwap:");if(p)*swap=strtoul(p+7,NULL,10);
}
static unsigned t2_residency_evictable(const struct nvn_texture *t) {
    return t->live && t->name && (t->t2_page.mode==T2_PAGE_MODE_IDX8 || t->t2_page.mode==T2_PAGE_MODE_COMPACT) &&
        !t->t2_page.pending_entry && !t->t2_page.deferred && !t->draws && !t->host_scaled_bound && !t->composition_target_seen &&
        t->t2_page.palette_name && t->t2_page.blockmap_name && t->t2_budget_bytes;
}
static const struct t2_page_entry *t2_residency_entry(const struct nvn_texture *t) {
    for(unsigned i=0;i<t2_pages.manifest.count;i++) {
        const struct t2_page_entry *e=&t2_pages.manifest.entries[i];
        if(e->w==t->b.width && e->h==t->b.height && e->block==t->t2_page.block && e->flipped==t->t2_page.flipped &&
           e->palettes==t->t2_page.palettes && e->entries==t->t2_page.entries && !strcmp(e->name,t->t2_page.name))return e;
    }
    return NULL;
}
/* Context current, completion mutex held (nvn_present.h). */
static void t2_residency_after_present(unsigned frame) {
    t2_residency_configure();t2_residency.frame=frame;
    unsigned period=t2_residency.log_frames?t2_residency.log_frames:60;
    if((!t2_residency.log_frames && !t2_residency.enabled) || frame%period)return;
    unsigned long available=t2_mem_available_kib();
    if(t2_residency.enabled && (!t2_residency.pressure_mib || (available && (available>>10)<t2_residency.pressure_mib))) {
        struct nvn_texture *cold=NULL;unsigned oldest=0;
        for(unsigned i=0;i<nvn_textures_high && i<4096;i++)if(t2_residency_evictable(&nvn_textures[i])) {
            if(!t2_residency.last_sample[i])t2_residency.last_sample[i]=frame;
            unsigned age=frame-t2_residency.last_sample[i];
            if(age>=t2_residency.idle_frames && age>oldest){oldest=age;cold=&nvn_textures[i];}
        }
        if(cold) {
            const struct t2_page_entry *entry=t2_residency_entry(cold);
            if(!entry)stop("T2_PAGE_RESIDENCY_CATALOGUE",nvn_caller,cold->id,24);
            GLint active=0,binding=0;glFinish();GLenum error=glGetError();
            if(error)stop("T2_PAGE_RESIDENCY_FINISH",nvn_caller,error,21);
            glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
            size_t freed=cold->t2_budget_bytes;
            /* MEDIDO 27/09 on Mali-G31 r13p0 (tests/t2_gpu_release_probe.c): redefining the storage as 1x1
             * keeps the old allocation (and it is never returned, even after a later delete); only
             * glDeleteTextures gives the pages back. So the index plane gets a fresh, storage-less name. */
            GLuint old_name=cold->name,fresh=0,aux[3]={old_name,cold->t2_page.blockmap_name,cold->t2_page.palette_name};
            glDeleteTextures(3,aux);glGenTextures(1,&fresh);
            error=glGetError();if(error || !fresh)stop("T2_PAGE_RESIDENCY_RELEASE",nvn_caller,error,21);
            cold->name=fresh;
            glActiveTexture((GLenum)active);
            glBindTexture(GL_TEXTURE_2D,(GLuint)binding==old_name || (GLuint)binding==aux[1] || (GLuint)binding==aux[2]?0:(GLuint)binding);
            cold->t2_page.blockmap_name=cold->t2_page.palette_name=0;
            cold->t2_budget_bytes=0;cold->t2_page.gl_bytes=0;cold->t2_page.pending_entry=entry;cold->parameter_known=0;
            t2_texture_budget_sub(freed,nvn_caller);
            t2_residency.evictions++;t2_residency.evicted_bytes+=freed;t2_residency.evicted[cold-nvn_textures]=1;
            printf("T2_PAGE_RESIDENCY_EVICT page=%s frame=%u idle=%u freed-bytes=%zu available-MiB=%lu evictions=%"PRIu64"\n",
                entry->name,frame,oldest,freed,available>>10,t2_residency.evictions);
        }
    }
    if(!t2_residency.log_frames)return;
    uint16_t resident=0;
    for(unsigned i=0;i<nvn_textures_high && i<4096;i++) {
        const struct nvn_texture *t=&nvn_textures[i];
        if(t->live && t->t2_budget_bytes && (t->t2_page.mode==T2_PAGE_MODE_IDX8 || t->t2_page.mode==T2_PAGE_MODE_COMPACT)) {
            int n=t2_residency_page_number(t);if(n>=0)resident|=(uint16_t)(1u<<n);
        }
    }
    unsigned long rss,swap;t2_residency_proc(&rss,&swap);
    pthread_mutex_lock(&t2_texture_budget_mutex);uint64_t live=t2_texture_budget.live_bytes;pthread_mutex_unlock(&t2_texture_budget_mutex);
    printf("T2_PAGE_USE frame=%u used=0x%03x resident=0x%03x texture-live-KiB=%"PRIu64" available-KiB=%lu rss-KiB=%lu swap-KiB=%lu evictions=%"PRIu64" reloads=%"PRIu64"\n",
        frame,t2_residency.window_used,resident,live>>10,available,rss,swap,t2_residency.evictions,t2_residency.reloads);
    t2_residency.window_used=0;
}
#endif
