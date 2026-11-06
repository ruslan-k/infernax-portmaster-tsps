/* Optional CPU-side attribution of existing GL calls only. Include after
 * GLES2 declarations and before the native bridge implementation headers.
 * No GL query, flush, finish, draw or error read is added by this observer.
 * Pair with COTM_REPLAY_PHASES for complete record/setup/vertex wall time.
 *
 * Integration (while holding the usual GL ownership):
 *   zero_gpu_cost_context(nvn_functions[r.op].name,0,0,0,0,0,0);
 *   ... execute the original record and its error boundary ...
 *   zero_gpu_cost_context("outside-record",0,0,0,0,0,0);
 * After resolving a draw's program/target, set its sources and logical/actual
 * dimensions with the same context function and the native draw op name.
 * Call zero_gpu_cost_report(last_frame) from submit/present before releasing
 * GL ownership; reports are per calling thread, every 60 frame numbers.
 */
#ifndef ZERO_GPU_COST_H
#define ZERO_GPU_COST_H
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>

#define ZERO_GPU_COST_SLOTS 128U
enum zero_gpu_cost_kind {ZGC_FINISH,ZGC_FLUSH,ZGC_DRAW_ARRAYS,ZGC_DRAW_ELEMENTS,
    ZGC_BIND_FRAMEBUFFER,ZGC_READ_PIXELS,ZGC_GET_ERROR,ZGC_GET_INTEGER,
    ZGC_GET_BOOLEAN,ZGC_GET_FLOAT,ZGC_IS_ENABLED,ZGC_CHECK_FRAMEBUFFER,
    ZGC_GET_TEX_PARAMETER,ZGC_GET_UNIFORM,ZGC_GET_STRING,ZGC_CLEAR,ZGC_COUNT};
static const char *const zero_gpu_cost_names[ZGC_COUNT]={"glFinish","glFlush",
    "glDrawArrays","glDrawElements","glBindFramebuffer","glReadPixels",
    "glGetError","glGetIntegerv","glGetBooleanv","glGetFloatv","glIsEnabled",
    "glCheckFramebufferStatus","glGetTexParameteriv","glGetUniformiv","glGetString","glClear"};
struct zero_gpu_cost_key {const char *op;unsigned vs,fs,width,height,actual_width,actual_height;};
struct zero_gpu_cost_stat {uint64_t calls,wall,cpu,cpu_samples,max_wall;};
struct zero_gpu_cost_slot {struct zero_gpu_cost_key key;unsigned kind;struct zero_gpu_cost_stat stat;};
struct zero_gpu_cost_clock {uint64_t wall,cpu;unsigned active,cpu_valid;};
static _Thread_local struct {
    unsigned configured,enabled,used,report_frame;uint64_t clock_failures,overflow;
    struct zero_gpu_cost_key key;
    struct zero_gpu_cost_stat totals[ZGC_COUNT];
    struct zero_gpu_cost_slot slots[ZERO_GPU_COST_SLOTS];
} zero_gpu_cost;

