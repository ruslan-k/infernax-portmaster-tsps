/* Physical framebuffer reads only. An earlier complete read supplies a spatial
 * hint, never evidence. Sparse/inconclusive reads always fall back to a new
 * full read of this same backbuffer before classification or presentation. */
#include "video_pixels.h"
enum nvn_video_readback_status {NVR_OK,NVR_DIMENSIONS,NVR_MEMORY,NVR_CONTEXT};
struct nvn_video_readback {
    uint8_t *pixels;size_t bytes;unsigned y,rows;GLenum error;
    uint64_t elapsed_ns;struct nvn_pixel_measure measure;
};
static unsigned nvn_video_readback(unsigned width,unsigned height,unsigned capture,struct nvn_pixel_band *band,struct nvn_video_readback *out) {
    *out=(struct nvn_video_readback){0};
    if(!nvn_pixel_dimensions(width,height))return NVR_DIMENSIONS;
    out->rows=capture?height:nvn_pixel_rows(height);out->y=capture?0:nvn_pixel_band_y(band,width,height);
    out->bytes=(size_t)width*out->rows*4;out->pixels=malloc(out->bytes);
    if(!out->pixels)return NVR_MEMORY;
    glPixelStorei(GL_PACK_ALIGNMENT,1);uint64_t started=nvn_nanoseconds();
    for(;;) {
        glReadPixels(0,out->y,width,out->rows,GL_RGBA,GL_UNSIGNED_BYTE,out->pixels);
        out->error=glGetError();
        if(out->error || !glGetString(GL_RENDERER))return NVR_CONTEXT;
        if(out->rows==height) {
            out->measure=nvn_measure_full_pixels(out->pixels,width,height,band);break;
        }
        out->measure=nvn_measure_pixels(out->pixels,out->bytes/4);
        /* ZERO_BAND_ONLY_DARK=1 (13/09, physical 1023): during the native title
         * loading every negative band forced a 1280x720 glReadPixels (~40 ms per
         * frame, 300 frames). A negative band then stays a band-only sample:
         * the streak/black-limit policy is unchanged, only its evidence is a
         * rotating 16-row band instead of the complete backbuffer. */
        static int band_only_configured;static unsigned band_only;
        if(!band_only_configured){const char *o=getenv("ZERO_BAND_ONLY_DARK");band_only=o&&!strcmp(o,"1");band_only_configured=1;
            if(band_only)printf("ZERO_BAND_ONLY_DARK enabled=1 negative-band=band-only-sample capture=full-read-unchanged\n");}
        if(!nvn_need_full_readback(capture,out->measure) || (band_only && !capture))break;
        free(out->pixels);out->y=0;out->rows=height;out->bytes=(size_t)width*height*4;out->pixels=malloc(out->bytes);
        if(!out->pixels)return NVR_MEMORY;
    }
    out->elapsed_ns=nvn_nanoseconds()-started;return NVR_OK;
}
