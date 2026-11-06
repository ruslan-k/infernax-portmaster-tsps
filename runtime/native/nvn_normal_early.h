/* B13 opt-in: once a normal target has passed full qualification and owns its
 * CPU guard, its discarded draw has no matrix/index/vertex consumer. Preserve
 * fresh sampler handles and every sampler/target consumer guard. No GL state
 * is touched: subsequent real draws still compare against actual GL values. */
static pthread_once_t nvn_normal_early_once=PTHREAD_ONCE_INIT;
static struct {unsigned enabled,after_frame;uint64_t elided,first_use,zero_count,sampler_reads,dead_uniforms;} nvn_normal_early;
static void nvn_normal_early_configure(void) {
    const char *option=getenv("COTM_NORMAL_EARLY");
    if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("NVN_NORMAL_EARLY_OPTION",nvn_caller,0,24);
    nvn_normal_early.enabled=option&&!strcmp(option,"1");
    if(nvn_normal_early.enabled)printf("NVN_NORMAL_EARLY_EXPERIMENT enabled=1 first-use=complete sampler-consumers=fresh dead-inputs=matrix-index-vertex gl-state=unchanged\n");
}
static void nvn_normal_early_report(unsigned frame) {
    if(!nvn_normal_early.enabled || frame<nvn_normal_early.after_frame || frame-nvn_normal_early.after_frame<60)return;
    printf("NVN_NORMAL_EARLY_PROFILE after-frame=%u elided=%"PRIu64" first-use-fallback=%"PRIu64" zero-count-fallback=%"PRIu64
        " sampler-handle-reads=%"PRIu64" dead-uniform-values=%"PRIu64" scope=cumulative pixels=unmaterialized proof=none\n",
        frame,nvn_normal_early.elided,nvn_normal_early.first_use,nvn_normal_early.zero_count,nvn_normal_early.sampler_reads,nvn_normal_early.dead_uniforms);
    nvn_normal_early.after_frame=frame;
}
static unsigned nvn_normal_early_try(const struct nvn_record *r,const struct nvn_program *p,GLuint program,unsigned diagnose) {
    if(pthread_once(&nvn_normal_early_once,nvn_normal_early_configure))stop("NVN_NORMAL_EARLY_ONCE",nvn_caller,0,21);
    if(!nvn_normal_early.enabled || !nvn_normal_enabled() ||
       !((p->sources[0]==4&&p->sources[1]==5)||(p->sources[0]==9&&p->sources[1]==10)))return 0;
    struct nvn_texture *t=nvn_render.target;
    if(!t->normal_guarded){nvn_normal_early.first_use++;return 0;}
    const uint64_t *a=r->args;uint64_t count=a[2];
    if(!count){nvn_normal_early.zero_count++;return 0;}
    if(a[0]>6)stop("NVN_DRAW_PRIMITIVE_REQUIRED",nvn_caller,a[0],20);
    if(r->op==NI_nvnCommandBufferDrawArrays) {
        if(count>1048576 || a[1]>UINT32_MAX)stop("NVN_DRAW_COUNT",nvn_caller,count,24);
    } else {
        if(r->op!=NI_nvnCommandBufferDrawElements && r->op!=NI_nvnCommandBufferDrawElementsBaseVertex)
            stop("NVN_NORMAL_EARLY_OPCODE",nvn_caller,r->op,24);
        if(a[1]>2 || count>1048576)stop("NVN_INDEX_RANGE",nvn_caller,count,24);
    }
    nvn_normal_validate_target(t,nvn_render.color,nvn_render.depth_state[0],nvn_render.mask);
    uint64_t base=t->pool->address+t->b.offset;
    if(!t->id || normal_guard_overlap(base,1)!=t->id || normal_guard_overlap(base+t->size-1,1)!=t->id)
        stop("NVN_NORMAL_EARLY_GUARD",nvn_caller,t->id,24);
    uint32_t blend=nvn_render.blend[1];
    nvn_blend_factor(blend&127);nvn_blend_factor((blend>>8)&127);nvn_blend_factor((blend>>16)&127);nvn_blend_factor((blend>>24)&127);
    nvn_blend_equation((nvn_render.blend[0]>>24)&7);nvn_blend_equation((nvn_render.blend[0]>>28)&7);
    unsigned function=(nvn_render.depth_state[0]>>4)&15;
    if(function<1||function>8)stop("NVN_DEPTH_FUNC_INVALID",nvn_caller,function,24);
    unsigned units=0;
    for(unsigned i=0;i<p->uniform_count;i++) {
        const struct nvn_uniform *u=&p->uniforms[i];if(u->location<0)continue;
        unsigned bytes=u->kind==1?64:u->kind==2?16:u->kind==3?8:u->kind==7?u->size:0;
        if(!bytes || u->size!=bytes || u->stage>1 || u->binding>=16)stop("NVN_DRAW_UNIFORM_LAYOUT",nvn_caller,u->size,24);
        if(u->kind!=3){nvn_normal_early.dead_uniforms++;continue;}
        struct nvn_range range=nvn_render.uniform[u->stage][u->binding];
        if(u->offset>range.size || u->size>range.size-u->offset || range.address>UINT64_MAX-u->offset)
{printf("AW_UNIFORM_RANGE program=%"PRIu64" name=%s stage=%u binding=%u offset=%u size=%u bound-size=%"PRIu64" path=normal-early\n",p->id,u->name,u->stage,u->binding,u->offset,u->size,range.size);
            for(unsigned sx=0;sx<2;sx++)for(unsigned bx=0;bx<16;bx++)if(nvn_render.uniform[sx][bx].size)printf("AW_UNIFORM_BOUND stage=%u index=%u size=%"PRIu64"\n",sx,bx,nvn_render.uniform[sx][bx].size);
            stop("NVN_DRAW_UNIFORM_RANGE",nvn_caller,u->offset,24);}
        uint64_t handle;memcpy(&handle,nvn_gpu_pointer(range.address+u->offset,8,PROT_READ),8);
        if(units>=(unsigned)nvn_device.texture_units)stop("NVN_DRAW_TEXTURE_UNITS",nvn_caller,units,24);
        nvn_render_sampler_prepare(handle,0);units++;nvn_normal_early.sampler_reads++;
    }
    if(!nvn_normal_elide(t,p->sources[0],p->sources[1],nvn_render.color,nvn_render.depth_state[0],nvn_render.mask))
        stop("NVN_NORMAL_EARLY_ELISION",nvn_caller,t->id,24);
    nvn_normal_early.elided++;nvn_replay.draws++;t->draws++;nvn_render_count_program(p,program);
    nvn_normal_early_report(nvn_replay.last_frame);nvn_normal_report(nvn_replay.last_frame);
    if(diagnose)printf("NVN_NORMAL_EARLY_OMITTED draw=%u target=%"PRIu64" count=%"PRIu64" sampler-handles=%u pixels=unmaterialized proof=none\n",
        nvn_replay.draws,t->id,count,units);
    return 1;
}
