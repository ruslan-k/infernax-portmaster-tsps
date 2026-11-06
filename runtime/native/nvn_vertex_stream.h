/* B10: replace each already validated client attribute range with a fresh
 * streaming-buffer data store. No guest bytes survive as a cache source.
 * BufferData replaces the store while GL retains prior draws' old storage;
 * no CPU wait, BufferSubData overwrite or element-buffer binding is added. */
static struct {
    unsigned enabled,after_frame;EGLContext context;GLuint buffers[16];
    uint64_t uploads,bytes,created;
} nvn_vertex_stream;
static pthread_once_t nvn_vertex_stream_once=PTHREAD_ONCE_INIT;
static void nvn_vertex_stream_configure(void) {
    const char *option=getenv("COTM_STREAM_VERTEX_BUFFERS");
    if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("NVN_VERTEX_STREAM_OPTION",nvn_caller,0,24);
    nvn_vertex_stream.enabled=option&&!strcmp(option,"1");
    if(nvn_vertex_stream.enabled)printf("NVN_VERTEX_STREAM_EXPERIMENT enabled=1 stores=per-attribute data=fresh-every-draw usage=STREAM_DRAW indices=client waits=none\n");
}
static unsigned nvn_vertex_stream_pointer(unsigned index,GLint size,GLenum type,GLboolean normalized,GLsizei stride,const void *data,uint64_t bytes) {
    if(pthread_once(&nvn_vertex_stream_once,nvn_vertex_stream_configure))stop("NVN_VERTEX_STREAM_ONCE",nvn_caller,0,21);
    if(!nvn_vertex_stream.enabled)return 0;
    if(index>=16 || !data || !bytes || bytes>PTRDIFF_MAX)stop("NVN_VERTEX_STREAM_RANGE",nvn_caller,bytes,24);
    if(nvn_device.context==EGL_NO_CONTEXT ||
       (nvn_vertex_stream.context!=EGL_NO_CONTEXT && nvn_vertex_stream.context!=nvn_device.context))
        stop("NVN_VERTEX_STREAM_CONTEXT",nvn_caller,0,24);
    nvn_vertex_stream.context=nvn_device.context;
    GLuint *buffer=&nvn_vertex_stream.buffers[index];
    if(!*buffer){glGenBuffers(1,buffer);if(!*buffer)stop("NVN_VERTEX_STREAM_MEMORY",nvn_caller,index,21);nvn_vertex_stream.created++;}
    glBindBuffer(GL_ARRAY_BUFFER,*buffer);nvn_render_state.client_buffers_known=0;
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)bytes,data,GL_STREAM_DRAW);
    nvn_render_vertex_pointer_buffer(index,size,type,normalized,stride,NULL,*buffer);
    nvn_vertex_stream.uploads++;nvn_vertex_stream.bytes+=bytes;return 1;
}
static void nvn_vertex_stream_report(unsigned frame) {
    if(!nvn_vertex_stream.enabled || frame<nvn_vertex_stream.after_frame || frame-nvn_vertex_stream.after_frame<60)return;
    printf("NVN_VERTEX_STREAM_PROFILE after-frame=%u frames=%u uploads=%"PRIu64" bytes=%"PRIu64" created=%"PRIu64
           " scope=since-last-report data-cache=none stores=per-attribute index-buffer=0\n",frame,frame-nvn_vertex_stream.after_frame,
           nvn_vertex_stream.uploads,nvn_vertex_stream.bytes,nvn_vertex_stream.created);
    nvn_vertex_stream.after_frame=frame;nvn_vertex_stream.uploads=nvn_vertex_stream.bytes=nvn_vertex_stream.created=0;
}

/* B10b (opt-in COTM_STREAM_VBO): each distinct vertex stream range used in a
 * submit is copied ONCE into its own rotating buffer object with BufferData
 * (fresh store, no wait on in-flight draws) and every attribute of every draw
 * points into that copy, so the driver never copies client arrays per draw.
 * Ranges are stable during a synchronous replay; the range table is reset
 * at every submit. A range table overflow falls back to client arrays. */
