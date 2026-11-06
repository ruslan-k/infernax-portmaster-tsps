#include "t2_video.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static nxcompat_settings t2_video_settings;
static nxcompat_video_owner_decision t2_video_decision;
static int t2_video_ready, t2_video_have_layout, t2_video_checked;

int t2_video_parse(const char *bytes, size_t size, nxcompat_settings *s,
                   char *error, size_t capacity) {
    static const char defaults[]="# NEXTOS_SETTINGS/2\nlanguage=auto\n";
    nxcompat_settings_error parsed;
    if(!bytes){bytes=defaults;size=sizeof defaults-1;}
    if(nxcompat_settings_parse2(bytes,size,s,&parsed)) {
        snprintf(error,capacity,"NEXTOSSETTINGS.txt:%u: %s",parsed.line,parsed.what);
        return -1;
    }
    /* Do not accept a firmware mode switch, a different filter, or a policy
     * whose required persistence/native-config contract this port lacks. */
    if((s->video_authority[0] && strcmp(s->video_authority,"nextos")) ||
       (s->video_output_size[0] && strcmp(s->video_output_size,"display")) ||
       (s->video_filter[0] && strcmp(s->video_filter,"nearest")) ||
       (s->video_invalid_policy[0] && strcmp(s->video_invalid_policy,"fail_closed")) ||
       !strcmp(s->video_aspect,"engine")) {
        snprintf(error,capacity,"unsupported video setting: use nextos/display/nearest/fail_closed and auto|preserve|stretch|crop|integer");
        return -1;
    }
    return 0;
}

int t2_video_resolve(const nxcompat_settings *s,int sw,int sh,int dw,int dh,
                     nxcompat_video_owner_decision *d) {
    nxcompat_video_owner_input in={0};
    if(!s || !d || sw<1 || sh<1 || dw<1 || dh<1 ||
       sw>4096 || sh>4096 || dw>4096 || dh>4096)return -1;
    in.settings_authority=s->video_authority;
    in.settings_aspect=s->video_aspect;
    in.auto_algorithm_declared=1;
    in.auto_algorithm_result=nxcompat_video_auto_stretch();
    in.package_default=NXCOMPAT_VIDEO_ASPECT_STRETCH;
    in.source_w=sw;in.source_h=sh;in.drawable_w=dw;in.drawable_h=dh;
    if(nxcompat_video_resolve_owner(&in,d))return -1;
    if(d->geometry.unsupported || d->geometry.content.w<1 || d->geometry.content.h<1)return -1;
    /* Keep expanded crop rectangles within the port's supported extent.
     * The first applied viewport is also checked against driver readback. */
    if(d->geometry.content.w>4096 || d->geometry.content.h>4096)return -1;
    return 0;
}

int t2_video_init(void) {
    char bytes[NXCOMPAT_SETTINGS_MAX_BYTES+1],error[256];
    const char *path=getenv("T2_SETTINGS_PATH");
    size_t size=0;
    if(path && *path) {
        int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
        struct stat st;
        if(fd<0){fprintf(stderr,"T2_VIDEO_SETTINGS open failed errno=%d path=%s\n",errno,path);return -1;}
        if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size<0 ||
           (uint64_t)st.st_size>NXCOMPAT_SETTINGS_MAX_BYTES) {
            close(fd);fprintf(stderr,"T2_VIDEO_SETTINGS requires regular file <=4096 bytes\n");return -1;
        }
        while(size<sizeof bytes) {
            ssize_t n=read(fd,bytes+size,sizeof bytes-size);
            if(n<0 && errno==EINTR)continue;
            if(n<0){close(fd);fprintf(stderr,"T2_VIDEO_SETTINGS read failed\n");return -1;}
            if(!n)break;
            size+=(size_t)n;
        }
        close(fd);
    }
    if(t2_video_parse(path && *path?bytes:NULL,size,&t2_video_settings,error,sizeof error)) {
        fprintf(stderr,"T2_VIDEO_SETTINGS %s\n",error);return -1;
    }
    t2_video_ready=1;t2_video_have_layout=0;t2_video_checked=0;
    return 0;
}

const nxcompat_video_owner_decision *t2_video_layout(int sw,int sh,int dw,int dh) {
    nxcompat_video_decision *g=&t2_video_decision.geometry;
    if(!t2_video_ready)return NULL;
    if(t2_video_have_layout && sw==g->source_w && sh==g->source_h &&
       dw==g->drawable_w && dh==g->drawable_h)return &t2_video_decision;
    if(t2_video_resolve(&t2_video_settings,sw,sh,dw,dh,&t2_video_decision)) {
        fprintf(stderr,"T2_VIDEO_LAYOUT refused source=%dx%d drawable=%dx%d aspect=%s reason=%s\n",
            sw,sh,dw,dh,t2_video_settings.video_aspect,t2_video_decision.geometry.reason);
        return NULL;
    }
    char receipt[768];
    if(nxcompat_video_owner_receipt(&t2_video_decision,receipt,sizeof receipt)<0)return NULL;
    puts(receipt);t2_video_have_layout=1;t2_video_checked=0;
    return &t2_video_decision;
}

int t2_video_needs_readback(void) {return !t2_video_checked;}

int t2_video_readback(int dw,int dh,int x,int y,int w,int h) {
    nxcompat_video_readback got={0};char receipt[768];
    if(t2_video_checked)return 0;
    got.api_version=NXCOMPAT_VIDEO_OWNER_API_VERSION;
    got.drawable_w=dw;got.drawable_h=dh;
    got.content=(nxcompat_video_rect){x,y,w,h};
    got.effective=t2_video_decision.geometry.effective;
    int result=nxcompat_video_readback_check(&t2_video_decision,&got,receipt,sizeof receipt);
    puts(receipt);if(!result)t2_video_checked=1;
    return result;
}
