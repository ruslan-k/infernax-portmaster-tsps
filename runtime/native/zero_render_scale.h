/* Zero/ZX-only compact render storage. Include after struct nvn_texture.
 * The native pool, NVN dimensions, crop and shader texture-size answers remain
 * logical. Only qualified GPU storage/raster dimensions change. No LC2 flag,
 * native address, shader number or 4:3 composition assumption is reused.
 * Physical quality/performance approval belongs to the resulting executable.
 */
#ifndef ZERO_RENDER_SCALE_H
#define ZERO_RENDER_SCALE_H
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static pthread_once_t zero_render_scale_once=PTHREAD_ONCE_INIT;
static unsigned zero_render_scale_active;
static void zero_render_scale_configure(void) {
    const char *option=getenv("ZERO_RENDER_SCALE");
    if(option && strcmp(option,"0") && strcmp(option,"1") && strcmp(option,"2") && strcmp(option,"3") &&
       strcmp(option,"4"))
        stop("ZERO_RENDER_SCALE_OPTION",nvn_caller,0,24);
    /* 1 is the compact diagnostic profile; 1009 exposed unreadable glyphs.
     * 2 preserves native 1280x720 composition/UI and only maps the 1080p
     * window to the physical 720p framebuffer. No menu surface is reduced. */
    zero_render_scale_active=option?(unsigned)(option[0]-'0'):0;
}
static unsigned zero_render_scale_enabled(void) {
    pthread_once(&zero_render_scale_once,zero_render_scale_configure);
    return zero_render_scale_active;
}

/* Geometry of the observed 16:9 window/composition/postprocess family only.
 * This is not storage eligibility: layout() below admits only the windows.
 * In particular,
 * 256x192/240x160 emulation surfaces, R8 palettes, data and arbitrary images
 * never qualify by a broad greater-than comparison. */
static unsigned zero_render_scale_dimensions(unsigned width,unsigned height,
        unsigned *actual_width,unsigned *actual_height) {
    if(zero_render_scale_enabled()==2) {
        if(width!=1920 || height!=1080)return 0;
        *actual_width=1280;*actual_height=720;return 1;
    }
    /* 3 (13/09, physical bench): every render-target switch costs ~5 ns per
     * pixel of write-back on this Mali-450 (4.5 ms at 1280x720, 9 ms at
     * 1920x1080); the CAPCOM/title frame pays 12 switches. Mode 3 keeps the
     * 1080p window at 1280x720 (as 2) and halves only the 1280x720 flags 8
     * offscreen chain (as 1). Menu glyph legibility must be judged physically. */
    /* 4 (Momodora, 19/09): the floor of this port is 640x360 and nothing may render below it, so this
     * mode maps BOTH the 1920x1080 window and the 1280x720 offscreen chain to exactly 640x360 and leaves
     * every other shape native. Mode 1 would halve the 640x360 targets to 320x180, under the floor; mode
     * 3 keeps the window at 1280x720, which is four times the pixels for every UI draw of the title. */
    if(zero_render_scale_enabled()==4) {
        if(width==1920 && height==1080){*actual_width=640;*actual_height=360;return 1;}
        if(width==1280 && height==720){*actual_width=640;*actual_height=360;return 1;}
        return 0;
    }
    if(zero_render_scale_enabled()==3) {
        if(width==1920 && height==1080){*actual_width=1280;*actual_height=720;return 1;}
        if(width==1280 && height==720){*actual_width=640;*actual_height=360;return 1;}
        return 0;
    }
    unsigned divisor=width==1920 && height==1080?3:
        (width==1280 && height==720)||(width==640 && height==360)||
        (width==320 && height==180)?2:0;
    if(!divisor)return 0;
    *actual_width=width/divisor;*actual_height=height/divisor;return 1;
}

