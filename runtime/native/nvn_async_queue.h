/* Optional asynchronous producer queue. API serialization is separate from
 * context ownership: guest table lifetimes stay locked while GL waits release
 * the CPU to the driver. GPU completion never follows from a flush or timeout. */
#define NVN_ASYNC_TICKETS 512U
#include "nvn_megaman_async_contract.h"
static struct {
    pthread_once_t once;pthread_mutex_t api_mutex;
    unsigned enabled,lc2_qualified,ticket_high_water,automatic,auto_pending;uint64_t next_id,created,observed,polls,timeouts,wait_ns,after_frame;
    uint64_t lc2_reuse_waits,lc2_reuse_timeouts;
} nvn_async={.once=PTHREAD_ONCE_INIT};
struct nvn_async_ticket {
    uint64_t id,serial,first_head,end_head,first_submitted,end_submitted;
    EGLSyncKHR fence;EGLContext context;struct nvn_queue *queue;
    uint64_t handles[256];unsigned count,refs,live,queued,signaled;
};
static struct nvn_async_ticket nvn_async_tickets[NVN_ASYNC_TICKETS];
/* Ticket publication/traversal holds the completion mutex; the profile reads
 * only this bound under API serialization. Every live slot is below the
 * monotonic bound; holes and retained Sync references do not lower it.
 * Legacy scans retain the full fixed capacity. */
static unsigned nvn_async_ticket_bound(void) {
    return nvn_async.lc2_qualified?nvn_async.ticket_high_water:NVN_ASYNC_TICKETS;
}
static void nvn_lc2_async_reuse_poll(uint64_t handle);
static void nvn_async_initialize(void) {
    const char *option=getenv("COTM_ASYNC_QUEUE");
    if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("NVN_ASYNC_QUEUE_OPTION",nvn_caller,0,24);
    nvn_async.enabled=option&&!strcmp(option,"1");
    if(!option) {
        /* FRENTE-C: T2_GPU_PIPELINE (default auto). With COTM_ASYNC_QUEUE unset, the asynchronous queue plus the
         * GPU-side present wait are admitted by CAPABILITY: EGL_KHR_fence_sync + EGL_KHR_wait_sync on the display,
         * GL_OES_EGL_sync on the current context, and none of the host modes this path excludes. Measured on the
         * Mali-G31 (.138, ES launch profile): 36.2 -> 38.8 fps, p99 47.4 -> 40.9 ms, zero frames >100 ms.
         * The Mali-450 (.77) lacks EGL_KHR_wait_sync and keeps the synchronous queue. T2_GPU_PIPELINE=0 = off. */
        nvn_async.auto_pending=1; /* decided by t2_gpu_pipeline_decide() once the EGL display exists */
    }
    nvn_async.lc2_qualified=nvn_lc2_async_option(nvn_async.enabled);
    if(nvn_async.lc2_qualified)nvn_command_pending_poll=nvn_lc2_async_reuse_poll;
    pthread_mutexattr_t attributes;
    if(pthread_mutexattr_init(&attributes)||pthread_mutexattr_settype(&attributes,PTHREAD_MUTEX_RECURSIVE)||
       pthread_mutex_init(&nvn_async.api_mutex,&attributes))stop("NVN_ASYNC_API_MUTEX",nvn_caller,0,21);
    pthread_mutexattr_destroy(&attributes);
    if(nvn_async.enabled)printf("NVN_ASYNC_QUEUE_EXPERIMENT enabled=1 submit=fence-flush completion=signaled-only api=serialized barrier=%s\n",nvn_async.lc2_qualified?"finish":"gles-order-flush");
    if(nvn_async.lc2_qualified)printf("NVN_LC2_ASYNC_QUEUE enabled=1 replay=calling-thread gpu=pending-fence completion=real-signal barrier=finish max-pending=%u context=detach-per-api early-signal=forbidden\n",NVN_LC2_ASYNC_PENDING_LIMIT);
}
/* FRENTE-C: the first queue API can run before the EGL display exists, so the automatic decision is taken at
 * the first call that sees a display, and only while no command list was ever submitted (the queue has never
 * run synchronously); otherwise the synchronous queue is kept for the whole run. */
