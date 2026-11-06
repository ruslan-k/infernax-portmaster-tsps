#ifndef T2_VIDEO_H
#define T2_VIDEO_H
#include "nxcompat_settings.h"
#include "nxcompat_video.h"

/* The framework owns parsing and geometry; this adapter admits only the
 * policies it can apply to T2's final presentation quad. */
int t2_video_parse(const char *bytes, size_t size, nxcompat_settings *settings,
                   char *error, size_t capacity);
int t2_video_resolve(const nxcompat_settings *settings, int sw, int sh,
                     int dw, int dh, nxcompat_video_owner_decision *decision);
int t2_video_init(void);
const nxcompat_video_owner_decision *t2_video_layout(int sw, int sh, int dw, int dh);
int t2_video_needs_readback(void);
int t2_video_readback(int dw, int dh, int x, int y, int w, int h);
#endif
