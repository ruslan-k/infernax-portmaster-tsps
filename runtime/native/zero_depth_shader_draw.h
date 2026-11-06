struct zero_depth_draw {unsigned active;GLfloat old_range[2];};
static struct zero_depth_draw zero_depth_draw_begin(const struct nvn_program *p) {
    struct zero_depth_draw state={0};
    if(!p->zero_depth_bank)return state;
    if(!nvn_render.depth || !(nvn_render.depth_state[0]&1))return state; /* unobserved native depth output */
    if(!(nvn_render.texture_known[1][0]&1ULL))stop("ZERO_DEPTH_SHADER_TEXTURE_UNDEFINED",nvn_caller,p->sources[1],24);
    uint64_t handle=nvn_render.texture_handles[1][0];
    struct nvn_descriptor *td=nvn_render_descriptor(nvn_replay.texture_pool,1,(uint32_t)handle),*sd=nvn_render_descriptor(nvn_replay.sampler_pool,2,handle>>32);
    struct nvn_texture *t=NULL;struct nvn_sampler *s=NULL;nvn_render_resources(td,sd,&t,&s);
    /* D24S8 aliases can share bytes while selecting different sampled
     * components. This qualified shader consumes depth; inspect the native
     * view before replacing its metadata with the shared storage state. */
    if(t && t->b.depth_mode!=0) {
        printf("ZERO_DEPTH_SHADER_VIEW_REJECT source=%u view=%"PRIu64" mode=%u\n",
               p->sources[1],t->id,t->b.depth_mode);
        stop("ZERO_DEPTH_SHADER_VIEW_REQUIRED",nvn_caller,t->id,20);
    }
    if(zero_texture_storage_same(t,nvn_render.depth))
        stop("ZERO_DEPTH_SHADER_STORAGE_FEEDBACK",nvn_caller,t?t->id:0,20);
    t=zero_texture_storage_state(t);
    if(!t || !s || t->b.target!=1 || t->b.levels!=1 || t->b.format!=0x35 || !t->depth_buffer ||
       t->zero_cpu_dirty || !(t->zero_depth_valid&1) || (t->zero_depth_value!=0 && t->zero_depth_value!=1)) {
        printf("ZERO_DEPTH_SHADER_INPUT source=%u texture=%"PRIu64" target=%"PRIu64" valid=%u value=%g\n",
            p->sources[1],t?t->id:0,nvn_render.depth->id,t?t->zero_depth_valid:0,t?t->zero_depth_value:0);
        stop("ZERO_DEPTH_SHADER_NONUNIFORM_REQUIRED",nvn_caller,p->sources[1],20);
    }
    struct nvn_sampler_builder *b=&s->b;
    if(b->compare || b->reduction || b->anisotropy!=1 || b->lod_bias!=0 || b->lod_snap!=0 ||
       !nvn_megaman_sampler_wrap(b->wrap[0],b->min,b->mag) || !nvn_megaman_sampler_wrap(b->wrap[1],b->min,b->mag) ||
       nvn_megaman_polygon_offset_fill(nvn_render.polygon))stop("ZERO_DEPTH_SHADER_SAMPLER_RASTER_REQUIRED",nvn_caller,s->id,20);
    struct nvn_range range=nvn_render.uniform[1][p->zero_depth_bank-1];
    if(range.size<16)stop("ZERO_DEPTH_SHADER_UNIFORM_RANGE",nvn_caller,range.size,24);
    float offset;memcpy(&offset,nvn_gpu_pointer(range.address,16,PROT_READ),4);
    float value=t->zero_depth_value+offset;
    if(!isfinite(offset) || !isfinite(value))stop("ZERO_DEPTH_SHADER_UNIFORM_NONFINITE",nvn_caller,p->sources[1],24);
    glGetFloatv(GL_DEPTH_RANGE,state.old_range);glDepthRangef(value,value);state.active=1;
    static unsigned count;if(count++<8)printf("ZERO_DEPTH_SHADER_DRAW source=%u input=%"PRIu64" target=%"PRIu64" depth=%g offset=%g backend=GLES-constant-depth-range native-geometry=preserved\n",
        p->sources[1],t->id,nvn_render.depth->id,t->zero_depth_value,offset);
    return state;
}
static void zero_depth_draw_end(const struct zero_depth_draw *state) {
    if(state->active)glDepthRangef(state->old_range[0],state->old_range[1]);
}