static uint64_t t2_queue_submits_seen; /* incremented by every NI_nvnQueueSubmitCommands */
static void t2_gpu_pipeline_decide(void) {
    static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&lock);
    if(nvn_async.auto_pending&&nvn_device.display!=EGL_NO_DISPLAY) {
        nvn_async.auto_pending=0;
        const char *pipeline=getenv("T2_GPU_PIPELINE");
        const char *excluded[]={"COTM_SINGLE_CONTEXT","COTM_GL_THREAD","COTM_ASYNC_FRAME","MEGAMAN_LOGIC60_RENDER30","COTM_FRAMESKIP"};
        unsigned blocked=pipeline&&!strcmp(pipeline,"0");const char *reason=blocked?"T2_GPU_PIPELINE=0":NULL;
        for(unsigned i=0;!blocked&&i<sizeof(excluded)/sizeof(*excluded);i++){const char *v=getenv(excluded[i]);if(v&&strcmp(v,"0")){blocked=1;reason=excluded[i];}}
        if(!blocked&&t2_queue_submits_seen){blocked=1;reason="submits-before-display";}
        if(!blocked) {
            /* EGL-only probe: the first use may come before any context is current. A GLES client exposing
             * EGL_KHR_fence_sync implements GL_OES_EGL_sync; nvn_egl_fence_init still verifies it on first fence. */
            const char *egl=eglQueryString(nvn_device.display,EGL_EXTENSIONS);
            if(!nvn_egl_extension(egl,"EGL_KHR_fence_sync")||!nvn_egl_extension(egl,"EGL_KHR_wait_sync")){blocked=1;reason="no-EGL_KHR_wait_sync";}
        }
        nvn_async.enabled=nvn_async.automatic=!blocked;
        printf("T2_GPU_PIPELINE mode=auto admitted=%u reason=%s\n",nvn_async.automatic,blocked?reason:"EGL_KHR_wait_sync");
    }
    pthread_mutex_unlock(&lock);
}
static unsigned nvn_async_enabled(void) {
    pthread_once(&nvn_async.once,nvn_async_initialize);
    if(nvn_async.auto_pending&&nvn_device.display!=EGL_NO_DISPLAY)t2_gpu_pipeline_decide();
    return nvn_async.enabled;
}
static unsigned nvn_lc2_async_enabled(void) {nvn_async_enabled();return nvn_async.lc2_qualified;}
/* Early fence (opt-in): every guest-visible input of a batch (uniform bytes,
 * client vertex/index arrays, texture uploads) is consumed by the CPU replay
 * or copied by the driver inside the draw call, so the guest may reuse its
 * memory as soon as SubmitCommands returned. The guest fence is therefore
 * reported signaled at replay completion; real EGL signals still retire
 * tickets (non-blocking poll) and at most NVN_EARLY_MAX_IN_FLIGHT batches
 * stay truly pending on the GPU. Not valid for GPU->guest readbacks. */