static unsigned zero_render_scale_layout(const struct nvn_texture *t) {
    unsigned width,height;
    /* 1007 identifies exact-layout RGBA/D24S8 1280 aliases. Those are owned
     * by zero_texture_storage, including shared pixels/caches/depth proof.
     * Intermediate1920 and depth640 overlap with incompatible layouts and
     * remain native. Never qualify by hard-coded object/pool offsets. */
#ifdef MOMO_SHADERS
    /* Unity 2021 allocates its window and post-process images from pools with flags 0xa2, not the 0xa4
     * Zero/ZX uses (MEDIDO run momo-140). Both are GPU-only render-target pools here; what matters for
     * reduced storage is the image shape below, not which pool object the engine happened to use. */
    return t && t->pool && (t->pool->flags==0xa4 || t->pool->flags==0xa2) &&
#else
    return t && t->pool && t->pool->flags==0xa4 &&
#endif
        ((t->b.width==1920 && t->b.height==1080 && t->b.format==0x25 && t->b.flags==9) ||
#ifdef MOMO_SHADERS
         /* The window's depth companion here is 1920x1080 D24S8 with flags 8 (MEDIDO run momo-142); it has
          * to shrink with its colour or the attachment pair stops. */
         (t->b.width==1920 && t->b.height==1080 && t->b.format==0x35 && t->b.flags==8) ||
#endif
         ((zero_render_scale_enabled()==1 || zero_render_scale_enabled()==3) && t->b.width==1280 && t->b.height==720 && t->b.flags==8 &&
          (t->b.format==0x25 || t->b.format==0x35)) ||
         /* Unity's post-process chain allocates its 1280x720 colour target with flags 9, the same shape
          * the window colour uses, so mode 4 admits both flags for that size (MEDIDO run momo-200: the
          * Uber pass runs at a full 1280x720 and is 80 ms of the frame). */
         (zero_render_scale_enabled()==4 && t->b.width==1280 && t->b.height==720 &&
          (t->b.flags==8 || t->b.flags==9) && (t->b.format==0x25 || t->b.format==0x35))) &&
        zero_render_scale_dimensions(t->b.width,t->b.height,&width,&height) &&
        t->b.target==1 && t->b.depth==1 && t->b.levels==1 && t->b.samples<=1 &&
        !t->b.stride && !t->b.packaged && !t->b.packaged_layout && !t->b.gl_name &&
        !t->host_half && !t->packaged_lazy && !t->host_upload_type &&
        t->b.swizzle[0]==2 && t->b.swizzle[1]==3 && t->b.swizzle[2]==4 && t->b.swizzle[3]==5;
}

/* Called before first GL storage allocation. The existing alias checks still
 * apply, and the complete original RGBA decode must pass initial() below. */
static unsigned zero_render_scale_assign(struct nvn_texture *t) {
    unsigned width,height;
    if(!zero_render_scale_enabled())return 0;
    if(!zero_render_scale_layout(t)) {
        if(t && t->pool && t->pool->flags==0xa4 && t->b.flags==8 &&
           (t->b.format==0x25 || t->b.format==0x35) &&
           zero_render_scale_dimensions(t->b.width,t->b.height,&width,&height))
            printf("ZERO_RENDER_SCALE_NATIVE_STORAGE object=0x%"PRIx64" id=%"PRIu64" pool=0x%"PRIx64" format=0x%x flags=0x%x logical=%ux%u offset=0x%"PRIx64" bytes=0x%"PRIx64" reason=offscreen-alias-unqualified\n",
                t->object,t->id,t->pool->object,t->b.format,t->b.flags,t->b.width,t->b.height,t->b.offset,t->size);
        return 0;
    }
    if(!zero_render_scale_dimensions(t->b.width,t->b.height,&width,&height))
        stop("ZERO_RENDER_SCALE_DIMENSIONS",nvn_caller,t->id,24);
    t->host_scaled=1;t->host_width=width;t->host_height=height;return 1;
}

