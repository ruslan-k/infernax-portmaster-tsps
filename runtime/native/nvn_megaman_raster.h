#ifndef NVN_MEGAMAN_RASTER_H
#define NVN_MEGAMAN_RASTER_H
#include <stdint.h>
#include <math.h>

/* SDK 4.4.1: samples bits8..12, raster samples bits15..19; the
 * texture adapter admits only 0/1 samples. Bit0 cannot create additional
 * samples in that domain. Alpha dither bit2 has no effect with A2C off.
 * Reject coverage, programmable locations, and every unknown flag. The
 * location bytes are inactive when bit3 is clear and need not be canonical. */
static unsigned nvn_megaman_multisample_supported(const uint32_t state[6]) {
    return state && !(state[0]&~UINT32_C(0x00008105));
}

/* The native API takes factor/units/clamp in s0/s1/s2. Both NVN and Mesa's
 * NVIDIA backend multiply API units by two when emitting hardware state;
 * glPolygonOffset therefore receives the original API units exactly once.
 * The LC2 caller always supplies zero clamp. A nonzero clamp needs a separate
 * extension backend; it must never be silently dropped by the GLES2 path. */
static unsigned nvn_megaman_polygon_offset_supported(float factor,float units,float clamp) {
    return isfinite(factor)&&isfinite(units)&&isfinite(clamp)&&clamp==0.0f;
}
static unsigned nvn_megaman_polygon_offset_fill(uint32_t polygon) {
    /* SDK 303f78..304058: POINT=1, LINE=2, FILL=4. */
    return !!(polygon&(UINT32_C(4)<<15));
}
static void nvn_megaman_raster_apply(uint32_t polygon,float factor,float units) {
    /* Apply on every admitted draw because present and auxiliary GLES passes
     * share this context. These capabilities are outside the old state cache. */
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glDisable(GL_SAMPLE_COVERAGE);
    glPolygonOffset(factor,units);
    if(nvn_megaman_polygon_offset_fill(polygon))glEnable(GL_POLYGON_OFFSET_FILL);
    else glDisable(GL_POLYGON_OFFSET_FILL);
}
#endif
