/* LC2 SDK 4.4 format identities, from the owner's pinned SDK image.
 * Table SDK+0x99f2b8, stride 0x34: RGBA bits +0x0a..0x0d,
 * depth/stencil bits +0x0e/+0x0f, storage bytes +0x10.
 * Pure CPU functions shared by initialization, copies and derived TXF views.
 * No packaged/block-linear interpretation is implied by this decoder.
 */
#ifndef NVN_MEGAMAN_TEXTURE_FORMATS_H
#define NVN_MEGAMAN_TEXTURE_FORMATS_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "zero_bc.h"

enum nvn_texture_cpu_view {
    NVN_TEXTURE_VIEW_NONE=0,
    NVN_TEXTURE_VIEW_BGR5551=1,
    NVN_TEXTURE_VIEW_BGRA4444=2,
    NVN_TEXTURE_VIEW_BGR8_TRIPLES=3
};
static inline unsigned nvn_texture_format_bytes(unsigned format) {
    switch(format) {
    case 1:return 1; /* R8_UNORM */
    case 0x0d:return 2; /* RG8_UNORM */
    case 6:return 2; /* R16_UNORM */
    case 5:return 2; /* R16F */
    case 0x11:return 4; /* RG16F */
    case 0x29:return 8; /* RGBA16F */
    case 0x0a:return 4; /* R32F */
    case 0x16:return 8; /* RG32F */
    case 0x2e:return 16; /* RGBA32F */
    case 0x12:return 4; /* RG16_UNORM */
    case 0x2a:return 8; /* RGBA16_UNORM */
    case 0x39:return 2; /* A4B4G4R4_UNORM: R in the low nibble */
    case 0x97:return 2; /* BGR5A1_UNORM: B low, R bits10..14, A bit15 */
    case 0x25:case 0x9a:return 4; /* RGBA8_UNORM, BGRA8_UNORM */
    case 0x34:case 0x35:return 4; /* D32F, D24_UNORM_S8_UINT */
    /* R11G11B10F: Unity's HDR camera/bloom target (MEDIDO run momo-065). GLES2 on Mali-450 has no
     * float render target, so the host keeps the same 4 bytes per pixel and stores it as RGBA8: the
     * range above 1.0 is lost. Registered as a deviation in the HANDOFF. */
    case 0x3f:return 4;
    default:return 0;
    }
}
static inline unsigned zero_texture_half_channels(unsigned format) {
    return format==5?1U:format==0x11?2U:format==0x29?4U:0U;
}
static inline unsigned zero_texture_unorm16_channels(unsigned format) {
    return format==0x12?2U:format==0x2a?4U:0U;
}
static inline unsigned zero_texture_float_channels(unsigned format) {
    return format==0x0a?1U:format==0x16?2U:format==0x2e?4U:0U;
}
static inline unsigned nvn_texture_format_depth(unsigned format) {
    return format==0x34?1U:format==0x35?2U:0U;
}
/* NVN swizzle values: 0 ZERO, 1 ONE, 2 R, 3 G, 4 B, 5 A. */
static inline unsigned nvn_texture_swizzle_supported(unsigned format,const uint32_t swizzle[4]) {
    unsigned identity=1,constant_or_red=1,constant_or_rg=1,any_channel=1;
    for(unsigned i=0;i<4;i++){identity&=swizzle[i]==i+2;constant_or_red&=swizzle[i]<=2;constant_or_rg&=swizzle[i]<=3;any_channel&=swizzle[i]<=5;}
    /* A single-channel image can be expanded into RGBA8 for any per-channel choice of ZERO/ONE/R,
     * which is what Unity's font and mask textures use (e.g. ONE,ONE,ONE,R; MEDIDO run momo-032). */
    /* Two-channel images expand the same way for any choice of ZERO/ONE/R/G (MEDIDO run momo-033). */
    /* Four-channel 8-bit images are materialized as RGBA8 by the host, so any per-channel choice of
     * ZERO/ONE/R/G/B/A can be applied while decoding (Unity 2021 uses them, MEDIDO run momo-051). */
    return identity || (format==1 && constant_or_red) || ((format==6 || format==0x0d) && constant_or_rg) ||
           ((format==0x25 || format==0x9a) && any_channel);
}
/* R16's base image preserves bytes in R/G. It is NOT normalized R16 color:
 * ordinary TEX sampling must be refused, or bind a separately proven view.
 * This representation is retained for the highp v3 reference only.
 */
