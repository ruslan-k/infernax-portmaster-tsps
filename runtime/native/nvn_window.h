/* VI native window -> system fbdev EGL, preserving the live GLES context.
 * The native-window ABI was reused from the explicitly authorized Hades
 * study (exact source pin: evidence/nvn-bridge-reuse.json).
 */
#include <linux/fb.h>
#include <sys/ioctl.h>
struct nvn_window_builder {uint64_t device;uint32_t count,pad;uint64_t textures,native;int32_t interval_minus_one;uint32_t pad2;};
static struct {
    uint64_t object,native;struct nvn_texture *textures[3];unsigned count,next,acquired[3],live;
    int32_t interval,crop[4];struct {unsigned short width,height;} fb;
    EGLSurface surface,pbuffer;unsigned game_pbuffer;
} nvn_window;
static void nvn_window_require(uint64_t object) {
    if(!nvn_window.live||nvn_window.object!=object)stop("NVN_WINDOW_INVALID",nvn_caller,object,24);
}
static unsigned nvn_window_crop_dispatch(unsigned id,struct nvn_frame *f,uint64_t *result) {
    if(id!=NI_nvnWindowSetCrop && id!=NI_nvnWindowGetCrop)return 0;
    nvn_window_require(f->args[0]);*result=0;
    if(id==NI_nvnWindowGetCrop) {
        /* SDK 2eadfc writes x,y,width,height to one contiguous int32[4]. */
        memcpy(nvn_pointer(f->args[1],sizeof(nvn_window.crop),PROT_WRITE),nvn_window.crop,sizeof(nvn_window.crop));
        return 1;
    }
    int32_t crop[4];for(unsigned i=0;i<4;i++)crop[i]=(int32_t)f->args[i+1];
    int32_t width=nvn_window.textures[0]->b.width,height=nvn_window.textures[0]->b.height;
    /* SDK 2ead8c resets the entire rectangle when either extent is zero. */
    if(!crop[2] || !crop[3]){crop[0]=crop[1]=0;crop[2]=width;crop[3]=height;}
    if(crop[0]<0 || crop[1]<0 || crop[2]<1 || crop[3]<1 || crop[0]>width || crop[1]>height ||
       crop[2]>width-crop[0] || crop[3]>height-crop[1])
        stop("NVN_WINDOW_CROP_RANGE",nvn_caller,((uint64_t)(uint32_t)crop[2]<<32)|(uint32_t)crop[3],24);
    memcpy(nvn_window.crop,crop,sizeof(crop));return 1;
}
static unsigned nvn_window_dispatch(unsigned id,struct nvn_frame *f,uint64_t *result) {
    if(nvn_window_crop_dispatch(id,f,result))return 1;
    uint64_t *a=f->args;*result=0;
    switch(id) {
    case NI_nvnWindowBuilderSetDevice:case NI_nvnWindowBuilderSetDefaults:case NI_nvnWindowBuilderSetNativeWindow:
    case NI_nvnWindowBuilderSetTextures:case NI_nvnWindowBuilderSetPresentInterval:
    /* NVN 18.x active-texture subset (Unity asks for 3 of 3 at main+0x7b9600, MEDIDO): pure builder
     * bookkeeping, so the original SDK setter/getter writes the field and the window contract below
     * still validates the resulting count. */
    case NI_nvnWindowBuilderSetNumActiveTextures:case NI_nvnWindowBuilderGetNumActiveTextures:
    case NI_nvnWindowBuilderGetNumTextures:
    case NI_nvnWindowBuilderGetNativeWindow:case NI_nvnWindowBuilderGetPresentInterval:
        nvn_pointer(a[0],8+sizeof(struct nvn_window_builder),PROT_READ|PROT_WRITE);*result=nvn_original_helper(id,f);return 1;
    default:break;
    }
    if(id==NI_nvnWindowInitialize) {
        nvn_single_require_owner();
        /* SDK 6.5 builder has an opaque eight-byte prefix. */
        /* Animal Well SDK 17.5.3 window builder (measured): +8 device, +0x10 count, +0x18 native window,
         * +0x20 present interval minus one, +0x28 textures pointer. */
        struct nvn_window_builder b;const uint8_t *raw=nvn_pointer(a[1],0x30,PROT_READ);
        memset(&b,0,sizeof(b));memcpy(&b.device,raw+8,8);memcpy(&b.count,raw+0x10,4);memcpy(&b.native,raw+0x18,8);
        memcpy(&b.interval_minus_one,raw+0x20,4);memcpy(&b.textures,raw+0x28,8);
        if(!nvn_device.live || b.device!=nvn_device.address || nvn_window.live || !layer.opened || !b.native || b.count<2 || b.count>3)
            stop("NVN_WINDOW_CONTRACT",nvn_caller,b.count,24);
        if(b.interval_minus_one<-1 || b.interval_minus_one>0)stop("NVN_WINDOW_INTERVAL_REQUIRED",nvn_caller,b.interval_minus_one,20);
        const char *offscreen=getenv("COTM_GAME_PBUFFER");
        if(offscreen&&strcmp(offscreen,"0")&&strcmp(offscreen,"1"))stop("NVN_PBUFFER_OPTION",nvn_caller,0,24);
        unsigned game_pbuffer=offscreen&&!strcmp(offscreen,"1");nvn_single_window_mode(game_pbuffer);
        nvn_pointer(a[0],8,PROT_READ|PROT_WRITE);nvn_pointer(b.native,16,PROT_READ);
        const uint64_t *textures=nvn_pointer(b.textures,b.count*8,PROT_READ);
        for(unsigned i=0;i<b.count;i++) {
            struct nvn_texture *t=nvn_texture_find(textures[i]);
            nvn_normal_require_materialized(t);
            if(!(t->b.flags&1)||!t->name||t->b.levels!=1)stop("NVN_WINDOW_TEXTURE_INVALID",nvn_caller,textures[i],24);
            for(unsigned j=0;j<i;j++)if(nvn_window.textures[j]==t)stop("NVN_WINDOW_TEXTURE_REPEATED",nvn_caller,textures[i],24);
            if(i&&(t->b.width!=nvn_window.textures[0]->b.width||t->b.height!=nvn_window.textures[0]->b.height))stop("NVN_WINDOW_TEXTURE_DIMENSIONS",nvn_caller,textures[i],24);
            nvn_window.textures[i]=t;
        }
        struct fb_var_screeninfo info={0};
        if(nvn_platform_sdl_window) {
            int width=0,height=0;
            SDL_GL_GetDrawableSize(nvn_platform_sdl_window,&width,&height);
            info.xres=width;info.yres=height;info.bits_per_pixel=32;
        } else if(MEGAMAN_HOST_DIAGNOSTIC||t2_pc_bench()) {
            unsigned compact=nvn_output_640_enabled();
            info.xres=compact?640:1280;info.yres=compact?480:720;info.bits_per_pixel=32;
        } else {
            int fd=open("/dev/fb0",O_RDONLY|O_CLOEXEC);
            if(fd<0 || ioctl(fd,FBIOGET_VSCREENINFO,&info)<0)stop("NVN_FBDEV_QUERY",nvn_caller,errno,21);
            close(fd);
        }
        if(!info.xres || !info.yres || info.xres>4096 || info.yres>4096)stop("NVN_FBDEV_DIMENSIONS",nvn_caller,info.xres,24);
        nvn_window.fb.width=info.xres;nvn_window.fb.height=info.yres;
        nvn_low_configure(nvn_window.textures,b.count,info.xres,info.yres);
        EGLint types=0;eglGetConfigAttrib(nvn_device.display,nvn_device.config,EGL_SURFACE_TYPE,&types);
        if(nvn_platform_sdl_window) {
            nvn_window.surface=nvn_platform_sdl_surface;
        } else if(MEGAMAN_HOST_DIAGNOSTIC||t2_pc_bench()) {
            const EGLint attributes[]={EGL_WIDTH,(EGLint)info.xres,EGL_HEIGHT,(EGLint)info.yres,EGL_NONE};
            nvn_window.surface=eglCreatePbufferSurface(nvn_device.display,nvn_device.config,attributes);
        } else {
            if(!(types&EGL_WINDOW_BIT))stop("NVN_EGL_WINDOW_CONFIG",nvn_caller,types,21);
            nvn_window.surface=eglCreateWindowSurface(nvn_device.display,nvn_device.config,(EGLNativeWindowType)&nvn_window.fb,NULL);
        }
        if(nvn_window.surface==EGL_NO_SURFACE)stop("NVN_EGL_WINDOW_CREATE",nvn_caller,eglGetError(),21);
        nvn_window.game_pbuffer=game_pbuffer;
        nvn_window.pbuffer=nvn_device.surface;
        if(!nvn_window.game_pbuffer)nvn_device.surface=nvn_window.surface;
        nvn_diag_window_surface=nvn_window.surface;
        nvn_gl_acquire();EGLint width=0,height=0;
        /* Swap interval belongs to the window surface, even when the producer
         * context normally renders its FBOs while bound to the pbuffer. */
        if(nvn_window.game_pbuffer&&!nvn_single_make_current(nvn_window.surface,nvn_window.surface,nvn_device.context))
            stop("NVN_WINDOW_CONFIG_CONTEXT",nvn_caller,eglGetError(),21);
        eglQuerySurface(nvn_device.display,nvn_window.surface,EGL_WIDTH,&width);eglQuerySurface(nvn_device.display,nvn_window.surface,EGL_HEIGHT,&height);
        if(width!=(int)info.xres||height!=(int)info.yres||!glGetString(GL_RENDERER))stop("NVN_EGL_WINDOW_SIZE",nvn_caller,width,21);
        if(!eglSwapInterval(nvn_device.display,b.interval_minus_one+1))stop("NVN_EGL_WINDOW_INTERVAL",nvn_caller,eglGetError(),21);
#ifdef ANIMAL_WELL_SHADERS
        {   /* AW_WINDOW_HALF=1: the swapchain textures (final CRT composite + overlay, 1280x720) get 640x360 storage.
             * Viewport/scissor into them are halved (nvn_low_apply_rect), the present samples the whole texture and
             * scales 2x NEAREST. The CRT filter pass costs 26.8 ms at 1280x720 on the Mali-450 (aw146). */
            const char *o=getenv("AW_WINDOW_HALF");
            if(o&&!strcmp(o,"1"))for(unsigned i=0;i<b.count;i++) {
                struct nvn_texture *t=nvn_window.textures[i];
                if(t->b.width!=1280||t->b.height!=720||t->b.levels!=1||t->host_scaled||t->depth_buffer)stop("AW_WINDOW_HALF_TEXTURE",nvn_caller,t->id,20);
                glBindTexture(GL_TEXTURE_2D,t->name);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,640,360,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
                glBindTexture(GL_TEXTURE_2D,0);nvn_framebuffer_forget(t->id);t->host_half=1;
                printf("AW_WINDOW_HALF texture=%"PRIu64" logical=1280x720 storage=640x360 present=2x-nearest\n",t->id);
            }
        }
#endif
        nvn_gl_release();
        for(unsigned i=0;i<b.count;i++)nvn_window.textures[i]->references++;
        nvn_window.object=a[0];nvn_window.native=b.native;nvn_window.count=b.count;nvn_window.interval=b.interval_minus_one+1;
        nvn_window.crop[0]=nvn_window.crop[1]=0;
        nvn_window.crop[2]=nvn_window.textures[0]->b.width;nvn_window.crop[3]=nvn_window.textures[0]->b.height;nvn_window.live=1;
        /* SDK 6.5 WindowInitialize calls its platform connection at +0x408098.
         * Zero has created/opened the VI layer but has not connected a producer
         * beforehand. This driver owns that connection after real EGL success. */
        if(!bq_state.api){bq_state.api=1;bq_state.app_controlled=1;bq_state.connections++;
            printf("ZERO_WINDOW_PRODUCER_CONNECTED owner=GLES-driver api=EGL after-surface-initialization\n");}
        printf("NVN_WINDOW_REAL framebuffer=%dx%d bpp=%u textures=%u context-preserved=1 no-draw-no-present\n",width,height,info.bits_per_pixel,b.count);
        printf("NVN_RENDER_SURFACE producer=%s presentation=%s shared-textures=1\n",nvn_window.game_pbuffer?"pbuffer":"window",
               MEGAMAN_HOST_DIAGNOSTIC?"host-software-pbuffer":"physical-window");
        *result=1;return 1;
    }
    if(id==NI_nvnQueueAcquireTexture || id==NI_nvnWindowAcquireTexture) {
        uint64_t output;struct nvn_sync *sync=NULL;
        if(id==NI_nvnQueueAcquireTexture){nvn_queue_find(a[0]);nvn_window_require(a[1]);output=a[2];}
        else{nvn_window_require(a[0]);sync=nvn_sync_find(a[1]);output=a[2];}
        int32_t slot=-1;
        for(unsigned i=0;i<nvn_window.count;i++){unsigned candidate=(nvn_window.next+i)%nvn_window.count;if(!nvn_window.acquired[candidate]){slot=candidate;break;}}
        if(slot<0)stop("NVN_WINDOW_NO_FREE_TEXTURE",nvn_caller,nvn_window.count,20);
        pthread_mutex_lock(&nvn_gl_completion.mutex);
        nvn_single_require_owner();
        if(!nvn_single_enabled())nvn_egl_present_reader_wait(); /* a shared-context reader must finish before reuse */
        if(sync){nvn_async_sync_reset_locked(sync);sync->generation++;sync->completed=sync->generation;sync->signaled=1;}
        pthread_mutex_unlock(&nvn_gl_completion.mutex);
        memcpy(nvn_pointer(output,4,PROT_WRITE),&slot,4);nvn_window.acquired[slot]=1;nvn_window.next=(slot+1)%nvn_window.count;
        /* Sync reports available texture ownership, not GPU execution. */
        return 1;
    }
    switch(id) {
    case NI_nvnWindowSetDebugLabel:case NI_nvnWindowSetPresentInterval:case NI_nvnWindowGetPresentInterval:
    case NI_nvnWindowGetNativeWindow:break;
    default:return 0;
    }
    nvn_window_require(a[0]);
    if(id==NI_nvnWindowSetDebugLabel)nvn_debug_label(a[1]);
    if(id==NI_nvnWindowGetPresentInterval)*result=nvn_window.interval;
    if(id==NI_nvnWindowGetNativeWindow)*result=nvn_window.native;
    if(id==NI_nvnWindowSetPresentInterval) {
        if(a[1]>1)stop("NVN_WINDOW_INTERVAL_REQUIRED",nvn_caller,a[1],20);
        nvn_gl_acquire();
        if(nvn_window.game_pbuffer&&!nvn_single_make_current(nvn_window.surface,nvn_window.surface,nvn_device.context))
            stop("NVN_WINDOW_CONFIG_CONTEXT",nvn_caller,eglGetError(),21);
        if(!eglSwapInterval(nvn_device.display,a[1]))stop("NVN_EGL_WINDOW_INTERVAL",nvn_caller,eglGetError(),21);
        nvn_gl_release();nvn_window.interval=a[1];
    }
    return 1;
}
