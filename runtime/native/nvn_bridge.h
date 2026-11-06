/* Port-local NVN API boundary. Original pure builder helpers are called in
 * normal guest context. Hardware operations require a real host backend.
 * A resolved entry is never evidence that its operation is implemented.
 */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include "zero_gpu_cost.h"
#include "megaman_host_diagnostic.h"
struct nvn_frame {uint64_t args[8],indirect,platform,fp,lr,sp,reserved;__uint128_t vectors[8];uint64_t fpcr,fpsr;};
_Static_assert(sizeof(struct nvn_frame)==256 && offsetof(struct nvn_frame,vectors)==112,"NVN assembly frame");
#define NVN_ENTRY(name,id) extern void cotm_##name(void);
#include "nvn_entries.inc"
#undef NVN_ENTRY
enum nvn_id {
#define NVN_ENTRY(name,id) NI_##name=id,
#include "nvn_entries.inc"
#undef NVN_ENTRY
    NI_COUNT
};
static const struct {const char *name;void (*entry)(void);} nvn_functions[NI_COUNT]={
#define NVN_ENTRY(name,id) [id]={#name,cotm_##name},
#include "nvn_entries.inc"
#undef NVN_ENTRY
};
static uint64_t nvn_original[NI_COUNT],nvn_calls[NI_COUNT];
static _Thread_local unsigned nvn_bridge_depth;
static _Thread_local uint64_t nvn_caller;
extern uint64_t cotm_nvn_original_lookup(uint64_t,const char *,uint64_t);
extern uint64_t cotm_nvn_call_original(uint64_t,struct nvn_frame *);
static struct {uint64_t address;uint32_t flags;EGLDisplay display;EGLConfig config;EGLContext context;EGLSurface surface;
    GLint texture_limit,attributes,texture_units,uniform_block_size,uniform_buffer_bindings,vertex_uniform_blocks,fragment_uniform_blocks,combined_uniform_blocks;unsigned live,window_origin,depth_mode,intermediate_cache_hint;} nvn_device;
static struct {int configured;unsigned enabled,started;pthread_t thread;pthread_mutex_t mutex;pthread_cond_t post,finished;
    struct nvn_gl_job *pending;uint64_t jobs;} nvn_gl_worker={.mutex=PTHREAD_MUTEX_INITIALIZER,.post=PTHREAD_COND_INITIALIZER,.finished=PTHREAD_COND_INITIALIZER};
static unsigned nvn_gl_worker_enabled(void) {
    if(!nvn_gl_worker.configured){const char *o=getenv("COTM_GL_THREAD");nvn_gl_worker.enabled=o&&!strcmp(o,"1");nvn_gl_worker.configured=1;}
    return nvn_gl_worker.enabled;
}
static unsigned nvn_gl_worker_is_self(void){return nvn_gl_worker.started && pthread_equal(pthread_self(),nvn_gl_worker.thread);}
struct nvn_snapshot;
static unsigned nvn_present_from_job;
static void nvn_redirect_advance_draw(void);
static uint64_t nvn_gl_frames_posted(void),nvn_gl_frames_dropped(void);
static unsigned nvn_async_frame_enabled(void);
static unsigned nvn_async_frame_local(unsigned id,struct nvn_frame *f,uint64_t *result);
static void nvn_gl_post_present(uint64_t queue,uint64_t window,uint64_t index,uint64_t caller);
static void nvn_gl_post_frame(struct nvn_snapshot *s,uint64_t caller);
static void nvn_gl_drain(void);
static unsigned nvn_gl_frame_pending(void);
static void nvn_gl_sync_report(void);
#include "nvn_single_context.h"
#include "nvn_pointer.h"
static unsigned nvn_find(const char *name) {
    for(unsigned i=1;i<NI_COUNT;i++)if(!strcmp(name,nvn_functions[i].name))return i;
    return 0;
}
static uint64_t nvn_lookup(struct nvn_frame *f) {
    char name[129];uint64_t a=f->args[1];unsigned n=0;
    for(;n<128;n++){name[n]=*(char *)nvn_pointer(a+n,1,PROT_READ);if(!name[n])break;}
    if(n==128)stop("NVN_API_NAME_TOO_LONG",nvn_caller,a,24);
    unsigned id=nvn_find(name);
    if(!id) {
        /* NVN 17.x names outside the runtime table resolve to NULL, as the guest loader tolerates for unused entries;
         * a later call through such a pointer faults at a null pc and is reported by the guest-fault path. */
        struct line log={0};text(&log,"NVN_API_NAME_UNKNOWN_NULL name=");text(&log,name);emit(&log);return 0;
    }
    if(!nvn_original[id]) {
        /* Null device selects the original non-debug helper implementation.
         * A host-backed device must never be passed to Tegra's device internals. */
        nvn_original[id]=cotm_nvn_original_lookup(0,(void *)(uintptr_t)a,f->platform);
        if(!nvn_original[id])stop("NVN_ORIGINAL_API_MISSING",nvn_caller,id,24);
        nvn_pointer(nvn_original[id],4,PROT_EXEC);
    }
    return (uintptr_t)nvn_functions[id].entry;
}
static uint64_t nvn_original_helper(unsigned id,struct nvn_frame *f) {
    if(!nvn_original[id])stop("NVN_HELPER_NOT_RESOLVED",nvn_caller,id,24);
    return cotm_nvn_call_original(nvn_original[id],f);
}
/* TERMINATOR: NX_PC_BENCH=1 (tools/pc/run_qemu.sh) = headless PC bench under qemu + Mesa surfaceless:
 * no fbdev, no window configs -> pbuffer surfaces, like the compile-time diagnostic host. Never set on device. */
