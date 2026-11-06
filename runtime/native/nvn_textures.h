/* Port-local linear guest texture layout, consumed by our upload/copy paths.
 * Original builder setters retain their native layout. Packaged Tegra images,
 * sparse storage and unimplemented formats are rejected, never reinterpreted.
 */
#include "nvn_texture_formats.h"
/* Texture support promoted to GLES3 core need not retain GLES2 OES names. */
static unsigned nvn_texture_context_major(void) {
    return nvn_texture_gles_major((const char *)glGetString(GL_VERSION));
}
#include "aw_float_emu.h"
#include "megaman_blocklinear.h"
#include "zero_bc_layout.h"
#include "megaman_texture_view.h"
#include "t2_page_substitution.h"   /* Frente F: core (fingerprint, catalogue, variant); host hooks below */
struct nvn_texture_builder {
    uint64_t device,pool,offset;uint32_t flags,target,width,height,depth,levels,format,samples,swizzle[4],depth_mode,pad;
    uint64_t packaged;uint16_t packaged_layout,pad2;uint32_t stride,gl_name,pad3;
};
_Static_assert(offsetof(struct nvn_texture_builder,packaged)==0x50 && offsetof(struct nvn_texture_builder,stride)==0x5c,"observed texture builder");
struct nvn_texture {
    uint64_t object,id,size;struct nvn_texture_builder b;struct nvn_pool *pool;
    struct zero_texture_storage *zero_storage;
    uint64_t levels[14];GLuint name,depth_buffer;unsigned live,references,draws,normal_guarded,normal_unmaterialized;
    uint64_t parameter_id;EGLContext parameter_context;GLuint parameter_name;
    unsigned parameter_known;GLint parameters[4];
    uint64_t depth_constant_id;EGLContext depth_constant_context;GLuint depth_constant_name;
    unsigned depth_constant_known,depth_discarded;GLfloat depth_constant_value; unsigned host_half;
    unsigned composition_initial_uniform,composition_target_seen,composition_other_target;
    unsigned host_scaled,host_width,host_height,host_scaled_sampled,host_scaled_bound,host_scaled_drawn;
    unsigned zero_cpu_dirty,zero_backend_deferred,aw_float_kind,aw_native_format;
    unsigned aw_clear_valid,aw_clear_mask,aw_clear_known,aw_draws_since_clear,aw_caustic_half;float aw_clear_color[4];GLint aw_clear_scissor[4];
    GLuint zero_mip_masks[2];unsigned zero_mip_tail;unsigned momo_scale_alias_checked,momo_scale_alias_free;
    unsigned zero_depth_valid,zero_stencil_value,zero_depth_discarded;float zero_depth_value;
    unsigned packaged_lazy,packaged_uploaded,host_upload_type;uint64_t packaged_generation;
    uint8_t *cpu_snapshot;uint64_t cpu_generation,view_generation[3];GLuint cpu_views[3];
    struct t2_page_state t2_page;uint64_t t2_budget_bytes;   /* Frente F: palettized page state, bytes counted in the texture budget */};
/* Animal Well SDK 17.5.3 texture view (measured): [0] level base, [1..2] level count (byte order varies), [3] layer base,
 * [4..5] layer count, [6] target, [7] field flags (0x3f = levels|layers|format|swizzle|depth-stencil|target),
 * [8..11] format, [12..15] swizzle, [16] depth-stencil mode. Only views identical to the whole single-level texture pass. */
static inline unsigned aw_texture_view_identity_native(const uint8_t *v,const struct nvn_texture_builder *b,unsigned native_format) {
#ifdef MOMO_SHADERS
    /* SDK 18.3.2 view layout (16 bytes, megaman_texture_view.h). */
    uint32_t seen;memcpy(&seen,v+8,4);
    /* A cube normalized from a one-element cube array keeps its original target in views. */
    if(b->target==8 && v[6]==9 && (v[7]&MOMO_VIEW_FLAG_TARGET))
        return momo_texture_view_identity_target(v,b->levels,native_format&&seen==native_format?seen:b->format,9);
    return momo_texture_view_identity_target(v,b->levels,native_format&&seen==native_format?seen:b->format,b->target);
#else
    uint32_t format;memcpy(&format,v+8,4);if(native_format && format==native_format)format=b->format;
    if(v[7]&~0x3fU)return 0;
    if(b->levels!=1 || v[0] || !((v[1]==0&&v[2]==1)||(v[1]==1&&v[2]==0)))return 0;
    if(v[3] || !((v[4]==0&&v[5]==1)||(v[4]==1&&v[5]==0)))return 0;
    if(v[6]!=b->target && (v[7]&0x20))return 0;
    if((v[7]&0x4) && format!=b->format)return 0;
    if((v[7]&0x8) && (v[12]!=2||v[13]!=3||v[14]!=4||v[15]!=5))return 0;
    if(v[16])return 0;
    for(unsigned i=17;i<MEGAMAN_TEXTURE_VIEW_BYTES;i++)if(v[i])return 0;
    return 1;
#endif
}
static inline unsigned aw_texture_view_identity(const uint8_t *v,const struct nvn_texture_builder *b){return aw_texture_view_identity_native(v,b,0);}
static struct nvn_texture nvn_textures[4096];
/* Highest slot index that has ever been live. The lookup tables are 4096 entries of a large struct, so a
 * full scan walks well over a megabyte; the game never fills the table, and stopping at the high-water
 * mark costs one comparison to publish and saves the tail of every scan that misses the hint. */
static unsigned nvn_textures_high;
static void zero_texture_storage_require(const struct nvn_texture *t);
#include "zero_render_scale.h"
#include "zero_texture_storage.h"
/* Cached GPU targets have no fabricated CPU completion. Ordinary GPU
 * sampling uses their live texture; any native CPU/packed-view/alias access
 * requires a separately implemented readback before it can be admitted. */
static void zero_texture_require_cpu(const struct nvn_pool *pool,uint64_t offset,uint64_t size) {
    if(!size || !pool)return;
    if(!__atomic_load_n(&pool->zero_r8_target_seen,__ATOMIC_ACQUIRE) &&
       (!zero_render_scale_enabled() || pool->flags!=0xa4))return;
    for(unsigned i=0;i<4096;i++) {
        struct nvn_texture *t=&nvn_textures[i];
        if(!t->live || t->pool!=pool || offset>=t->b.offset+t->size || t->b.offset>=offset+size)continue;
        struct nvn_texture *storage=zero_texture_storage_state(t);
        if(t->b.format==1 && storage->composition_target_seen)
            stop("ZERO_R8_GPU_READBACK_REQUIRED",nvn_caller,t->id,20);
        /* Map/Invalidate and adapter GPU-address reads cannot certify native
         * CPU bytes after scaled rendering. Shared aliases are joined without
         * invoking this CPU frontier; actual readback remains unimplemented. */
        if(storage->host_scaled && (storage->composition_target_seen || storage->draws || storage->zero_depth_valid))
            stop("ZERO_SCALED_GPU_READBACK_REQUIRED",nvn_caller,t->id,20);
    }
}
#define NVN_TEXTURE_HINT_SLOTS 4096U
/* stamp is 0 for an object the table owns, and the stamped id plus one for a copy resolved by identity;
 * a stamped hit is only taken after re-reading the stamp, so a reused scratch object never keeps the
 * texture it used to hold. */
