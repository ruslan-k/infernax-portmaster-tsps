/* SDK 6.5 CPU texel writes with explicit level/layer views. Host uploads and
 * the retained guest storage receive the same bytes in native call order. */
static void zero_texture_write(struct nvn_frame *f,unsigned flush) {
    uint64_t *a=f->args;struct nvn_texture *t=nvn_texture_find(a[0]);
    uint8_t view[MEGAMAN_TEXTURE_VIEW_BYTES]={0};
    if(a[1])memcpy(view,nvn_pointer(a[1],MEGAMAN_TEXTURE_VIEW_BYTES,PROT_READ),MEGAMAN_TEXTURE_VIEW_BYTES);
    unsigned level=0,layer=0;uint16_t first,count;
#ifdef MOMO_SHADERS
    /* SDK 18.3.2 view (16 bytes, megaman_texture_view.h): flags at [7], level base/count at [4]/[5],
     * layer base/count as u16 at [0]/[2]. A texel write selects one level of one layer. */
    memcpy(&first,view,2);memcpy(&count,view+2,2);
    /* A texel write may also restate the image target (cube faces upload one layer at a time). */
    /* A cube face is written through a view that retargets the image to 2D and selects one layer
     * (MEDIDO run momo-042: flags 0x23, target byte 1 on a cube). */
    if(view[7]&~0x23U || ((view[7]&0x20U) && view[6]!=t->b.target && !(t->b.target==8 && (view[6]==9 || view[6]==1))))stop("ZERO_TEXEL_VIEW_REQUIRED",nvn_caller,((uint64_t)view[7]<<16)|((uint64_t)view[6]<<8)|t->b.target,20);
    if(view[7]&1){level=view[4];if(view[5]!=1)stop("ZERO_TEXEL_LEVEL_COUNT",nvn_caller,view[5],20);}
    else if(view[4]||view[5])stop("ZERO_TEXEL_VIEW_RESERVED",nvn_caller,1,24);
    if(view[7]&2){layer=first;if(count!=1)stop("ZERO_TEXEL_LAYER_COUNT",nvn_caller,count,20);}
    else if(first||count)stop("ZERO_TEXEL_VIEW_RESERVED",nvn_caller,4,24);
    if(!(view[7]&0x20U) && view[6])stop("ZERO_TEXEL_VIEW_RESERVED",nvn_caller,6,24);
    for(unsigned i=8;i<MEGAMAN_TEXTURE_VIEW_BYTES;i++)if(view[i])stop("ZERO_TEXEL_VIEW_RESERVED",nvn_caller,i,24);
#else
    if(view[0]&~3U || view[3])stop("ZERO_TEXEL_VIEW_REQUIRED",nvn_caller,view[0],20);
    if(view[0]&1){level=view[1];if(view[2]!=1)stop("ZERO_TEXEL_LEVEL_COUNT",nvn_caller,view[2],20);}
    else if(view[1]||view[2])stop("ZERO_TEXEL_VIEW_RESERVED",nvn_caller,1,24);
    memcpy(&first,view+4,2);memcpy(&count,view+6,2);
    if(view[0]&2){layer=first;if(count!=1)stop("ZERO_TEXEL_LAYER_COUNT",nvn_caller,count,20);}
    else if(first||count)stop("ZERO_TEXEL_VIEW_RESERVED",nvn_caller,4,24);
    for(unsigned i=8;i<36;i++)if(view[i])stop("ZERO_TEXEL_VIEW_RESERVED",nvn_caller,i,24);
#endif
    if(level>=t->b.levels || layer>=(t->b.target==8?6U:1U) || (t->b.target!=1&&t->b.target!=8&&t->b.target!=2&&t->b.target!=4) ||
       t->b.packaged_layout || t->packaged_lazy || t->host_scaled || t->host_half || t->depth_buffer ||
       (t->pool->flags&1))
        stop("ZERO_TEXEL_STORAGE_REQUIRED",nvn_caller,t->id,20);
    uint32_t box[6];memcpy(box,nvn_pointer(a[2],sizeof(box),PROT_READ),sizeof(box));
    unsigned width=t->b.width>>level,height=t->b.height>>level;
    if(!width)width=1;
    if(!height)height=1;
    /* A flattened volume/array image (nvn_textures.h) stacks its slices vertically: a write that
     * covers whole slices maps to one contiguous row range (MEDIDO run momo-044). */
    unsigned slices=(t->b.target!=8 && t->b.gl_name>1)?t->b.gl_name:1;   /* cube faces use the layer path */
    if(slices>1) {
        unsigned slice_height=t->b.height/slices;
        if(level || !box[5] || box[2]>slices || box[5]>slices-box[2] || box[1] || box[4]!=slice_height)
            stop("MOMO_VOLUME_TEXEL_REGION",nvn_caller,((uint64_t)box[2]<<32)|box[5],20);
        box[1]=box[2]*slice_height;box[4]=box[4]*box[5];box[2]=0;box[5]=1;
    }
    if(box[2] || box[5]!=1 || !box[3] || !box[4] || box[0]>width || box[3]>width-box[0] || box[1]>height || box[4]>height-box[1])
        stop("ZERO_TEXEL_REGION",nvn_caller,t->id,24);
    unsigned bc=zero_bc_bytes(t->b.format),bpp=nvn_texture_format_bytes(t->b.format),unit=bc?4:1;
    if((!bc&&!bpp) || box[0]%unit || box[1]%unit ||
       (box[3]%unit&&box[0]+box[3]!=width) || (box[4]%unit&&box[1]+box[4]!=height))
        stop("ZERO_TEXEL_BLOCK_REGION",nvn_caller,t->b.format,24);
    uint64_t row=(uint64_t)((box[3]+unit-1)/unit)*(bc?bc:bpp),rows=(box[4]+unit-1)/unit;
    uint64_t stride=t->b.flags&0x50?t->b.stride:(uint64_t)((width+unit-1)/unit)*(bc?bc:bpp);
    uint64_t offset=t->levels[level]+layer*(t->size/6)+(box[1]/unit)*stride+(box[0]/unit)*(bc?bc:bpp);
    struct zero_bc_level mip={0};uint64_t span=stride*(rows-1)+row;
    if(bc) {
        if(!zero_bc_level_layout(t->b.width,t->b.height,bc,level,&mip) || mip.offset!=t->levels[level])
            stop("ZERO_TEXEL_BC_MIP",nvn_caller,t->id,24);
        /* Cube faces keep their own copy of the whole BC mip chain (nvn_textures.h). */
        offset=mip.offset+(t->b.target==8?layer*(t->size/6):0);span=mip.size;
    }
    if(offset>t->size || span>t->size-offset)stop("ZERO_TEXEL_DESTINATION_RANGE",nvn_caller,t->id,24);
    uint8_t *destination=nvn_pointer(t->pool->address+t->b.offset+offset,span,PROT_READ|PROT_WRITE);
    normal_guard_require_clear(t->pool->address+t->b.offset,t->size,nvn_caller);
    if(!flush) {
        const uint8_t *source=nvn_pointer(a[3],row*rows,PROT_READ);
        uint8_t *copy=malloc(row*rows);if(!copy)stop("ZERO_TEXEL_COPY_MEMORY",nvn_caller,row*rows,21);
        memcpy(copy,source,row*rows);
        if(bc) {
            if(!zero_bc_blocks_copy(&mip,destination,span,copy,row*rows,box[0]/4,box[1]/4,(box[3]+3)/4,rows,1))
                stop("ZERO_TEXEL_BC_WRITE",nvn_caller,t->id,24);
        } else for(unsigned y=0;y<rows;y++)memcpy(destination+y*stride,copy+y*row,row);
        free(copy);__atomic_thread_fence(__ATOMIC_SEQ_CST);
        /* Cached pools publish by FlushTexels/FlushMappedRange. Uncached
         * explicit writes are uploaded before their next native sampler. */
        if((t->pool->flags&7)==2)__atomic_store_n(&t->zero_cpu_dirty,1,__ATOMIC_RELEASE);
        printf("ZERO_TEXEL_WRITE id=%"PRIu64" level=%u layer=%u region=%u,%u,%u,%u native-bytes=%"PRIu64" CPU-storage-updated\n",
               t->id,level,layer,box[0],box[1],box[3],box[4],row*rows);return;
    }
    if(t->aw_native_format)stop("AW_FLOAT_TEXEL_WRITE_UNSUPPORTED",nvn_caller,t->aw_native_format,20);
    if(t->zero_backend_deferred) {
        __atomic_thread_fence(__ATOMIC_SEQ_CST);__atomic_store_n(&t->zero_cpu_dirty,1,__ATOMIC_RELEASE);
        printf("ZERO_TEXEL_FLUSH_DEFERRED id=%"PRIu64" level=%u layer=%u exact-native-bytes=retained GPU-use=guarded\n",t->id,level,layer);
        return;
    }
    const uint8_t *source=destination;
    unsigned type=nvn_cpu_image_upload_type(t),pixel_bytes=nvn_texture_upload_bytes(type);
    size_t bytes=(size_t)box[3]*box[4]*pixel_bytes;uint8_t *pixels=malloc(bytes);
    if(!pixels)stop("ZERO_TEXEL_MEMORY",nvn_caller,bytes,21);
    if(bc) {
        zero_texture_bc_pixels(t,level,box,pixels,bytes);
    } else for(unsigned y=0;y<box[4];y++)if(!nvn_texture_decode_upload(t->b.format,t->b.swizzle,type,pixels+(size_t)y*box[3]*pixel_bytes,source+y*stride,box[3]))
        stop("ZERO_TEXEL_DECODE",nvn_caller,t->id,24);
    nvn_gl_acquire();
    normal_guard_require_clear(t->pool->address+t->b.offset,t->size,nvn_caller);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    GLenum binding=t->b.target==8?GL_TEXTURE_CUBE_MAP:GL_TEXTURE_2D;
    glBindTexture(binding,t->name);GLint alignment;glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexSubImage2D(t->b.target==8?GL_TEXTURE_CUBE_MAP_POSITIVE_X+layer:GL_TEXTURE_2D,level,box[0],box[1],box[3],box[4],
                    zero_bc_srgb(t->b.format)?0x8c42U:GL_RGBA,type,pixels);
    free(pixels);glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);
    GLenum error=glGetError();if(error)stop("ZERO_TEXEL_UPLOAD",nvn_caller,error,21);
    nvn_gl_release();
    if(t->b.target==1 && t->b.levels==1 && !box[0] && !box[1] && box[3]==width && box[4]==height)
        __atomic_store_n(&t->zero_cpu_dirty,0,__ATOMIC_RELEASE);
    printf("ZERO_TEXEL_FLUSH id=%"PRIu64" level=%u layer=%u region=%u,%u,%u,%u native-bytes=%"PRIu64" GPU-updated\n",
           t->id,level,layer,box[0],box[1],box[3],box[4],row*rows);
}