static int t2_pc_bench(void){static int v=-1;if(v<0){const char *e=getenv("NX_PC_BENCH");v=e&&*e&&*e!='0';}return v;}
#include "nvn_platform_sdl.h"
static uint64_t nvn_device_initialize(struct nvn_frame *f) {
    nvn_single_require_owner();
    if(nvn_device.live)stop("NVN_SECOND_DEVICE_UNSUPPORTED",nvn_caller,f->args[0],20);
    nvn_pointer(f->args[0],0x3000,PROT_READ|PROT_WRITE);
    uint32_t flags=read32(nvn_pointer(f->args[1],64,PROT_READ));
    if(flags&~8U)stop("NVN_DEVICE_FLAGS_UNSUPPORTED",nvn_caller,flags,20);
    EGLint major=0,minor=0,count=0;
    unsigned system_window=nvn_platform_sdl_initialize(&major,&minor);
    if(!system_window) {
    nvn_device.display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if(!eglInitialize(nvn_device.display,&major,&minor) || !eglBindAPI(EGL_OPENGL_ES_API))stop("NVN_EGL_INITIALIZE",nvn_caller,eglGetError(),21);
    const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT|((MEGAMAN_HOST_DIAGNOSTIC||t2_pc_bench())?0:EGL_WINDOW_BIT),EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    if(!eglChooseConfig(nvn_device.display,attrs,&nvn_device.config,1,&count) || count!=1)stop("NVN_EGL_CONFIG",nvn_caller,eglGetError(),21);
    }
    const EGLint pb[]={EGL_WIDTH,8,EGL_HEIGHT,8,EGL_NONE},ctx[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLint surface_types=0;
    if(system_window && !eglGetConfigAttrib(nvn_device.display,nvn_device.config,EGL_SURFACE_TYPE,&surface_types))
        stop("NVN_SDL_EGL_SURFACE_TYPES",nvn_caller,eglGetError(),21);
    /* GBM window configs need not support pbuffers. The already-current SDL
     * window is a valid bootstrap surface; fbdev keeps its original pbuffer. */
    nvn_device.surface=system_window && !(surface_types&EGL_PBUFFER_BIT)
        ?nvn_platform_sdl_surface:eglCreatePbufferSurface(nvn_device.display,nvn_device.config,pb);
    if(!system_window)nvn_device.context=eglCreateContext(nvn_device.display,nvn_device.config,EGL_NO_CONTEXT,ctx);
    if(nvn_device.surface==EGL_NO_SURFACE || nvn_device.context==EGL_NO_CONTEXT ||
       !nvn_single_make_current(nvn_device.surface,nvn_device.surface,nvn_device.context))stop("NVN_EGL_CONTEXT",nvn_caller,eglGetError(),21);
    const char *renderer=(const char *)glGetString(GL_RENDERER),*version=(const char *)glGetString(GL_VERSION);
    if(!renderer || !version)stop("NVN_GL_CONTEXT_DEAD",nvn_caller,glGetError(),21);
    if(MEGAMAN_HOST_DIAGNOSTIC && !strstr(renderer,"softpipe") && !strstr(renderer,"llvmpipe") && !strstr(renderer,"virgl (LLVMPIPE ("))
        stop("MEGAMAN_HOST_SOFTWARE_RENDERER_REQUIRED",nvn_caller,0,24);
    if(MEGAMAN_HOST_DIAGNOSTIC)printf("MEGAMAN_HOST_DIAGNOSTIC_SOFTWARE_ONLY physical-approval=forbidden\n");
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&nvn_device.texture_limit);glGetIntegerv(GL_MAX_VERTEX_ATTRIBS,&nvn_device.attributes);
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&nvn_device.texture_units);
    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE,&nvn_device.uniform_block_size);
    glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS,&nvn_device.uniform_buffer_bindings);
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_BLOCKS,&nvn_device.vertex_uniform_blocks);
    glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_BLOCKS,&nvn_device.fragment_uniform_blocks);
    glGetIntegerv(GL_MAX_COMBINED_UNIFORM_BLOCKS,&nvn_device.combined_uniform_blocks);
    printf("NVN_GLES2_DEVICE_REAL renderer=%s version=%s EGL=%d.%d texture-limit=%d vertex-attributes=%d fragment-samplers=%d flags=0x%x surface=pbuffer-8x8 no-draw-no-present\n",
        renderer,version,major,minor,nvn_device.texture_limit,nvn_device.attributes,nvn_device.texture_units,flags);
    printf("NVN_GLES_UBO_CAPS max-block-size=%d max-bindings=%d vertex-blocks=%d fragment-blocks=%d combined-blocks=%d\n",
        nvn_device.uniform_block_size,nvn_device.uniform_buffer_bindings,nvn_device.vertex_uniform_blocks,
        nvn_device.fragment_uniform_blocks,nvn_device.combined_uniform_blocks);
    if(glGetError()!=GL_NO_ERROR || !nvn_single_detach())stop("NVN_EGL_DETACH",nvn_caller,eglGetError(),21);
    nvn_device.address=f->args[0];nvn_device.flags=flags;nvn_device.live=1;return 1;
}
#include "nvn_memory.h"
#include "nvn_descriptors.h"
/* Diagnostic only: the queue path makes the device surface current, so a later failure needs the window
 * surface next to it to tell "destroyed" from "wrong surface". */
