/* Port-local EGL fence ownership. A flush submits work; only a signaled fence
 * proves completion. Poll/timeout never consumes or completes a pending fence. */
#include <EGL/eglext.h>
static struct {
    PFNEGLCREATESYNCKHRPROC create;
    PFNEGLDESTROYSYNCKHRPROC destroy;
    PFNEGLCLIENTWAITSYNCKHRPROC wait;
    unsigned initialized;
    EGLSyncKHR present_reader;
    uint64_t reader_wait_ns,reader_waits;
} nvn_egl_fences;
static unsigned nvn_egl_extension(const char *list,const char *name) {
    if(!list)return 0;
    size_t n=strlen(name);const char *p=list;
    while((p=strstr(p,name))){if((p==list||p[-1]==' ')&&(p[n]==' '||!p[n]))return 1;p+=n;}
    return 0;
}
static void nvn_egl_fence_init(void) {
    if(nvn_egl_fences.initialized)return;
    if(!nvn_egl_extension(eglQueryString(nvn_device.display,EGL_EXTENSIONS),"EGL_KHR_fence_sync") ||
       !nvn_egl_extension((const char *)glGetString(GL_EXTENSIONS),"GL_OES_EGL_sync"))
        stop("NVN_EGL_FENCE_EXTENSION_REQUIRED",nvn_caller,0,20);
    nvn_egl_fences.create=(PFNEGLCREATESYNCKHRPROC)eglGetProcAddress("eglCreateSyncKHR");
    nvn_egl_fences.destroy=(PFNEGLDESTROYSYNCKHRPROC)eglGetProcAddress("eglDestroySyncKHR");
    nvn_egl_fences.wait=(PFNEGLCLIENTWAITSYNCKHRPROC)eglGetProcAddress("eglClientWaitSyncKHR");
    if(!nvn_egl_fences.create||!nvn_egl_fences.destroy||!nvn_egl_fences.wait)
        stop("NVN_EGL_FENCE_ENTRYPOINT_REQUIRED",nvn_caller,0,20);
    nvn_egl_fences.initialized=1;
    printf("NVN_EGL_FENCE_BACKEND egl=KHR_fence_sync gl=OES_EGL_sync completion=signaled-only\n");
}
static EGLSyncKHR nvn_egl_fence_create_current(void) {
    nvn_egl_fence_init();
    EGLSyncKHR fence=nvn_egl_fences.create(nvn_device.display,EGL_SYNC_FENCE_KHR,NULL);
    if(fence==EGL_NO_SYNC_KHR)stop("NVN_EGL_FENCE_CREATE",nvn_caller,eglGetError(),21);
    glFlush();GLenum error=glGetError();
    if(error)stop("NVN_EGL_FENCE_FLUSH",nvn_caller,error,21);
    return fence;
}
/* Physical 1014 (.26): the Mali blob's eglClientWaitSyncKHR spins a whole
 * core while the GPU works (replay thread ~70% CPU, present-ms=138), which is
 * pure heat. ZERO_FENCE_SLEEP_POLL=1 replaces a timed wait by zero-timeout
 * polls separated by a short sleep. Completion is still only the driver's
 * real signal; the timeout contract is unchanged (bounded by the deadline). */
static struct {unsigned configured,enabled;uint64_t polls,sleeps;} nvn_egl_fence_poll;
static unsigned nvn_egl_fence_sleep_poll_enabled(void) {
    if(!nvn_egl_fence_poll.configured) {
        const char *option=getenv("ZERO_FENCE_SLEEP_POLL");
        if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("ZERO_FENCE_SLEEP_POLL_OPTION",nvn_caller,0,24);
        nvn_egl_fence_poll.enabled=option&&!strcmp(option,"1");nvn_egl_fence_poll.configured=1;
        if(nvn_egl_fence_poll.enabled)printf("ZERO_FENCE_SLEEP_POLL enabled=1 poll-timeout=0 sleep-us=500 completion=driver-signal-only deadline=unchanged\n");
    }
    return nvn_egl_fence_poll.enabled;
}
static unsigned nvn_egl_fence_wait(EGLSyncKHR fence,EGLTimeKHR timeout) {
    if(!fence||!nvn_egl_fences.initialized)stop("NVN_EGL_FENCE_INVALID",nvn_caller,0,24);
    if(timeout && timeout!=EGL_FOREVER_KHR && nvn_egl_fence_sleep_poll_enabled()) {
        struct timespec now;if(clock_gettime(CLOCK_MONOTONIC,&now))stop("NVN_EGL_FENCE_CLOCK",nvn_caller,errno,21);
        uint64_t deadline=(uint64_t)now.tv_sec*1000000000ULL+now.tv_nsec+timeout;
        for(;;) {
            EGLint result=nvn_egl_fences.wait(nvn_device.display,fence,0,0);nvn_egl_fence_poll.polls++;
            if(result==EGL_CONDITION_SATISFIED_KHR)return 1;
            if(result!=EGL_TIMEOUT_EXPIRED_KHR)stop("NVN_EGL_FENCE_WAIT",nvn_caller,eglGetError(),21);
            if(clock_gettime(CLOCK_MONOTONIC,&now))stop("NVN_EGL_FENCE_CLOCK",nvn_caller,errno,21);
            if((uint64_t)now.tv_sec*1000000000ULL+now.tv_nsec>=deadline)return 0;
            struct timespec pause={0,500000};nanosleep(&pause,NULL);nvn_egl_fence_poll.sleeps++;
        }
    }
    EGLint result=nvn_egl_fences.wait(nvn_device.display,fence,0,timeout);
    if(result==EGL_CONDITION_SATISFIED_KHR)return 1;
    if(result==EGL_TIMEOUT_EXPIRED_KHR)return 0;
    stop("NVN_EGL_FENCE_WAIT",nvn_caller,eglGetError(),21);return 0;
}
static void nvn_egl_fence_destroy(EGLSyncKHR *fence) {
    if(!*fence||!nvn_egl_fences.destroy(nvn_device.display,*fence))
        stop("NVN_EGL_FENCE_DESTROY",nvn_caller,eglGetError(),21);
    *fence=EGL_NO_SYNC_KHR;
}
static void nvn_egl_present_reader_wait(void) {
    if(!nvn_egl_fences.present_reader)return;
    struct timespec start,end;clock_gettime(CLOCK_MONOTONIC,&start);
    /* No current context is necessary: create_current flushed the owning
     * context before detach. Bound a broken-driver wait to2seconds. */
    if(!nvn_egl_fence_wait(nvn_egl_fences.present_reader,UINT64_C(2000000000)))
        stop("NVN_PRESENT_READER_TIMEOUT",nvn_caller,0,22);
    nvn_egl_fence_destroy(&nvn_egl_fences.present_reader);
    clock_gettime(CLOCK_MONOTONIC,&end);
    nvn_egl_fences.reader_wait_ns+=(end.tv_sec-start.tv_sec)*UINT64_C(1000000000)+end.tv_nsec-start.tv_nsec;
    nvn_egl_fences.reader_waits++;
}
