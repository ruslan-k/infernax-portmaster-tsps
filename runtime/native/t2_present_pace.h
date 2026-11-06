/* FRENTE-C (27/09/2026): even presentation cadence, opt-in by T2_PRESENT_PACE=<Hz>.
 *
 * The game logic runs in Unity FixedUpdate (UnityMain.FixedUpdate -> terminator.Main.update, measured in the
 * IL2CPP code), so a slower render does not slow the game down: Unity runs 1 or 2 fixed steps per rendered
 * frame. At an uneven 40-45 presents/s the picture alternates 16.7 and 33.3 ms on screen (judder). With
 * T2_PRESENT_PACE=<Hz> every present is held for the whole number of display refreshes closest to that rate
 * (30 on a 60 Hz panel = 2 refreshes, 33.3 ms, always two fixed steps; the .138 panel runs at 80 Hz, where 40
 * = 2 refreshes of 12.5 ms), so each frame stays the same time on screen.
 *
 * Clock: the display's own vblank counter when /dev/dri/card0 answers DRM_IOCTL_WAIT_VBLANK (no DRM master
 * needed; the page flip that SDL queues lands on the next vblank), otherwise an absolute CLOCK_MONOTONIC grid.
 * Decision by capability, not by firmware name. The wait is taken BEFORE the GL mutex, never inside it.
 * Unset/0 = unchanged behaviour. Frames that are already late are never delayed (no catch-up bursts either:
 * the grid restarts from the late present). */
#ifndef T2_PRESENT_PACE_H
#define T2_PRESENT_PACE_H
#include <drm/drm.h>
#include <sys/ioctl.h>
static struct {
    int configured;unsigned hz,vblanks;uint64_t period_ns,next_ns;int fd;unsigned have_seq,last_seq;
    uint64_t presents,waits,wait_ns,late,vblank_errors;
} t2_pace={.fd=-1};
static int t2_pace_vblank(unsigned type,unsigned sequence,unsigned *out) {
    union drm_wait_vblank v;memset(&v,0,sizeof(v));
    v.request.type=(enum drm_vblank_seq_type)type;v.request.sequence=sequence;
    int rc;do rc=ioctl(t2_pace.fd,DRM_IOCTL_WAIT_VBLANK,&v);while(rc&&errno==EINTR);
    if(rc)return -1;
    *out=v.reply.sequence;return 0;
}
static void t2_pace_configure(void) {
    t2_pace.configured=1;
    const char *o=getenv("T2_PRESENT_PACE");
    unsigned hz=o?(unsigned)strtoul(o,NULL,10):0;
    if(!hz)return;
    if(hz<10||hz>120)stop("T2_PRESENT_PACE_OPTION",nvn_caller,hz,24);
    t2_pace.hz=hz;t2_pace.vblanks=1;t2_pace.period_ns=1000000000ull/hz;
    const char *clock="monotonic";double refresh=0;
    if(!(o=getenv("T2_PRESENT_PACE_CLOCK"))||strcmp(o,"monotonic")) {
        t2_pace.fd=open("/dev/dri/card0",O_RDWR|O_CLOEXEC);
        unsigned a,b;
        /* The panel is not always 60 Hz (the .138 RK3326 panel measures 80 Hz): measure it over 8 refreshes and
         * hold each frame the whole number of refreshes closest to the requested rate. */
        if(t2_pace.fd>=0&&!t2_pace_vblank(_DRM_VBLANK_RELATIVE,1,&a)) {
            uint64_t t0=nvn_nanoseconds();
            if(!t2_pace_vblank(_DRM_VBLANK_RELATIVE,8,&b)&&b>a) {
                refresh=(b-a)*1e9/(double)(nvn_nanoseconds()-t0);
                unsigned n=(unsigned)(refresh/hz+0.5);t2_pace.vblanks=n?n:1;clock="drm-vblank";
            }
        }
        if(!refresh){if(t2_pace.fd>=0)close(t2_pace.fd);t2_pace.fd=-1;}
    }
    printf("T2_PRESENT_PACE hz=%u refresh-hz=%.2f vblanks=%u effective-hz=%.2f clock=%s wait=before-present-outside-gl-mutex late=never-delayed\n",
        hz,refresh,t2_pace.vblanks,refresh?refresh/t2_pace.vblanks:(double)hz,clock);
}
/* Before the present of a rendered frame (never for an omitted one). */
static void t2_present_pace_before(void) {
    if(!t2_pace.configured)t2_pace_configure();
    if(!t2_pace.hz)return;
    uint64_t started=nvn_nanoseconds();unsigned waited=0;
    if(t2_pace.fd>=0) {
        unsigned now;
        if(!t2_pace.have_seq)return;
        if(t2_pace_vblank(_DRM_VBLANK_RELATIVE,0,&now)){t2_pace.vblank_errors++;return;}
        /* The previous flip is shown from last_seq+1. To stay `vblanks` refreshes on screen this frame must be
         * shown from last_seq+1+vblanks, so it is queued after vblank last_seq+vblanks. */
        unsigned target=t2_pace.last_seq+t2_pace.vblanks;
        if((int)(now-target)<0) {
            if(t2_pace_vblank(_DRM_VBLANK_ABSOLUTE,target,&now))t2_pace.vblank_errors++;else waited=1;
        } else if((int)(now-target)>0)t2_pace.late++;
    } else {
        if(!t2_pace.next_ns)return;
        if(started<t2_pace.next_ns) {
            struct timespec until={(time_t)(t2_pace.next_ns/1000000000ull),(long)(t2_pace.next_ns%1000000000ull)};
            while(clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&until,NULL)==EINTR){}
            waited=1;
        } else if(started-t2_pace.next_ns>t2_pace.period_ns/4)t2_pace.late++;
    }
    if(waited){t2_pace.waits++;t2_pace.wait_ns+=nvn_nanoseconds()-started;}
}
/* Right after the swap returned. */
static void t2_present_pace_after(void) {
    if(!t2_pace.hz)return;
    t2_pace.presents++;
    if(t2_pace.fd>=0) {
        unsigned seq;
        if(!t2_pace_vblank(_DRM_VBLANK_RELATIVE,0,&seq)){t2_pace.last_seq=seq;t2_pace.have_seq=1;}
        else t2_pace.vblank_errors++;
    } else {
        uint64_t now=nvn_nanoseconds();
        /* Keep the grid while on time; restart it from a late present instead of bursting to catch up. */
        if(!t2_pace.next_ns||now>t2_pace.next_ns+t2_pace.period_ns/4)t2_pace.next_ns=now+t2_pace.period_ns;
        else t2_pace.next_ns+=t2_pace.period_ns;
    }
    if(t2_pace.presents%600==0)
        printf("T2_PRESENT_PACE_PROFILE presents=%"PRIu64" waits=%"PRIu64" wait-ms-mean=%.2f late=%"PRIu64" vblank-errors=%"PRIu64"\n",
            t2_pace.presents,t2_pace.waits,t2_pace.waits?t2_pace.wait_ns*1e-6/t2_pace.waits:0.0,t2_pace.late,t2_pace.vblank_errors);
}
#endif
