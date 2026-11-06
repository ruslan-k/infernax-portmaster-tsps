/* Per-draw integration of the independently tested VS17/FS16 CPU contract. */
#include "megaman_wave.h"
struct nvn_megaman_wave_draw {
    float constants[28];
    struct megaman_wave_quad quad;
    struct nvn_texture *source;
    unsigned attributes;
};
static void nvn_megaman_wave_begin(struct nvn_program *p,struct nvn_megaman_wave_draw *w) {
    if(!p->wave_contract)return;
    if(p->sources[0]!=17 || p->sources[1]!=16 || nvn_render.target->host_half ||
       nvn_render.target->b.samples>1 || !nvn_render.target->b.height || nvn_render.target->b.height>MEGAMAN_WAVE_FOLD_MAX_HEIGHT)
        stop("NVN_LC2_WAVE_TARGET_CONTRACT",nvn_caller,nvn_render.target->b.height,20);
    struct nvn_range range=nvn_render.uniform[0][0];
    if(range.size<sizeof(w->constants))stop("NVN_LC2_WAVE_UNIFORM_RANGE",nvn_caller,range.size,24);
    memcpy(w->constants,nvn_gpu_pointer(range.address,sizeof(w->constants),PROT_READ),sizeof(w->constants));
    memcpy(w->quad.matrix_rows,w->constants,64);memcpy(w->quad.tex_crop,w->constants+16,16);
    if(nvn_device.window_origin)for(unsigned i=0;i<4;i++)w->quad.matrix_rows[1][i]=-w->quad.matrix_rows[1][i];
    GLint viewport[4];glGetIntegerv(GL_VIEWPORT,viewport);
    for(unsigned i=0;i<4;i++)w->quad.viewport[i]=(float)viewport[i];
}
static void nvn_megaman_wave_upload(struct nvn_program *p,struct nvn_megaman_wave_draw *w,unsigned unit) {
    if(!p->wave_contract)return;
    if(w->attributes!=3 || !w->source || w->source->host_half || unit>=(unsigned)nvn_device.texture_units)
        stop("NVN_LC2_WAVE_DRAW_CONTRACT",nvn_caller,w->attributes,20);
    struct megaman_wave_input input={.target_height=nvn_render.target->b.height,
        .source_width=w->source->b.width,.source_height=w->source->b.height};
    enum megaman_wave_status status=megaman_wave_map_quad_fold(&w->quad,input.target_height,&input.mapping);
    if(status!=MEGAMAN_WAVE_OK)stop("NVN_LC2_WAVE_FOLD_QUAD_MAPPING",nvn_caller,status,20);
    memcpy(input.varying1,w->constants+20,16);memcpy(input.varying2,w->constants+24,16);
    struct megaman_wave_output output;uint8_t pixels[MEGAMAN_WAVE_FOLD_BYTES];
    status=megaman_wave_generate_fold(&input,&output,pixels,sizeof(pixels));
    if(status!=MEGAMAN_WAVE_OK)stop("NVN_LC2_WAVE_GENERATION",nvn_caller,status,20);
    static GLuint texture;static unsigned height;
    glActiveTexture(GL_TEXTURE0+unit);
    if(!texture){glGenTextures(1,&texture);if(!texture)stop("NVN_LC2_WAVE_GL_MEMORY",nvn_caller,0,21);}
    glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    GLint alignment;glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    if(height!=output.texture_height){glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE,MEGAMAN_WAVE_FOLD_WIDTH,output.texture_height,0,GL_LUMINANCE,GL_UNSIGNED_BYTE,pixels);height=output.texture_height;}
    else glTexSubImage2D(GL_TEXTURE_2D,0,0,0,MEGAMAN_WAVE_FOLD_WIDTH,height,GL_LUMINANCE,GL_UNSIGNED_BYTE,pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);
    GLint sampler=(GLint)unit;nvn_uniform_send(p->wave_lookup,NVN_UNIFORM_INT,&sampler);
    GLfloat viewport[2]={w->quad.viewport[1],w->quad.viewport[3]},target_height=(GLfloat)input.target_height;
    nvn_uniform_send(p->wave_viewport,NVN_UNIFORM_VECTOR2,viewport);
    nvn_uniform_send(p->wave_target_height,NVN_UNIFORM_FLOAT,&target_height);
    nvn_uniform_send(p->wave_chroma,NVN_UNIFORM_FLOAT,&output.chroma_offset);
    GLenum error=glGetError();if(error)stop("NVN_LC2_WAVE_GL_UPLOAD",nvn_caller,error,21);
    static unsigned reports;if(reports++<4)printf("NVN_LC2_WAVE_LOOKUP source=%ux%u target-height=%u folded=2x%u integral-quad=1 map=%.9g/%.9g unit=%u\n",
        input.source_width,input.source_height,input.target_height,height,input.mapping.uv_y_at_zero,input.mapping.uv_y_per_pixel,unit);
}