#define NVN_EARLY_MAX_IN_FLIGHT 2U
static struct {int configured;unsigned enabled;uint64_t early_signals,bound_waits,polls,retired;} nvn_async_early;
static unsigned nvn_async_early_enabled(void) {
    if(!nvn_async_early.configured) {
        const char *option=getenv("COTM_EARLY_FENCE");
        if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("NVN_EARLY_FENCE_OPTION",nvn_caller,0,24);
        nvn_async_early.enabled=option&&!strcmp(option,"1")&&nvn_async_enabled();nvn_async_early.configured=1;
        if(nvn_async_early.enabled)printf("NVN_EARLY_FENCE_EXPERIMENT enabled=1 guest-signal=replay-complete gpu-retire=poll max-in-flight=%u\n",NVN_EARLY_MAX_IN_FLIGHT);
    }
    return nvn_async_early.enabled;
}
static void nvn_async_api_enter(void){if(nvn_async_enabled())pthread_mutex_lock(&nvn_async.api_mutex);}
static void nvn_async_api_exit(void){if(nvn_async.enabled)pthread_mutex_unlock(&nvn_async.api_mutex);}
static uint64_t nvn_async_nanoseconds(void) {
    struct timespec now;if(clock_gettime(CLOCK_MONOTONIC,&now))stop("NVN_ASYNC_CLOCK",nvn_caller,errno,21);
    return (uint64_t)now.tv_sec*UINT64_C(1000000000)+now.tv_nsec;
}
static struct nvn_async_ticket *nvn_async_ticket_find(uint64_t id) {
    for(unsigned i=0,bound=nvn_async_ticket_bound();i<bound;i++)if(nvn_async_tickets[i].live&&nvn_async_tickets[i].id==id)return &nvn_async_tickets[i];
    stop("NVN_ASYNC_TICKET_INVALID",nvn_caller,id,24);return NULL;
}
static void nvn_async_validate_queue(struct nvn_queue *q) {
    if(!q->live || !q->arena || !q->command_size || q->arena->head<q->arena->tail || q->arena->submitted<q->arena->completed ||
       q->arena->head-q->arena->tail!=q->arena->submitted-q->arena->completed || q->arena->head-q->arena->tail>q->command_size/8)
        stop("NVN_ASYNC_QUEUE_METADATA",nvn_caller,q->object,24);
}
static void nvn_async_collect_locked(void) {
    for(unsigned i=0,bound=nvn_async_ticket_bound();i<bound;i++) {
        struct nvn_async_ticket *t=&nvn_async_tickets[i];
        if(t->live&&!t->queued&&!t->refs){nvn_single_require_owner();nvn_egl_fence_destroy(&t->fence);t->live=0;t->queue=NULL;}
    }
}
static unsigned nvn_async_handle_pending(uint64_t handle) {
    for(unsigned i=0,bound=nvn_async_ticket_bound();i<bound;i++) {
        struct nvn_async_ticket *t=&nvn_async_tickets[i];if(!t->live||!t->queued||t->signaled)continue;
        for(unsigned j=0;j<t->count;j++)if(t->handles[j]==handle)return 1;
    }
    return 0;
}
static void nvn_async_retire_locked(void) {
    /* Each queue advances only through its contiguous signaled prefix. */
    unsigned changed;
    do {
        changed=0;
        for(unsigned i=0,bound=nvn_async_ticket_bound();i<bound;i++) {
            struct nvn_async_ticket *t=&nvn_async_tickets[i];
            if(!t->live||!t->queued||!t->signaled)continue;
            struct nvn_queue *q=t->queue;nvn_async_validate_queue(q);
            if(q->arena->tail!=t->first_head)continue;
            if(q->arena->completed!=t->first_submitted || q->arena->head<t->end_head || q->arena->submitted<t->end_submitted)
                stop("NVN_ASYNC_COMPLETION_ORDER",nvn_caller,t->id,24);
            q->arena->tail=t->end_head;q->arena->completed=t->end_submitted;t->queued=0;t->queue=NULL;changed=1;
            for(unsigned j=0;j<t->count;j++) {
                struct nvn_command_list *list=nvn_command_list_lookup(t->handles[j]);
                if(!list){if(nvn_async_early_enabled())continue;stop("NVN_ASYNC_LIST_LIFETIME",nvn_caller,t->handles[j],24);} /* early mode: guest already recycled the list */
                list->completed=!nvn_async_handle_pending(t->handles[j]);
            }
        }
    }while(changed);
    nvn_async_collect_locked();
}
static unsigned nvn_async_wait_locked(uint64_t id,EGLTimeKHR timeout) {
    struct nvn_async_ticket *t=nvn_async_ticket_find(id);if(t->signaled)return 1;
    nvn_single_require_owner();
    if(t->context!=nvn_device.context)stop("NVN_ASYNC_CONTEXT_CHANGED",nvn_caller,id,24);
    uint64_t started=nvn_async_nanoseconds();unsigned signaled=nvn_egl_fence_wait(t->fence,timeout);
    nvn_async.wait_ns+=nvn_async_nanoseconds()-started;nvn_async.polls++;
    if(!signaled){nvn_async.timeouts++;return 0;}
    uint64_t serial=t->serial;EGLContext context=t->context;
    /* Later producer fences prove earlier producer commands complete. No
     * metadata is advanced until that real signal has been observed. */
    for(unsigned i=0,bound=nvn_async_ticket_bound();i<bound;i++) {
        struct nvn_async_ticket *earlier=&nvn_async_tickets[i];
        if(earlier->live&&earlier->context==context&&earlier->id<=id)earlier->signaled=1;
    }
    if(serial==nvn_gl_completion.use_serial) {
        nvn_gl_completion.completed_serial=serial;nvn_gl_completion.completion_valid=1;
    }
    nvn_async.observed++;nvn_gl_completion.real_waits++;nvn_async_retire_locked();return 1;
}
static uint64_t nvn_async_oldest_pending_locked(unsigned *pending) {
    uint64_t oldest=0;*pending=0;
    for(unsigned i=0,bound=nvn_async_ticket_bound();i<bound;i++) {
        struct nvn_async_ticket *t=&nvn_async_tickets[i];
        if(t->live&&t->queued&&!t->signaled){(*pending)++;if(!oldest||t->id<oldest)oldest=t->id;}
    }
    return oldest;
}
/* API mutex is already held by the dispatcher (recursive for native memory
 * callbacks). Recording/reuse never retains a producer context across APIs.
 * Polling needs no current context: every ticket was flushed before detach. */
