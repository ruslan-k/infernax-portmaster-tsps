/* Descriptor storage is a host-consumed table. The guest receives the actual
 * 32-byte slot size and the reserved prefix used by this adapter. Registering
 * images/samplers requires a typed resource implementation before success.
 */
#define NVN_DESCRIPTOR_BYTES 32U
#define NVN_DESCRIPTOR_RESERVED 256U
/* Zero/ZX reserves 65536 texture slots plus the 256-entry prefix at boot.
 * These are 32-byte borrowed descriptors, not resident texture images. */
#define NVN_DESCRIPTOR_MAX (65536U+NVN_DESCRIPTOR_RESERVED)
struct nvn_descriptor {uint64_t object,view;uint32_t valid,generation,flags,pad;};
_Static_assert(sizeof(struct nvn_descriptor)==NVN_DESCRIPTOR_BYTES,"descriptor slot");
struct nvn_descriptor_pool {uint64_t object,offset;struct nvn_pool *pool;struct nvn_descriptor *entries;uint32_t count,type;unsigned live;};
static struct nvn_descriptor_pool nvn_descriptor_pools[32];
static unsigned nvn_descriptors_dispatch(unsigned id,struct nvn_frame *f,uint64_t *result) {
    unsigned texture=id==NI_nvnTexturePoolInitialize || id==NI_nvnTexturePoolFinalize || id==NI_nvnTexturePoolGetSize ||
        id==NI_nvnTexturePoolGetMemoryPool || id==NI_nvnTexturePoolGetMemoryOffset || id==NI_nvnTexturePoolSetDebugLabel;
    unsigned sampler=id==NI_nvnSamplerPoolInitialize || id==NI_nvnSamplerPoolFinalize || id==NI_nvnSamplerPoolGetSize ||
        id==NI_nvnSamplerPoolGetMemoryPool || id==NI_nvnSamplerPoolGetMemoryOffset || id==NI_nvnSamplerPoolSetDebugLabel;
    if(!texture && !sampler)return 0;
    uint64_t *a=f->args;*result=0;
    if(id==NI_nvnTexturePoolInitialize || id==NI_nvnSamplerPoolInitialize) {
        struct nvn_pool *pool=nvn_pool_find(a[1]);uint64_t count=a[3],offset=a[2];
        nvn_pointer(a[0],8,PROT_READ|PROT_WRITE);
        if(count<NVN_DESCRIPTOR_RESERVED || count>NVN_DESCRIPTOR_MAX || offset%NVN_DESCRIPTOR_BYTES ||
           offset>pool->size || count*NVN_DESCRIPTOR_BYTES>pool->size-offset)stop("NVN_DESCRIPTOR_POOL_RANGE",nvn_caller,count,24);
        unsigned slot=32;
        for(unsigned i=0;i<32;i++) {
            struct nvn_descriptor_pool *d=&nvn_descriptor_pools[i];
            if(!d->live){if(slot==32)slot=i;continue;}
            if(d->object==a[0] || (d->pool==pool && offset<d->offset+d->count*NVN_DESCRIPTOR_BYTES && d->offset<offset+count*NVN_DESCRIPTOR_BYTES))
                stop("NVN_DESCRIPTOR_POOL_OVERLAP",nvn_caller,offset,24);
        }
        if(slot==32)stop("NVN_DESCRIPTOR_POOL_CAPACITY",nvn_caller,slot,21);
        struct nvn_descriptor *entries=nvn_pointer(pool->address+offset,count*NVN_DESCRIPTOR_BYTES,PROT_READ|PROT_WRITE);
        memset(entries,0,count*NVN_DESCRIPTOR_BYTES);
        nvn_descriptor_pools[slot]=(struct nvn_descriptor_pool){a[0],offset,pool,entries,count,texture?1:2,1};pool->references++;
        *result=1;return 1;
    }
    unsigned slot=0;while(slot<32 && !(nvn_descriptor_pools[slot].live && nvn_descriptor_pools[slot].object==a[0]))slot++;
    if(slot==32 || nvn_descriptor_pools[slot].type!=(texture?1U:2U))stop("NVN_DESCRIPTOR_POOL_INVALID",nvn_caller,a[0],24);
    struct nvn_descriptor_pool *d=&nvn_descriptor_pools[slot];
    if(id==NI_nvnTexturePoolGetSize || id==NI_nvnSamplerPoolGetSize)*result=d->count;
    if(id==NI_nvnTexturePoolGetMemoryPool || id==NI_nvnSamplerPoolGetMemoryPool)*result=d->pool->object;
    if(id==NI_nvnTexturePoolGetMemoryOffset || id==NI_nvnSamplerPoolGetMemoryOffset)*result=d->offset;
    if(id==NI_nvnTexturePoolSetDebugLabel || id==NI_nvnSamplerPoolSetDebugLabel)nvn_debug_label(a[1]);
    if(id==NI_nvnTexturePoolFinalize || id==NI_nvnSamplerPoolFinalize) {
        d->live=0;if(!--d->pool->references)d->pool->retired=0;
    }
    return 1;
}
