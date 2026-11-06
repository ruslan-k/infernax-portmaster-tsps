/* System SDL owns the DRM/GBM window and page flips on handheld firmware.
 * The existing fbdev path remains available on firmware without DRM. */
#include <SDL2/SDL.h>
#include "nxgl_provider_discovery_adapter.h"
static SDL_Window *nvn_platform_sdl_window;
static EGLSurface nvn_platform_sdl_surface;
static SDL_GLContext nvn_platform_sdl_context;
static int nvn_platform_sdl_video_initialized;
static char **nvn_platform_argv;
struct nvn_platform_open { EGLint *major, *minor; };

static void nvn_platform_sdl_teardown(void) {
    if(nvn_platform_sdl_context)SDL_GL_DeleteContext(nvn_platform_sdl_context);
    nvn_platform_sdl_context=NULL;
    if(nvn_platform_sdl_window)SDL_DestroyWindow(nvn_platform_sdl_window);
    nvn_platform_sdl_window=NULL;
    nvn_platform_sdl_surface=EGL_NO_SURFACE;
    nvn_device.display=EGL_NO_DISPLAY;nvn_device.context=EGL_NO_CONTEXT;
    if(nvn_platform_sdl_video_initialized)SDL_QuitSubSystem(SDL_INIT_VIDEO);
    nvn_platform_sdl_video_initialized=0;
}

static int nvn_platform_sdl_attempt(void *userdata,nxgl_provider_attempt_v1 *attempt) {
    struct nvn_platform_open *open=userdata;
    attempt->failed_stage=NXGL_OPEN_STAGE_V2_VIDEO_START;
    attempt->final_reason=NXGL_OPEN_REASON_V2_VIDEO_UNAVAILABLE;

    if(SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        fprintf(stderr,"NVN_SDL_VIDEO_INIT error=%s\n",SDL_GetError());
        goto failed;
    }
    nvn_platform_sdl_video_initialized=1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE,8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE,8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,0);
    SDL_DisplayMode mode;
    attempt->failed_stage=NXGL_OPEN_STAGE_V2_RESOLUTION;
    attempt->final_reason=NXGL_OPEN_REASON_V2_RESOLUTION_UNAVAILABLE;
    if(SDL_GetCurrentDisplayMode(0,&mode) || mode.w<=0 || mode.h<=0)goto failed;
    attempt->failed_stage=NXGL_OPEN_STAGE_V2_WINDOW_CREATE;
    attempt->final_reason=NXGL_OPEN_REASON_V2_WINDOW_FAILED;
    nvn_platform_sdl_window=SDL_CreateWindow("Terminator 2D: NO FATE",
        SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,mode.w,mode.h,
        SDL_WINDOW_OPENGL|SDL_WINDOW_FULLSCREEN_DESKTOP|SDL_WINDOW_SHOWN);
    if(!nvn_platform_sdl_window) {
        fprintf(stderr,"NVN_SDL_WINDOW_CREATE error=%s\n",SDL_GetError());
        goto failed;
    }
    attempt->window_opened=1;
    attempt->failed_stage=NXGL_OPEN_STAGE_V2_CONTEXT_CREATE;
    attempt->final_reason=NXGL_OPEN_REASON_V2_CONTEXT_FAILED;
    nvn_platform_sdl_context=SDL_GL_CreateContext(nvn_platform_sdl_window);
    if(!nvn_platform_sdl_context) {
        fprintf(stderr,"NVN_SDL_CONTEXT_CREATE error=%s\n",SDL_GetError());
        goto failed;
    }
    attempt->failed_stage=NXGL_OPEN_STAGE_V2_MAKE_CURRENT;
    attempt->final_reason=NXGL_OPEN_REASON_V2_CURRENT_FAILED;
    if(SDL_GL_MakeCurrent(nvn_platform_sdl_window,nvn_platform_sdl_context))goto failed;
    nvn_device.display=eglGetCurrentDisplay();
    nvn_device.context=eglGetCurrentContext();
    nvn_platform_sdl_surface=eglGetCurrentSurface(EGL_DRAW);
    EGLint config_id=0,count=0;
    if(nvn_device.display==EGL_NO_DISPLAY || nvn_device.context==EGL_NO_CONTEXT ||
       nvn_platform_sdl_surface==EGL_NO_SURFACE ||
       !eglInitialize(nvn_device.display,open->major,open->minor) ||
       !eglQueryContext(nvn_device.display,nvn_device.context,EGL_CONFIG_ID,&config_id))
        goto failed;
    attempt->context_current=1;
    attempt->failed_stage=NXGL_OPEN_STAGE_V2_CONFIG_QUERY;
    attempt->final_reason=NXGL_OPEN_REASON_V2_EGL_CONFIG_UNAVAILABLE;
    const EGLint attrs[]={EGL_CONFIG_ID,config_id,EGL_NONE};
    if(!eglChooseConfig(nvn_device.display,attrs,&nvn_device.config,1,&count) || count!=1)
        goto failed;
    int drawable_w=0,drawable_h=0;
    SDL_GL_GetDrawableSize(nvn_platform_sdl_window,&drawable_w,&drawable_h);
    attempt->failed_stage=NXGL_OPEN_STAGE_V2_DRAWABLE_QUERY;
    attempt->final_reason=NXGL_OPEN_REASON_V2_DRAWABLE_INVALID;
    if(drawable_w<=0 || drawable_h<=0)goto failed;
    attempt->drawable_positive=1;
    attempt->renderer=(const char *)glGetString(GL_RENDERER);
    attempt->video_backend=SDL_GetCurrentVideoDriver();
    attempt->failed_stage=NXGL_OPEN_STAGE_V2_NONE;
    attempt->final_reason=NXGL_OPEN_REASON_V2_SELECTED;
    SDL_GL_SetSwapInterval(0);
    SDL_ShowCursor(SDL_DISABLE);
    printf("NVN_SYSTEM_WINDOW backend=SDL driver=%s width=%d height=%d EGL-config=%d\n",
        SDL_GetCurrentVideoDriver(),mode.w,mode.h,config_id);
    return 1;
