/* Keep guest VAMM outside the low GPU aperture when the active kernel exposes
 * a modern GPU allocation node. Legacy /dev/mali keeps its measured layout. */
#ifndef T2_ALIAS_REGION_H
#define T2_ALIAS_REGION_H
#include <stdint.h>
#include <string.h>
#define T2_ALIAS_BASE_FBDEV UINT64_C(0x100000000)
#define T2_ALIAS_BASE_DRM UINT64_C(0x300000000)
#define T2_ALIAS_REGION_SIZE UINT64_C(0x100000000)
static uint64_t t2_alias_region_select(unsigned diagnostic,unsigned pc_bench,
                                      const char *backend,unsigned drm_present,
                                      unsigned modern_gpu_node) {
    if(diagnostic || pc_bench)
        return T2_ALIAS_BASE_FBDEV;
    return (backend && !strcmp(backend,"sdl")) || drm_present || modern_gpu_node
        ?T2_ALIAS_BASE_DRM:T2_ALIAS_BASE_FBDEV;
}
#endif