#define NVN_STREAM_VBO_BUFFERS 64U
#define NVN_STREAM_VBO_RANGES 64U
static struct {
    int configured;unsigned enabled;EGLContext context;GLuint buffers[NVN_STREAM_VBO_BUFFERS];unsigned next;
    unsigned count;struct {uint64_t start,bytes;GLuint buffer;} ranges[NVN_STREAM_VBO_RANGES];
    uint64_t uploads,bytes,hits,overflows,submits;unsigned after_frame;
} nvn_stream_vbo;
static unsigned nvn_stream_vbo_enabled(void) {
    if(!nvn_stream_vbo.configured) {
        const char *option=getenv("COTM_STREAM_VBO");
        if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("NVN_STREAM_VBO_OPTION",nvn_caller,0,24);
        nvn_stream_vbo.enabled=option&&!strcmp(option,"1");nvn_stream_vbo.configured=1;
        if(nvn_stream_vbo.enabled)printf("NVN_STREAM_VBO_EXPERIMENT enabled=1 store=one-buffer-per-stream-range rotating=%u ranges=%u upload=BufferData-STREAM_DRAW\n",NVN_STREAM_VBO_BUFFERS,NVN_STREAM_VBO_RANGES);
    }
    return nvn_stream_vbo.enabled;
}
static void nvn_stream_vbo_begin_submit(void) {
    if(!nvn_stream_vbo_enabled())return;
    if(nvn_device.context==EGL_NO_CONTEXT || (nvn_stream_vbo.context!=EGL_NO_CONTEXT && nvn_stream_vbo.context!=nvn_device.context))
        stop("NVN_STREAM_VBO_CONTEXT",nvn_caller,0,24);
    nvn_stream_vbo.context=nvn_device.context;
    if(!nvn_stream_vbo.buffers[0]){glGenBuffers(NVN_STREAM_VBO_BUFFERS,nvn_stream_vbo.buffers);if(!nvn_stream_vbo.buffers[0])stop("NVN_STREAM_VBO_MEMORY",nvn_caller,0,21);}
    nvn_stream_vbo.count=0;nvn_stream_vbo.submits++;
}
static unsigned nvn_stream_vbo_pointer(unsigned index,GLint size,GLenum type,GLboolean normalized,GLsizei stride,
                                       uint64_t stream_start,uint64_t stream_bytes,unsigned attribute_offset) {
    if(!nvn_stream_vbo_enabled()||!nvn_stream_vbo.buffers[0])return 0;
    if(index>=16||!stream_bytes)stop("NVN_STREAM_VBO_RANGE",nvn_caller,stream_bytes,24);
    GLuint buffer=0;uint64_t offset=0;
    for(unsigned i=0;i<nvn_stream_vbo.count;i++) {
        const typeof(nvn_stream_vbo.ranges[0]) *r=&nvn_stream_vbo.ranges[i];
        if(stream_start>=r->start && stream_start-r->start<=r->bytes && stream_bytes<=r->bytes-(stream_start-r->start)) {
            buffer=r->buffer;offset=stream_start-r->start;nvn_stream_vbo.hits++;break;
        }
    }
    if(!buffer) {
        if(nvn_stream_vbo.count==NVN_STREAM_VBO_RANGES || stream_bytes>PTRDIFF_MAX) {
            nvn_stream_vbo.overflows++;
            glBindBuffer(GL_ARRAY_BUFFER,0);nvn_render_state.client_buffers_known=1;return 0;
        }
        const void *source=nvn_gpu_pointer(stream_start,stream_bytes,PROT_READ);
        buffer=nvn_stream_vbo.buffers[nvn_stream_vbo.next];nvn_stream_vbo.next=(nvn_stream_vbo.next+1)%NVN_STREAM_VBO_BUFFERS;
        glBindBuffer(GL_ARRAY_BUFFER,buffer);nvn_render_state.client_buffers_known=0;
        glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)stream_bytes,source,GL_STREAM_DRAW);
        nvn_stream_vbo.ranges[nvn_stream_vbo.count++]=(typeof(nvn_stream_vbo.ranges[0])){stream_start,stream_bytes,buffer};
        nvn_stream_vbo.uploads++;nvn_stream_vbo.bytes+=stream_bytes;
    } else {glBindBuffer(GL_ARRAY_BUFFER,buffer);nvn_render_state.client_buffers_known=0;}
    nvn_render_vertex_pointer_buffer(index,size,type,normalized,stride,(const void *)(uintptr_t)(offset+attribute_offset),buffer);
    return 1;
}
static void nvn_stream_vbo_report(unsigned frame) {
    if(!nvn_stream_vbo.enabled || frame<nvn_stream_vbo.after_frame || frame-nvn_stream_vbo.after_frame<60)return;
    printf("NVN_STREAM_VBO_PROFILE after-frame=%u frames=%u submits=%"PRIu64" uploads=%"PRIu64" bytes=%"PRIu64" hits=%"PRIu64" overflows=%"PRIu64" scope=since-last-report\n",
        frame,frame-nvn_stream_vbo.after_frame,nvn_stream_vbo.submits,nvn_stream_vbo.uploads,nvn_stream_vbo.bytes,nvn_stream_vbo.hits,nvn_stream_vbo.overflows);
    nvn_stream_vbo.after_frame=frame;nvn_stream_vbo.submits=nvn_stream_vbo.uploads=nvn_stream_vbo.bytes=nvn_stream_vbo.hits=nvn_stream_vbo.overflows=0;
}