static struct {uint64_t object;unsigned index;uint32_t stamp;} nvn_texture_hints[NVN_TEXTURE_HINT_SLOTS];
static uint64_t nvn_texture_hint_hits,nvn_texture_hint_misses,nvn_texture_hint_stamp_hits;
#include "zero_mip_clamp_runtime.h"
#include "nvn_texture_diagnostic.h"
#include "momo_texture_stats.h"
#include "nvn_texture_parameters.h"
#include "nvn_framebuffers.h"
#include "zero_depth_framebuffer.h"
#include "nvn_low_composition.h"
#include "nvn_normal.h"
#include "nvn_texture_coherence.h"
static uint64_t nvn_texture_layout(uint64_t address,struct nvn_texture_builder *b,uint64_t levels[14]) {
    /* SDK 6.5 prefixes this builder with an opaque word; remaining fields
     * match the normalized layout. SetDevice +0x405a60, SetStride +0x405ba0. */
    memcpy(b,nvn_pointer(address+8,sizeof(*b),PROT_READ),sizeof(*b));
    /* SDK 17.5.3 builder (measured): the linear stride lives at +0x50 (the former packaged slot), the former stride and
     * packaged-layout slots are unused and +0x60 holds a builder marker (0x50). Packaged textures never occur here. */
    b->stride=(uint32_t)b->packaged;b->packaged=0;b->packaged_layout=0;b->gl_name=0;
    if(!nvn_device.live || b->device!=nvn_device.address)stop("NVN_TEXTURE_DEVICE",nvn_caller,b->device,24);
    /* Unity 2021 creates its default cube as a one-element cube ARRAY (target 9, six layers; MEDIDO
     * run momo-040). One element is exactly a cube, so it is normalized to the cube path. */
    if(b->target==9 && b->depth==6){b->target=8;b->depth=1;}
    if(b->target==8) {
        if(b->width && b->width==b->height && b->width<=(uint32_t)nvn_device.texture_limit && b->depth==1 &&
           nvn_texture_format_bytes(b->format) && !nvn_texture_format_depth(b->format) && b->levels && b->levels<=14 &&
           !(b->flags&~8U) && !b->samples && !b->stride && !b->packaged && !b->packaged_layout && !b->gl_name &&
           nvn_texture_swizzle_supported(b->format,b->swizzle)) {
            /* Driver-owned cubes use the same linear CPU adapter contract as
             * 2D images. Explicit texel writes convert to the GLES face; only
             * the independently pinned packaged cube below is Tegra tiled. */
            uint64_t face=0;unsigned width=b->width,bpp=nvn_texture_format_bytes(b->format);
            for(unsigned i=0;i<b->levels;i++) {
                levels[i]=face;face=(face+(uint64_t)width*width*bpp+511)&~UINT64_C(511);
                if(i+1<b->levels && width==1)stop("NVN_TEXTURE_EXCESS_MIPS",nvn_caller,b->levels,24);
                if(width>1)width>>=1;
            }
            return face*6;
        }

        /* Unity's default reflection probe is a BC6H cube (128x128, 8 levels, MEDIDO run momo-045).
         * GLES2 can neither sample a cube of compressed HDR blocks nor a float cube, so the storage is
         * laid out exactly (six faces of the BC mip chain) and the image is marked unsamplable: it is
         * only ever uploaded by this 2D title, and a real sampler bind stops instead of drawing wrong. */
        if(zero_bc_bytes(b->format) && b->width==b->height && b->depth==1 && b->levels && b->levels<=14 &&
           !b->flags && !b->samples && !b->stride && !b->packaged && !b->packaged_layout && !b->gl_name) {
            unsigned bc=zero_bc_bytes(b->format);uint64_t face=0;
            for(unsigned i=0;i<b->levels;i++) {
                struct zero_bc_level m;
                if(!zero_bc_level_layout(b->width,b->height,bc,i,&m))stop("NVN_TEXTURE_EXCESS_MIPS",nvn_caller,b->levels,24);
                levels[i]=m.offset;if(m.offset+m.size>face)face=m.offset+m.size;
            }
            face=(face+511)&~UINT64_C(511);
            b->gl_name=6;      /* unsamplable marker, see nvn_render.h */
            return face*6;
        }

        /* DefaultCube_CM.tex has six 8192-byte faces. Its original mip table
         * is 0/1000/1400/1600/1800/1a00/1c00/2000, in Tegra BC3 blocks.
         * The engine requests six levels; retain the complete face stride. */
        if(b->width!=64 || b->height!=64 || b->depth!=1 || b->format!=0x44 ||
           (b->levels!=6 && b->levels!=7) || b->flags || b->samples || b->stride ||
           b->packaged || b->packaged_layout || b->gl_name || !nvn_texture_swizzle_supported(b->format,b->swizzle)) {
            printf("ZERO_CUBE_LAYOUT target=%u size=%ux%ux%u levels=%u format=0x%x flags=0x%x samples=%u stride=%u packaged=0x%"PRIx64" layout=0x%x gl=%u swizzle=%u,%u,%u,%u\n",
                   b->target,b->width,b->height,b->depth,b->levels,b->format,b->flags,b->samples,b->stride,b->packaged,b->packaged_layout,b->gl_name,b->swizzle[0],b->swizzle[1],b->swizzle[2],b->swizzle[3]);
            stop("ZERO_CUBE_LAYOUT_REQUIRED",nvn_caller,b->format,20);
        }
        const uint64_t offsets[7]={0,0x1000,0x1400,0x1600,0x1800,0x1a00,0x1c00};
        memcpy(levels,offsets,b->levels*sizeof(offsets[0]));return 0xc000;
    }
    /* Unity 2021 also builds tiny 3D images (target 2) with a single slice - a 1x1x1 default LUT
     * (MEDIDO run momo-037) and single-layer 2D arrays (target 4, run momo-039). With depth/layers 1
     * the storage and sampling are exactly the 2D case, so they are admitted here; a real volume or
     * array (depth > 1) still stops, because GLES2 has neither. */
    /* A real volume or array (depth > 1) has no GLES2 equivalent. Unity's default resources include
     * one (4x4x16 R8, MEDIDO run momo-043); its storage is laid out as stacked slices in a single 2D
     * image so creation and uploads are exact, and gl_name records the slice count so that binding it
     * to a sampler stops instead of drawing a wrong image. */
    if((b->target==2||b->target==4) && b->depth>1) {
        if(b->levels!=1 || (uint64_t)b->height*b->depth>(uint64_t)nvn_device.texture_limit)
            stop("MOMO_VOLUME_TEXTURE_LAYOUT",nvn_caller,((uint64_t)b->depth<<32)|b->levels,20);
        b->gl_name=b->depth;b->height*=b->depth;b->depth=1;
    }
    if((b->target!=1 && !((b->target==2||b->target==4) && b->depth==1)) || b->depth!=1 || !b->width || !b->height ||
       b->width>(uint32_t)nvn_device.texture_limit || b->height>(uint32_t)nvn_device.texture_limit ||
       0){static unsigned told;if(told++<8)printf("MOMO_TEXTURE_BUILDER flags=0x%x target=%u size=%ux%ux%u levels=%u format=0x%x samples=%u stride=%u\n",
           b->flags,b->target,b->width,b->height,b->depth,b->levels,b->format,b->samples,b->stride);}
    if((b->target!=1 && !((b->target==2||b->target==4) && b->depth==1)) || b->depth!=1 || !b->width || !b->height ||
       b->width>(uint32_t)nvn_device.texture_limit || b->height>(uint32_t)nvn_device.texture_limit ||
       !b->levels || b->levels>14 || b->samples>1)stop("NVN_TEXTURE_DIMENSIONS_REQUIRED",nvn_caller,((uint64_t)b->width<<32)|b->height,20);
    /* SDK 17.5.3 keeps a builder marker (0x50) at the former gl_name slot; the game never sets a GL texture name. */
    if(b->flags&~0x5dU || b->packaged)stop("NVN_TEXTURE_LAYOUT_REQUIRED",nvn_caller,b->flags,20); /* 0x4 IMAGE: Animal Well marks its window textures; no image store happens */
    /* Unity's 256x512 single-level UI atlas arrives as format 0x79, which the SDK enum list in
     * this file does not name; every other single-channel atlas in the log is 0x1 with the same
     * shape, so it is stored as 8-bit R8 and sampled through the same path.
     * shortcut: 0x79 is admitted as R8 by shape, not by a documented enum entry; revisit if the
     * texture shows as garbage or a later stop disagrees with a one-byte-per-pixel layout. */
    if(b->format==0x79) {
        static unsigned told;if(!told++)printf("NVN_FORMAT_0x79_AS_RGBA8 size=%ux%u levels=%u stride=%u flags=0x%x swizzle=%u,%u,%u,%u\n",b->width,b->height,b->levels,b->stride,b->flags,b->swizzle[0],b->swizzle[1],b->swizzle[2],b->swizzle[3]);
        b->format=0x25;
    }
    unsigned bpp=nvn_texture_format_bytes(b->format),bc=zero_bc_bytes(b->format);
    if(!bpp && !bc)stop("NVN_TEXTURE_FORMAT_REQUIRED",nvn_caller,((uint64_t)b->width<<40)|((uint64_t)b->height<<20)|b->format,20);
#ifdef MOMO_SHADERS
    /* Mali-450 GLES2 has no float render target, so the HDR formats Unity asks for (R11G11B10F and
     * RGBA16F for the camera and the PPv2 chain) are stored as RGBA8: values above 1.0 are lost.
     * Registered as a deviation in the HANDOFF (MEDIDO runs momo-065 and momo-068). */
    if(b->format==0x3f || b->format==0x29) {
        static unsigned told[2];
        unsigned which=b->format==0x29;
        if(!told[which]++)printf("MOMO_HDR_TARGET_AS_RGBA8 format=0x%x size=%ux%u note=range-above-1-lost\n",b->format,b->width,b->height);
        b->format=0x25;
    }
#endif
    /* Unity's 128x1 ramp/lookup tables are single-channel R16F with swizzle R,ONE,ONE,ONE. GLES has
     * no texture swizzle, so the constant lanes are dropped by forcing identity: a shader that reads
     * .g or .b of such a table gets 0 instead of 1. Every caller samples .r.
     * shortcut: constant lanes lost; decode to RGBA8 if a shader ever reads .g/.b. */
    if(b->format==5 && !nvn_texture_swizzle_supported(b->format,b->swizzle)) {
        static unsigned told;if(!told++)printf("NVN_R16F_LUT_IDENTITY size=%ux%u swizzle=%u,%u,%u,%u\n",b->width,b->height,b->swizzle[0],b->swizzle[1],b->swizzle[2],b->swizzle[3]);
        for(unsigned i=0;i<4;i++)b->swizzle[i]=i+2;
    }
    if(!nvn_texture_swizzle_supported(b->format,b->swizzle))stop("NVN_TEXTURE_SWIZZLE_REQUIRED",nvn_caller,
        ((uint64_t)b->width<<40)|((uint64_t)b->height<<28)|((uint64_t)b->format<<4)|(b->swizzle[3]),20);
    if(bc) {
        if(b->flags&0x50 || b->packaged_layout)stop("ZERO_BC_LINEAR_BLOCK_LAYOUT_REQUIRED",nvn_caller,b->packaged_layout,20);
        uint64_t size=0;
        for(unsigned i=0;i<b->levels;i++) {
            struct zero_bc_level m;
            if(!zero_bc_level_layout(b->width,b->height,bc,i,&m))stop("NVN_TEXTURE_EXCESS_MIPS",nvn_caller,b->levels,24);
            levels[i]=m.offset;size=m.offset+m.size;
        }
        return (size+4095)&~UINT64_C(4095);
    }
    if(b->packaged_layout) {
        size_t storage=0;
        if(b->format!=0x25 || b->levels!=1 || b->flags&0x50 || b->stride ||
           !megaman_blocklinear_rgba8_size(b->width,b->height,b->packaged_layout,&storage))
            stop("NVN_TEXTURE_PACKAGED_LAYOUT_REQUIRED",nvn_caller,b->packaged_layout,20);
        levels[0]=0;return ((uint64_t)storage+4095)&~UINT64_C(4095);
    }
    uint64_t size=0;unsigned width=b->width,height=b->height;
    for(unsigned i=0;i<b->levels;i++) {
        uint64_t stride=(uint64_t)width*bpp;
        if(b->flags&0x50) {
            if(b->levels!=1 || b->stride<stride || b->stride%16)stop("NVN_TEXTURE_LINEAR_STRIDE",nvn_caller,b->stride,24);
            stride=b->stride;
        }
        levels[i]=size;size=(size+stride*height+255)&~UINT64_C(255);
        if(i+1<b->levels && width==1 && height==1)stop("NVN_TEXTURE_EXCESS_MIPS",nvn_caller,b->levels,24);
        if(width>1)width>>=1;
        if(height>1)height>>=1;
    }
    return (size+4095)&~UINT64_C(4095);
}
#ifdef MOMO_SHADERS
/* Unity 2021 copies whole NVNtexture objects into its own storage and binds the copy, so the bound
 * address is not the one nvnTextureInitialize saw (MEDIDO run momo-023: render target 0x19f634538 is
 * a copy of a window texture). The host owns the object's bytes (it never runs the original
 * initializer), so it stamps its own identity into the object at initialization; a copy carries the
 * stamp and resolves to the same texture. An unstamped unknown object is still an error. */
