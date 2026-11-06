/* Port-local partial-mip transport. All calls hold the producer GL context. */
#ifndef ZERO_MIP_CLAMP_RUNTIME_H
#define ZERO_MIP_CLAMP_RUNTIME_H
static unsigned zero_mip_full_levels(unsigned width,unsigned height) {
    unsigned levels=1,extent=width>height?width:height;
    while(extent>1){extent>>=1;levels++;}return levels;
}
static unsigned zero_mip_partial(const struct nvn_texture *t) {
    return t->b.target==1 && t->b.levels>1 && t->b.levels<zero_mip_full_levels(t->b.width,t->b.height);
}
static unsigned zero_mip_native_limit(void) {
    const char *version=(const char *)glGetString(GL_VERSION);
    unsigned major=0,minor=0;
    /* GLES 3 promotes TEXTURE_MAX_LEVEL to core; those drivers need not
     * advertise the GLES 2 APPLE extension or the shader-LOD workaround. */
    if(version && sscanf(version,"OpenGL ES %u.%u",&major,&minor)==2 && major>=3)return 1;
    const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
    return extensions && strstr(extensions,"GL_APPLE_texture_max_level");
}
static void zero_mip_complete_storage(struct nvn_texture *t,unsigned format,unsigned type) {
    if(!zero_mip_partial(t) || zero_mip_native_limit())return;
    if(t->host_half || t->host_scaled || t->packaged_lazy || !t->name)
        stop("ZERO_MIP_STORAGE_CONFLICT",nvn_caller,t->id,20);
    unsigned full=zero_mip_full_levels(t->b.width,t->b.height),bpp=nvn_texture_upload_bytes(type);
    if(!bpp)stop("ZERO_MIP_STORAGE_FORMAT",nvn_caller,type,20);
    for(unsigned level=t->b.levels;level<full;level++) {
        unsigned width=t->b.width>>level,height=t->b.height>>level;
        if(!width)width=1;
        if(!height)height=1;
        size_t bytes=(size_t)width*height*bpp;void *zero=calloc(1,bytes);
        if(!zero)stop("ZERO_MIP_STORAGE_MEMORY",nvn_caller,bytes,21);
        glTexImage2D(GL_TEXTURE_2D,level,nvn_texture_upload_internal(type,nvn_texture_context_major(),format),width,height,0,format,type,zero);free(zero);
    }
    t->zero_mip_tail=1;
}
static GLuint zero_mip_mask_texture(struct nvn_texture *t,GLint min_filter) {
    unsigned linear=min_filter==GL_NEAREST_MIPMAP_LINEAR || min_filter==GL_LINEAR_MIPMAP_LINEAR;
    if(t->zero_mip_masks[linear])return t->zero_mip_masks[linear];
    if(!t->zero_mip_tail || !zero_mip_partial(t))stop("ZERO_MIP_MASK_STORAGE",nvn_caller,t->id,24);
    GLuint mask;glGenTextures(1,&mask);glBindTexture(GL_TEXTURE_2D,mask);
    if(!mask)stop("ZERO_MIP_MASK_MEMORY",nvn_caller,t->id,21);
    unsigned width=t->b.width,height=t->b.height,full=zero_mip_full_levels(width,height);
    GLint alignment;glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    for(unsigned level=0;level<full;level++) {
        size_t bytes=(size_t)width*height*4;uint8_t *pixels=malloc(bytes);
        if(!pixels)stop("ZERO_MIP_MASK_MEMORY",nvn_caller,bytes,21);
        memset(pixels,level<t->b.levels?0:255,bytes);
        glTexImage2D(GL_TEXTURE_2D,level,GL_RGBA,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);free(pixels);
        if(width>1)width>>=1;
        if(height>1)height>>=1;
    }
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,linear?GL_LINEAR_MIPMAP_LINEAR:GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);
    GLenum error=glGetError();if(error)stop("ZERO_MIP_MASK_GL",nvn_caller,error,21);
    t->zero_mip_masks[linear]=mask;
    printf("ZERO_MIP_CLAMP_STORAGE id=%"PRIu64" native-levels=%u transport-levels=%u mask=RGBA8 filter=%s game-mips=unchanged\n",
           t->id,t->b.levels,full,linear?"linear-lod":"nearest-lod");return mask;
}
#endif