static void nvn_lc2_async_reuse_poll(uint64_t handle) {
    if(!nvn_lc2_async_enabled())return;
    pthread_mutex_lock(&nvn_gl_completion.mutex);
    uint64_t newest=0;
    for(unsigned i=0,bound=nvn_async_ticket_bound();i<bound;i++) {
        const struct nvn_async_ticket *t=&nvn_async_tickets[i];
        if(!t->live||!t->queued||t->signaled)continue;
        for(unsigned j=0;j<t->count;j++)if(t->handles[j]==handle&&t->id>newest)newest=t->id;
    }
    if(newest&&!nvn_async_wait_locked(newest,0)) {
        nvn_async.lc2_reuse_waits++;
        if(!nvn_async_wait_locked(newest,UINT64_C(2000000000)))nvn_async.lc2_reuse_timeouts++;
    }
    pthread_mutex_unlock(&nvn_gl_completion.mutex);
}
static void nvn_lc2_async_admit_locked(void) {
    if(!nvn_lc2_async_enabled())return;
    unsigned pending;uint64_t id;
    while((id=nvn_async_oldest_pending_locked(&pending))) {
        if(nvn_async_wait_locked(id,0))continue;
        if(pending<NVN_LC2_ASYNC_PENDING_LIMIT)return;
        if(!nvn_async_wait_locked(id,UINT64_C(2000000000)))stop("NVN_ASYNC_QUEUE_TIMEOUT",nvn_caller,id,22);
    }
}
/* Retire already-signaled batches without blocking, then bound the GPU backlog. */
static void nvn_async_early_settle_locked(void) {
    unsigned pending;uint64_t id;
    while((id=nvn_async_oldest_pending_locked(&pending))) {
        nvn_async_early.polls++;
        if(nvn_async_wait_locked(id,0)){nvn_async_early.retired++;continue;}
        if(pending<=NVN_EARLY_MAX_IN_FLIGHT)break;
        nvn_async_early.bound_waits++;
        if(!nvn_async_wait_locked(id,UINT64_C(2000000000)))stop("NVN_ASYNC_QUEUE_TIMEOUT",nvn_caller,id,22);
    }
}
static struct nvn_async_ticket *nvn_async_new_locked(struct nvn_queue *q,unsigned refs) {
    nvn_single_require_owner();
    unsigned slot=0;while(slot<NVN_ASYNC_TICKETS&&nvn_async_tickets[slot].live)slot++;
    if(slot==NVN_ASYNC_TICKETS)stop("NVN_ASYNC_TICKET_CAPACITY",nvn_caller,slot,21);
    if(nvn_async.next_id==UINT64_MAX)stop("NVN_ASYNC_TICKET_WRAP",nvn_caller,0,24);
    EGLSyncKHR fence=nvn_egl_fence_create_current();
    struct nvn_async_ticket *t=&nvn_async_tickets[slot];
    *t=(struct nvn_async_ticket){.id=++nvn_async.next_id,.serial=nvn_gl_completion.use_serial,.fence=fence,
        .context=nvn_device.context,.queue=q,.refs=refs,.live=1,.queued=q!=NULL};
    if(slot>=nvn_async.ticket_high_water)nvn_async.ticket_high_water=slot+1;
    nvn_async.created++;return t;
}
static void nvn_async_release_locked(uint64_t id) {
    if(!id)return;
    struct nvn_async_ticket *t=nvn_async_ticket_find(id);
    if(!t->refs)stop("NVN_ASYNC_REFERENCE",nvn_caller,id,24);
    t->refs--;nvn_async_collect_locked();
}
static uint64_t nvn_async_oldest_queue(struct nvn_queue *q) {
    uint64_t oldest=0;
    for(unsigned i=0,bound=nvn_async_ticket_bound();i<bound;i++) {
        struct nvn_async_ticket *t=&nvn_async_tickets[i];
        if(t->live&&t->queued&&t->queue==q&&(!oldest||t->id<oldest))oldest=t->id;
    }
    if(oldest) {
        struct nvn_async_ticket *t=nvn_async_ticket_find(oldest);
        if(t->first_head!=q->arena->tail||t->first_submitted!=q->arena->completed)
            stop("NVN_ASYNC_COMPLETION_ORDER",nvn_caller,oldest,24);
    }
    return oldest;
}
static void nvn_async_wait_queue_locked(struct nvn_queue *q) {
    nvn_async_validate_queue(q);
    while(q->arena->head!=q->arena->tail) {
        uint64_t id=nvn_async_oldest_queue(q);if(!id)stop("NVN_ASYNC_COMPLETION_UNPROVED",nvn_caller,q->object,24);
        if(!nvn_async_wait_locked(id,UINT64_C(2000000000)))stop("NVN_ASYNC_QUEUE_TIMEOUT",nvn_caller,id,22);
    }
}
static void nvn_async_admit_locked(struct nvn_queue *q,unsigned count) {
    nvn_async_validate_queue(q);
    if(count)nvn_lc2_async_admit_locked();
    if(count>q->command_size/8)stop("NVN_ASYNC_RING_CAPACITY",nvn_caller,count,24);
    while(count>q->command_size/8-(q->arena->head-q->arena->tail)) {
        uint64_t id=nvn_async_oldest_queue(q);if(!id)stop("NVN_ASYNC_RING_UNPROVED",nvn_caller,q->object,24);
        if(!nvn_async_wait_locked(id,UINT64_C(2000000000)))stop("NVN_ASYNC_QUEUE_TIMEOUT",nvn_caller,id,22);
    }
    if(q->arena->head>UINT64_MAX-count || q->arena->submitted>UINT64_MAX-count)
        stop("NVN_ASYNC_QUEUE_WRAP",nvn_caller,q->object,24);
    unsigned free_slot=0;while(free_slot<NVN_ASYNC_TICKETS&&nvn_async_tickets[free_slot].live)free_slot++;
    if(count && free_slot==NVN_ASYNC_TICKETS)stop("NVN_ASYNC_TICKET_CAPACITY",nvn_caller,free_slot,21);
    if(count && nvn_async.next_id==UINT64_MAX)stop("NVN_ASYNC_TICKET_WRAP",nvn_caller,0,24);
}
static void nvn_async_submit_locked(struct nvn_queue *q,const uint64_t *handles,unsigned count,uint64_t first_head,uint64_t first_submitted) {
    if(!count)return;
    struct nvn_async_ticket *t=nvn_async_new_locked(q,0);t->count=count;
    memcpy(t->handles,handles,count*sizeof(*handles));t->first_head=first_head;t->first_submitted=first_submitted;
    t->end_head=q->arena->head;t->end_submitted=q->arena->submitted;
}
static uint64_t nvn_async_fence_queue_locked(struct nvn_queue *q) {
    nvn_single_require_owner();nvn_async_validate_queue(q);
    if(nvn_gl_completion.completion_valid&&nvn_gl_completion.completed_serial==nvn_gl_completion.use_serial) {
        nvn_async_wait_queue_locked(q);return 0;
    }
    for(unsigned i=0,bound=nvn_async_ticket_bound();i<bound;i++) {
        struct nvn_async_ticket *t=&nvn_async_tickets[i];
        if(t->live&&t->serial==nvn_gl_completion.use_serial&&t->context==nvn_device.context){t->refs++;return t->id;}
    }
    nvn_gl_acquire_locked();struct nvn_async_ticket *t=nvn_async_new_locked(NULL,1);uint64_t id=t->id;
    /* The caller keeps the context mutex; only detach here. */
    GLenum error=glGetError();if(error)stop("NVN_ASYNC_GL_ERROR",nvn_caller,error,21);
    if(!nvn_single_detach())stop("NVN_QUEUE_DETACH",nvn_caller,eglGetError(),21);
    return id;
}
static void nvn_async_queue_complete(struct nvn_queue *q,unsigned finish) {
    pthread_mutex_lock(&nvn_gl_completion.mutex);nvn_single_require_owner();nvn_async_validate_queue(q);
    if(finish)nvn_async_wait_queue_locked(q);
    if(nvn_gl_completion.completion_valid&&nvn_gl_completion.completed_serial==nvn_gl_completion.use_serial) {
        nvn_gl_completion.skipped_waits++;pthread_mutex_unlock(&nvn_gl_completion.mutex);return;
    }
    if(finish) {
        uint64_t id=nvn_async_fence_queue_locked(q);
        if(id){if(!nvn_async_wait_locked(id,UINT64_C(2000000000)))stop("NVN_ASYNC_QUEUE_TIMEOUT",nvn_caller,id,22);nvn_async_release_locked(id);}
        pthread_mutex_unlock(&nvn_gl_completion.mutex);return;
    }
    /* Every live ticket was explicitly flushed in its producing context.
     * A later acquire/upload invalidates that proof and needs a new flush. */
    for(unsigned i=0,bound=nvn_async_ticket_bound();i<bound;i++)if(nvn_async_tickets[i].live&&nvn_async_tickets[i].serial==nvn_gl_completion.use_serial) {
        pthread_mutex_unlock(&nvn_gl_completion.mutex);return;
    }
    nvn_gl_acquire_locked();glFlush();nvn_gl_release();
}
/* FRENTE-C: T2_PRESENT_GPU_WAIT=1 (async queue, separate present context). Instead of blocking the guest
 * render thread until the frame's GPU work is done (CPU wait before the present quad), the present context
 * issues eglWaitSyncKHR on the newest pending producer fence: the GPU orders the quad after the frame, the
 * CPU returns to the guest at once. Admitted only when the display exposes EGL_KHR_wait_sync (capability,
 * e.g. Mali-G31); otherwise the CPU wait stays. Tickets still retire only on a real signal. */
