/* A5 opt-in: the host main pthread owns one GLES context permanently.
 * Logical acquisitions still invalidate render/completion epochs. Only the
 * EGL bind/detach calls are elided; no work is declared complete here. */
#include <stdatomic.h>
static struct {
    unsigned enabled,transfer_enabled,bootstrap_detached;
    _Atomic unsigned owner_ready;
    _Atomic unsigned transferred;
    pthread_t owner,render_owner;
    pid_t render_tid;
    EGLDisplay display;
    EGLSurface draw,read;
    EGLContext context;
    uint64_t make_current_calls,bind_skips,detach_skips,owner_checks,brackets,bootstrap_detaches;
} nvn_single_state;
static pthread_once_t nvn_single_once=PTHREAD_ONCE_INIT;
static void nvn_single_configure(void) {
    const char *option=getenv("COTM_SINGLE_CONTEXT");
    if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("NVN_SINGLE_CONTEXT_OPTION",nvn_caller,0,24);
    nvn_single_state.enabled=option&&!strcmp(option,"1");
    const char *transfer=getenv("MEGAMAN_SINGLE_CONTEXT_TRANSFER");
    if(transfer&&strcmp(transfer,"0")&&strcmp(transfer,"1"))stop("NVN_LC2_SINGLE_TRANSFER_OPTION",nvn_caller,0,24);
    nvn_single_state.transfer_enabled=transfer&&!strcmp(transfer,"1");
    if(nvn_single_state.transfer_enabled) {
#ifndef LC2_MAIN_BASE
        stop("NVN_LC2_SINGLE_TRANSFER_PORT",nvn_caller,0,24);
#endif
        if(!nvn_single_state.enabled)stop("NVN_LC2_SINGLE_TRANSFER_DEPENDENCY",nvn_caller,0,24);
        const char *incompatible[]={"COTM_GL_THREAD","COTM_ASYNC_FRAME","COTM_ASYNC_QUEUE","COTM_GAME_PBUFFER","COTM_PRESENT_LEAN"};
        for(unsigned i=0;i<sizeof(incompatible)/sizeof(incompatible[0]);i++) {
            const char *value=getenv(incompatible[i]);
            if(value&&strcmp(value,"0"))stop("NVN_LC2_SINGLE_TRANSFER_INCOMPATIBLE",nvn_caller,i,24);
        }
    }
}
static unsigned nvn_single_enabled(void) {
    if(pthread_once(&nvn_single_once,nvn_single_configure))stop("NVN_SINGLE_CONTEXT_ONCE",nvn_caller,0,21);
    return nvn_single_state.enabled;
}
static void nvn_single_require_owner(void) {
    if(!nvn_single_enabled())return;
    if(nvn_single_state.transfer_enabled&&atomic_load_explicit(&nvn_single_state.transferred,memory_order_acquire)) {
        if(guest_thread_index!=1||!pthread_equal(nvn_single_state.render_owner,pthread_self()))
            stop("NVN_SINGLE_CONTEXT_OWNER",nvn_caller,guest_thread_index,24);
        nvn_single_state.owner_checks++;return;
    }
    if(nvn_gl_worker_enabled()) {
        if(!nvn_gl_worker_is_self())stop("NVN_SINGLE_CONTEXT_OWNER",nvn_caller,guest_thread_index,24);
        if(!atomic_load_explicit(&nvn_single_state.owner_ready,memory_order_acquire)) {
            nvn_single_state.owner=pthread_self();atomic_store_explicit(&nvn_single_state.owner_ready,1,memory_order_release);
            printf("NVN_SINGLE_CONTEXT enabled=1 owner=gl-worker context-count=1 state=present-bracket\n");
        }
        nvn_single_state.owner_checks++;return;
    }
    if(!atomic_load_explicit(&nvn_single_state.owner_ready,memory_order_acquire)) {
        /* guest_thread_index alone cannot distinguish a new host pthread
         * whose zero-initialized TLS also says guest thread zero. */
        if(guest_thread_index || (pid_t)syscall(SYS_gettid)!=getpid())
            stop("NVN_SINGLE_CONTEXT_OWNER",nvn_caller,guest_thread_index,24);
        nvn_single_state.owner=pthread_self();
        atomic_store_explicit(&nvn_single_state.owner_ready,1,memory_order_release);
        printf("NVN_SINGLE_CONTEXT enabled=1 owner=host-main owner-tid=%ld guest-thread=0 context-count=1 pbuffer-producer=unsupported state=present-bracket\n",(long)getpid());
    }
    if(guest_thread_index || !pthread_equal(nvn_single_state.owner,pthread_self()))
        stop("NVN_SINGLE_CONTEXT_OWNER",nvn_caller,guest_thread_index,24);
    nvn_single_state.owner_checks++;
}
/* The only transfer call site is nvn_gl_acquire_locked, under the existing
 * completion mutex. Startup owner remains immutable; publish a separate
 * render pthread only after the previous owner really detached its binding.
 * LC2's first renderer acquire is its window-color TextureInitialize. */
