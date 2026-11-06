/* Physical presentation normally uses a separate shared GLES context. The
 * single-context experiment brackets the copy quad and leaves producer state
 * caches cold. Proof samples only the real backbuffer before eglSwapBuffers.
 */
#include "video_pixels.h"
#include "nvn_video_readback.h"
#include "nvn_video_journal.h"
#include "nvn_video_cadence.h"
#include "nvn_single_present.h"
#include "aw_release.h"
#include "nvn_frame_timing.h"
#include "nvn_gpu_frame_gate.h"
#include "nvn_capture_request.h"
#include "nxgl_frame_proof_adapter.h"
#include "t2_present_pace.h"
static struct {EGLContext context;GLuint program,vertices;GLint image_scale,image_offset;unsigned frames,black_streak,proved,sampled_pixels,full_readback,sample_y,sample_rows;
    unsigned sparse_readback,sample_frame_logs;struct nvn_pixel_band band;} nvn_present;
static void nvn_present_receipt(const char *status,unsigned nonblack,unsigned maximum,unsigned draws) {
    if(aw_release_mode())return;
    uint64_t started=nvn_nanoseconds();
    char json[640];const char *run=getenv("COTM_RUN_ID");
    int size=snprintf(json,sizeof(json),"{\"port\":\"terminator2d-switch-study\",\"run\":\"%s\",\"generation_pid\":%ld,\"frame\":%u,\"status\":\"%s\",\"nonblack_pixels\":%u,\"maximum_rgb\":%u,\"game_draws\":%u,\"black_streak\":%u,\"sampled_pixels\":%u,\"full_readback\":%s,\"sample_y\":%u,\"sample_rows\":%u,\"boundary\":\"%s\"}\n",
        run?run:"",(long)getpid(),nvn_present.frames,status,nonblack,maximum,draws,nvn_present.black_streak,nvn_present.sampled_pixels,nvn_present.full_readback?"true":"false",nvn_present.sample_y,nvn_present.sample_rows,MEGAMAN_VIDEO_BOUNDARY);
    if(size<0 || size>=(int)sizeof(json))stop("NVN_VIDEO_RECEIPT_SIZE",nvn_caller,size,24);
    /* FAT directory lookups grow with every old diagnostic file. A fresh
     * per-run directory retains every sample without scanning previous runs. */
    static int directory=-1;
    static struct nvn_video_journal journal={.fd=-1};
    if(directory<0) {
        if(!run||!*run||strlen(run)>12||strspn(run,"0123456789")!=strlen(run))stop("NVN_CAPTURE_RUN_ID",nvn_caller,0,24);
        char path[64];snprintf(path,sizeof(path),"nvn-video-%s",run);
        printf("NVN_RECEIPT_DIRECTORY run=%s path=%s cwd-present=1\n",run,path);
        if(mkdir(path,0700)){printf("NVN_RECEIPT_DIRECTORY_ERR path=%s errno=%d\n",path,errno);stop("NVN_RECEIPT_DIRECTORY",nvn_caller,errno,21);}
        directory=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(directory<0)stop("NVN_RECEIPT_DIRECTORY_OPEN",nvn_caller,errno,21);
        printf("NVN_VIDEO_RECEIPTS path=%s format=jsonl files=group-N.jsonl group-frames=256 records=one-per-frame\n",path);
    }
    unsigned journal_status=nvn_video_journal_append(&journal,directory,nvn_present.frames,json,size);
    if(journal_status==NVJ_CLOSE)stop("NVN_RECEIPT_GROUP_CLOSE",nvn_caller,errno,21);
    if(journal_status==NVJ_OPEN)stop("NVN_RECEIPT_GROUP_OPEN",nvn_caller,errno,21);
    if(journal_status==NVJ_WRITE)stop("NVN_RECEIPT_WRITE",nvn_caller,errno,21);
    nvn_profile.receipt_ns+=nvn_nanoseconds()-started;
}
#ifdef ANIMAL_WELL_SHADERS
#include <sys/mman.h>
/* AW_FB_COPY=1: the Mali fbdev eglSwapBuffers waits one or two vsyncs (16-33 ms measured, tools/aw_pass_bench.c)
 * and ignores eglSwapInterval(0). Present instead into an FBO, read it back and copy it into the mapped
 * /dev/fb0 (BGRA, rows flipped), flipping between the two framebuffer pages with FBIOPAN_DISPLAY. */