static EGLSurface nvn_diag_window_surface;
#include "nvn_queue.h"
static uint64_t nvn_program_record_id(uint64_t object);
struct nvn_texture;static struct nvn_texture *nvn_texture_find(uint64_t object);
static void t2_page_record_copy(struct nvn_texture *t,uint64_t source,const uint8_t *payload,unsigned has_view); /* ROOT */
static void *nvn_gpu_pointer(uint64_t address,uint64_t size,unsigned permission); /* ROOT */
#include "nvn_commands.h"
#include "nvn_sync.h"
#include "nvn_textures.h"
/* Env flags exported as "0" by the launcher are off; only unset/1/true count as on. */
static int nvn_flag_on(const char *name){const char *v=getenv(name);return v&&*v&&strcmp(v,"0")!=0&&strcmp(v,"false")!=0;}
/* Diagnostic only: highest channel value in a 16x16 block at the texture centre. Tells "the guest wrote
 * visible pixels" apart from "the quad sampled nothing" without a full readback. */
static unsigned nvn_texture_probe_max(struct nvn_texture *t,unsigned *complete_out,unsigned char *pixel_out) {
    GLint previous_fbo;GLuint fbo=0;unsigned maximum=0;EGLint complete=0;
    if(!t||!t->name){if(complete_out)*complete_out=0;return 0;}
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&previous_fbo);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFinish();
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t->name,0);
    GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);complete=(EGLint)status;
    if(status==GL_FRAMEBUFFER_COMPLETE) {
        unsigned char px[16*16*4];GLint pack;glGetIntegerv(GL_PACK_ALIGNMENT,&pack);glPixelStorei(GL_PACK_ALIGNMENT,1);
        GLint x=(GLint)((t->b.width>16?(t->b.width/2)&~15u:0)),y=(GLint)((t->b.height>16?(t->b.height/2)&~15u:0));
        GLint w=(GLint)(t->b.width>16?16:t->b.width),h=(GLint)(t->b.height>16?16:t->b.height);
        glReadPixels(x,y,w,h,GL_RGBA,GL_UNSIGNED_BYTE,px);
        for(GLint i=0;i<w*h;i++)for(unsigned c=0;c<3;c++)if(px[i*4+(GLint)c]>maximum)maximum=px[i*4+(GLint)c];
        if(pixel_out)for(unsigned c=0;c<4;c++)pixel_out[c]=px[c];
        {static int full=-1;if(full<0){const char*f=getenv("NX_PROBE_FULL");full=f&&*f=='1';}
         if(full&&t->b.width<=2048&&t->b.height<=2048){size_t n=(size_t)t->b.width*t->b.height*4;unsigned char*b=(unsigned char*)malloc(n);
           if(b){glReadPixels(0,0,(GLint)t->b.width,(GLint)t->b.height,GL_RGBA,GL_UNSIGNED_BYTE,b);unsigned hit=0;
             for(size_t i=0;i<n;i+=4){unsigned m=b[i]>b[i+1]?b[i]:b[i+1];if(b[i+2]>m)m=b[i+2];if(m>maximum)maximum=m;if(m>8)hit++;}
             printf("NVN_PROBE_FULL obj=0x%"PRIx64" %ux%u max=%u nonblack=%u\n",t->object,(unsigned)t->b.width,(unsigned)t->b.height,maximum,hit);
             {static int dumped;const char *pd=getenv("NX_PROBE_DUMP");if(pd&&*pd=='1'&&dumped<10){char path[192];
               snprintf(path,sizeof path,"/mnt/SDCARD/Roms/PORTS/infernax/probe-%016"PRIx64".ppm",t->object);
               FILE *fp=fopen(path,"wb");if(fp){fprintf(fp,"P6\n%u %u\n255\n",(unsigned)t->b.width,(unsigned)t->b.height);
                 for(int yy=(int)t->b.height-1;yy>=0;yy--)for(unsigned xx=0;xx<t->b.width;xx++)fwrite(b+((size_t)yy*t->b.width+xx)*4,1,3,fp);
                 fclose(fp);printf("NVN_PROBE_DUMP obj=0x%"PRIx64" wrote=1\n",t->object);dumped++;}}}
             free(b);}}}
        glPixelStorei(GL_PACK_ALIGNMENT,pack);
    }
    glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)previous_fbo);glDeleteFramebuffers(1,&fbo);
    if(complete_out)*complete_out=complete;
    return maximum;
}
#include "nvn_samplers.h"
#include "nvn_window.h"
#include "momo_window_clear.h"
#include "nvn_shaders.h"
#include "nvn_replay.h"
#include "momo_stencil_bench.h"
#include "nvn_present.h"
static uint64_t nvn_device_initialize(struct nvn_frame *f);
#include "nvn_async_frame.h"
static unsigned nvn_gl_dispatch_body(unsigned id,struct nvn_frame *f,uint64_t *result) {
    if(nvn_queue_dispatch(id,f,result))return 1;
    if(nvn_sync_dispatch(id,f,result))return 1;
    if(nvn_texture_dispatch(id,f,result))return 1;
    if(nvn_window_dispatch(id,f,result))return 1;
    if(nvn_shader_dispatch(id,f,result))return 1;
    if(nvn_replay_dispatch(id,f,result))return 1;
    if(nvn_present_dispatch(id,f,result))return 1;
    if(id==NI_nvnDeviceInitialize){*result=nvn_device_initialize(f);return 1;}
    return 0;
}
#include "nvn_gl_worker.h"
/* MOMO_DEVICE_INFO="0x5e=1,0x1b=0": override device-info answers for one run, so a capability can be
 * bisected against the game's own behaviour without rebuilding. Diagnostic; the default answers stay. */
