/* LC2 CPU_UNCACHED image family, proved by original map/lock/write/unlock
 * and host2602 stable CPU-changed/GPU-old snapshots. This runs synchronously
 * in the ordinary sampler under the existing producer context mutex.
 */
#ifndef NVN_TEXTURE_COHERENCE_H
#define NVN_TEXTURE_COHERENCE_H
static unsigned nvn_cpu_image_family(const struct nvn_texture *t) {
    return t&&t->pool&&t->pool->flags==0x22&&t->b.flags==0x10&&
        (t->b.format==0x39||t->b.format==0x97);
}
#include "nvn_cpu_packed.h"
static unsigned nvn_cpu_image_overlap(const struct nvn_texture *a,const struct nvn_texture *b) {
    return a!=b&&a->pool==b->pool&&a->b.offset<b->b.offset+b->size&&b->b.offset<a->b.offset+a->size;
}
/* CopyBufferToTexture writes CPU and GL independently of this sampler cache.
 * Invalidate even if the CPU producer later restores the previous bytes. */
static void nvn_cpu_image_invalidate(struct nvn_texture *t) {
    if(nvn_cpu_image_family(t)){free(t->cpu_snapshot);t->cpu_snapshot=NULL;}
}
/* Reject before framebuffer binding, including before first sampling. A
 * clear-only pass writes pixels without incrementing texture->draws. */
