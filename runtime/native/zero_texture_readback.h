/* NVN CopyTextureToBuffer: real GPU pixels, copied only during ordered replay.
 * SDK6.5 +0x3f91c0 reads the24-byte box fromx3, destinationx4 and flagsw5.
 * This bounded path admits identity views, one2D level, RGBA8/BGRA8/R8. */
static void zero_texture_readback(const struct nvn_record *r) {
    const uint64_t *a=r->args;struct nvn_texture *t=nvn_texture_find(a[0]);
    if(t->id!=a[5])stop("ZERO_READBACK_TEXTURE_STALE",nvn_caller,t->id,24);
    uint32_t box[6];memcpy(box,r->payload,24);
    if(a[4] || t->b.target!=1 || t->b.depth!=1 || t->b.levels!=1 || t->b.samples || !t->name ||
       t->host_scaled || t->host_half || t->zero_backend_deferred || t->packaged_lazy || t->b.packaged_layout ||
       (t->b.format!=0x25 && t->b.format!=0x9a && t->b.format!=1))
        stop("ZERO_READBACK_FORMAT_REQUIRED",nvn_caller,t->b.format,20);
    if(a[1] && !zero_render_target_view_identity(r->payload+24,t->b.levels,t->b.depth,t->b.format,t->b.target))
        stop("ZERO_READBACK_VIEW_REQUIRED",nvn_caller,t->id,20);
    if(box[2] || box[5]!=1 || box[0]>t->b.width || box[3]>t->b.width-box[0] || box[1]>t->b.height || box[4]>t->b.height-box[1])
        stop("ZERO_READBACK_REGION_REQUIRED",nvn_caller,t->id,24);
    if(!box[3] || !box[4])return;
    unsigned bpp=t->b.format==1?1:4;
    uint64_t row=(uint64_t)box[3]*bpp,stride=nvn_replay.row_stride?nvn_replay.row_stride:row;
    if(stride<row || stride>64U*1024U*1024U)stop("ZERO_READBACK_STRIDE",nvn_caller,stride,24);
    uint64_t span=stride*(box[4]-1)+row;
    uint8_t *destination=nvn_gpu_pointer(a[3],span,PROT_WRITE);
    /* No implicit alias can observe a GL-old texture or claim pool readback. */
    for(unsigned i=0;i<4096;i++) {
        const struct nvn_texture *other=&nvn_textures[i];
        if(!other->live || !other->pool)continue;
        uint64_t begin=other->pool->address+other->b.offset;
        if(a[3]<begin+other->size && begin<a[3]+span)
            stop("ZERO_READBACK_DESTINATION_TEXTURE_ALIAS",nvn_caller,other->id,20);
    }
    zero_texture_refresh(t);nvn_normal_require_materialized(t);nvn_mfc_require(t,MFC_COLOR,0);
    size_t bytes=(size_t)box[3]*box[4]*4;uint8_t *pixels=malloc(bytes);
    if(!pixels)stop("ZERO_READBACK_MEMORY",nvn_caller,bytes,21);
    GLint old_fbo,pack;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&old_fbo);glGetIntegerv(GL_PACK_ALIGNMENT,&pack);
    GLuint fbo=0;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t->name,0);
    if(!fbo || glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)stop("ZERO_READBACK_FRAMEBUFFER",nvn_caller,t->id,21);
    glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(box[0],box[1],box[3],box[4],GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    GLenum error=glGetError();glPixelStorei(GL_PACK_ALIGNMENT,pack);glBindFramebuffer(GL_FRAMEBUFFER,old_fbo);glDeleteFramebuffers(1,&fbo);
    if(!error)error=glGetError();
    if(error)stop("ZERO_READBACK_GL",nvn_caller,error,21);
    for(unsigned y=0;y<box[4];y++) {
        const uint8_t *source=pixels+(size_t)y*box[3]*4;uint8_t *target=destination+y*stride;
        if(t->b.format==0x25)memcpy(target,source,row);
        else for(unsigned x=0;x<box[3];x++,source+=4,target+=bpp) {
            if(bpp==1)*target=source[0];
            else {target[0]=source[2];target[1]=source[1];target[2]=source[0];target[3]=source[3];}
        }
    }
    free(pixels);__atomic_thread_fence(__ATOMIC_SEQ_CST);
    static unsigned count;if(count++<8)printf("ZERO_TEXTURE_READBACK id=%"PRIu64" format=0x%x box=%u/%u/%u/%u stride=%"PRIu64" bytes=%"PRIu64" completion=real-glReadPixels destination=native-buffer\n",
        t->id,t->b.format,box[0],box[1],box[3],box[4],stride,span);
}