static struct {int configured;unsigned enabled,w,h,line,pages,page;GLuint fbo,tex;int fd;uint8_t *map;size_t map_len;uint8_t *pixels;struct fb_var_screeninfo var;uint64_t frames,read_ns,copy_ns;} aw_fbcopy={.fd=-1};
static unsigned aw_fbcopy_enabled(void) {
    if(!aw_fbcopy.configured){const char *o=getenv("AW_FB_COPY");aw_fbcopy.enabled=o&&!strcmp(o,"1");aw_fbcopy.configured=1;}
    return aw_fbcopy.enabled;
}
static void aw_fbcopy_open(unsigned w,unsigned h) {
    if(aw_fbcopy.fd>=0)return;
    aw_fbcopy.fd=open("/dev/fb0",O_RDWR|O_CLOEXEC);if(aw_fbcopy.fd<0)stop("AW_FB_COPY_OPEN",nvn_caller,errno,21);
    struct fb_fix_screeninfo fix;if(ioctl(aw_fbcopy.fd,FBIOGET_FSCREENINFO,&fix)<0||ioctl(aw_fbcopy.fd,FBIOGET_VSCREENINFO,&aw_fbcopy.var)<0)stop("AW_FB_COPY_INFO",nvn_caller,errno,21);
    if(aw_fbcopy.var.xres!=w||aw_fbcopy.var.yres!=h||aw_fbcopy.var.bits_per_pixel!=32||aw_fbcopy.var.blue.offset!=0||aw_fbcopy.var.red.offset!=16)stop("AW_FB_COPY_FORMAT",nvn_caller,aw_fbcopy.var.bits_per_pixel,21);
    aw_fbcopy.line=fix.line_length;aw_fbcopy.pages=aw_fbcopy.var.yres_virtual/h;if(aw_fbcopy.pages<1)aw_fbcopy.pages=1;if(aw_fbcopy.pages>2)aw_fbcopy.pages=2;
    aw_fbcopy.map_len=fix.smem_len;aw_fbcopy.map=mmap(NULL,aw_fbcopy.map_len,PROT_READ|PROT_WRITE,MAP_SHARED,aw_fbcopy.fd,0);
    if(aw_fbcopy.map==MAP_FAILED)stop("AW_FB_COPY_MMAP",nvn_caller,errno,21);
    aw_fbcopy.pixels=malloc((size_t)w*h*4);if(!aw_fbcopy.pixels)stop("AW_FB_COPY_MEMORY",nvn_caller,w*h*4,21);
    aw_fbcopy.w=w;aw_fbcopy.h=h;
    glGenTextures(1,&aw_fbcopy.tex);glBindTexture(GL_TEXTURE_2D,aw_fbcopy.tex);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,(GLsizei)w,(GLsizei)h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glGenFramebuffers(1,&aw_fbcopy.fbo);glBindFramebuffer(GL_FRAMEBUFFER,aw_fbcopy.fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,aw_fbcopy.tex,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)stop("AW_FB_COPY_FBO",nvn_caller,0,21);
    printf("AW_FB_COPY enabled=1 fb=%ux%u line=%u pages=%u swap=none present=fbo+readpixels+memcpy pan=FBIOPAN_DISPLAY\n",w,h,aw_fbcopy.line,aw_fbcopy.pages);
}
static void aw_fbcopy_present(void) {
    uint64_t t0=nvn_nanoseconds();unsigned w=aw_fbcopy.w,h=aw_fbcopy.h;
    glBindFramebuffer(GL_FRAMEBUFFER,aw_fbcopy.fbo);glReadPixels(0,0,(GLsizei)w,(GLsizei)h,GL_RGBA,GL_UNSIGNED_BYTE,aw_fbcopy.pixels);
    uint64_t t1=nvn_nanoseconds();
    unsigned page=(aw_fbcopy.page+1)%aw_fbcopy.pages;uint8_t *dst=aw_fbcopy.map+(size_t)page*h*aw_fbcopy.line;
    if(aw_fbcopy.line==w*4)memcpy(dst,aw_fbcopy.pixels,(size_t)w*h*4);
    else for(unsigned y=0;y<h;y++)memcpy(dst+(size_t)y*aw_fbcopy.line,aw_fbcopy.pixels+(size_t)y*w*4,(size_t)w*4);
    aw_fbcopy.var.yoffset=page*h;aw_fbcopy.var.xoffset=0;ioctl(aw_fbcopy.fd,FBIOPAN_DISPLAY,&aw_fbcopy.var);aw_fbcopy.page=page;
    aw_fbcopy.read_ns+=t1-t0;aw_fbcopy.copy_ns+=nvn_nanoseconds()-t1;
    if(++aw_fbcopy.frames%300==0)printf("AW_FB_COPY_PROFILE frames=%"PRIu64" readpixels-ms=%.2f copy-ms=%.2f per-frame\n",aw_fbcopy.frames,aw_fbcopy.read_ns*1e-6/300,aw_fbcopy.copy_ns*1e-6/300),aw_fbcopy.read_ns=aw_fbcopy.copy_ns=0;
}
#endif
static void nvn_present_setup(void) {
    if(nvn_present.context)return;
    nxgl_frame_proof_launch_receipt();
    int request_installed=nvn_capture_request_install();
    if(request_installed<0)stop("NVN_CAPTURE_SIGNAL_INSTALL",nvn_caller,errno,21);
    if(request_installed) {
        const char *run=getenv("COTM_RUN_ID");
        if(!run||!*run||strlen(run)>12||strspn(run,"0123456789")!=strlen(run))stop("NVN_CAPTURE_RUN_ID",nvn_caller,0,24);
        printf("NVN_CAPTURE_ON_DEMAND_READY pid=%ld run=%s signal=%d\n",(long)getpid(),run,SIGUSR1);
    }
    const char *sparse=getenv("COTM_PRESENT_READBACK_60");
    if(sparse&&strcmp(sparse,"0")&&strcmp(sparse,"1"))stop("NVN_PRESENT_READBACK_OPTION",nvn_caller,0,24);
    nvn_present.sparse_readback=sparse&&!strcmp(sparse,"1");
    const char *sample_logs=getenv("COTM_SAMPLE_FRAME_LOGS");
    if(sample_logs&&strcmp(sample_logs,"0")&&strcmp(sample_logs,"1"))stop("NVN_PRESENT_LOG_OPTION",nvn_caller,0,24);
    nvn_present.sample_frame_logs=sample_logs&&!strcmp(sample_logs,"1");
    if(nvn_present.sample_frame_logs)printf("NVN_VIDEO_LOG_CADENCE measured=always unmeasured=journal-only journal=one-per-frame\n");
    const EGLint attrs[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    nvn_present.context=nvn_single_enabled()?nvn_device.context:eglCreateContext(nvn_device.display,nvn_device.config,nvn_device.context,attrs);
    if(nvn_present.context==EGL_NO_CONTEXT || !nvn_single_make_current(nvn_window.surface,nvn_window.surface,nvn_present.context))
        stop("NVN_PRESENT_CONTEXT",nvn_caller,eglGetError(),21);
    const char *gl_extensions=(const char *)glGetString(GL_EXTENSIONS),*egl_extensions=eglQueryString(nvn_device.display,EGL_EXTENSIONS);
    EGLint proof_width=0,proof_height=0;
    (void)eglQuerySurface(nvn_device.display,nvn_window.surface,EGL_WIDTH,&proof_width);
    (void)eglQuerySurface(nvn_device.display,nvn_window.surface,EGL_HEIGHT,&proof_height);
    nxgl_frame_proof_set_video_context(proof_width,proof_height,"EGL/GLES2",
        (const char *)glGetString(GL_RENDERER),(const char *)glGetString(GL_VERSION));
    printf("NVN_GL_EXTENSIONS context=presentation value=%s\n",gl_extensions?gl_extensions:"UNAVAILABLE");
    printf("NVN_EGL_EXTENSIONS value=%s\n",egl_extensions?egl_extensions:"UNAVAILABLE");
    EGLint minimum_interval=-1,maximum_interval=-1;
    if(!eglGetConfigAttrib(nvn_device.display,nvn_device.config,EGL_MIN_SWAP_INTERVAL,&minimum_interval) ||
       !eglGetConfigAttrib(nvn_device.display,nvn_device.config,EGL_MAX_SWAP_INTERVAL,&maximum_interval))
        stop("NVN_PRESENT_SWAP_CONFIG",nvn_caller,eglGetError(),21);
    printf("NVN_PRESENT_SWAP_EXPERIMENT requested=0 guest=%u config-min=%d config-max=%d scope=A1-driver-may-clamp\n",
        nvn_window.interval,minimum_interval,maximum_interval);
    unsigned sample_period=nvn_video_period(),capture_period=nvn_video_capture_period();
    printf("NVN_VIDEO_CADENCE every=%u capture-every=%u dark-every=%u initial-every=1 initial-full=1 band-rows=16 negative-band=fresh-full unmeasured=NOT-SAMPLED max-initial-dark-detection-delay=%u-frames\n",
        nvn_present.sparse_readback?sample_period:1,capture_period,nvn_present.sparse_readback?4:1,
        nvn_present.sparse_readback?sample_period-1:0);
    const char *sources[]={
        "attribute vec2 p;attribute vec2 t;uniform vec2 imageScale;uniform vec2 imageOffset;varying mediump vec2 uv;void main(){gl_Position=vec4(p,0.,1.);uv=t*imageScale+imageOffset;}",
#ifdef ANIMAL_WELL_SHADERS
        aw_fbcopy_enabled()?"precision mediump float;varying mediump vec2 uv;uniform sampler2D image;void main(){gl_FragColor=vec4(texture2D(image,uv).bgr,1.);}":
#endif
        "precision mediump float;varying mediump vec2 uv;uniform sampler2D image;void main(){gl_FragColor=vec4(texture2D(image,uv).rgb,1.);}"};
    GLuint shaders[2];nvn_present.program=glCreateProgram();
    for(unsigned i=0;i<2;i++) {
        shaders[i]=glCreateShader(i?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);glShaderSource(shaders[i],1,&sources[i],NULL);glCompileShader(shaders[i]);
        GLint ok=0;glGetShaderiv(shaders[i],GL_COMPILE_STATUS,&ok);if(!ok)stop("NVN_PRESENT_SHADER",nvn_caller,i,21);
        glAttachShader(nvn_present.program,shaders[i]);
    }
    glBindAttribLocation(nvn_present.program,0,"p");glBindAttribLocation(nvn_present.program,1,"t");glLinkProgram(nvn_present.program);
    GLint ok=0;glGetProgramiv(nvn_present.program,GL_LINK_STATUS,&ok);if(!ok)stop("NVN_PRESENT_PROGRAM",nvn_caller,0,21);
    for(unsigned i=0;i<2;i++){glDetachShader(nvn_present.program,shaders[i]);glDeleteShader(shaders[i]);}
    GLfloat quad[]={-1,-1,0,0, 1,-1,1,0, -1,1,0,1, 1,1,1,1};
    if(nvn_device.window_origin)for(unsigned i=0;i<4;i++)quad[i*4+3]=1.0f-quad[i*4+3];
#ifdef ANIMAL_WELL_SHADERS
    if(aw_fbcopy_enabled())for(unsigned i=0;i<4;i++)quad[i*4+3]=1.0f-quad[i*4+3]; /* rows come out top-down for the fb copy */
#endif
    glGenBuffers(1,&nvn_present.vertices);glBindBuffer(GL_ARRAY_BUFFER,nvn_present.vertices);glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,16,0);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,16,(void *)8);
    glUseProgram(nvn_present.program);glUniform1i(glGetUniformLocation(nvn_present.program,"image"),0);
    nvn_present.image_scale=glGetUniformLocation(nvn_present.program,"imageScale");
    nvn_present.image_offset=glGetUniformLocation(nvn_present.program,"imageOffset");
    if(nvn_present.image_scale<0 || nvn_present.image_offset<0)stop("NVN_PRESENT_IMAGE_SCALE",nvn_caller,nvn_present.program,21);
    glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);
}
static unsigned nvn_present_dispatch(unsigned id,struct nvn_frame *f,uint64_t *result) {
    if(id!=NI_nvnQueuePresentTexture)return 0;
    unsigned timing=nvn_frame_timing_enabled(),timing_sampled=0,timing_capture=0,cadence_pixel_proof=0;
    uint64_t timing_readback_ns=0,timing_dump_ns=0;
    uint64_t started=nvn_nanoseconds();
    *result=0;struct nvn_queue *q=nvn_queue_find(f->args[0]);nvn_window_require(f->args[1]);uint64_t index=f->args[2];
    if(index>=nvn_window.count || (!nvn_present_from_job && !nvn_window.acquired[index]))stop("NVN_PRESENT_OWNERSHIP",nvn_caller,index,24);
    if(megaman_frame_cadence_begin_present(nvn_window.textures[index])) {
        nvn_window.acquired[index]=0;
        megaman_frame_cadence_end_present(0,0);
        return 1;
    }
    nvn_normal_require_materialized(nvn_window.textures[index]);
    {static unsigned pc;if(pc<8){struct nvn_texture *pt=nvn_window.textures[index];unsigned c=0;unsigned char px[4]={0,0,0,0};
        unsigned mx=pt?nvn_texture_probe_max(pt,&c,px):0;
        if(nvn_present.frames==5&&pt&&pt->name){GLint fbo0=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fbo0);GLuint tf=0;glGenFramebuffers(1,&tf);glBindFramebuffer(GL_FRAMEBUFFER,tf);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,pt->name,0);GLenum st=glCheckFramebufferStatus(GL_FRAMEBUFFER);
            printf("NVN_SWAP_DUMP status=0x%x %ux%u\n",(unsigned)st,(unsigned)pt->b.width,(unsigned)pt->b.height);
            if(st==GL_FRAMEBUFFER_COMPLETE){int w=pt->b.width,h=pt->b.height;unsigned char *b=(unsigned char*)malloc((size_t)w*h*4);
                if(b){glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,b);
                    FILE *fp=fopen("/mnt/SDCARD/Roms/PORTS/infernax/swap.ppm","wb");
                    if(fp){fprintf(fp,"P6\n%d %d\n255\n",w,h);for(int i=h-1;i>=0;i--)for(int x=0;x<w;x++)fwrite(b+((size_t)i*w+x)*4,1,3,fp);fclose(fp);printf("NVN_SWAP_DUMP wrote=1\n");} else printf("NVN_SWAP_DUMP wrote=0 errno=%d\n",errno);free(b);}}
            glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)fbo0);glDeleteFramebuffers(1,&tf);}
        printf("NVN_PRESENT_SRC frame=%u index=%"PRIu64" obj=0x%"PRIx64" name=%u %ux%u centre-max=%u px=%u,%u,%u,%u\n",
            nvn_present.frames,index,pt?pt->object:0,pt?pt->name:0,pt?(unsigned)pt->b.width:0,pt?(unsigned)pt->b.height:0,mx,px[0],px[1],px[2],px[3]);pc++;}}
