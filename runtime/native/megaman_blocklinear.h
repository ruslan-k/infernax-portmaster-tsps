#ifndef MEGAMAN_BLOCKLINEAR_H
#define MEGAMAN_BLOCKLINEAR_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* LC2 SDK 4.4.1 packaged 2D RGBA8, one level, identity channels only.
 * The caller validates format 0x25, target 1, depth 1, levels 1 and swizzle.
 * layout is the builder halfword, not the XTX metadata word: 1 | (h << 4).
 * SDK 0x2e8cc0 -> 0x2e9190 -> 0x2d455c -> 0x312b30 proves h is log2
 * GOB height; small levels clamp h as in SDK 0x312c44. GOBs are 64B x 8.
 * Independent address-layout reference: Mesa NIL TegraColor, commit
 * 398d45f61fc3dbe5d723362d603bedf4d604a2db; see docs/blocklinear-textures.md.
 * No GL calls, allocation, source writes, mip generation, or color conversion.
 */
static inline unsigned megaman_blocklinear_height(unsigned height,unsigned layout) {
    unsigned h=(layout>>4)&15U;
    while(h && height<=(8U<<(h-1U)))h--;
    return h;
}

static inline unsigned megaman_blocklinear_rgba8_size(unsigned width,unsigned height,
        unsigned layout,size_t *storage) {
    unsigned h=(layout>>4)&15U;
    if(!storage || !width || !height || width>4096U || height>4096U || h>4U ||
            layout!=(1U|(h<<4)))return 0;
    h=megaman_blocklinear_height(height,layout);
    size_t rows=8U<<h;
    *storage=(((size_t)width*4U+63U)&~(size_t)63U)*
        (((size_t)height+rows-1U)&~(rows-1U));
    return 1;
}

static inline unsigned megaman_blocklinear_rgba8_decode(void *destination,size_t destination_size,
        const void *source,size_t source_size,unsigned width,unsigned height,unsigned layout) {
    size_t storage;
    if(!megaman_blocklinear_rgba8_size(width,height,layout,&storage))return 0;
    size_t stride=(size_t)width*4U,linear_size=stride*height;
    if(!destination || !source || source_size<storage || destination_size<linear_size)return 0;
    uintptr_t dst_address=(uintptr_t)destination,src_address=(uintptr_t)source;
    if(dst_address>UINTPTR_MAX-linear_size || src_address>UINTPTR_MAX-storage)return 0;
    if(dst_address<src_address+storage && src_address<dst_address+linear_size)return 0;
    unsigned h=megaman_blocklinear_height(height,layout),rows=8U<<h;
    size_t block_size=(size_t)512U<<h,blocks_x=(stride+63U)/64U;
    unsigned char *dst=(unsigned char *)destination;
    const unsigned char *src=(const unsigned char *)source;
    for(unsigned y=0;y<height;y++) {
        size_t row_base=(size_t)(y/rows)*blocks_x*block_size+
            (size_t)((y/8U)&((1U<<h)-1U))*512U+
            (size_t)(y&6U)*32U+(size_t)(y&1U)*16U;
        for(size_t x=0;x<stride;x+=16U) {
            size_t offset=row_base+(x/64U)*block_size+(x&32U)*8U+(x&16U)*2U;
            size_t bytes=stride-x<16U?stride-x:16U;
            memcpy(dst+(size_t)y*stride+x,src+offset,bytes);
        }
    }
    return 1;
}

#endif
