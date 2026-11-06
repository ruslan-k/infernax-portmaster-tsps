/* LC2 host composition geometry; logical guest dimensions remain unchanged. */
#ifndef MEGAMAN_COMPOSITION_H
#define MEGAMAN_COMPOSITION_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
static int64_t megaman_composition_floor(int64_t n,int64_t d) {
    int64_t q=n/d;return q-(n%d<0);
}
static int64_t megaman_composition_ceil(int64_t n,int64_t d) {
    return -megaman_composition_floor(-n,d);
}
static unsigned megaman_composition_rect(const int32_t in[4],int32_t out[4]) {
    if(in[2]<0 || in[3]<0)return 0;
    const int64_t numerator[2]={1,4},denominator[2]={3,9};
    for(unsigned axis=0;axis<2;axis++) {
        int64_t start=megaman_composition_floor((int64_t)in[axis]*numerator[axis],denominator[axis]);
        int64_t end=megaman_composition_ceil(((int64_t)in[axis]+in[axis+2])*numerator[axis],denominator[axis]);
        if(!in[axis+2])end=start;
        if(start<INT32_MIN || start>INT32_MAX || end-start>INT32_MAX)return 0;
        out[axis]=(int32_t)start;out[axis+2]=(int32_t)(end-start);
    }
    return 1;
}
static unsigned megaman_composition_uniform_rgba(const uint8_t *rgba,size_t bytes) {
    if(!rgba || bytes<4 || bytes%4)return 0;
    for(size_t i=4;i<bytes;i+=4)if(memcmp(rgba,rgba+i,4))return 0;
    return 1;
}
#endif
