/* Host diagnostic ALSA null has no hardware clock. Pace its actual SDL
 * callback at 48 kHz without altering PCM, guest state, or the mixer.
 * None of this helper exists in a physical build. */
#ifndef MEGAMAN_HOST_AUDIO_CLOCK_H
#define MEGAMAN_HOST_AUDIO_CLOCK_H
#include "megaman_host_diagnostic.h"
#if MEGAMAN_HOST_DIAGNOSTIC
#include <errno.h>
#include <stdint.h>
#include <time.h>

struct megaman_host_audio_clock {
    uint64_t next_ns,callbacks,frames,rebases;
    unsigned remainder;
};
static struct megaman_host_audio_clock megaman_host_audio_time;

static uint64_t megaman_host_audio_deadline(struct megaman_host_audio_clock *clock,
                                           uint64_t now,unsigned frames) {
    uint64_t numerator=(uint64_t)frames*1000000000ULL+clock->remainder;
    uint64_t period=numerator/48000U;
    clock->remainder=(unsigned)(numerator%48000U);
    /* A paused/late consumer resumes from real time, never catches up a long
     * null-device backlog. Ordinary scheduling jitter keeps the absolute
     * clock; a delay longer than one complete period explicitly rebases. */
    if(!clock->next_ns || (now>clock->next_ns && now-clock->next_ns>period)) {
        if(clock->next_ns)clock->rebases++;
        clock->next_ns=now;
    }
    clock->next_ns+=period;clock->callbacks++;clock->frames+=frames;
    return clock->next_ns;
}

static int megaman_host_audio_clock_wait(unsigned frames) {
    if(!frames || frames>48000U)return EINVAL;
    struct timespec current;
    if(clock_gettime(CLOCK_MONOTONIC,&current))return errno;
    uint64_t now=(uint64_t)current.tv_sec*1000000000ULL+(uint64_t)current.tv_nsec;
    uint64_t next=megaman_host_audio_deadline(&megaman_host_audio_time,now,frames);
    struct timespec target={(time_t)(next/1000000000ULL),(long)(next%1000000000ULL)};
    int result;
    do {result=clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&target,NULL);} while(result==EINTR);
    return result;
}
#endif
#endif