static unsigned zero_gpu_cost_enabled(void) {
    if(!zero_gpu_cost.configured) {
        const char *option=getenv("ZERO_GPU_COST");
        zero_gpu_cost.enabled=option&&!strcmp(option,"1");zero_gpu_cost.configured=1;
        zero_gpu_cost.key.op="outside-record";
        if(option&&strcmp(option,"0")&&strcmp(option,"1"))
            fprintf(stderr,"ZERO_GPU_COST_DISABLED reason=invalid-option\n");
    }
    return zero_gpu_cost.enabled;
}
static unsigned zero_gpu_cost_now(clockid_t kind,uint64_t *out) {
    struct timespec ts;
    if(clock_gettime(kind,&ts)){zero_gpu_cost.clock_failures++;return 0;}
    *out=(uint64_t)ts.tv_sec*UINT64_C(1000000000)+(uint64_t)ts.tv_nsec;return 1;
}
static struct zero_gpu_cost_clock zero_gpu_cost_begin(void) {
    struct zero_gpu_cost_clock start={0};
    if(!zero_gpu_cost_enabled())return start;
    int saved=errno;
    start.cpu_valid=zero_gpu_cost_now(CLOCK_THREAD_CPUTIME_ID,&start.cpu);
    start.active=zero_gpu_cost_now(CLOCK_MONOTONIC,&start.wall);errno=saved;return start;
}
static void zero_gpu_cost_add(struct zero_gpu_cost_stat *s,uint64_t wall,uint64_t cpu,unsigned valid) {
    s->calls++;s->wall+=wall;if(wall>s->max_wall)s->max_wall=wall;
    if(valid){s->cpu+=cpu;s->cpu_samples++;}
}
static unsigned zero_gpu_cost_key_equal(const struct zero_gpu_cost_key *a,const struct zero_gpu_cost_key *b) {
    return a->op==b->op && a->vs==b->vs && a->fs==b->fs && a->width==b->width &&
        a->height==b->height && a->actual_width==b->actual_width && a->actual_height==b->actual_height;
}
static void zero_gpu_cost_end(unsigned kind,struct zero_gpu_cost_clock start) {
    if(!start.active)return;
    int saved=errno;uint64_t end=0,cpu=0;
    unsigned valid=zero_gpu_cost_now(CLOCK_MONOTONIC,&end)&&end>=start.wall;
    unsigned cpu_valid=zero_gpu_cost_now(CLOCK_THREAD_CPUTIME_ID,&cpu)&&start.cpu_valid&&cpu>=start.cpu;
    if(valid) {
        uint64_t wall=end-start.wall,elapsed_cpu=cpu_valid?cpu-start.cpu:0;
        zero_gpu_cost_add(&zero_gpu_cost.totals[kind],wall,elapsed_cpu,cpu_valid);
        unsigned i;
        for(i=0;i<zero_gpu_cost.used;i++)if(zero_gpu_cost.slots[i].kind==kind &&
            zero_gpu_cost_key_equal(&zero_gpu_cost.slots[i].key,&zero_gpu_cost.key))break;
        if(i==zero_gpu_cost.used && i<ZERO_GPU_COST_SLOTS) {
            zero_gpu_cost.slots[i]=(struct zero_gpu_cost_slot){.key=zero_gpu_cost.key,.kind=kind};zero_gpu_cost.used++;
        }
        if(i<ZERO_GPU_COST_SLOTS)zero_gpu_cost_add(&zero_gpu_cost.slots[i].stat,wall,elapsed_cpu,cpu_valid);
        else zero_gpu_cost.overflow++;
    }
    errno=saved;
}
static void zero_gpu_cost_context(const char *op,unsigned vs,unsigned fs,
        unsigned width,unsigned height,unsigned actual_width,unsigned actual_height) {
    if(!zero_gpu_cost_enabled())return;
    zero_gpu_cost.key=(struct zero_gpu_cost_key){op?op:"outside-record",vs,fs,width,height,actual_width,actual_height};
}
static void zero_gpu_cost_report(unsigned frame) {
    if(!zero_gpu_cost_enabled() || frame<zero_gpu_cost.report_frame || frame-zero_gpu_cost.report_frame<60)return;
    unsigned frames=frame-zero_gpu_cost.report_frame;double scale=1e-6/frames;
    unsigned long thread=(unsigned long)pthread_self();
    printf("ZERO_GPU_COST_SCOPE after-frame=%u frames=%u thread=%lu slots=%u overflow-calls=%"PRIu64" clock-failures=%"PRIu64" clocks=wall-and-thread-CPU GPU-time=unmeasured extra-GL-calls=0 scope=calling-thread-api-wall nested-in-replay=1\n",
        frame,frames,thread,zero_gpu_cost.used,zero_gpu_cost.overflow,zero_gpu_cost.clock_failures);
    for(unsigned k=0;k<ZGC_COUNT;k++) {
        const struct zero_gpu_cost_stat *s=&zero_gpu_cost.totals[k];if(!s->calls)continue;
        printf("ZERO_GPU_COST_GL after-frame=%u thread=%lu call=%s count=%"PRIu64" per-frame=%.2f wall-ms-per-frame=%.3f cpu-ms-per-frame=%.3f cpu-samples=%"PRIu64" max-wall-ms=%.3f\n",
            frame,thread,zero_gpu_cost_names[k],s->calls,(double)s->calls/frames,s->wall*scale,
            s->cpu_samples==s->calls?s->cpu*scale:-1.0,s->cpu_samples,s->max_wall*1e-6);
    }
    unsigned emitted[ZERO_GPU_COST_SLOTS]={0};
    for(unsigned rank=0;rank<48 && rank<zero_gpu_cost.used;rank++) {
        unsigned best=ZERO_GPU_COST_SLOTS;
        for(unsigned i=0;i<zero_gpu_cost.used;i++)if(!emitted[i] &&
            (best==ZERO_GPU_COST_SLOTS || zero_gpu_cost.slots[i].stat.wall>zero_gpu_cost.slots[best].stat.wall))best=i;
        if(best==ZERO_GPU_COST_SLOTS)break;
        emitted[best]=1;const struct zero_gpu_cost_slot *s=&zero_gpu_cost.slots[best];
        printf("ZERO_GPU_COST_OP after-frame=%u thread=%lu op=%s call=%s sources=%u/%u logical=%ux%u actual=%ux%u count=%"PRIu64" wall-ms-per-frame=%.3f cpu-ms-per-frame=%.3f cpu-samples=%"PRIu64" max-wall-ms=%.3f\n",
            frame,thread,s->key.op,zero_gpu_cost_names[s->kind],s->key.vs,s->key.fs,s->key.width,s->key.height,
            s->key.actual_width,s->key.actual_height,s->stat.calls,s->stat.wall*scale,
            s->stat.cpu_samples==s->stat.calls?s->stat.cpu*scale:-1.0,s->stat.cpu_samples,s->stat.max_wall*1e-6);
    }
    memset(zero_gpu_cost.totals,0,sizeof(zero_gpu_cost.totals));
    memset(zero_gpu_cost.slots,0,sizeof(zero_gpu_cost.slots));zero_gpu_cost.used=0;
    zero_gpu_cost.overflow=zero_gpu_cost.clock_failures=0;zero_gpu_cost.report_frame=frame;
}