static void nvn_single_transfer_acquire_locked(void) {
    if(!nvn_single_enabled()||!nvn_single_state.transfer_enabled)return;
    if(atomic_load_explicit(&nvn_single_state.transferred,memory_order_acquire)||!guest_thread_index) {
        nvn_single_require_owner();return;
    }
#ifdef LC2_MAIN_BASE
    if(guest_thread_index!=1 || !atomic_load_explicit(&nvn_single_state.owner_ready,memory_order_acquire) ||
       !nvn_device.live || !nvn_single_state.bootstrap_detached ||
       nvn_single_state.context!=EGL_NO_CONTEXT ||
       pthread_equal(nvn_single_state.owner,pthread_self()) ||
       nvn_caller!=LC2_MAIN_BASE+0x37df64ULL || eglGetCurrentContext()!=EGL_NO_CONTEXT)
        stop("NVN_LC2_SINGLE_TRANSFER_CONTRACT",nvn_caller,guest_thread_index,24);
    nvn_single_state.render_owner=pthread_self();nvn_single_state.render_tid=(pid_t)syscall(SYS_gettid);
    atomic_store_explicit(&nvn_single_state.transferred,1,memory_order_release);
    printf("NVN_LC2_SINGLE_CONTEXT_TRANSFER from-guest=0 to-guest=1 owner-tid=%ld bootstrap-detaches=%"PRIu64" caller=0x%"PRIx64" context-count=1 completion=unchanged\n",
        (long)nvn_single_state.render_tid,nvn_single_state.bootstrap_detaches,nvn_caller);
#else
    stop("NVN_LC2_SINGLE_TRANSFER_PORT",nvn_caller,0,24);
#endif
}
static EGLBoolean nvn_single_make_current(EGLSurface draw,EGLSurface read,EGLContext context) {
    if(!nvn_single_enabled())return eglMakeCurrent(nvn_device.display,draw,read,context);
    nvn_single_require_owner();
    if(context==EGL_NO_CONTEXT || context!=nvn_device.context || draw==EGL_NO_SURFACE || read==EGL_NO_SURFACE)
        stop("NVN_SINGLE_CONTEXT_BIND",nvn_caller,0,24);
    if(nvn_single_state.context==context && nvn_single_state.display==nvn_device.display &&
       nvn_single_state.draw==draw && nvn_single_state.read==read) {
        nvn_single_state.bind_skips++;return EGL_TRUE;
    }
    nvn_single_state.make_current_calls++;
    EGLBoolean ok=eglMakeCurrent(nvn_device.display,draw,read,context);
    if(ok) {
        nvn_single_state.display=nvn_device.display;nvn_single_state.context=context;
        nvn_single_state.draw=draw;nvn_single_state.read=read;
        if(nvn_single_state.transfer_enabled&&!atomic_load_explicit(&nvn_single_state.transferred,memory_order_acquire))
            nvn_single_state.bootstrap_detached=0;
    }
    return ok;
}
static EGLBoolean nvn_single_detach(void) {
    if(!nvn_single_enabled())return eglMakeCurrent(nvn_device.display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    nvn_single_require_owner();
    if(nvn_single_state.transfer_enabled&&!atomic_load_explicit(&nvn_single_state.transferred,memory_order_acquire)) {
        EGLBoolean ok=eglMakeCurrent(nvn_device.display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        if(ok) {
            if(eglGetCurrentContext()!=EGL_NO_CONTEXT)stop("NVN_LC2_SINGLE_TRANSFER_DETACH",nvn_caller,0,21);
            nvn_single_state.display=EGL_NO_DISPLAY;nvn_single_state.context=EGL_NO_CONTEXT;
            nvn_single_state.draw=nvn_single_state.read=EGL_NO_SURFACE;
            nvn_single_state.bootstrap_detached=1;nvn_single_state.bootstrap_detaches++;
        }
        return ok;
    }
    nvn_single_state.detach_skips++;return EGL_TRUE;
}
static void nvn_single_window_mode(unsigned game_pbuffer) {
    if(!nvn_single_enabled())return;
    nvn_single_require_owner();
    if(game_pbuffer)stop("NVN_SINGLE_CONTEXT_PBUFFER",nvn_caller,game_pbuffer,24);
}
static void nvn_single_report(unsigned frame) {
    if(!nvn_single_enabled())return;
    nvn_single_require_owner();
    printf("NVN_SINGLE_CONTEXT_PROFILE frame=%u owner-tid=%ld guest-thread=%u make-current-calls=%"PRIu64
           " bind-skips=%"PRIu64" detach-skips=%"PRIu64" owner-checks=%"PRIu64" restored-brackets=%"PRIu64
           " scope=cumulative context-count=1 completion-proof=separate\n",frame,
           (long)(atomic_load_explicit(&nvn_single_state.transferred,memory_order_acquire)?nvn_single_state.render_tid:getpid()),guest_thread_index,
           nvn_single_state.make_current_calls,nvn_single_state.bind_skips,nvn_single_state.detach_skips,
           nvn_single_state.owner_checks,nvn_single_state.brackets);
}
