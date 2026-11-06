/* GLES2 has texture sampling state rather than separate sampler objects.
 * Preserve the exact logical sampler here; replay must apply/translate it.
 */
struct nvn_sampler_builder {
    uint64_t device;uint32_t min,mag,wrap[3];float lod_min,lod_max,lod_bias;
    uint32_t compare,compare_func,border[4];float anisotropy;uint32_t reduction;float lod_snap;
};
_Static_assert(offsetof(struct nvn_sampler_builder,anisotropy)==0x40,"observed sampler builder");
/* Animal Well SDK 17.5.3 sampler builder (measured from the game's setter calls): device +0, lodMin +8, lodMax +0xc,
 * lodBias +0x10, anisotropy +0x14, lodSnap +0x18, border[4] +0x1c, packed +0x2c = wrapS|wrapT<<4|wrapR<<8|min<<16|mag<<20|
 * compareEnable<<24|compareFunc<<26|reduction<<30. */
static void aw_sampler_builder_translate(const uint8_t *raw,struct nvn_sampler_builder *b) {
    uint32_t packed;memcpy(&packed,raw+0x2c,4);
    memset(b,0,sizeof(*b));memcpy(&b->device,raw,8);
    memcpy(&b->lod_min,raw+8,4);memcpy(&b->lod_max,raw+0xc,4);memcpy(&b->lod_bias,raw+0x10,4);
    memcpy(&b->anisotropy,raw+0x14,4);memcpy(&b->lod_snap,raw+0x18,4);memcpy(b->border,raw+0x1c,16);
    b->wrap[0]=packed&15;b->wrap[1]=(packed>>4)&15;b->wrap[2]=(packed>>8)&15;b->min=(packed>>16)&15;b->mag=(packed>>20)&15;
    b->compare=(packed>>24)&1;b->compare_func=(packed>>26)&15;b->reduction=(packed>>30)&3;
}
struct nvn_sampler {uint64_t object,id;struct nvn_sampler_builder b;unsigned live,references;};
static struct nvn_sampler nvn_samplers[4096];
static struct nvn_sampler *nvn_sampler_find(uint64_t object) {
    for(unsigned i=0;i<4096;i++)if(nvn_samplers[i].live&&nvn_samplers[i].object==object)return &nvn_samplers[i];
    stop("NVN_SAMPLER_INVALID",nvn_caller,object,24);return NULL;
}
static unsigned nvn_sampler_dispatch(unsigned id,struct nvn_frame *f,uint64_t *result) {
    uint64_t *a=f->args;*result=0;
    switch(id) {
    case NI_nvnSamplerBuilderSetDevice:case NI_nvnSamplerBuilderSetDefaults:case NI_nvnSamplerBuilderSetMinMagFilter:
    case NI_nvnSamplerBuilderSetWrapMode:case NI_nvnSamplerBuilderSetLodClamp:case NI_nvnSamplerBuilderSetLodBias:
    case NI_nvnSamplerBuilderSetCompare:case NI_nvnSamplerBuilderSetMaxAnisotropy:case NI_nvnSamplerBuilderSetReductionFilter:
    case NI_nvnSamplerBuilderSetLodSnap:
        nvn_pointer(a[0],sizeof(struct nvn_sampler_builder),PROT_READ|PROT_WRITE);*result=nvn_original_helper(id,f);return 1;
    case NI_nvnSamplerBuilderSetBorderColor:case NI_nvnSamplerBuilderSetBorderColori:case NI_nvnSamplerBuilderSetBorderColorui:
        nvn_pointer(a[0],sizeof(struct nvn_sampler_builder),PROT_READ|PROT_WRITE);nvn_pointer(a[1],16,PROT_READ);
        *result=nvn_original_helper(id,f);return 1;
    default:break;
    }
    if(id==NI_nvnSamplerInitialize) {
        nvn_pointer(a[0],8,PROT_READ|PROT_WRITE);struct nvn_sampler_builder b;
        aw_sampler_builder_translate(nvn_pointer(a[1],sizeof(b),PROT_READ),&b);
        if(!nvn_device.live||b.device!=nvn_device.address)stop("NVN_SAMPLER_DEVICE",nvn_caller,b.device,24);
        printf("AW_SAMPLER min=%u mag=%u wrap=%u/%u/%u lod=%g..%g bias=%g compare=%u/%u reduction=%u\n",b.min,b.mag,b.wrap[0],b.wrap[1],b.wrap[2],b.lod_min,b.lod_max,b.lod_bias,b.compare,b.compare_func,b.reduction);
        if(b.min>5 || b.mag>1 || b.wrap[0]>7 || b.wrap[1]>7 || b.wrap[2]>7 || b.compare>1 || b.reduction>2 ||
           !isfinite(b.lod_min)||!isfinite(b.lod_max)||!isfinite(b.lod_bias)||!isfinite(b.anisotropy)||!isfinite(b.lod_snap)||b.anisotropy<1)
            stop("NVN_SAMPLER_PARAMETERS",nvn_caller,b.min,24);
        unsigned slot=4096;
        for(unsigned i=0;i<4096;i++) {
            if(nvn_samplers[i].live&&nvn_samplers[i].object==a[0])stop("NVN_SAMPLER_ALREADY_LIVE",nvn_caller,a[0],24);
            if(!nvn_samplers[i].live&&!nvn_samplers[i].references&&slot==4096)slot=i;
        }
        if(slot==4096)stop("NVN_SAMPLER_CAPACITY",nvn_caller,slot,21);
        nvn_samplers[slot]=(struct nvn_sampler){.object=a[0],.id=zero_next_object_id(),.b=b,.live=1};
        printf("NVN_SAMPLER_CREATED min=%u mag=%u wrap=%u/%u/%u lod=%g:%g bias=%g anisotropy=%g compare=%u reduction=%u\n",
            b.min,b.mag,b.wrap[0],b.wrap[1],b.wrap[2],b.lod_min,b.lod_max,b.lod_bias,b.anisotropy,b.compare,b.reduction);
        *result=1;return 1;
    }
    if(id==NI_nvnSamplerPoolRegisterSampler) {
        struct nvn_descriptor_pool *d=NULL;
        for(unsigned i=0;i<32;i++)if(nvn_descriptor_pools[i].live&&nvn_descriptor_pools[i].object==a[0]&&nvn_descriptor_pools[i].type==2)d=&nvn_descriptor_pools[i];
        if(!d || a[1]<NVN_DESCRIPTOR_RESERVED || a[1]>=d->count)stop("NVN_SAMPLER_DESCRIPTOR_INDEX",nvn_caller,a[1],24);
        struct nvn_sampler *s=nvn_sampler_find(a[2]);struct nvn_descriptor *slot=&d->entries[a[1]];
        if(slot->valid) {
            unsigned previous=0;while(previous<4096&&nvn_samplers[previous].id!=slot->object)previous++;
            if(previous==4096 || !nvn_samplers[previous].references)stop("NVN_SAMPLER_DESCRIPTOR_STALE",nvn_caller,slot->object,24);
            nvn_samplers[previous].references--;
        }
        unsigned generation=slot->generation+1;if(!generation)stop("NVN_SAMPLER_GENERATION_WRAP",nvn_caller,a[1],24);
        *slot=(struct nvn_descriptor){.object=s->id,.valid=1,.generation=generation};s->references++;return 1;
    }
    if(id==NI_nvnSamplerSetDebugLabel || id==NI_nvnSamplerGetDebugID || id==NI_nvnSamplerFinalize) {
        struct nvn_sampler *s=nvn_sampler_find(a[0]);
        if(id==NI_nvnSamplerSetDebugLabel)nvn_debug_label(a[1]);
        if(id==NI_nvnSamplerGetDebugID)*result=s->id;
        if(id==NI_nvnSamplerFinalize)s->live=0;
        return 1;
    }
    return 0;
}
