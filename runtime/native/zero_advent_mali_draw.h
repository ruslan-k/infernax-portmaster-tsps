/* Include after nvn_render_resources/descriptor and before draw replay. Call
 * after the ordinary uniform/sampler setup and index validation, before any
 * draw. Every check observes the current draw, never a first-frame approval. */
/* ZERO_ADVENT_MALI_RELAX=1 (13/09, fps-first per Felipe): a draw that does not
 * satisfy the Advent precision contract proceeds with the ordinary mediump
 * program instead of stopping; each reason is logged once. */
static unsigned zero_advent_relax(void) {
    static int configured;static unsigned enabled;
    if(!configured){const char *o=getenv("ZERO_ADVENT_MALI_RELAX");enabled=o&&!strcmp(o,"1");configured=1;
        if(enabled)printf("ZERO_ADVENT_MALI_RELAX enabled=1 contract-failure=draw-proceeds-unpinned\n");}
    return enabled;
}
static void zero_advent_relax_note(const char *reason,uint64_t detail) {
    static const char *seen[32];static unsigned count;
    for(unsigned i=0;i<count;i++)if(seen[i]==reason)return;
    if(count<32)seen[count++]=reason;
    printf("ZERO_ADVENT_MALI_RELAXED reason=%s detail=0x%"PRIx64"\n",reason,detail);
}
#define zadv_fail(reason,detail,code) do{if(zero_advent_relax()){zero_advent_relax_note(reason,(uint64_t)(detail));return;}stop(reason,nvn_caller,detail,code);}while(0)
static void zero_advent_mali_draw_require(const struct nvn_program *p,uint64_t base,
                                        uint64_t maximum,uint64_t count,const uint16_t *indices) {
#if ZERO_ZX_SHADER_PACKAGES
    unsigned source=p->sources[1];
    if(!p->zero_mediump || !zero_advent_mali_pin(source))return;
    if(!zero_advent_mali_admitted(source) || !zero_advent_mali_vertex_admitted(p->sources[0]))
        zadv_fail("ZERO_ADVENT_PRECISION_IDENTITY",source,20);
    float threshold,opacity,rect[8];
    struct nvn_range a=nvn_render.uniform[1][1],b=nvn_render.uniform[1][2],c=nvn_render.uniform[1][3];
    if(a.size<4 || b.size<4 || c.size<64)zadv_fail("ZERO_ADVENT_PRECISION_UNIFORM_RANGE",source,24);
    memcpy(&threshold,nvn_gpu_pointer(a.address,4,PROT_READ),4);
    memcpy(&opacity,nvn_gpu_pointer(b.address,4,PROT_READ),4);
    memcpy(rect,nvn_gpu_pointer(c.address+32,32,PROT_READ),32);
    if(!zero_advent_mali_constants(threshold,opacity,rect,nvn_render.target->b.width,nvn_render.target->b.height)) {
        printf("ZERO_ADVENT_PRECISION_CONSTANTS source=%u threshold=%g opacity=%g target=%ux%u\n",source,threshold,opacity,
               nvn_render.target->b.width,nvn_render.target->b.height);
        zadv_fail("ZERO_ADVENT_PRECISION_DOMAIN_REQUIRED",source,20);
    }
    /* attr1 is the complete source color in the pinned vertex programs. The
     * ordinary float32 path is exact here; conversion/scaled integer formats
     * need their own proof rather than interpreting bytes as floats. */
    uint32_t attr=nvn_render.attrib[1];unsigned stream=attr&31,format=(attr>>21)&1023,offset=(attr>>7)&16383;
    if(nvn_render.attrib_count<2 || !(attr&64) || !p->attribute_used[1] || stream>=nvn_render.stream_count || stream>=16 ||
       nvn_render.streams[stream][1])zadv_fail("ZERO_ADVENT_PRECISION_VERTEX_LAYOUT",source,20);
    struct nvn_vertex_format vf=nvn_vertex_format_decode(format);
    if(vf.type!=GL_FLOAT || vf.components!=4 || vf.normalized || vf.bytes!=16)
        zadv_fail("ZERO_ADVENT_PRECISION_VERTEX_FORMAT",format,20);
    unsigned stride=nvn_render.streams[stream][0];struct nvn_range range=nvn_render.vertex[stream];
    if(!stride || base>UINT64_MAX/stride || maximum>(UINT64_MAX-16)/stride)
        zadv_fail("ZERO_ADVENT_PRECISION_VERTEX_RANGE",source,24);
    uint64_t start=base*stride+offset,extent=maximum*stride+16;
    if(start<offset || start>range.size || extent>range.size-start)
        zadv_fail("ZERO_ADVENT_PRECISION_VERTEX_RANGE",source,24);
    const uint8_t *vertices=nvn_gpu_pointer(range.address+start,extent,PROT_READ);
    for(uint64_t k=0;k<count;k++) {
        uint64_t index=indices?indices[k]:k;float color[4];
        if(index>maximum)zadv_fail("ZERO_ADVENT_PRECISION_VERTEX_INDEX",index,24);
        memcpy(color,vertices+index*stride,16);
        if(!zero_advent_mali_color(color))zadv_fail("ZERO_ADVENT_PRECISION_VERTEX_COLOR",source,20);
    }
    for(unsigned slot=0;slot<(source==93?2u:1u);slot++) {
        if(!(nvn_render.texture_known[1][slot/64]&(1ULL<<(slot%64))))zadv_fail("ZERO_ADVENT_PRECISION_TEXTURE_UNDEFINED",slot,24);
        uint64_t handle=nvn_render.texture_handles[1][slot];
        struct nvn_descriptor *td=nvn_render_descriptor(nvn_replay.texture_pool,1,(uint32_t)handle),
                              *sd=nvn_render_descriptor(nvn_replay.sampler_pool,2,handle>>32);
        struct nvn_texture *t=NULL;struct nvn_sampler *s=NULL;nvn_render_resources(td,sd,&t,&s);
        if(!t || !s || !t->name || t->b.target!=1 || t->b.depth!=1 || t->b.levels!=1 || t->b.samples ||
           (t->b.format!=0x25 && t->b.format!=0x9a) || t->host_scaled || t->host_half ||
           s->b.mag || (s->b.min&1) || s->b.compare || s->b.reduction || s->b.anisotropy!=1 ||
           t->host_upload_type || t->zero_cpu_dirty)
            stop("ZERO_ADVENT_PRECISION_NEAREST_RGBA8_REQUIRED",nvn_caller,handle,20);
        for(unsigned component=0;component<4;component++)if(t->b.swizzle[component]!=component+2)
            zadv_fail("ZERO_ADVENT_PRECISION_RGBA_SWIZZLE",t->id,20);
        if(source==93 && slot==1 && (t->b.width!=16 || t->b.height!=16))
            zadv_fail("ZERO_ADVENT_PRECISION_PALETTE_SIZE",t->id,20);
        if(!slot) {
            uint32_t position=nvn_render.attrib[0];unsigned ps=position&31,pf=(position>>21)&1023,po=(position>>7)&16383;
            if(!(position&64) || !p->attribute_used[0] || ps>=nvn_render.stream_count || ps>=16 || nvn_render.streams[ps][1])
                zadv_fail("ZERO_ADVENT_PRECISION_TEXEL_LAYOUT",source,20);
            struct nvn_vertex_format pvf=nvn_vertex_format_decode(pf);
            if(pvf.type!=GL_FLOAT || pvf.components!=4 || pvf.normalized || pvf.bytes!=16)
                zadv_fail("ZERO_ADVENT_PRECISION_TEXEL_FORMAT",pf,20);
            unsigned ps_stride=nvn_render.streams[ps][0];struct nvn_range pr=nvn_render.vertex[ps];
            if(!ps_stride || base>UINT64_MAX/ps_stride || maximum>(UINT64_MAX-16)/ps_stride)
                zadv_fail("ZERO_ADVENT_PRECISION_TEXEL_RANGE",source,24);
            uint64_t begin=base*ps_stride+po,needed=maximum*ps_stride+16;
            if(begin<po || begin>pr.size || needed>pr.size-begin)zadv_fail("ZERO_ADVENT_PRECISION_TEXEL_RANGE",source,24);
            const uint8_t *positions=nvn_gpu_pointer(pr.address+begin,needed,PROT_READ);float first_offset[2]={0};
            for(uint64_t k=0;k<count;k++) {
                uint64_t index=indices?indices[k]:k;float pos[4],texel_offset[2];memcpy(pos,positions+index*ps_stride,16);
                if(!zero_advent_mali_texel_offset(pos,nvn_render.target->b.width,nvn_render.target->b.height,t->b.width,t->b.height,texel_offset) ||
                   (k && (texel_offset[0]!=first_offset[0] || texel_offset[1]!=first_offset[1])))
                    zadv_fail("ZERO_ADVENT_PRECISION_TEXEL_GRID_REQUIRED",source,20);
                memcpy(first_offset,texel_offset,sizeof(first_offset));
            }
        }
        if(source!=93 || slot)continue;
        if(t->composition_target_seen || t->draws || t->packaged_lazy || t->b.packaged_layout || t->b.packaged ||
           !t->pool || !(t->pool->live || t->pool->retired) || !t->b.width || !t->b.height ||
           t->b.width>1024 || t->b.height>1024)
            zadv_fail("ZERO_ADVENT_PRECISION_PALETTE_CPU_DOMAIN",t->id,20);
        uint64_t row=(uint64_t)t->b.width*4,stride=t->b.flags&0x50?t->b.stride:row,span=stride*t->b.height;
        if(stride<row || t->levels[0]>t->size || span>t->size-t->levels[0])
            zadv_fail("ZERO_ADVENT_PRECISION_PALETTE_RANGE",t->id,24);
        const uint8_t *pixels=nvn_pointer(t->pool->address+t->b.offset+t->levels[0],span,PROT_READ);
        uint8_t decoded[4096];
        for(unsigned y=0;y<t->b.height;y++) {
            if(!nvn_texture_decode_rgba(t->b.format,t->b.swizzle,decoded,pixels+y*stride,t->b.width) ||
               !zero_advent_mali_nibbles(decoded,t->b.width))
                zadv_fail("ZERO_ADVENT_PRECISION_PALETTE_NIBBLE_REQUIRED",t->id,20);
        }
    }
    static unsigned reported;if(reported++<8)printf("ZERO_ADVENT_PRECISION_DRAW source=%u native-domain=nearest-rgba8 threshold=1/255 opacity=1 vertex-alpha=1 palette-nibbles=%u\n",source,source==93);
#else
    (void)p;(void)base;(void)maximum;(void)count;(void)indices;
#endif
}
