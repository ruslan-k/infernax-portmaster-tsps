#ifndef COTM_NVN_VIDEO_CADENCE_H
#define COTM_NVN_VIDEO_CADENCE_H
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/* Unobserved frames supply no video health. Preserve the existing startup and
 * every-fourth-frame dark sampling; only full diagnostic capture has a new,
 * opt-in cadence. A sparse negative still requires a fresh complete read. */
static unsigned nvn_video_period(void) {
    static unsigned period;
    if(!period){const char *o=getenv("COTM_READBACK_PERIOD");period=o?(unsigned)strtoul(o,NULL,10):60;if(period<60)period=60;if(period>3600)period=3600;}
    return period;
}
static unsigned nvn_video_should_sample(unsigned sparse,unsigned frame,unsigned proved,unsigned black_streak) {
    static unsigned dark_every;
    if(!dark_every){const char *o=getenv("ZERO_DARK_SAMPLE_EVERY");dark_every=o?(unsigned)strtoul(o,NULL,10):4;if(dark_every<1)dark_every=1;if(dark_every>60)dark_every=60;}
    /* ROOT (MEDIDO device run 109): with every draw elided the image is never "proved", so the dark
     * sampler read the backbuffer EVERY frame = 99 of 127 ms. The floor measurement sets T2_READBACK_OFF=1
     * (only an explicit capture reads). */
    /* Frente P (MEDIDO, owner session run 300): the sparse readback + PPM dump every 60 frames made 517 of the
     * 528 frames >100 ms (~150-220 ms each, once per second). T2_READBACK_OFF=2 = play mode: sample only until
     * the first image proof (the drive's PIXELS marker and the capture proof still happen), then nothing but
     * explicit SIGUSR1 captures. =1 keeps the floor behaviour (never sample). */
    static int off=-1;if(off<0){const char *o=getenv("T2_READBACK_OFF");off=o&&*o=='1'?1:o&&*o=='2'?2:0;
        if(off==1)printf("T2_READBACK_OFF periodic-and-dark-sampling=disabled capture=explicit-only\n");
        if(off==2)printf("T2_READBACK_OFF mode=2 sampling=until-first-proof then=explicit-capture-only\n");}
    if(off==1)return 0;
    if(off==2)return !proved;
    return !sparse||!proved||(black_streak&&frame%dark_every==0)||frame%nvn_video_period()==0;
}
static unsigned nvn_video_capture_on_demand_only;
static unsigned nvn_video_capture_period(void) {
    static unsigned period;
    if(!period) {
        const char *demand=getenv("MEGAMAN_CAPTURE_ON_DEMAND_ONLY");
        if(demand&&strcmp(demand,"0")&&strcmp(demand,"1"))stop("NVN_LC2_CAPTURE_DEMAND_OPTION",nvn_caller,0,24);
        nvn_video_capture_on_demand_only=demand&&!strcmp(demand,"1");
        if(nvn_video_capture_on_demand_only)printf("NVN_LC2_CAPTURE_POLICY periodic=disabled startup=until-first-proof explicit-request=unchanged pixel-readback=unchanged journal=unchanged\n");
#if defined(MEGAMAN_HOST_DIAGNOSTIC) && MEGAMAN_HOST_DIAGNOSTIC
        const char *host=getenv("MEGAMAN_HOST_CAPTURE_PERIOD");
        if(host) {
            if((host[0]!='1'&&host[0]!='2'&&host[0]!='3'&&host[0]!='6')||host[1]!='0'||host[2])
                stop("MEGAMAN_HOST_CAPTURE_PERIOD_OPTION",nvn_caller,0,24);
            period=(unsigned)(host[0]-'0')*10U;
            return period;
        }
#endif
        const char *option=getenv("COTM_CAPTURE_PERIOD");unsigned value=0;
        if(option) {
            if(!*option)stop("NVN_VIDEO_CAPTURE_PERIOD_OPTION",nvn_caller,0,24);
            for(const char *p=option;*p;p++) {
                if(*p<'0'||*p>'9')stop("NVN_VIDEO_CAPTURE_PERIOD_OPTION",nvn_caller,0,24);
                value=value*10U+(unsigned)(*p-'0');
                if(value>3600)stop("NVN_VIDEO_CAPTURE_PERIOD_OPTION",nvn_caller,value,24);
            }
        }
        unsigned sample_period=nvn_video_period();
        if(value && (value<sample_period || value%sample_period))
            stop("NVN_VIDEO_CAPTURE_PERIOD_OPTION",nvn_caller,value,24);
        period=value?value:sample_period;
    }
    return period;
}
static unsigned nvn_video_should_capture(unsigned frame,unsigned proved) {
    unsigned period=nvn_video_capture_period();
    return !proved || (!nvn_video_capture_on_demand_only && frame%period==0);
}
/* This is the actual PPM admission guard, shared with the directed oracle.
 * A positive band is current pixel evidence but is never a complete image. */
static unsigned nvn_video_capture_ready(unsigned capture,unsigned proof,unsigned width,unsigned height,
                                        unsigned y,unsigned rows,size_t bytes) {
    if(!capture || !proof)return 0;
    if(!width || !height || width>4096 || height>4096 || y || rows!=height || bytes!=(size_t)width*height*4)
        stop("NVN_VIDEO_CAPTURE_FULL_REQUIRED",nvn_caller,bytes,24);
    return 1;
}
#endif
