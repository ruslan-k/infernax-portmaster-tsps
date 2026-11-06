/* LC2 optional host composition. RENDER_640 alone retains original GL storage;
 * additive RT_STORAGE_640 qualifies the exact Window/intermediate RT family
 * for compact GL storage. Logical NVN dimensions and pool storage never change. */
#include "megaman_composition.h"
static struct {
    unsigned enabled,count,viewport_known,scissor_known,ready[3];
    struct nvn_texture *colors[3],*depth;
    uint64_t color_ids[3],depth_id;
    GLint viewport[4],scissor[4];
    unsigned lc2_640,depth_count;
    struct nvn_texture *depths[2];uint64_t depth_ids[2];
} nvn_low;
static unsigned nvn_lc2_composition_enabled(void) {
    const char *option=getenv("MEGAMAN_RENDER_640");
    if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("NVN_LC2_COMPOSITION_OPTION",nvn_caller,0,24);
    return option&&!strcmp(option,"1");
}
static unsigned nvn_rt_storage_enabled(void) {
    const char *option=getenv("MEGAMAN_RT_STORAGE_640");
    if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("NVN_LC2_RT_STORAGE_OPTION",nvn_caller,0,24);
    unsigned enabled=option&&!strcmp(option,"1");
    if(enabled&&!nvn_lc2_composition_enabled())stop("NVN_LC2_RT_STORAGE_DEPENDENCY",nvn_caller,0,24);
    return enabled;
}
static unsigned nvn_output_640_enabled(void) {
    const char *option=getenv("MEGAMAN_OUTPUT_640");
    if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("NVN_LC2_OUTPUT_OPTION",nvn_caller,0,24);
    unsigned enabled=option&&!strcmp(option,"1");
    if(enabled&&!nvn_rt_storage_enabled())stop("NVN_LC2_OUTPUT_REQUIRES_RT_STORAGE",nvn_caller,0,24);
    return enabled;
}
static unsigned nvn_rt_storage_layout(const struct nvn_texture *t) {
    return t&&t->pool&&t->pool->flags==0xa1&&t->b.width==1920&&t->b.height==1080&&
        ((t->b.format==0x25&&(t->b.flags==8||t->b.flags==9))||(t->b.format==0x35&&t->b.flags==8))&&
        t->b.target==1&&t->b.depth==1&&t->b.levels==1&&t->b.samples<=1&&!t->b.stride&&
        !t->b.packaged&&!t->b.packaged_layout&&!t->b.gl_name&&!t->host_half&&!t->packaged_lazy&&
        t->b.swizzle[0]==2&&t->b.swizzle[1]==3&&t->b.swizzle[2]==4&&t->b.swizzle[3]==5;
}
static unsigned nvn_rt_storage_candidate(const struct nvn_texture *t) {
    return nvn_rt_storage_enabled()&&nvn_rt_storage_layout(t);
}
static unsigned nvn_rt_storage_width(const struct nvn_texture *t) {
    return t->host_scaled?t->host_width:t->host_half?t->b.width/2:t->b.width;
}
static unsigned nvn_rt_storage_height(const struct nvn_texture *t) {
    return t->host_scaled?t->host_height:t->host_half?t->b.height/2:t->b.height;
}
static void nvn_rt_storage_require(const struct nvn_texture *t) {
    if(!t||!t->host_scaled)return;
    if(zero_render_scale_enabled()){zero_render_scale_require(t);return;}
    if(!t->live||!t->id||!nvn_rt_storage_layout(t)||t->host_width!=640||t->host_height!=480||
       !(t->pool->live||t->pool->retired)||!t->pool->references||
       t->b.offset>t->pool->size||t->size>t->pool->size-t->b.offset)
        stop("NVN_LC2_SCALED_LIFETIME",nvn_caller,t->id,24);
}
static void nvn_rt_storage_target(const struct nvn_texture *color,const struct nvn_texture *depth) {
    if(zero_render_scale_enabled()){zero_render_scale_target(color,depth);return;}
    nvn_rt_storage_require(color);nvn_rt_storage_require(depth);
    if(!color)return;
    if(depth && (color->host_scaled||depth->host_scaled) && (color->host_scaled!=depth->host_scaled||
       nvn_rt_storage_width(color)!=nvn_rt_storage_width(depth)||nvn_rt_storage_height(color)!=nvn_rt_storage_height(depth)))
        stop("NVN_LC2_SCALED_ATTACHMENT_DIMENSIONS",nvn_caller,depth->id,20);
    if(color->host_scaled&&(!color->name||(depth&&(!depth->depth_buffer||depth->composition_other_target))))
        stop("NVN_LC2_SCALED_TARGET_HISTORY",nvn_caller,color->id,20);
}
static void nvn_rt_storage_initial(struct nvn_texture *t,const uint8_t *rgba,size_t bytes,unsigned level) {
    if(zero_render_scale_enabled()){zero_render_scale_initial(t,rgba,bytes,level);return;}
    if(!level && nvn_lc2_composition_enabled() && t->b.width==1920 && t->b.height==1080 && t->b.format==0x25 && t->b.levels==1)
        t->composition_initial_uniform=megaman_composition_uniform_rgba(rgba,bytes);
    if(t->host_scaled && (level || bytes!=1920U*1080U*4U || !t->composition_initial_uniform))
        stop("NVN_LC2_SCALED_INITIAL_NONUNIFORM",nvn_caller,t->id,20);
}
static void nvn_rt_storage_sample(const struct nvn_texture *t,unsigned vs,unsigned fs,unsigned cpu_view,unsigned size_query,unsigned texel_fetch) {
    if(!t->host_scaled)return;
    if(zero_render_scale_enabled()){zero_render_scale_sample(t,cpu_view,size_query,texel_fetch);return;}
    nvn_rt_storage_require(t);
    if(t->b.flags==9)stop("NVN_LOW_GUEST_SAMPLING_REQUIRED",nvn_caller,t->id,20);
    if(cpu_view||size_query||texel_fetch||vs!=32||fs!=29)
        stop("NVN_LC2_SCALED_SAMPLER_CONTRACT",nvn_caller,((uint64_t)vs<<32)|fs,20);
}
static void nvn_rt_storage_copy(const struct nvn_texture *t) {
    if(t->host_scaled)stop("NVN_LC2_SCALED_COPY_REQUIRED",nvn_caller,t->id,20);
}
static int nvn_low_color(const struct nvn_texture *t) {
    if(!nvn_low.enabled || !t)return -1;
    for(unsigned i=0;i<nvn_low.count;i++)if(nvn_low.colors[i]==t) {
        if(!t->live || t->id!=nvn_low.color_ids[i])stop("NVN_LOW_WINDOW_LIFETIME",nvn_caller,t->id,24);
        return (int)i;
    }
    return -1;
}
static unsigned nvn_low_storage_overlap(const struct nvn_texture *t,const struct nvn_pool *pool,uint64_t offset,uint64_t size) {
    return t && t->pool==pool && offset<t->b.offset+t->size && t->b.offset<offset+size;
}
static void nvn_low_alias_diagnostic(const char *phase,const struct nvn_texture *old,
        const struct nvn_pool *pool,uint64_t offset,uint64_t size,
        const struct nvn_texture_builder *builder,uint64_t object) {
    printf("NVN_STORAGE_ALIAS_DETAIL phase=%s caller=0x%"PRIx64" pool=0x%"PRIx64" pool-flags=0x%x old-object=0x%"PRIx64" old-id=%"PRIu64" old-format=0x%x old-flags=0x%x old-logical=%ux%u old-offset=0x%"PRIx64" old-end=0x%"PRIx64" old-bytes=0x%"PRIx64" old-scaled=%u old-actual=%ux%u new-object=0x%"PRIx64" new-format=0x%x new-flags=0x%x new-logical=%ux%u new-offset=0x%"PRIx64" new-end=0x%"PRIx64" new-bytes=0x%"PRIx64"\n",
        phase,nvn_caller,pool?pool->object:0,pool?pool->flags:0,
        old->object,old->id,old->b.format,old->b.flags,old->b.width,old->b.height,
        old->b.offset,old->b.offset+old->size,old->size,old->host_scaled,
        nvn_rt_storage_width(old),nvn_rt_storage_height(old),object,
        builder?builder->format:0,builder?builder->flags:0,
        builder?builder->width:0,builder?builder->height:0,offset,offset+size,size);
}
static void nvn_low_new_storage(const struct nvn_pool *pool,uint64_t offset,uint64_t size,
        const struct nvn_texture_builder *builder,uint64_t object) {
    for(unsigned i=0;i<4096;i++)if(nvn_textures[i].live&&
        nvn_low_storage_overlap(&nvn_textures[i],pool,offset,size)) {
        const struct nvn_texture *old=&nvn_textures[i];
        if(old->host_scaled) {
            if(builder && zero_texture_storage_compatible(builder,size,pool,old))continue;
            nvn_low_alias_diagnostic("reject-new-scaled-alias",old,pool,offset,size,builder,object);
            stop("NVN_LC2_SCALED_STORAGE_ALIAS",nvn_caller,offset,24);
        }
        /* Observe the native offscreen alias family without changing its
         * existing full-size allocation/coherence behavior. This provides
         * both descriptors for future qualification in the same next run. */
        if(zero_render_scale_enabled() && pool && pool->flags==0xa4 && builder &&
           builder->flags==8 && old->b.flags==8 &&
           (builder->format==0x25 || builder->format==0x35) &&
           (old->b.format==0x25 || old->b.format==0x35))
            nvn_low_alias_diagnostic("native-offscreen-unscaled",old,pool,offset,size,builder,object);
    }
    if(!nvn_low.enabled)return;
    for(unsigned i=0;i<nvn_low.count;i++)
        if(nvn_low_storage_overlap(nvn_low.colors[i],pool,offset,size))stop("NVN_LOW_STORAGE_ALIAS",nvn_caller,offset,24);
    if(nvn_low_storage_overlap(nvn_low.depth,pool,offset,size))stop("NVN_LOW_DEPTH_ALIAS",nvn_caller,offset,24);
    for(unsigned i=0;i<nvn_low.depth_count;i++)
        if(nvn_low_storage_overlap(nvn_low.depths[i],pool,offset,size))stop("NVN_LOW_DEPTH_ALIAS",nvn_caller,offset,24);
}
static void nvn_low_check_aliases(const struct nvn_texture *t) {
    for(unsigned i=0;i<4096;i++)if(nvn_textures[i].live && &nvn_textures[i]!=t &&
        nvn_low_storage_overlap(t,nvn_textures[i].pool,nvn_textures[i].b.offset,nvn_textures[i].size)) {
        if(zero_texture_storage_compatible(&t->b,t->size,t->pool,&nvn_textures[i]))continue;
        nvn_low_alias_diagnostic("reject-existing-alias",&nvn_textures[i],t->pool,t->b.offset,t->size,&t->b,t->object);
        stop("NVN_LOW_EXISTING_ALIAS",nvn_caller,nvn_textures[i].id,24);
    }
}
#include "megaman_alias_cache.h"
static void nvn_low_configure(struct nvn_texture *const *colors,unsigned count,unsigned width,unsigned height) {
    if(zero_render_scale_enabled()) {
        /* Storage qualification concerns the guest textures, not the physical
         * SDL/fbdev output. Present scales them to the measured drawable. */
        if(nvn_lc2_composition_enabled() || nvn_rt_storage_enabled() || count!=3 ||
           !width || !height || width>4096 || height>4096)
            stop("ZERO_RENDER_SCALE_WINDOW",nvn_caller,count,24);
        for(unsigned i=0;i<count;i++) {
            struct nvn_texture *t=colors[i];
            if(t)printf("MOMO_WINDOW_COLOR index=%u id=%"PRIu64" size=%ux%u flags=0x%x scaled=%u actual=%ux%u initial-uniform=%u target-seen=%u draws=%u pool-flags=0x%x\n",
                i,t->id,t->b.width,t->b.height,t->b.flags,t->host_scaled,t->host_width,t->host_height,
                t->composition_initial_uniform,t->composition_target_seen,t->draws,t->pool?t->pool->flags:0);
#ifdef MOMO_SHADERS
            /* Unity 2021 builds its three window images with flags 8 (no depth/stencil bit) and draws into
             * them before the window is configured, so Zero's "flags 9 and still uniform" shape never
             * matches here. What the reduced storage needs is only that the image is one of the window
             * colours and that its storage really was scaled (MEDIDO run momo-139). */
            if(!t || !t->host_scaled || t->b.width!=1920 || t->b.height!=1080)
                stop("MOMO_RENDER_SCALE_WINDOW_COLOR",nvn_caller,i,24);
#else
            if(!t || !t->host_scaled || t->b.width!=1920 || t->b.height!=1080 || t->b.flags!=9 ||
               !t->composition_initial_uniform)stop("ZERO_RENDER_SCALE_WINDOW_COLOR",nvn_caller,i,24);
#endif
            zero_render_scale_require(t);nvn_low_check_aliases(t);
        }
        printf("ZERO_RENDER_SCALE_WINDOW colors=3 logical=1920x1080 storage=640x360 output=%ux%u native-crop=preserved\n",width,height);
        return;
    }
    unsigned scaled=nvn_rt_storage_enabled(),output640=nvn_output_640_enabled();
    const char *surface=getenv("COTM_FBDEV_640");
    if(surface&&strcmp(surface,"0")&&strcmp(surface,"1"))stop("NVN_LOW_SURFACE_OPTION",nvn_caller,0,24);
    unsigned low_surface=surface&&!strcmp(surface,"1");
    const char *option=getenv("COTM_LOW_COMPOSITION");
    if(option && strcmp(option,"0") && strcmp(option,"1"))stop("NVN_LOW_OPTION",nvn_caller,0,24);
    if(nvn_lc2_composition_enabled()) {
        if(low_surface || (option&&strcmp(option,"0")) || count!=2 || width!=(output640?640U:1280U) || height!=(output640?480U:720U))
            stop("NVN_LC2_COMPOSITION_SURFACE",nvn_caller,width,24);
        for(unsigned i=0;i<count;i++) {
            struct nvn_texture *t=colors[i];
            if(!t || !t->live || !t->name || !t->pool || !t->pool->live || t->pool->flags!=0xa1 ||
               t->b.width!=1920 || t->b.height!=1080 || t->b.format!=0x25 || t->b.flags!=9 ||
               t->b.levels!=1 || t->b.samples>1 || t->b.target!=1 || t->b.depth!=1 || t->b.stride ||
               t->b.packaged || t->b.packaged_layout || t->b.gl_name ||
               t->host_half || t->packaged_lazy || t->draws || !t->composition_initial_uniform || t->composition_target_seen)
                stop("NVN_LC2_COMPOSITION_COLOR",nvn_caller,i,24);
            nvn_low_check_aliases(t);nvn_low.colors[i]=t;nvn_low.color_ids[i]=t->id;
            if(scaled!=t->host_scaled)stop("NVN_LC2_RT_STORAGE_WINDOW",nvn_caller,t->id,24);
            nvn_rt_storage_require(t);
            /* The actual initial GL upload was a uniform image. Its crop is
             * identical before the first native draw, so no invented clear
             * or dependency on an unobserved ClearColor command is needed. */
            nvn_low.ready[i]=1;
        }
        nvn_low.count=count;nvn_low.enabled=nvn_low.lc2_640=1;
        printf("NVN_LC2_COMPOSITION enabled=1 allocation=1920x1080 raster=640x480 present=%ux%u initial=verified-uniform-upload guest-data=unchanged gl-storage=%s output640=%u osd-scale=%s\n",width,height,scaled?"640x480":"1920x1080",output640,output640?"external-1280x720-validation-pending":"none");
        return;
    }
    /* This LC2 study preserves the game's allocation/raster by default.
     * CotM's implicit half-resolution policy is not an LC2 qualification. */
    if(!option || !strcmp(option,"0")){
        if(low_surface)stop("NVN_LOW_SURFACE_REQUIRES_LOW",nvn_caller,0,24);
        printf("NVN_LOW_COMPOSITION enabled=0 allocation-and-raster=original\n");return;
    }
    if(count<2 || count>3 || width!=(low_surface?640U:1280U) || height!=(low_surface?360U:720U))
        stop("NVN_LOW_WINDOW_DIMENSIONS",nvn_caller,width,24);
    for(unsigned i=0;i<count;i++) {
        struct nvn_texture *t=colors[i];
        if(!t || !t->live || !t->name || t->b.width!=1280 || t->b.height!=720 || t->b.levels!=1 || t->draws)
            stop("NVN_LOW_WINDOW_QUALIFICATION",nvn_caller,i,24);
        nvn_low_check_aliases(t);nvn_low.colors[i]=t;nvn_low.color_ids[i]=t->id;
    }
    nvn_low.count=count;nvn_low.enabled=1;
    printf("NVN_LOW_COMPOSITION enabled=1 colors=%u allocation=1280x720 raster=640x360 present=%ux%u nearest=1 initial-until-clear=full shader-original=1 osd-filter=external\n",count,width,height);
}
static void nvn_low_apply_rect(const struct nvn_texture *t,unsigned viewport) {
    const GLint *logical=viewport?nvn_low.viewport:nvn_low.scissor;GLint actual[4];memcpy(actual,logical,sizeof(actual));
    if(t && t->host_scaled && zero_render_scale_enabled()) {
        if(!zero_render_scale_rect(t,logical,actual))stop("ZERO_RENDER_SCALE_RECT",nvn_caller,t->id,24);
    } else if((t&&t->host_scaled)||(nvn_low_color(t)>=0 && nvn_low.lc2_640)) {
        if(!megaman_composition_rect(logical,actual))stop("NVN_LC2_COMPOSITION_RECT",nvn_caller,(uint32_t)logical[2],24);
    } else if(t && t->host_half && !nvn_low.enabled) {
        for(unsigned i=0;i<4;i++)actual[i]=logical[i]==0x7fffffff?logical[i]:logical[i]/2;
    } else if(nvn_low_color(t)>=0)for(unsigned i=0;i<4;i++) {
        if(actual[i]%2 || (i>=2 && actual[i]<0))stop("NVN_LOW_RECT_ALIGNMENT",nvn_caller,(uint32_t)actual[i],24);
        actual[i]/=2;
    }
    if(viewport)glViewport(actual[0],actual[1],actual[2],actual[3]);
    else {glEnable(GL_SCISSOR_TEST);glScissor(actual[0],actual[1],actual[2],actual[3]);}
}
static void nvn_low_set_rect(const struct nvn_texture *t,unsigned viewport,GLint x,GLint y,GLsizei width,GLsizei height) {
    GLint *logical=viewport?nvn_low.viewport:nvn_low.scissor;
    logical[0]=x;logical[1]=y;logical[2]=width;logical[3]=height;
    if(viewport)nvn_low.viewport_known=1;else nvn_low.scissor_known=1;
    nvn_low_apply_rect(t,viewport);
}
static void nvn_low_target(struct nvn_texture *t,struct nvn_texture *depth) {
    if(zero_render_scale_enabled()) {
        zero_render_scale_target(t,depth);
        /* NVN viewport/scissor state survives a target change. Restore its
         * native rectangle too when leaving compact composition for a
         * 256x192 emulation surface, even before another explicit setter. */
        if(nvn_low.viewport_known)nvn_low_apply_rect(t,1);
        if(nvn_low.scissor_known)nvn_low_apply_rect(t,0);
        return;
    }
    if(t&&t->host_scaled) {
        nvn_rt_storage_target(t,depth);
        if(nvn_low.viewport_known)nvn_low_apply_rect(t,1);
        if(nvn_low.scissor_known)nvn_low_apply_rect(t,0);
        return;
    }
    if(!nvn_low.enabled)return;
    if(nvn_low.lc2_640) {
        if(depth && nvn_low_color(t)>=0) {
            if(!depth->live || !depth->depth_buffer || !depth->pool || !depth->pool->live || depth->pool->flags!=0xa1 ||
               depth->b.width!=1920 || depth->b.height!=1080 || depth->b.format!=0x35 || depth->b.flags!=8 ||
               depth->b.levels!=1 || depth->b.samples>1 || depth->b.target!=1 || depth->b.depth!=1 || depth->host_half || depth->composition_other_target)
                stop("NVN_LC2_COMPOSITION_DEPTH",nvn_caller,depth->id,24);
            unsigned slot=0;while(slot<nvn_low.depth_count&&nvn_low.depths[slot]!=depth)slot++;
            if(slot<nvn_low.depth_count) {
                if(nvn_low.depth_ids[slot]!=depth->id)stop("NVN_LOW_DEPTH_IDENTITY",nvn_caller,depth->id,24);
            } else {
                if(slot==2)stop("NVN_LC2_COMPOSITION_DEPTH_COUNT",nvn_caller,depth->id,24);
                nvn_low_check_aliases(depth);nvn_low.depths[slot]=depth;nvn_low.depth_ids[slot]=depth->id;nvn_low.depth_count++;
            }
        } else if(depth) {
            for(unsigned i=0;i<nvn_low.depth_count;i++)if(nvn_low.depths[i]==depth)
                stop("NVN_LOW_DEPTH_OTHER_TARGET",nvn_caller,depth->id,24);
        }
        if(nvn_low.viewport_known)nvn_low_apply_rect(t,1);
        if(nvn_low.scissor_known)nvn_low_apply_rect(t,0);
        return;
    }
    if(nvn_low_color(t)>=0 && depth) {
        if(depth->b.width!=1280 || depth->b.height!=720 || !depth->depth_buffer)
            stop("NVN_LOW_DEPTH_DIMENSIONS",nvn_caller,depth->id,24);
        if(nvn_low.depth && (nvn_low.depth!=depth || nvn_low.depth_id!=depth->id))
            stop("NVN_LOW_DEPTH_IDENTITY",nvn_caller,depth->id,24);
        if(!nvn_low.depth){nvn_low_check_aliases(depth);nvn_low.depth=depth;nvn_low.depth_id=depth->id;}
    } else if(depth && nvn_low.depth==depth)stop("NVN_LOW_DEPTH_OTHER_TARGET",nvn_caller,depth->id,24);
    if(nvn_low.viewport_known)nvn_low_apply_rect(t,1);
    if(nvn_low.scissor_known)nvn_low_apply_rect(t,0);
}
static void nvn_low_clear(const struct nvn_texture *t,unsigned mask) {
    int color=nvn_low_color(t);
    if(color>=0 && (mask&15)==15 && nvn_low.scissor_known && nvn_low.scissor[0]==0 &&
       nvn_low.scissor[1]==0 && nvn_low.scissor[2]==1280 && nvn_low.scissor[3]==720)nvn_low.ready[color]=1;
}
static void nvn_low_draw(const struct nvn_texture *t) {
    if(t&&t->host_scaled) {
        nvn_rt_storage_require(t);
        if(!nvn_low.viewport_known||!nvn_low.scissor_known)stop("NVN_LC2_SCALED_DRAW_RECT_UNKNOWN",nvn_caller,t->id,24);
    }
    int color=nvn_low_color(t);
    if(color>=0 && (!nvn_low.ready[color] || !nvn_low.viewport_known || !nvn_low.scissor_known))
        stop("NVN_LOW_DRAW_BEFORE_CLEAR",nvn_caller,t->id,24);
}
static GLfloat nvn_low_present_scale(const struct nvn_texture *t) {
    if(t&&t->host_scaled){nvn_rt_storage_require(t);return 1.0f;}
    int color=nvn_low_color(t);
    /* Native startup presents before its first clears. Preserve that initial
     * full image, then crop only after this color's real full logical clear. */
    return color>=0 && nvn_low.ready[color]?(nvn_low.lc2_640?1.0f/3.0f:0.5f):1.0f;
}
static GLfloat nvn_low_present_scale_y(const struct nvn_texture *t) {
    if(t&&t->host_scaled){nvn_rt_storage_require(t);return 1.0f;}
    int color=nvn_low_color(t);
    return color>=0 && nvn_low.ready[color] && nvn_low.lc2_640?4.0f/9.0f:nvn_low_present_scale(t);
}
static unsigned nvn_low_render_width(const struct nvn_texture *t) {return t->host_scaled?t->host_width:nvn_low_color(t)>=0?(nvn_low.lc2_640?640:t->b.width/2):t->b.width;}
static unsigned nvn_low_render_height(const struct nvn_texture *t) {return t->host_scaled?t->host_height:nvn_low_color(t)>=0?(nvn_low.lc2_640?480:t->b.height/2):t->b.height;}
