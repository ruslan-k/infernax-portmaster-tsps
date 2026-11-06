/* Opt-in measurement only. Missing normal pixels may never escape to a
 * consumer: retain CPU guards until Finalize and GL taint until a real full
 * clear. Base73 keeps validation/source reads/state; the separate B13 opt-in
 * can bypass dead draw preparation only after this full qualification. */
static struct {int configured;unsigned enabled,last_frame;uint64_t emitted,elided,protected,cleared;} nvn_normal;
static unsigned nvn_normal_enabled(void) {
    if(!nvn_normal.configured) {
        const char *option=getenv("COTM_LOW_NORMAL_ELISION");
        if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("NVN_NORMAL_OPTION",nvn_caller,0,24);
        nvn_normal.enabled=option&&!strcmp(option,"1");nvn_normal.configured=1;
        if(nvn_normal.enabled)printf("NVN_NORMAL_EXPERIMENT enabled=1 sources=4/5,9/10 target=512x512 cpu-guard=PROT_NONE consumers=fail-closed\n");
    }
    return nvn_normal.enabled;
}
static void nvn_normal_require_materialized(const struct nvn_texture *t) {
    if(t && t->normal_unmaterialized)stop("NVN_NORMAL_UNMATERIALIZED_CONSUMER",nvn_caller,t->id,24);
}
static void nvn_normal_protect_texture(struct nvn_texture *t) {
    if(t->normal_guarded)return;
    uint64_t base=t->pool->address+t->b.offset;
    pthread_mutex_lock(&kernel_mutex);
    pthread_mutex_lock(&regions_mutex);
    for(unsigned i=0;i<4096;i++) {
        struct nvn_texture *other=&nvn_textures[i];
        if(other!=t && other->live && normal_ranges_overlap(base,t->size,other->pool->address+other->b.offset,other->size))
            stop("NVN_NORMAL_EXISTING_TEXTURE_ALIAS",nvn_caller,other->id,24);
    }
    for(unsigned i=0;i<nvn_buffer_stats.slots;i++) {
        struct nvn_buffer *buffer=&nvn_buffers[i];
        if(buffer->live && normal_ranges_overlap(base,t->size,buffer->pool->address+buffer->offset,buffer->size))
            stop("NVN_NORMAL_EXISTING_BUFFER_ALIAS",nvn_caller,buffer->id,24);
    }
    normal_cpu_protect_locked(base,t->size,t->id,nvn_caller);t->normal_guarded=1;nvn_normal.protected++;
    pthread_mutex_unlock(&regions_mutex);pthread_mutex_unlock(&kernel_mutex);
    printf("NVN_NORMAL_PROTECTED id=%"PRIu64" texture=%u address=0x%"PRIx64" bytes=%"PRIu64" pool-flags=%x\n",t->id,t->name,base,t->size,t->pool->flags);
}
static void nvn_normal_validate_target(const struct nvn_texture *t,unsigned color,unsigned depth,unsigned mask) {
    if(!nvn_low.enabled || nvn_low_color(t)>=0 || !t->live || !t->name || t->depth_buffer || !t->pool || !t->pool->live ||
       (t->pool->flags&7)!=1 || t->b.width!=512 || t->b.height!=512 || t->b.depth!=1 || t->b.target!=1 || t->b.levels!=1 ||
       t->b.samples>1 || (t->b.format!=0x25&&t->b.format!=0x9a) || t->size!=512U*512U*4U ||
       (color&0x100) || (depth&5) || mask!=15)
        stop("NVN_NORMAL_DRAW_CONTRACT",nvn_caller,t->id,24);
}
static unsigned nvn_normal_elide(struct nvn_texture *t,unsigned vertex,unsigned fragment,unsigned color,unsigned depth,unsigned mask) {
    if(!nvn_normal_enabled())return 0;
    unsigned normal=(vertex==4&&fragment==5)||(vertex==9&&fragment==10);
    if(!normal){nvn_normal_require_materialized(t);nvn_normal.emitted++;return 0;}
    nvn_normal_validate_target(t,color,depth,mask);
    nvn_normal_protect_texture(t);t->normal_unmaterialized=1;nvn_normal.elided++;return 1;
}
static void nvn_normal_clear(struct nvn_texture *t,unsigned mask) {
    if(t->normal_unmaterialized && mask==15 && nvn_low.scissor_known && nvn_low.scissor[0]==0 && nvn_low.scissor[1]==0 &&
       nvn_low.scissor[2]==512 && nvn_low.scissor[3]==512) {
        t->normal_unmaterialized=0;nvn_normal.cleared++;
    }
}
static void nvn_normal_report(unsigned frame) {
    if(!nvn_normal.enabled || frame<nvn_normal.last_frame || frame-nvn_normal.last_frame<60)return;
    printf("NVN_NORMAL_PROFILE after-frame=%u frames=%u emitted=%"PRIu64" elided=%"PRIu64" protected=%"PRIu64" full-clears=%"PRIu64" scope=GL-draw-calls\n",
        frame,frame-nvn_normal.last_frame,nvn_normal.emitted,nvn_normal.elided,nvn_normal.protected,nvn_normal.cleared);
    nvn_normal.last_frame=frame;nvn_normal.emitted=nvn_normal.elided=nvn_normal.protected=nvn_normal.cleared=0;
}