/* The adapter uses one anonymous name=0 color descriptor for a real depth-only
 * FBO. It is not a native texture and must never receive a sampler handle. */
static unsigned zero_render_scale_depth_only(const struct nvn_texture *t) {
    return t && t->live && !t->id && !t->object && !t->name && !t->depth_buffer &&
        !t->pool && t->b.format==0x25 && t->b.target==1 && t->b.depth==1 && t->b.levels==1;
}

static void zero_render_scale_require(const struct nvn_texture *t) {
    if(!t || !t->host_scaled)return;
    unsigned width,height;
    if(!zero_render_scale_enabled() ||
       !zero_render_scale_dimensions(t->b.width,t->b.height,&width,&height) ||
       t->host_width!=width || t->host_height!=height)
        stop("ZERO_RENDER_SCALE_GEOMETRY",nvn_caller,t->id,24);
    if(zero_render_scale_depth_only(t))return;
    if(t->b.width==1280 && !t->zero_storage) {
#ifdef MOMO_SHADERS
        /* Zero/ZX needs the shared-storage owner because its 1280x720 offscreen images alias each other in
         * one pool. Unity's post-process images here are separate allocations, so the requirement is
         * replaced by the thing it was protecting: no other live image overlaps this one's bytes (MEDIDO
         * run momo-143 stopped on an image with no owner and no overlap). */
        /* The scan is over 4096 texture records and this runs on every render-target change; the answer
         * cannot change while the image lives, so it is decided once (0.78 ms per SetRenderTargets,
         * 26 per frame, MEDIDO run momo-130). */
        struct nvn_texture *mutable_t=(struct nvn_texture *)t;
        if(!mutable_t->momo_scale_alias_checked) {
            unsigned overlaps=0;
            for(unsigned i=0;i<4096 && !overlaps;i++) {
                const struct nvn_texture *o=&nvn_textures[i];
                if(o==t || !o->live || o->pool!=t->pool)continue;
                if(t->b.offset<o->b.offset+o->size && o->b.offset<t->b.offset+t->size)overlaps=1;
            }
            mutable_t->momo_scale_alias_checked=1;mutable_t->momo_scale_alias_free=!overlaps;
        }
        if(!mutable_t->momo_scale_alias_free)
#endif
        stop("ZERO_RENDER_SCALE_OWNER_REQUIRED",nvn_caller,t->id,24);
    }
    zero_texture_storage_require(t);
    if(!t->live || !t->id || !zero_render_scale_layout(t) ||
       !(t->pool->live || t->pool->retired) || !t->pool->references ||
       t->b.offset>t->pool->size || t->size>t->pool->size-t->b.offset)
        stop("ZERO_RENDER_SCALE_LIFETIME",nvn_caller,t->id,24);
}

static void zero_render_scale_initial(struct nvn_texture *t,const uint8_t *rgba,
        size_t bytes,unsigned level) {
    if(!t || !t->host_scaled)return;
    size_t expected=(size_t)t->b.width*t->b.height*4;
    if(level || t->b.format!=0x25 || !rgba || bytes!=expected || bytes<4)
        stop("ZERO_RENDER_SCALE_INITIAL_FORMAT",nvn_caller,t->id,24);
    for(size_t i=4;i<bytes;i+=4)if(memcmp(rgba,rgba+i,4))
        stop("ZERO_RENDER_SCALE_INITIAL_NONUNIFORM",nvn_caller,t->id,20);
    t->composition_initial_uniform=1;
}

static unsigned zero_render_scale_width(const struct nvn_texture *t) {
    return t->host_scaled?t->host_width:t->b.width;
}
static unsigned zero_render_scale_height(const struct nvn_texture *t) {
    return t->host_scaled?t->host_height:t->b.height;
}