#ifdef ANIMAL_WELL_SHADERS
    {   /* AW_NO_PRESENT=1: fps experiment. No GL, no swap: the screen keeps the last frame; ownership and counters
         * behave as a skipped frame. Placed before the GL mutex like the frameskip path. */
        static int npc;static unsigned np;if(!npc){const char *o=getenv("AW_NO_PRESENT");np=o&&!strcmp(o,"1");npc=1;if(np)printf("AW_NO_PRESENT enabled=1 present-quad=skipped swap=skipped\n");}
        if(np){nvn_present.frames++;nvn_replay.last_frame=nvn_present.frames;nvn_window.acquired[index]=0;return 1;}
    }
#endif
    if(nvn_skip.active) {
        /* Skipped frame: no GL, no swap; keep ownership, counters and the
         * per-60-logic-frame profile line consumed by the automation. */
        nvn_present.frames++;nvn_replay.last_frame=nvn_present.frames;nvn_window.acquired[index]=0;
        uint64_t now=nvn_nanoseconds();nvn_profile.present_ns+=now-started;nvn_profile.frames++;
        if(nvn_profile.start && nvn_profile.frames>=60) {
            double ms=1e-6/nvn_profile.frames;
            printf("NVN_PROFILE frame=%u count=%u elapsed-ms=%.3f replay-ms=%.3f present-ms=%.3f readback-ms=%.3f replay-cpu-ms=%.3f scope=wall-time-per-frame frameskip=%u rendered=%"PRIu64" skipped=%"PRIu64"\n",nvn_present.frames,nvn_profile.frames,
                (now-nvn_profile.start)*ms,nvn_profile.replay_ns*ms,nvn_profile.present_ns*ms,nvn_profile.readback_ns*ms,nvn_profile.replay_cpu_ns*ms,nvn_skip.every,nvn_skip.rendered,nvn_skip.skipped);
            nvn_phase_report(nvn_present.frames,nvn_profile.frames);
        }
        if(!nvn_profile.start||nvn_profile.frames>=60){nvn_profile=(typeof(nvn_profile)){.start=now};nvn_counter_profile_reset();}
        return 1;
    }
    t2_present_pace_before(); /* FRENTE-C: opt-in even cadence, outside the GL mutex */
    nvn_gpu_frame_gate_wait(NVN_GFG_PRESENT);
    pthread_mutex_lock(&nvn_gl_completion.mutex);
    unsigned single=nvn_single_enabled();nvn_single_require_owner();
    if(!nvn_async_frame_enabled())nvn_async_present_wait_locked(q,single);
    if(!single)nvn_egl_present_reader_wait();
    if(single) {
        if(!nvn_single_make_current(nvn_window.surface,nvn_window.surface,nvn_device.context))stop("NVN_PRESENT_CONTEXT",nvn_caller,eglGetError(),21);
        /* This is a logical context use even though its physical binding is
         * retained. Do not reuse a completion proof from before the quad. */
        if(!++nvn_gl_completion.use_serial)stop("NVN_GL_SERIAL_WRAP",nvn_caller,0,24);
        nvn_gl_completion.completion_valid=0;
    }
    struct nvn_single_present_state saved;nvn_single_present_save(&saved);
    {   static int pd_configured;static unsigned pd_enabled;
        if(!pd_configured){const char *o=getenv("COTM_DEPTH_DISCARD_ALL");pd_enabled=o&&!strcmp(o,"1");pd_configured=1;}
        if(pd_enabled&&nvn_render.framebuffer&&nvn_render.depth&&nvn_normal_discard_resolve()){const GLenum attachment=GL_DEPTH_ATTACHMENT;nvn_normal_discard_state.discard(GL_FRAMEBUFFER,1,&attachment);}
    }
    nvn_present_setup();
    momo_stencil_selftest();
    if(!nvn_single_make_current(nvn_window.surface,nvn_window.surface,nvn_present.context))stop("NVN_PRESENT_CONTEXT",nvn_caller,eglGetError(),21);
    t2_present_gpu_wait_issue(); /* FRENTE-C: GPU-side order after the frame (T2_PRESENT_GPU_WAIT=1) */
    nvn_single_present_quad(nvn_present.program,nvn_present.vertices);
    struct nvn_texture *t=nvn_window.textures[index];unsigned width=nvn_window.fb.width,height=nvn_window.fb.height;
