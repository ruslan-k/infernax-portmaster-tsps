/* AW_RELEASE=1 (set by the owner-facing launcher): the runtime writes no diagnostic file next to the game
 * (protocol snapshots, per-frame video receipts, captures). Probe runs keep every one of them. */
#ifndef AW_RELEASE_H
#define AW_RELEASE_H
#include <stdlib.h>
static inline int aw_release_mode(void) {
    static int value=-1;
    if(value<0){const char *e=getenv("AW_RELEASE");value=e&&e[0]=='1'&&!e[1];}
    return value;
}
#endif