static void zero_render_scale_target(const struct nvn_texture *color,const struct nvn_texture *depth) {
    zero_render_scale_require(color);zero_render_scale_require(depth);
    if(!color || !depth || (!color->host_scaled && !depth->host_scaled))return;
    if(color->b.width!=depth->b.width || color->b.height!=depth->b.height ||
       color->host_scaled!=depth->host_scaled ||
       zero_render_scale_width(color)!=zero_render_scale_width(depth) ||
       zero_render_scale_height(color)!=zero_render_scale_height(depth) || !depth->depth_buffer) {
        printf("MOMO_SCALE_ATTACHMENTS color-id=%"PRIu64" %ux%u scaled=%u actual=%ux%u fmt=0x%x flags=0x%x pool=0x%x | depth-id=%"PRIu64" %ux%u scaled=%u actual=%ux%u fmt=0x%x flags=0x%x pool=0x%x buffer=%u\n",
            color->id,color->b.width,color->b.height,color->host_scaled,zero_render_scale_width(color),zero_render_scale_height(color),color->b.format,color->b.flags,color->pool?color->pool->flags:0,
            depth->id,depth->b.width,depth->b.height,depth->host_scaled,zero_render_scale_width(depth),zero_render_scale_height(depth),depth->b.format,depth->b.flags,depth->pool?depth->pool->flags:0,depth->depth_buffer);
        stop("ZERO_RENDER_SCALE_ATTACHMENTS",nvn_caller,depth->id,20);
    }
}

static int64_t zero_render_scale_floor(int64_t value,int64_t divisor) {
    int64_t result=value/divisor;return result-(value%divisor<0);
}

/* Map covered half-open logical rectangles with outward-rounded boundaries.
 * A zero extent remains zero, negative origins are supported, and arithmetic
 * occurs in int64_t before the result is admitted by GLES's int32 rectangle.
 * Use this for viewport/scissor and uniform-depth copy destination rectangles.
 */
static unsigned zero_render_scale_rect(const struct nvn_texture *t,const int32_t in[4],int32_t out[4]) {
    if(!t || !in || !out || in[2]<0 || in[3]<0)return 0;
    if(!t->host_scaled){memcpy(out,in,4*sizeof(*out));return 1;}
    zero_render_scale_require(t);
    const unsigned logical[2]={t->b.width,t->b.height},actual[2]={t->host_width,t->host_height};
    for(unsigned axis=0;axis<2;axis++) {
        int64_t begin=zero_render_scale_floor((int64_t)in[axis]*actual[axis],logical[axis]);
        int64_t end=-zero_render_scale_floor(-((int64_t)in[axis]+in[axis+2])*actual[axis],logical[axis]);
        if(!in[axis+2])end=begin;
        if(begin<INT32_MIN || begin>INT32_MAX || end<begin || end-begin>INT32_MAX)return 0;
        out[axis]=(int32_t)begin;out[axis+2]=(int32_t)(end-begin);
    }
    return 1;
}

static void zero_render_scale_sample(const struct nvn_texture *t,unsigned cpu_view,
        unsigned size_query,unsigned texel_fetch) {
    if(!t || !t->host_scaled)return;
    zero_render_scale_require(t);
    if(!t->name || t->b.format!=0x25 || t->b.flags==9 || cpu_view || texel_fetch || size_query>1)
        stop("ZERO_RENDER_SCALE_SAMPLER",nvn_caller,t->id,20);
    /* size_query here is the translator's level-zero ConstOffset helper.
     * Upload the original b.width/b.height, not host storage dimensions:
     * normalized UVs and the native logical footprint remain unchanged.
     * Native integer texel fetch and CPU views need a different contract. */
}

static void zero_render_scale_fragment_ratio(const struct nvn_texture *t,float out[2]) {
    out[0]=out[1]=1.0f;
    if(!t || !t->host_scaled)return;
    zero_render_scale_require(t);
    out[0]=(float)t->b.width/t->host_width;out[1]=(float)t->b.height/t->host_height;
}