#ifdef ANIMAL_WELL_SHADERS
    if(aw_fbcopy_enabled()){aw_fbcopy_open(width,height);glBindFramebuffer(GL_FRAMEBUFFER,aw_fbcopy.fbo);}
    else glBindFramebuffer(GL_FRAMEBUFFER,0);
#else
    glBindFramebuffer(GL_FRAMEBUFFER,0);
#endif
    const nxcompat_video_owner_decision *video=t2_video_layout(
        (int)((uint64_t)nvn_window.crop[2]*nvn_low_render_width(t)/t->b.width),
        (int)((uint64_t)nvn_window.crop[3]*nvn_low_render_height(t)/t->b.height),width,height);
    if(!video)stop("T2_VIDEO_LAYOUT",nvn_caller,((uint64_t)width<<32)|height,24);
    const nxcompat_video_rect *rect=&video->geometry.content;
    if(video->geometry.bar_left || video->geometry.bar_right ||
       video->geometry.bar_top || video->geometry.bar_bottom) {
        /* Clear all backbuffer pages each frame; never leak stale pixels into
         * bars. The present context has scissor disabled and RGBA writes on.
         * Preserve the producer's clear color in the single-context path. */
        GLfloat previous_clear[4];glGetFloatv(GL_COLOR_CLEAR_VALUE,previous_clear);
        glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(previous_clear[0],previous_clear[1],previous_clear[2],previous_clear[3]);
    }
    glViewport(rect->x,(int)height-rect->y-rect->h,rect->w,rect->h);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,t->name);
    nvn_texture_parameters(t,GL_NEAREST,GL_NEAREST,GL_CLAMP_TO_EDGE,GL_CLAMP_TO_EDGE);
    GLfloat scale=(t->host_half||t->host_scaled)?1.0f:nvn_low_present_scale(t),scale_y=(t->host_half||t->host_scaled)?1.0f:nvn_low_present_scale_y(t);
    nvn_rt_storage_require(t);
    glUniform2f(nvn_present.image_scale,scale*nvn_window.crop[2]/t->b.width,scale_y*nvn_window.crop[3]/t->b.height);
    glUniform2f(nvn_present.image_offset,scale*nvn_window.crop[0]/t->b.width,scale_y*nvn_window.crop[1]/t->b.height);
    {   /* One probe of the texture the present quad samples. Black + black means the guest replay never
         * wrote visible pixels; black + lit source means the quad/UV/state is wrong. */
        static unsigned probed;
        if(probed<6) {
            probed++;
            GLint previous_fbo;GLuint fbo=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&previous_fbo);
            glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t->name,0);
            unsigned maximum=0;GLenum complete=glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if(complete==GL_FRAMEBUFFER_COMPLETE) {
                unsigned char px[16*16*4];GLint pack;glGetIntegerv(GL_PACK_ALIGNMENT,&pack);glPixelStorei(GL_PACK_ALIGNMENT,1);
                glReadPixels((GLint)((t->b.width/2)&~15u),(GLint)((t->b.height/2)&~15u),16,16,GL_RGBA,GL_UNSIGNED_BYTE,px);
                glPixelStorei(GL_PACK_ALIGNMENT,pack);
                for(unsigned i=0;i<16*16;i++)for(unsigned c=0;c<3;c++)if(px[i*4+c]>maximum)maximum=px[i*4+c];
            }
            glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)previous_fbo);glDeleteFramebuffers(1,&fbo);
            printf("NVN_PRESENT_SOURCE frame=%u id=%"PRIu64" size=%ux%u draws=%u complete=0x%x max=%u scale=%.3f/%.3f offset=%.3f/%.3f viewport=%d,%d,%d,%d\n",
                nvn_present.frames+1,t->id,t->b.width,t->b.height,t->draws,(unsigned)complete,maximum,
                (double)scale,(double)scale_y,(double)(nvn_window.crop[0]/t->b.width),(double)(nvn_window.crop[1]/t->b.height),
                (int)rect->x,(int)rect->y,(int)rect->w,(int)rect->h);
        }
    }
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);nvn_present.frames++;
    if(nvn_flag_on("NX_PRESENT_CLEAR")){ /* Physical check: is the swap chain itself reaching the panel? */
        static int told;glClearColor(1.0f,0.0f,1.0f,1.0f);glClear(GL_COLOR_BUFFER_BIT);
        if(told++<3)printf("NVN_PRESENT_CLEAR frame=%u color=magenta error=0x%x\n",nvn_present.frames,(unsigned)glGetError());}
    if(t2_video_needs_readback()) {
        GLint viewport[4];EGLint measured_w=0,measured_h=0;
        glGetIntegerv(GL_VIEWPORT,viewport);
        if(!eglQuerySurface(nvn_device.display,nvn_window.surface,EGL_WIDTH,&measured_w) ||
           !eglQuerySurface(nvn_device.display,nvn_window.surface,EGL_HEIGHT,&measured_h) ||
           t2_video_readback(measured_w,measured_h,viewport[0],
               measured_h-viewport[1]-viewport[3],viewport[2],viewport[3]))
            stop("T2_VIDEO_READBACK",nvn_caller,0,24);
    }
    {   /* MOMO_FRAME_CLOCK=1: one wall-clock stamp per presented frame. The owner's rule measures more
         * than the median - the worst 1% and the jitter of the period decide too - and that needs a
         * timestamp per frame, not a rate averaged over sixty. One printf, no readback, no sync. */
        static int cfg,on;
        if(!cfg){on=getenv("MOMO_FRAME_CLOCK")!=NULL;cfg=1;}
        if(on){struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
            printf("MOMO_FRAME frame=%u ns=%llu\n",nvn_present.frames,
                (unsigned long long)now.tv_sec*1000000000ull+(unsigned long long)now.tv_nsec);}
    }
    unsigned capture_requested=nvn_capture_request_consume();
    if(capture_requested||nvn_video_should_sample(nvn_present.sparse_readback,nvn_present.frames,nvn_present.proved,nvn_present.black_streak)) {
    unsigned capture=capture_requested||nvn_video_should_capture(nvn_present.frames,nvn_present.proved);
    struct nvn_video_readback readback;unsigned read_status=nvn_video_readback(width,height,capture,&nvn_present.band,&readback);
    if(timing){timing_sampled=1;timing_readback_ns=readback.elapsed_ns;}
    if(read_status==NVR_DIMENSIONS)stop("NVN_VIDEO_READBACK_DIMENSIONS",nvn_caller,((uint64_t)width<<32)|height,24);
    if(read_status==NVR_MEMORY)stop("NVN_VIDEO_READBACK_MEMORY",nvn_caller,readback.bytes,21);
    nvn_present.sampled_pixels=readback.bytes/4;nvn_present.full_readback=readback.rows==height;
    nvn_present.sample_y=readback.y;nvn_present.sample_rows=readback.rows;
    uint8_t *pixels=readback.pixels;
    if(read_status!=NVR_OK) {
        nvn_present_receipt("DEAD-CONTEXT",0,0,nvn_replay.draws);free(pixels);stop("NVN_PRESENT_DEAD_CONTEXT",nvn_caller,readback.error,22);
    }
    nvn_profile.readback_ns+=readback.elapsed_ns;
    unsigned nonblack=readback.measure.nonblack,maximum=readback.measure.maximum;
    if(!nonblack)nvn_present.black_streak++;else nvn_present.black_streak=0;
    nvn_replay.last_frame=nvn_present.frames;nvn_replay.black_after_proof=nvn_present.proved?nvn_present.black_streak:0;
    unsigned proof=nonblack>=32 && t->draws;
    cadence_pixel_proof=proof;
    const char *status=proof?"PIXELS":nonblack?"INCONCLUSIVE":"BLACK-SAMPLE";
    printf("NVN_PRESENT_PIXELS frame=%u nonblack=%u max=%u white=%u texture-draws=%u status=%s\n",nvn_present.frames,nonblack,maximum,readback.measure.white,t->draws,status);
    /* Native54 frame381 proves a black atlas background, RGB0 glyph vertices
     * and a transparent final overlay in the color layer; the normal target
     * still renders. Twelve samples were insufficient to observe this native
     * transition. Keep every dark sample unproved, bound this diagnostic
     * observation to120frames, and terminate on the first conclusive BLACK.
     * No prior PIXELS result grants health during a dark interval; the external
     * wall-clock/memory limits remain. This is not a release acceptance policy. */
    static unsigned black_limit;if(!black_limit){const char *o=getenv("COTM_BLACK_LIMIT");black_limit=o?(unsigned)strtoul(o,NULL,10):120;if(black_limit<120)black_limit=120;}
    nvn_present_receipt(nvn_present.black_streak>=black_limit?"BLACK":status,nonblack,maximum,t->draws);
    if(nvn_present.black_streak>=black_limit){free(pixels);stop("NVN_PRESENT_BLACK",nvn_caller,nvn_present.frames,22);}
    if(capture_requested&&!proof)printf("NVN_CAPTURE_ON_DEMAND_NO_CAPTURE pid=%ld run=%s frame=%u status=%s\n",
        (long)getpid(),getenv("COTM_RUN_ID"),nvn_present.frames,status);
    if(nvn_video_capture_ready(capture,proof,width,height,readback.y,readback.rows,readback.bytes)) {
        uint64_t dump_started=timing?nvn_nanoseconds():0;
        char header[64];int n=snprintf(header,sizeof(header),"P6\n%u %u\n255\n",width,height);
        size_t size=(size_t)width*height*3+n;uint8_t *ppm=malloc(size);if(!ppm)stop("NVN_VIDEO_CAPTURE_MEMORY",nvn_caller,size,21);
        memcpy(ppm,header,n);
        for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++)memcpy(ppm+n+((size_t)y*width+x)*3,pixels+((size_t)(height-1-y)*width+x)*4,3);
        nvn_private_dump("frame-ppm",nvn_present.frames,ppm,size);free(ppm);nvn_present.proved=1;
        if(timing){timing_capture=1;timing_dump_ns=nvn_nanoseconds()-dump_started;}
        if(capture_requested)printf("NVN_CAPTURE_ON_DEMAND_CAPTURE_READY pid=%ld run=%s frame=%u signal=%d\n",
            (long)getpid(),getenv("COTM_RUN_ID"),nvn_present.frames,SIGUSR1);
    }
    free(pixels);
    } else {
        nvn_present.sampled_pixels=nvn_present.full_readback=nvn_present.sample_y=nvn_present.sample_rows=0;
        nvn_replay.last_frame=nvn_present.frames;
        if(!nvn_present.sample_frame_logs&&!aw_release_mode())printf("NVN_PRESENT_UNMEASURED frame=%u status=NOT-SAMPLED\n",nvn_present.frames);
        if(nvn_present_lean_enabled())nvn_present_lean.skipped_receipts++;else nvn_present_receipt("NOT-SAMPLED",0,0,t->draws);
        if(!single) {
            if(nvn_egl_fences.present_reader)stop("NVN_PRESENT_READER_REUSED",nvn_caller,nvn_present.frames,24);
            nvn_egl_fences.present_reader=nvn_egl_fence_create_current();
        }
    }
    uint64_t swap_started=nvn_nanoseconds();
