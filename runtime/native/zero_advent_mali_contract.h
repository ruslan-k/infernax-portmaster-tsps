/* Precision characterization is specific to these original bytes. Admission
 * also requires zero_advent_mali_draw.h on every draw; identity alone cannot
 * establish the discrete alpha or palette-index domains. */
#ifndef ZERO_ADVENT_MALI_CONTRACT_H
#define ZERO_ADVENT_MALI_CONTRACT_H
#include <stddef.h>
#include <stdint.h>
#include <math.h>
struct zero_advent_mali_pin {unsigned index;const char *code,*control,*source;};
static const struct zero_advent_mali_pin zero_advent_mali_pins[]={
    {93,"74b8bd4fe5ed6499b015ebd8ccd8b99d2ce398497de3645608ad000224906a2a","b9ad35830d85a9d92ff434eecc67404f6b0a326f5ce684f6cd00a0749f0417a2","ca6ba9b7f219f54fcd92563400bbea13068d17d3e7b00dc04776f443d01fa98f"},
    {163,"c61581092e36c09f8401746a3709c50ce1be0c9163dc053310a791992cee4ede","9cb0dc1a93cc924b69d16e061037ae5c3442f56ac82df6ec890ea56a06117933","b30f0429023b82b485dc35736f3f54bbbbe1022c925064807da697ff87ca8960"}
};
static inline const struct zero_advent_mali_pin *zero_advent_mali_pin(unsigned index) {
    for(size_t i=0;i<sizeof(zero_advent_mali_pins)/sizeof(zero_advent_mali_pins[0]);i++)
        if(zero_advent_mali_pins[i].index==index)return zero_advent_mali_pins+i;
    return NULL;
}
/* The tested source vertices forward attr1.w exactly. Keep their complete
 * native code/control/source identities, including duplicate program slots. */
static const struct zero_advent_mali_pin zero_advent_vertex_pins[]={
    {12,"bf6c578085fff6a12cb4f4b4fd74b8348f7d39a77926267c09511dd7824251db","8610d0e0faefca439f31542fe263679cd7a9f09135a73d82bcc382d34ec02174","b2764d99ac5c4d917e78892b5351b619402fb21feb2fd76a21e12956c23e9b09"},
    {54,"bf6c578085fff6a12cb4f4b4fd74b8348f7d39a77926267c09511dd7824251db","43326d820a9572cc1ae33244d38674c94271339cab90905bba22a3d23f60afce","b2764d99ac5c4d917e78892b5351b619402fb21feb2fd76a21e12956c23e9b09"},
    {82,"bf6c578085fff6a12cb4f4b4fd74b8348f7d39a77926267c09511dd7824251db","204ee34d77672c125e10ce45c06ff9cb32b21d4d7ea8329100ed4b9964a4ac0e","b2764d99ac5c4d917e78892b5351b619402fb21feb2fd76a21e12956c23e9b09"}
};
#if ZERO_ZX_SHADER_PACKAGES
static inline unsigned zero_advent_mali_pin_matches(const struct zero_advent_mali_pin *pin) {
    if(!pin)return 0;
#ifdef TERMINATOR2D
    return 0;   /* the pins index the Zero/ZX catalogue, not this title's */
#else
    const struct mm_shader_binding *m=&nvn_megaman_shader_bindings[pin->index];
    const char *source=nvn_shader_sources[pin->index].gles;uint8_t hash[32];
    if(!source)return 0;
    zero_sha256(source,strlen(source),hash);
    return zero_shader_hash_equal(m->code_hash,pin->code) && zero_shader_hash_equal(m->control_hash,pin->control) &&
           zero_shader_hash_equal(hash,pin->source);
#endif
}
static inline unsigned zero_advent_mali_admitted(unsigned index) {
    return zero_advent_mali_pin_matches(zero_advent_mali_pin(index));
}
static inline unsigned zero_advent_mali_vertex_admitted(unsigned index) {
    for(size_t i=0;i<sizeof(zero_advent_vertex_pins)/sizeof(zero_advent_vertex_pins[0]);i++)
        if(zero_advent_vertex_pins[i].index==index)return zero_advent_mali_pin_matches(zero_advent_vertex_pins+i);
    return 0;
}
#endif
static inline unsigned zero_advent_mali_constants(float threshold,float opacity,
                                                 const float rect[8],unsigned width,unsigned height) {
    if(threshold!=1.0f/255.0f || opacity!=1.0f || !width || !height || width>1024 || height>1024)return 0;
    /* Half-pixel boundaries remain exact at these native target sizes. */
    for(unsigned i=0;i<8;i++)if(!isfinite(rect[i]) || fabsf(rect[i])>1024.0f || floorf(rect[i]*2.0f)!=rect[i]*2.0f)return 0;
    return 1;
}
static inline unsigned zero_advent_mali_color(const float rgba[4]) {
    if(rgba[3]!=1.0f)return 0;
    for(unsigned i=0;i<3;i++)if(!isfinite(rgba[i]) || rgba[i]<0.0f || rgba[i]>1.25f)return 0;
    return 1;
}
static inline unsigned zero_advent_mali_texel_offset(const float position_uv[4],unsigned width,unsigned height,
                                                     unsigned texture_width,unsigned texture_height,float result[2]) {
    if(!width || !height || !texture_width || !texture_height || texture_width>1024 || texture_height>1024 ||
       (texture_width&(texture_width-1)) || (texture_height&(texture_height-1)))return 0;
    for(unsigned i=0;i<4;i++)if(!isfinite(position_uv[i]))return 0;
    result[0]=position_uv[2]*texture_width-(position_uv[0]+1.0f)*width*.5f;
    result[1]=position_uv[3]*texture_height-(1.0f-position_uv[1])*height*.5f;
    return isfinite(result[0]) && isfinite(result[1]) && fabsf(result[0])<=1024 && fabsf(result[1])<=1024 &&
           floorf(result[0])==result[0] && floorf(result[1])==result[1];
}
/* Input is decoded RGBA8, not the original BGRA storage order. Complete
 * source rows are examined; accepting only the first visible texel is unsafe. */
static inline unsigned zero_advent_mali_nibbles(const uint8_t *rgba,size_t pixels) {
    if(!rgba || !pixels || pixels>SIZE_MAX/4)return 0;
    for(size_t i=0;i<pixels;i++)if(rgba[i*4+1]>15 || rgba[i*4+2]>15)return 0;
    return 1;
}
#endif
