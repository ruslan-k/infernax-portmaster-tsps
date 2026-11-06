/* A hint stores a resource index, never a copied resource/descriptor. Every
 * hit rechecks current live/id; the descriptor id/generation is freshly read.
 * Misses use the complete original bounded scan and retain its guards below. */
#define NVN_RESOURCE_LOOKUP_SLOTS 256U
struct nvn_resource_hint {uint64_t id;uint32_t generation,index;unsigned valid;};
static struct {
    unsigned enabled,after_frame;
    struct nvn_resource_hint textures[NVN_RESOURCE_LOOKUP_SLOTS],samplers[NVN_RESOURCE_LOOKUP_SLOTS];
    uint64_t hits[2],misses[2],scan_steps;
} nvn_resource_lookup;
static pthread_once_t nvn_resource_lookup_once=PTHREAD_ONCE_INIT;
static void nvn_resource_lookup_configure(void) {
    const char *option=getenv("COTM_RESOURCE_LOOKUP_CACHE");
    if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("NVN_RESOURCE_LOOKUP_OPTION",nvn_caller,0,24);
    nvn_resource_lookup.enabled=option&&!strcmp(option,"1");
}
static void nvn_render_resources(const struct nvn_descriptor *td,const struct nvn_descriptor *sd,
                                 struct nvn_texture **texture,struct nvn_sampler **sampler) {
    pthread_once(&nvn_resource_lookup_once,nvn_resource_lookup_configure);
    uint64_t tid=td->object,sid=sd->object;unsigned tg=td->generation,sg=sd->generation;
    *texture=NULL;*sampler=NULL;
    struct nvn_resource_hint *th=&nvn_resource_lookup.textures[tid&(NVN_RESOURCE_LOOKUP_SLOTS-1)],
                             *sh=&nvn_resource_lookup.samplers[sid&(NVN_RESOURCE_LOOKUP_SLOTS-1)];
    if(nvn_resource_lookup.enabled) {
        if(th->valid && th->id==tid && th->generation==tg && th->index<4096 &&
           nvn_textures[th->index].live && nvn_textures[th->index].id==tid) {
            *texture=&nvn_textures[th->index];nvn_resource_lookup.hits[0]++;
        } else nvn_resource_lookup.misses[0]++;
        if(sh->valid && sh->id==sid && sh->generation==sg && sh->index<4096 &&
           nvn_samplers[sh->index].live && nvn_samplers[sh->index].id==sid) {
            *sampler=&nvn_samplers[sh->index];nvn_resource_lookup.hits[1]++;
        } else nvn_resource_lookup.misses[1]++;
    }
    for(unsigned i=0;i<4096 && (!*texture||!*sampler);i++) {
        nvn_resource_lookup.scan_steps++;
        if(!*texture && nvn_textures[i].id==tid && nvn_textures[i].live) {
            *texture=&nvn_textures[i];
            if(nvn_resource_lookup.enabled)*th=(struct nvn_resource_hint){tid,tg,i,1};
        }
        if(!*sampler && nvn_samplers[i].id==sid && nvn_samplers[i].live) {
            *sampler=&nvn_samplers[i];
            if(nvn_resource_lookup.enabled)*sh=(struct nvn_resource_hint){sid,sg,i,1};
        }
    }
}
static void nvn_resource_lookup_report(unsigned frame) {
    if(!nvn_resource_lookup.enabled || frame<nvn_resource_lookup.after_frame || frame-nvn_resource_lookup.after_frame<60)return;
    printf("NVN_RESOURCE_LOOKUP_PROFILE after-frame=%u texture-hits=%"PRIu64" texture-misses=%"PRIu64
           " sampler-hits=%"PRIu64" sampler-misses=%"PRIu64" scan-steps=%"PRIu64
           " scope=cumulative key=id-generation hit=current-live-id fallback=full-scan\n",frame,
           nvn_resource_lookup.hits[0],nvn_resource_lookup.misses[0],nvn_resource_lookup.hits[1],nvn_resource_lookup.misses[1],nvn_resource_lookup.scan_steps);
    nvn_resource_lookup.after_frame=frame;
}
