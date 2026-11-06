/* MOMO_SWIZZLE_FLAT=1 (measurement, then a rule): replace the body of the generated
 * `ms_apply_swizzle` with `return c;`.
 *
 * Why: the ESSL lowering routes EVERY texture2D of EVERY shader through a helper that asks five
 * comparisons per channel - twenty per sample - guarded by a vec4 equality against the identity. The
 * bloom prefilter samples thirteen times a pixel, so that helper is paid thirteen times over 230400
 * pixels. On this driver a comparison chain in a fragment shader is not free: the same shape cost 163 ms
 * of a 606 ms frame when it was used for the R8 unswizzle (runs momo-190/196).
 *
 * Safety: this is only correct while every sampled image arrives with the identity selectors, which is
 * what the CPU decode guarantees (it bakes the swizzle into the uploaded RGBA8). A GPU-written image
 * keeps its native storage and needs the real selectors - so with this on, the draw path STOPS the host
 * the moment a non-identity selector is required, rather than drawing the wrong colour quietly. */
#ifndef MOMO_SWIZZLE_FLATTEN_H
#define MOMO_SWIZZLE_FLATTEN_H
#include <stdlib.h>
#include <string.h>

static unsigned momo_swizzle_flat_enabled(void) {
    static int configured;static unsigned enabled;
    if(!configured){const char *o=getenv("MOMO_SWIZZLE_FLAT");enabled=o&&!strcmp(o,"1");configured=1;
        if(enabled)printf("MOMO_SWIZZLE_FLAT enabled=1 body=return-c non-identity-selector=host-stops\n");}
    return enabled;
}
static char *momo_swizzle_flat_source(const char *source) {
    static const char body[]=
        " if(all(equal(s,vec4(2.0,3.0,4.0,5.0))))return c;\n"
        " return vec4(ms_channel(c,s.x),ms_channel(c,s.y),ms_channel(c,s.z),ms_channel(c,s.w));\n";
    const char *at=strstr(source,body);
    if(!at)return NULL;   /* the catalogue no longer emits the shape this rewrites */
    static const char replacement[]=" return c;\n";
    size_t head=(size_t)(at-source),tail=strlen(at+sizeof(body)-1);
    char *out=malloc(head+sizeof(replacement)-1+tail+1);
    if(!out)return NULL;
    memcpy(out,source,head);
    memcpy(out+head,replacement,sizeof(replacement)-1);
    memcpy(out+head+sizeof(replacement)-1,at+sizeof(body)-1,tail+1);
    return out;
}
#endif