/* Definitions precede macro redirects, so each wrapper invokes exactly one
 * original GL function and preserves its arguments, return value and errors. */
#define ZGC_VOID(name,kind,params,args) static void zero_gpu_cost_##name params { \
    struct zero_gpu_cost_clock start=zero_gpu_cost_begin();name args;zero_gpu_cost_end(kind,start); }
#define ZGC_RETURN(type,name,kind,params,args) static type zero_gpu_cost_##name params { \
    struct zero_gpu_cost_clock start=zero_gpu_cost_begin();type result=name args;zero_gpu_cost_end(kind,start);return result; }
ZGC_VOID(glFinish,ZGC_FINISH,(void),())
ZGC_VOID(glFlush,ZGC_FLUSH,(void),())
ZGC_VOID(glDrawArrays,ZGC_DRAW_ARRAYS,(GLenum mode,GLint first,GLsizei count),(mode,first,count))
ZGC_VOID(glDrawElements,ZGC_DRAW_ELEMENTS,(GLenum mode,GLsizei count,GLenum type,const void *indices),(mode,count,type,indices))
ZGC_VOID(glBindFramebuffer,ZGC_BIND_FRAMEBUFFER,(GLenum target,GLuint framebuffer),(target,framebuffer))
ZGC_VOID(glReadPixels,ZGC_READ_PIXELS,(GLint x,GLint y,GLsizei width,GLsizei height,GLenum format,GLenum type,void *pixels),(x,y,width,height,format,type,pixels))
ZGC_RETURN(GLenum,glGetError,ZGC_GET_ERROR,(void),())
ZGC_VOID(glGetIntegerv,ZGC_GET_INTEGER,(GLenum pname,GLint *data),(pname,data))
ZGC_VOID(glGetBooleanv,ZGC_GET_BOOLEAN,(GLenum pname,GLboolean *data),(pname,data))
ZGC_VOID(glGetFloatv,ZGC_GET_FLOAT,(GLenum pname,GLfloat *data),(pname,data))
ZGC_RETURN(GLboolean,glIsEnabled,ZGC_IS_ENABLED,(GLenum cap),(cap))
ZGC_RETURN(GLenum,glCheckFramebufferStatus,ZGC_CHECK_FRAMEBUFFER,(GLenum target),(target))
ZGC_VOID(glGetTexParameteriv,ZGC_GET_TEX_PARAMETER,(GLenum target,GLenum pname,GLint *params),(target,pname,params))
ZGC_VOID(glGetUniformiv,ZGC_GET_UNIFORM,(GLuint program,GLint location,GLint *params),(program,location,params))
ZGC_RETURN(const GLubyte *,glGetString,ZGC_GET_STRING,(GLenum name),(name))
ZGC_VOID(glClear,ZGC_CLEAR,(GLbitfield mask),(mask))
#undef ZGC_VOID
#undef ZGC_RETURN
#define glFinish zero_gpu_cost_glFinish
#define glFlush zero_gpu_cost_glFlush
#define glDrawArrays zero_gpu_cost_glDrawArrays
#define glDrawElements zero_gpu_cost_glDrawElements
#define glBindFramebuffer zero_gpu_cost_glBindFramebuffer
#define glReadPixels zero_gpu_cost_glReadPixels
#define glGetError zero_gpu_cost_glGetError
#define glGetIntegerv zero_gpu_cost_glGetIntegerv
#define glGetBooleanv zero_gpu_cost_glGetBooleanv
#define glGetFloatv zero_gpu_cost_glGetFloatv
#define glIsEnabled zero_gpu_cost_glIsEnabled
#define glCheckFramebufferStatus zero_gpu_cost_glCheckFramebufferStatus
#define glGetTexParameteriv zero_gpu_cost_glGetTexParameteriv
#define glGetUniformiv zero_gpu_cost_glGetUniformiv
#define glGetString zero_gpu_cost_glGetString
#define glClear zero_gpu_cost_glClear
#endif
