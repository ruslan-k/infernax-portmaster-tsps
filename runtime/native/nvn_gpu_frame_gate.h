#ifndef COTM_NVN_GPU_FRAME_GATE_H
#define COTM_NVN_GPU_FRAME_GATE_H
/* Experimental host scheduling only. This fence never changes guest sync
 * tickets, queue completion, or the 60Hz guest loop. The worker exclusively
 * owns it; only a real swap creates it. */
enum {NVN_GFG_FRAME,NVN_GFG_PRESENT,NVN_GFG_TEARDOWN};
static struct {
    unsigned enabled;
    EGLSyncKHR pending;
    EGLDisplay display;
    EGLContext context;
    unsigned pending_frame;
    uint64_t created,waits[3],wait_ns,max_wait_ns;
} nvn_gpu_frame_gate;
static pthread_once_t nvn_gpu_frame_gate_once=PTHREAD_ONCE_INIT;
static void nvn_gpu_frame_gate_configure(void) {
    const char *option=getenv("COTM_GPU_FRAME_GATE");
    if(option&&strcmp(option,"0")&&strcmp(option,"1")&&strcmp(option,"2"))stop("NVN_GPU_FRAME_GATE_OPTION",nvn_caller,0,24);
    nvn_gpu_frame_gate.enabled=option?(unsigned)(option[0]-'0'):0;
    if(nvn_gpu_frame_gate.enabled) {
        if(!nvn_gl_worker_enabled()||!nvn_async_frame_enabled()||!nvn_single_enabled())
            stop("NVN_GPU_FRAME_GATE_DEPENDENCY",nvn_caller,0,24);
        printf("NVN_GPU_FRAME_GATE enabled=1 mode=%u scope=paired-FRAME-PRESENT-groups max-paired-groups=%u fence-handles=1 fence=after-real-swap wait=%s guest-completion=unchanged timeout-ms=2000\n",
               nvn_gpu_frame_gate.enabled,nvn_gpu_frame_gate.enabled==2?2:1,
               nvn_gpu_frame_gate.enabled==2?"before-next-PRESENT":"before-next-FRAME");
    }
}
static unsigned nvn_gpu_frame_gate_enabled(void) {
    if(pthread_once(&nvn_gpu_frame_gate_once,nvn_gpu_frame_gate_configure))
        stop("NVN_GPU_FRAME_GATE_ONCE",nvn_caller,0,21);
    return nvn_gpu_frame_gate.enabled;
}
static void nvn_gpu_frame_gate_owner(void) {
    if(!nvn_gl_worker_is_self())stop("NVN_GPU_FRAME_GATE_OWNER",nvn_caller,0,24);
    nvn_single_require_owner();
}
static void nvn_gpu_frame_gate_context(void) {
    nvn_gpu_frame_gate_owner();
    if(!nvn_device.live||nvn_device.display==EGL_NO_DISPLAY||nvn_device.context==EGL_NO_CONTEXT||
       eglGetCurrentDisplay()!=nvn_device.display||eglGetCurrentContext()!=nvn_device.context)
        stop("NVN_GPU_FRAME_GATE_CONTEXT",nvn_caller,0,24);
    if(nvn_gpu_frame_gate.pending && (nvn_gpu_frame_gate.display!=nvn_device.display||
       nvn_gpu_frame_gate.context!=nvn_device.context))
        stop("NVN_GPU_FRAME_GATE_IDENTITY",nvn_caller,nvn_gpu_frame_gate.pending_frame,24);
}
/* Caller has already removed/pinned the FRAME job and released worker.mutex.
 * PRESENT uses this before drawing another quad, including repeated PRESENT
 * with no new FRAME. A skipped present never calls this or after_swap. */
static void nvn_gpu_frame_gate_wait(unsigned reason) {
    if(!nvn_gpu_frame_gate_enabled())return;
    nvn_gpu_frame_gate_owner();
    if(reason>NVN_GFG_TEARDOWN)stop("NVN_GPU_FRAME_GATE_REASON",nvn_caller,reason,24);
    /* Mode2 permits replay N while the GPU completes group N-1. Its private
     * fence is retained until before PRESENT N; teardown still drains it. */
    if(reason==NVN_GFG_FRAME&&nvn_gpu_frame_gate.enabled==2)return;
    if(!nvn_gpu_frame_gate.pending)return;
    nvn_gpu_frame_gate_context();
    uint64_t started=nvn_nanoseconds();
    if(!nvn_egl_fence_wait(nvn_gpu_frame_gate.pending,UINT64_C(2000000000)))
        stop("NVN_GPU_FRAME_GATE_TIMEOUT",nvn_caller,nvn_gpu_frame_gate.pending_frame,22);
    nvn_egl_fence_destroy(&nvn_gpu_frame_gate.pending);
    uint64_t elapsed=nvn_nanoseconds()-started;
    nvn_gpu_frame_gate.waits[reason]++;nvn_gpu_frame_gate.wait_ns+=elapsed;
    if(elapsed>nvn_gpu_frame_gate.max_wait_ns)nvn_gpu_frame_gate.max_wait_ns=elapsed;
    nvn_gpu_frame_gate.display=EGL_NO_DISPLAY;nvn_gpu_frame_gate.context=EGL_NO_CONTEXT;
    nvn_gpu_frame_gate.pending_frame=0;
}
static void nvn_gpu_frame_gate_after_swap(unsigned frame) {
    if(!nvn_gpu_frame_gate_enabled())return;
    nvn_gpu_frame_gate_context();
    if(nvn_gpu_frame_gate.pending)stop("NVN_GPU_FRAME_GATE_OVERWRITE",nvn_caller,frame,24);
    nvn_gpu_frame_gate.pending=nvn_egl_fence_create_current();
    nvn_gpu_frame_gate.display=nvn_device.display;nvn_gpu_frame_gate.context=nvn_device.context;
    nvn_gpu_frame_gate.pending_frame=frame;nvn_gpu_frame_gate.created++;
}
static void nvn_gpu_frame_gate_report(unsigned frame,unsigned frames) {
    if(!nvn_gpu_frame_gate_enabled())return;
    nvn_gpu_frame_gate_owner();
    printf("NVN_GPU_FRAME_GATE_PROFILE frame=%u frames=%u mode=%u max-paired-groups=%u created=%"PRIu64" frame-waits=%"PRIu64
           " present-waits=%"PRIu64" teardown-waits=%"PRIu64" wait-total-ms=%.3f wait-ms-per-frame=%.3f max-wait-ms=%.3f pending=%u scope=since-last-report wait-location=outside-replay\n",
           frame,frames,nvn_gpu_frame_gate.enabled,nvn_gpu_frame_gate.enabled==2?2:1,nvn_gpu_frame_gate.created,nvn_gpu_frame_gate.waits[NVN_GFG_FRAME],
           nvn_gpu_frame_gate.waits[NVN_GFG_PRESENT],nvn_gpu_frame_gate.waits[NVN_GFG_TEARDOWN],
           nvn_gpu_frame_gate.wait_ns*1e-6,frames?nvn_gpu_frame_gate.wait_ns*1e-6/frames:0.0,
           nvn_gpu_frame_gate.max_wait_ns*1e-6,nvn_gpu_frame_gate.pending!=EGL_NO_SYNC_KHR);
    nvn_gpu_frame_gate.created=0;memset(nvn_gpu_frame_gate.waits,0,sizeof(nvn_gpu_frame_gate.waits));
    nvn_gpu_frame_gate.wait_ns=nvn_gpu_frame_gate.max_wait_ns=0;
}
#endif
