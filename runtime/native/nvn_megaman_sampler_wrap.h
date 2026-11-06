/* LC2 sampler enum mapping. Legacy CLAMP equals EDGE only for nearest
 * texel filtering (OpenGL 3.0 table 3.22), including coordinates at 1.
 * Do not extend this reduction to linear/border or mipmapped filtering. */
#ifndef NVN_MEGAMAN_SAMPLER_WRAP_H
#define NVN_MEGAMAN_SAMPLER_WRAP_H
static inline unsigned nvn_megaman_sampler_wrap(unsigned mode,unsigned min,unsigned mag) {
    if(mode==1)return 0x2901U; /* REPEAT */
    if(mode==6)return 0x8370U; /* MIRRORED_REPEAT */
    if(mode==7)return 0x812fU; /* CLAMP_TO_EDGE */
    if(mode==0 && min==0 && mag==0)return 0x812fU; /* nearest CLAMP */
    return 0;
}
#endif