#ifdef ANIMAL_WELL_SHADERS
    if(aw_fbcopy_enabled())aw_fbcopy_present();else {
        nxgl_frame_proof_before_present(width,height);
        if(!eglSwapInterval(nvn_device.display,0) || !nvn_platform_swap(nvn_device.display,nvn_window.surface))stop("NVN_PRESENT_SWAP_FAILED",nvn_caller,eglGetError(),22);
    }
#else
    nxgl_frame_proof_before_present(width,height);
    if(!eglSwapInterval(nvn_device.display,0) || !nvn_platform_swap(nvn_device.display,nvn_window.surface))stop("NVN_PRESENT_SWAP_FAILED",nvn_caller,eglGetError(),22);
#endif
    {   /* SDL owns the window's EGL surface and rebinds it when a foreign context becomes current, so the
         * pointer taken at init goes stale after the first swap: eglQuerySurface then answers
         * EGL_BAD_SURFACE and the next queue submit dies. Re-publish whichever surface is live. */
        EGLSurface live=eglGetCurrentSurface(EGL_DRAW);
        if(live!=EGL_NO_SURFACE && live!=nvn_window.surface) {
            printf("NVN_WINDOW_SURFACE_REPUBLISH old=%p live=%p\n",(void *)nvn_window.surface,(void *)live);
            if(nvn_device.surface==nvn_window.surface)nvn_device.surface=live;
            nvn_window.surface=live;nvn_platform_sdl_surface=live;nvn_diag_window_surface=live;
        }
    }
    if(nxgl_frame_proof_consume_fatal())stop("NVN_FRAME_PROOF_FATAL",nvn_caller,nvn_present.frames,22);
    uint64_t swap_finished=nvn_nanoseconds();nvn_profile.swap_ns+=swap_finished-swap_started;
    t2_runtime_health_after_present(cadence_pixel_proof);
    t2_present_pace_after();
    nvn_gpu_frame_gate_after_swap(nvn_present.frames);
    if(timing)nvn_frame_timing_observe(nvn_present.frames,swap_finished,timing_sampled,timing_capture,timing_readback_ns,timing_dump_ns);
    {   /* Frente P: T2P_SLOW_FRAME - for every presented frame whose period exceeds T2P_SLOW_MS (default 40; 0 = off),
         * the host-side parts of that period: replay wall (incl. glFinish), finish, present path, swap, readback.
         * Deltas of the nvn_profile accumulators since the previous present (they reset every 60 frames). */
        static int cfg;static double slow_ms;static uint64_t last_swap,l_replay,l_finish,l_read,l_swap,l_present;
        if(!cfg){const char *o=getenv("T2P_SLOW_MS");slow_ms=o?strtod(o,NULL):40.0;cfg=1;}
        uint64_t pres_now=nvn_profile.present_ns+(swap_finished-started);
        if(slow_ms>0&&last_swap&&(swap_finished-last_swap)*1e-6>slow_ms)
            printf("T2P_SLOW_FRAME frame=%u period-ms=%.1f replay-ms=%.1f finish-ms=%.1f present-ms=%.1f swap-ms=%.1f readback-ms=%.1f\n",
                nvn_present.frames,(swap_finished-last_swap)*1e-6,(nvn_profile.replay_ns-l_replay)*1e-6,(nvn_profile.finish_ns-l_finish)*1e-6,
                (pres_now-l_present)*1e-6,(nvn_profile.swap_ns-l_swap)*1e-6,(nvn_profile.readback_ns-l_read)*1e-6);
        last_swap=swap_finished;l_replay=nvn_profile.replay_ns;l_finish=nvn_profile.finish_ns;l_read=nvn_profile.readback_ns;l_swap=nvn_profile.swap_ns;l_present=pres_now;
        if(!nvn_profile.start||nvn_profile.frames+1>=60)l_replay=l_finish=l_read=l_swap=l_present=0; /* the accumulators reset below */
    }
    t2_page_cache_after_present(nvn_present.frames); /* context current, completion mutex held */
    t2_residency_after_present(nvn_present.frames); /* Frente B: working-set log / idle residency */
    nvn_counter_profile.last_swap_ns=swap_finished;
    nvn_single_present_restore(&saved);
    if(!nvn_single_detach())stop("NVN_PRESENT_DETACH",nvn_caller,eglGetError(),21);
    pthread_mutex_unlock(&nvn_gl_completion.mutex);
    if(!nvn_present_from_job)nvn_window.acquired[index]=0;
    uint64_t now=nvn_nanoseconds();nvn_profile.present_ns+=now-started;nvn_profile.frames++;
    if(nvn_profile.start && nvn_profile.frames>=60) {
        nvn_single_report(nvn_present.frames);
        nvn_gpu_frame_gate_report(nvn_present.frames,nvn_profile.frames);
        double ms=1e-6/nvn_profile.frames;
        printf("NVN_PROFILE frame=%u count=%u elapsed-ms=%.3f replay-ms=%.3f present-ms=%.3f readback-ms=%.3f scope=wall-time-per-frame\n",nvn_present.frames,nvn_profile.frames,
            (now-nvn_profile.start)*ms,nvn_profile.replay_ns*ms,nvn_profile.present_ns*ms,nvn_profile.readback_ns*ms);
        printf("NVN_PROFILE_DETAIL frame=%u counter-ms=%.3f finish-ms=%.3f receipt-ms=%.3f swap-ms=%.3f elided-waits=%"PRIu64" real-final-waits=%"PRIu64"\n",
            nvn_present.frames,nvn_profile.counter_ns*ms,nvn_profile.finish_ns*ms,nvn_profile.receipt_ns*ms,nvn_profile.swap_ns*ms,
            nvn_gl_completion.skipped_waits,nvn_gl_completion.real_waits);
        printf("T2_COMMAND_RECORDER frame=%u locks=%"PRIu64" contended=%"PRIu64" recording-threads=%u scope=cumulative\n",
            nvn_present.frames,nvn_command_recorder_stats.locks,nvn_command_recorder_stats.contended,nvn_command_recorder_threads());
        printf("NVN_PRESENT_READER_PROFILE frame=%u waits=%"PRIu64" wait-ms=%.3f scope=cumulative-context-handoff\n",
            nvn_present.frames,nvn_egl_fences.reader_waits,nvn_egl_fences.reader_wait_ns*1e-6);
        nvn_phase_report(nvn_present.frames,nvn_profile.frames);
        zero_gpu_cost_report(nvn_present.frames);
        printf("NVN_PROFILE_REPLAY_CPU frame=%u cpu-ms=%.3f cpu-samples=%u submits=%u scope=thread-cpu-per-frame includes-counters=1\n",
            nvn_present.frames,nvn_profile.replay_cpu_samples==nvn_profile.replay_submits?nvn_profile.replay_cpu_ns*ms:-1.0,
            nvn_profile.replay_cpu_samples,nvn_profile.replay_submits);
        for(unsigned ordinal=0;ordinal<NVN_COUNTER_PROFILE_SLOTS;ordinal++) {
            const struct nvn_counter_profile_slot *slot=&nvn_counter_profile.slots[ordinal];if(!slot->count)continue;
            printf("NVN_COUNTER_PROFILE frame=%u ordinal=%u calls=%"PRIu64" wall-ms=%.3f wall-min-ms=%.3f wall-max-ms=%.3f cpu-ms=%.3f cpu-samples=%"PRIu64" draws-mean=%.2f zero-draw-calls=%"PRIu64" since-swap-ms=%.3f swap-samples=%"PRIu64" destination=0x%"PRIx64" target=0x%"PRIx64" target-size=%ux%u fbo=%u state-changes=%"PRIu64"\n",
                nvn_present.frames,ordinal,slot->count,(double)slot->wall_ns/slot->count*1e-6,slot->wall_min*1e-6,slot->wall_max*1e-6,
                slot->cpu_count?(double)slot->cpu_ns/slot->cpu_count*1e-6:-1.0,slot->cpu_count,(double)slot->draws/slot->count,slot->zero_draws,
                slot->swap_count?(double)slot->swap_ns/slot->swap_count*1e-6:-1.0,slot->swap_count,slot->destination,slot->target,
                slot->width,slot->height,slot->framebuffer,slot->state_changes);
        }
        printf("NVN_COUNTER_PROFILE_LIMIT frame=%u lists=%"PRIu64" slots=%u overflow-calls=%"PRIu64" overflow-wall-ms=%.3f overflow-cpu-ms=%.3f overflow-cpu-samples=%"PRIu64" scope=per-command-list-ordinal cpu=thread target=tracked-state since-swap=wall-start-after-last-swap-return\n",
            nvn_present.frames,nvn_counter_profile.lists,NVN_COUNTER_PROFILE_SLOTS,nvn_counter_profile.overflow,
            nvn_counter_profile.overflow_wall_ns*1e-6,nvn_counter_profile.overflow_cpu_ns*1e-6,nvn_counter_profile.overflow_cpu_count);
    }
    if(!nvn_profile.start||nvn_profile.frames>=60)guest_profile_boundary(nvn_present.frames,nvn_profile.frames,nvn_profile.start!=0);
    if(!nvn_profile.start||nvn_profile.frames>=60){nvn_profile=(typeof(nvn_profile)){.start=now};nvn_counter_profile_reset();}
    megaman_frame_cadence_end_present(1,cadence_pixel_proof);
    return 1;
}
