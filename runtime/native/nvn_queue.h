/* Opt-in replay phase clocks (wall): decode, non-draw records, draw setup
 * (state/uniforms/samplers), vertex setup, GL draw call, texture copies. */
enum {NPH_DECODE,NPH_OTHER,NPH_SETUP,NPH_VERTEX,NPH_DRAWCALL,NPH_COPY,NPH_DRAWFIRST,NPH_COUNT};
static uint64_t nvn_bridge_calls,nvn_bridge_calls_mark;
static struct {uint64_t ns,calls;} nvn_record_profile;
static struct {int configured;unsigned enabled,target_changed;uint64_t ns[NPH_COUNT],calls[NPH_COUNT],draw_cpu_ns;uint64_t mark;uint64_t op_ns[NI_COUNT],op_calls[NI_COUNT];} nvn_phase;
static struct {uint64_t resolve,prestate,state,uniforms,geometry,n;} aw_setup_ns;
static unsigned nvn_phase_enabled(void) {
    if(!nvn_phase.configured){const char *o=getenv("COTM_REPLAY_PHASES");nvn_phase.enabled=o&&!strcmp(o,"1");nvn_phase.configured=1;}
    return nvn_phase.enabled;
}
static inline uint64_t nvn_phase_now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000ULL+t.tv_nsec;}
static inline void nvn_phase_begin(void){if(nvn_phase.enabled)nvn_phase.mark=nvn_phase_now();}
static inline void nvn_phase_end(unsigned k){if(nvn_phase.enabled){uint64_t n=nvn_phase_now();nvn_phase.ns[k]+=n-nvn_phase.mark;nvn_phase.calls[k]++;nvn_phase.mark=n;}}
static void nvn_phase_report(unsigned frame,unsigned frames) {
    if(!nvn_phase_enabled()||!frames)return;
    double f=1e-6/frames;
    printf("NVN_REPLAY_PHASES frame=%u decode-ms=%.3f other-ms=%.3f setup-ms=%.3f vertex-ms=%.3f drawcall-ms=%.3f copy-ms=%.3f drawfirst-ms=%.3f draw-cpu-ms=%.3f calls=%"PRIu64"/%"PRIu64"/%"PRIu64"/%"PRIu64"/%"PRIu64"/%"PRIu64"/%"PRIu64" scope=wall-per-frame drawfirst=first-draw-after-target-change\n",
        frame,nvn_phase.ns[0]*f,nvn_phase.ns[1]*f,nvn_phase.ns[2]*f,nvn_phase.ns[3]*f,nvn_phase.ns[4]*f,nvn_phase.ns[5]*f,nvn_phase.ns[6]*f,nvn_phase.draw_cpu_ns*f,
        nvn_phase.calls[0]/frames,nvn_phase.calls[1]/frames,nvn_phase.calls[2]/frames,nvn_phase.calls[3]/frames,nvn_phase.calls[4]/frames,nvn_phase.calls[5]/frames,nvn_phase.calls[6]/frames);
    printf("NVN_RECORD_PROFILE frame=%u record-ms-per-frame=%.3f records-per-frame=%.1f bridge-calls-per-frame=%.1f scope=guest-thread-recording\n",
        frame,nvn_record_profile.ns*f,(double)nvn_record_profile.calls/frames,(double)(nvn_bridge_calls-nvn_bridge_calls_mark)/frames);
    nvn_record_profile.ns=nvn_record_profile.calls=0;nvn_bridge_calls_mark=nvn_bridge_calls;
    printf("AW_DRAW_SETUP frame=%u draws=%"PRIu64" resolve-ms=%.3f prestate-ms=%.3f state-ms=%.3f uniforms-ms=%.3f scope=per-frame\n",frame,aw_setup_ns.n/frames,aw_setup_ns.resolve*f,aw_setup_ns.prestate*f,aw_setup_ns.state*f,aw_setup_ns.uniforms*f);memset(&aw_setup_ns,0,sizeof(aw_setup_ns));
    for(unsigned rank=0;rank<10;rank++) {
        unsigned best=0;for(unsigned i=1;i<NI_COUNT;i++)if(nvn_phase.op_ns[i]>nvn_phase.op_ns[best])best=i;
        if(!nvn_phase.op_ns[best])break;
        printf("NVN_REPLAY_OP frame=%u op=%s ms-per-frame=%.3f calls-per-frame=%.1f us-per-call=%.1f\n",frame,nvn_functions[best].name,
            nvn_phase.op_ns[best]*f,(double)nvn_phase.op_calls[best]/frames,nvn_phase.op_calls[best]?nvn_phase.op_ns[best]/1e3/nvn_phase.op_calls[best]:0.0);
        nvn_phase.op_ns[best]=0;
    }
    memset(nvn_phase.op_ns,0,sizeof(nvn_phase.op_ns));memset(nvn_phase.op_calls,0,sizeof(nvn_phase.op_calls));
    memset(nvn_phase.ns,0,sizeof(nvn_phase.ns));memset(nvn_phase.calls,0,sizeof(nvn_phase.calls));nvn_phase.draw_cpu_ns=0;
}
/* CPU submission queue for the GLES backend. Builder layout was measured in
 * this SDK's original setters. Queue arena requirements describe this adapter,
 * not Tegra allocations. Nonempty submission needs the typed command decoder.
 */