#define MOMO_TEXTURE_STAMP_MAGIC 0x4d4f544dU   /* 'MTOM' */
struct momo_texture_stamp {uint32_t magic,id,check,zero;};
static void momo_texture_stamp(uint64_t object,uint64_t id) {
    struct momo_texture_stamp stamp={MOMO_TEXTURE_STAMP_MAGIC,(uint32_t)id,
        (uint32_t)(MOMO_TEXTURE_STAMP_MAGIC^(id*2654435761U)),0};
    memcpy(nvn_pointer(object,sizeof(stamp),PROT_READ|PROT_WRITE),&stamp,sizeof(stamp));
}
static struct nvn_texture *momo_texture_alias(uint64_t object,unsigned *index_out);
/* The stamped id plus one, or zero when the object carries no valid stamp. */
static uint32_t momo_texture_stamp_read(uint64_t object) {
    struct momo_texture_stamp stamp;
    memcpy(&stamp,nvn_pointer(object,sizeof(stamp),PROT_READ),sizeof(stamp));
    if(stamp.magic!=MOMO_TEXTURE_STAMP_MAGIC ||
       stamp.check!=(uint32_t)(MOMO_TEXTURE_STAMP_MAGIC^(stamp.id*2654435761U)))return 0;
    return stamp.id+1;
}
#endif
/* A hint table over the same linear scan, never a copy of the resource. Each hit rechecks live and the
 * object, so a slot reused by another texture simply misses and falls back to the complete scan.
 * Why: nvn_textures is 4096 entries of a large struct, so one miss walks hundreds of kilobytes. The title
 * frame calls this twice per SetRenderTargets, 26 times a frame, and those calls alone were about 22 ms
 * of the 27 ms that operation costs (MEDIDO run momo-214 against the GL attribution of momo-215, which
 * puts only 4.8 ms in glBindFramebuffer). MOMO_TEXTURE_INDEX=0 goes back to the plain scan. */
static unsigned nvn_texture_hint_enabled(void) {
    static int configured;static unsigned enabled;
    if(!configured){const char *o=getenv("MOMO_TEXTURE_INDEX");enabled=!o||strcmp(o,"0");configured=1;}
    return enabled;
}
/* Mix the whole word before taking the slot: these objects are guest pointers with 128- or 256-byte
 * alignment, so a plain shift leaves the low bits constant and only a sixteenth of the table gets used
 * (MEDIDO: 12% of lookups missed with a >>4 slot on a 4096-entry table). */
static inline unsigned nvn_texture_hint_slot(uint64_t object) {
    uint64_t h=object;h^=h>>13;h*=0x9e3779b97f4a7c15ULL;h^=h>>29;
    return (unsigned)(h&(NVN_TEXTURE_HINT_SLOTS-1));
}
static struct nvn_texture *nvn_texture_find(uint64_t object) {
    unsigned hinted=nvn_texture_hint_enabled(),slot=nvn_texture_hint_slot(object);
    if(hinted) {
        unsigned index=nvn_texture_hints[slot].index;
        if(nvn_texture_hints[slot].object==object && index<4096 && nvn_textures[index].live) {
            uint32_t stamp=nvn_texture_hints[slot].stamp;
            if(!stamp && nvn_textures[index].object==object){nvn_texture_hint_hits++;return &nvn_textures[index];}
#ifdef MOMO_SHADERS
            /* A copy is only accepted while it still stamps the identity it was cached under; otherwise
             * this falls through to the scan exactly as an unknown object would. */
            if(stamp && (uint32_t)nvn_textures[index].id==stamp-1 && momo_texture_stamp_read(object)==stamp) {
                nvn_texture_hint_hits++;nvn_texture_hint_stamp_hits++;return &nvn_textures[index];
            }
#endif
        }
        nvn_texture_hint_misses++;
    }
    /* The high-water mark is an optimisation, never a truth: if the object is not found below it the
     * scan still covers the whole table before anyone concludes it does not exist. */
    unsigned live_end=__atomic_load_n(&nvn_textures_high,__ATOMIC_ACQUIRE);
    for(unsigned pass=0;pass<2;pass++) {
        unsigned end=pass?4096:live_end;
        for(unsigned i=pass?live_end:0;i<end;i++)if(nvn_textures[i].live&&nvn_textures[i].object==object) {
            if(hinted){nvn_texture_hints[slot].index=i;nvn_texture_hints[slot].object=object;nvn_texture_hints[slot].stamp=0;}
            return &nvn_textures[i];
        }
        if(live_end>=4096)break;
    }
#ifdef MOMO_SHADERS
    unsigned aliased_index=4096;
    struct nvn_texture *aliased=momo_texture_alias(object,&aliased_index);
    if(aliased) {
        if(hinted&&aliased_index<4096) {
            nvn_texture_hints[slot].index=aliased_index;nvn_texture_hints[slot].object=object;
            nvn_texture_hints[slot].stamp=(uint32_t)aliased->id+1;
        }
        return aliased;
    }
#endif
    stop("NVN_TEXTURE_INVALID",nvn_caller,object,24);return NULL;
}
#ifdef MOMO_SHADERS
/* The stamp lives in the copy itself, so it is read on every lookup: caching the mapping by address
 * would keep resolving a reused scratch object to the texture it held before. */
static struct nvn_texture *momo_texture_alias(uint64_t object,unsigned *index_out) {
    uint32_t stamped=momo_texture_stamp_read(object);
    if(!stamped)return NULL;
    for(unsigned i=0;i<4096;i++)if(nvn_textures[i].live&&(uint32_t)nvn_textures[i].id==stamped-1) {
        static unsigned reported;
        if(reported++<16)printf("MOMO_TEXTURE_ALIAS object=0x%"PRIx64" original=0x%"PRIx64" id=%"PRIu64" identity=host-stamp\n",
            object,nvn_textures[i].object,nvn_textures[i].id);
        if(index_out)*index_out=i;
        return &nvn_textures[i];
    }
    return NULL;
}
#endif
#if ZERO_ZX_GAME
static uint64_t zero_texture_record_id(uint64_t object) {return nvn_texture_find(object)->id;}
static void aw_clear_texture_admit(uint64_t object,uint64_t view,const int32_t *region) {
    struct nvn_texture *t=nvn_texture_find(object);
    if(view && !aw_texture_view_identity_native(nvn_pointer(view,MEGAMAN_TEXTURE_VIEW_BYTES,PROT_READ),&t->b,t->aw_native_format))stop("NVN_CLEAR_TEXTURE_VIEW_REQUIRED",nvn_caller,view,20);
    if(region[0]<0||region[1]<0||region[2]||region[3]<=0||region[4]<=0||region[5]!=1||
       (uint64_t)region[0]+region[3]>t->b.width||(uint64_t)region[1]+region[4]>t->b.height)stop("NVN_CLEAR_TEXTURE_REGION",nvn_caller,object,20);
}
static void zero_record_target_view(uint64_t object,const uint8_t *view,unsigned attachment) {
    struct nvn_texture *t=object?nvn_texture_find(object):NULL;
#ifdef MOMO_SHADERS
    /* Unity passes a zeroed view together with a null attachment when it detaches depth
     * (MEDIDO run momo-064): there is no image to validate the view against. */
    if(!object) {
        for(unsigned i=0;i<MEGAMAN_TEXTURE_VIEW_BYTES;i++)
            if(view[i])stop("MOMO_DETACHED_TARGET_VIEW",nvn_caller,attachment,24);
        return;
    }
#endif
    if(!t || !zero_render_target_view_identity(view,t->b.levels,t->b.depth,t->aw_native_format?t->aw_native_format:t->b.format,t->b.target)) {
        printf("ZERO_RENDER_TARGET_VIEW_REJECT attachment=%u object=0x%"PRIx64" format=0x%x target=%u levels=%u layers=%u view=",
               attachment,object,t?t->b.format:0,t?t->b.target:0,t?t->b.levels:0,t?t->b.depth:0);
        for(unsigned j=0;j<MEGAMAN_TEXTURE_VIEW_BYTES;j++)printf("%02x",view[j]);
        puts("");stop("ZERO_RENDER_TARGET_VIEW_REQUIRED",nvn_caller,object,20);
    }
    static unsigned reported;
    if(reported++<8)printf("ZERO_RENDER_TARGET_VIEW_IDENTITY attachment=%u object=0x%"PRIx64" flags=0x%x level=0 layer=0 copied=36\n",attachment,object,view[0]);
}
#endif
/* Keep the adapter's linear storage coherent with GPU copy commands. The
 * source stride is in guest bytes. Call before uploading the converted region.
 * R8 GPU targets cannot feed CPU views without a proven readback. R16 render
 * targets remain unsupported.
 */
