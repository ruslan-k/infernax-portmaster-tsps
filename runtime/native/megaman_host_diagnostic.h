/* Optional host-only software graphics build. A real Mesa pbuffer executes
 * the original game's draws; these pixels can never certify a device. */
#ifndef MEGAMAN_HOST_DIAGNOSTIC
#define MEGAMAN_HOST_DIAGNOSTIC 0
#endif
#if MEGAMAN_HOST_DIAGNOSTIC != 0 && MEGAMAN_HOST_DIAGNOSTIC != 1
#error "Invalid host diagnostic build selection"
#endif
#define MEGAMAN_VIDEO_BOUNDARY (MEGAMAN_HOST_DIAGNOSTIC?"host-software-pbuffer-before-swap":"physical-backbuffer-before-present")