static void nvn_cpu_image_require_target(const struct nvn_texture *t) {
    if(t && t->zero_backend_deferred)stop("ZERO_FLOAT_TEXTURE_TARGET_BACKEND_REQUIRED",nvn_caller,t->b.format,20);
    if(t && zero_bc_bytes(t->b.format))stop("ZERO_BC_RENDER_TARGET_UNSUPPORTED",nvn_caller,t->b.format,20);
    if(nvn_cpu_image_family(t))stop("NVN_LC2_CPU_IMAGE_RENDER_TARGET_REQUIRED",nvn_caller,t->id,20);
    /* An overlapping CPU image must share this exact pool pointer, and that
     * family requires pool flags 0x22. Other pools cannot contain such an
     * alias; their independent target/lifetime checks still run normally. */
    if(!t||!t->pool||t->pool->flags!=0x22)return;
    for(unsigned i=0;t&&i<4096;i++) {
        const struct nvn_texture *other=&nvn_textures[i];
        if(other->live&&nvn_cpu_image_family(other)&&nvn_cpu_image_overlap(t,other))
            stop("NVN_LC2_CPU_IMAGE_TARGET_ALIAS_REQUIRED",nvn_caller,other->id,20);
    }
}
static void nvn_cpu_image_refresh(struct nvn_texture *t,unsigned omitting) {
    if(!nvn_cpu_image_family(t))return;
    if(!t->live||!t->name||!(t->pool->live||t->pool->retired)||!t->pool->references)
        stop("NVN_LC2_CPU_IMAGE_LIFETIME",nvn_caller,t->id,24);
    if(t->b.target!=1||t->b.depth!=1||t->b.levels!=1||t->b.samples||
       !t->b.width||!t->b.height||t->b.width>4096||t->b.height>4096||
       t->b.packaged||t->b.packaged_layout||t->b.gl_name||t->host_half||t->packaged_lazy||
       t->b.swizzle[0]!=2||t->b.swizzle[1]!=3||t->b.swizzle[2]!=4||t->b.swizzle[3]!=5)
        stop("NVN_LC2_CPU_IMAGE_CONTRACT",nvn_caller,t->id,20);
    uint64_t row=(uint64_t)t->b.width*2,stride=t->b.stride,span=stride*t->b.height;
    if(stride<row||stride%16||span>t->size||t->b.offset>t->pool->size||
       t->size>t->pool->size-t->b.offset||t->pool->address>UINT64_MAX-t->b.offset)
        stop("NVN_LC2_CPU_IMAGE_RANGE",nvn_caller,t->id,24);
    /* Storage membership is immutable between the same Initialize/Finalize
     * epoch transitions already guarded by cadence. Cache only that absence
     * of aliases, never bytes, lifetime, native memory permissions or guards. */
    unsigned alias_hit=nvn_texture_aliases_cached(t,"NVN_LC2_CPU_IMAGE_ALIAS_REQUIRED",20);
    static unsigned alias_reported;
    if(alias_hit&&!alias_reported) {
        alias_reported=1;
        printf("NVN_LC2_CPU_IMAGE_ALIAS_CACHE id=%"PRIu64" epoch=%"PRIu64" membership=unchanged cpu-bytes=uncached guards=every-sample\n",
            t->id,nvn_mfc_alias_cache.epoch);
    }
    normal_guard_require_clear(t->pool->address+t->b.offset,t->size,nvn_caller);
    const uint8_t *source=nvn_pointer(t->pool->address+t->b.offset,span,PROT_READ);
    /* Only an omitted draw may defer CPU-image transfer. Keep every storage
     * validation above, and leave cpu_snapshot at the last successful GPU
     * upload: the next real sample must compare against the latest CPU rows.
     * Explicit NVN copies still upload and invalidate independently. */
    if(omitting) {
        static unsigned reported;
        if(!reported) {
            reported=1;
            printf("NVN_LC2_CPU_IMAGE_DEFERRED id=%"PRIu64" texture=%u format=0x%x validation=complete snapshot=unchanged reason=omitted-draw\n",
                t->id,t->name,t->b.format);
        }
        return;
    }
    size_t bytes=(size_t)row*t->b.height;
    uint8_t *snapshot=malloc(bytes);if(!snapshot)stop("NVN_LC2_CPU_IMAGE_MEMORY",nvn_caller,bytes,21);
    for(unsigned y=0;y<t->b.height;y++)memcpy(snapshot+y*row,source+y*stride,(size_t)row);
    /* Compare complete, tightly packed CPU snapshots. MM8's 1024x512 image
     * may contain a smaller changed region; unchanged pixels need no conversion
     * or driver upload. Padding is excluded and all four channels, including
     * alpha, remain bit-exact. An invalidated/new snapshot still uploads all. */
    unsigned x0=0,y0=0,x1=t->b.width,y1=t->b.height;
    if(t->cpu_snapshot) {
        x0=t->b.width;y0=t->b.height;x1=y1=0;
        for(unsigned y=0;y<t->b.height;y++) {
            const uint8_t *old=t->cpu_snapshot+y*row,*now=snapshot+y*row;
            if(!memcmp(old,now,(size_t)row))continue;
            unsigned left=0,right=t->b.width;
            while(left<right&&!memcmp(old+left*2,now+left*2,2))left++;
            while(right>left&&!memcmp(old+(right-1)*2,now+(right-1)*2,2))right--;
            if(left<x0)x0=left;
            if(right>x1)x1=right;
            if(y<y0)y0=y;
            y1=y+1;
        }
        if(!y1){free(snapshot);return;}
    }
    unsigned update_width=x1-x0,update_height=y1-y0;
    GLenum previous_error=glGetError();
    if(previous_error){free(snapshot);stop("NVN_LC2_CPU_IMAGE_PREEXISTING_GL",nvn_caller,previous_error,21);}
    GLint bound,alignment;glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);
    if((GLuint)bound!=t->name){free(snapshot);stop("NVN_LC2_CPU_IMAGE_BINDING",nvn_caller,t->id,24);}
    unsigned upload_type=nvn_cpu_image_upload_type(t);
    size_t output_row=(size_t)update_width*nvn_texture_upload_bytes(upload_type);
    size_t rgba_bytes=output_row*update_height;uint8_t *rgba=malloc(rgba_bytes);
    if(!rgba){free(snapshot);stop("NVN_LC2_CPU_IMAGE_MEMORY",nvn_caller,rgba_bytes,21);}
    for(unsigned y=0;y<update_height;y++)
        if(!nvn_texture_decode_upload(t->b.format,t->b.swizzle,upload_type,
                rgba+y*output_row,snapshot+(y+y0)*row+x0*2,update_width)) {
            free(rgba);free(snapshot);stop("NVN_LC2_CPU_IMAGE_DECODE",nvn_caller,t->b.format,20);
        }
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexSubImage2D(GL_TEXTURE_2D,0,x0,y0,update_width,update_height,GL_RGBA,upload_type,rgba);
    glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);free(rgba);
    GLenum error=glGetError();
    if(error){free(snapshot);stop("NVN_LC2_CPU_IMAGE_UPLOAD_GL",nvn_caller,error,21);}
    /* These formats cannot enter any CPU-view path. Finalize already frees
     * this snapshot, and a new texture identity starts with a NULL pointer. */
    unsigned first=!t->cpu_snapshot;free(t->cpu_snapshot);t->cpu_snapshot=snapshot;
    static uint64_t updates,full_bytes,uploaded_bytes;
    updates++;full_bytes+=(bytes/2)*nvn_texture_upload_bytes(upload_type);uploaded_bytes+=rgba_bytes;
    if(updates<=4||updates%120==0)
        printf("NVN_LC2_CPU_IMAGE_DAMAGE updates=%"PRIu64" full-bytes=%"PRIu64" uploaded-bytes=%"PRIu64" last-id=%"PRIu64" rect=%u/%u/%u/%u channels=unchanged snapshot=after-success\n",
            updates,full_bytes,uploaded_bytes,t->id,x0,y0,update_width,update_height);
    if(first)printf("NVN_LC2_CPU_IMAGE_COHERENCE id=%"PRIu64" texture=%u format=0x%x size=%ux%u stride=%"PRIu64" pool-flags=0x22 source=cpu-rows snapshot=after-success diagnostic=after-update\n",
        t->id,t->name,t->b.format,t->b.width,t->b.height,stride);
}
#endif
