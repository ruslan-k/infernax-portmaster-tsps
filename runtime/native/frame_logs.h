/* Sampling changes emission only. Counters belong to one caller thread/channel;
 * callers must use TLS or their existing serialization, never shared races. */
#ifndef COTM_FRAME_LOGS_H
#define COTM_FRAME_LOGS_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
struct frame_log_counter {uint64_t occurrence,suppressed,keys[3];unsigned valid;};
struct frame_log_event {uint64_t occurrence,suppressed;unsigned emit,changed;};
static pthread_once_t frame_log_once=PTHREAD_ONCE_INIT;
static unsigned frame_log_verbose,frame_log_draw_diagnostics;
static void frame_log_configure(void) {
    /* The immutable process environment is read once. This callback never
     * takes the kernel lock, calls guest/SDL, or emits through stdio. */
    const char *option=getenv("COTM_VERBOSE_FRAME_LOGS");
    if(option && strcmp(option,"0") && strcmp(option,"1"))stop("FRAME_LOG_OPTION",0,0,24);
    frame_log_verbose=option && !strcmp(option,"1");
    frame_log_draw_diagnostics=getenv("COTM_DRAW_DIAGNOSTICS")!=NULL;
}
static inline unsigned frame_log_full_commands(void) {
    pthread_once(&frame_log_once,frame_log_configure);return frame_log_draw_diagnostics;
}
static struct frame_log_event frame_log_sample(struct frame_log_counter *counter,uint64_t key0,uint64_t key1,uint64_t key2,unsigned force) {
    pthread_once(&frame_log_once,frame_log_configure);
    unsigned changed=!counter->valid || counter->keys[0]!=key0 || counter->keys[1]!=key1 || counter->keys[2]!=key2;
    if(counter->occurrence!=UINT64_MAX)counter->occurrence++;
    unsigned print=frame_log_verbose || force || changed || counter->occurrence<=4 ||
        !(counter->occurrence&(counter->occurrence-1)) || counter->occurrence==UINT64_MAX;
    if(!print)counter->suppressed++;
    counter->valid=1;counter->keys[0]=key0;counter->keys[1]=key1;counter->keys[2]=key2;
    return (struct frame_log_event){counter->occurrence,counter->suppressed,print,changed};
}
#endif
