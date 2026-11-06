/* SDK 4.4.1 TextureView: Defaults at 2e8d14 zeroes exactly 36 bytes;
 * SetLevels at 2e8d24 sets flag bit 0 and stores byte base/count at 1/2.
 * Only views identical to the complete single-level texture are admitted.
 * Other level ranges, formats, layers and swizzles need actual conversion.
 */
#ifndef MEGAMAN_TEXTURE_VIEW_H
#define MEGAMAN_TEXTURE_VIEW_H
#include <stdint.h>
#include <string.h>
#ifdef MOMO_SHADERS
/* NintendoSDK 18.3.2 NVNtextureView, MEDIDO in the sdk NSO: SetDefaults (0x4766d0) zeroes exactly
 * 16 bytes; SetLevels (0x4766e0) writes base at byte 4, count at byte 5 and flag bit 56; SetLayers
 * (0x476710) writes base/count in bits 0..31 and flag bit 57; SetFormat (0x476730) writes u32 at +8
 * and flag bit 58; SetSwizzle (0x476750) writes bytes 0xc..0xf and flag bit 59; SetDepthStencilMode
 * (0x476770) writes bits 62..63 and flag bit 60. The 17.5.3 36-byte layout does not apply. */
#define MEGAMAN_TEXTURE_VIEW_BYTES 16U
#define MOMO_VIEW_FLAG_LEVELS 0x01U
#define MOMO_VIEW_FLAG_LAYERS 0x02U
#define MOMO_VIEW_FLAG_FORMAT 0x04U
#define MOMO_VIEW_FLAG_SWIZZLE 0x08U
#define MOMO_VIEW_FLAG_DEPTH_STENCIL 0x10U
#define MOMO_VIEW_FLAG_TARGET 0x20U         /* SetTarget (sdk 0x476540): target byte at [6], flag bit 61 */
/* The whole single-level 2D image, optionally restated: any other range needs a real conversion. */
static inline unsigned momo_texture_view_identity_target(const uint8_t *v,unsigned levels,unsigned format,unsigned target) {
    if(!v)return 1;
    unsigned flags=v[7];
    if(flags&~0x3fU)return 0;
    if((flags&MOMO_VIEW_FLAG_TARGET) && target && v[6]!=target)return 0;
    if((flags&MOMO_VIEW_FLAG_LEVELS) && (v[4] || (v[5]!=1 && v[5]!=0) || levels!=1))return 0;
    if((flags&MOMO_VIEW_FLAG_LAYERS)) {
        uint32_t layers;memcpy(&layers,v,4);
        if((layers&0xffffU) || ((layers>>16)!=1 && (layers>>16)!=0))return 0;
    }
    if((flags&MOMO_VIEW_FLAG_FORMAT)) {
        uint32_t vformat;memcpy(&vformat,v+8,4);
        if(format && vformat!=format)return 0;
    }
    if((flags&MOMO_VIEW_FLAG_SWIZZLE) && (v[12]!=2||v[13]!=3||v[14]!=4||v[15]!=5))return 0;
    if((!(flags&MOMO_VIEW_FLAG_TARGET) && v[6]) || (v[7]&0xc0U))return 0;   /* depth-stencil mode bits stay clear */
    if(!(flags&MOMO_VIEW_FLAG_LEVELS) && (v[4]||v[5]))return 0;
    return 1;
}
/* A copy destination selects one level of the image; level 0 of a mipped texture is still the
 * base image the host stores, so the level count of the texture itself is not constrained. */
static inline unsigned momo_texture_view_single_level(const uint8_t *v,unsigned format,unsigned target) {
    if(!v)return 1;
    if((v[7]&MOMO_VIEW_FLAG_LEVELS) && v[5]!=1 && v[5]!=0)return 0;
    uint8_t single[MEGAMAN_TEXTURE_VIEW_BYTES];
    memcpy(single,v,sizeof(single));single[4]=0;   /* the base level is validated by the caller */
    return momo_texture_view_identity_target(single,1,format,target);
}
static inline unsigned momo_texture_view_identity(const uint8_t *v,unsigned levels,unsigned format) {
    return momo_texture_view_identity_target(v,levels,format,0);
}
static inline unsigned megaman_texture_view_identity(const uint8_t *view,unsigned levels) {
    return momo_texture_view_identity_target(view,levels,0,0);
}
#else
#define MEGAMAN_TEXTURE_VIEW_BYTES 36U
static inline unsigned megaman_texture_view_identity(const uint8_t *view,unsigned levels) {
    if(!view)return 1;
    if(levels!=1 || (view[0]!=0 && view[0]!=1) || view[1] || view[2]!=(view[0]?1:0))return 0;
    for(unsigned i=3;i<MEGAMAN_TEXTURE_VIEW_BYTES;i++)if(view[i])return 0;
    return 1;
}
#endif
#endif