static struct {unsigned which;int32_t value;} momo_device_info[16];
static unsigned momo_device_info_count,momo_device_info_configured;
static int momo_device_info_override(unsigned which,int32_t *value) {
    if(!momo_device_info_configured) {
        momo_device_info_configured=1;
        const char *option=getenv("MOMO_DEVICE_INFO");
        while(option && *option && momo_device_info_count<16) {
            char *rest=NULL;
            unsigned long key=strtoul(option,&rest,0);
            if(!rest || *rest!='=')break;
            long answer=strtol(rest+1,&rest,0);
            momo_device_info[momo_device_info_count++]=(typeof(momo_device_info[0])){(unsigned)key,(int32_t)answer};
            printf("MOMO_DEVICE_INFO_OVERRIDE which=0x%lx value=%ld\n",key,answer);
            if(*rest!=',')break;
            option=rest+1;
        }
    }
    for(unsigned i=0;i<momo_device_info_count;i++)
        if(momo_device_info[i].which==which){*value=momo_device_info[i].value;return 1;}
    return 0;
}
static void nvn_device_integer(struct nvn_frame *f) {
    /* SDK 4.4.1 version queries 0/1 are independent of a device. Original
     * 2e0498 dispatches them without dereferencing x19 (including NULL). */
    if(f->args[1]<=1 && (!f->args[0] || (nvn_device.live&&f->args[0]==nvn_device.address))) {
        nvn_pointer(f->args[2],4,PROT_WRITE);nvn_original_helper(NI_nvnDeviceGetInteger,f);return;
    }
    if(!nvn_device.live || f->args[0]!=nvn_device.address)stop("NVN_DEVICE_INVALID",nvn_caller,f->args[0],24);
    int32_t value;uint32_t which=f->args[1];
    {   /* MOMO_DEVICE_INFO_PASSTHROUGH="0x1c,0x1d,...": let the original SDK answer these queries and
         * log what it says. The SDK's own tables are the truth for 18.3.2; guessing a capability is
         * how the whole screen ended up on the error shader. */
        static int configured;static unsigned list[16],count;
        if(!configured) {
            configured=1;const char *option=getenv("MOMO_DEVICE_INFO_PASSTHROUGH");
            while(option && *option && count<16) {
                char *rest=NULL;unsigned long key=strtoul(option,&rest,0);
                if(!rest || rest==option)break;
                list[count++]=(unsigned)key;
                if(*rest!=',')break;
                option=rest+1;
            }
        }
        for(unsigned i=0;i<count;i++)if(list[i]==which) {
            nvn_pointer(f->args[2],4,PROT_WRITE);nvn_original_helper(NI_nvnDeviceGetInteger,f);
            int32_t answered;memcpy(&answered,nvn_pointer(f->args[2],4,PROT_READ),4);
            printf("MOMO_DEVICE_INFO which=0x%x value=%d source=original-sdk\n",which,answered);
            return;
        }
    }
    if(momo_device_info_override(which,&value)) {
        memcpy(nvn_pointer(f->args[2],4,PROT_WRITE),&value,4);
        printf("MOMO_DEVICE_INFO which=0x%x value=%d source=override\n",which,value);
        return;
    }
    switch(which) {
    case 4:value=16;break; /* alignment of reflected host uniform reads */
    case 0xa:value=16;break; /* {u64 value,u64 timestamp} counters */
    case 0xf:case 0x10:value=NVN_DESCRIPTOR_BYTES;break;
    case 0x11:case 0x12:value=NVN_DESCRIPTOR_RESERVED;break;
    case 0x24:case 0x25:value=NVN_DESCRIPTOR_MAX;break;
    case 0xe:value=nvn_device.attributes;break;
    /* MOMODORA: the per-stage binding counts are an NVN contract, not a GL limit. Unity 2021 derives
     * its shader capability level from them, and reporting the Mali GL_MAX_TEXTURE_IMAGE_UNITS (8)
     * makes it treat every shader as unsupported and draw with the internal error shader. The real
     * console reports 32; the host's own translation keeps each program within the 8 physical units. */
    case 9:value=32;break;
    case 0x3d:value=nvn_device.texture_limit;break;
    case 0x38:case 0x39:value=16;break; /* host linear texture rows, nvn_texture_layout */
    case 0x3a:value=4096;break;
    case 0x50:case 0x51:case 0x56:case 0x5b:case 0x5d:value=NVN_QUEUE_PAGE;break;
    case 0x52:value=NVN_QUEUE_DEFAULT_COMMAND;break;
    case 0x5c:value=NVN_QUEUE_DEFAULT_CONTROL;break;
    case 0x13:value=NVN_COMMAND_ALIGNMENT;break;
    case 0x14:value=NVN_CONTROL_ALIGNMENT;break;
    case 0x15:value=512;break; /* one maximal compact record, including payload */
    case 0x16:value=16;break; /* one linked-list control cell */
    case 0x1a:value=NVN_SHADER_SCRATCH_GRANULARITY;break;
    case 0x19:value=ZERO_SHADER_SCRATCH_ALIGNMENT;break;
    case 0x58:value=3;break;
    case 0x59:value=2;break;
    /* Animal Well (NVN 17.x queries): 40 SUPPORTS_STENCIL8_FORMAT; 45..48 GLSLC gpu-code version bounds the
     * game compares with its offline BNSH programs (major 1); 62 advanced blend; 64 draw texture. */
    case 0x28:value=1;break;   /* stencil8 */
    case 0x29:value=0;break;   /* ASTC */
    case 0x2a:value=262144;break; /* L2 size */
    case 0x2d:value=1;break;
    case 0x2e:value=1;break;
    case 0x2f:value=31;break;
    case 0x30:value=0;break;
    case 0x32:value=8;break;   /* subpixel bits */
    case 0x3b:case 0x3e:case 0x40:case 0x42:case 0x43:case 0x44:case 0x45:case 0x48:case 0x4b:case 0x57:case 0x5a:value=0;break; /* optional features absent */
    case 0x47:value=1024;break; /* shader code pool padding */
    case 0x49:value=1048576;break; /* textures per descriptor pool */
    case 0x4a:value=4096;break;    /* samplers per descriptor pool */
    case 0x4c:value=0;break;
    /* MOMODORA: these describe the Maxwell GPU and the NVN/GLSLC generation, not this host's GLES
     * backend, so the guest SDK's own tables are the truth. MEDIDO by forwarding each query to the
     * original nvnDeviceGetInteger on the live device (run momo-094): 0x1b=16, 0x5e=128, 0x8=16,
     * 0x1c=1536, 0x1d=1024, 0x1e=64, 0x1f=1536. The invented 1/0/32/1024 answers are what made Unity
     * treat shaders as unsupported. */
    case 0x1b:value=16;break;
    case 0x5e:value=128;break;
    case 8:value=16;break;
    case 0x1c:value=1536;break;
    case 0x1d:value=1024;break;
    case 0x1e:value=64;break;
    case 0x1f:value=1536;break;
    default:stop("NVN_DEVICE_INTEGER_REQUIRED",nvn_caller,which,20);
    }
    memcpy(nvn_pointer(f->args[2],4,PROT_WRITE),&value,4);
}
#ifdef AW_NO_ROMFS
static uint64_t cotm_nvn_dispatch_body(struct nvn_frame *f,unsigned id);
/* AW_DISPATCH_PROFILE=1: wall time spent inside the NVN bridge on guest thread 0 (the game loop), per API id,
 * reported every 600 bridge-level submits together with the wall time between submits. */
