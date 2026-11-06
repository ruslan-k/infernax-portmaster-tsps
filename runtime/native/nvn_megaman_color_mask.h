/* The pinned FP06 is ClearDepthShader with NVIDIA SPH OmapTarget == 0.
 * It has no color export; GLES color writes must be disabled, while the draw's
 * rasterized depth and stencil operations still run. No invented fragment RGBA.
 * All other programs retain the existing undefined-component rejection.
 */
#ifndef NVN_MEGAMAN_COLOR_MASK_H
#define NVN_MEGAMAN_COLOR_MASK_H
static inline unsigned nvn_megaman_color_mask(unsigned fragment_source,
        unsigned declared_mask,unsigned requested_mask,unsigned *effective_mask) {
    if(!effective_mask || (requested_mask&~15U) || (declared_mask&~15U))return 0;
    if(fragment_source==6) {
        if(declared_mask)return 0;
        *effective_mask=0;return 1;
    }
    if(requested_mask&~declared_mask)return 0;
    *effective_mask=requested_mask;return 1;
}
#endif
