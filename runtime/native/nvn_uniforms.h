/* Guest ranges and bytes are validated/read on every draw. A location owns
 * exactly one value, including reflection/alpha/sampler aliases. The opt-in
 * retains values across acquires by actual program/context/lifetime identity;
 * present only writes its own program. No GL state query or wait is added. */
enum nvn_uniform_kind {NVN_UNIFORM_FLOAT,NVN_UNIFORM_INT,NVN_UNIFORM_VECTOR,NVN_UNIFORM_MATRIX,NVN_UNIFORM_VECTOR2};
#define NVN_UNIFORM_CACHE_SIZE 22U /* 16 uniforms, four sampler sizes, two alpha values */
struct nvn_uniform_values {
    uint64_t serial,program_id;GLuint program;
    EGLContext context;
    unsigned count,next;
    struct {GLint location;unsigned kind,size;uint8_t value[64];} entries[NVN_UNIFORM_CACHE_SIZE];
};
static struct nvn_uniform_values nvn_uniform_cache;
#define NVN_UNIFORM_PROGRAM_SLOTS 64U
static struct nvn_uniform_values nvn_uniform_program_values[NVN_UNIFORM_PROGRAM_SLOTS];
static struct nvn_uniform_values *nvn_uniform_active=&nvn_uniform_cache;
static unsigned nvn_uniform_program_enabled;
static pthread_once_t nvn_uniform_program_once=PTHREAD_ONCE_INIT;
static void nvn_uniform_program_configure(void) {
    const char *option=getenv("COTM_UNIFORM_PROGRAM_CACHE");
    if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("NVN_UNIFORM_PROGRAM_OPTION",nvn_caller,0,24);
    nvn_uniform_program_enabled=option&&!strcmp(option,"1");
}
static struct {uint64_t sent[5],skipped[5],inactive,guest_reads,guest_bytes,compared_bytes,epochs,program_changes,evictions,program_evictions,acquire_survived;unsigned after_frame;} nvn_uniform_profile;
static void nvn_uniform_begin(const struct nvn_program *p,GLuint program) {
    pthread_once(&nvn_uniform_program_once,nvn_uniform_program_configure);
    if(nvn_replay.last_frame>=nvn_uniform_profile.after_frame && nvn_replay.last_frame-nvn_uniform_profile.after_frame>=60) {
        printf("NVN_UNIFORM_CACHE_PROFILE after-frame=%u frames=%u sent=%"PRIu64"/%"PRIu64"/%"PRIu64"/%"PRIu64"/%"PRIu64
               " skipped=%"PRIu64"/%"PRIu64"/%"PRIu64"/%"PRIu64"/%"PRIu64" inactive=%"PRIu64" guest-reads=%"PRIu64
               " guest-bytes=%"PRIu64" compared-bytes=%"PRIu64" epochs=%"PRIu64" program-changes=%"PRIu64" evictions=%"PRIu64
               " program-evictions=%"PRIu64" acquire-survived=%"PRIu64
               " kinds=float/int/vec4/mat4/vec2 scope=since-last-report cache=%s\n",
               nvn_replay.last_frame,nvn_replay.last_frame-nvn_uniform_profile.after_frame,
               nvn_uniform_profile.sent[0],nvn_uniform_profile.sent[1],nvn_uniform_profile.sent[2],nvn_uniform_profile.sent[3],nvn_uniform_profile.sent[4],
               nvn_uniform_profile.skipped[0],nvn_uniform_profile.skipped[1],nvn_uniform_profile.skipped[2],nvn_uniform_profile.skipped[3],nvn_uniform_profile.skipped[4],
               nvn_uniform_profile.inactive,nvn_uniform_profile.guest_reads,nvn_uniform_profile.guest_bytes,nvn_uniform_profile.compared_bytes,
               nvn_uniform_profile.epochs,nvn_uniform_profile.program_changes,nvn_uniform_profile.evictions,
               nvn_uniform_profile.program_evictions,nvn_uniform_profile.acquire_survived,
               nvn_uniform_program_enabled?"per-program-lifetime":"one-program-per-acquire");
        nvn_uniform_profile=(typeof(nvn_uniform_profile)){.after_frame=nvn_replay.last_frame};
    }
    struct nvn_uniform_values *cache=nvn_uniform_program_enabled?
        &nvn_uniform_program_values[program&(NVN_UNIFORM_PROGRAM_SLOTS-1)]:&nvn_uniform_cache;
    nvn_uniform_active=cache;
    unsigned epoch=cache->serial!=nvn_gl_completion.use_serial;
    unsigned changed=cache->program_id!=p->id || cache->program!=program;
    unsigned reset=changed || (nvn_uniform_program_enabled?cache->context!=nvn_device.context:epoch);
    if(nvn_uniform_program_enabled && (!p->id || !program || nvn_device.context==EGL_NO_CONTEXT))
        stop("NVN_UNIFORM_PROGRAM_IDENTITY",nvn_caller,p->id,24);
    if(reset) {
        if(nvn_uniform_program_enabled && cache->program)nvn_uniform_profile.program_evictions++;
        cache->program_id=p->id;cache->program=program;cache->context=nvn_device.context;
        cache->count=cache->next=0;
        nvn_uniform_profile.epochs+=epoch;nvn_uniform_profile.program_changes+=changed;
    } else if(epoch)nvn_uniform_profile.acquire_survived++;
    cache->serial=nvn_gl_completion.use_serial;
}
static unsigned momo_uniform_finite_enabled(void) {
    static int configured;static unsigned enabled;
    if(!configured){const char *o=getenv("MOMO_UNIFORM_FINITE");enabled=!o||strcmp(o,"0");configured=1;}
    return enabled;
}
static void nvn_uniform_send(GLint location,unsigned kind,const void *snapshot) {
    if(location<0){nvn_uniform_profile.inactive++;return;}
    if(kind>NVN_UNIFORM_VECTOR2)stop("NVN_UNIFORM_CACHE_KIND",nvn_caller,kind,24);
    unsigned size=kind==NVN_UNIFORM_MATRIX?64:kind==NVN_UNIFORM_VECTOR?16:kind==NVN_UNIFORM_VECTOR2?8:4;
    struct nvn_uniform_values *cache=nvn_uniform_active;
    unsigned slot=0;while(slot<cache->count&&cache->entries[slot].location!=location)slot++;
    if(slot<cache->count && cache->entries[slot].kind==kind && cache->entries[slot].size==size) {
        nvn_uniform_profile.compared_bytes+=size;
        if(!memcmp(cache->entries[slot].value,snapshot,size)){nvn_uniform_profile.skipped[kind]++;return;}
    }
    /* MOMO_UNIFORM_FINITE=1 (diagnostic; the shipping form is momo_uniform_finite below): Unity sends the
     * UI clip rectangle as (-inf,-inf,inf,inf) for "no clipping". GLSL ES 1.00 has no infinity in mediump,
     * and any subtraction of two infinities in the clip test produces NaN, which discards the primitive. */
    float finite[16];
    if(momo_uniform_finite_enabled() && kind!=NVN_UNIFORM_INT) {
        unsigned count=size/4;memcpy(finite,snapshot,size);
        for(unsigned i=0;i<count;i++) {
            if(isinf(finite[i]))finite[i]=finite[i]<0?-1.0e6f:1.0e6f;
            else if(isnan(finite[i]))finite[i]=0.0f;
        }
        snapshot=finite;
    }
    if(kind==NVN_UNIFORM_MATRIX)glUniformMatrix4fv(location,1,GL_FALSE,snapshot);
    else if(kind==NVN_UNIFORM_VECTOR)glUniform4fv(location,1,snapshot);
    else if(kind==NVN_UNIFORM_VECTOR2)glUniform2fv(location,1,snapshot);
    else if(kind==NVN_UNIFORM_FLOAT){GLfloat value;memcpy(&value,snapshot,4);glUniform1f(location,value);}
    else {GLint value;memcpy(&value,snapshot,4);glUniform1i(location,value);}
    nvn_uniform_profile.sent[kind]++;
    if(slot==NVN_UNIFORM_CACHE_SIZE) {
        slot=cache->next;cache->next=(slot+1)%NVN_UNIFORM_CACHE_SIZE;nvn_uniform_profile.evictions++;
    } else if(slot==cache->count)cache->count++;
    cache->entries[slot].location=location;cache->entries[slot].kind=kind;cache->entries[slot].size=size;
    memcpy(cache->entries[slot].value,snapshot,size);
}