failed:
    fprintf(stderr,"NVN_SDL_OPEN_FAILED stage=%d reason=%d error=%s\n",
        (int)attempt->failed_stage,(int)attempt->final_reason,SDL_GetError());
    nvn_platform_sdl_teardown();
    return 0;
}

static unsigned nvn_platform_sdl_initialize(EGLint *major,EGLint *minor) {
    if(MEGAMAN_HOST_DIAGNOSTIC || t2_pc_bench())return 0;
    const char *backend=getenv("T2_DISPLAY_BACKEND");
    if(backend && !strcmp(backend,"fbdev"))return 0;
    if(!(backend && !strcmp(backend,"sdl")) && access("/dev/dri/card0",F_OK))return 0;
    struct nvn_platform_open open={major,minor};
    nxgl_provider_open_options_v1 options;
    nxgl_provider_open_receipt_v1 receipt;
    nxgl_provider_open_options_v1_init(&options);
    nxgl_provider_open_receipt_v1_init(&receipt);
    options.attempt=nvn_platform_sdl_attempt;
    options.teardown=nvn_platform_sdl_teardown;
    options.userdata=&open;
    options.argv=nvn_platform_argv;
    static const char *const symbols[]={"glGetString","glGetError","glCreateShader",
        "glShaderSource","glCompileShader","glCreateProgram","glLinkProgram",
        "glBindTexture","glTexImage2D","glBindFramebuffer","glReadPixels","glDrawElements"};
    options.required_gles_symbols=symbols;
    options.required_gles_symbol_count=sizeof symbols/sizeof symbols[0];
    int live=nxgl_provider_open_measured_v1(&options,&receipt);
    fprintf(stderr,"NVN_PROVIDER_MEASURED %s\n",receipt.text);
    if(!live)stop("NVN_SDL_PROVIDER_OPEN",nvn_caller,receipt.outcome,21);
    return 1;
}

static EGLBoolean nvn_platform_swap(EGLDisplay display,EGLSurface surface) {
    if(!nvn_platform_sdl_window || surface!=nvn_platform_sdl_surface)
        return eglSwapBuffers(display,surface);
    /* Raw EGL context switches do not update SDL's per-thread current-window
     * record. Synchronize that record before asking SDL to page-flip GBM. */
    SDL_ClearError();
    if(SDL_GL_MakeCurrent(nvn_platform_sdl_window,(SDL_GLContext)eglGetCurrentContext())) {
        fprintf(stderr,"NVN_SDL_MAKE_CURRENT error=%s\n",SDL_GetError());
        return EGL_FALSE;
    }
    SDL_GL_SwapWindow(nvn_platform_sdl_window);
    if(*SDL_GetError()) {
        fprintf(stderr,"NVN_SDL_SWAP error=%s\n",SDL_GetError());
        return EGL_FALSE;
    }
    return EGL_TRUE;
}
