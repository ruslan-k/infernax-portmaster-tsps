/* NVN BC block formats -> GLES upload pixels. Dependency and license are
 * pinned in vendor/bcdec/PIN.json. Bounds/layout belong to this adapter. */
#ifndef ZERO_BC_H
#define ZERO_BC_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define BCDECDEF static inline
#define BCDEC_BC4BC5_PRECISE
#define BCDEC_IMPLEMENTATION
#include "vendor/bcdec/bcdec.h"
static inline unsigned zero_bc_bytes(unsigned format) {
    if(format<0x41 || format>0x50)return 0;
    return (format==0x41 || format==0x42 || format==0x45 || format==0x46 || format==0x49 || format==0x4a)?8U:16U;
}
static inline unsigned zero_bc_float(unsigned format) {
    return (format>=0x49 && format<=0x4c) || format==0x4f || format==0x50;
}
static inline unsigned zero_bc_srgb(unsigned format) {
    return (format>=0x45 && format<=0x48) || format==0x4e;
}
/* Signed char is unsigned by default on this AArch64 toolchain. Keep signed
 * BC4/5 endpoint interpretation explicit without changing the vendored header. */
static inline void zero_bc_signed_channel(const uint64_t *input,float *output,unsigned step) {
    const uint8_t *bytes=(const uint8_t *)input;float p[8];
    int a=(int8_t)bytes[0],b=(int8_t)bytes[1];if(a< -127)a=-127;if(b< -127)b=-127;
    p[0]=a/127.0f;p[1]=b/127.0f;
    if(a>b)for(unsigned i=2;i<8;i++)p[i]=((8-i)*p[0]+(i-1)*p[1])/7.0f;
    else {for(unsigned i=2;i<6;i++)p[i]=((6-i)*p[0]+(i-1)*p[1])/5.0f;p[6]=-1;p[7]=1;}
    uint64_t bits=*input>>16;for(unsigned i=0;i<16;i++){output[i*step]=p[bits&7];bits>>=3;}
}
static inline unsigned zero_bc_decode(unsigned format,void *destination,size_t destination_bytes,
                                      const void *source,size_t source_bytes,unsigned width,unsigned height,size_t stride) {
    unsigned block_bytes=zero_bc_bytes(format),floating=zero_bc_float(format),pixel_bytes=floating?16:4;
    if(!block_bytes || !source || !destination || !width || !height || width>4096 || height>4096)return 0;
    unsigned nx=(width+3)/4,ny=(height+3)/4;
    if(stride<(size_t)nx*block_bytes || stride>64U*1024U*1024U ||
       source_bytes<(ny-1)*stride+(size_t)nx*block_bytes || destination_bytes<(size_t)width*height*pixel_bytes)return 0;
    for(unsigned y=0;y<ny;y++)for(unsigned x=0;x<nx;x++) {
        /* memcpy provides the alignment required by the original decoder. */
        uint64_t input[2]={0};memcpy(input,(const uint8_t *)source+y*stride+x*block_bytes,block_bytes);
        uint32_t rgba[16];float channel[16*3]={0},fp[16][4];
        unsigned normalized=format>=0x45&&format<=0x48?format-4:format;
        if(normalized==0x41 || normalized==0x42)bcdec_bc1(input,rgba,16);
        else if(normalized==0x43)bcdec_bc2(input,rgba,16);
        else if(normalized==0x44)bcdec_bc3(input,rgba,16);
        else if(format==0x4d || format==0x4e)bcdec_bc7(input,rgba,16);
        else if(format==0x49)bcdec_bc4_float(input,channel,4,0);
        else if(format==0x4a)zero_bc_signed_channel(input,channel,1);
        else if(format==0x4b)bcdec_bc5_float(input,channel,8,0);
        else if(format==0x4c){zero_bc_signed_channel(input,channel,2);zero_bc_signed_channel(input+1,channel+1,2);}
        else if(format==0x4f || format==0x50)bcdec_bc6h_float(input,channel,12,format==0x4f);
        else return 0;
        if(normalized==0x41)for(unsigned i=0;i<16;i++)((uint8_t *)rgba)[i*4+3]=255;
        if(floating) {
            unsigned channels=format<=0x4a?1:format<=0x4c?2:3;
            for(unsigned i=0;i<16;i++) {
                fp[i][0]=channel[i*channels];fp[i][1]=channels>1?channel[i*channels+1]:0;
                fp[i][2]=channels>2?channel[i*channels+2]:0;fp[i][3]=1;
            }
        }
        for(unsigned dy=0;dy<4 && y*4+dy<height;dy++)for(unsigned dx=0;dx<4 && x*4+dx<width;dx++) {
            const void *pixel=floating?(const void *)fp[dy*4+dx]:(const void *)&rgba[dy*4+dx];
            memcpy((uint8_t *)destination+((size_t)(y*4+dy)*width+x*4+dx)*pixel_bytes,pixel,pixel_bytes);
        }
    }
    return 1;
}
#endif
