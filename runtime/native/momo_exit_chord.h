/* SELECT+START held together closes the port, like every other port of the house.
 * The chord is read from the PHYSICAL pad only (never from the diagnostic press socket) and has to be
 * held, so a game that happens to use both buttons at once cannot be quit by accident.
 * The exit itself is `_exit`: the teardown of an EGL/GL context on this driver is where ports die with
 * a SIGSEGV that has nothing to do with the game, and the status the owner checks is the one the
 * process returns. Everything this host owns is either already on disk or is diagnostic. */
#ifndef MOMO_EXIT_CHORD_H
#define MOMO_EXIT_CHORD_H
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define MOMO_EXIT_CHORD_MASK ((1ULL<<10)|(1ULL<<11))   /* Npad Plus (START) + Minus (SELECT) */
#define MOMO_EXIT_CHORD_HOLD_MS 600

static void momo_exit_chord(uint64_t physical_buttons) {
    static int cfg,enabled;static uint64_t since_ms;
    if(!cfg){const char *o=getenv("MOMO_EXIT_CHORD");enabled=!o||strcmp(o,"0");cfg=1;}
    if(!enabled)return;
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    uint64_t ms=(uint64_t)now.tv_sec*1000ull+(uint64_t)now.tv_nsec/1000000ull;
    if((physical_buttons&MOMO_EXIT_CHORD_MASK)!=MOMO_EXIT_CHORD_MASK){since_ms=0;return;}
    if(!since_ms){since_ms=ms;return;}
    if(ms-since_ms<MOMO_EXIT_CHORD_HOLD_MS)return;
    printf("MOMO_EXIT_CHORD buttons=SELECT+START held-ms=%llu status=0 teardown=skipped\n",
        (unsigned long long)(ms-since_ms));
    fflush(NULL);
    _exit(0);
}
#endif
