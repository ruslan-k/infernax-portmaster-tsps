/* Nintendo DSP ADPCM: 8 encoded bytes -> 14 signed PCM samples, two-tap
 * prediction with signed Q11 coefficients, rounding and saturating history.
 * Independently implemented; pinned public vgmstream decoder is the test oracle.
 */
#ifndef COTM_AUDREN_DSP_H
#define COTM_AUDREN_DSP_H
#include <stddef.h>
#include <stdint.h>
static uint64_t zero_dsp_required_bytes(uint32_t samples) {
    if(!samples)return 0;
    uint64_t last=(uint64_t)samples-1;
    return last/14*8+2+(last%14)/2;
}
static int cotm_dsp_sample(const uint8_t *encoded,size_t bytes,uint32_t position,
                          const int16_t coefficients[16],int16_t history[2],int16_t *output) {
    size_t block=(size_t)(position/14)*8;unsigned part=position%14;
    /* A final DSP frame can end after its last used nibble. Zero/ZX sends
     * 36262 samples in20722 bytes:2590 complete frames plus header+one byte.
     * Read only the header and the packed byte containing this sample. */
    if(block>bytes||bytes-block<2+part/2)return 0;
    unsigned header=encoded[block],predictor=header>>4;
    if(predictor>=8)return 0;
    unsigned packed=encoded[block+1+part/2],bits=part&1?packed&15:packed>>4;
    int delta=(int)bits-(bits>=8?16:0);
    int64_t sum=(int64_t)delta*(1U<<(header&15))*2048+
        (int64_t)coefficients[predictor*2]*history[0]+(int64_t)coefficients[predictor*2+1]*history[1]+1024;
    /* Defined floor division for negative predictions; no signed-left-shift UB. */
    int64_t sample=sum>=0?sum/2048:-((-sum+2047)/2048);
    if(sample>32767)sample=32767;
    if(sample<-32768)sample=-32768;
    history[1]=history[0];history[0]=(int16_t)sample;*output=(int16_t)sample;return 1;
}
#endif
