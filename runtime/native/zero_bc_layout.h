/* Zero/ZX SDK6.5: 2D, non-sparse, non-MSAA BC storage. Default GOB height
 * follows SDK4304c0; each mip extent follows SDK430550. Tests execute these
 * original arithmetic helpers independently, without NVN/device initialization.
 * Address order is TegraColor GOB64x8; no data/format conversion occurs here. */
#ifndef ZERO_BC_LAYOUT_H
#define ZERO_BC_LAYOUT_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
struct zero_bc_level {unsigned width,height,blocks_x,blocks_y,bytes,h;size_t stride,size,offset;};
static inline unsigned zero_bc_default_height(unsigned blocks_y) {
    unsigned n=blocks_y+blocks_y/2;
    return n>127?4:n>=64?3:n>31?2:n>15?1:0;
}
static inline unsigned zero_bc_level_layout(unsigned width,unsigned height,unsigned bytes,unsigned level,
                                            struct zero_bc_level *out) {
    if(!out || !width || !height || width>4096 || height>4096 || (bytes!=8&&bytes!=16) || level>12)return 0;
    unsigned base_h=zero_bc_default_height((height+3)/4);size_t offset=0;
    for(unsigned i=0;;i++) {
        unsigned nx=(width+3)/4,ny=(height+3)/4,h=base_h;
        while(h && ny<=(8U<<(h-1)))h--;
        size_t rows=8U<<h,stride=((size_t)nx*bytes+63)&~(size_t)63;
        size_t size=stride*((ny+rows-1)&~(rows-1));
        if(i==level){*out=(struct zero_bc_level){width,height,nx,ny,bytes,h,stride,size,offset};return 1;}
        if(width==1 && height==1)return 0;
        offset+=size;if(width>1)width>>=1;if(height>1)height>>=1;
    }
}
static inline size_t zero_bc_block_address(const struct zero_bc_level *m,unsigned x,unsigned y) {
    size_t byte_x=(size_t)x*m->bytes,block_size=(size_t)512<<m->h;
    return (y/(8U<<m->h))*(m->stride/64)*block_size+(byte_x/64)*block_size+
        ((y/8)&((1U<<m->h)-1))*512+(byte_x&32)*8+(y&6)*32+(byte_x&16)*2+(y&1)*16+(byte_x&15);
}
/* Coordinates are compressed blocks, pitches/lengths are bytes. Padding in the
 * tiled destination is untouched. Caller owns overlap-safe source staging. */
static inline unsigned zero_bc_blocks_copy(const struct zero_bc_level *m,void *tiled,size_t tiled_bytes,
        void *linear,size_t linear_bytes,unsigned x,unsigned y,unsigned width,unsigned height,unsigned write) {
    if(!m || !tiled || !linear || !width || !height || write>1 || tiled_bytes<m->size ||
       x>m->blocks_x || width>m->blocks_x-x || y>m->blocks_y || height>m->blocks_y-y ||
       linear_bytes<(size_t)width*height*m->bytes)return 0;
    size_t bytes=(size_t)width*height*m->bytes;uintptr_t ta=(uintptr_t)tiled,la=(uintptr_t)linear;
    if(ta>UINTPTR_MAX-m->size || la>UINTPTR_MAX-bytes || (ta<la+bytes && la<ta+m->size))return 0;
    for(unsigned dy=0;dy<height;dy++)for(unsigned dx=0;dx<width;dx++) {
        uint8_t *a=(uint8_t *)tiled+zero_bc_block_address(m,x+dx,y+dy);
        uint8_t *b=(uint8_t *)linear+((size_t)dy*width+dx)*m->bytes;
        if(write)memcpy(a,b,m->bytes);else memcpy(b,a,m->bytes);
    }
    return 1;
}
#endif
