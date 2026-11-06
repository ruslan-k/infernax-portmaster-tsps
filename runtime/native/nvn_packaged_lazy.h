/* LC2 XTX assets: retain native pool bytes and object identities, and create
 * GLES image storage only when an actual sampler needs them. No extra snapshot.
 * The admitted pool is CPU_NO_ACCESS/GPU_CACHED (0x21), matching main37f000's
 * allocator/copy-before-TextureInitialize path. Arbitrary later writes through
 * private allocator aliases are not an advertised coherent CPU-map operation.
 */
#ifndef NVN_PACKAGED_LAZY_H
#define NVN_PACKAGED_LAZY_H
static unsigned nvn_packaged_lazy_eligible(const struct nvn_texture *t) {
    return t&&t->pool&&t->pool->flags==0x21&&t->b.packaged_layout&&
        t->b.format==0x25&&t->b.target==1&&t->b.depth==1&&t->b.levels==1&&
        !(t->b.flags&0x50)&&!t->b.stride&&!t->host_half;
}
static void nvn_packaged_require_writable(const struct nvn_texture *t,const char *operation) {
    if(t&&t->packaged_lazy)stop(operation,nvn_caller,t->id,20);
}
/* Called with producer context/mutex held and the destination texture unit
 * active, after descriptor/live/id/sampler validation. The raw guest address
 * is revalidated at upload; the original pool reference remains unchanged. */
static void nvn_packaged_materialize(struct nvn_texture *t) {
    if(!t||!t->packaged_lazy)return;
    if(!t->live||!t->name||!nvn_packaged_lazy_eligible(t)||
       !(t->pool->live||t->pool->retired)||!t->pool->references||
       t->b.offset>t->pool->size||t->size>t->pool->size-t->b.offset)
        stop("NVN_LC2_PACKAGED_LIFETIME",nvn_caller,t->id,24);
    if(t->packaged_uploaded&&t->packaged_generation==t->pool->generation)return;
    normal_guard_require_clear(t->pool->address+t->b.offset,t->size,nvn_caller);
    const uint8_t *source=nvn_pointer(t->pool->address+t->b.offset,t->size,PROT_READ);
    size_t bytes=(size_t)t->b.width*t->b.height*4;
    uint8_t *rgba=malloc(bytes);if(!rgba)stop("NVN_LC2_PACKAGED_UPLOAD_MEMORY",nvn_caller,bytes,21);
    if(!megaman_blocklinear_rgba8_decode(rgba,bytes,source,t->size,t->b.width,t->b.height,t->b.packaged_layout)) {
        free(rgba);stop("NVN_TEXTURE_PACKAGED_DECODE",nvn_caller,t->b.packaged_layout,20);
    }
    glBindTexture(GL_TEXTURE_2D,t->name);GLint alignment;glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    if(t->packaged_uploaded)glTexSubImage2D(GL_TEXTURE_2D,0,0,0,t->b.width,t->b.height,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    else glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,t->b.width,t->b.height,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    free(rgba);glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);
    GLenum error=glGetError();if(error)stop("NVN_LC2_PACKAGED_UPLOAD_GL",nvn_caller,error,21);
    t->packaged_uploaded=1;t->packaged_generation=t->pool->generation;
    printf("NVN_LC2_PACKAGED_MATERIALIZED id=%"PRIu64" texture=%u size=%ux%u RGBA-bytes=%zu pool-generation=%"PRIu64" guest-storage=retained snapshot=none\n",
        t->id,t->name,t->b.width,t->b.height,bytes,t->packaged_generation);
}
#endif