#define NVN_QUEUE_PAGE 4096U
#include "nvn_egl_fence.h"
#define NVN_QUEUE_DEFAULT_COMMAND (64U*1024U)
#define NVN_QUEUE_DEFAULT_COMPUTE (256U*1024U)
#define NVN_QUEUE_DEFAULT_CONTROL (16U*1024U)
struct nvn_queue_builder {
    uint64_t device;uint32_t flags;
    uint8_t command_set,compute_set,control_set,threshold_set;
    uint32_t command,compute,control,provided_size,threshold,pad;uint64_t provided;
};
_Static_assert(sizeof(struct nvn_queue_builder)==48 && offsetof(struct nvn_queue_builder,provided)==40,"observed queue builder");
struct nvn_queue_arena {uint64_t magic,submitted,completed,head,tail;uint32_t error,reserved;};
struct nvn_queue {
    uint64_t object,address,size,command_used,control_used;uint32_t command_size,compute_size,control_size,threshold;
    struct nvn_queue_arena *arena;uint64_t *handles;uint8_t *control;unsigned live;
};
static struct nvn_queue nvn_queues[8];
static unsigned nvn_async_enabled(void);
static void nvn_async_queue_complete(struct nvn_queue *q,unsigned finish);
static uint64_t nvn_queue_layout(uint64_t address,struct nvn_queue_builder *b) {
    /* SDK 6.5 SetDevice +0x4018e0 / SetQueueMemory +0x401960. */
    memcpy(b,nvn_pointer(address+8,sizeof(*b),PROT_READ),sizeof(*b));
    if(!nvn_device.live || b->device!=nvn_device.address)stop("NVN_QUEUE_DEVICE",nvn_caller,b->device,24);
    /* NVN QueueFlags: 1 disables fragment interlock; 2 disables ZCull.
     * Neither optional Tegra path is enabled by this GLES2 backend. Ordinary
     * depth testing and ordered raster draws remain active. Interface pinned
     * to open-ead/nnheaders 6bc6466d, include/nvn/nvn.h. */
    if(b->flags&~3U)stop("NVN_QUEUE_FLAGS_REQUIRED",nvn_caller,b->flags,20);
    if(!b->command_set)b->command=NVN_QUEUE_DEFAULT_COMMAND;
    if(!b->compute_set)b->compute=NVN_QUEUE_DEFAULT_COMPUTE;
    if(!b->control_set)b->control=NVN_QUEUE_DEFAULT_CONTROL;
    if(!b->threshold_set)b->threshold=b->command;
    if(b->command<NVN_QUEUE_PAGE || b->control<NVN_QUEUE_PAGE || (b->command|b->compute|b->control)%NVN_QUEUE_PAGE ||
       b->command>16U*1024U*1024U || b->compute>16U*1024U*1024U || b->control>16U*1024U*1024U || b->threshold<NVN_QUEUE_PAGE || b->threshold>b->command)
        stop("NVN_QUEUE_ARENA_SIZE",nvn_caller,b->command,24);
    /* LC2 reserves compute scratch for its third queue. Reservation does not
     * enable DispatchCompute; typed replay still rejects unsupported commands.
     * The unused compute interval separates command and control storage. */
    return NVN_QUEUE_PAGE+(uint64_t)b->command+b->compute+b->control;
}
static struct nvn_queue *nvn_queue_find(uint64_t object) {
    for(unsigned i=0;i<8;i++)if(nvn_queues[i].live&&nvn_queues[i].object==object)return &nvn_queues[i];
    stop("NVN_QUEUE_INVALID",nvn_caller,object,24);return NULL;
}
static unsigned nvn_queue_error_dispatch(unsigned id,struct nvn_frame *f,uint64_t *result) {
    if(id!=NI_nvnQueueGetError)return 0;
    struct nvn_queue *q=nvn_queue_find(f->args[0]);
    /* SDK 2e6910 returns NONE=0 without touching optional errorInfo. We
     * have no Tegra MMU fault details: a latched adapter error is UNKNOWN=1.
     * Context operations already fail at their GL error boundary. Querying
     * status neither clears errors nor waits for pending GPU completion. */
    *result=__atomic_load_n(&q->arena->error,__ATOMIC_ACQUIRE)?1:0;
    return 1;
}
/* A completed queue alone does not cover texture/shader operations issued
 * directly on this context. Every acquire invalidates the conservative proof;
 * only a final glFinish, with no following GL work before release, restores it.
 * The context mutex also makes a proof check atomic with another acquire. */