static unsigned zero_render_scale_identifier(unsigned char c) {
    return (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_';
}

/* Insert after the leading version/extensions/macros/comments, before any
 * declaration/function. This keeps ESSL extension directives before code. */
static size_t zero_render_scale_declarations(const char *source,size_t bytes) {
    size_t at=0;
    for(;;) {
        while(at<bytes && (source[at]==' ' || source[at]=='\t' || source[at]=='\r' || source[at]=='\n'))at++;
        if(at+1<bytes && source[at]=='/' && source[at+1]=='/') {
            while(at<bytes && source[at]!='\n')at++;
            continue;
        }
        if(at+1<bytes && source[at]=='/' && source[at+1]=='*') {
            at+=2;while(at+1<bytes && !(source[at]=='*' && source[at+1]=='/'))at++;
            if(at+1>=bytes)stop("ZERO_RENDER_SCALE_GLSL_COMMENT",nvn_caller,at,24);
            at+=2;continue;
        }
        if(at<bytes && source[at]=='#') {
            do {while(at<bytes && source[at]!='\n')at++;
                if(at==bytes)return at;
                unsigned continued=at && source[at-1]=='\\';at++;
                if(!continued)break;
            } while(at<bytes);
            continue;
        }
        return at;
    }
}

/* Each draw uploads zero_render_scale_xy even for an unscaled target (1,1).
 * Original z/w are untouched: perspective reciprocal-w and depth semantics
 * do not change. The XY correction is essential for Advent's pixel rectangles.
 * Input is the final fragment source after depth/precision/domain wrappers;
 * runtime shader selection still verifies the original code/control identity.
 * The caller owns the returned allocation, including the unchanged-source case.
 */
static char *zero_render_scale_fragment(const char *source) {
    if(!source)stop("ZERO_RENDER_SCALE_GLSL_MISSING",nvn_caller,0,24);
    size_t bytes=strlen(source);
    if(bytes>(2U<<20))stop("ZERO_RENDER_SCALE_GLSL_SIZE",nvn_caller,bytes,24);
    static const char token[]="gl_FragCoord";
    static const char replacement[]="(vec4(gl_FragCoord.xy * zero_render_scale_xy, gl_FragCoord.zw))";
    static const char declaration[]="uniform mediump vec2 zero_render_scale_xy;\n";
    unsigned enabled=zero_render_scale_enabled();size_t count=0;
    if(enabled)for(size_t at=0;at+sizeof(token)-1<=bytes;at++)
        if(!memcmp(source+at,token,sizeof(token)-1) &&
           (!at || !zero_render_scale_identifier((unsigned char)source[at-1])) &&
           !zero_render_scale_identifier((unsigned char)source[at+sizeof(token)-1]))count++;
    if(count && strstr(source,"zero_render_scale_xy"))stop("ZERO_RENDER_SCALE_GLSL_DUPLICATE",nvn_caller,0,24);
    size_t insert=count?zero_render_scale_declarations(source,bytes):0;
    size_t extra=count*(sizeof(replacement)-sizeof(token))+(count?sizeof(declaration)-1:0);
    char *out=malloc(bytes+extra+1);
    if(!out)stop("ZERO_RENDER_SCALE_GLSL_MEMORY",nvn_caller,bytes+extra+1,21);
    size_t used=0;
    for(size_t at=0;at<bytes;) {
        if(count && at==insert){memcpy(out+used,declaration,sizeof(declaration)-1);used+=sizeof(declaration)-1;}
        if(count && at+sizeof(token)-1<=bytes && !memcmp(source+at,token,sizeof(token)-1) &&
           (!at || !zero_render_scale_identifier((unsigned char)source[at-1])) &&
           !zero_render_scale_identifier((unsigned char)source[at+sizeof(token)-1])) {
            memcpy(out+used,replacement,sizeof(replacement)-1);used+=sizeof(replacement)-1;at+=sizeof(token)-1;
        } else out[used++]=source[at++];
    }
    out[used]='\0';return out;
}
#endif