static struct {int configured;unsigned enabled;uint64_t ns[NI_COUNT],calls[NI_COUNT],total_ns,submits,window_start;} aw_dispatch;
#endif
uint64_t cotm_nvn_dispatch(struct nvn_frame *f,unsigned id) {
#ifdef AW_NO_ROMFS
    if(!aw_dispatch.configured){const char *o=getenv("AW_DISPATCH_PROFILE");aw_dispatch.enabled=o&&!strcmp(o,"1");aw_dispatch.configured=1;}
    if(aw_dispatch.enabled && guest_thread_index==0 && !nvn_bridge_depth) {
        uint64_t t0=nvn_nanoseconds();
        uint64_t r=cotm_nvn_dispatch_body(f,id);
        uint64_t dt=nvn_nanoseconds()-t0;aw_dispatch.ns[id]+=dt;aw_dispatch.calls[id]++;aw_dispatch.total_ns+=dt;
        if(id==NI_nvnQueuePresentTexture && ++aw_dispatch.submits%300==0) {
            uint64_t now=nvn_nanoseconds(),wall=aw_dispatch.window_start?now-aw_dispatch.window_start:0;aw_dispatch.window_start=now;
            printf("AW_DISPATCH_PROFILE frames=300 wall-ms-per-frame=%.3f bridge-ms-per-frame=%.3f\n",wall*1e-6/300,aw_dispatch.total_ns*1e-6/300);
            for(unsigned rank=0;rank<12;rank++){unsigned best=0;for(unsigned i=1;i<NI_COUNT;i++)if(aw_dispatch.ns[i]>aw_dispatch.ns[best])best=i;
                if(!aw_dispatch.ns[best])break;
                printf("AW_DISPATCH_TOP op=%s ms-per-frame=%.3f calls-per-frame=%.1f\n",nvn_functions[best].name,aw_dispatch.ns[best]*1e-6/300,aw_dispatch.calls[best]/300.0);aw_dispatch.ns[best]=0;}
            memset(aw_dispatch.ns,0,sizeof(aw_dispatch.ns));memset(aw_dispatch.calls,0,sizeof(aw_dispatch.calls));aw_dispatch.total_ns=0;
        }
        return r;
    }
    return cotm_nvn_dispatch_body(f,id);
}
static uint64_t cotm_nvn_dispatch_body(struct nvn_frame *f,unsigned id) {
#endif
    if(!id || id>=NI_COUNT || signal_handler_depth || nvn_bridge_depth>=8)stop("NVN_BRIDGE_CONTEXT",f->lr,id,24);
    if(t2p_in_cmdmem_callback){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);printf("T2P_CB_NVN name=%s caller=0x%"PRIx64" ns=%"PRIu64"\n",nvn_functions[id].name,f->lr,(uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec);} /* FRENTE-P diag */
    nvn_async_api_enter();
    nvn_bridge_depth++;uint64_t previous=nvn_caller;nvn_caller=f->lr;
    uint64_t count=++nvn_calls[id];nvn_bridge_calls++;
    if(id!=NI_nvnDeviceGetProcAddress && (count<=4 || !(count&(count-1)))) {
        struct line log={0};text(&log,"NVN_CALL name=");text(&log,nvn_functions[id].name);text(&log," caller=");location(&log,f->lr);
        text(&log," original=");location(&log,nvn_original[id]);
        for(unsigned i=0;i<6;i++){text(&log," arg=");hex(&log,f->args[i]);}emit(&log);
    }
    uint64_t result=0;
    if(nvn_memory_dispatch(id,f,&result))goto complete;
    if(nvn_descriptors_dispatch(id,f,&result))goto complete;
    if(nvn_command_dispatch(id,f,&result))goto complete;
    if(nvn_sampler_dispatch(id,f,&result))goto complete;
    switch(id) {
    case NI_nvnDeviceGetProcAddress:result=nvn_lookup(f);break;
    case NI_nvnDeviceGetInteger:nvn_device_integer(f);break;
    case NI_nvnDeviceRegisterFastClearDepth: {
        float depth;memcpy(&depth,&f->vectors[0],sizeof(depth));
        if(!nvn_device.live || f->args[0]!=nvn_device.address || !isfinite(depth))
            stop("NVN_FAST_CLEAR_DEPTH_INVALID",nvn_caller,f->args[0],24);
        /* No Tegra compression registration. Actual depth clears are replayed
         * through GLES; report no optional fast-clear slot. Zero/ZX's caller
         * at main+0x1b7c9cc preserves its normal path for either result. */
        result=0;break;
    }
    case NI_nvnDeviceRegisterFastClearColor:
    case NI_nvnDeviceRegisterFastClearColori:
    case NI_nvnDeviceRegisterFastClearColorui:
        if(!nvn_device.live || f->args[0]!=nvn_device.address)
            stop("NVN_FAST_CLEAR_COLOR_DEVICE",nvn_caller,f->args[0],24);
        nvn_pointer(f->args[1],16,PROT_READ);
        result=0;break; /* normal GLES clears; no optional compression slot */
    case NI_nvnDeviceSetIntermediateShaderCache:
        if(!nvn_device.live || f->args[0]!=nvn_device.address || f->args[1]>INT_MAX)
            stop("NVN_SHADER_CACHE_HINT_INVALID",nvn_caller,f->args[1],24);
        /* This optional NVN compilation cache controls no shader semantics.
         * Retain the requested entry count; GLES owns its compiler cache and
         * this adapter retains linked programs separately. */
        nvn_device.intermediate_cache_hint=(unsigned)f->args[1];
        printf("ZERO_ZX_SHADER_CACHE_HINT entries=%u compiler-cache=GLES-driver\n",nvn_device.intermediate_cache_hint);
        break;
    case NI_nvnDeviceSetWindowOriginMode:case NI_nvnDeviceSetDepthMode:
        if(!nvn_device.live || f->args[0]!=nvn_device.address || f->args[1]>1)
            stop("NVN_DEVICE_CLIP_MODE_INVALID",nvn_caller,f->args[1],24);
        {unsigned *mode=id==NI_nvnDeviceSetWindowOriginMode?&nvn_device.window_origin:&nvn_device.depth_mode;
         if((nvn_window.live || nvn_next_command_handle) && *mode!=f->args[1])
             stop("NVN_DEVICE_LIVE_CLIP_MODE_CHANGE_REQUIRED",nvn_caller,id,20);
         *mode=(unsigned)f->args[1];
         printf("NVN_LC2_CLIP_MODE window-origin=%u depth-mode=%u storage=native-numeric-rows\n",nvn_device.window_origin,nvn_device.depth_mode);}
        break;
    case NI_nvnDeviceGetWindowOriginMode:case NI_nvnDeviceGetDepthMode:
        if(!nvn_device.live || f->args[0]!=nvn_device.address)stop("NVN_DEVICE_INVALID",nvn_caller,f->args[0],24);
        result=id==NI_nvnDeviceGetWindowOriginMode?nvn_device.window_origin:nvn_device.depth_mode;break;
#define NVN_STATE_HELPER(name) case NI_##name:
#include "nvn_state_helpers.inc"
#undef NVN_STATE_HELPER
        nvn_pointer(f->args[0],4,PROT_READ|PROT_WRITE);result=nvn_original_helper(id,f);break;
    case NI_nvnDeviceBuilderSetDefaults:case NI_nvnDeviceBuilderSetFlags:
    case NI_nvnMemoryPoolBuilderSetDefaults:case NI_nvnMemoryPoolBuilderSetDevice:
    case NI_nvnMemoryPoolBuilderSetStorage:case NI_nvnMemoryPoolBuilderSetFlags:
    case NI_nvnMemoryPoolBuilderGetFlags:case NI_nvnMemoryPoolBuilderGetMemory:case NI_nvnMemoryPoolBuilderGetSize:
    case NI_nvnBufferBuilderSetDefaults:case NI_nvnBufferBuilderSetDevice:case NI_nvnBufferBuilderSetStorage:
    case NI_nvnBufferBuilderGetMemoryPool:case NI_nvnBufferBuilderGetMemoryOffset:case NI_nvnBufferBuilderGetSize:
    case NI_nvnQueueBuilderSetDefaults:case NI_nvnQueueBuilderSetDevice:case NI_nvnQueueBuilderSetFlags:
    case NI_nvnQueueBuilderSetCommandMemorySize:case NI_nvnQueueBuilderSetControlMemorySize:
    case NI_nvnQueueBuilderSetComputeMemorySize:case NI_nvnQueueBuilderSetCommandFlushThreshold:case NI_nvnQueueBuilderSetQueueMemory:
        nvn_pointer(f->args[0],64,PROT_READ|PROT_WRITE);result=nvn_original_helper(id,f);break;
    case NI_nvnDeviceFinalizeCommandHandle: {
        /* Animal Well retires the previous frame's handle each frame (SDK 17). The
         * recording's control storage is caller memory; completion is required first. */
        if(!nvn_device.live||f->args[0]!=nvn_device.address)stop("NVN_COMMAND_HANDLE_DEVICE",nvn_caller,f->args[0],24);
        struct nvn_command_list *list=nvn_command_list_lookup(f->args[1]);
        if(!list){static unsigned retired_logged;if(retired_logged<2){retired_logged++;printf("AW_COMMAND_HANDLE_FINALIZE handle=%"PRIu64" already-retired-by-arena-reuse=1\n",f->args[1]);}result=0;break;}
        if(list->submitted && !list->completed){if(nvn_command_pending_poll)nvn_command_pending_poll(list->handle);
            if(!list->completed)stop("AW_COMMAND_HANDLE_FINALIZE_BEFORE_COMPLETION",nvn_caller,list->handle,24);}
        for(unsigned i=0;i<32;i++)if(nvn_command_buffers[i].live && nvn_command_buffers[i].list==list)nvn_command_buffers[i].list=NULL;
        list->live=0;result=0;break;
    }
    default:
        if(nvn_gl_route(id,f,&result))goto complete;
        stop("NVN_OPERATION_REQUIRED",f->lr,id,20);
    }
complete:
    /* TERMINATOR (pc-004): the swapchain pool arithmetic depends on these answers; log them. */
    if(id==NI_nvnTextureBuilderGetStorageSize || id==NI_nvnTextureBuilderGetStorageAlignment || id==NI_nvnTextureBuilderGetZCullStorageSize ||
       id==NI_nvnMemoryPoolBuilderGetSize || id==NI_nvnMemoryPoolGetSize)
        printf("NVN_RESULT name=%s result=0x%"PRIx64"\n",nvn_functions[id].name,result);
    if(id==NI_nvnQueueSubmitCommands)nvn_pointer_report(nvn_replay.last_frame);
    nvn_caller=previous;nvn_bridge_depth--;nvn_async_api_exit();return result;
}
