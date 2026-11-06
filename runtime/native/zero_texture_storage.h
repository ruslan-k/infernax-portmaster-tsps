/* Zero-only exact-layout aliases for scaled 1280x720 offscreen images.
 * Include after nvn_texture is defined with a zero_storage pointer.
 * Caller holds the existing producer/API mutex. This header performs no GL
 * operation and never fabricates queue completion. Finalize must retain its
 * existing glFinish before release; delete the returned state before unlock.
 * Native texture identity/descriptors belong to each view. Mutable GL image
 * state (parameters, dirty bits, depth proof) belongs to the storage state.
 * Reference: native SDK6.5/main and physical partial trace1007, not gameplay
 * approval. Mixed RGBA1920/depth640 and partial overlaps are not admitted.
 */
#ifndef ZERO_TEXTURE_STORAGE_H
#define ZERO_TEXTURE_STORAGE_H
#define ZERO_TEXTURE_STORAGE_CAPACITY 128U
struct zero_texture_storage {
    struct nvn_texture state;
    uint64_t pool_object,pool_address,pool_size;
    uint32_t pool_flags;
    unsigned active,views;
};
static struct zero_texture_storage zero_texture_storages[ZERO_TEXTURE_STORAGE_CAPACITY];

static unsigned zero_texture_storage_builder(const struct nvn_texture_builder *b,
        uint64_t size,const struct nvn_pool *pool) {
    return b && pool && pool->flags==0xa4 && b->pool==pool->object &&
        b->flags==8 && b->target==1 && b->width==1280 && b->height==720 &&
        b->depth==1 && b->levels==1 && b->samples<=1 &&
        (b->format==0x25 || b->format==0x35) &&
        (b->format==0x35?b->depth_mode<=1:b->depth_mode==0) &&
        !b->packaged && !b->packaged_layout && !b->stride && !b->gl_name &&
        b->swizzle[0]==2 && b->swizzle[1]==3 && b->swizzle[2]==4 && b->swizzle[3]==5 &&
        size==UINT64_C(0x384000) && b->offset%4096==0 && b->offset<=pool->size &&
        size<=pool->size-b->offset;
}
static unsigned zero_texture_storage_candidate(const struct nvn_texture *t) {
    return t && zero_texture_storage_builder(&t->b,t->size,t->pool) &&
        t->host_scaled && t->host_width==640 && t->host_height==360 &&
        !t->levels[0] && !t->host_half && !t->packaged_lazy &&
        !t->host_upload_type && !t->zero_backend_deferred &&
        !t->normal_guarded && !t->normal_unmaterialized;
}
static unsigned zero_texture_storage_layout(const struct nvn_texture_builder *a,
        const struct nvn_texture_builder *b) {
    /* depth_mode selects D24S8 sampling interpretation, not its storage.
     * Everything else relevant to byte addressing/image interpretation must
     * match. Opaque padding and the guest builder address are not identity. */
    return a->device==b->device && a->pool==b->pool && a->offset==b->offset &&
        a->flags==b->flags && a->target==b->target && a->width==b->width &&
        a->height==b->height && a->depth==b->depth && a->levels==b->levels &&
        a->format==b->format && a->samples==b->samples &&
        !memcmp(a->swizzle,b->swizzle,sizeof(a->swizzle)) &&
        a->packaged==b->packaged && a->packaged_layout==b->packaged_layout &&
        a->stride==b->stride && a->gl_name==b->gl_name &&
        (a->format==0x35 || a->depth_mode==b->depth_mode);
}
static void zero_texture_storage_require(const struct nvn_texture *t) {
    const struct zero_texture_storage *s=t?t->zero_storage:NULL;
    if(!s)return;
    const struct nvn_pool *p=t->pool;
    if(!s->active || !s->views || !p || p!=s->state.pool ||
       p->object!=s->pool_object || p->address!=s->pool_address ||
       p->size!=s->pool_size || p->flags!=s->pool_flags ||
       !(p->live || p->retired) || !zero_texture_storage_candidate(t) ||
       t->size!=s->state.size || !zero_texture_storage_layout(&t->b,&s->state.b) ||
       t->name!=s->state.name || t->depth_buffer!=s->state.depth_buffer)
        stop("ZERO_STORAGE_OWNER_STALE",nvn_caller,t?t->id:0,24);
}
static struct nvn_texture *zero_texture_storage_state(struct nvn_texture *t) {
    zero_texture_storage_require(t);
    return t && t->zero_storage?&t->zero_storage->state:t;
}
static unsigned zero_texture_storage_same(const struct nvn_texture *a,const struct nvn_texture *b) {
    if(!a || !b)return 0;
    zero_texture_storage_require(a);zero_texture_storage_require(b);
    return a==b || (a->zero_storage && a->zero_storage==b->zero_storage);
}
static struct {
    struct zero_texture_storage *owner;
    uint64_t id;
    unsigned unit,valid;
    GLint values[4];
} zero_texture_storage_samplers[4];
static void zero_texture_storage_sampler_begin(void) {
    memset(zero_texture_storage_samplers,0,sizeof(zero_texture_storage_samplers));
}
static void zero_texture_storage_sampler_require(struct nvn_texture *t,unsigned unit,
        GLint min,GLint mag,GLint wrap_s,GLint wrap_t) {
    zero_texture_storage_require(t);
    struct zero_texture_storage *owner=t?t->zero_storage:NULL;
    const GLint values[4]={min,mag,wrap_s,wrap_t};unsigned slot=4;
    for(unsigned i=0;i<4;i++) {
        if(zero_texture_storage_samplers[i].valid && zero_texture_storage_samplers[i].unit==unit) {
            zero_texture_storage_samplers[i].valid=0;slot=i;
        }
    }
    if(!owner)return;
    for(unsigned i=0;i<4;i++) {
        if(!zero_texture_storage_samplers[i].valid){if(slot==4)slot=i;continue;}
        if(zero_texture_storage_samplers[i].owner!=owner)continue;
        if(zero_texture_storage_samplers[i].id!=owner->state.id ||
           memcmp(zero_texture_storage_samplers[i].values,values,sizeof(values))) {
            printf("ZERO_STORAGE_SAMPLER_CONFLICT id=%"PRIu64" owner=%"PRIu64" units=%u/%u\n",
                   t->id,owner->state.id,zero_texture_storage_samplers[i].unit,unit);
            stop("ZERO_STORAGE_SAMPLER_STATE_REQUIRED",nvn_caller,t->id,20);
        }
    }
    if(slot==4)stop("ZERO_STORAGE_SAMPLER_CAPACITY",nvn_caller,unit,24);
    zero_texture_storage_samplers[slot].owner=owner;
    zero_texture_storage_samplers[slot].id=owner->state.id;
    zero_texture_storage_samplers[slot].unit=unit;
    memcpy(zero_texture_storage_samplers[slot].values,values,sizeof(values));
    zero_texture_storage_samplers[slot].valid=1;
}
static unsigned zero_texture_storage_compatible(const struct nvn_texture_builder *b,
        uint64_t size,const struct nvn_pool *pool,const struct nvn_texture *old) {
    if(!old || !old->zero_storage || !zero_texture_storage_builder(b,size,pool))return 0;
    zero_texture_storage_require(old);
    return old->pool==pool && old->size==size && zero_texture_storage_layout(b,&old->b);
}
static void zero_texture_storage_reject(const char *reason,const struct nvn_texture *t,
        const struct nvn_texture *other) {
    printf("ZERO_STORAGE_ALIAS_REJECT reason=%s object=0x%"PRIx64" id=%"PRIu64
           " format=0x%x size=%ux%u offset=0x%"PRIx64" bytes=0x%"PRIx64
           " other-object=0x%"PRIx64" other-id=%"PRIu64"\n",reason,
           t?t->object:0,t?t->id:0,t?t->b.format:0,t?t->b.width:0,t?t->b.height:0,
           t?t->b.offset:0,t?t->size:0,other?other->object:0,other?other->id:0);
    stop("ZERO_STORAGE_ALIAS_UNQUALIFIED",nvn_caller,t?t->id:0,24);
}
static unsigned zero_texture_storage_join(struct nvn_texture *t) {
    if(t->zero_storage || t->name || t->depth_buffer)
        stop("ZERO_STORAGE_JOIN_ORDER",nvn_caller,t->id,24);
    if(!zero_texture_storage_candidate(t))return 0;
    struct zero_texture_storage *found=NULL;
    for(unsigned i=0;i<4096;i++) {
        const struct nvn_texture *old=&nvn_textures[i];
        if(old==t || !old->live || old->pool!=t->pool ||
           t->b.offset>=old->b.offset+old->size || old->b.offset>=t->b.offset+t->size)continue;
        if(!zero_texture_storage_compatible(&t->b,t->size,t->pool,old))
            zero_texture_storage_reject("range-layout-or-owner",t,old);
        if(found && found!=old->zero_storage)zero_texture_storage_reject("multiple-owners",t,old);
        found=old->zero_storage;
    }
    if(!found)return 0;
    if(found->views==UINT32_MAX)stop("ZERO_STORAGE_REFERENCE_OVERFLOW",nvn_caller,t->id,24);
    /* No initialization/upload/cache reset here: a view created after a GPU
     * write must observe those current pixels, not the old native CPU bytes. */
    t->zero_storage=found;t->name=found->state.name;t->depth_buffer=found->state.depth_buffer;
    found->views++;
    printf("ZERO_STORAGE_ALIAS_JOIN id=%"PRIu64" owner=%"PRIu64" views=%u name=%u depth=%u contents=retained\n",
           t->id,found->state.id,found->views,t->name,t->depth_buffer);
    return 1;
}
static void zero_texture_storage_publish(struct nvn_texture *t) {
    if(t->zero_storage){zero_texture_storage_require(t);return;}
    if(!zero_texture_storage_candidate(t))return;
    if(!t->id || (t->b.format==0x25?(!t->name || t->depth_buffer):(t->name || !t->depth_buffer)))
        stop("ZERO_STORAGE_PUBLISH_GL",nvn_caller,t->id,24);
    for(unsigned i=0;i<4096;i++) {
        const struct nvn_texture *old=&nvn_textures[i];
        if(old!=t && old->live && old->pool==t->pool &&
           t->b.offset<old->b.offset+old->size && old->b.offset<t->b.offset+t->size)
            zero_texture_storage_reject("publish-without-join",t,old);
    }
    unsigned i=0;while(i<ZERO_TEXTURE_STORAGE_CAPACITY && zero_texture_storages[i].active)i++;
    if(i==ZERO_TEXTURE_STORAGE_CAPACITY)stop("ZERO_STORAGE_CAPACITY",nvn_caller,i,21);
    struct zero_texture_storage *s=&zero_texture_storages[i];
    *s=(struct zero_texture_storage){.state=*t,.pool_object=t->pool->object,
        .pool_address=t->pool->address,.pool_size=t->pool->size,.pool_flags=t->pool->flags,.active=1,.views=1};
    t->zero_storage=s;s->state.zero_storage=s;s->state.live=1;
    printf("ZERO_STORAGE_OWNER id=%"PRIu64" logical=1280x720 actual=640x360 format=0x%x name=%u depth=%u\n",
           t->id,t->b.format,t->name,t->depth_buffer);
}
/* Obtain state(t) BEFORE release. The returned state remains stable until the
 * next publish, so the caller can delete its GL resources before unlocking.
 * Do not free per-owner CPU snapshots/masks for a non-final view. */
static unsigned zero_texture_storage_release(struct nvn_texture *t) {
    if(!t->zero_storage)return 1;
    zero_texture_storage_require(t);
    struct zero_texture_storage *s=t->zero_storage;
    if(t==&s->state)stop("ZERO_STORAGE_RELEASE_STATE_NOT_VIEW",nvn_caller,t->id,24);
    t->zero_storage=NULL;
    if(--s->views)return 0;
    s->active=0; /* state intentionally retained for the immediate GL delete */
    return 1;
}
#endif