/* Unity 2021 uploads each mip level with its own CopyBufferToTexture whose view selects the level
 * (MEDIDO run momo-053), so the CPU copy takes the destination level explicitly. */
static void nvn_texture_copy_cpu(struct nvn_texture *t,const uint32_t box[6],const uint8_t *src,uint64_t stride,unsigned level) {
    zero_texture_require_cpu(t->pool,t->b.offset,t->size);
    nvn_rt_storage_copy(t);
    t->composition_initial_uniform=0;
    unsigned bpp=nvn_texture_format_bytes(t->b.format);
    unsigned level_width=t->b.width>>level,level_height=t->b.height>>level;
    if(!level_width)level_width=1;
    if(!level_height)level_height=1;
    if(!bpp || nvn_texture_format_depth(t->b.format) || t->b.packaged_layout || level>=t->b.levels || box[2] || box[5]!=1 ||
       box[0]>level_width || box[3]>level_width-box[0] || box[1]>level_height || box[4]>level_height-box[1])
        stop("NVN_TEXTURE_CPU_COPY_CONTRACT",nvn_caller,t->id,20);
    uint64_t destination_stride=(t->b.flags&0x50 && !level)?t->b.stride:(uint64_t)level_width*bpp;
    for(unsigned i=0;i<4096;i++) {
        const struct nvn_texture *other=&nvn_textures[i];
        if(other->live&&other->packaged_lazy&&other->pool==t->pool&&
           t->b.offset<other->b.offset+other->size&&other->b.offset<t->b.offset+t->size)
            stop("NVN_LC2_PACKAGED_ALIAS_WRITE_REQUIRED",nvn_caller,other->id,20);
    }
    uint8_t *destination=nvn_pointer(t->pool->address+t->b.offset+t->levels[level],destination_stride*level_height,PROT_READ|PROT_WRITE);
    if(box[3] && box[4]) {
        uintptr_t source_begin=(uintptr_t)src,source_end=source_begin+stride*(box[4]-1)+(uint64_t)box[3]*bpp;
        uintptr_t destination_begin=(uintptr_t)destination+(uint64_t)box[1]*destination_stride+(uint64_t)box[0]*bpp;
        uintptr_t destination_end=destination_begin+destination_stride*(box[4]-1)+(uint64_t)box[3]*bpp;
        if(source_end<source_begin || destination_end<destination_begin || (source_begin<destination_end && destination_begin<source_end))
            stop("NVN_TEXTURE_CPU_COPY_ALIAS_REQUIRED",nvn_caller,t->id,20);
    }
    for(unsigned y=0;y<box[4];y++)memmove(destination+(box[1]+y)*destination_stride+(uint64_t)box[0]*bpp,src+y*stride,(size_t)box[3]*bpp);
    nvn_cpu_image_invalidate(t);
}
#define T2_PAGE_HOST_IMPL 1
#include "t2_page_substitution.h"   /* host section: deferral, copy interception, fallback, sampler filter */
/* Must run on the GL replay thread, after glActiveTexture. Returned view has
 * fixed nearest/base-level sampling, as TXF ignores the original sampler.
 * A byte comparison detects CPU writes as well as adapter copy commands.
 */
static GLuint nvn_texture_cpu_view_name(struct nvn_texture *t,unsigned view) {
    zero_texture_require_cpu(t->pool,t->b.offset,t->size);
    t2_page_require_rgba8(t,"T2_PAGE_CPU_VIEW_REQUIRED");
    if(t->aw_native_format)stop("AW_FLOAT_CPU_VIEW_UNSUPPORTED",nvn_caller,t->aw_native_format,20);
    unsigned width=nvn_texture_view_width(t->b.format,view,t->b.width),height=t->b.height;
    if(!width || !t->live || t->host_half || t->host_scaled || width>1024 || height>1024 || view<1 || view>3)
        stop("NVN_TEXTURE_CPU_VIEW_CONTRACT",nvn_caller,((uint64_t)t->b.format<<32)|view,20);
    unsigned bpp=nvn_texture_format_bytes(t->b.format);size_t row=(size_t)t->b.width*bpp,bytes=row*height;
    uint64_t stride=t->b.flags&0x50?t->b.stride:row;
    const uint8_t *raw=nvn_pointer(t->pool->address+t->b.offset,stride*height,PROT_READ);
    unsigned changed=!t->cpu_snapshot;
    if(!t->cpu_snapshot){t->cpu_snapshot=malloc(bytes);if(!t->cpu_snapshot)stop("NVN_TEXTURE_CPU_SNAPSHOT_MEMORY",nvn_caller,bytes,21);}
    for(unsigned y=0;y<height;y++) {
        if(!changed && memcmp(t->cpu_snapshot+y*row,raw+y*stride,row))changed=1;
        if(changed)memcpy(t->cpu_snapshot+y*row,raw+y*stride,row);
    }
    if(changed && !++t->cpu_generation)stop("NVN_TEXTURE_CPU_GENERATION_WRAP",nvn_caller,t->id,24);
    unsigned slot=view-1;
    if(!t->cpu_views[slot])glGenTextures(1,&t->cpu_views[slot]);
    glBindTexture(GL_TEXTURE_2D,t->cpu_views[slot]);
    if(t->view_generation[slot]!=t->cpu_generation) {
        size_t output_row=(size_t)width*(view==NVN_TEXTURE_VIEW_BGR8_TRIPLES?4U:2U),output_bytes=output_row*height;
        uint8_t *pixels=malloc(output_bytes);if(!pixels)stop("NVN_TEXTURE_CPU_VIEW_MEMORY",nvn_caller,output_bytes,21);
        for(unsigned y=0;y<height;y++)if(!nvn_texture_decode_view(t->b.format,view,pixels+y*output_row,t->cpu_snapshot+y*row,t->b.width))
            stop("NVN_TEXTURE_CPU_VIEW_DECODE",nvn_caller,t->b.format,20);
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width,height,0,GL_RGBA,nvn_texture_view_type(view),pixels);free(pixels);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        t->view_generation[slot]=t->cpu_generation;
    }
    return t->cpu_views[slot];
}
/* An uncached pool gets no FlushMappedRange, so nothing tells us the pixels changed and the sampled
 * texture keeps the bytes it had at creation (Infernax's 4x4 atlas: zero then, white now). Small
 * textures are cheap to verify: hash the guest rows and re-upload only when they differ. */