/* Stable-range VBO cache (opt-in COTM_STABLE_VBO): a vertex stream range that
 * reappears with identical bytes on consecutive rendered submits (tile grids)
 * is uploaded once with BufferData into its own persistent buffer and reused
 * until its guest bytes change (compared against a private shadow copy on
 * every rendered submit). Changing ranges (sprites) stay on client arrays. */
#define NVN_STABLE_VBO_ENTRIES 32U
#define NVN_STABLE_VBO_MIN_STABLE 2U
static struct {
    int configured;unsigned enabled;EGLContext context;uint64_t submit;
    struct {uint64_t start,bytes,last_submit,checked_submit;uint8_t *shadow;GLuint buffer;unsigned stable,uploaded,live;} e[NVN_STABLE_VBO_ENTRIES];
    uint64_t compares,compare_bytes,uploads,upload_bytes,hits,changes,evictions;unsigned after_frame;
} nvn_stable_vbo;
static unsigned nvn_stable_vbo_enabled(void) {
    if(!nvn_stable_vbo.configured) {
        const char *option=getenv("COTM_STABLE_VBO");
        nvn_stable_vbo.enabled=option&&!strcmp(option,"1");nvn_stable_vbo.configured=1;
        if(nvn_stable_vbo.enabled)printf("NVN_STABLE_VBO_EXPERIMENT enabled=1 entries=%u min-stable=%u compare=shadow-memcmp-per-rendered-submit upload=BufferData-on-change\n",NVN_STABLE_VBO_ENTRIES,NVN_STABLE_VBO_MIN_STABLE);
    }
    return nvn_stable_vbo.enabled;
}
static void nvn_stable_vbo_begin_submit(void){if(nvn_stable_vbo_enabled())nvn_stable_vbo.submit++;}
static unsigned nvn_stable_vbo_pointer(unsigned index,GLint size,GLenum type,GLboolean normalized,GLsizei stride,
                                       uint64_t start,uint64_t bytes,unsigned attribute_offset) {
    if(!nvn_stable_vbo_enabled()||!bytes||bytes>(8U<<20))return 0;
    if(nvn_device.context==EGL_NO_CONTEXT || (nvn_stable_vbo.context!=EGL_NO_CONTEXT && nvn_stable_vbo.context!=nvn_device.context))
        stop("NVN_STABLE_VBO_CONTEXT",nvn_caller,0,24);
    nvn_stable_vbo.context=nvn_device.context;
    typeof(nvn_stable_vbo.e[0]) *entry=NULL,*victim=NULL;
    for(unsigned i=0;i<NVN_STABLE_VBO_ENTRIES;i++) {
        typeof(nvn_stable_vbo.e[0]) *c=&nvn_stable_vbo.e[i];
        if(c->live&&c->start==start&&c->bytes==bytes){entry=c;break;}
        if(!victim||!c->live||(victim->live&&c->last_submit<victim->last_submit))victim=c;
    }
    const uint8_t *guest=nvn_gpu_pointer(start,bytes,PROT_READ);
    if(!entry) {
        entry=victim;
        if(entry->live){nvn_stable_vbo.evictions++;free(entry->shadow);entry->shadow=NULL;}
        entry->shadow=malloc(bytes);if(!entry->shadow)stop("NVN_STABLE_VBO_MEMORY",nvn_caller,bytes,21);
        memcpy(entry->shadow,guest,bytes);
        entry->start=start;entry->bytes=bytes;entry->stable=0;entry->uploaded=0;entry->live=1;
        entry->last_submit=entry->checked_submit=nvn_stable_vbo.submit;
        nvn_stable_vbo.compare_bytes+=bytes;return 0;
    }
    entry->last_submit=nvn_stable_vbo.submit;
    if(entry->checked_submit!=nvn_stable_vbo.submit) {
        entry->checked_submit=nvn_stable_vbo.submit;nvn_stable_vbo.compares++;nvn_stable_vbo.compare_bytes+=bytes;
        if(memcmp(entry->shadow,guest,bytes)){memcpy(entry->shadow,guest,bytes);entry->stable=0;entry->uploaded=0;nvn_stable_vbo.changes++;}
        else if(entry->stable<UINT_MAX)entry->stable++;
    }
    if(entry->stable<NVN_STABLE_VBO_MIN_STABLE)return 0;
    if(!entry->uploaded) {
        if(!entry->buffer){glGenBuffers(1,&entry->buffer);if(!entry->buffer)stop("NVN_STABLE_VBO_MEMORY",nvn_caller,0,21);}
        glBindBuffer(GL_ARRAY_BUFFER,entry->buffer);nvn_render_state.client_buffers_known=0;
        glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)bytes,entry->shadow,GL_STATIC_DRAW);
        entry->uploaded=1;nvn_stable_vbo.uploads++;nvn_stable_vbo.upload_bytes+=bytes;
    } else {glBindBuffer(GL_ARRAY_BUFFER,entry->buffer);nvn_render_state.client_buffers_known=0;nvn_stable_vbo.hits++;}
    nvn_render_vertex_pointer_buffer(index,size,type,normalized,stride,(const void *)(uintptr_t)attribute_offset,entry->buffer);
    return 1;
}
static void nvn_stable_vbo_report(unsigned frame) {
    if(!nvn_stable_vbo.enabled || frame<nvn_stable_vbo.after_frame || frame-nvn_stable_vbo.after_frame<60)return;
    printf("NVN_STABLE_VBO_PROFILE after-frame=%u frames=%u compares=%"PRIu64" compare-bytes=%"PRIu64" uploads=%"PRIu64" upload-bytes=%"PRIu64" hits=%"PRIu64" changes=%"PRIu64" evictions=%"PRIu64" scope=since-last-report\n",
        frame,frame-nvn_stable_vbo.after_frame,nvn_stable_vbo.compares,nvn_stable_vbo.compare_bytes,nvn_stable_vbo.uploads,nvn_stable_vbo.upload_bytes,nvn_stable_vbo.hits,nvn_stable_vbo.changes,nvn_stable_vbo.evictions);
    nvn_stable_vbo.after_frame=frame;nvn_stable_vbo.compares=nvn_stable_vbo.compare_bytes=nvn_stable_vbo.uploads=nvn_stable_vbo.upload_bytes=nvn_stable_vbo.hits=nvn_stable_vbo.changes=nvn_stable_vbo.evictions=0;
}