static struct {int configured;unsigned enabled;PFNEGLWAITSYNCKHRPROC wait;EGLSyncKHR pending;uint64_t gpu_waits,cpu_waits,cpu_wait_ns;} t2_present_gpu_wait;
static unsigned t2_present_gpu_wait_enabled(void) {
    if(!t2_present_gpu_wait.configured) {
        t2_present_gpu_wait.configured=1;
        const char *o=getenv("T2_PRESENT_GPU_WAIT");
        if(((o&&!strcmp(o,"1"))||(!o&&nvn_async.automatic))&&nvn_async_enabled()) {
            nvn_egl_fence_init();
            if(nvn_egl_extension(eglQueryString(nvn_device.display,EGL_EXTENSIONS),"EGL_KHR_wait_sync"))
                t2_present_gpu_wait.wait=(PFNEGLWAITSYNCKHRPROC)eglGetProcAddress("eglWaitSyncKHR");
            t2_present_gpu_wait.enabled=t2_present_gpu_wait.wait!=NULL;
        }
        if((o&&!strcmp(o,"1"))||(!o&&nvn_async.automatic))printf("T2_PRESENT_GPU_WAIT requested=1 admitted=%u reason=%s\n",t2_present_gpu_wait.enabled,
            !nvn_async_enabled()?"needs-COTM_ASYNC_QUEUE=1":t2_present_gpu_wait.enabled?"EGL_KHR_wait_sync":"no-EGL_KHR_wait_sync");
    }
    return t2_present_gpu_wait.enabled;
}
/* Present context is current: make its GPU queue wait for the producer's frame. */
static void t2_present_gpu_wait_issue(void) {
    if(!t2_present_gpu_wait.pending)return;
    if(t2_present_gpu_wait.wait(nvn_device.display,t2_present_gpu_wait.pending,0)!=EGL_TRUE)
        stop("T2_PRESENT_GPU_WAIT_FAILED",nvn_caller,eglGetError(),21);
    t2_present_gpu_wait.pending=EGL_NO_SYNC_KHR;t2_present_gpu_wait.gpu_waits++;
}
static void nvn_async_present_wait_locked(struct nvn_queue *q,unsigned same_context) {
    if(!nvn_async_enabled()) {
        if(q->arena->head!=q->arena->tail)stop("NVN_PRESENT_OWNERSHIP",nvn_caller,q->object,24);
        return;
    }
    nvn_single_require_owner();nvn_async_validate_queue(q);
    if(!same_context&&t2_present_gpu_wait_enabled()) {
        struct nvn_async_ticket *newest=NULL;
        for(unsigned i=0,bound=nvn_async_ticket_bound();i<bound;i++) {
            struct nvn_async_ticket *t=&nvn_async_tickets[i];
            if(t->live&&t->queued&&!t->signaled&&t->queue==q&&(!newest||t->id>newest->id))newest=t;
        }
        /* Held under the completion mutex until the quad is issued: the ticket cannot be collected meanwhile. */
        t2_present_gpu_wait.pending=newest?newest->fence:EGL_NO_SYNC_KHR;
        return;
    }
    if(!same_context){uint64_t t0=nvn_async_nanoseconds();nvn_async_wait_queue_locked(q);
        t2_present_gpu_wait.cpu_waits++;t2_present_gpu_wait.cpu_wait_ns+=nvn_async_nanoseconds()-t0;}
    else if(q->arena->head!=q->arena->tail&&!nvn_async_oldest_queue(q))
        stop("NVN_ASYNC_PRESENT_UNPROVED",nvn_caller,q->object,24);
}
static void nvn_async_report(unsigned frame) {
    if(!nvn_async.enabled||frame<nvn_async.after_frame||frame-nvn_async.after_frame<60)return;
    printf("NVN_ASYNC_QUEUE_PROFILE after-frame=%u frames=%"PRIu64" batches-and-fences=%"PRIu64" observed=%"PRIu64" waits=%"PRIu64" timeouts=%"PRIu64" wait-ms=%.3f completion=real-signal\n",
        frame,(uint64_t)frame-nvn_async.after_frame,nvn_async.created,nvn_async.observed,nvn_async.polls,nvn_async.timeouts,
        nvn_async.wait_ns*1e-6/(frame-nvn_async.after_frame));
    printf("T2_PRESENT_WAIT_PROFILE after-frame=%u gpu-waits=%"PRIu64" cpu-waits=%"PRIu64" cpu-wait-ms-per-frame=%.3f scope=since-last-report\n",
        frame,t2_present_gpu_wait.gpu_waits,t2_present_gpu_wait.cpu_waits,t2_present_gpu_wait.cpu_wait_ns*1e-6/(frame-nvn_async.after_frame));
    t2_present_gpu_wait.gpu_waits=t2_present_gpu_wait.cpu_waits=t2_present_gpu_wait.cpu_wait_ns=0;
    if(nvn_async_early.enabled) {
        printf("NVN_EARLY_FENCE_PROFILE after-frame=%u early-signals=%"PRIu64" polls=%"PRIu64" retired=%"PRIu64" bound-waits=%"PRIu64" max-in-flight=%u scope=since-last-report\n",
            frame,nvn_async_early.early_signals,nvn_async_early.polls,nvn_async_early.retired,nvn_async_early.bound_waits,NVN_EARLY_MAX_IN_FLIGHT);
        nvn_async_early.early_signals=nvn_async_early.polls=nvn_async_early.retired=nvn_async_early.bound_waits=0;
    }
    if(nvn_async.lc2_qualified)printf("NVN_LC2_ASYNC_REUSE_PROFILE after-frame=%u waits=%"PRIu64" timeouts=%"PRIu64" limit-ns=2000000000 completion=real-signal scope=cumulative ticket-scan-bound=%u capacity=%u bound=monotonic-published\n",
        frame,nvn_async.lc2_reuse_waits,nvn_async.lc2_reuse_timeouts,nvn_async_ticket_bound(),NVN_ASYNC_TICKETS);
    nvn_async.after_frame=frame;nvn_async.created=nvn_async.observed=nvn_async.polls=nvn_async.timeouts=nvn_async.wait_ns=0;
}