static void nvn_texture_small_reload(struct nvn_texture *t) {
    if(!t || !t->live || !t->pool || t->b.target!=1 || t->b.levels!=1)return;
    if(t->packaged_lazy || t->b.packaged_layout || t->host_scaled || t->host_half)return;
    uint64_t area=(uint64_t)t->b.width*t->b.height;
    {const char *e=getenv("NX_TEX_RELOAD_AREA");uint64_t lim=e?strtoull(e,NULL,0):4096;
     if(!area || area>lim)return;}
    unsigned bpp=nvn_texture_format_bytes(t->b.format);
    if(!bpp)return;
    uint64_t stride=(t->b.flags&0x50)?t->b.stride:(uint64_t)t->b.width*bpp;
    if(!stride)stride=(uint64_t)t->b.width*bpp;
    uint64_t bytes=stride*t->b.height;
    /* A bogus stride would ask to hash gigabytes and stall the whole frame. */
    if(!t->size || bytes>t->size || bytes>262144)return;
    const uint8_t *raw=nvn_pointer(t->pool->address+t->b.offset+t->levels[0],bytes,PROT_READ);
    uint64_t h=1469598103934665603ULL;
    for(uint64_t i=0;i<bytes;i++){h^=raw[i];h*=1099511628211ULL;}
    static struct {GLuint name;uint64_t hash;} seen[256];
    unsigned slot=(unsigned)(t->name&255);
    if(seen[slot].name==t->name && seen[slot].hash==h)return;
    unsigned type=nvn_cpu_image_upload_type(t),obpp=nvn_texture_upload_bytes(type);
    if(!obpp)return;
    size_t out_row=(size_t)t->b.width*obpp,out_bytes=out_row*t->b.height;
    uint8_t *pixels=malloc(out_bytes);if(!pixels)return;
    for(unsigned y=0;y<t->b.height;y++)
        if(!nvn_texture_decode_upload(t->b.format,t->b.swizzle,type,pixels+(size_t)y*out_row,raw+y*stride,t->b.width))
            {free(pixels);return;}
    /* Called from the sampler bind, where the caller already holds GL and has this texture bound. */
    GLint alignment;glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,t->b.width,t->b.height,GL_RGBA,type,pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);
    (void)glGetError();
    free(pixels);
    seen[slot].name=t->name;seen[slot].hash=h;
    {static unsigned relog;if(relog<32){relog++;printf("T2_TEX_RELOAD id=%"PRIu64" name=%u %ux%u fmt=0x%x first=%u,%u,%u,%u\n",
           t->id,t->name,t->b.width,t->b.height,t->b.format,raw[0],raw[1],raw[2],raw[3]);}}
}
#include "nvn_packaged_lazy.h"
#include "zero_sha256.h"
static void zero_cube_initialize(struct nvn_texture *t) {
    const uint8_t *source=nvn_pointer(t->pool->address+t->b.offset,t->size,PROT_READ);
    static const uint8_t expected[32]={142,255,1,7,29,163,133,249,165,207,47,120,57,86,140,71,31,192,133,57,11,154,119,92,81,82,84,173,30,0,160,247};uint8_t actual[32];
    zero_sha256(source,t->size,actual);
    if(t->b.format==0x44 && memcmp(expected,actual,32))stop("ZERO_CUBE_PAYLOAD_CHANGED",nvn_caller,t->id,24);
    unsigned type=nvn_cpu_image_upload_type(t);
    if(type==GL_FLOAT || type==0x8d61U || type==0x140bU) {
        const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
        if(!nvn_texture_float_backend_available(t->b.format,nvn_texture_context_major(),extensions))
            stop("ZERO_CUBE_FLOAT_BACKEND_REQUIRED",nvn_caller,t->b.format,20);
    }
    glGenTextures(1,&t->name);if(!t->name)stop("ZERO_CUBE_GL_NAME",nvn_caller,t->id,21);
    glBindTexture(GL_TEXTURE_CUBE_MAP,t->name);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    for(unsigned face=0;face<6;face++)for(unsigned level=0;level<t->b.levels;level++) {
        unsigned width=t->b.width>>level;if(!width)width=1;
        unsigned blocks=(width+3)/4;size_t linear_bytes=(size_t)blocks*blocks*16,rgba_bytes=(size_t)width*width*4;
        uint8_t linear[4096];size_t bytes=(size_t)width*width*nvn_texture_upload_bytes(type);
        uint8_t *rgba=malloc(bytes);if(!rgba)stop("ZERO_CUBE_UPLOAD_MEMORY",nvn_caller,bytes,21);
        const uint8_t *mip=source+face*(t->size/6)+t->levels[level];
        if(t->b.format!=0x44) {
            if(!nvn_texture_decode_upload(t->b.format,t->b.swizzle,type,rgba,mip,(size_t)width*width))stop("ZERO_CUBE_PIXEL_FORMAT",nvn_caller,t->b.format,20);
        }
        else if(!megaman_blocklinear_rgba8_decode(linear,linear_bytes,mip,0x2000-t->levels[level],blocks*4,blocks,0x11) ||
           !zero_bc_decode(0x44,rgba,rgba_bytes,linear,linear_bytes,width,width,blocks*16))
            stop("ZERO_CUBE_DECODE",nvn_caller,level,24);
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,level,nvn_texture_upload_internal(type,nvn_texture_context_major(),GL_RGBA),width,width,0,GL_RGBA,type,rgba);
        free(rgba);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_CUBE_MAP,0);
    GLenum error=glGetError();if(error)stop("ZERO_CUBE_UPLOAD",nvn_caller,error,21);
    printf("ZERO_CUBE_REAL faces=6 size=%u levels=%u format=0x%x payload=%"PRIu64" per-face=%"PRIu64"\n",t->b.width,t->b.levels,t->b.format,t->size,t->size/6);
}
static void zero_texture_pool_flushed(struct nvn_pool *pool,uint64_t offset,uint64_t bytes) {
    if(!bytes)return;
    for(unsigned i=0;i<4096;i++) {
        struct nvn_texture *t=&nvn_textures[i];
        if(t->live && t->pool==pool && offset<t->b.offset+t->size && t->b.offset<offset+bytes)
            __atomic_store_n(&zero_texture_storage_state(t)->zero_cpu_dirty,1,__ATOMIC_RELEASE);
    }
}
static void zero_texture_bc_pixels(struct nvn_texture *t,unsigned level,const uint32_t *box,void *pixels,size_t bytes) {
    struct zero_bc_level m;
    if(t->b.target!=1 || !zero_bc_level_layout(t->b.width,t->b.height,zero_bc_bytes(t->b.format),level,&m) ||
       m.offset!=t->levels[level] || m.offset>t->size || m.size>t->size-m.offset)
        stop("ZERO_BC_MIP_STORAGE",nvn_caller,t->id,24);
    unsigned x=box?box[0]/4:0,y=box?box[1]/4:0,width=box?box[3]:m.width,height=box?box[4]:m.height;
    unsigned nx=(width+3)/4,ny=(height+3)/4;size_t linear_bytes=(size_t)nx*ny*m.bytes;
    uint8_t *linear=malloc(linear_bytes);if(!linear)stop("ZERO_BC_LINEAR_MEMORY",nvn_caller,linear_bytes,21);
    void *source=nvn_pointer(t->pool->address+t->b.offset+m.offset,m.size,PROT_READ);
    if(!zero_bc_blocks_copy(&m,source,m.size,linear,linear_bytes,x,y,nx,ny,0) ||
       !zero_bc_decode(t->b.format,pixels,bytes,linear,linear_bytes,width,height,(size_t)nx*m.bytes))
        stop("ZERO_BC_TILED_DECODE",nvn_caller,t->id,24);
    free(linear);
}
static void zero_texture_refresh(struct nvn_texture *t) {
    t=zero_texture_storage_state(t);
    if(!__atomic_exchange_n(&t->zero_cpu_dirty,0,__ATOMIC_ACQ_REL))return;
    t2_page_require_rgba8(t,"T2_PAGE_CPU_REFRESH_REQUIRED");
    if(t->composition_target_seen || t->draws || t->depth_buffer || t->b.target!=1 ||
       t->host_scaled || t->host_half || t->packaged_lazy || t->b.packaged_layout)
        stop("ZERO_TEXTURE_CPU_GPU_COHERENCE_REQUIRED",nvn_caller,t->id,20);
    unsigned width=t->b.width,height=t->b.height,bc=zero_bc_bytes(t->b.format);
    unsigned type=nvn_cpu_image_upload_type(t),output_bpp=nvn_texture_upload_bytes(type);
    glBindTexture(GL_TEXTURE_2D,t->name);GLint alignment;glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    for(unsigned level=0;level<t->b.levels;level++) {
        uint64_t row=bc?(uint64_t)((width+3)/4)*bc:(uint64_t)width*nvn_texture_format_bytes(t->b.format);
        uint64_t stride=t->b.flags&0x50?t->b.stride:row;
        unsigned rows=bc?(height+3)/4:height;
        if(stride<row || t->levels[level]>t->size || stride*rows>t->size-t->levels[level])stop("ZERO_TEXTURE_REFRESH_RANGE",nvn_caller,t->id,24);
        const uint8_t *source=nvn_pointer(t->pool->address+t->b.offset+t->levels[level],stride*rows,PROT_READ);
        size_t bytes=(size_t)width*height*output_bpp;uint8_t *pixels=malloc(bytes);
        if(!pixels)stop("ZERO_TEXTURE_REFRESH_MEMORY",nvn_caller,bytes,21);
        if(bc) {
            zero_texture_bc_pixels(t,level,NULL,pixels,bytes);
        } else for(unsigned y=0;y<height;y++)if(!nvn_texture_decode_upload(t->b.format,t->b.swizzle,type,pixels+(size_t)y*width*output_bpp,source+y*stride,width))
            stop("ZERO_TEXTURE_REFRESH_FORMAT",nvn_caller,t->b.format,20);
        glTexSubImage2D(GL_TEXTURE_2D,level,0,0,width,height,zero_bc_srgb(t->b.format)?0x8c42U:GL_RGBA,type,pixels);free(pixels);
        if(width>1)width>>=1;
        if(height>1)height>>=1;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);
    GLenum error=glGetError();if(error)stop("ZERO_TEXTURE_REFRESH_GL",nvn_caller,error,21);
    printf("ZERO_TEXTURE_FLUSH_UPLOADED id=%"PRIu64" format=0x%x native-cpu-bytes=preserved before-sampler\n",t->id,t->b.format);
}
#include "zero_texture_writes.h"
static unsigned nvn_texture_dispatch(unsigned id,struct nvn_frame *f,uint64_t *result) {
    uint64_t *a=f->args;*result=0;
    if(id==NI_nvnTextureWriteTexels || id==NI_nvnTextureFlushTexels){zero_texture_write(f,id==NI_nvnTextureFlushTexels);return 1;}
    if(id==NI_nvnTextureViewSetDefaults || id==NI_nvnTextureViewSetLevels || id==NI_nvnTextureViewSetLayers ||
       id==NI_nvnTextureViewSetFormat || id==NI_nvnTextureViewSetSwizzle || id==NI_nvnTextureViewSetTarget ||
       id==NI_nvnTextureViewSetDepthStencilMode || id==NI_nvnTextureViewGetLevels || id==NI_nvnTextureViewGetLayers ||
       id==NI_nvnTextureViewGetTarget) {
        if(id==NI_nvnTextureViewGetLevels || id==NI_nvnTextureViewGetLayers){nvn_pointer(a[1],4,PROT_WRITE);nvn_pointer(a[2],4,PROT_WRITE);}
        if(id==NI_nvnTextureViewGetTarget)nvn_pointer(a[1],4,PROT_WRITE);
        nvn_pointer(a[0],MEGAMAN_TEXTURE_VIEW_BYTES,PROT_READ|PROT_WRITE);
        *result=nvn_original_helper(id,f);return 1;
    }
    switch(id) {
    case NI_nvnTextureBuilderSetDevice:case NI_nvnTextureBuilderSetDefaults:case NI_nvnTextureBuilderSetFlags:
    case NI_nvnTextureBuilderSetTarget:case NI_nvnTextureBuilderSetWidth:case NI_nvnTextureBuilderSetHeight:case NI_nvnTextureBuilderSetDepth:
    case NI_nvnTextureBuilderSetSize1D:case NI_nvnTextureBuilderSetSize2D:case NI_nvnTextureBuilderSetSize3D:
    case NI_nvnTextureBuilderSetLevels:case NI_nvnTextureBuilderSetFormat:case NI_nvnTextureBuilderSetSamples:
    case NI_nvnTextureBuilderSetSwizzle:case NI_nvnTextureBuilderSetDepthStencilMode:case NI_nvnTextureBuilderSetStorage:case NI_nvnTextureBuilderSetStride:
    case NI_nvnTextureBuilderSetPackagedTextureData:case NI_nvnTextureBuilderSetPackagedTextureLayout:
        if(id==NI_nvnTextureBuilderSetPackagedTextureLayout)nvn_pointer(a[1],4,PROT_READ);
        nvn_pointer(a[0],8+sizeof(struct nvn_texture_builder),PROT_READ|PROT_WRITE);*result=nvn_original_helper(id,f);return 1;
    default:break;
    }
    /* ZCull is Tegra depth-culling side storage. On this GLES2 host depth buffers are driver-owned
     * renderbuffers with no external save/restore allocation, so the requirement is zero bytes
     * (same answer as the-messenger-switch-study/native/nvn_textures.h:207-218 @ 90cbe57). */
    if(id==NI_nvnTextureGetZCullStorageSize) {
        (void)nvn_texture_find(a[0]);*result=0;return 1;
    }
    if(id==NI_nvnTextureBuilderGetZCullStorageSize) {
        struct nvn_texture_builder b;uint64_t levels[14]={0};
        (void)nvn_texture_layout(a[0],&b,levels);
        /* TERMINATOR (MEDIDO pc-005): Unity 6 budgets its swapchain pool as depth + ZCull + 3 colours
         * (4 x 0x7e9000 = pool 0x1fa4000 with ZCull answered 0), but its allocator still advances one
         * 4 KiB page for the ZCull block, so the third colour texture gets offset -1 (pool short by
         * 0x1000). A real Tegra never answers 0 here. Answer one page: budget and allocator agree.
         * INFERIDO prototype; T2_ZCULL_BYTES overrides (0 = the Messenger/Momodora answer). */
        static uint64_t zcull=UINT64_MAX;
        if(zcull==UINT64_MAX){const char *e=getenv("T2_ZCULL_BYTES");zcull=e?strtoull(e,NULL,0):0x1000;}
        *result=nvn_texture_format_depth(b.format)?zcull:0;return 1;
    }
    if(id==NI_nvnTextureBuilderGetStorageClass || id==NI_nvnTextureBuilderGetRawStorageClass) {
        struct nvn_texture_builder b;uint64_t levels[14]={0};nvn_texture_layout(a[0],&b,levels);
        if(b.packaged_layout)stop("ZERO_PACKAGED_STORAGE_CLASS_REQUIRED",nvn_caller,b.packaged_layout,20);
        /* SDK +0x405e7c: linear storage class 0x100. +0x405f90: raw class 1.
         * All currently accepted non-packaged adapter images use linear CPU
         * storage, including render targets backed by separate GLES objects. */
        *result=id==NI_nvnTextureBuilderGetStorageClass?0x100:zero_bc_bytes(b.format)?2:1;return 1;
    }
    if(id==NI_nvnTextureGetStorageClass || id==NI_nvnTextureGetRawStorageClass) {
        struct nvn_texture *t=nvn_texture_find(a[0]);
        if(t->b.packaged_layout)stop("ZERO_PACKAGED_STORAGE_CLASS_REQUIRED",nvn_caller,t->b.packaged_layout,20);
        *result=id==NI_nvnTextureGetStorageClass?0x100:zero_bc_bytes(t->b.format)?2:1;return 1;
    }
    if(id==NI_nvnTextureBuilderGetViewOffset) {
        /* Terminator 2D: ptrdiff_t nvnTextureBuilderGetViewOffset(builder, view) (INFERIDO from the NVN
         * naming; this main resolves the name but has no call site, docs/HOST.md 4). The answer is the
         * byte offset of the view's base level in the storage this host lays out; a view selecting a
         * layer other than 0 has no single-layer answer here and stops instead of guessing. */
        struct nvn_texture_builder b;uint64_t levels[14]={0};(void)nvn_texture_layout(a[0],&b,levels);
        unsigned level=0;
        if(a[1]) {
            const uint8_t *v=nvn_pointer(a[1],MEGAMAN_TEXTURE_VIEW_BYTES,PROT_READ);
            if(v[7]&MOMO_VIEW_FLAG_LEVELS)level=v[4];
            if(v[7]&MOMO_VIEW_FLAG_LAYERS){uint32_t layers;memcpy(&layers,v,4);if(layers&0xffffU)stop("T2_VIEW_OFFSET_LAYER_REQUIRED",nvn_caller,layers,20);}
        }
        if(level>=b.levels || level>=14)stop("T2_VIEW_OFFSET_LEVEL_RANGE",nvn_caller,level,24);
        static unsigned told;
        if(told++<4)printf("T2_TEXTURE_VIEW_OFFSET builder=0x%"PRIx64" view=0x%"PRIx64" level=%u offset=%"PRIu64" note=inferred-prototype\n",a[0],a[1],level,levels[level]);
        *result=levels[level];return 1;
    }
    if(id==NI_nvnTextureBuilderGetStorageSize || id==NI_nvnTextureBuilderGetStorageAlignment || id==NI_nvnTextureInitialize) {
        struct nvn_texture_builder b;uint64_t levels[14]={0},size=nvn_texture_layout(a[id==NI_nvnTextureInitialize],&b,levels);
        if(id!=NI_nvnTextureInitialize){*result=id==NI_nvnTextureBuilderGetStorageSize?size:4096;return 1;}
        struct nvn_pool *pool=nvn_pool_find(b.pool);nvn_pointer(a[0],8,PROT_READ|PROT_WRITE);
        if(b.offset%4096 || b.offset>pool->size || size>pool->size-b.offset || pool->flags&8)stop("NVN_TEXTURE_POOL_RANGE",nvn_caller,b.offset,24);
        nvn_gl_acquire();
        normal_guard_require_clear(pool->address+b.offset,size,nvn_caller);
        nvn_low_new_storage(pool,b.offset,size,&b,a[0]);
        unsigned slot=4096;
        for(unsigned i=0;i<4096;i++) {
            if(nvn_textures[i].live&&nvn_textures[i].object==a[0])stop("NVN_TEXTURE_ALREADY_LIVE",nvn_caller,a[0],24);
            if(!nvn_textures[i].live&&!nvn_textures[i].references&&slot==4096)slot=i;
        }
        if(slot==4096)stop("NVN_TEXTURE_CAPACITY",nvn_caller,slot,21);
        struct nvn_texture *t=&nvn_textures[slot];*t=(struct nvn_texture){.object=a[0],.id=zero_next_object_id(),.size=size,.b=b,.pool=pool};
        /* Frente F: an eligible page gets no GL storage now (the pool holds nothing yet, MEDIDO pc-008: the
         * copy arrives later) and no budget; the copy decides between the palettized form and RGBA8. */
        t->t2_page.deferred=t2_page_eligible(&b);
        t->t2_budget_bytes=t->t2_page.deferred?0:size;
        if(t->t2_budget_bytes)t2_texture_budget_add(size,nvn_caller);
        else if(t2_pages.deferred_logged++<4)printf("T2_PAGE_DEFERRED id=%"PRIu64" size=%ux%u rgba8-bytes=%"PRIu64" pool-flags=0x%x storage=none-until-copy\n",t->id,b.width,b.height,size,pool->flags);
#ifdef MOMO_SHADERS
        momo_texture_stamp(t->object,t->id);
#endif
        memcpy(t->levels,levels,sizeof(levels));
#ifdef ANIMAL_WELL_SHADERS
        if(aw_float_kind_of_format(b.format)) {
            t->aw_float_kind=aw_float_kind_of_format(b.format);t->aw_native_format=b.format;b.format=0x25;t->b.format=0x25;
            printf("AW_FLOAT_TEXTURE id=%"PRIu64" native-format=0x%x kind=%u host-storage=RGBA8 size=%ux%u range=%g\n",t->id,t->aw_native_format,t->aw_float_kind,b.width,b.height,aw_float_range());
        }
#endif
        /* Opt-in COTM_LOW_STORAGE: the 1280x720 window targets (color and depth)
         * only ever hold the 640x360 LOW raster, so allocate them at that size. */
        static int ls_configured;static unsigned ls_enabled;
        if(!ls_configured){const char *o=getenv("COTM_LOW_STORAGE");ls_enabled=o&&!strcmp(o,"1");ls_configured=1;if(ls_enabled)printf("NVN_LOW_STORAGE_EXPERIMENT enabled=1 window-targets=640x360-host-storage initial-data=none\n");}
        t->host_half=ls_enabled&&b.width==1280&&b.height==720&&b.levels==1;
        t->packaged_lazy=nvn_packaged_lazy_eligible(t);
        t->host_scaled=nvn_rt_storage_candidate(t);
        if(t->host_scaled){nvn_low_check_aliases(t);t->host_width=640;t->host_height=480;}
        else if(zero_render_scale_assign(t))nvn_low_check_aliases(t);
        t->host_upload_type=nvn_cpu_packed_candidate(t);
        unsigned shared_storage=zero_texture_storage_join(t);
        if(!shared_storage)zero_texture_require_cpu(pool,b.offset,size);
        const char *required_extension=zero_texture_half_channels(b.format)?"GL_OES_texture_half_float":
            (zero_texture_float_channels(b.format)||zero_texture_unorm16_channels(b.format)||zero_bc_float(b.format))?"GL_OES_texture_float":NULL;
        const char *available_extensions=(const char *)glGetString(GL_EXTENSIONS);
        t->zero_backend_deferred=!nvn_texture_float_backend_available(b.format,nvn_texture_context_major(),available_extensions);
        if(shared_storage) {
            printf("ZERO_TEXTURE_STORAGE_JOIN view=%"PRIu64" owner=%"PRIu64" format=0x%x logical=%ux%u actual=%ux%u texture=%u depth=%u native-content=preserved\n",
                t->id,zero_texture_storage_state(t)->id,b.format,b.width,b.height,t->host_width,t->host_height,t->name,t->depth_buffer);
        } else if(t->zero_backend_deferred) {
            /* Engine initialization creates unused default float images on
             * Mali too. Retain their exact native backing and descriptor;
             * refuse their first GPU use until a real backend is provided. */
            if(t->host_half||t->host_scaled||t->packaged_lazy||t->host_upload_type)
                stop("ZERO_DEFERRED_TEXTURE_OPTIMIZATION_CONFLICT",nvn_caller,b.format,24);
            printf("ZERO_TEXTURE_CPU_BACKING id=%"PRIu64" format=0x%x retained-bytes=%"PRIu64" missing=%s GPU-use=guarded\n",t->id,b.format,size,required_extension);
        } else if(b.target==8)zero_cube_initialize(t);
        else if(nvn_texture_format_depth(b.format)) {
            const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
            if(!extensions || !strstr(extensions,b.format==0x35?"GL_OES_packed_depth_stencil":"GL_OES_depth24"))stop("NVN_DEPTH_BACKEND_REQUIRED",nvn_caller,b.format,20);
            glGenRenderbuffers(1,&t->depth_buffer);glBindRenderbuffer(GL_RENDERBUFFER,t->depth_buffer);
            glRenderbufferStorage(GL_RENDERBUFFER,b.format==0x35?0x88f0:0x81a6,nvn_rt_storage_width(t),nvn_rt_storage_height(t));
            glBindRenderbuffer(GL_RENDERBUFFER,0);
            if(b.format==0x34)printf("NVN_DEPTH_CONVERSION guest=DEPTH32F host=DEPTH24 precision-reduced rendering-validation-pending\n");
        } else {
            if(zero_texture_half_channels(b.format) || zero_texture_float_channels(b.format) || zero_texture_unorm16_channels(b.format) || zero_bc_float(b.format)) {
                const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
                if(!nvn_texture_float_backend_available(b.format,nvn_texture_context_major(),extensions))
                    stop("ZERO_TEXTURE_FLOAT_BACKEND_REQUIRED",nvn_caller,b.format,20);
                if(t->host_half || t->host_scaled || t->packaged_lazy || t->host_upload_type)
                    stop("ZERO_FLOAT_TEXTURE_OPTIMIZATION_CONFLICT",nvn_caller,b.format,24);
            }
            if(zero_bc_srgb(b.format)) {
                const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
                if(!extensions || !strstr(extensions,"GL_EXT_sRGB"))stop("ZERO_SRGB_TEXTURE_BACKEND_REQUIRED",nvn_caller,b.format,20);
            }
            glGenTextures(1,&t->name);if(!t->name)stop("NVN_TEXTURE_GL_NAME",nvn_caller,t->id,21);glBindTexture(GL_TEXTURE_2D,t->name);
#if ZERO_ZX_GAME
            /* This backend exposes NVN's storage-level limit directly. Mali
             * lacks the extension and needs the separately tested mip-mask
             * translation before partial chains can be sampled there. */
            if(zero_mip_native_limit())
                glTexParameteri(GL_TEXTURE_2D,0x813d,b.levels-1);
#endif
            unsigned width=b.width,height=b.height;
            if(t->host_half){glPixelStorei(GL_UNPACK_ALIGNMENT,1);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width/2,height/2,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);}
            for(unsigned i=0;i<b.levels&&!t->host_half&&!t->packaged_lazy&&!t->t2_page.deferred;i++) {
                unsigned upload_type=nvn_cpu_image_upload_type(t),upload_bpp=nvn_texture_upload_bytes(upload_type);
                size_t bytes=(size_t)width*height*upload_bpp;uint8_t *rgba=malloc(bytes);
                if(!rgba)stop("NVN_TEXTURE_INITIAL_DATA_MEMORY",nvn_caller,bytes,21);
                unsigned bpp=nvn_texture_format_bytes(b.format);
                uint64_t stride=(b.flags&0x50)?b.stride:(uint64_t)width*bpp;
                unsigned bc=zero_bc_bytes(b.format);
                if(bc) {
                    zero_texture_bc_pixels(t,i,NULL,rgba,bytes);
                } else if(b.packaged_layout) {
                    const uint8_t *src=nvn_pointer(pool->address+b.offset,size,PROT_READ);
                    if(!megaman_blocklinear_rgba8_decode(rgba,bytes,src,size,width,height,b.packaged_layout))
                        stop("NVN_TEXTURE_PACKAGED_DECODE",nvn_caller,b.packaged_layout,20);
                } else {
                    const uint8_t *src=nvn_pointer(pool->address+b.offset+levels[i],stride*height,PROT_READ);
                    if(!i)nvn_texdiag_initialize_begin(t,src,stride);
                    for(unsigned y=0;y<height;y++)if(!nvn_texture_decode_upload(b.format,b.swizzle,upload_type,rgba+(size_t)y*width*upload_bpp,src+y*stride,width))
                        stop("NVN_TEXTURE_INITIAL_FORMAT",nvn_caller,b.format,20);
                    if((size_t)width*height<=16){
                        static unsigned tiny;if(tiny<16){tiny++;
                            printf("MOMO_TINY_TEX id=%"PRIu64" %ux%u fmt=0x%x bpp=%u stride=%"PRIu64" guest=%u,%u,%u,%u decoded=%u,%u,%u,%u\n",
                                   t->id,width,height,b.format,bpp,stride,src[0],src[1],src[2],src[3],rgba[0],rgba[1],rgba[2],rgba[3]);}}
                }
                if(upload_type==GL_UNSIGNED_BYTE)nvn_rt_storage_initial(t,rgba,bytes,i);
                /* Uniformity was checked across the complete original decode.
                 * The compact upload consumes the same constant pixel value. */
                glPixelStorei(GL_UNPACK_ALIGNMENT,1);
                unsigned format=zero_bc_srgb(b.format)?0x8c42U:GL_RGBA;
                unsigned internal=nvn_texture_upload_internal(upload_type,nvn_texture_context_major(),format);
                glTexImage2D(GL_TEXTURE_2D,i,internal,t->host_scaled?t->host_width:width,t->host_scaled?t->host_height:height,0,format,upload_type,rgba);
                if(zero_texture_half_channels(b.format)||zero_texture_float_channels(b.format)) {
                    GLenum error=glGetError();static unsigned reported;
                    if(reported<32||error){reported++;printf("INFERNAX_FLOAT_TEXTURE id=%"PRIu64" guest-format=0x%x level=%u size=%ux%u internal=0x%x external=0x%x type=0x%x gl-version=%s error=0x%x\n",t->id,b.format,i,width,height,internal,format,upload_type,(const char *)glGetString(GL_VERSION),(unsigned)error);}
                    if(error)stop("INFERNAX_FLOAT_TEXTURE_UPLOAD",nvn_caller,error,20);
                }
#if MEGAMAN_HOST_DIAGNOSTIC
                /* Observation remains an independent RGBA8 reference; packed
                 * UNORM5 may truthfully differ by one readback code value. */
                if(upload_type!=GL_UNSIGNED_BYTE) {
                    struct nvn_texdiag_entry *e=nvn_texdiag_find(t->id);
                    if(e) {
                        uint8_t *reference=malloc((size_t)width*height*4);
                        if(!reference)stop("NVN_TEXDIAG_MEMORY",nvn_caller,bytes,21);
                        for(unsigned y=0;y<height;y++)if(!nvn_texture_decode_rgba(b.format,b.swizzle,reference+(size_t)y*width*4,e->initial+y*e->stride,width))
                            stop("NVN_TEXDIAG_DECODE",nvn_caller,b.format,24);
                        nvn_texdiag_initialize_end(t,i,reference);free(reference);
                    }
                } else
#endif
                nvn_texdiag_initialize_end(t,i,rgba);
                free(rgba);
                if(width>1)width>>=1;
                if(height>1)height>>=1;
            }
#if ZERO_ZX_GAME
            zero_mip_complete_storage(t,zero_bc_srgb(b.format)?0x8c42U:GL_RGBA,nvn_cpu_image_upload_type(t));
#endif
            nvn_texture_parameters(t,GL_NEAREST,GL_NEAREST,GL_CLAMP_TO_EDGE,GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D,0);
        }
        if(!shared_storage)zero_texture_storage_publish(t);
        nvn_texture_alias_epoch_advance();
        t->live=1;
        {   unsigned slot=(unsigned)(t-nvn_textures);
            if(slot<4096 && slot+1>__atomic_load_n(&nvn_textures_high,__ATOMIC_RELAXED))
                __atomic_store_n(&nvn_textures_high,slot+1,__ATOMIC_RELEASE);
        }
        pool->references++;nvn_gl_release();
        /* MOMO_TEXTURE_IDENTITY: pool+offset of every texture, so two render targets of the same size can
         * be told apart from two names for one piece of memory (the scene lands in one target while the post
         * chain reads another — MEDIDO run momo-111). */
        if(getenv("MOMO_TEXTURE_IDENTITY"))
            printf("MOMO_TEXTURE_IDENTITY id=%"PRIu64" object=0x%"PRIx64" pool=0x%"PRIx64" offset=0x%"PRIx64" bytes=%"PRIu64" size=%ux%u format=0x%x flags=0x%x levels=%u target=%u\n",
                t->id,a[0],b.pool,b.offset,size,b.width,b.height,b.format,b.flags,b.levels,b.target);
        printf("NVN_TEXTURE_REAL object=0x%"PRIx64" format=0x%x size=%ux%u levels=%u bytes=%"PRIu64" texture=%u depth=%u gpu-storage=%s no-draw\n",a[0],b.format,b.width,b.height,b.levels,size,t->name,t->depth_buffer,t->zero_backend_deferred?"unavailable-native-backing-retained":t->packaged_lazy?"deferred":"allocated");
        if(t->host_scaled)printf("NVN_LC2_SCALED_STORAGE id=%"PRIu64" texture=%u depth=%u flags=0x%x pool=0x%x logical=%ux%u actual=%ux%u initial=%s\n",
            t->id,t->name,t->depth_buffer,b.flags,pool->flags,b.width,b.height,t->host_width,t->host_height,
            shared_storage?"existing-GPU-owner":t->name?"full-decode-uniform":"native-depth-uninitialized");
        if(t->host_upload_type)printf("NVN_LC2_CPU_PACKED_TEXTURE id=%"PRIu64" texture=%u format=0x%x upload-type=0x%x size=%ux%u stride=%u bytes-per-pixel=2\n",
            t->id,t->name,b.format,t->host_upload_type,b.width,b.height,b.stride);
        *result=1;return 1;
    }
    if(id==NI_nvnTexturePoolRegisterTexture) {
        struct nvn_descriptor_pool *d=NULL;
        for(unsigned i=0;i<32;i++)if(nvn_descriptor_pools[i].live&&nvn_descriptor_pools[i].object==a[0]&&nvn_descriptor_pools[i].type==1)d=&nvn_descriptor_pools[i];
        if(!d || a[1]<NVN_DESCRIPTOR_RESERVED || a[1]>=d->count)stop("NVN_TEXTURE_DESCRIPTOR_INDEX",nvn_caller,a[1],24);
        struct nvn_texture *t=nvn_texture_find(a[2]);struct nvn_descriptor *slot=&d->entries[a[1]];
        if(a[3] && !megaman_texture_view_identity(nvn_pointer(a[3],MEGAMAN_TEXTURE_VIEW_BYTES,PROT_READ),t->b.levels) &&
           !aw_texture_view_identity_native(nvn_pointer(a[3],MEGAMAN_TEXTURE_VIEW_BYTES,PROT_READ),&t->b,t->aw_native_format))
            stop("NVN_TEXTURE_DESCRIPTOR_VIEW_REQUIRED",nvn_caller,a[3],20);
        if(slot->valid) {
            unsigned old=0;while(old<4096&&nvn_textures[old].id!=slot->object)old++;
            if(old==4096||!nvn_textures[old].references)stop("NVN_TEXTURE_DESCRIPTOR_STALE",nvn_caller,slot->object,24);
            nvn_textures[old].references--;
        }
        unsigned generation=slot->generation+1;if(!generation)stop("NVN_TEXTURE_GENERATION_WRAP",nvn_caller,a[1],24);
        if(getenv("MOMO_DESCRIPTOR_TRACE"))
            printf("MOMO_DESCRIPTOR index=%"PRIu64" pool=0x%"PRIx64" id=%"PRIu64" object=0x%"PRIx64" size=%ux%u generation=%u\n",
                a[1],a[0],t->id,t->object,t->b.width,t->b.height,generation);
        *slot=(struct nvn_descriptor){.object=t->id,.valid=1,.generation=generation};t->references++;return 1;
    }
    /* Unity 2021 also asks for separate handles (MEDIDO run momo-055): the texture handle carries the
     * texture index in the low word and the sampler handle the sampler index in the high word, so the
     * pair combines into exactly the same value as nvnDeviceGetTextureHandle. */
    if(id==NI_nvnDeviceGetSeparateTextureHandle || id==NI_nvnDeviceGetSeparateSamplerHandle) {
        if(a[0]!=nvn_device.address || a[1]<NVN_DESCRIPTOR_RESERVED || a[1]>=NVN_DESCRIPTOR_MAX)
            stop("NVN_TEXTURE_HANDLE_INDICES",nvn_caller,a[1],24);
        *result=id==NI_nvnDeviceGetSeparateTextureHandle?a[1]:(a[1]<<32);return 1;
    }
    if(id==NI_nvnDeviceGetTextureHandle) {
        if(a[0]!=nvn_device.address || a[1]<NVN_DESCRIPTOR_RESERVED || a[1]>=NVN_DESCRIPTOR_MAX || a[2]<NVN_DESCRIPTOR_RESERVED || a[2]>=NVN_DESCRIPTOR_MAX)
            stop("NVN_TEXTURE_HANDLE_INDICES",nvn_caller,a[1],24);
        *result=a[1]|(a[2]<<32);return 1;
    }
    switch(id) {
    case NI_nvnTextureGetWidth:case NI_nvnTextureGetHeight:case NI_nvnTextureGetDepth:case NI_nvnTextureGetFormat:case NI_nvnTextureGetLevels:
    case NI_nvnTextureGetFlags:case NI_nvnTextureGetTarget:case NI_nvnTextureGetSamples:case NI_nvnTextureGetDepthStencilMode:
    case NI_nvnTextureGetMemoryPool:case NI_nvnTextureGetMemoryOffset:case NI_nvnTextureGetStorageSize:case NI_nvnTextureGetTextureAddress:
    case NI_nvnTextureGetDebugID:case NI_nvnTextureSetDebugLabel:case NI_nvnTextureFinalize:break;
    default:return 0;
    }
    struct nvn_texture *t=nvn_texture_find(a[0]);
    if(id==NI_nvnTextureFinalize) {
        /* Descriptors keep the old identity so a later stale draw is rejected;
         * they do not keep freed guest storage alive. GL completes/deletes the
         * actual resource, while a replacement receives a new identity. */
        /* ZERO_FINALIZE_NO_FINISH=1 (13/09): the GBA layer of Zero 1 creates and
         * finalizes ~8 textures per frame; a glFinish per Finalize serializes the
         * GPU 8x per frame. GL defers the actual deletion until the GPU is done. */
        static int nf_configured;static unsigned nf_enabled;
        if(!nf_configured){const char *o=getenv("ZERO_FINALIZE_NO_FINISH");nf_enabled=o&&!strcmp(o,"1");nf_configured=1;
            if(nf_enabled)printf("ZERO_FINALIZE_NO_FINISH enabled=1 finalize=glFlush deletion=driver-deferred\n");}
        nvn_gl_acquire();if(nf_enabled)glFlush();else glFinish();nvn_framebuffer_forget(t->id);
        nvn_texdiag_finalize(t);
        struct nvn_texture *storage=zero_texture_storage_state(t);
        unsigned delete_storage=zero_texture_storage_release(t);
        if(delete_storage) {
            zero_depth_framebuffer_forget(storage->id);
            if(storage->name)glDeleteTextures(1,&storage->name);
            glDeleteTextures(2,storage->zero_mip_masks);
            if(storage->depth_buffer)glDeleteRenderbuffers(1,&storage->depth_buffer);
            for(unsigned i=0;i<3;i++)if(storage->cpu_views[i])glDeleteTextures(1,&storage->cpu_views[i]);
            free(storage->cpu_snapshot);storage->cpu_snapshot=NULL;
        }
        t->cpu_snapshot=NULL;
        memset(t->zero_mip_masks,0,sizeof(t->zero_mip_masks));memset(t->cpu_views,0,sizeof(t->cpu_views));
        t->depth_constant_known=t->depth_discarded=0;t->zero_depth_discarded=0;if(storage)storage->zero_depth_discarded=0;
        if(t->normal_guarded){normal_cpu_restore(t->id,nvn_caller);t->normal_guarded=t->normal_unmaterialized=0;}
        nvn_texture_alias_epoch_advance();
        t2_page_finalize(t);
        t->name=0;t->depth_buffer=0;t->live=0;nvn_gl_release();
        if(t->t2_budget_bytes)t2_texture_budget_sub(t->t2_budget_bytes,nvn_caller);
        t->t2_budget_bytes=0;
        if(!t->pool->references)stop("NVN_TEXTURE_POOL_REFERENCE",nvn_caller,a[0],24);
        if(!--t->pool->references)t->pool->retired=0;
        return 1;
    }
    switch(id) {
    case NI_nvnTextureGetWidth:*result=t->b.width;break;
    case NI_nvnTextureGetHeight:*result=t->b.height;break;
    case NI_nvnTextureGetDepth:*result=t->b.depth;break;
    case NI_nvnTextureGetFormat:*result=t->aw_native_format?t->aw_native_format:t->b.format;break;
    case NI_nvnTextureGetLevels:*result=t->b.levels;break;
    case NI_nvnTextureGetFlags:*result=t->b.flags;break;
    case NI_nvnTextureGetTarget:*result=t->b.target;break;
    case NI_nvnTextureGetSamples:*result=t->b.samples;break;
    case NI_nvnTextureGetDepthStencilMode:*result=t->b.depth_mode;break;
    case NI_nvnTextureGetMemoryPool:*result=t->b.pool;break;
    case NI_nvnTextureGetMemoryOffset:*result=t->b.offset;break;
    case NI_nvnTextureGetStorageSize:*result=t->size;break;
    case NI_nvnTextureGetTextureAddress:normal_guard_require_clear(t->pool->address+t->b.offset,t->size,nvn_caller);*result=t->pool->address+t->b.offset;break;
    case NI_nvnTextureGetDebugID:*result=t->id;break;
    case NI_nvnTextureSetDebugLabel:nvn_debug_label(a[1]);break;
    default:break;
    }
    return 1;
}
