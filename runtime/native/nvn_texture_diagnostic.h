/* Host-only observation of LC2 CPU-backed images. Never uploads or repairs.
 * Snapshot0 is Initialize; snapshots1..11 are first sampling / CPU changes.
 * All guest/GL state and buffers remain owned by their existing paths. */
#ifndef NVN_TEXTURE_DIAGNOSTIC_H
#define NVN_TEXTURE_DIAGNOSTIC_H
#if MEGAMAN_HOST_DIAGNOSTIC
static void nvn_private_dump(const char *,unsigned,const void *,size_t);
enum {NVN_TEXDIAG_TEXTURES=16,NVN_TEXDIAG_SHOTS=12,NVN_TEXDIAG_MAX_BYTES=8*1024*1024};
static struct nvn_texdiag_entry {
    uint64_t id,stride;size_t bytes;uint8_t *initial,*last;unsigned shots,sampled;
} nvn_texdiag_entries[NVN_TEXDIAG_TEXTURES];
static unsigned nvn_texdiag_sequence;
static _Thread_local struct {
    unsigned active,frame,draw,vs,fs,program,uniform_count,pending_count,pending[16];uint64_t program_id;
    struct {unsigned index,stage,binding,offset,size,kind;int location;char name[64];uint8_t bytes[64];} uniforms[64];
} nvn_texdiag_draw;
static unsigned nvn_texdiag_eligible(const struct nvn_texture *t) {
    return t&&t->pool&&(t->b.format==0x39||t->b.format==0x97||t->b.format==0x9a)&&
        t->b.target==1&&t->b.depth==1&&t->b.levels==1&&(t->b.flags&0x50)&&
        t->b.width&&t->b.height&&t->b.width<=1024&&t->b.height<=1024&&
        !t->b.packaged_layout&&!t->b.packaged&&!t->host_half&&!t->packaged_lazy;
}
static struct nvn_texdiag_entry *nvn_texdiag_find(uint64_t id) {
    for(unsigned i=0;i<NVN_TEXDIAG_TEXTURES;i++)if(nvn_texdiag_entries[i].id==id)return &nvn_texdiag_entries[i];
    return NULL;
}
static unsigned nvn_texdiag_changed_bytes(const uint8_t *a,const uint8_t *b,size_t bytes) {
    unsigned changed=0;for(size_t i=0;i<bytes;i++)changed+=a[i]!=b[i];return changed;
}
static void nvn_texdiag_gpu(const struct nvn_texture *t,uint8_t *out) {
    if(glGetError()!=GL_NO_ERROR)stop("NVN_TEXDIAG_PREEXISTING_GL_ERROR",nvn_caller,t->id,21);
    GLint previous,pack;GLuint framebuffer=0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&previous);glGetIntegerv(GL_PACK_ALIGNMENT,&pack);
    glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t->name,0);
    GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if(status==GL_FRAMEBUFFER_COMPLETE){glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,t->b.width,t->b.height,GL_RGBA,GL_UNSIGNED_BYTE,out);}
    GLenum error=glGetError();
    glPixelStorei(GL_PACK_ALIGNMENT,pack);glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)previous);glDeleteFramebuffers(1,&framebuffer);
    if(status!=GL_FRAMEBUFFER_COMPLETE || error!=GL_NO_ERROR)
        stop("NVN_TEXDIAG_GPU_READ",nvn_caller,((uint64_t)status<<32)|error,21);
}
static unsigned nvn_texdiag_capture(struct nvn_texture *t,struct nvn_texdiag_entry *e,
                                   const uint8_t *raw,const uint8_t *rgba,const char *phase,
                                   uint64_t handle,unsigned unit) {
    if(e->shots>=NVN_TEXDIAG_SHOTS)stop("NVN_TEXDIAG_SHOT_BOUND",nvn_caller,e->shots,24);
    size_t rgba_bytes=(size_t)t->b.width*t->b.height*4;
    uint8_t *gpu=malloc(rgba_bytes);if(!gpu)stop("NVN_TEXDIAG_MEMORY",nvn_caller,rgba_bytes,21);
    nvn_texdiag_gpu(t,gpu);
    unsigned sequence=++nvn_texdiag_sequence;if(!sequence)stop("NVN_TEXDIAG_SEQUENCE",nvn_caller,0,24);
    const uint8_t *current=nvn_pointer(t->pool->address+t->b.offset,e->bytes,PROT_READ);
    unsigned stable=!memcmp(current,raw,e->bytes),different=nvn_texdiag_changed_bytes(gpu,rgba,rgba_bytes);
    unsigned from_initial=nvn_texdiag_changed_bytes(raw,e->initial,e->bytes);
    char metadata[2048];
    int length=snprintf(metadata,sizeof(metadata),
        "{\"scope\":\"host-texture-observation-no-upload\",\"phase\":\"%s\",\"sequence\":%u,\"shot\":%u,"
        "\"frame\":%u,\"draw\":%u,\"shader_vs\":%u,\"shader_fs\":%u,\"program\":%u,\"program_id\":%"PRIu64","
        "\"texture_object\":%"PRIu64",\"texture_id\":%"PRIu64",\"gl_name\":%u,\"handle\":%"PRIu64",\"unit\":%u,"
        "\"format\":%u,\"flags\":%u,\"width\":%u,\"height\":%u,\"stride\":%"PRIu64",\"raw_bytes\":%zu,\"rgba_bytes\":%zu,"
        "\"pool_object\":%"PRIu64",\"pool_address\":%"PRIu64",\"pool_size\":%"PRIu64",\"pool_offset\":%"PRIu64","
        "\"pool_flags\":%u,\"pool_generation\":%"PRIu64",\"cpu_stable_during_capture\":%s,"
        "\"cpu_bytes_changed_since_initialize\":%u,\"gpu_vs_decoded_different_bytes\":%u}\n",
        phase,sequence,e->shots,nvn_texdiag_draw.active?nvn_texdiag_draw.frame:0,nvn_texdiag_draw.active?nvn_texdiag_draw.draw:0,
        nvn_texdiag_draw.active?nvn_texdiag_draw.vs:0,nvn_texdiag_draw.active?nvn_texdiag_draw.fs:0,
        nvn_texdiag_draw.active?nvn_texdiag_draw.program:0,nvn_texdiag_draw.active?nvn_texdiag_draw.program_id:0,
        t->object,t->id,t->name,handle,unit,t->b.format,t->b.flags,t->b.width,t->b.height,e->stride,e->bytes,rgba_bytes,
        t->pool->object,t->pool->address,t->pool->size,t->b.offset,t->pool->flags,t->pool->generation,
        stable?"true":"false",from_initial,different);
    if(length<0 || (size_t)length>=sizeof(metadata))stop("NVN_TEXDIAG_METADATA",nvn_caller,sequence,24);
    nvn_private_dump("texdiag-raw",sequence,raw,e->bytes);
    nvn_private_dump("texdiag-decoded",sequence,rgba,rgba_bytes);
    nvn_private_dump("texdiag-gpu",sequence,gpu,rgba_bytes);
    nvn_private_dump("texdiag-json",sequence,metadata,(size_t)length);
    printf("NVN_HOST_TEXTURE_OBSERVATION sequence=%u phase=%s texture-id=%"PRIu64" format=0x%x size=%ux%u raw-changed=%u gpu-decode-different=%u cpu-stable=%u shots=%u/12 no-upload=1\n",
        sequence,phase,t->id,t->b.format,t->b.width,t->b.height,from_initial,different,stable,e->shots+1);
    free(gpu);memcpy(e->last,raw,e->bytes);e->shots++;return sequence;
}
static void nvn_texdiag_initialize_begin(struct nvn_texture *t,const uint8_t *raw,uint64_t stride) {
    if(!nvn_texdiag_eligible(t))return;
    if(nvn_texdiag_find(t->id))stop("NVN_TEXDIAG_DUPLICATE_INITIALIZE",nvn_caller,t->id,24);
    uint64_t bytes=stride*t->b.height;
    if(stride<(uint64_t)t->b.width*nvn_texture_format_bytes(t->b.format)||
       bytes>NVN_TEXDIAG_MAX_BYTES||bytes>t->size||t->b.offset>t->pool->size||bytes>t->pool->size-t->b.offset)
        stop("NVN_TEXDIAG_RANGE",nvn_caller,bytes,24);
    struct nvn_texdiag_entry *e=NULL;
    for(unsigned i=0;i<NVN_TEXDIAG_TEXTURES;i++)if(!nvn_texdiag_entries[i].id){e=&nvn_texdiag_entries[i];break;}
    if(!e){printf("NVN_HOST_TEXTURE_OBSERVATION_SKIP capacity=16 texture-id=%"PRIu64"\n",t->id);return;}
    *e=(struct nvn_texdiag_entry){.id=t->id,.stride=stride,.bytes=(size_t)bytes};
    e->initial=malloc(e->bytes);e->last=malloc(e->bytes);
    if(!e->initial||!e->last)stop("NVN_TEXDIAG_MEMORY",nvn_caller,bytes,21);
    memcpy(e->initial,raw,e->bytes);memcpy(e->last,raw,e->bytes);
}
static void nvn_texdiag_initialize_end(struct nvn_texture *t,unsigned level,const uint8_t *rgba) {
    if(level)return;
    struct nvn_texdiag_entry *e=nvn_texdiag_find(t->id);if(!e)return;
    (void)nvn_texdiag_capture(t,e,e->initial,rgba,"initialize",0,0);
}
static void nvn_texdiag_finalize(struct nvn_texture *t) {
    struct nvn_texdiag_entry *e=nvn_texdiag_find(t->id);if(!e)return;
    free(e->initial);free(e->last);memset(e,0,sizeof(*e));
}
static void nvn_texdiag_draw_begin(unsigned frame,unsigned draw,unsigned vs,unsigned fs,unsigned program,uint64_t id) {
    memset(&nvn_texdiag_draw,0,sizeof(nvn_texdiag_draw));nvn_texdiag_draw.active=1;
    nvn_texdiag_draw.frame=frame;nvn_texdiag_draw.draw=draw;nvn_texdiag_draw.vs=vs;nvn_texdiag_draw.fs=fs;
    nvn_texdiag_draw.program=program;nvn_texdiag_draw.program_id=id;
}
static void nvn_texdiag_uniform(unsigned index,unsigned stage,unsigned binding,unsigned offset,unsigned kind,
                               int location,const char *name,const void *bytes,unsigned size) {
    if(!nvn_texdiag_draw.active)return;
    if(nvn_texdiag_draw.uniform_count>=64||size>64||strlen(name)>=64)stop("NVN_TEXDIAG_UNIFORM_BOUND",nvn_caller,size,24);
    typeof(nvn_texdiag_draw.uniforms[0]) *u=&nvn_texdiag_draw.uniforms[nvn_texdiag_draw.uniform_count++];
    u->index=index;u->stage=stage;u->binding=binding;u->offset=offset;u->kind=kind;u->location=location;u->size=size;
    strcpy(u->name,name);memcpy(u->bytes,bytes,size);
}
static void nvn_texdiag_sample(struct nvn_texture *t,uint64_t handle,unsigned unit,unsigned cpu_view) {
    if(cpu_view||!nvn_texdiag_draw.active||!nvn_texdiag_eligible(t))return;
    struct nvn_texdiag_entry *e=nvn_texdiag_find(t->id);if(!e||e->shots>=NVN_TEXDIAG_SHOTS)return;
    const uint8_t *current=nvn_pointer(t->pool->address+t->b.offset,e->bytes,PROT_READ);
    if(e->sampled&&!memcmp(current,e->last,e->bytes))return;
    size_t rgba_bytes=(size_t)t->b.width*t->b.height*4;
    uint8_t *raw=malloc(e->bytes),*rgba=malloc(rgba_bytes);
    if(!raw||!rgba)stop("NVN_TEXDIAG_MEMORY",nvn_caller,e->bytes+rgba_bytes,21);
    memcpy(raw,current,e->bytes);
    for(unsigned y=0;y<t->b.height;y++)if(!nvn_texture_decode_rgba(t->b.format,t->b.swizzle,
        rgba+(size_t)y*t->b.width*4,raw+y*e->stride,t->b.width))stop("NVN_TEXDIAG_DECODE",nvn_caller,t->b.format,24);
    unsigned sequence=nvn_texdiag_capture(t,e,raw,rgba,e->sampled?"cpu-change":"first-sampler",handle,unit);
    if(nvn_texdiag_draw.pending_count>=16)stop("NVN_TEXDIAG_PENDING_BOUND",nvn_caller,sequence,24);
    nvn_texdiag_draw.pending[nvn_texdiag_draw.pending_count++]=sequence;e->sampled=1;free(rgba);free(raw);
}
static void nvn_texdiag_draw_end(void) {
    for(unsigned p=0;p<nvn_texdiag_draw.pending_count;p++){
        unsigned sequence=nvn_texdiag_draw.pending[p];
        nvn_private_dump("texdiag-uniforms",sequence,nvn_texdiag_draw.uniforms,
            nvn_texdiag_draw.uniform_count*sizeof(nvn_texdiag_draw.uniforms[0]));
        for(unsigned i=0;i<nvn_texdiag_draw.uniform_count;i++){
            const typeof(nvn_texdiag_draw.uniforms[0]) *u=&nvn_texdiag_draw.uniforms[i];
            printf("NVN_HOST_TEXTURE_UNIFORM sequence=%u index=%u stage=%u binding=%u offset=%u kind=%u location=%d name=%s bytes=%u hex=",
                sequence,u->index,u->stage,u->binding,u->offset,u->kind,u->location,u->name,u->size);
            for(unsigned j=0;j<u->size;j++)printf("%02x",u->bytes[j]);
            putchar('\n');
        }
    }
    nvn_texdiag_draw.active=0;
}
#else
#define nvn_texdiag_initialize_begin(...) ((void)0)
#define nvn_texdiag_initialize_end(...) ((void)0)
#define nvn_texdiag_finalize(...) ((void)0)
#define nvn_texdiag_draw_begin(...) ((void)0)
#define nvn_texdiag_uniform(...) ((void)0)
#define nvn_texdiag_sample(...) ((void)0)
#define nvn_texdiag_draw_end(...) ((void)0)
#endif
#endif
