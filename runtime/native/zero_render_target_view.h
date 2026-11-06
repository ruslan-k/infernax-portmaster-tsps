/* Original Zero/ZX SDK setters at 0x406040..0x4061d0. Render attachments
 * select one image, unlike a sampling view. Admit only the same 2D image at
 * level/layer zero; format and target overrides must preserve their identity.
 * The caller copies all 36 bytes into the command stream before returning. */
#ifndef ZERO_RENDER_TARGET_VIEW_H
#define ZERO_RENDER_TARGET_VIEW_H
#include <stdint.h>
#include <string.h>
#include "megaman_texture_view.h"
static inline unsigned zero_render_target_view_identity(const uint8_t *view,
        unsigned levels,unsigned layers,unsigned format,unsigned target) {
    if(!view)return 1;
#ifdef MOMO_SHADERS
    /* SDK 18.3.2 layout (16 bytes, see megaman_texture_view.h). Attachments select one image. */
    (void)target;
    if(layers!=1)return 0;
    return momo_texture_view_identity(view,levels,format);
#else
    /* Animal Well SDK 17.5.3 view: flags at [7] (1 levels, 2 layers, 4 format, 8 swizzle, 0x10 depth-stencil, 0x20 target),
     * level base/count at [0..2], layer base/count at [3..5], target at [6], format at [8..11], swizzle [12..15], ds [16]. */
    if(view[7] && !(view[7]&~0x3fU)) {
        uint32_t vformat;memcpy(&vformat,view+8,4);
        if(!levels || layers!=1 || target!=1)return 0;
        if(view[0] || !((view[1]==0&&view[2]==1)||(view[1]==1&&view[2]==0)))return 0;
        if(view[3] || !((view[4]==0&&view[5]==1)||(view[4]==1&&view[5]==0)))return 0;
        if((view[7]&0x20) && view[6]!=target)return 0;
        if((view[7]&4) && vformat!=format)return 0;
        if((view[7]&8) && (view[12]!=2||view[13]!=3||view[14]!=4||view[15]!=5))return 0;
        if(view[16])return 0;
        for(unsigned i=17;i<36;i++)if(view[i])return 0;
        return 1;
    }
    if(!levels || layers!=1 || target!=1 || (view[0]&~0x27U))return 0;
    uint8_t expected[36]={0};expected[0]=view[0];
    if(view[0]&1)expected[2]=1;
    if(view[0]&2)expected[6]=1;
    if(view[0]&4)memcpy(expected+8,&format,4);
    if(view[0]&32)memcpy(expected+32,&target,4);
    return !memcmp(view,expected,36);
#endif
}
#endif