static inline unsigned nvn_texture_decode_rgba(unsigned format,const uint32_t swizzle[4],
                                               uint8_t *dst,const uint8_t *src,size_t pixels) {
    if(!nvn_texture_swizzle_supported(format,swizzle))return 0;
    if(format!=1 && format!=6 && format!=0x0d && format!=0x25 && format!=0x39 && format!=0x97 && format!=0x9a)return 0;
    for(size_t i=0;i<pixels;i++,dst+=4) {
        if(format==1) {
            uint8_t red=*src++;
            for(unsigned c=0;c<4;c++)dst[c]=swizzle[c]==2?red:(swizzle[c]==1?255:0);
        }
        else if(format==6 || format==0x0d) {
            uint8_t channel[2];channel[0]=*src++;channel[1]=*src++;
            for(unsigned c=0;c<4;c++)dst[c]=swizzle[c]>=2?channel[swizzle[c]-2]:(swizzle[c]==1?255:0);
        }
        else if(format==0x39){
            /* SDK table 0x39 texture word 0x2c692492 selects NVIDIA
             * A4B4G4R4, four UNORM channels and identity swizzles.
             * n/15 == (17*n)/255, so RGBA8 preserves normalization. */
            dst[0]=(uint8_t)((src[0]&15U)*17U);
            dst[1]=(uint8_t)((src[0]>>4)*17U);
            dst[2]=(uint8_t)((src[1]&15U)*17U);
            dst[3]=(uint8_t)((src[1]>>4)*17U);src+=2;
        }
        else if(format==0x97){
            /* SDK texture word 0x2a712494: A1B5G5R5 hardware with
             * B/G/R/A swizzles. Round each 5-bit UNORM to nearest
             * RGBA8 (error <= 15/(31*255)); alpha remains exact. */
            unsigned word=src[0]|((unsigned)src[1]<<8);
            dst[0]=(uint8_t)((((word>>10)&31U)*255U+15U)/31U);
            dst[1]=(uint8_t)((((word>>5)&31U)*255U+15U)/31U);
            dst[2]=(uint8_t)(((word&31U)*255U+15U)/31U);
            dst[3]=(uint8_t)((word>>15)*255U);src+=2;
        }
        else {
            uint8_t channel[4];
            channel[0]=src[format==0x9a?2:0];channel[1]=src[1];channel[2]=src[format==0x9a?0:2];channel[3]=src[3];src+=4;
            for(unsigned c=0;c<4;c++)dst[c]=swizzle[c]>=2?channel[swizzle[c]-2]:(swizzle[c]==1?255:0);
        }
    }
    return 1;
}
/* Ordinary CPU images may keep their native channel precision. These GL
 * packed words put R at the high end; guest words are little-endian. */