static struct {
    pthread_mutex_t mutex;
    uint64_t use_serial,completed_serial,skipped_waits,real_waits;
    unsigned completion_valid;
} nvn_gl_completion={.mutex=PTHREAD_MUTEX_INITIALIZER};
/* Diagnostic only: one line per acquire boundary tells "surface destroyed" (query fails) apart from
 * "surface belongs elsewhere" (query works, make-current refuses). */
static void nvn_surface_diag(const char *tag,EGLint bind_error) {
    EGLint w=-1,h=-1,cid=-1;
    EGLBoolean qw=eglQuerySurface(nvn_device.display,nvn_device.surface,EGL_WIDTH,&w);
    EGLBoolean qh=eglQuerySurface(nvn_device.display,nvn_device.surface,EGL_HEIGHT,&h);
    EGLBoolean qc=eglQuerySurface(nvn_device.display,nvn_device.surface,EGL_CONFIG_ID,&cid);
    EGLint query_error=eglGetError();
    printf("NVN_SURFACE_DIAG tag=%s engine=%s device=%p window=%p same=%u cur-draw=%p cur-read=%p cur-ctx=%p dev-ctx=%p query=%d/%d/%d size=%dx%d cfg=%d bind-error=0x%x query-error=0x%x tid=%ld\n",
        tag,nvn_single_enabled()?"single":"multi",(void *)nvn_device.surface,(void *)nvn_diag_window_surface,
        (unsigned)(nvn_device.surface==nvn_diag_window_surface),(void *)eglGetCurrentSurface(EGL_DRAW),
        (void *)eglGetCurrentSurface(EGL_READ),(void *)eglGetCurrentContext(),(void *)nvn_device.context,
        (int)qw,(int)qh,(int)qc,(int)w,(int)h,(int)cid,(unsigned)bind_error,
        (unsigned)query_error,(long)syscall(SYS_gettid));
}
static void nvn_gl_acquire_locked(void) {
    nvn_single_transfer_acquire_locked();
    nvn_single_require_owner();
    if(!nvn_single_enabled())nvn_egl_present_reader_wait();
    eglGetError();
    if(!nvn_single_make_current(nvn_device.surface,nvn_device.surface,nvn_device.context)) {
        EGLint bind_error=eglGetError();
        nvn_surface_diag("fail",bind_error);
        stop("NVN_QUEUE_CONTEXT",nvn_caller,(uint64_t)bind_error,21);
    }
    {static unsigned told;if(told++<2)nvn_surface_diag("ok",0);}
    if(!++nvn_gl_completion.use_serial)stop("NVN_GL_SERIAL_WRAP",nvn_caller,0,24);
    nvn_gl_completion.completion_valid=0;
}
static void nvn_gl_acquire(void) {
    pthread_mutex_lock(&nvn_gl_completion.mutex);nvn_gl_acquire_locked();
}
static void nvn_gl_finish_current(void) {
    glFinish();GLenum err=glGetError();if(err!=GL_NO_ERROR)stop("NVN_QUEUE_FINISH_ERROR",nvn_caller,err,21);
    nvn_gl_completion.completed_serial=nvn_gl_completion.use_serial;nvn_gl_completion.completion_valid=1;
    nvn_gl_completion.real_waits++;
}
static void nvn_gl_release(void) {
    GLenum err=glGetError();if(err!=GL_NO_ERROR)stop("NVN_QUEUE_GL_ERROR",nvn_caller,err,21);
    if(!nvn_single_detach())stop("NVN_QUEUE_DETACH",nvn_caller,eglGetError(),21);
    pthread_mutex_unlock(&nvn_gl_completion.mutex);
}
static void nvn_queue_complete(struct nvn_queue *q,unsigned finish) {
    if(nvn_async_enabled()){nvn_async_queue_complete(q,finish);return;}
    pthread_mutex_lock(&nvn_gl_completion.mutex);
    nvn_single_require_owner();
    if(q->arena->head!=q->arena->tail)stop("NVN_QUEUE_PENDING_DECODER",nvn_caller,q->arena->head,20);
    if(q->arena->completed!=q->arena->submitted)stop("NVN_QUEUE_COMPLETION_UNPROVED",nvn_caller,q->arena->submitted,24);
    if(nvn_gl_completion.completion_valid && nvn_gl_completion.completed_serial==nvn_gl_completion.use_serial) {
        nvn_gl_completion.skipped_waits++;pthread_mutex_unlock(&nvn_gl_completion.mutex);return;
    }
    nvn_gl_acquire_locked();
    if(finish)nvn_gl_finish_current();else glFlush();
    nvn_gl_release();
}
static unsigned nvn_queue_dispatch(unsigned id,struct nvn_frame *f,uint64_t *result) {
    if(nvn_queue_error_dispatch(id,f,result))return 1;
    uint64_t *a=f->args;*result=0;
    if(id==NI_nvnQueueBuilderGetQueueMemorySize || id==NI_nvnQueueInitialize) {
        struct nvn_queue_builder b;uint64_t size=nvn_queue_layout(a[id==NI_nvnQueueInitialize],&b);
        if(id==NI_nvnQueueBuilderGetQueueMemorySize){*result=size;return 1;}
        nvn_pointer(a[0],8,PROT_READ|PROT_WRITE);
        if(!b.provided) {
            /* Animal Well leaves the queue memory to the driver: back it with a private guest-visible region. */
            static uint64_t aw_queue_base=0x2000000000ULL;
            uint64_t bytes=(size+PAGE-1)&~(uint64_t)(PAGE-1);
            void *mp=mmap((void *)(uintptr_t)aw_queue_base,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
            if(mp==MAP_FAILED || (uint64_t)(uintptr_t)mp!=aw_queue_base)stop("AW_QUEUE_MEMORY_MAP",nvn_caller,aw_queue_base,21);
            pthread_mutex_lock(&regions_mutex);region_write_begin(0);
            region_insert((struct region){aw_queue_base,bytes,PROT_READ|PROT_WRITE,3,"aw_queue_memory",0},0);
            region_write_end(0);pthread_mutex_unlock(&regions_mutex);
            b.provided=aw_queue_base;b.provided_size=(uint32_t)bytes;aw_queue_base+=bytes+PAGE;
            printf("NVN_QUEUE_HOST_MEMORY address=0x%"PRIx64" size=%"PRIu64"\n",b.provided,bytes);
        }
        if(b.provided%NVN_QUEUE_PAGE || b.provided_size<size)stop("NVN_QUEUE_PROVIDED_MEMORY",nvn_caller,b.provided_size,24);
        struct nvn_queue_arena *arena=nvn_pointer(b.provided,size,PROT_READ|PROT_WRITE);unsigned slot=8;
        for(unsigned i=0;i<8;i++) {
            struct nvn_queue *q=&nvn_queues[i];if(!q->live){if(slot==8)slot=i;continue;}
            if(q->object==a[0] || (b.provided<q->address+q->size && q->address<b.provided+size))stop("NVN_QUEUE_ARENA_REUSED",nvn_caller,a[0],24);
        }
        if(slot==8)stop("NVN_QUEUE_CAPACITY",nvn_caller,slot,21);
        memset(arena,0,size);arena->magic=UINT64_C(0x43544d5155455545);
        nvn_queues[slot]=(struct nvn_queue){.object=a[0],.address=b.provided,.size=size,.command_size=b.command,
            .compute_size=b.compute,.control_size=b.control,.threshold=b.threshold,.arena=arena,.handles=(void *)(uintptr_t)(b.provided+NVN_QUEUE_PAGE),
            .control=(void *)(uintptr_t)(b.provided+NVN_QUEUE_PAGE+b.command+b.compute),.live=1};
        printf("NVN_QUEUE_INITIALIZED arena=0x%"PRIx64" size=%"PRIu64" command=%u control=%u compute-reserved=%u compute-used=0 submitted=0\n",b.provided,size,b.command,b.control,b.compute);
        *result=1;return 1;
    }
    switch(id) {
    case NI_nvnQueueSetDebugLabel:case NI_nvnQueueFinalize:case NI_nvnQueueFlush:case NI_nvnQueueFinish:
    case NI_nvnQueueGetTotalCommandMemoryUsed:case NI_nvnQueueGetTotalControlMemoryUsed:case NI_nvnQueueGetTotalComputeMemoryUsed:break;
    default:return 0;
    }
    struct nvn_queue *q=nvn_queue_find(a[0]);
    if(id==NI_nvnQueueSetDebugLabel)nvn_debug_label(a[1]);
    if(id==NI_nvnQueueFlush || id==NI_nvnQueueFinish || id==NI_nvnQueueFinalize) {
        nvn_queue_complete(q,id!=NI_nvnQueueFlush);
        if(id==NI_nvnQueueFinalize){q->arena->magic=0;q->live=0;}
    }
    /* Actual consumed queue bytes; no commands have been submitted yet. */
    if(id==NI_nvnQueueGetTotalCommandMemoryUsed || id==NI_nvnQueueGetTotalControlMemoryUsed || id==NI_nvnQueueGetTotalComputeMemoryUsed) {
        *result=id==NI_nvnQueueGetTotalCommandMemoryUsed?q->command_used:id==NI_nvnQueueGetTotalControlMemoryUsed?q->control_used:0;
    }
    return 1;
}