static inline unsigned nvn_texture_packed_type(unsigned format) {
    return format==0x39?0x8033U:format==0x97?0x8034U:0U;
}
static inline unsigned nvn_texture_upload_bytes(unsigned type) {
    return type==0x1401U?4U:type==0x1406U?16U:(type==0x8d61U||type==0x140bU)?8U:(type==0x8033U||type==0x8034U)?2U:0U;
}
#define NVN_TEXTURE_CORE_FLOAT_POLICY 1
static inline unsigned nvn_texture_gles_major(const char *version) {
    const char prefix[]="OpenGL ES ";unsigned major=0;
    if(!version||strncmp(version,prefix,sizeof(prefix)-1))return 0;
    version+=sizeof(prefix)-1;
    while(*version>='0'&&*version<='9'){major=major*10U+(unsigned)(*version-'0');version++;}
    return *version=='.'?major:0;
}
static inline unsigned nvn_texture_has_extension(const char *extensions,const char *name) {
    if(!extensions||!name||!*name)return 0;
    size_t n=strlen(name);const char *p=extensions;
    while((p=strstr(p,name))!=NULL) {
        if((p==extensions||p[-1]==' ')&&(p[n]==' '||!p[n]))return 1;
        p+=n;
    }
    return 0;
}
static inline unsigned nvn_texture_float_backend_available(unsigned format,unsigned major,const char *extensions) {
    const char *required=zero_texture_half_channels(format)?"GL_OES_texture_half_float":
        (zero_texture_float_channels(format)||zero_texture_unorm16_channels(format)||zero_bc_float(format))?"GL_OES_texture_float":NULL;
    return !required||major>=3||nvn_texture_has_extension(extensions,required);
}
static inline unsigned nvn_texture_upload_gl_type(unsigned type,unsigned major) {
    return type==0x8d61U&&major>=3?0x140bU:type;
}
static inline unsigned nvn_texture_upload_internal(unsigned type,unsigned major,unsigned fallback) {
    if(major>=3) {
        if(type==0x140bU||type==0x8d61U)return 0x881aU; /* RGBA16F */
        if(type==0x1406U)return 0x8814U; /* RGBA32F */
    }
    return fallback;
}
static inline unsigned nvn_texture_decode_upload(unsigned format,const uint32_t swizzle[4],unsigned type,
                                                 uint8_t *dst,const uint8_t *src,size_t pixels) {
    if(type==0x1401U) {
#if ZERO_ZX_GAME
        /* Preserve absent attachment channels even after GPU writes. The
         * native all-red swizzle is applied by zero_r8_channels at sampling. */
        if(format==1) {
            const uint32_t identity[4]={2,3,4,5};
            if(!nvn_texture_swizzle_supported(format,swizzle))return 0;
            return nvn_texture_decode_rgba(format,identity,dst,src,pixels);
        }
#endif
        return nvn_texture_decode_rgba(format,swizzle,dst,src,pixels);
    }
    if(type==0x1406U && zero_texture_unorm16_channels(format)) {
        unsigned channels=zero_texture_unorm16_channels(format);
        for(unsigned c=0;c<4;c++)if(swizzle[c]!=c+2)return 0;
        for(size_t i=0;i<pixels;i++,src+=channels*2,dst+=16) {
            uint16_t values[4]={0};float rgba[4]={0,0,0,1};memcpy(values,src,channels*2);
            for(unsigned c=0;c<channels;c++)rgba[c]=values[c]/65535.0f;
            memcpy(dst,rgba,16);
        }
        return 1;
    }
    if(type==0x1406U) {
        unsigned channels=zero_texture_float_channels(format);
        if(!channels)return 0;
        for(unsigned c=0;c<4;c++)if(swizzle[c]!=c+2)return 0;
        for(size_t i=0;i<pixels;i++,src+=channels*4,dst+=16) {
            uint32_t values[4]={0,0,0,0x3f800000};
            memcpy(values,src,channels*4);memcpy(dst,values,16);
        }
        return 1;
    }
    if(type==0x8d61U||type==0x140bU) {
        unsigned channels=zero_texture_half_channels(format);
        if(!channels)return 0;
        for(unsigned c=0;c<4;c++)if(swizzle[c]!=c+2)return 0;
        for(size_t i=0;i<pixels;i++,src+=channels*2,dst+=8) {
            uint16_t values[4]={0,0,0,0x3c00};
            /* Preserve all supplied IEEE binary16 bits, including subnormals,
             * signed zero and NaNs. Missing components are exactly 0/0/1. */
            memcpy(values,src,channels*2);memcpy(dst,values,8);
        }
        return 1;
    }
    if(!type||type!=nvn_texture_packed_type(format))return 0;
    for(unsigned c=0;c<4;c++)if(swizzle[c]!=c+2)return 0;
    for(size_t i=0;i<pixels;i++,src+=2,dst+=2) {
        unsigned word=src[0]|((unsigned)src[1]<<8);
        uint16_t out=format==0x39?
            (uint16_t)(((word&0x000fU)<<12)|((word&0x00f0U)<<4)|((word&0x0f00U)>>4)|(word>>12)):
            (uint16_t)((word<<1)|(word>>15));
        memcpy(dst,&out,sizeof(out));
    }
    return 1;
}
/* Returned GL token is GL_UNSIGNED_SHORT_5_5_5_1 (0x8034),
 * GL_UNSIGNED_SHORT_4_4_4_4 (0x8033), or GL_UNSIGNED_BYTE (0x1401).
 * Repacking preserves each original component exactly; no RGBA8 quantization
 * is inserted for the 5/4-bit formats. GL consumes native-endian uint16_t.
 */
static inline unsigned nvn_texture_view_type(unsigned view) {
    return view==NVN_TEXTURE_VIEW_BGR5551?0x8034U:
           view==NVN_TEXTURE_VIEW_BGRA4444?0x8033U:
           view==NVN_TEXTURE_VIEW_BGR8_TRIPLES?0x1401U:0U;
}
static inline unsigned nvn_texture_view_width(unsigned format,unsigned view,unsigned width) {
    if(format==6 && (view==NVN_TEXTURE_VIEW_BGR5551 || view==NVN_TEXTURE_VIEW_BGRA4444))return width;
    if(format==1 && view==NVN_TEXTURE_VIEW_BGR8_TRIPLES && width%3==0)return width/3;
    return 0;
}
static inline unsigned nvn_texture_decode_view(unsigned format,unsigned view,uint8_t *dst,
                                               const uint8_t *src,unsigned original_width) {
    unsigned width=nvn_texture_view_width(format,view,original_width);
    if(!width)return 0;
    for(unsigned x=0;x<width;x++) {
        if(view==NVN_TEXTURE_VIEW_BGR8_TRIPLES){dst[0]=src[2];dst[1]=src[1];dst[2]=src[0];dst[3]=255;src+=3;dst+=4;}
        else {
            uint16_t word=(uint16_t)src[0]|((uint16_t)src[1]<<8),out;
            if(view==NVN_TEXTURE_VIEW_BGR5551)
                out=(uint16_t)(((word&0x7c00U)<<1)|((word&0x03e0U)<<1)|((word&0x001fU)<<1)|(word>>15));
            else out=(uint16_t)(((word&0x0fffU)<<4)|(word>>12));
            memcpy(dst,&out,sizeof(out));src+=2;dst+=2;
        }
    }
    return 1;
}
#endif
