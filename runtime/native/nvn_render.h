/* Packed state layout measured from this build's original SDK accessors.
 * Resource data is consumed in command order; uniform locations and byte
 * offsets come from the original compiler reflection, not guessed bindings.
 */
static int nvn_upload_seen; /* set by movie upload, consumed by NX_DUMP_ON_UPLOAD */
static unsigned char *nvn_stg; static unsigned nvn_stg_w,nvn_stg_h; static GLuint nvn_stg_tex; static int nvn_stg_pend;
static void nvn_stage(const unsigned char *src,unsigned w,unsigned h,GLuint tex){size_t n=(size_t)w*h*4;
  if(!nvn_stg||(size_t)nvn_stg_w*nvn_stg_h*4<n){free(nvn_stg);nvn_stg=malloc(n);}
  memcpy(nvn_stg,src,n);nvn_stg_w=w;nvn_stg_h=h;nvn_stg_tex=tex;nvn_stg_pend=1;}
static void nvn_flush(void){if(!nvn_stg_pend)return;GLint prev=0,fbo=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&prev);glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fbo);
  if(fbo)glBindFramebuffer(GL_FRAMEBUFFER,0);glBindTexture(GL_TEXTURE_2D,nvn_stg_tex);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
  glTexSubImage2D(GL_TEXTURE_2D,0,0,0,nvn_stg_w,nvn_stg_h,GL_RGBA,GL_UNSIGNED_BYTE,nvn_stg);
  glBindTexture(GL_TEXTURE_2D,(GLuint)prev);if(fbo)glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)fbo);nvn_stg_pend=0;}
struct nvn_range {uint64_t address,size;};
/* The movie path decodes into the SDK's plane textures (R8 luma, RG8 chroma) and samples them
 * with the game's own YUV shader; the decoded picture has to reach those GL objects. */
static void nvd_gl_refresh(struct nvn_texture *t)
{
    static uint64_t y_at=(uint64_t)-1,uv_at=(uint64_t)-1;static uint8_t *px;static size_t cap;
    {static unsigned ent;if((t&&(t->b.format==0x1||t->b.format==0xd))&&ent++<16)printf("NVD_ENTER dec=%d name=%u fmt=0x%x %ux%u tgt=%d\n",(int)nvd_decoded,t?t->name:0,t?t->b.format:0,t?t->b.width:0,t?t->b.height:0,t?(int)t->b.target:-1);}
    if(!nvd_decoded||!t||!t->name||t->b.target!=1)return;
    unsigned w=(unsigned)nvd_w,h=(unsigned)nvd_h;if(!w||!h)return;
    int luma=t->b.format==0x1&&t->b.width==w&&(t->b.height==h||t->b.height==((h+7)&~7u));
    int chroma=t->b.format==0xd&&t->b.width==w/2&&(t->b.height==h/2||t->b.height==((h+7)&~15u));
    /* handheld movie quad: game samples a 1280x720 RGBA8 texture (1:1) or 2/3 of the stream */
    int rgb=t->b.format==0x25&&((t->b.width==1280&&t->b.height==720&&w==1280&&h==720)||((uint64_t)t->b.width*3==(uint64_t)w*2&&(uint64_t)t->b.height*3==(uint64_t)h*2));
    if(rgb){
        static uint64_t rgb_at=(uint64_t)-1;static uint8_t *rp;static size_t rcap;
        static int seen=0;
        unsigned tw=t->b.width,th=t->b.height;
        if(rgb_at==nvd_serial)return;
        int depth=(nvd_pic->format==62||nvd_pic->format==63||nvd_pic->format==64||nvd_pic->format==124||nvd_pic->format==125)?10:8;
        (void)depth;
        if(!seen){seen=1;
            printf("NVD_RGBDBG fmt=%d depth=%d cs=%d range=%d ls=%d/%d/%d d2=%d %ux%u\n",
                nvd_pic->format,depth,(int)nvd_pic->colorspace,(int)nvd_pic->color_range,
                nvd_pic->linesize[0],nvd_pic->linesize[1],nvd_pic->linesize[2],nvd_pic->data[2]!=NULL,w,h);}
        if(rcap<(size_t)tw*th*4){free(rp);rp=malloc((size_t)tw*th*4);rcap=(size_t)tw*th*4;if(!rp)return;}
        for(unsigned y=0;y<th;y++){
            unsigned sy=(unsigned)((uint64_t)y*h/th),cy=sy/2;
            const uint8_t *ys=nvd_pic->data[0]+(size_t)sy*nvd_pic->linesize[0];
            const uint8_t *us=nvd_pic->data[1]+(size_t)cy*nvd_pic->linesize[1];
            const uint8_t *vs=nvd_pic->data[2]?nvd_pic->data[2]+(size_t)cy*nvd_pic->linesize[2]:NULL;
            uint8_t *d=rp+(size_t)y*tw*4;
            for(unsigned x=0;x<tw;x++){
                unsigned sx=(unsigned)((uint64_t)x*w/tw),cx=sx/2;
                int C,D,E;
                if(vs){C=(int)ys[sx]-16;D=(int)us[cx]-128;E=(int)vs[cx]-128;}
                else{C=(int)ys[sx]-16;D=(int)us[cx*2]-128;E=(int)us[cx*2+1]-128;}
                int r=(298*C+409*E+128)>>8,g=(298*C-100*D-208*E+128)>>8,b=(298*C+516*D+128)>>8;
                d[x*4]=(uint8_t)(r<0?0:r>255?255:r);
                d[x*4+1]=(uint8_t)(g<0?0:g>255?255:g);
                d[x*4+2]=(uint8_t)(b<0?0:b>255?255:b);
                d[x*4+3]=255;
            }
        }
        {static unsigned du;if(du<2&&nvd_pic->data[1]&&du++==0){
            unsigned cw=(w+1)/2,ch=(h+1)/2;
            for(int k=1;k<=2;k++){
                const uint8_t *pl=nvd_pic->data[k];
                if(!pl)continue;
                FILE*f=fopen(k==1?"/tmp/nvdU.ppm":"/tmp/nvdV.ppm","wb");
                if(!f)continue;
                fprintf(f,"P5\n%u %u\n255\n",cw,ch);
                for(unsigned y=0;y<ch;y++)fwrite(pl+(size_t)y*nvd_pic->linesize[k],1,cw,f);
                fclose(f);
            }
        }
        }
        nvn_upload_seen=1;
        {const char *sol=getenv("NVD_SOLID");
         if(sol){unsigned v=(unsigned)strtoul(sol,NULL,16);for(size_t i=0;i<(size_t)tw*th;i++){rp[i*4]=(uint8_t)(v>>24);rp[i*4+1]=(uint8_t)(v>>16);rp[i*4+2]=(uint8_t)(v>>8);rp[i*4+3]=255;}}}
        if(getenv("NVD_DEFER"))nvn_stage(rp,tw,th,t->name);else{
        GLint prev=0,fbo=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&prev);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fbo);
        if(fbo)glBindFramebuffer(GL_FRAMEBUFFER,0);
        glBindTexture(GL_TEXTURE_2D,t->name);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,tw,th,GL_RGBA,GL_UNSIGNED_BYTE,rp);
        glBindTexture(GL_TEXTURE_2D,(GLuint)prev);
        if(fbo)glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)fbo);}
        rgb_at=nvd_serial;
        {static unsigned rn;if(rn<2&&rn++==0){char p[64];FILE*f;
            snprintf(p,sizeof p,"/tmp/nvdRGB_%llu.ppm",(unsigned long long)nvd_serial);
            if((f=fopen(p,"wb"))){fprintf(f,"P6\n%u %u\n255\n",tw,th);
                for(unsigned y=0;y<th;y++)for(unsigned x=0;x<tw;x++)fwrite(rp+(size_t)(y*tw+x)*4,1,3,f);
                fclose(f);}}
        }
        printf("NVD_TEXUPLOAD id=%"PRIu64" format=0x%x %ux%u serial=%"PRIu64" rgb=1\n",t->id,t->b.format,tw,th,nvd_serial);
        return;
    }
    if(!luma&&!chroma){static unsigned rm;if(rm++<8)printf("NVD_REFMISS fmt=0x%x %ux%u want=%ux%u\n",t->b.format,t->b.width,t->b.height,w,h);return;}
    uint64_t *at=luma?&y_at:&uv_at;if(*at==nvd_serial)return;
    unsigned pw=luma?w:w/2,ph=luma?h:h/2;
    if(cap<(size_t)pw*ph*4){free(px);px=malloc((size_t)pw*ph*4);cap=(size_t)pw*ph*4;if(!px)return;}
    for(unsigned y=0;y<ph;y++){
        const uint8_t *s=nvd_pic->data[0]+(size_t)y*nvd_pic->linesize[0];
        const uint8_t *u=nvd_pic->data[1]+(size_t)y*nvd_pic->linesize[1];
        const uint8_t *v=nvd_pic->data[2]?nvd_pic->data[2]+(size_t)y*nvd_pic->linesize[2]:u+1;
        uint8_t *d=px+(size_t)y*pw*4;
        for(unsigned x=0;x<pw;x++){
            if(luma){d[x*4]=s[x];d[x*4+1]=0;d[x*4+2]=0;}
            else{d[x*4]=nvd_pic->data[2]?u[x]:u[x*2];d[x*4+1]=nvd_pic->data[2]?v[x]:u[x*2+1];d[x*4+2]=0;}
            d[x*4+3]=255;
        }
    }
    GLint prev=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&prev);
    glBindTexture(GL_TEXTURE_2D,t->name);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,pw,ph,GL_RGBA,GL_UNSIGNED_BYTE,px);
    glBindTexture(GL_TEXTURE_2D,(GLuint)prev);
    *at=nvd_serial;
    if(luma){char p[64];FILE*f;
        snprintf(p,sizeof p,"/tmp/nvdY_%llu.ppm",(unsigned long long)nvd_serial);
        if((f=fopen(p,"wb"))){fprintf(f,"P5\n%u %u\n255\n",pw,ph);
            for(unsigned y=0;y<ph;y++){fwrite(nvd_pic->data[0]+(size_t)y*nvd_pic->linesize[0],1,pw,f);}
            fclose(f);}}
    printf("NVD_TEXUPLOAD id=%"PRIu64" format=0x%x %ux%u serial=%"PRIu64"\n",t->id,t->b.format,pw,ph,nvd_serial);
    if(getenv("NVD_TEXSTAT")){static unsigned char rb[1920*16*4];{GLuint fb=0;glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t->name,0);glReadPixels(0,0,pw,16,GL_RGBA,GL_UNSIGNED_BYTE,rb);glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fb);}
        unsigned long sum=0;unsigned mx=0;for(unsigned i=0;i<pw*16*4;i+=4){sum+=rb[i];if(rb[i]>mx)mx=rb[i];}
        printf("NVD_TEXSTAT id=%"PRIu64" mean=%lu max=%u\n",t->id,sum/(pw*16),mx);}
}
static struct {
    GLuint framebuffer;struct nvn_texture *target,*depth;struct nvn_program *program;
    struct megaman_program_stage program_stages[2];
    uint32_t attrib[16],streams[16][2],attrib_count,stream_count,color,blend[2],depth_state[2],polygon,mask;
    uint32_t multisample[6];float polygon_offset[3],line_width,point_size;
    struct nvn_range vertex[16],uniform[2][16];
    /* Unity 2021 binds separate textures well past unit 32 (the reserved constant bank reaches
     * offset 0x148 = unit 41, MEDIDO run momo-100), so the table follows the hardware's 128. */
    uint64_t texture_handles[2][128];uint64_t texture_known[2][2];
#ifdef INFERNAX_CBUF_SAMPLERS
    uint32_t separate_textures[2][128],separate_samplers[2][128];
    uint64_t separate_texture_known[2][2],separate_sampler_known[2][2];
#endif
    uint16_t *indices;unsigned index_capacity;unsigned pass_skipped,aw_deferred_target;uint64_t pass_skips,pass_skipped_draws,pass_materialized;
    unsigned pending_color_valid,pending_mask,pending_depth_valid;float pending_color[4],pending_depth;
    uint8_t stencil_ref[2],stencil_write[2],stencil_compare[2],stencil_known[2];
    struct nvn_pool *scratch_pool;uint64_t scratch_offset,scratch_size;
    unsigned momo_quads_reported;
} nvn_render={.color=0x08030000,.blend={0x110c0000,0x01020102},.depth_state={0x22,0x11181118},.polygon=0x14,.mask=15,
    .multisample={5,0x88888888,0x88888888,0x88888888,0x88888888,0},.line_width=1,.point_size=1};
static int nvn_ptr_mapped(const void *q,size_t need)
{
    FILE *f;char line[512];unsigned long long a=0,b=0,want,ok=0;
    if(!q)return 0;
    f=fopen("/proc/self/maps","r");
    if(!f)return 0;
    want=(unsigned long long)(uintptr_t)q;
    while(fgets(line,sizeof line,f)){
        a=0;b=0;
        if(sscanf(line,"%llx-%llx",&a,&b)!=2)continue;
        if(want<a||want+need>b)continue;
        if(strchr(line,'r'))ok=1;
        break;
    }
    fclose(f);
    return (int)ok;
}
/* The kind-7 loop in nvn_render_draw_inner already uploads live bytes for
 * every draw. Do not add a second global refresh cache: it outlives programs,
 * overreads short ranges and can destroy the just-installed texture binding. */
static const void *nvn_cb_src[8];static unsigned nvn_cb_len[8];
/* Read-only, bounded contract audit. No texture binding is changed except
 * selecting a unit temporarily, and GL_ACTIVE_TEXTURE is restored. */
static void infernax_cbuf_audit(GLuint program,struct nvn_program *p,const char *point) {
    static unsigned calls;
    if(!nvn_flag_on("NX_CBUF_AUDIT")||calls>=24)return;
    calls++;
    GLint saved=0,current=0,limit=0;
    glGetIntegerv(GL_ACTIVE_TEXTURE,&saved);glGetIntegerv(GL_CURRENT_PROGRAM,&current);
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,&limit);
    for(unsigned i=0;i<p->uniform_count;i++) {
        struct nvn_uniform *u=&p->uniforms[i];
        if((u->kind!=7&&u->kind!=3)||u->location<0)continue;
        GLint unit=-1,bound=-1,sampler=-1;
        glGetUniformiv(program,u->location,&unit);
        if(unit>=0&&unit<limit) {
            glActiveTexture(GL_TEXTURE0+unit);glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);
            glGetIntegerv(GL_SAMPLER_BINDING,&sampler);
        }
        printf("INFERNAX_CBUF_AUDIT point=%s call=%u draw=%u frame=%u pair=%u/%u program=%u current=%d name=%s kind=%u stage=%u slot=%u loc=%d unit=%d texture=%d sampler=%d\n",
               point,calls,nvn_replay.draws,nvn_replay.last_frame+1,p->sources[0],p->sources[1],program,current,u->name,u->kind,u->stage,u->binding,u->location,unit,bound,sampler);
    }
    if(calls<=12)for(unsigned stage=0;stage<2;stage++)for(unsigned slot=0;slot<16;slot++) {
        struct nvn_range range=nvn_render.uniform[stage][slot];
        if(!range.address||!range.size)continue;
        unsigned bytes=range.size<64?(unsigned)range.size:64;
        const void *src=nvn_gpu_pointer(range.address,bytes,PROT_READ);
        if(!src){printf("INFERNAX_CBUF_RANGE_UNMAPPED stage=%u slot=%u\n",stage,slot);continue;}
        float words[16]={0};memcpy(words,src,bytes);
        printf("INFERNAX_CBUF_RANGE point=%s call=%u stage=%u slot=%u address=0x%"PRIx64" size=%"PRIu64" words=",point,calls,stage,slot,range.address,range.size);
        for(unsigned k=0;k<16;k++)printf("%s%.9g",k?",":"",(double)words[k]);
        putchar('\n');
    }
    glActiveTexture((GLenum)saved);
}
/* Bounded, default-off scene contract audit. It never mutates guest data,
 * attributes, program, raster state, or render-target contents. */
static unsigned infernax_scene_audit_before(struct nvn_program *p,GLuint program,
                                           GLenum primitive,GLsizei count,const void *indices) {
    static unsigned last_frame,shots;
    unsigned frame=nvn_replay.last_frame+1;
    if(!nvn_flag_on("NX_SCENE_AUDIT")||!nvn_render.target||p->sources[0]!=2||p->sources[1]!=0||
       nvn_render.target->b.width!=1280||nvn_render.target->b.height!=720||
       (frame!=20&&frame!=100))return 0;
    if(last_frame!=frame){last_frame=frame;shots=0;}
    if(shots++>=32)return 0;
    flockfile(stdout);
    GLint cur=0,linked=0,vao=0,ebo=0,fb=0,db=0,rb=0,attachment=0,atype=0;
    GLint front=0,cull=0,vp[4]={0},sc[4]={0},mask[4]={0};
    glGetIntegerv(GL_CURRENT_PROGRAM,(void*)&cur);glGetProgramiv(program,GL_LINK_STATUS,&linked);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,(void*)&vao);glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,(void*)&ebo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,(void*)&fb);glGetIntegerv(GL_DRAW_BUFFER0,(void*)&db);glGetIntegerv(GL_READ_BUFFER,(void*)&rb);
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&attachment);
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&atype);
    glGetIntegerv(GL_FRONT_FACE,(void*)&front);glGetIntegerv(GL_CULL_FACE_MODE,(void*)&cull);
    glGetIntegerv(GL_VIEWPORT,(void*)vp);glGetIntegerv(GL_SCISSOR_BOX,(void*)sc);glGetIntegerv(GL_COLOR_WRITEMASK,(void*)mask);
    printf("NX_SCENE_STATE frame=%u draw=%u target=%"PRIu64" pair=%u/%u expected_program=%u current_program=%d linked=%d vao=%d ebo=%d fbo=%d status=0x%x attachment=%d attachment_type=0x%x draw_buffer=0x%x read_buffer=0x%x primitive=0x%x count=%d indexed=%u index_type=0x%x indices=%p cull=%d cullmode=0x%x front=0x%x origin=%u depth_mode=%u vp=%d,%d,%d,%d sc=%d,%d,%d,%d mask=%d%d%d%d depth=%d stencil=%d blend=%d\n",
        frame,nvn_replay.draws,nvn_render.target->id,p->sources[0],p->sources[1],program,cur,linked,vao,ebo,fb,
        (unsigned)glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER),attachment,(unsigned)atype,(unsigned)db,(unsigned)rb,
        (unsigned)primitive,(int)count,indices!=NULL,(unsigned)GL_UNSIGNED_SHORT,indices,(int)glIsEnabled(GL_CULL_FACE),
        (unsigned)cull,(unsigned)front,nvn_device.window_origin,nvn_device.depth_mode,
        vp[0],vp[1],vp[2],vp[3],sc[0],sc[1],sc[2],sc[3],mask[0],mask[1],mask[2],mask[3],
        (int)glIsEnabled(GL_DEPTH_TEST),(int)glIsEnabled(GL_STENCIL_TEST),(int)glIsEnabled(GL_BLEND));
    GLint active=0;glGetIntegerv(GL_ACTIVE_TEXTURE,(void*)&active);
    for(unsigned j=0;j<p->uniform_count;j++) {
        struct nvn_uniform *u=&p->uniforms[j];
        if(u->location<0||(u->kind!=7&&u->kind!=3))continue;
        GLint unit=-1,texture=-1,sampler=-1;glGetUniformiv(program,u->location,&unit);
        if(unit>=0){glActiveTexture(GL_TEXTURE0+unit);glGetIntegerv(GL_TEXTURE_BINDING_2D,(void*)&texture);glGetIntegerv(GL_SAMPLER_BINDING,(void*)&sampler);}
        printf("NX_SCENE_BIND frame=%u draw=%u name=%s kind=%u stage=%u binding=%u offset=%u unit=%d texture=%d sampler=%d\n",
               frame,nvn_replay.draws,u->name,u->kind,u->stage,u->binding,u->offset,unit,texture,sampler);
        if(u->kind==7&&u->stage==0&&(u->binding==1||u->binding==2)) {
            struct nvn_range range=nvn_render.uniform[u->stage][u->binding];
            unsigned available=u->offset>=range.size?0:(unsigned)(range.size-u->offset);
            if(available>160)available=160;
            const void *source=available?nvn_gpu_pointer(range.address+u->offset,available,PROT_READ):NULL;
            float v[40]={0};if(source)memcpy(v,source,available);
            printf("NX_SCENE_BANK frame=%u draw=%u name=%s binding=%u bound_bytes=%"PRIu64" copied=%u source_ok=%u values=",
                   frame,nvn_replay.draws,u->name,u->binding,range.size,available,source!=NULL);
            for(unsigned k=0;k<40;k++)printf("%s%.9g",k?",":"",(double)v[k]);
            putchar('\n');
        }
    }
    glActiveTexture((GLenum)active);
    unsigned sample=count>0?(unsigned)count:0;if(sample>12)sample=12;
    unsigned ix[12]={0};unsigned index_ok=!indices||(ebo==0&&nvn_ptr_mapped(indices,(size_t)sample*2));
    printf("NX_SCENE_INDEX frame=%u draw=%u source_ok=%u truncated=%u values=",frame,nvn_replay.draws,index_ok,(unsigned)(count>(GLsizei)sample));
    for(unsigned k=0;k<sample;k++) {
        uint16_t raw=0;if(indices&&index_ok)memcpy(&raw,(const char*)indices+k*2,2);
        ix[k]=indices?raw:k;printf("%s%u",k?",":"",ix[k]);
    }
    putchar('\n');
    for(unsigned a=0;a<2;a++) {
        GLint enabled=0,size=0,type=0,stride=0,buffer=0;void *pointer=NULL;
        glGetVertexAttribiv(a,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&enabled);glGetVertexAttribiv(a,GL_VERTEX_ATTRIB_ARRAY_SIZE,&size);
        glGetVertexAttribiv(a,GL_VERTEX_ATTRIB_ARRAY_TYPE,&type);glGetVertexAttribiv(a,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&stride);
        glGetVertexAttribiv(a,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&buffer);glGetVertexAttribPointerv(a,GL_VERTEX_ATTRIB_ARRAY_POINTER,&pointer);
        printf("NX_SCENE_ATTR frame=%u draw=%u attr=%u enabled=%d size=%d type=0x%x stride=%d buffer=%d pointer=%p\n",
            frame,nvn_replay.draws,a,enabled,size,(unsigned)type,stride,buffer,pointer);
        if(index_ok&&enabled&&!buffer&&pointer&&type==GL_FLOAT&&size>0&&size<=4&&stride>=0) {
            unsigned step=stride?(unsigned)stride:(unsigned)size*4;
            for(unsigned k=0;k<sample;k++) {
                unsigned duplicate=0;for(unsigned l=0;l<k;l++)if(ix[l]==ix[k])duplicate=1;
                if(duplicate)continue;
                const void *source=(const char*)pointer+(size_t)step*ix[k];float v[4]={0,0,0,1};
                unsigned ok=nvn_ptr_mapped(source,(size_t)size*4);if(ok)memcpy(v,source,(size_t)size*4);
                printf("NX_SCENE_VERTEX frame=%u draw=%u attr=%u index=%u source_ok=%u values=%.9g,%.9g,%.9g,%.9g\n",
                       frame,nvn_replay.draws,a,ix[k],ok,(double)v[0],(double)v[1],(double)v[2],(double)v[3]);
            }
        }
    }
    GLenum error=glGetError();printf("NX_SCENE_AUDIT_ERROR frame=%u draw=%u error=0x%x\n",frame,nvn_replay.draws,(unsigned)error);
    funlockfile(stdout);
    return 1;
}
static void infernax_scene_audit_after(void) {
    unsigned w=nvn_render.target->b.width,h=nvn_render.target->b.height;
    size_t pixels=(size_t)w*h;uint8_t *data=malloc(pixels*4);
    if(!data){printf("NX_SCENE_PIXELS allocation_failed=1\n");return;}
    glReadPixels(0,0,(GLsizei)w,(GLsizei)h,GL_RGBA,GL_UNSIGNED_BYTE,data);GLenum error=glGetError();
    size_t nonblack=0;unsigned maxv=0;
    if(!error)for(size_t k=0;k<pixels;k++){unsigned v=data[k*4];if(data[k*4+1]>v)v=data[k*4+1];if(data[k*4+2]>v)v=data[k*4+2];nonblack+=v>8;if(v>maxv)maxv=v;}
    printf("NX_SCENE_PIXELS frame=%u draw=%u target=%"PRIu64" error=0x%x nonblack=%zu max=%u\n",
           nvn_replay.last_frame+1,nvn_replay.draws,nvn_render.target->id,(unsigned)error,nonblack,maxv);
    free(data);
}
#include "nvn_megaman_raster.h"
#include "nvn_render_state.h"
#include "nvn_vertex_stream.h"
#include "nvn_vertex_format.h"
#include "nvn_uniforms.h"
#include "nvn_resource_lookup.h"
#include "nvn_megaman_color_mask.h"
#include "nvn_normal_discard.h"
#include "nvn_depth_constant.h"
#include "zero_depth_copy.h"
#include "zero_gles_barrier.h"
#include "nvn_index_fast.h"
#include "nvn_megaman_sampler_wrap.h"
static unsigned nvn_mfc_slot(const struct nvn_texture *t) {
    uintptr_t address=(uintptr_t)t,first=(uintptr_t)nvn_textures;
    if(!t||address<first||address-first>=sizeof(nvn_textures)||(address-first)%sizeof(*t)||!t->live||!t->id)
        stop("NVN_LC2_CADENCE_TEXTURE_ID",nvn_caller,t?t->id:0,24);
    return (unsigned)(t-nvn_textures);
}
static void nvn_mfc_target(const struct nvn_texture *t) {
    if(!megaman_frame_cadence_enabled()||!t)return;
    nvn_mfc_slot(t);
    /* Only scaled composition targets participate in omitted writes. Native
     * targets, including MM7's 256x224 color/depth pair, keep every draw and
     * clear. A composition allocation cannot lose its scaling flag silently. */
    if(!t->host_scaled&&nvn_rt_storage_candidate(t))stop("NVN_LC2_CADENCE_WRITE_TARGET",nvn_caller,t->id,20);
    nvn_rt_storage_require(t);nvn_mfc_check_aliases_cached(t);
}
static unsigned nvn_mfc_omitting_draw(void) {
    return megaman_frame_cadence_omitting()&&nvn_render.target&&nvn_render.target->host_scaled;
}
static void nvn_mfc_require(const struct nvn_texture *t,unsigned consumed,unsigned overwritten) {
    if(!megaman_frame_cadence_enabled()||!t||!t->host_scaled)return;
    unsigned slot=nvn_mfc_slot(t);
    enum mfc_result status=mfc_require(&megaman_frame_cadence,slot,t->id,consumed,overwritten);
    if(status) {
        printf("NVN_LC2_CADENCE_PENDING_STATE id=%"PRIu64" pending=0x%x consumed=0x%x overwritten=0x%x depth=0x%x stencil=0x%x write=%u/%u compare=%u/%u ref=%u/%u boundary=%"PRIu64" skip=%u\n",
            t->id,megaman_frame_cadence.resources[slot].pending,consumed,overwritten,
            nvn_render.depth_state[0],nvn_render.depth_state[1],nvn_render.stencil_write[0],nvn_render.stencil_write[1],
            nvn_render.stencil_compare[0],nvn_render.stencil_compare[1],nvn_render.stencil_ref[0],nvn_render.stencil_ref[1],
            megaman_frame_cadence.boundaries,megaman_frame_cadence.skip);
        stop("NVN_LC2_CADENCE_PENDING_READ",nvn_caller,t->id,20);
    }
}
static void nvn_mfc_change(const struct nvn_texture *t,unsigned bits,unsigned commit) {
    if(!megaman_frame_cadence_enabled()||!t||!t->host_scaled||!bits)return;
    nvn_mfc_target(t);
    enum mfc_result status=commit?mfc_commit(&megaman_frame_cadence,nvn_mfc_slot(t),t->id,bits):
        mfc_mark(&megaman_frame_cadence,nvn_mfc_slot(t),t->id,bits);
    if(status)stop("NVN_LC2_CADENCE_TEXTURE_STATE",nvn_caller,status,24);
}
static unsigned nvn_mfc_stencil_candidate(unsigned vs,unsigned fs,unsigned mask) {
    if(!nvn_mfc_omitting_draw() || vs!=24 || fs!=23)return 0;
    int window=nvn_low_color(nvn_render.target);
    unsigned rejected=(!nvn_render.target||!nvn_render.depth)?1U:0U;
    if(window<0)rejected|=2U;
    if((nvn_render.target&&!nvn_render.target->host_scaled)||(nvn_render.depth&&!nvn_render.depth->host_scaled))rejected|=4U;
    if(nvn_render.depth&&nvn_render.depth->b.format!=0x35)rejected|=8U;
    if(!mfc_stencil_preserve_state(vs,fs,nvn_render.color,nvn_render.depth_state[0],
        nvn_render.depth_state[1],nvn_render.polygon,mask,nvn_render.stencil_known,nvn_render.stencil_write,
        nvn_render.stencil_compare,nvn_render.stencil_ref,nvn_low.viewport_known,nvn_low.viewport,
        nvn_low.scissor_known,nvn_low.scissor))rejected|=16U;
    if(rejected) {
        static unsigned reported;
        if((nvn_render.depth_state[0]&4U)&&reported<16) {
            printf("NVN_LC2_CADENCE_STENCIL_REJECTED event=%u bits=0x%x window=%d color-id=%"PRIu64" depth-id=%"PRIu64" scaled=%u/%u known=%u/%u depth=0x%x stencil=0x%x effective-mask=%u legacy-depth-count=%u boundary=%"PRIu64" cap=16\n",
                ++reported,rejected,window,nvn_render.target?nvn_render.target->id:0,nvn_render.depth?nvn_render.depth->id:0,
                nvn_render.target?nvn_render.target->host_scaled:0,nvn_render.depth?nvn_render.depth->host_scaled:0,
                nvn_render.stencil_known[0],nvn_render.stencil_known[1],nvn_render.depth_state[0],nvn_render.depth_state[1],
                mask,nvn_low.depth_count,megaman_frame_cadence.boundaries);
        }
        return 0;
    }
    /* RT_STORAGE uses nvn_low_target's early host_scaled branch. The old
     * raster-only depth registry is deliberately unused there. Validate the
     * actual current attachments, as SetRenderTargets does, instead. Draw
     * entry has already run MFC identity/lifetime/alias guards on both. */
    nvn_rt_storage_target(nvn_render.target,nvn_render.depth);return 1;
}
static unsigned nvn_mfc_stencil_reported;
static void nvn_mfc_stencil_mark(const struct nvn_texture *t,unsigned bits,const struct mfc_stencil_origin *o) {
    unsigned observe=megaman_frame_cadence_enabled()&&t&&(bits&MFC_STENCIL)&&nvn_mfc_stencil_reported<16;
    unsigned slot=observe?nvn_mfc_slot(t):0;
    unsigned before=observe&&megaman_frame_cadence.resources[slot].id==t->id?
        megaman_frame_cadence.resources[slot].pending:0;
    nvn_mfc_change(t,bits,0); /* Exactly the existing mark, including all guards. */
    if(!observe)return;
    unsigned sequence=++nvn_mfc_stencil_reported;
    printf("NVN_LC2_CADENCE_STENCIL_MARK event=%u origin=%s id=%"PRIu64" color-id=%"PRIu64" boundary=%"PRIu64" skip=%u bits=0x%x before=0x%x after=0x%x sources=%u/%u depth=0x%x stencil=0x%x write=%u/%u compare=%u/%u ref=%u/%u color=0x%x polygon=0x%x color-mask=0x%x viewport-known=%u viewport=%d/%d/%d/%d scissor-known=%u scissor=%d/%d/%d/%d primitive=%u count=%"PRIu64" base=%"PRIu64" maximum=%"PRIu64" indexed=%u",
        sequence,o->kind==1?"DRAW":"CLEAR",t->id,nvn_render.target?nvn_render.target->id:0,
        megaman_frame_cadence.boundaries,megaman_frame_cadence.skip,bits,before,megaman_frame_cadence.resources[slot].pending,
        o->vs,o->fs,nvn_render.depth_state[0],nvn_render.depth_state[1],nvn_render.stencil_write[0],nvn_render.stencil_write[1],
        nvn_render.stencil_compare[0],nvn_render.stencil_compare[1],nvn_render.stencil_ref[0],nvn_render.stencil_ref[1],
        nvn_render.color,nvn_render.polygon,nvn_render.mask,nvn_low.viewport_known,
        nvn_low.viewport[0],nvn_low.viewport[1],nvn_low.viewport[2],nvn_low.viewport[3],nvn_low.scissor_known,
        nvn_low.scissor[0],nvn_low.scissor[1],nvn_low.scissor[2],nvn_low.scissor[3],o->primitive,o->count,o->base,o->maximum,!!o->indices);
    if(o->indices&&o->count>=3){uint16_t index[3];memcpy(index,o->indices,sizeof(index));printf(" indices=%u/%u/%u",index[0],index[1],index[2]);}
    else printf(" indices=unavailable");
    if(o->position_valid&&o->position)printf(" position-validated=1 xy=%g/%g/%g/%g/%g/%g",
        o->position[0],o->position[1],o->position[2],o->position[3],o->position[4],o->position[5]);
    else printf(" position-validated=0");
    printf(" clear-depth=%"PRIu64" clear-value=0x%"PRIx64" clear-mask=0x%"PRIx64" diagnostic-only=1 cap=16\n",
        o->clear_depth,o->clear_value,o->clear_mask);
}
static unsigned megaman_frame_cadence_begin_present(struct nvn_texture *t) {
    if(!megaman_frame_cadence_enabled())return 0;
    pthread_mutex_lock(&nvn_gl_completion.mutex);
    enum mfc_result status=mfc_begin_present(&megaman_frame_cadence,nvn_nanoseconds());
    if(status)stop("NVN_LC2_CADENCE_PRESENT_ORDER",nvn_caller,status,24);
    nvn_mfc_target(t);
    if(!megaman_frame_cadence.skip)nvn_mfc_require(t,MFC_COLOR,0);
    unsigned skip=megaman_frame_cadence.skip;
    pthread_mutex_unlock(&nvn_gl_completion.mutex);return skip;
}
/* Replay holds the context mutex. Scratch reuse caches capacity only; every
 * draw validates fresh indices, then borrows u16 bytes or copies/rebases them.
 * Client index data was already freed immediately after glDrawElements; its
 * lifetime remains valid through that call and no longer depends on malloc. */
static uint16_t *nvn_render_index_scratch(uint64_t count) {
    if(count>1048576)stop("NVN_INDEX_RANGE",nvn_caller,count,24);
    if(count>nvn_render.index_capacity) {
        unsigned capacity=nvn_render.index_capacity?nvn_render.index_capacity:256;
        while(capacity<count)capacity*=2;
        uint16_t *indices=realloc(nvn_render.indices,(size_t)capacity*sizeof(*indices));
        if(!indices)stop("NVN_INDEX_MEMORY",nvn_caller,count,21);
        nvn_render.indices=indices;nvn_render.index_capacity=capacity;
    }
    return nvn_render.indices;
}
/* Sample the beginning and the final bounded frame of a black interval.
 * This adds diagnosis only; the 120-frame terminal BLACK limit is unchanged. */
/* Opt-in one-frame probe, requested through a file while the game is paused.
 * No framebuffer device access; captures only the current GL draw target. */
static unsigned t2_pass_probe_active(void) {
    static int configured;
    static const char *request;
    static unsigned checked=~0U, selected=~0U;
    if(!configured){request=getenv("T2_PASS_PROBE_REQUEST");configured=1;}
    if(!request)return 0;
    unsigned frame=nvn_replay.last_frame+1;
    if(frame!=checked){
        checked=frame;
        if(!(frame%30) && access(request,F_OK)==0 && unlink(request)==0){
            selected=frame;
            printf("T2_PASS_PROBE frame=%u scope=diagnostic-exclude-from-perf\n",frame);
        }
    }
    return frame==selected;
}
static unsigned nvn_draw_diagnostic_active(void) {
    if(megaman_frame_cadence_omitting())return 0;
    static int cfg;static unsigned explicit_diag,async_mode,black_dumps;
    if(!cfg){explicit_diag=getenv("COTM_DRAW_DIAGNOSTICS")!=NULL;const char *o=getenv("COTM_ASYNC_FRAME");async_mode=o&&!strcmp(o,"1");
        /* MOMO_DIAG_BLACK=1 brings back the draw dumps taken at the start and end of a black interval.
         * They are NOT free: each one reads every sampled texture back through an FBO and scans every
         * texel, so a 1024x1024 atlas is a million pixels off the GPU, per draw. MEDIDO run momo-262:
         * five consecutive frames at 10.5-11.7 s each - about a third of the entire 151 s boot - and the
         * log of those frames is nothing but these dumps. The boot screen is black for a long time, so
         * this fires on every normal run. Off by default now that the black-screen walls are closed; the
         * terminal 120-frame BLACK limit itself is untouched and still guards regressions. */
        const char *f=getenv("MOMO_DIAG_BLACK");black_dumps=f&&!strcmp(f,"1");cfg=1;}
#if MEGAMAN_HOST_DIAGNOSTIC && ZERO_ZX_SHADER_PACKAGES
    if(black_dumps && nvn_render.program && (nvn_render.program->sources[1]==93 || nvn_render.program->sources[1]==132 || nvn_render.program->sources[1]==135) && !nvn_render.program->zero_diagnostic_draws)return 1;
#endif
    if(async_mode&&!explicit_diag)return 0; /* black-interval draw dumps would starve the GL worker */
#if MEGAMAN_HOST_DIAGNOSTIC && ZERO_ZX_SHADER_PACKAGES
    if(explicit_diag && nvn_render.program && !nvn_render.program->zero_diagnostic_draws)return 1;
#endif
    return (getenv("COTM_DRAW_DIAGNOSTICS")&&nvn_replay.draws<16) ||
        (black_dumps && (nvn_replay.black_after_proof==1 || nvn_replay.black_after_proof==119));
}
static GLenum nvn_blend_factor(unsigned value) {
    static const GLenum factors[]={0,GL_ZERO,GL_ONE,GL_SRC_COLOR,GL_ONE_MINUS_SRC_COLOR,GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,
        GL_DST_ALPHA,GL_ONE_MINUS_DST_ALPHA,GL_DST_COLOR,GL_ONE_MINUS_DST_COLOR,GL_SRC_ALPHA_SATURATE};
    if(value<1||value>=sizeof(factors)/sizeof(factors[0]))stop("NVN_BLEND_FACTOR_REQUIRED",nvn_caller,value,20);
    return factors[value];
}
#include <GLES2/gl2ext.h>
static GLenum nvn_blend_equation(unsigned value) {
    if(value==1)return GL_FUNC_ADD;
    if(value==2)return GL_FUNC_SUBTRACT;
    if(value==3)return GL_FUNC_REVERSE_SUBTRACT;
#if ZERO_ZX_GAME
    if(value==4 || value==5) {
        const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
        if(!nvn_egl_extension(extensions,"GL_EXT_blend_minmax"))
            stop("ZERO_BLEND_MINMAX_BACKEND_REQUIRED",nvn_caller,value,20);
        return value==4?GL_MIN_EXT:GL_MAX_EXT;
    }
#endif
    stop("NVN_BLEND_EQUATION_REQUIRED",nvn_caller,value,20);return 0;
}
static GLenum nvn_stencil_operation(unsigned value) {
    static const GLenum operations[]={0,GL_KEEP,GL_ZERO,GL_REPLACE,GL_INCR,GL_DECR,GL_INVERT,GL_INCR_WRAP,GL_DECR_WRAP};
    if(value<1||value>8)stop("NVN_STENCIL_OPERATION_REQUIRED",nvn_caller,value,20);
    return operations[value];
}
static void nvn_render_stencil(void) {
    unsigned enabled=!!(nvn_render.depth_state[0]&4);
    nvn_render_cap(NCS_STENCIL_ENABLE,GL_STENCIL_TEST,enabled);
    if(!enabled)return;
    if(!nvn_render.depth || nvn_render.depth->b.format!=0x35)
        stop("NVN_STENCIL_DRAW_ATTACHMENT_REQUIRED",nvn_caller,0,20);
    for(unsigned face=0;face<2;face++) {
        if(nvn_render.stencil_known[face]!=7)
            stop("NVN_STENCIL_DYNAMIC_STATE_UNDEFINED",nvn_caller,(face<<8)|nvn_render.stencil_known[face],20);
        unsigned state=nvn_render.depth_state[1]>>(face*16),compare=state&15;
        if(compare<1||compare>8)stop("NVN_STENCIL_COMPARE_REQUIRED",nvn_caller,compare,20);
        GLenum side=face?GL_BACK:GL_FRONT;
        glStencilFuncSeparate(side,GL_NEVER+compare-1,nvn_render.stencil_ref[face],nvn_render.stencil_compare[face]);
        glStencilMaskSeparate(side,nvn_render.stencil_write[face]);
        glStencilOpSeparate(side,nvn_stencil_operation((state>>4)&15),
                            nvn_stencil_operation((state>>12)&15),nvn_stencil_operation((state>>8)&15));
    }
}
static struct nvn_descriptor *nvn_render_descriptor(uint64_t pool,unsigned type,uint32_t index) {
    for(unsigned i=0;i<32;i++) {
        struct nvn_descriptor_pool *d=&nvn_descriptor_pools[i];
        if(d->live&&d->object==pool&&d->type==type) {
            /* An index inside the reserved prefix is the guest's "no texture" handle: the hardware
             * samples zeros for it, so bind an empty descriptor instead of stopping the draw. */
            if(index<NVN_DESCRIPTOR_RESERVED) {
                static struct nvn_descriptor null_descriptor;
                memset(&null_descriptor,0,sizeof null_descriptor);
                static unsigned told;
                if(told++<8)printf("NVN_DESCRIPTOR_NULL index=%u type=%u pool=0x%"PRIx64"\n",index,type,pool);
                return &null_descriptor;
            }
            if(index>=d->count||!d->entries[index].valid)stop("NVN_RENDER_DESCRIPTOR_INVALID",nvn_caller,index,24);
            return &d->entries[index];
        }
    }
    stop("NVN_RENDER_DESCRIPTOR_POOL",nvn_caller,pool,24);return NULL;
}
#ifdef ANIMAL_WELL_SHADERS
static void aw_clear_flush(struct nvn_texture *t);
#endif
struct nvn_sampler_binding {struct nvn_texture *texture;GLint min,mag;GLenum wrap[2];};
/* The reserved constant bank has two texture-handle areas (MEDIDO in the sdk NSO): the combined
 * BindTexture writes at 0x20 + 8*unit, and BindSeparateTexture/Sampler write at 0x168 + 8*unit
 * (sdk+0x4833e8: index<<3 + 0x168). The shader's sampler offset in the catalogue is the bank offset
 * minus 0x20, so both areas resolve to the same binding table here. */
#ifdef MOMO_SHADERS
static unsigned momo_texture_unit(unsigned catalogue_offset,unsigned *ok) {
    unsigned bank=catalogue_offset+0x20;
    if(bank>=0x168){*ok=!((bank-0x168)%8);return (bank-0x168)/8;}
    if(bank>=0x20){*ok=!((bank-0x20)%8);return (bank-0x20)/8;}
    *ok=0;return 0;
}
#endif
#ifdef INFERNAX_CBUF_SAMPLERS
static unsigned infernax_sampler_seq,infernax_sampler_writer[2][128];
static uint64_t infernax_separate_sampler_handle(const struct nvn_uniform *u) {
    if(u->stage>1 || u->offset<0x148 || (u->offset-0x148)%8 || (u->offset-0x148)/8>=128 ||
       u->sampler_offset<0x568 || (u->sampler_offset-0x568)%8 || (u->sampler_offset-0x568)/8>=128)
        stop("INFERNAX_SEPARATE_SAMPLER_LAYOUT",nvn_caller,u->sampler_offset,24);
    unsigned texture_slot=(u->offset-0x148)/8,sampler_slot=(u->sampler_offset-0x568)/8;
    if(!(nvn_render.separate_texture_known[u->stage][texture_slot/64]&(UINT64_C(1)<<(texture_slot%64))))
        stop("INFERNAX_SEPARATE_TEXTURE_MISSING",nvn_caller,texture_slot,20);
    if(!(nvn_render.separate_sampler_known[u->stage][sampler_slot/64]&(UINT64_C(1)<<(sampler_slot%64))))
        stop("INFERNAX_SEPARATE_SAMPLER_MISSING",nvn_caller,sampler_slot,20);
    uint32_t texture=nvn_render.separate_textures[u->stage][texture_slot],sampler=nvn_render.separate_samplers[u->stage][sampler_slot];
    uint64_t handle=texture|((uint64_t)sampler<<32);
    static unsigned told;
    if(nvn_flag_on("NX_CBUF_AUDIT") && told++<32)
        printf("INFERNAX_SAMPLER_RESOLVED name=%s stage=%u texture-slot=%u sampler-slot=%u texture-written=1 sampler-written=1 texture-id=0x%x sampler-id=0x%x handle=0x%"PRIx64"\n",u->name,u->stage,texture_slot,sampler_slot,texture,sampler,handle);
    return handle;
}
static void infernax_sampler_record(const char *op,unsigned stage,unsigned unit,uint64_t before,uint64_t known) {
    if(!nvn_flag_on("NX_CBUF_AUDIT"))return;
    unsigned seq=++infernax_sampler_seq;infernax_sampler_writer[stage][unit]=seq;
    uint64_t after=nvn_render.texture_handles[stage][unit];
    if(seq<=64)printf("INFERNAX_SAMPLER_BIND seq=%u op=%s stage=%u unit=%u before=0x%"PRIx64" after=0x%"PRIx64" texture=%u sampler=%u known-before=0x%"PRIx64" known-after=0x%"PRIx64"\n",
        seq,op,stage,unit,before,after,(unsigned)after,(unsigned)(after>>32),known,nvn_render.texture_known[stage][unit/64]);
    if(seq==65)puts("INFERNAX_SAMPLER_BIND_TRUNCATED limit=64 last-writers=retained");
}
#endif
static struct nvn_sampler_binding nvn_render_sampler_prepare_mode(uint64_t handle,unsigned cpu_view,unsigned real_sample) {
    struct nvn_descriptor *td=nvn_render_descriptor(nvn_replay.texture_pool,1,(uint32_t)handle),*sd=nvn_render_descriptor(nvn_replay.sampler_pool,2,handle>>32);
    struct nvn_texture *t=NULL;struct nvn_sampler *s=NULL;nvn_render_resources(td,sd,&t,&s);
    if(t && t->zero_backend_deferred)stop("ZERO_FLOAT_TEXTURE_SAMPLER_BACKEND_REQUIRED",nvn_caller,t->b.format,20);
#ifdef ANIMAL_WELL_SHADERS
    if(t && t->aw_clear_valid)aw_clear_flush(t);
#endif
#ifdef MOMO_SHADERS
    /* Unity 2021 samples the very render target it is drawing into for its 640x360 post pass (MEDIDO
     * run momo-105: same object, same id). The hardware reads the pixel it is about to overwrite, so
     * the draw is admitted here with a bounded note instead of stopping the game. */
    if(t && s && t->name && zero_texture_storage_same(t,nvn_render.target)) {
        static unsigned told;
        if(told++<8)printf("MOMO_SAMPLE_RENDER_TARGET id=%"PRIu64" size=%ux%u note=same-pixel-read\n",t->id,t->b.width,t->b.height);
    } else
#endif
    if(!t||!s||!t->name||zero_texture_storage_same(t,nvn_render.target)) {
        struct nvn_texture *target=nvn_render.target;
        printf("MOMO_SAMPLER_REJECT handle=0x%"PRIx64" sampled-id=%"PRIu64" object=0x%"PRIx64" pool=0x%"PRIx64"+0x%"PRIx64
               " size=%ux%u | target-id=%"PRIu64" object=0x%"PRIx64" pool=0x%"PRIx64"+0x%"PRIx64" size=%ux%u same-object=%u\n",
               handle,t?t->id:0,t?t->object:0,t&&t->pool?t->pool->address:0,t?t->b.offset:0,t?t->b.width:0,t?t->b.height:0,
               target?target->id:0,target?target->object:0,target&&target->pool?target->pool->address:0,target?target->b.offset:0,
               target?target->b.width:0,target?target->b.height:0,t==target);
        fflush(stdout);
        stop("NVN_RENDER_TEXTURE_LIFETIME_FEEDBACK",nvn_caller,handle,24);
    }
    if(t->b.target!=1)stop("ZERO_SAMPLED_TEXTURE_TARGET_REQUIRED",nvn_caller,t->b.target,20);
    zero_texture_refresh(t);
    nvn_normal_require_materialized(t);
    nvn_rt_storage_require(t);
    nvn_rt_storage_sample(t,nvn_render.program?nvn_render.program->sources[0]:0,nvn_render.program?nvn_render.program->sources[1]:0,cpu_view,0,0);
    /* An omitted draw never samples on the GPU. A real draw cannot consume
     * pixels whose native writes were omitted, even through another handle. */
    if(real_sample)nvn_mfc_require(t,MFC_COLOR,0);
    if(nvn_low_color(t)>=0)stop("NVN_LOW_GUEST_SAMPLING_REQUIRED",nvn_caller,t->id,20);
    if(cpu_view) {
        if(t->host_scaled)stop("NVN_LC2_SCALED_CPU_VIEW_REQUIRED",nvn_caller,t->id,20);
        if(!nvn_texture_view_width(t->b.format,cpu_view,t->b.width))
            stop("NVN_LC2_CPU_VIEW_FORMAT_REQUIRED",nvn_caller,t->b.format,20);
        return (struct nvn_sampler_binding){t,GL_NEAREST,GL_NEAREST,{GL_CLAMP_TO_EDGE,GL_CLAMP_TO_EDGE}};
    }
    if(t->b.format==6)stop("NVN_LC2_R16_NORMALIZED_SAMPLING_REQUIRED",nvn_caller,handle,20);
    struct nvn_sampler_builder *b=&s->b;
    if((zero_texture_half_channels(t->b.format) || zero_texture_float_channels(t->b.format) || zero_texture_unorm16_channels(t->b.format) || zero_bc_float(t->b.format)) && (b->mag || (b->min!=0 && b->min!=2))) {
        const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
        unsigned core_half=nvn_texture_context_major()>=3&&zero_texture_half_channels(t->b.format);
        if(!core_half&&!nvn_texture_has_extension(extensions,zero_texture_half_channels(t->b.format)?"GL_OES_texture_half_float_linear":"GL_OES_texture_float_linear"))
            stop("ZERO_TEXTURE_FLOAT_LINEAR_REQUIRED",nvn_caller,t->id,20);
    }
    /* Animal Well: samplers carry lodMin=-1000 (below level 0 nothing changes) and a compare mode that its plain
     * sampler2D shaders never evaluate; both are admitted once-logged. Reduction/anisotropy/bias stay closed. */
    {static unsigned aw_sampler_reported;
     if((b->compare || b->lod_min<0) && !aw_sampler_reported++)printf("AW_SAMPLER_ADMIT compare=%u lod-min=%g shaders=sampler2D-only\n",b->compare,b->lod_min);}
    if(b->reduction||b->anisotropy!=1||b->lod_min>0||b->lod_max<1000||b->lod_bias!=0||b->lod_snap!=0)
        stop("NVN_RENDER_SAMPLER_FEATURE_REQUIRED",nvn_caller,s->id,20);
    GLenum wrap[2];for(unsigned i=0;i<2;i++) {
        wrap[i]=nvn_megaman_sampler_wrap(b->wrap[i],b->min,b->mag);
        if(!wrap[i])stop("NVN_RENDER_WRAP_REQUIRED",nvn_caller,b->wrap[i],20);
    }
    static const GLint min[]={GL_NEAREST,GL_LINEAR,GL_NEAREST_MIPMAP_NEAREST,GL_LINEAR_MIPMAP_NEAREST,GL_NEAREST_MIPMAP_LINEAR,GL_LINEAR_MIPMAP_LINEAR};
    /* NVN texture storage limits available LODs. GLES2 has no core maximum
     * texture-level parameter: requesting mip filtering for a one-level
     * image otherwise makes it incomplete and returns black. Collapse only
     * the nonexistent LOD interpolation, preserving the texel filter. */
    GLint min_filter=t->b.levels==1?((b->min&1)?GL_LINEAR:GL_NEAREST):min[b->min];
#if MEGAMAN_HOST_DIAGNOSTIC && ZERO_ZX_SHADER_PACKAGES
    if(b->min>=2 && t->b.levels>1) {
        unsigned full=1,extent=t->b.width>t->b.height?t->b.width:t->b.height;
        while(extent>1){extent>>=1;full++;}
        if(t->b.levels<full) {
            static uint64_t reported[64];static unsigned count;
            unsigned i=0;while(i<count && reported[i]!=t->id)i++;
            if(i==count && count<64) {
                reported[count++]=t->id;
                printf("ZERO_PARTIAL_MIP_SAMPLE id=%"PRIu64" texture=%u size=%ux%u levels=%u complete-levels=%u min=%u sources=%u/%u\n",
                       t->id,t->name,t->b.width,t->b.height,t->b.levels,full,b->min,
                       nvn_render.program->sources[0],nvn_render.program->sources[1]);
            }
        }
    }
#endif
    return (struct nvn_sampler_binding){t,min_filter,b->mag?GL_LINEAR:GL_NEAREST,{wrap[0],wrap[1]}};
}
static struct nvn_sampler_binding nvn_render_sampler_prepare(uint64_t handle,unsigned cpu_view) {
    return nvn_render_sampler_prepare_mode(handle,cpu_view,!nvn_mfc_omitting_draw());
}
static struct nvn_texture *nvn_render_sampler(uint64_t handle,unsigned unit,unsigned cpu_view,unsigned real_sample,GLint *min_filter,GLenum wrap[2]) {
    struct nvn_sampler_binding binding=nvn_render_sampler_prepare_mode(handle,cpu_view,real_sample);struct nvn_texture *t=binding.texture;
    if(t2_pass_probe_active())printf("T2_PASS_SAMPLER draw=%u id=%"PRIu64" min=%x mag=%x wrap=%x/%x\n",nvn_replay.draws,t->id,(unsigned)binding.min,(unsigned)binding.mag,(unsigned)binding.wrap[0],(unsigned)binding.wrap[1]);
    t2_page_sampler_filter(t,&binding.min,&binding.mag);   /* Frente F: an index plane is NEAREST whatever the sampler says */
    zero_texture_storage_sampler_require(t,unit,binding.min,binding.mag,binding.wrap[0],binding.wrap[1]);
    if(min_filter)*min_filter=binding.min;
    wrap[0]=binding.wrap[0];wrap[1]=binding.wrap[1];
    unsigned omitting=!real_sample;
    glActiveTexture(GL_TEXTURE0+unit);
    /* A flattened volume/array image (nvn_textures.h) cannot be sampled correctly on GLES2. */
    if(t->b.gl_name>1)stop("MOMO_VOLUME_TEXTURE_SAMPLED",nvn_caller,t->id,20);
    nvn_packaged_materialize(t);
    if(real_sample){t2_page_cache_touch(t,nvn_replay.last_frame+1);t2_residency_touch(t);t2_residency_materialize(t);} /* index/palette/blockmap exist before a real sampled bind */
    if(t->t2_page.deferred)t2_page_materialize_rgba8(t,NULL,0,"sampled-before-copy");   /* Frente F */
    GLuint texture_name=t->name;
    unsigned width=nvn_rt_storage_width(t),height=nvn_rt_storage_height(t);
    if(cpu_view){texture_name=nvn_texture_cpu_view_name(t,cpu_view);width=nvn_texture_view_width(t->b.format,cpu_view,width);}
    else {glBindTexture(GL_TEXTURE_2D,texture_name);nvn_texture_parameters(t,binding.min,binding.mag,
        t->t2_page.mode==T2_PAGE_MODE_COMPACT?GL_CLAMP_TO_EDGE:binding.wrap[0],
        t->t2_page.mode==T2_PAGE_MODE_COMPACT?GL_CLAMP_TO_EDGE:binding.wrap[1]);
        nvn_cpu_image_refresh(t,omitting);nvn_texture_small_reload(t);
        if((uint64_t)t->b.width*t->b.height<=64 && getenv("T2_TEX_TRACE")){static unsigned sread;
            if(sread<6) {sread++;
                GLuint tf=0;glGenFramebuffers(1,&tf);glBindFramebuffer(GL_FRAMEBUFFER,tf);
                glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t->name,0);
                GLenum sst=glCheckFramebufferStatus(GL_FRAMEBUFFER);unsigned char sp[16]={0};
                if(sst==GL_FRAMEBUFFER_COMPLETE)glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,sp);
                printf("T2_TEX_SAMPLED id=%"PRIu64" name=%u %ux%u status=0x%x px=%u,%u,%u,%u\n",
                       t->id,t->name,t->b.width,t->b.height,(unsigned)sst,sp[0],sp[1],sp[2],sp[3]);
                glBindFramebuffer(GL_FRAMEBUFFER,nvn_render.framebuffer);glDeleteFramebuffers(1,&tf);}}}
    if(t->host_scaled&&!t->host_scaled_sampled){t->host_scaled_sampled=1;
        printf("NVN_LC2_SCALED_SAMPLE object=0x%"PRIx64" id=%"PRIu64" texture=%u pool=0x%"PRIx64" format=0x%x flags=0x%x target=%"PRIu64" program=%u sources=%u/%u logical=%ux%u actual=%ux%u normalized-UV=unchanged\n",
            t->object,t->id,t->name,t->pool->object,t->b.format,t->b.flags,nvn_render.target->id,nvn_render.program->name,nvn_render.program->sources[0],nvn_render.program->sources[1],t->b.width,t->b.height,width,height);}
    /* Host observation is after CPU coherence; deferred CPU images have no
     * post-update observation. Initialize still records the original upload. */
    if(!omitting||!nvn_cpu_image_family(t))nvn_texdiag_sample(t,handle,unit,cpu_view);
    nvn_resource_lookup_report(nvn_replay.last_frame);nvn_texture_parameter_report(nvn_replay.last_frame);
    if(nvn_draw_diagnostic_active()) {
        GLint previous;GLuint fb;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&previous);glGenFramebuffers(1,&fb);
        glBindFramebuffer(GL_FRAMEBUFFER,fb);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture_name,0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)stop("NVN_SAMPLED_TEXTURE_DIAGNOSTIC_FBO",nvn_caller,t->name,21);
        size_t bytes=(size_t)width*height*4;uint8_t *pixels=malloc(bytes);if(!pixels)stop("NVN_SAMPLER_DIAGNOSTIC_MEMORY",nvn_caller,bytes,21);
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);unsigned nz=0,an=0;
        for(size_t k=0;k<bytes;k+=4){if(pixels[k]>3||pixels[k+1]>3||pixels[k+2]>3)nz++;if(pixels[k+3]>3)an++;}
        printf("NVN_SAMPLED_TEXTURE handle=%016"PRIx64" texture=%u size=%ux%u cpu-view=%u rgb-nonblack=%u alpha-nonzero=%u\n",handle,texture_name,width,height,cpu_view,nz,an);
        nvn_private_dump("sample-rgba-slot",nvn_replay.draws*16+unit,pixels,bytes);
        free(pixels);glBindFramebuffer(GL_FRAMEBUFFER,previous);glDeleteFramebuffers(1,&fb);
    }
    return t;
}
#include "nvn_normal_early.h"
#include "nvn_megaman_wave.h"
#include "zero_depth_shader_draw.h"
#include "zero_advent_mali_draw.h"
#ifdef ANIMAL_WELL_SHADERS
/* AW_DEFERRED_PASS=1: a color-only SetRenderTargets binds nothing; the FBO is bound at the pass's first draw and
 * the pass's ClearColor is applied there (same scissor). A clear-only pass stashes its clear on the target texture
 * and it is applied when that texture is next sampled, or at the end of the command list. On the Mali-450 every
 * bound-and-cleared target is a separate tiler job: 31 clear-only passes per frame measured (aw112: 25 ms of GPU
 * time with every draw skipped). */
static struct {uint64_t passes,materialized,stashed,flushed,list_flushed;} aw_deferred;
static unsigned aw_deferred_pass_enabled(void) {
    static int c;static unsigned e;
    if(!c){const char *o=getenv("AW_DEFERRED_PASS");e=o&&!strcmp(o,"1");c=1;if(e)printf("AW_DEFERRED_PASS enabled=1 bind=first-draw clears=stashed-per-target flush=sample-or-list-end\n");}
    return e;
}
static void nvn_render_state_epoch(void);
static void nvn_render_color_mask(unsigned mask);
static void aw_clear_apply(const struct nvn_texture *t,const float color[4],unsigned mask,const GLint scissor[4],unsigned known) {
    nvn_render_state_epoch();
    unsigned h=t&&t->host_half?2:1;
    if(known){glEnable(GL_SCISSOR_TEST);glScissor(scissor[0]/(GLint)h,scissor[1]/(GLint)h,scissor[2]==0x7fffffff?scissor[2]:scissor[2]/(GLint)h,scissor[3]==0x7fffffff?scissor[3]:scissor[3]/(GLint)h);}else glDisable(GL_SCISSOR_TEST);
    nvn_render_color_mask(mask);glClearColor(color[0],color[1],color[2],color[3]);glClear(GL_COLOR_BUFFER_BIT);
    nvn_render_state_epoch();
}
static void aw_clear_flush(struct nvn_texture *t) {
    if(!t || !t->aw_clear_valid)return;
    t->aw_clear_valid=0;
    GLuint previous=nvn_render.framebuffer;unsigned previous_bound=previous && !nvn_render.pass_skipped;
    nvn_framebuffer_bind(t,NULL);
    aw_clear_apply(t,t->aw_clear_color,t->aw_clear_mask,t->aw_clear_scissor,t->aw_clear_known);
    if(previous_bound){glBindFramebuffer(GL_FRAMEBUFFER,previous);nvn_framebuffers.active=previous;}
    if(nvn_low.scissor_known)nvn_low_apply_rect(nvn_render.target,0);
    aw_deferred.flushed++;
}
static void aw_clear_stash(struct nvn_texture *t,const float color[4],unsigned mask) {
    t->aw_draws_since_clear=0;
    if(t->aw_clear_valid)aw_clear_flush(t);
    memcpy(t->aw_clear_color,color,16);t->aw_clear_mask=mask;memcpy(t->aw_clear_scissor,nvn_low.scissor,sizeof(t->aw_clear_scissor));
    t->aw_clear_known=nvn_low.scissor_known;t->aw_clear_valid=1;aw_deferred.stashed++;
}
static void aw_clear_flush_all(void) {
    if(!aw_deferred_pass_enabled())return;
    if(nvn_render.pass_skipped && nvn_render.aw_deferred_target && nvn_render.pending_color_valid && nvn_render.target){
        aw_clear_stash(nvn_render.target,nvn_render.pending_color,nvn_render.pending_mask);nvn_render.pending_color_valid=0;}
    for(unsigned i=0;i<4096;i++)if(nvn_textures[i].live && nvn_textures[i].aw_clear_valid){aw_clear_flush(&nvn_textures[i]);aw_deferred.list_flushed++;}
    static uint64_t reports;
    if(aw_deferred.passes && (++reports%600)==0)
        printf("AW_DEFERRED_PASS_PROFILE passes=%"PRIu64" materialized=%"PRIu64" stashed=%"PRIu64" flushed=%"PRIu64" list-flushed=%"PRIu64"\n",aw_deferred.passes,aw_deferred.materialized,aw_deferred.stashed,aw_deferred.flushed,aw_deferred.list_flushed);
}
/* Shared simplex-noise field of Background/FarBackgroundLighting/NoiseBlur, rendered once per frame into a 320x180
 * texture (8.8 fixed, see prepare_aw_shaders.py noise_slice); consumers fetch it on texture unit 7 as `aw_noise`. */
#ifndef AW_NOISE_PASS_SCALE
#define AW_NOISE_PASS_SCALE 1
#endif
#if AW_NOISE_PASS_SCALE != 1 && AW_NOISE_PASS_SCALE != 2 && AW_NOISE_PASS_SCALE != 4
#error Unsupported Animal Well noise scale
#endif
#define AW_NOISE_WIDTH (320/AW_NOISE_PASS_SCALE)
#define AW_NOISE_HEIGHT (180/AW_NOISE_PASS_SCALE)
static struct {GLuint fbo,tex,program,vbo;GLint time_location;unsigned frame,ready;uint64_t passes;} aw_noise;
static void aw_noise_setup(void) {
    if(aw_noise.ready)return;
    aw_noise.ready=1;
    const char *vs="attribute vec2 p;void main(){gl_Position=vec4(p,0.0,1.0);}";
    GLuint shaders[2];aw_noise.program=glCreateProgram();
    for(unsigned i=0;i<2;i++) {
        const char *src=i?aw_noise_pass_source:vs;
        shaders[i]=glCreateShader(i?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);glShaderSource(shaders[i],1,&src,NULL);glCompileShader(shaders[i]);
        GLint ok=0;glGetShaderiv(shaders[i],GL_COMPILE_STATUS,&ok);
        if(!ok){char log[2048]={0};glGetShaderInfoLog(shaders[i],sizeof(log),NULL,log);printf("NVN_SHADER_COMPILE_ERROR aw_noise %s\n",log);stop("AW_NOISE_PASS_COMPILE",nvn_caller,i,21);}
        glAttachShader(aw_noise.program,shaders[i]);
    }
    glBindAttribLocation(aw_noise.program,0,"p");glLinkProgram(aw_noise.program);GLint ok=0;glGetProgramiv(aw_noise.program,GL_LINK_STATUS,&ok);
    if(!ok)stop("AW_NOISE_PASS_LINK",nvn_caller,0,21);
    aw_noise.time_location=glGetUniformLocation(aw_noise.program,"zero_f_b0_o1392");
    static const GLfloat quad[8]={-1,-1,1,-1,-1,1,1,1};
    glGenBuffers(1,&aw_noise.vbo);glBindBuffer(GL_ARRAY_BUFFER,aw_noise.vbo);glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);glBindBuffer(GL_ARRAY_BUFFER,0);
    glGenTextures(1,&aw_noise.tex);glBindTexture(GL_TEXTURE_2D,aw_noise.tex);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,AW_NOISE_WIDTH,AW_NOISE_HEIGHT,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    GLenum noise_filter=AW_NOISE_PASS_SCALE!=1?GL_LINEAR:GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,noise_filter);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,noise_filter);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1,&aw_noise.fbo);glBindFramebuffer(GL_FRAMEBUFFER,aw_noise.fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,aw_noise.tex,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)stop("AW_NOISE_PASS_FBO",nvn_caller,0,21);
    printf("AW_NOISE_PASS ready=1 size=%ux%u scale=%u encode=8.8 consumers=0,12,13,36 unit=7\n",AW_NOISE_WIDTH,AW_NOISE_HEIGHT,AW_NOISE_PASS_SCALE);
}
static void aw_noise_render(struct nvn_program *p) {
    unsigned frame=nvn_replay.last_frame+1;
    if(p->aw_noise_location<0 || aw_noise.frame==frame)return;
    struct nvn_range range=nvn_render.uniform[1][0];
    if(1392+16>range.size)stop("AW_NOISE_TIME_RANGE",nvn_caller,range.size,24);
    float time[4];memcpy(time,nvn_gpu_pointer(range.address+1392,16,PROT_READ),16);
    aw_noise_setup();
    glBindFramebuffer(GL_FRAMEBUFFER,aw_noise.fbo);glViewport(0,0,AW_NOISE_WIDTH,AW_NOISE_HEIGHT);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glColorMask(1,1,1,1);
    glUseProgram(aw_noise.program);glUniform4fv(aw_noise.time_location,1,time);
    glBindBuffer(GL_ARRAY_BUFFER,aw_noise.vbo);glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,0);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    glBindBuffer(GL_ARRAY_BUFFER,0);nvn_render_state.client_buffers_known=0;
    aw_noise.frame=frame;aw_noise.passes++;
    if(nvn_render.framebuffer && !nvn_render.pass_skipped){glBindFramebuffer(GL_FRAMEBUFFER,nvn_render.framebuffer);nvn_framebuffers.active=nvn_render.framebuffer;}
    if(nvn_low.viewport_known)nvn_low_apply_rect(nvn_render.target,1);
    if(nvn_low.scissor_known)nvn_low_apply_rect(nvn_render.target,0);
    nvn_render_state_epoch();
}
/* Per-target drawer bookkeeping for AW_HALF_RATE (two frames: current and previous). */
static struct {uint64_t id;unsigned frame,all_half,any;} aw_target_drawers[256];
static void aw_target_drawers_note(uint64_t id,unsigned fragment,unsigned half) {
    (void)fragment;unsigned frame=nvn_replay.last_frame+1;unsigned slot=(unsigned)(id%256);
    for(unsigned i=0;i<256;i++){unsigned k=(slot+i)%256;
        if(aw_target_drawers[k].id==id){if(aw_target_drawers[k].frame!=frame){aw_target_drawers[k].frame=frame;aw_target_drawers[k].all_half=half;aw_target_drawers[k].any=1;}else aw_target_drawers[k].all_half&=half;return;}
        if(!aw_target_drawers[k].id){aw_target_drawers[k]=(typeof(aw_target_drawers[0])){id,frame,half,1};return;}}
}
static unsigned aw_target_frozen(uint64_t id) {
    unsigned frame=nvn_replay.last_frame+1;if(!(frame&1))return 0;unsigned slot=(unsigned)(id%256);
    for(unsigned i=0;i<256;i++){unsigned k=(slot+i)%256;
        if(aw_target_drawers[k].id==id)return aw_target_drawers[k].frame==frame-1 && aw_target_drawers[k].all_half;
        if(!aw_target_drawers[k].id)return 0;}
    return 0;
}
#endif
static inline uint64_t aw_setup_now(void){if(!nvn_phase.enabled)return 0;struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000ULL+t.tv_nsec;}
static void nvn_render_draw_inner(struct nvn_record *r) {
    uint64_t aw_t0=aw_setup_now();
    if(r->args[0]==0)stop("ZERO_POINT_DRAW_SHADER_REQUIRED",nvn_caller,0,20);
    if(r->args[0]<=3)glLineWidth(nvn_render.line_width);
    if(nvn_skip.active){nvn_skip.draws_skipped++;return;}
#ifdef ANIMAL_WELL_SHADERS
    aw_link_all();
#endif
    struct nvn_program *p=nvn_program_resolve(nvn_render.program_stages);nvn_render.program=p;
#ifdef MOMO_SHADERS
    /* Frente F: a fragment sampler bound to a palettized page selects the program variant whose read of
     * that sampler goes through the palette (t2_page_substitution.h); the base program is untouched. */
    if(p && p->live && p->linked && t2_pages.mode) {
        unsigned t2_key=0,t2_mode=t2_pages.mode;
        for(unsigned i=0;i<p->uniform_count;i++) {
            struct nvn_uniform *u=&p->uniforms[i];
            if(u->kind!=3 || u->binding!=14 || u->stage!=1)continue;
            unsigned aligned=1,slot=momo_texture_unit(u->offset,&aligned);
            if(!aligned || slot>=128 || !(nvn_render.texture_known[1][slot/64]&(1ULL<<(slot%64))))continue;
            uint64_t handle=nvn_render.texture_handles[1][slot];
            struct nvn_descriptor *td=nvn_render_descriptor(nvn_replay.texture_pool,1,(uint32_t)handle),*sd=nvn_render_descriptor(nvn_replay.sampler_pool,2,handle>>32);
            struct nvn_texture *t=NULL;struct nvn_sampler *sm=NULL;nvn_render_resources(td,sd,&t,&sm);
            if(!t || !t->t2_page.mode || u->t2_sampler>=8)continue;
            if(t2_key && t->t2_page.mode!=t2_mode)stop("T2_PAGE_MIXED_MODES_IN_DRAW",nvn_caller,t->id,20); /* ROOT */
            t2_key|=1u<<u->t2_sampler;t2_mode=t->t2_page.mode;
        }
        if(t2_key){t2_page_calibrate();nvn_render_state_epoch();}   /* once: palette formula measured on this GPU */
        if(t2_key!=p->t2_page_key || (t2_key && t2_mode!=p->t2_page_mode)){p=nvn_program_resolve_key2(nvn_render.program_stages,p->aw_float_key,t2_key,t2_mode);nvn_render.program=p;} /* ROOT: per-page mode */
    }
#endif
#ifdef ANIMAL_WELL_SHADERS
    {   /* AW_FLUID_JACOBI_KEEP=k (odd): run only the first k of the 15 pressure Jacobi iterations per frame. The
         * ping-pong starts and ends on the same texture for odd k; the solve is warm-started from the previous
         * frame. FPS experiment (measured 62 ms/frame for 15 iterations at 320x180 on Mali-450). */
        static int cfg;static unsigned keep;static unsigned frame_seen,jacobi_in_frame;
        if(!cfg){const char *o=getenv("AW_FLUID_JACOBI_KEEP");keep=o?(unsigned)strtoul(o,NULL,10):0;if(keep&&!(keep&1))keep++;cfg=1;
            if(keep)printf("AW_FLUID_JACOBI_KEEP iterations=%u of=15 warm-start=previous-frame scope=fps-experiment\n",keep);}
        static char skip_set[256];static int skip_cfg;
        if(!skip_cfg){skip_cfg=1;const char *o=getenv("AW_SKIP_FRAGMENTS");if(o){printf("AW_SKIP_FRAGMENTS list=%s scope=fps-experiment output=invalid\n",o);
            for(const char *q=o;*q;){char *e;unsigned long v=strtoul(q,&e,10);if(e==q)break;if(v<256)skip_set[v]=1;q=*e==','?e+1:e;}}}
        if(p && skip_set[p->sources[1]&255])return;
        /* AW_HALF_RATE=list: those fragment passes execute on even frames only; their targets keep the previous
         * result (their per-frame ClearColor is skipped on odd frames when every previous-frame drawer of that
         * target is half-rate). Screen-space effects and the fluid simulation update at half the frame rate. */
        static char half_set[256];static int half_cfg;
        if(!half_cfg){half_cfg=1;const char *o=getenv("AW_HALF_RATE");if(o){printf("AW_HALF_RATE fragments=%s scope=fps-policy effects=every-other-frame\n",o);
            for(const char *q=o;*q;){char *e;unsigned long v=strtoul(q,&e,10);if(e==q)break;if(v<256)half_set[v]=1;q=*e==','?e+1:e;}}}
        if(p && half_set[p->sources[1]&255] && ((nvn_replay.last_frame+1)&1))return;
        if(p && nvn_render.target){ /* remember who draws each target this frame */
            aw_target_drawers_note(nvn_render.target->id,p->sources[1],half_set[p->sources[1]&255]);
        }
        if(keep && p && p->sources[1]==20) {
            if(frame_seen!=nvn_replay.last_frame+1){frame_seen=nvn_replay.last_frame+1;jacobi_in_frame=0;}
            if(++jacobi_in_frame>keep)return;
        }
        /* AW_BLOOM_KEEP=k (odd): first k of the 3 horizontal and 3 vertical bloom blur iterations (26/28), same ping-pong parity. */
        static int bcfg;static unsigned bkeep;static unsigned bframe,bh,bv;
        if(!bcfg){const char *o=getenv("AW_BLOOM_KEEP");bkeep=o?(unsigned)strtoul(o,NULL,10):0;if(bkeep&&!(bkeep&1))bkeep++;bcfg=1;
            if(bkeep)printf("AW_BLOOM_KEEP iterations=%u of=3 per-direction scope=fps-experiment\n",bkeep);}
        if(bkeep && p && (p->sources[1]==26||p->sources[1]==28)) {
            if(bframe!=nvn_replay.last_frame+1){bframe=nvn_replay.last_frame+1;bh=bv=0;}
            unsigned *n=p->sources[1]==26?&bh:&bv;
            if(++*n>bkeep)return;
        }
    }
#endif
#ifdef ANIMAL_WELL_SHADERS
    if(p && p->live && p->linked && nvn_render.target) {
        /* Float-emulated bindings select a program variant: decode on the sampled fluid textures, encode on a fluid target. */
        unsigned aw_key=aw_float_code(nvn_render.target->aw_float_kind)<<16;
        const struct mm_shader_binding *fb=&nvn_megaman_shader_bindings[p->sources[1]];
        for(unsigned i=0;i<p->uniform_count;i++) {
            struct nvn_uniform *u=&p->uniforms[i];
            if(u->kind!=3 || u->binding!=14 || u->stage!=1)continue;
#ifdef MOMO_SHADERS
            unsigned aligned=1,slot=momo_texture_unit(u->offset,&aligned);
            if(!aligned || slot>=128 || !(nvn_render.texture_known[1][slot/64]&(1ULL<<(slot%64))))continue;
#else
            unsigned slot=u->offset/8;if(slot>=128 || !(nvn_render.texture_known[1][slot/64]&(1ULL<<(slot%64))))continue;
#endif
            uint64_t handle=nvn_render.texture_handles[1][slot];
            struct nvn_descriptor *td=nvn_render_descriptor(nvn_replay.texture_pool,1,(uint32_t)handle),*sd=nvn_render_descriptor(nvn_replay.sampler_pool,2,handle>>32);
            struct nvn_texture *t=NULL;struct nvn_sampler *s=NULL;nvn_render_resources(td,sd,&t,&s);
            if(!t || !t->aw_float_kind)continue;
            unsigned j=0;while(j<fb->sampler_count && strcmp(fb->samplers[j].name,u->name))j++;
            if(j>=fb->sampler_count || j>=8)stop("AW_FLOAT_SAMPLER_INDEX",nvn_caller,i,20);
            aw_key|=aw_float_code(t->aw_float_kind)<<(2*j);
        }
        if(aw_key){p=nvn_program_resolve_key(nvn_render.program_stages,aw_key);nvn_render.program=p;}
        if(((aw_key>>16)&3)==1 && (nvn_render.color&0x100))stop("AW_FLOAT_RG_TARGET_BLEND_UNSUPPORTED",nvn_caller,nvn_render.color,20);
    }
#endif
#ifdef ANIMAL_WELL_SHADERS
    /* Only this two-pass ambient effect is resampled. Scene, fluid simulation,
     * collision masks, normal maps and UI retain their original dimensions. */
#ifdef AW_CAUSTIC_HALF
    if(p && nvn_render.target) {
        struct nvn_texture *t=nvn_render.target;
        if(t->aw_caustic_half && (p->sources[0]!=45 || (p->sources[1]!=36 && p->sources[1]!=7 && p->sources[1]!=8)))
            stop("AW_CAUSTIC_UNEXPECTED_WRITER",nvn_caller,p->sources[1],20);
        if(p->sources[0]==45 && p->sources[1]==36 && !t->aw_caustic_half) {
            printf("AW_CAUSTIC_PREFLIGHT target=%"PRIu64" size=%ux%u fmt=%x levels=%u samples=%u scaled=%u half=%u depth=%u attached=%u draws=%u skip=%u deferred=%u pending=%u mask=%u stashed=%u smask=%u\n",
                t->id,t->b.width,t->b.height,t->b.format,t->b.levels,t->b.samples,t->host_scaled,t->host_half,t->depth_buffer,!!nvn_render.depth,t->draws,nvn_render.pass_skipped,nvn_render.aw_deferred_target,nvn_render.pending_color_valid,nvn_render.pending_mask,t->aw_clear_valid,t->aw_clear_mask);
            if(t->b.width!=320 || t->b.height!=180 || t->b.format!=0x25 || t->b.levels!=1 ||
               t->b.samples || t->host_scaled || t->host_half || t->depth_buffer || nvn_render.depth ||
               t->draws || !nvn_render.pass_skipped || !nvn_render.aw_deferred_target ||
               !((nvn_render.pending_color_valid && nvn_render.pending_mask==15) ||
                 (t->aw_clear_valid && t->aw_clear_mask==15)))
                stop("AW_CAUSTIC_TARGET_CONTRACT",nvn_caller,t->id,20);
            GLint bound=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);
            glBindTexture(GL_TEXTURE_2D,t->name);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,160,90,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
            glBindTexture(GL_TEXTURE_2D,(GLuint)bound);
            nvn_framebuffer_forget(t->id);t->host_half=1;t->aw_caustic_half=1;
            printf("AW_CAUSTIC_HALF target=%"PRIu64" size=160x90 writers=36,7,8 scene=unchanged\n",t->id);
        }
    }
#endif
    if(nvn_render.pass_skipped && nvn_render.aw_deferred_target && nvn_render.target) {
        struct nvn_texture *t=nvn_render.target;
        nvn_render.pass_skipped=0;nvn_render.aw_deferred_target=0;
        nvn_render.framebuffer=nvn_framebuffer_bind(t,nvn_render.depth);
        nvn_low_target(t,nvn_render.depth);
        if(t->aw_clear_valid){t->aw_clear_valid=0;aw_clear_apply(t,t->aw_clear_color,t->aw_clear_mask,t->aw_clear_scissor,t->aw_clear_known);aw_deferred.flushed++;}
        if(nvn_render.pending_color_valid){aw_clear_apply(t,nvn_render.pending_color,nvn_render.pending_mask,nvn_low.scissor,nvn_low.scissor_known);nvn_render.pending_color_valid=0;}
        if(nvn_low.viewport_known)nvn_low_apply_rect(t,1);
        if(nvn_low.scissor_known)nvn_low_apply_rect(t,0);
        nvn_render_state_epoch();aw_deferred.materialized++;
    }
#endif
    if(nvn_render.pass_skipped) {
        unsigned normal=p&&((p->sources[0]==4&&p->sources[1]==5)||(p->sources[0]==9&&p->sources[1]==10));
        if(normal){nvn_render.pass_skipped_draws++;return;}
        /* A real draw reached a target we assumed was normal-only: bind it now
         * and replay the clears we deferred, then draw normally. */
        nvn_render.pass_skipped=0;nvn_render.pass_materialized++;
        nvn_render.framebuffer=nvn_framebuffer_bind(nvn_render.target,nvn_render.depth);
        nvn_low_target(nvn_render.target,nvn_render.depth);
        nvn_render_state_epoch();
        if(nvn_render.pending_color_valid){nvn_render_color_mask(nvn_render.pending_mask);glClearColor(nvn_render.pending_color[0],nvn_render.pending_color[1],nvn_render.pending_color[2],nvn_render.pending_color[3]);glClear(GL_COLOR_BUFFER_BIT);nvn_render.pending_color_valid=0;}
        if(nvn_render.pending_depth_valid){nvn_render_depth_mask(GL_TRUE);glClearDepthf(nvn_render.pending_depth);glClear(GL_DEPTH_BUFFER_BIT);nvn_render.pending_depth_valid=0;}
        printf("NVN_NORMAL_PASS_MATERIALIZED target=%"PRIu64" program=%u/%u count=%"PRIu64"\n",nvn_render.target->id,p?p->sources[0]:0,p?p->sources[1]:0,nvn_render.pass_materialized);
    }
    uint64_t aw_t1=aw_setup_now();
    unsigned diagnose=nvn_draw_diagnostic_active();
    {   /* MOMO_FBO_DIAGNOSE=1 turns the full per-draw dump on for the census frames alone. */
        static int cfg;static unsigned wanted,on;
        if(!cfg){const char *o=getenv("MOMO_FBO_FRAME");wanted=o?(unsigned)strtoul(o,NULL,0):0;on=getenv("MOMO_FBO_DIAGNOSE")!=NULL;cfg=1;}
        if(on && wanted && nvn_replay.last_frame+1>=wanted && nvn_replay.last_frame+1<wanted+1)diagnose=1;
        /* MOMO_DIAG_MASK=1: full dump of the draws that write the stencil with the colour mask closed
         * (Unity's UI mask), which is the draw whose geometry covers nothing here. */
        static int mcfg;static unsigned mask_diag,mask_seen;
        if(!mcfg){mask_diag=getenv("MOMO_DIAG_MASK")!=NULL;mcfg=1;}
        if(mask_diag && mask_seen<4 && (nvn_render.depth_state[0]&4U) && !(nvn_render.mask&15U) &&
           (nvn_render.stencil_write[0]||nvn_render.stencil_write[1])){diagnose=1;mask_seen++;}
    }
#if MEGAMAN_HOST_DIAGNOSTIC && ZERO_ZX_SHADER_PACKAGES
    if((p->sources[1]==163||p->sources[1]==93||p->sources[1]==97||p->sources[1]==135) && !p->zero_diagnostic_draws)diagnose=1;
#endif
    if(!p||!p->live||!p->linked||!nvn_render.target)stop("NVN_DRAW_PIPELINE_INCOMPLETE",nvn_caller,r->op,24);
    nvn_mfc_target(nvn_render.target);nvn_mfc_target(nvn_render.depth);
    if(nvn_render.scratch_pool && !nvn_render.scratch_pool->live)
        stop("NVN_SHADER_SCRATCH_POOL_LIFETIME",nvn_caller,nvn_render.scratch_pool->object,24);
    unsigned effective_color_mask,requested_color_mask=nvn_render.mask&15U;
    zero_gpu_cost_context(nvn_functions[r->op].name,p->sources[0],p->sources[1],
        nvn_render.target->b.width,nvn_render.target->b.height,
        nvn_rt_storage_width(nvn_render.target),nvn_rt_storage_height(nvn_render.target));
#if ZERO_ZX_GAME
    if(nvn_render.target->b.format==1)requested_color_mask&=1;
#endif
    if(!nvn_megaman_color_mask(p->sources[1],p->color_write_mask,requested_color_mask,&effective_color_mask))
        stop("NVN_LC2_UNDEFINED_FRAGMENT_COLOR",nvn_caller,nvn_render.mask,20);
    if(nvn_render.target==&zero_no_color)effective_color_mask=0;
    nvn_low_draw(nvn_render.target);
    if(nvn_render.target->host_scaled&&p->wave_contract)stop("NVN_LC2_SCALED_WAVE_TARGET_REQUIRED",nvn_caller,nvn_render.target->id,20);
    if(nvn_render.target->host_scaled&&!nvn_render.target->host_scaled_drawn){struct nvn_texture *t=nvn_render.target;t->host_scaled_drawn=1;
        printf("NVN_LC2_SCALED_DRAW object=0x%"PRIx64" id=%"PRIu64" texture=%u pool=0x%"PRIx64" format=0x%x flags=0x%x program=%u sources=%u/%u logical=%ux%u actual=%ux%u\n",
            t->object,t->id,t->name,t->pool?t->pool->object:0,t->b.format,t->b.flags,p->name,p->sources[0],p->sources[1],t->b.width,t->b.height,t->host_width,t->host_height);}
    unsigned alpha=(nvn_render.color>>24)&15,logic=(nvn_render.color>>16)&255;
    unsigned specialized=nvn_alpha_selected(p,alpha,nvn_replay.alpha_ref);GLuint program=specialized?p->alpha5.name:p->name;
    if(diagnose)printf("NVN_DRAW_DIAGNOSTIC_FRAME frame=%u draw=%u following-black=%u target=0x%"PRIx64" texture=%u framebuffer=%u program=%u sources=%u/%u\n",
        nvn_replay.last_frame+1,nvn_replay.draws,nvn_replay.black_after_proof,nvn_render.target->object,
        nvn_render.target->name,nvn_render.framebuffer,program,p->sources[0],p->sources[1]);
    if(logic!=3||alpha<1||alpha>8||((nvn_render.polygon>>3)&3)!=2)
        stop("NVN_DRAW_FIXED_STATE_REQUIRED",nvn_caller,nvn_render.color,20);
    if((nvn_render.polygon>>15)&8U)stop("NVN_LC2_POLYGON_OFFSET_ENABLE_REQUIRED",nvn_caller,nvn_render.polygon,20);
    nvn_megaman_raster_apply(nvn_render.polygon,nvn_render.polygon_offset[0],nvn_render.polygon_offset[1]);
#ifdef ANIMAL_WELL_SHADERS
    (void)nvn_normal_early_try; /* COTM normal-map elision: it reads sampler handles from guest memory, which the async snapshot never captures */
#else
    if(nvn_normal_early_try(r,p,program,diagnose))return;
#endif
#ifdef ANIMAL_WELL_SHADERS
    aw_noise_render(p);
#endif
    uint64_t aw_t2=aw_setup_now();
    nvn_render_state_epoch();nvn_render_use_program(p,program);
    nvn_uniform_begin(p,program);
    zero_texture_storage_sampler_begin();
#ifdef ANIMAL_WELL_SHADERS
    if(p->aw_float_key && p->aw_float_range_location>=0){GLfloat range=aw_float_range();nvn_uniform_send(p->aw_float_range_location,NVN_UNIFORM_FLOAT,&range);}
#endif
    if(zero_render_scale_enabled()) {
        GLfloat ratio[2];zero_render_scale_fragment_ratio(nvn_render.target,ratio);
        nvn_uniform_send(p->zero_scale_xy,NVN_UNIFORM_VECTOR2,ratio);
    }
    GLfloat clip_control[2]={nvn_device.window_origin?-1.0f:1.0f,(GLfloat)nvn_device.depth_mode};
    nvn_uniform_send(specialized?p->alpha5.clip_control:p->clip_control,NVN_UNIFORM_VECTOR2,clip_control);
    if(!specialized) {
        GLfloat reference;memcpy(&reference,&nvn_replay.alpha_ref,4);GLint function=alpha;
        nvn_uniform_send(p->alpha_ref,NVN_UNIFORM_FLOAT,&reference);nvn_uniform_send(p->alpha_func,NVN_UNIFORM_INT,&function);
    }
    nvn_render_cap(NCS_BLEND_ENABLE,GL_BLEND,!!(nvn_render.color&0x100));
    uint32_t blend=nvn_render.blend[1];
    GLenum factors[4]={nvn_blend_factor(blend&127),nvn_blend_factor((blend>>8)&127),nvn_blend_factor((blend>>16)&127),nvn_blend_factor((blend>>24)&127)};
    if(nvn_render_state_send(NCS_BLEND_FUNC,blend))glBlendFuncSeparate(factors[0],factors[1],factors[2],factors[3]);
    GLenum equations[2]={nvn_blend_equation((nvn_render.blend[0]>>24)&7),nvn_blend_equation((nvn_render.blend[0]>>28)&7)};
    if(nvn_render_state_send(NCS_BLEND_EQUATION,nvn_render.blend[0]))glBlendEquationSeparate(equations[0],equations[1]);
    nvn_render_cap(NCS_DEPTH_ENABLE,GL_DEPTH_TEST,nvn_render.depth_state[0]&1);
    unsigned func=(nvn_render.depth_state[0]>>4)&15;if(func<1||func>8)stop("NVN_DEPTH_FUNC_INVALID",nvn_caller,func,24);
    if(nvn_render_state_send(NCS_DEPTH_FUNC,func))glDepthFunc(GL_NEVER+func-1);
    nvn_render_depth_mask((nvn_render.depth_state[0]>>1)&1);nvn_render_stencil();
    unsigned cull=nvn_render.polygon&3;
    unsigned front_ccw=!!(nvn_render.polygon&4)^nvn_device.window_origin;
    if(nvn_render_state_send(NCS_FRONT_FACE,front_ccw))glFrontFace(front_ccw?GL_CCW:GL_CW);
    nvn_render_cap(NCS_CULL_ENABLE,GL_CULL_FACE,!!cull);
    if(cull&&nvn_render_state_send(NCS_CULL_FACE,cull))glCullFace(cull==1?GL_FRONT:cull==2?GL_BACK:GL_FRONT_AND_BACK);
    /* Unity's UI mask writes the stencil with the colour mask fully closed. On this Mali-450 the whole
     * menu and every text glyph then disappear, and disabling only the stencil test brings them back
     * (MEDIDO run momo-163): the closed-colour draw never reaches the stencil buffer. The substitution
     * keeps the draw real and leaves the image untouched by blending it with src=ZERO, dst=ONE, which
     * writes exactly the destination colour back. MOMO_STENCIL_COLOR_PRESERVE=0 restores the plain mask. */
    unsigned stencil_writes=(nvn_render.depth_state[0]&4U) &&
        (nvn_render.stencil_write[0]||nvn_render.stencil_write[1]);
    static int preserve_cfg;static unsigned preserve_enabled;
    /* Default OFF: the bench below proved this driver writes the stencil with the colour mask closed
     * (MOMO_STENCIL_WRITE_TEST, run momo-170: 14400/14400 pixels with the mask open AND closed), so the
     * substitution is not needed. It stays available as MOMO_STENCIL_COLOR_PRESERVE=1. */
    if(!preserve_cfg){const char *o=getenv("MOMO_STENCIL_COLOR_PRESERVE");preserve_enabled=o&&strcmp(o,"0");preserve_cfg=1;}
    unsigned preserve=preserve_enabled && !effective_color_mask && stencil_writes;
    if(preserve) {
        nvn_render_color_mask(15);
        nvn_render_cap(NCS_BLEND_ENABLE,GL_BLEND,1);
        if(nvn_render_state_send(NCS_BLEND_FUNC,UINT64_C(0x5052455345525645)))glBlendFuncSeparate(GL_ZERO,GL_ONE,GL_ZERO,GL_ONE);
        if(nvn_render_state_send(NCS_BLEND_EQUATION,UINT64_C(0x5052455345525645)))glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
        static unsigned told;
        if(told++<4)printf("MOMO_STENCIL_COLOR_PRESERVE draw=%u sources=%u/%u stencil-write=0x%x/0x%x colour=destination-preserved\n",
            nvn_replay.draws+1,p->sources[0],p->sources[1],nvn_render.stencil_write[0],nvn_render.stencil_write[1]);
    }
    else nvn_render_color_mask(effective_color_mask);
    /* A preserved stencil draw really samples even on an omitted boundary.
     * Keep normal pending-data checks and current CPU texture coherence. */
    unsigned cadence_stencil=nvn_mfc_stencil_candidate(p->sources[0],p->sources[1],effective_color_mask) &&
        r->op!=NI_nvnCommandBufferDrawArrays && r->args[0]==5 && r->args[2]==4;
    if(cadence_stencil)nvn_mfc_require(nvn_render.depth,MFC_STENCIL,0);
    /* MOMO_DIAG_OPEN=1: diagnostic only (never a shipping path). Drops depth, stencil and blending and
     * opens the colour mask, to tell "the fragments are killed" apart from "no geometry arrives" when a
     * whole group of draws writes nothing (title-screen UI, MEDIDO run momo-116). */
    {   static int cfg;static unsigned open_all;
        /* 1 = all three, 2 = depth only, 3 = stencil only, 4 = blend only: which test is killing a group
         * of draws is then a single measurement instead of a guess. */
        if(!cfg){const char *o=getenv("MOMO_DIAG_OPEN");open_all=o?(unsigned)strtoul(o,NULL,10):0;cfg=1;}
        if(open_all) {
            if(open_all==1||open_all==2)glDisable(GL_DEPTH_TEST);
            if(open_all==1||open_all==3)glDisable(GL_STENCIL_TEST);
            if(open_all==1||open_all==4)glDisable(GL_BLEND);
            if(open_all==1)glColorMask(1,1,1,1);
            nvn_render_state.valid=0;
        }
    }
    unsigned real_sample=!nvn_mfc_omitting_draw() || cadence_stencil;
    if(diagnose) {
        GLint vp[4],sc[4];glGetIntegerv(GL_VIEWPORT,(void*)vp);glGetIntegerv(GL_SCISSOR_BOX,(void*)sc);
        printf("NVN_DRAW_STATE color=%08x blend=%08x/%08x depth=%08x polygon=%08x mask=%x alpha=%u ref=%g viewport=%d/%d/%d/%d scissor=%d/%d/%d/%d\n",nvn_render.color,nvn_render.blend[0],nvn_render.blend[1],nvn_render.depth_state[0],nvn_render.polygon,nvn_render.mask,alpha,nvn_replay.alpha_ref,vp[0],vp[1],vp[2],vp[3],sc[0],sc[1],sc[2],sc[3]);
    }
    struct nvn_megaman_wave_draw wave={0};nvn_megaman_wave_begin(p,&wave);
    nvn_texdiag_draw_begin(nvn_replay.last_frame+1,nvn_replay.draws,p->sources[0],p->sources[1],program,p->id);
    uint64_t aw_t3=aw_setup_now();
    unsigned units=0,samplers_total=0,momo_tmp_sprite_atlas=0;
    uint64_t pass_textures[8],pass_handles[8];unsigned pass_slots[8],pass_offsets[8],pass_texture_count=0;const char *pass_names[8];unsigned pass_sizes[8][2];
    for(unsigned i=0;i<p->uniform_count;i++)if(p->uniforms[i].kind==3)samplers_total++;
    for(unsigned i=0;i<p->uniform_count;i++) {
        struct nvn_uniform *u=&p->uniforms[i];GLint location=specialized?p->alpha5.locations[i]:u->location;
        if(u->location<0 && u->size_location<0)continue;
        if(u->texel_fetch_bits && !u->cpu_view)stop("NVN_LC2_PACKED_TEXTURE_CONTRACT_REQUIRED",nvn_caller,u->texel_fetch_bits,20);
        unsigned bytes=u->kind==1?64:u->kind==2?16:u->kind==3||u->kind==5?8:u->kind==4||u->kind==6?4:0;
        if(u->kind==7) {
            /* Whole dynamic range of the constant buffer as one vec4 array; short buffers read as zero.
             * Preserve CPU words without exponent-based numeric conversion. LC2 titles bind 64 KB banks, which a
             * fixed 64-vec4 stack buffer cannot hold (Animal Well only ever bound a fraction of that). */
            const char *sh=getenv("NX_CBUF_RANGE_SHIFT");int shift=sh?atoi(sh):0;int bi=(int)u->binding+shift;if(bi<0)bi=0;if(bi>15)bi=15;
            struct nvn_range range=nvn_render.uniform[u->stage][bi];
            unsigned n=u->size;
            float *words=malloc(n?n:1);if(!words)stop("NVN_UNIFORM_ARRAY_ALLOC",nvn_caller,n,21);
            memset(words,0,n);
            unsigned avail=u->offset>=range.size?0:(unsigned)(range.size-u->offset);if(avail>n)avail=n;
            if(avail) {
                const void *src=nvn_gpu_pointer(range.address+u->offset,avail,PROT_READ);
                if(!src)stop("INFERNAX_CBUF_RANGE_UNMAPPED",nvn_caller,range.address+u->offset,24);
                memcpy(words,src,avail);
            }
#ifdef INFERNAX_CBUF_SAMPLERS
            /* This catalogue lowers every constant block to `uniform sampler2D` + texelFetch
             * (1024 texels per row, four floats per texel), so the bank goes up as an RGBA32F
             * texture and the sampler gets a unit above the program's ordinary samplers.
             * glUniform4fv against a sampler is GL_INVALID_OPERATION (measured, 0x502). */
            static int texture_mode=-1;
            if(texture_mode<0){const char *o=getenv("NX_CBUF_TEXTURE");texture_mode=!(o&&!strcmp(o,"0"));}
            if(texture_mode && location>=0 && n<=65536) {
                struct nvn_cbuf_bank {GLuint name;unsigned height;};
                static struct nvn_cbuf_bank banks[2][16];
                static GLint unit_limit[2];
                unsigned texels=n/16,height=(texels+1023)/1024;if(!height)height=1;
                if(!unit_limit[u->stage]){GLint q=0;glGetIntegerv(u->stage?GL_MAX_TEXTURE_IMAGE_UNITS:GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS,&q);if(!u->stage&&q>32)q=32; /* Mali vertex samplers: units above 31 are not usable */ unit_limit[u->stage]=q;}
                GLint unit=unit_limit[u->stage]-1-(GLint)u->binding;
                /* Ordinary samplers take units 0..samplers_total-1; a cbuf unit below that would alias one. */
                if(unit<0 || (unsigned)unit<samplers_total)stop("NVN_CBUF_TEXTURE_UNIT",nvn_caller,(uint64_t)unit,24);
                struct nvn_cbuf_bank *bank=&banks[u->stage][u->binding];
                glGetError();
                glActiveTexture(GL_TEXTURE0+unit);
                if(!bank->name) {
                    glGenTextures(1,&bank->name);
                    glBindTexture(GL_TEXTURE_2D,bank->name);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
                }
                glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,bank->name);
                if(bank->height<height) {
                    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,1024,height,0,GL_RGBA,GL_FLOAT,NULL);bank->height=height;
                }
                {static unsigned tv;const char *tve=getenv("NX_TILE_TRACE");if(tve&&*tve=='1'&&u->stage==0&&u->binding==2&&n>=64&&tv<33){printf("NVN_TILE draw=%u tx=%.6g ty=%.6g tz=%.6g s=%.6g\n",nvn_replay.draws+1,words[12],words[13],words[14],words[0]);tv++;}}
                if(nvn_flag_on("NX_CBUF_BANKDUMP")&&u->stage==0&&u->binding==0){static int dmp;if(!dmp){dmp=1;FILE *df=fopen("/tmp/nvn-c4.bin","wb");if(df){fwrite(words,1,n,df);fclose(df);printf("NVN_CBUF_BANKDUMP bytes=%u\n",n);}}}
                if(u->stage==0&&u->binding<8){nvn_cb_src[u->binding]=(const void*)((const char*)range.address+u->offset);nvn_cb_len[u->binding]=n;}
                if(nvn_flag_on("NX_CBUF_IDENTITY")&&n>=64&&u->stage==0&&(getenv("NX_CBUF_IDENTITY_BANK")==NULL||(int)u->binding==atoi(getenv("NX_CBUF_IDENTITY_BANK")))){
                  {int io=getenv("NX_CBUF_IDENTITY_OFF")?atoi(getenv("NX_CBUF_IDENTITY_OFF")):0;float *w=words+io;
                  w[0]=2.0f;w[1]=0;w[2]=0;w[3]=0;
                  w[4]=0;w[5]=2.0f;w[6]=0;w[7]=0;
                  w[8]=0;w[9]=0;w[10]=1.0f;w[11]=0;
                  w[12]=0;w[13]=0;w[14]=0;w[15]=1.0f;}
                  static unsigned idtold;if(!idtold){idtold=1;printf("NVN_CBUF_IDENTITY applied name=%s binding=%u off=%s n=%u\n",u->name,u->binding,getenv("NX_CBUF_IDENTITY_OFF")?getenv("NX_CBUF_IDENTITY_OFF"):"0",n);}}
                glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1024,height,GL_RGBA,GL_FLOAT,words);

                glUniform1i(location,unit);
                glActiveTexture(GL_TEXTURE0);
                                {GLenum e=glGetError();float maxabs=0.0f;for(unsigned wi=0;wi<n/4;wi++){float av=words[wi]<0?-words[wi]:words[wi];if(av>maxabs)maxabs=av;}static unsigned told;
                 if(e||told<8){if(!e)told++;GLint bound=-1;glGetUniformiv(program,location,&bound);
                  printf("NVN_CBUF_TEXTURE name=%s stage=%u binding=%u unit=%d bound=%d tex=%u size=%ux%u bytes=%u maxabs=%g error=0x%x\n",
                      u->name,u->stage,u->binding,unit,bound,bank->name,1024,height,n,maxabs,e);}}
                free(words);
                continue;
            }
#endif
            {static unsigned told;if(u->stage==0&&told<8){printf("NVN_UNIFORM_UPLOAD draw=%u binding=%u bytes=%u vecs=%u kind=%u\n",nvn_replay.draws+1,u->binding,n,n/16,u->kind);told++;}
             const char *bf=getenv("NX_BANK_FILL");
             if(bf&&*bf=='1'&&u->stage==0&&n>=384&&n%128==0){for(unsigned k=1;k<n/128;k++)memcpy(words+(size_t)k*32,words,128);
               printf("NVN_BANK_FILL binding=%u bytes=%u slots=%u\n",u->binding,n,n/128);}}
            if(location>=0)glUniform4fv(location,(GLsizei)(n/16),words);
            {GLenum e=glGetError();if(e)printf("NVN_UNIFORM_ARRAY_GL_ERROR bytes=%u vectors=%u error=0x%x\n",n,n/16,e);}
            free(words);
            continue;
        }
        {   /* MOMO_UNIFORM_DUMP=<draw>: the whole constant bank bound for that draw, so a shader whose
             * values look shifted can be checked against the raw buffer instead of guessed. */
            static int cfg;static unsigned wanted;static unsigned done;
            /* MOMO_UNIFORM_PAIR=<vertex>/<fragment>: the same dump keyed by the shader pair instead of the
             * draw number, because the draw number is NOT stable between runs (MEDIDO) while the pair is. */
            static unsigned pair_v,pair_f,pair_on,pair_after,pair_shots=16,pair_draw;
            if(!cfg){const char *o=getenv("MOMO_UNIFORM_DUMP");wanted=o?(unsigned)strtoul(o,NULL,0):0;
                const char *q=getenv("MOMO_UNIFORM_PAIR");
                if(q){char *end=NULL;pair_v=(unsigned)strtoul(q,&end,0);pair_f=end&&*end=='/'?(unsigned)strtoul(end+1,NULL,0):0;pair_on=1;}
                const char *a=getenv("MOMO_UNIFORM_AFTER");pair_after=a?(unsigned)strtoul(a,NULL,0):0;
                cfg=1;}
            if((wanted && nvn_replay.draws+1==wanted && !done) ||
               (pair_on && done<pair_shots && pair_draw!=nvn_replay.draws+1 && nvn_replay.last_frame+1>=pair_after && p->sources[0]==pair_v && p->sources[1]==pair_f) ||
               (diagnose && getenv("MOMO_DIAG_MASK") && !done)) {
                /* Once per DRAW, not once per uniform: this block sits inside the uniform loop. */
                if(pair_on && pair_draw!=nvn_replay.draws+1){done++;pair_draw=nvn_replay.draws+1;}
                printf("MOMO_UNIFORM_DUMP_AT draw=%u frame=%u pair=%u/%u target=%" PRIu64 " shot=%u\n",
                    nvn_replay.draws+1,nvn_replay.last_frame+1,p->sources[0],p->sources[1],
                    nvn_render.target?nvn_render.target->id:0,done);
                for(unsigned st=0;st<2;st++)for(unsigned bi=0;bi<4;bi++) {
                    struct nvn_range rg=nvn_render.uniform[st][bi];
                    if(!rg.size)continue;
                    printf("MOMO_UNIFORM_RANGE draw=%u stage=%u binding=%u address=0x%"PRIx64" size=%"PRIu64,
                        nvn_replay.draws+1,st,bi,rg.address,(uint64_t)rg.size);
                    /* Print past the bound size as well: the bank the shader reads may be a different
                     * binding than the one we map, and the neighbouring pool bytes show it. */
                    const float *head=nvn_pointer(rg.address,192,PROT_READ);
                    if(head)for(unsigned k=0;k<48;k++)printf("%s%.9g",k%4?" ":(k==(rg.size/4)?" || ":" | "),head[k]);
                    putchar('\n');
                }
                for(unsigned k=0;k<p->uniform_count;k++)
                    printf("MOMO_UNIFORM_ENTRY draw=%u name=%s stage=%u binding=%u offset=0x%x kind=%u\n",
                        nvn_replay.draws+1,p->uniforms[k].name,p->uniforms[k].stage,p->uniforms[k].binding,p->uniforms[k].offset,p->uniforms[k].kind);
                struct nvn_range range=nvn_render.uniform[u->stage][u->binding];
                printf("MOMO_UNIFORM_BUFFER draw=%u stage=%u binding=%u address=0x%"PRIx64" size=%"PRIu64,
                    nvn_replay.draws+1,u->stage,u->binding,range.address,(uint64_t)range.size);
                const float *raw=nvn_pointer(range.address,range.size<256?range.size:256,PROT_READ);
                if(raw)for(unsigned k=0;k<64 && k*4<range.size;k++)printf("%s%.9g",k%4?" ":k?" | ":" ",raw[k]);
                putchar('\n');
            }
        }
        if(!bytes || u->size!=bytes)stop("NVN_DRAW_UNIFORM_LAYOUT",nvn_caller,u->size,24);
        union {float values[16];uint64_t handles[8];} snapshot;
#ifdef INFERNAX_CBUF_SAMPLERS
        if(u->kind==3 && u->binding==14 && u->separate_sampler) {
            snapshot.handles[0]=infernax_separate_sampler_handle(u);
        } else
#endif
        if(u->kind==3 && u->binding==14) {
            /* SDK BindTexture writes its reserved CB at 0x20+8*slot.
             * Pinned machine TEXS/TLDS/TXQ adds that 0x20 header to the
             * source assembly offset. Keep the adapter's complete 64-bit
             * descriptor handle, rather than NVIDIA's packed low32 form. */
#ifdef MOMO_SHADERS
            unsigned aligned=1,slot=momo_texture_unit(u->offset,&aligned);
            if(u->stage>1 || !aligned || slot>=128 || !(nvn_render.texture_known[u->stage][slot/64]&(1ULL<<(slot%64)))) {
#ifdef INFERNAX_CBUF_SAMPLERS
                if(nvn_flag_on("NX_CBUF_AUDIT")) {
                    unsigned valid=u->stage<=1;
                    printf("INFERNAX_SAMPLER_MISSING program=%u pair=%u/%u name=%s stage=%u binding=%u offset=0x%x raw-offset=0x%x slot=%u aligned=%u location=%d effective-location=%d size-location=%d specialized=%u known0=0x%"PRIx64" known1=0x%"PRIx64" seq=%u writer=%u handle=0x%"PRIx64" reason=%s\n",
                        program,p->sources[0],p->sources[1],u->name,u->stage,u->binding,u->offset,u->offset+0x20,slot,aligned,u->location,location,u->size_location,specialized,
                        valid?nvn_render.texture_known[u->stage][0]:0,valid?nvn_render.texture_known[u->stage][1]:0,infernax_sampler_seq,
                        valid&&slot<128?infernax_sampler_writer[u->stage][slot]:0,valid&&slot<128?nvn_render.texture_handles[u->stage][slot]:0,
                        !valid?"BAD_STAGE":!aligned?"UNALIGNED":slot>=128?"OUT_OF_RANGE":"UNKNOWN_SLOT");
                    if(valid)for(unsigned k=0;k<4;k++)printf("INFERNAX_SAMPLER_SLOT stage=%u unit=%u handle=0x%"PRIx64" writer=%u\n",u->stage,k,nvn_render.texture_handles[u->stage][k],infernax_sampler_writer[u->stage][k]);
                }
#endif
                stop("NVN_LC2_TEXTURE_BINDING_UNDEFINED",nvn_caller,u->offset,20);
            }
#else
            unsigned slot=u->offset/8;
            if(u->stage>1 || u->offset%8 || slot>=128 || !(nvn_render.texture_known[u->stage][slot/64]&(1ULL<<(slot%64))))
                stop("NVN_LC2_TEXTURE_BINDING_UNDEFINED",nvn_caller,u->offset,20);
#endif
            snapshot.handles[0]=nvn_render.texture_handles[u->stage][slot];
        } else {
            struct nvn_range range=nvn_render.uniform[u->stage][u->binding];
            /* MOMO_UNIFORM_SHIFT=<vertex index>:<bytes>: read this program's vertex constants <bytes>
             * further into the bound buffer. Diagnostic for the UI draws whose bound buffer looks one
             * vec4 ahead of what the shader reads (MEDIDO run momo-173). */
            {   static int scfg;static unsigned ssource,sbytes;
                if(!scfg){const char *o=getenv("MOMO_UNIFORM_SHIFT");
                    if(o){char *end=NULL;ssource=(unsigned)strtoul(o,&end,0);sbytes=end&&*end==':'?(unsigned)strtoul(end+1,NULL,0):0;}
                    scfg=1;}
                /* ssource 0xfffe = every uniform of constant bank 4 (binding 1), whichever program reads it. */
                /* Constant bank 4 is a SECOND block inside the same bound memory, not a buffer of its own:
                 * Unity's UI draws bind one range at binding 0 (16 bytes here) and the shared UI buffer at
                 * binding 1, and the bank-4 shader's offsets start after the first block. Reading bank 4
                 * from the range start gave _ScreenParams where the shader wants the model matrix, so every
                 * UI mask collapsed and the whole menu was invisible (MEDIDO runs momo-173..185: with the
                 * base the mask covers the screen, the menu panel, the version line and the subtitle all
                 * appear). The base is taken from the guest's own binding, never a constant.
                 * OPEN: this is the shape the measurements support, not yet the proven NVN rule.
                 * Terminator 2D: measured on Unity 2021 only, so it is OPT-IN here (MOMO_BANK4_BASE=1);
                 * the default reads bank 4 from the range start until this title's binds are measured. */
                {   static int bcfg;static unsigned benabled;
                    if(!bcfg){const char *o=getenv("MOMO_BANK4_BASE");benabled=o&&!strcmp(o,"1");bcfg=1;}
                    if(benabled && u->binding==1) {
                        uint64_t base=nvn_render.uniform[u->stage][0].size;
                        if(base && range.size>base){range.address+=base;range.size-=base;}
                    }
                }
                if(sbytes && ((ssource==0xfffeU && u->binding==1) || (!u->stage && (ssource==0xffffU||p->sources[0]==ssource))) && range.size>sbytes) {
                    range.address+=sbytes;range.size-=sbytes;
                }
            }
            /* Animal Well binds constant buffers of exactly the bytes its shaders read (8 for a vec2 pair) while the
             * adapter declares whole 16-byte slots: the missing tail of a slot is read as zero, never past the buffer. */
            unsigned avail=u->offset>=range.size?0:(unsigned)(range.size-u->offset);if(avail>u->size)avail=u->size;
#ifdef MOMO_SHADERS
            /* The hardware reads the constant bank straight out of its memory pool: a slot past the
             * bound size is not an error, it is whatever the pool holds there. Unity 2021 binds 16
             * bytes for a skybox fragment whose code reads 32 (MEDIDO run momo-097). Read the pool
             * bytes when they are mapped, zeros otherwise, and never stop the draw. */
            if(!avail) {
                static unsigned told;
                if(told++<8)printf("MOMO_UNIFORM_PAST_BOUND program=%"PRIu64" name=%s stage=%u binding=%u offset=%u size=%u bound=%"PRIu64"\n",
                    p->id,u->name,u->stage,u->binding,u->offset,u->size,range.size);
                memset(&snapshot,0,sizeof(snapshot));
                const void *tail=range.address?nvn_gpu_pointer_or_null(range.address+u->offset,u->size,PROT_READ):NULL;
                if(tail)memcpy(&snapshot,tail,u->size);
                avail=0;goto momo_uniform_ready;
            }
#endif
            if(!avail) {
                printf("AW_UNIFORM_RANGE program=%"PRIu64" name=%s stage=%u binding=%u offset=%u size=%u bound-size=%"PRIu64" path=render\n",p->id,u->name,u->stage,u->binding,u->offset,u->size,range.size);
                for(unsigned sx=0;sx<2;sx++)for(unsigned bx=0;bx<16;bx++)if(nvn_render.uniform[sx][bx].size)printf("AW_UNIFORM_BOUND stage=%u index=%u size=%"PRIu64"\n",sx,bx,nvn_render.uniform[sx][bx].size);
                stop("NVN_DRAW_UNIFORM_RANGE",nvn_caller,u->offset,24);
            }
            memset(&snapshot,0,sizeof(snapshot));
#ifdef ANIMAL_WELL_SHADERS
            snprintf(aw_trace_context_buffer,sizeof(aw_trace_context_buffer),"draw=%u pair=%u/%u uniform=%s kind=%u stage=%u binding=%u offset=%u size=%u bound=%"PRIu64,nvn_replay.draws,p->sources[0],p->sources[1],u->name,u->kind,u->stage,u->binding,u->offset,u->size,range.size);
            aw_trace_context=aw_trace_context_buffer;
#endif
            if(p->wave_contract && u->stage==0 && u->binding==0 && u->offset+u->size<=sizeof(wave.constants))
                memcpy(&snapshot,(const uint8_t *)wave.constants+u->offset,u->size);
            else memcpy(&snapshot,nvn_gpu_pointer(range.address+u->offset,avail,PROT_READ),avail);
#ifdef MOMO_SHADERS
            momo_uniform_ready: ;
#endif
        }
#ifdef ANIMAL_WELL_SHADERS
        /* Animal Well constant buffers mix float and integer words; the lowering reads both through the float-typed
         * block. An integer word read as float is a denormal/tiny value or NaN (never a game float), so convert those
         * components to (float)(int32)word. Real floats (including 0) pass unchanged; the static word_int mask only logs. */
        if(u->kind==2||u->kind==4||u->kind==5) {
            for(unsigned c=0;c<u->size/4;c++) {
                uint32_t word;memcpy(&word,&snapshot.values[c],4);
                if(!word)continue;
                unsigned exponent=(word>>23)&0xff;
                if(exponent>=0x20 && exponent!=0xff)continue;
                int32_t sw;memcpy(&sw,&word,4);snapshot.values[c]=(float)sw;
                static unsigned logged;
                if(logged<24 && !(u->word_int&(1U<<c))){logged++;printf("AW_WORD_INT_VALUE name=%s component=%u word=0x%x static-mask=%u\n",u->name,c,word,u->word_int);}
            }
        }
#endif
        const void *data=&snapshot;nvn_uniform_profile.guest_reads++;nvn_uniform_profile.guest_bytes+=u->size;
        nvn_texdiag_uniform(i,u->stage,u->binding,u->offset,u->kind,location,u->name,data,u->size);
        if(diagnose || t2_pass_probe_active()) {
            printf("NVN_DRAW_UNIFORM name=%s location=%d bytes=%u data=",u->name,location,u->size);
            if(u->kind==3){uint64_t handle;memcpy(&handle,data,8);printf("%016"PRIx64,handle);}
            else for(unsigned k=0;k<u->size/4;k++){float value;memcpy(&value,(const uint8_t *)data+k*4,4);printf(" %g",value);}
            putchar('\n');
        }
        if(u->kind==1)nvn_uniform_send(location,NVN_UNIFORM_MATRIX,data);
        else if(u->kind==2)nvn_uniform_send(location,NVN_UNIFORM_VECTOR,data);
        else if(u->kind==4)nvn_uniform_send(location,NVN_UNIFORM_FLOAT,data);
        else if(u->kind==5)nvn_uniform_send(location,NVN_UNIFORM_VECTOR2,data);
        else if(u->kind==6){uint32_t word;memcpy(&word,data,4);float flag=word!=0;nvn_uniform_send(location,NVN_UNIFORM_FLOAT,&flag);}
        else {uint64_t handle;memcpy(&handle,data,8);if(units>=(unsigned)nvn_device.texture_units)stop("NVN_DRAW_TEXTURE_UNITS",nvn_caller,units,24);
            /* A null texture handle samples black on hardware (incomplete texture -> 0,0,0,1).
             * Bind unit 0 that way instead of running the texture path with no object. */
            if(!handle) {
                static unsigned told;
                if(told++<8)printf("NVN_NULL_SAMPLER_BIND unit=%u name=%s note=black-sample\n",units,u->name);
                glActiveTexture(GL_TEXTURE0+units);glBindTexture(GL_TEXTURE_2D,0);
                continue;
            }
            GLint min_filter;GLenum sampler_wrap[2];struct nvn_texture *texture=nvn_render_sampler(handle,units,u->cpu_view,real_sample,&min_filter,sampler_wrap);
#ifdef ANIMAL_WELL_SHADERS
            {
                const struct mm_shader_binding *fb=&nvn_megaman_shader_bindings[p->sources[1]];
                unsigned j=0;while(j<fb->sampler_count && strcmp(fb->samplers[j].name,u->name))j++;
                unsigned expected=j<fb->sampler_count?aw_float_kind_from_code((p->aw_float_key>>(2*j))&3):0;
                if(expected!=texture->aw_float_kind)stop("AW_FLOAT_SAMPLER_VARIANT_MISMATCH",nvn_caller,texture->id,20);
                if(p->aw_light_tiles_location>=0){GLfloat tiles=(GLfloat)(texture->aw_draws_since_clear>16?16:texture->aw_draws_since_clear);nvn_uniform_send(p->aw_light_tiles_location,NVN_UNIFORM_FLOAT,&tiles);}
                static int pcfg;static unsigned pframe;
                if(!pcfg){const char *o=getenv("AW_DRAW_PROBE");pframe=o?(unsigned)strtoul(o,NULL,10):0;pcfg=1;}
                if(pframe && nvn_replay.last_frame+1==pframe)
                    printf("AW_PROBE_SAMPLER draw=%u pair=%u/%u name=%s texture=%"PRIu64" size=%ux%u fmt=0x%x/%u min=0x%x cpu-view=%u draws=%u\n",
                        nvn_replay.draws,p->sources[0],p->sources[1],u->name,texture->id,texture->b.width,texture->b.height,texture->b.format,texture->aw_float_kind,(unsigned)min_filter,u->cpu_view,texture->draws);
            }
#endif
#if ZERO_ZX_GAME
#ifdef ANIMAL_WELL_SHADERS
            if(!p->zero_r8_wrapper && texture->b.format==1)stop("AW_R8_SAMPLE_WITHOUT_WRAPPER",nvn_caller,texture->id,20);
#endif
            if(p->zero_r8_wrapper) {
                /* The real NVN selectors of an R8 image, not a two-case guess: Unity binds its Alpha8
                 * atlases as (ZERO,ZERO,ZERO,R) and the old shape answered (R,0,0,1), so their alpha was a
                 * constant 1. Anything that is not an R8 image keeps the identity and is untouched. */
                /* The selector arithmetic happens here, once per draw, not per fragment: an R8 image has
                 * no G/B (ZERO) and no A (ONE), so every NVN selector is either "take R" or a constant and
                 * the unswizzle is mul*v.r + add. `use` is 0 for anything that is not an R8 image. */
                unsigned r8=texture->b.format==1 && !u->cpu_view;
                GLfloat mul[4]={0,0,0,0},add[4]={0,0,0,0},use=r8?1.0f:0.0f;
                if(r8)for(unsigned c=0;c<4;c++) {
                    unsigned selector=texture->b.swizzle[c];
                    if(selector==2)mul[c]=1.0f;
                    else if(selector==1||selector==5)add[c]=1.0f;
                    else if(selector>2 && selector<5)add[c]=0.0f;   /* G and B do not exist in an R8 image */
                }
                if(r8 && (u->zero_r8_mode<0||u->zero_r8_add<0||u->zero_r8_use<0))
                    stop("ZERO_R8_SAMPLING_PATH_REQUIRED",nvn_caller,texture->id,20);
                nvn_uniform_send(u->zero_r8_mode,NVN_UNIFORM_VECTOR,mul);
                nvn_uniform_send(u->zero_r8_add,NVN_UNIFORM_VECTOR,add);
                nvn_uniform_send(u->zero_r8_use,NVN_UNIFORM_FLOAT,&use);
            }
            if(!p->zero_mip_wrapper && zero_mip_partial(texture) && min_filter!=GL_NEAREST && min_filter!=GL_LINEAR)
                stop("ZERO_MIP_WRAPPER_DISABLED_PARTIAL",nvn_caller,texture->id,20);
            if(p->zero_mip_wrapper) {
                unsigned active=zero_mip_partial(texture) && min_filter!=GL_NEAREST && min_filter!=GL_LINEAR;
                if(active && (u->cpu_view || u->zero_mip_mask<0 || u->zero_mip_last<0))
                    stop("ZERO_MIP_SAMPLING_PATH_REQUIRED",nvn_caller,texture->id,20);
                if(u->zero_mip_mask>=0) {
                    /* The mask companions take units from the TOP of the range downwards: the real samplers
                     * grow from 0, so the two only meet when a program genuinely needs more than the hardware
                     * has. A fixed base of 4 (imported from Zero/ZX, where no shader used more than four
                     * samplers) collided at the eighth unit on Momodora's pair 1252/459 (MEDIDO run momo-107). */
                    /* An INACTIVE companion costs no unit at all: it points at the sampler's own unit, which is
                     * about to receive this very texture, and `last=-1` keeps the shader off that path. Only a
                     * live mask needs a unit of its own, taken from the TOP of the range downwards (the real
                     * samplers grow from 0). Momodora's pair 1252/459 has 6 samplers and no live mask; the old
                     * fixed base of 4 — imported from Zero/ZX, where no shader used more than four samplers —
                     * ran out at the eighth unit (MEDIDO run momo-107/108). */
                    GLint mask_unit=active?nvn_device.texture_units-1-(GLint)u->zero_mip_slot:(GLint)units;
                    if(active && mask_unit<(GLint)(samplers_total+2*t2_page_key_count(p->t2_page_key)))stop("ZERO_MIP_TEXTURE_UNITS",nvn_caller,(uint64_t)mask_unit,20);
                    if(active) {
                        glActiveTexture(GL_TEXTURE0+mask_unit);
                        glBindTexture(GL_TEXTURE_2D,zero_mip_mask_texture(texture,min_filter));
                    }
                    float last=active?(float)texture->b.levels-1:-1.0f;
                    nvn_uniform_send(u->zero_mip_mask,NVN_UNIFORM_INT,&mask_unit);
                    nvn_uniform_send(u->zero_mip_last,NVN_UNIFORM_FLOAT,&last);
                    if(active)glActiveTexture(GL_TEXTURE0+units);
                }
            }
#endif
            nvn_rt_storage_sample(texture,p->sources[0],p->sources[1],u->cpu_view,u->size_location>=0,u->texel_fetch_bits);
            if(p->wave_contract){
                if(wave.source || u->cpu_view || u->stage!=1 || u->binding!=14 || u->offset)
                    stop("NVN_LC2_WAVE_SOURCE_CONTRACT",nvn_caller,handle,20);
                wave.source=texture;
            }
            if(u->swizzle_location>=0) {
                if(specialized)stop("MOMO_SWIZZLE_VARIANT_REQUIRED",nvn_caller,handle,20);
                /* The CPU decode already bakes the selectors into the uploaded RGBA8 (nvn_texture_formats.h),
                 * so an uploaded image samples with the identity. A GPU-written image keeps its native
                 * storage and needs the real selectors after the sample. */
                GLfloat selectors[4]={2,3,4,5};
                if(texture->composition_target_seen)
                    for(unsigned c=0;c<4;c++)selectors[c]=(GLfloat)texture->b.swizzle[c];
                nvn_uniform_send(u->swizzle_location,NVN_UNIFORM_VECTOR,selectors);
                if(selectors[0]!=2.0f||selectors[1]!=3.0f||selectors[2]!=4.0f||selectors[3]!=5.0f) {
                    /* With the flattened helper the shader can no longer apply a real selector, so the
                     * host stops here instead of drawing the wrong colour quietly. */
                    if(momo_swizzle_flat_enabled())stop("MOMO_SWIZZLE_FLAT_SELECTOR_REQUIRED",nvn_caller,texture->id,20);
                    static unsigned reported;
                    if(reported++<8)printf("MOMO_SWIZZLE_NON_IDENTITY id=%"PRIu64" format=0x%x selectors=%g/%g/%g/%g target-seen=%u\n",
                        texture->id,texture->b.format,selectors[0],selectors[1],selectors[2],selectors[3],texture->composition_target_seen);
                }
            }
            /* The TextMeshPro colour choice needs the format of the ATLAS sampler, which is the one the
             * fragment reads its alpha from: a single-channel page is a font, anything else is a sprite. */
            if(p->momo_tmp_sprite>=0 && !strcmp(u->name,"fs_tex2_168"))momo_tmp_sprite_atlas=texture->b.format;
            momo_texture_stats(texture,p->sources[0],p->sources[1],units,nvn_replay.last_frame+1);
            nvd_gl_refresh(texture);
            if(pass_texture_count<8){pass_textures[pass_texture_count]=texture->id;pass_handles[pass_texture_count]=handle;
                unsigned al=1;pass_slots[pass_texture_count]=momo_texture_unit(u->offset,&al);pass_offsets[pass_texture_count]=u->offset;pass_names[pass_texture_count]=u->name;pass_sizes[pass_texture_count][0]=texture->b.width;pass_sizes[pass_texture_count][1]=texture->b.height;pass_texture_count++;}
            GLint unit=units++;nvn_uniform_send(location,NVN_UNIFORM_INT,&unit);
            {   /* Frente F: T2_DRAW_TEXTURE_FRAMES=240:360 (qemu -E splits on commas) lists every sampler binding of those frames (texture, page, mode). */
                static int cfg;static unsigned frames[8],nframes,every;
                if(!cfg){cfg=1;const char *period=getenv("T2_DRAW_TEXTURE_EVERY");every=period?(unsigned)strtoul(period,NULL,10):0;const char *o=getenv("T2_DRAW_TEXTURE_FRAMES");
                    for(const char *q=o;q&&*q&&nframes<8;){char *e;unsigned long v=strtoul(q,&e,10);if(e==q)break;frames[nframes++]=(unsigned)v;q=(*e==','||*e==':')?e+1:e;}}
                unsigned trace=t2_pass_probe_active() || (every && (nvn_replay.last_frame+1)%every==0);
                for(unsigned k=0;k<nframes;k++)if(frames[k]==nvn_replay.last_frame+1)trace=1;
                if(trace && texture->pool && (uint64_t)texture->b.width*texture->b.height<=16) {
                    uint64_t tbpp=nvn_texture_format_bytes(texture->b.format);
                    uint64_t tstride=(texture->b.flags&0x50)?texture->b.stride:(uint64_t)texture->b.width*tbpp;
                    if(!tstride)tstride=16;
                    const uint8_t *gs=nvn_pointer(texture->pool->address+texture->b.offset+texture->levels[0],tstride,PROT_READ);
                    printf("T2_TEXG frame=%u id=%"PRIu64" off=0x%"PRIx64" packaged=%u levels=%u dirty=%u now=%u,%u,%u,%u\n",
                           nvn_replay.last_frame+1,texture->id,texture->b.offset,texture->packaged_uploaded,texture->b.levels,texture->zero_cpu_dirty,
                           gs[0],gs[1],gs[2],gs[3]);
                }
                if(trace)
                    printf("T2_DRAW_TEXTURE frame=%u draw=%u pair=%u/%u program=%u sampler=%s unit=%d id=%"PRIu64" size=%ux%u format=0x%x page=%s mode=%u block=%u palettes=%u min=0x%x target=%"PRIu64"\n",
                        nvn_replay.last_frame+1,nvn_replay.draws,p->sources[0],p->sources[1],program,u->name,unit,texture->id,texture->b.width,texture->b.height,texture->b.format,
                        texture->t2_page.mode?texture->t2_page.name:"-",texture->t2_page.mode,texture->t2_page.block,texture->t2_page.palettes,(unsigned)min_filter,nvn_render.target?nvn_render.target->id:0);
            }
            /* Frente F: the palette (and block map) of a palettized page ride on the units above the real
             * samplers, fixed per variant at link time (t2_page_palette_unit/t2_page_blockmap_unit). */
            if(texture->t2_page.mode || (p->t2_page_key&(1u<<u->t2_sampler))) {
                if(!texture->t2_page.mode || !(p->t2_page_key&(1u<<u->t2_sampler)) || u->t2_palette<0 || specialized || texture->t2_page.mode!=p->t2_page_mode)
                    stop("T2_PAGE_VARIANT_MISMATCH",nvn_caller,texture->id,24);
                GLint pu=t2_page_palette_unit(samplers_total,p->t2_page_key,u->t2_sampler),bu=t2_page_blockmap_unit(samplers_total,p->t2_page_key,u->t2_sampler);
                if(bu>=nvn_device.texture_units)stop("T2_PAGE_TEXTURE_UNITS",nvn_caller,(uint64_t)bu,20);
                glActiveTexture(GL_TEXTURE0+pu);glBindTexture(GL_TEXTURE_2D,texture->t2_page.palette_name);nvn_uniform_send(u->t2_palette,NVN_UNIFORM_INT,&pu);
                if(texture->t2_page.mode!=T2_PAGE_MODE_IDX16){
                    glActiveTexture(GL_TEXTURE0+bu);glBindTexture(GL_TEXTURE_2D,texture->t2_page.blockmap_name);
                    unsigned compact=texture->t2_page.mode==T2_PAGE_MODE_COMPACT;
                    t2_page_wrap_apply(&texture->t2_page.blockmap_wrap,compact?GL_CLAMP_TO_EDGE:sampler_wrap[0],compact?GL_CLAMP_TO_EDGE:sampler_wrap[1]);
                    nvn_uniform_send(u->t2_blockmap,NVN_UNIFORM_INT,&bu);
                    if(compact) {
                        if(!texture->t2_page.compact_height || u->t2_compact<0)stop("T2_COMPACT_DRAW_STATE",nvn_caller,texture->id,24);
                        GLfloat params[4]={1.0f/texture->t2_page.compact_height,(GLfloat)texture->t2_page.flipped,
                            sampler_wrap[0]==GL_REPEAT?1.0f:sampler_wrap[0]==GL_MIRRORED_REPEAT?2.0f:0.0f,
                            sampler_wrap[1]==GL_REPEAT?1.0f:sampler_wrap[1]==GL_MIRRORED_REPEAT?2.0f:0.0f};
                        nvn_uniform_send(u->t2_compact,NVN_UNIFORM_VECTOR,params);
                        if(u->t2_compact4>=0) {
                            GLfloat rows[4]={(GLfloat)texture->t2_page.compact_rows[0],(GLfloat)texture->t2_page.compact_rows[1],(GLfloat)texture->t2_page.compact_rows[2],0.0f};
                            nvn_uniform_send(u->t2_compact4,NVN_UNIFORM_VECTOR,rows);
                        }
                    }
                }
                glActiveTexture(GL_TEXTURE0+unit);
                static unsigned told;
                if(told++<4)printf("T2_PAGE_DRAW page=%s sampler=%s unit=%d palette-unit=%d blockmap-unit=%d program=%u pair=%u/%u\n",texture->t2_page.name,u->name,unit,pu,bu,program,p->sources[0],p->sources[1]);
            }
            /* Non-default views are rejected when descriptors are registered.
             * TXQ therefore reads the actual base level of this exact handle. */
            if(u->size_location>=0) {
                if(texture->host_half||(texture->host_scaled && (!zero_render_scale_enabled() || u->texel_fetch_bits)))
                    stop("NVN_LC2_TXQ_RESIZED_STORAGE",nvn_caller,handle,20);
#if ZERO_ZX_SHADER_PACKAGES
                /* Zero's size-query metadata currently marks the SPIR-V
                 * ConstOffset lowering. Its texel step must be level zero. */
                if(!u->texel_fetch_bits && texture->b.levels!=1)
                    stop("ZERO_TEXTURE_OFFSET_MIPMAP_REQUIRED",nvn_caller,texture->id,20);
#endif
                unsigned width=u->cpu_view?nvn_texture_view_width(texture->b.format,u->cpu_view,texture->b.width):texture->b.width;
                GLfloat dimensions[2]={(GLfloat)width,(GLfloat)texture->b.height};
                nvn_uniform_send(u->size_location,NVN_UNIFORM_VECTOR2,dimensions);
            }}
    }
    nvn_texdiag_draw_end();
    if(diagnose) {
        GLint bound=0,min=0,mag=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,&min);glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,&mag);
        printf("NVN_DRAW_GL_TEXTURE bound=%d min=%x mag=%x\n",bound,min,mag);
        for(unsigned i=0;i<p->uniform_count;i++)if(p->uniforms[i].kind==3&&p->uniforms[i].location>=0){GLint unit=-1,location=specialized?p->alpha5.locations[i]:p->uniforms[i].location;glGetUniformiv(program,location,&unit);printf("NVN_DRAW_GL_SAMPLER name=%s location=%d unit=%d\n",p->uniforms[i].name,location,unit);}
    }
    uint64_t *a=r->args;unsigned primitive=a[0];
    /* NVN primitive 7 is QUADS, which GLES2 does not have; Unity 2021 uses it for its sprite/UI
     * batches (MEDIDO run momo-067). Each quad becomes the two triangles (0,1,2) and (0,2,3) of the
     * same four vertices, so the rasterized area and the vertex data are unchanged. */
    unsigned quads=primitive==7;
    if(primitive>6 && !quads)stop("NVN_DRAW_PRIMITIVE_REQUIRED",nvn_caller,primitive,20);
    uint64_t base=0,maximum=0,count;const uint16_t *indices=NULL;
    if(r->op==NI_nvnCommandBufferDrawArrays) {base=a[1];count=a[2];maximum=count?count-1:0;}
    else {
        unsigned type=a[1];count=a[2];if(type>2 || count>1048576)stop("NVN_INDEX_RANGE",nvn_caller,count,24);
        const uint8_t *src=nvn_gpu_pointer(a[3],count*(1U<<type),PROT_READ);
        int64_t vertex_base=r->op==NI_nvnCommandBufferDrawElementsBaseVertex?(int32_t)a[4]:0;
        if(!nvn_index_direct_u16(type,count,src,vertex_base,&base,&maximum,&indices)) {
        uint32_t min=UINT32_MAX,max=0;
        for(uint64_t i=0;i<count;i++){uint32_t v=type==0?src[i]:type==1?(uint32_t)src[i*2]|((uint32_t)src[i*2+1]<<8):read32(src+i*4);if(v<min)min=v;if(v>max)max=v;}
        if(!count)return;
        if(max-min>65535)stop("NVN_INDEX_REBASE_SPAN",nvn_caller,max-min,20);
        int64_t signed_base=(r->op==NI_nvnCommandBufferDrawElementsBaseVertex?(int32_t)a[4]:0)+(int64_t)min;
        if(signed_base<0)stop("NVN_INDEX_NEGATIVE_VERTEX",nvn_caller,signed_base,24);
        base=signed_base;maximum=max-min;uint16_t *scratch=nvn_render_index_scratch(count);indices=scratch;
        for(uint64_t i=0;i<count;i++){uint32_t v=type==0?src[i]:type==1?(uint32_t)src[i*2]|((uint32_t)src[i*2+1]<<8):read32(src+i*4);scratch[i]=v-min;}
        }
    }
    if(p->momo_tmp_sprite>=0) {
        /* A single-channel page cannot carry colour, so it is a font and the colour belongs to the face
         * texture; anything else is a sprite atlas and carries the picture. */
        GLfloat sprite=momo_tmp_sprite_atlas && momo_tmp_sprite_atlas!=1?1.0f:0.0f;
        nvn_uniform_send(p->momo_tmp_sprite,NVN_UNIFORM_FLOAT,&sprite);
    }
    if(nvn_phase.enabled){uint64_t aw_t4=aw_setup_now();aw_setup_ns.resolve+=aw_t1-aw_t0;aw_setup_ns.prestate+=aw_t2-aw_t1;aw_setup_ns.state+=aw_t3-aw_t2;aw_setup_ns.uniforms+=aw_t4-aw_t3;aw_setup_ns.n++;}
    if(quads) {
        if(count%4)stop("MOMO_QUAD_VERTEX_COUNT",nvn_caller,count,24);
        uint64_t triangles=count/4*6;
        if(triangles>3145728)stop("MOMO_QUAD_INDEX_CAPACITY",nvn_caller,triangles,24);
        static uint16_t *quad_scratch;static size_t quad_capacity;
        if(quad_capacity<triangles) {
            uint16_t *grown=realloc(quad_scratch,triangles*sizeof(*quad_scratch));
            if(!grown)stop("MOMO_QUAD_INDEX_MEMORY",nvn_caller,triangles,21);
            quad_scratch=grown;quad_capacity=triangles;
        }
        for(uint64_t q=0;q<count/4;q++) {
            uint16_t v[4];
            for(unsigned k=0;k<4;k++) {
                uint64_t at=q*4+k;
                uint64_t index=indices?indices[at]:at;
                if(index>65535)stop("MOMO_QUAD_INDEX_RANGE",nvn_caller,index,24);
                v[k]=(uint16_t)index;
            }
            uint16_t *out=quad_scratch+q*6;
            out[0]=v[0];out[1]=v[1];out[2]=v[2];out[3]=v[0];out[4]=v[2];out[5]=v[3];
        }
        indices=quad_scratch;count=triangles;primitive=GL_TRIANGLES;
        if(!nvn_render.momo_quads_reported++)
            printf("MOMO_QUAD_DRAW converted=triangles vertices-per-quad=4 indices-per-quad=6\n");
    }
    if(count>1048576||base>UINT32_MAX)stop("NVN_DRAW_COUNT",nvn_caller,count,24);
    if(!count)return;
    zero_advent_mali_draw_require(p,base,maximum,count,indices);
    if(p->wave_contract) {
        if(maximum!=3 || (primitive!=GL_TRIANGLES&&primitive!=GL_TRIANGLE_STRIP&&primitive!=GL_TRIANGLE_FAN) ||
           (count!=4 && !(indices&&count==6&&primitive==GL_TRIANGLES)))
            stop("NVN_LC2_WAVE_GEOMETRY",nvn_caller,count,20);
        unsigned referenced=0;for(uint64_t k=0;k<count;k++)referenced|=1U<<(indices?indices[k]:(unsigned)k);
        if(referenced!=15)stop("NVN_LC2_WAVE_VERTEX_SET",nvn_caller,referenced,20);
    }
    {   /* MOMO_SAMPLE_UNIFORMS=N: every 60th frame, the first floats of the uniform banks feeding this
         * draw. A screen that is frozen and a screen that is animating look the same through an error
         * shader; the guest's own uniforms tell them apart. */
        static int configured;static unsigned budget,seen;static unsigned last_frame;
        if(!configured){const char *o=getenv("MOMO_SAMPLE_UNIFORMS");budget=o?(unsigned)strtoul(o,NULL,0):0;configured=1;}
        if(seen<budget && nvn_replay.last_frame!=last_frame && nvn_replay.last_frame%60==0) {
            last_frame=nvn_replay.last_frame;seen++;
            for(unsigned stage=0;stage<2;stage++) {
                struct nvn_range range=nvn_render.uniform[stage][0];
                if(!range.address || range.size<32)continue;
                const float *values=nvn_gpu_pointer(range.address,32,PROT_READ);
                printf("MOMO_UNIFORMS frame=%u pair=%u/%u stage=%u bytes=%"PRIu64" v=%g,%g,%g,%g,%g,%g,%g,%g\n",
                    nvn_replay.last_frame+1,p->sources[0],p->sources[1],stage,range.size,
                    values[0],values[1],values[2],values[3],values[4],values[5],values[6],values[7]);
            }
        }
    }
    {   /* MOMO_TRACE_DRAWS=N: the first N draws of the run, with the linked pair and the target, so a
         * wrong full-screen pass can be attributed to one shader pair. */
        static int configured;static unsigned budget,seen;
        if(!configured){const char *o=getenv("MOMO_TRACE_DRAWS");budget=o?(unsigned)strtoul(o,NULL,0):0;configured=1;}
        if(seen<budget) {
            seen++;
            printf("MOMO_DRAW frame=%u draw=%u pair=%u/%u primitive=%u count=%"PRIu64" target=%"PRIu64" size=%ux%u"
                   " fs-textures=0x%"PRIx64",0x%"PRIx64" known=0x%x uniforms=%u op=%u index-type=%"PRIu64
                   " base=%"PRIu64" maximum=%"PRIu64" indexed=%u attribs=%u\n",
                nvn_replay.last_frame+1,nvn_replay.draws,p->sources[0],p->sources[1],primitive,count,
                nvn_render.target?nvn_render.target->id:0,
                nvn_render.target?nvn_render.target->b.width:0,nvn_render.target?nvn_render.target->b.height:0,
                nvn_render.texture_handles[1][0],nvn_render.texture_handles[1][1],(unsigned)nvn_render.texture_known[1][0],p->uniform_count,
                r->op,r->op==NI_nvnCommandBufferDrawArrays?(uint64_t)0xff:a[1],base,maximum,indices!=NULL,nvn_render.attrib_count);
        }
    }
    if(diagnose)printf("NVN_DRAW_GEOMETRY frame=%u draw=%u op=%u primitive=%u count=%"PRIu64" base=%"PRIu64" maximum=%"PRIu64" indexed=%u\n",
        nvn_replay.last_frame+1,nvn_replay.draws,r->op,primitive,count,base,maximum,indices!=NULL);
    if((nvn_replay.black_after_proof==119 || t2_pass_probe_active()) && indices)
        nvn_private_dump("draw-indices",nvn_replay.draws,indices,(size_t)count*sizeof(*indices));
    nvn_phase_end(NPH_SETUP);
    if(nvn_phase.enabled&&(nvn_replay.last_frame==3000||nvn_replay.last_frame==3001)) {
        uint64_t total=0;for(unsigned i=0;i<(unsigned)nvn_device.attributes&&i<nvn_render.attrib_count;i++){uint32_t attr=nvn_render.attrib[i];if(!(attr&64))continue;unsigned stream=attr&31;if(stream<16)total+=maximum*nvn_render.streams[stream][0]+nvn_render.streams[stream][0];}
        printf("NVN_DRAWGEOM frame=%u draw=%u indexed=%u count=%"PRIu64" base=%"PRIu64" maximum=%"PRIu64" attribs=%u bytes-per-attrib-extent=%"PRIu64" program=%u/%u target=%ux%u\n",
            nvn_replay.last_frame+1,nvn_replay.draws,indices!=NULL,count,base,maximum,nvn_render.attrib_count,total,p->sources[0],p->sources[1],nvn_render.target->b.width,nvn_render.target->b.height);
    }
    /* MOMO_ATTR_PAIR=<vs>/<fs> with MOMO_ATTR_AFTER=<frame>: the first vertices of every attribute of
     * that shader pair, late in the run. "The fragment computes the wrong colour" ends at the vertex
     * colour: when the uniforms are all ones and the sprite still comes out invisible, the attribute is
     * the only thing left to read. Up to eight draws, then it goes quiet. */
    unsigned attr_pair=t2_pass_probe_active();
    {   static int cfg;static unsigned pv,pf,on,after,shots;
        if(!cfg){const char *q=getenv("MOMO_ATTR_PAIR");
            if(q){char *end=NULL;pv=(unsigned)strtoul(q,&end,0);pf=end&&*end=='/'?(unsigned)strtoul(end+1,NULL,0):0;on=1;}
            const char *a2=getenv("MOMO_ATTR_AFTER");after=a2?(unsigned)strtoul(a2,NULL,0):0;cfg=1;}
        if(on && shots<4 && nvn_replay.last_frame+1>=after && p->sources[0]==pv && p->sources[1]==pf) {
            attr_pair=1;shots++;
            printf("MOMO_ATTR_AT draw=%u frame=%u pair=%u/%u target=%" PRIu64 " vertices=%" PRIu64 " textures=%u",
                nvn_replay.draws,nvn_replay.last_frame+1,p->sources[0],p->sources[1],
                nvn_render.target?nvn_render.target->id:0,maximum+1,pass_texture_count);
            for(unsigned k=0;k<pass_texture_count;k++)
                printf(" %s=id%" PRIu64 "/%ux%u",pass_names[k],pass_textures[k],pass_sizes[k][0],pass_sizes[k][1]);
            putchar('\n');
            /* The sampled textures themselves: a 4x4 atlas that is black turns every solid UI rect black
             * without any geometry or state defect. */
            for(unsigned k=0;k<pass_texture_count && k<4;k++) {
                GLuint tn=(GLuint)pass_textures[k];
                if(!tn)continue;
                GLuint tf=0;glGenFramebuffers(1,&tf);glBindFramebuffer(GL_FRAMEBUFFER,tf);
                glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tn,0);
                GLenum tst=glCheckFramebufferStatus(GL_FRAMEBUFFER);
                unsigned char tpx[64]={0};
                if(tst==GL_FRAMEBUFFER_COMPLETE)glReadPixels(0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,tpx);
                printf("MOMO_ATTR_TEX draw=%u %s=id%u status=0x%x px0=%u,%u,%u,%u px1=%u,%u,%u,%u\n",
                       nvn_replay.draws,pass_names[k],tn,(unsigned)tst,tpx[0],tpx[1],tpx[2],tpx[3],tpx[4],tpx[5],tpx[6],tpx[7]);
                glBindFramebuffer(GL_FRAMEBUFFER,nvn_render.framebuffer);glDeleteFramebuffers(1,&tf);
                /* Guest memory right now, not at creation: tells a texture the game fills later (our copy
                 * is stale) from one the game leaves black on purpose. */
                struct nvn_texture *tt=nvn_texture_find(pass_handles[k]);   /* find() keys on the guest object, not on id */
                if(tt&&tt->pool) {
                    uint64_t bpp=nvn_texture_format_bytes(tt->b.format);
                    uint64_t stride=(tt->b.flags&0x50)?tt->b.stride:(uint64_t)tt->b.width*bpp;
                    if(!stride)stride=16;
                    const uint8_t *gs=nvn_pointer(tt->pool->address+tt->b.offset+tt->levels[0],stride,PROT_READ);
                    printf("MOMO_ATTR_TEXG draw=%u id=%" PRIu64 " target=%u fmt=0x%x off=0x%" PRIx64 " packaged=%u dirty=%u guestnow=%u,%u,%u,%u\n",
                           nvn_replay.draws,tt->id,tt->b.target,tt->b.format,tt->b.offset,tt->packaged_uploaded,tt->zero_cpu_dirty,
                           gs[0],gs[1],gs[2],gs[3]);
                }
            }
            /* The matching draw's uniform banks. A quad that collapses to a pixel is a scale/offset
             * uniform that arrived as zero, not geometry the guest asked for. */
            for(unsigned st=0;st<2;st++)for(unsigned bank=0;bank<16;bank++) {
                struct nvn_range ur=nvn_render.uniform[st][bank];
                if(!ur.address||ur.size<16)continue;
                uint64_t shown=ur.size<160?ur.size:160;
                const float *uv=nvn_gpu_pointer(ur.address,shown,PROT_READ);
                printf("MOMO_ATTR_UNI draw=%u stage=%u bank=%u bytes=%" PRIu64 " v=",nvn_replay.draws,st,bank,ur.size);
                for(uint64_t k=0;k<shown/4;k++)printf("%s%g",k?",":"",uv[k]);
                putchar('\n');
            }
        }
    }
    nvn_render_client_buffers();
    /* Opt-in tight packing (COTM_TIGHT_PACK): repack the referenced vertices
     * of large draws into one interleaved scratch array holding only the
     * enabled attributes, so the driver's per-draw client copy shrinks from
     * (stride x vertices) per attribute to the packed element sizes. */
    static int tight_configured;static unsigned tight_enabled;static uint8_t *tight_scratch;static size_t tight_capacity;
    if(!tight_configured){const char *o=getenv("COTM_TIGHT_PACK");tight_enabled=o&&!strcmp(o,"1");tight_configured=1;if(tight_enabled)printf("NVN_TIGHT_PACK_EXPERIMENT enabled=1 min-vertices=64 layout=interleaved-enabled-attributes\n");}
    struct {unsigned index,components,element;GLenum type;GLboolean normalized;const uint8_t *data;unsigned stride;} tight[16];unsigned tight_count=0,tight_use=tight_enabled&&maximum>=63;
    float cadence_position[6]={0};unsigned cadence_position_valid=0;
    for(unsigned i=0;i<(unsigned)nvn_device.attributes;i++) {
        uint32_t attr=nvn_render.attrib[i];
        if(!p->attribute_used[i]){nvn_render_attrib_enable(i,0);continue;}
        if(i>=nvn_render.attrib_count || !(attr&64)){nvn_render_attrib_enable(i,0);continue;}
        unsigned stream=attr&31,format=(attr>>21)&1023,offset=(attr>>7)&16383;
        if(stream>=nvn_render.stream_count||stream>=16||nvn_render.streams[stream][1])stop("NVN_VERTEX_STREAM_REQUIRED",nvn_caller,stream,20);
        struct nvn_vertex_format vf=nvn_vertex_format_decode(format);
        unsigned components=vf.components;
        if(p->attribute_kind[i]!=vf.kind)stop("NVN_LC2_VERTEX_SHADER_KIND_MISMATCH",nvn_caller,format,20);
        unsigned element=vf.bytes,stride=nvn_render.streams[stream][0];struct nvn_range range=nvn_render.vertex[stream];
        uint64_t start=base*stride+offset,extent=maximum*stride+element;
        if(!stride||start>range.size||extent>range.size-start)stop("NVN_VERTEX_FETCH_RANGE",nvn_caller,start+extent,24);
        void *data=nvn_gpu_pointer(range.address+start,extent,PROT_READ);nvn_render_attrib_enable(i,1);
        if(megaman_frame_cadence_enabled() && p->sources[0]==8 && p->sources[1]==7 && i==0 &&
           vf.type==GL_FLOAT && components==2 && !vf.normalized && maximum==2 && count==3) {
            for(unsigned v=0;v<3;v++)memcpy(cadence_position+v*2,(const uint8_t *)data+v*stride,8);
            cadence_position_valid=1;
        }
        if(p->wave_contract && i<2) {
            if(components!=2 || vf.type!=(i?GL_UNSIGNED_BYTE:GL_BYTE) || vf.normalized)
                stop("NVN_LC2_WAVE_ATTRIBUTE",nvn_caller,format,20);
            for(unsigned v=0;v<4;v++)for(unsigned k=0;k<2;k++) {
                if(i)wave.quad.texcoord_xy[v][k]=((const uint8_t *)data)[v*stride+k];
                else wave.quad.position_xy[v][k]=((const int8_t *)data)[v*stride+k];
            }
            wave.attributes|=1U<<i;
        }
        if(diagnose||attr_pair){printf("NVN_DRAW_ATTR index=%u format=%x stream=%u stride=%u offset=%u first=",i,format,stream,stride,offset);
            for(unsigned v=0;v<4&&v<=maximum;v++){printf(" [");for(unsigned k=0;k<components;k++){if(vf.type==GL_UNSIGNED_BYTE)printf(" %u",((uint8_t *)data)[v*stride+k]);else if(vf.type==GL_BYTE)printf(" %d",((int8_t *)data)[v*stride+k]);else if(vf.type==GL_UNSIGNED_SHORT||vf.type==GL_SHORT){uint16_t value;memcpy(&value,(uint8_t *)data+v*stride+k*2,2);printf(" %d",vf.type==GL_SHORT?(int)(int16_t)value:(int)value);}else {float value;memcpy(&value,(uint8_t *)data+v*stride+k*4,4);printf(" %g",value);}}printf(" ]");}putchar('\n');}
        if(nvn_replay.black_after_proof==119 || t2_pass_probe_active()) {
            /* Copy only elements already validated above, tightly packed in
             * vertex order. The complete batch distinguishes an opaque quad
             * later in the batch from an incorrect first-three-vertex guess. */
            if(maximum<=65535) {
                size_t bytes=(size_t)(maximum+1)*element;uint8_t *packed=malloc(bytes);
                if(!packed)stop("NVN_DRAW_CAPTURE_MEMORY",nvn_caller,bytes,21);
                for(uint64_t v=0;v<=maximum;v++)memcpy(packed+v*element,(uint8_t *)data+v*stride,element);
                nvn_private_dump("draw-attr-slot",nvn_replay.draws*16+i,packed,bytes);free(packed);
                printf("NVN_DRAW_ATTR_CAPTURE draw=%u attr=%u vertices=%"PRIu64" element=%u packing=tight\n",nvn_replay.draws,i,maximum+1,element);
            } else printf("NVN_DRAW_ATTR_CAPTURE_OMITTED draw=%u attr=%u reason=vertex-limit maximum=%"PRIu64"\n",nvn_replay.draws,i,maximum);
        }
        if(tight_use&&tight_count<16){tight[tight_count++]=(typeof(tight[0])){i,components,element,vf.type,vf.normalized,data,stride};continue;}
        {
            uint64_t stream_start=base*stride,needed=offset+extent,span=maximum*stride+stride;
            if(needed>span)span=needed;
            if(span>range.size-stream_start)span=range.size-stream_start;
            if(nvn_stream_vbo_pointer(i,components,vf.type,vf.normalized,stride,range.address+stream_start,span,offset))continue;
            if(nvn_stable_vbo_pointer(i,components,vf.type,vf.normalized,stride,range.address+stream_start,span,offset))continue;
            if(nvn_stable_vbo_enabled()&&!nvn_render_state.client_buffers_known){glBindBuffer(GL_ARRAY_BUFFER,0);nvn_render_state.client_buffers_known=1;}
        }
        if(!nvn_vertex_stream_pointer(i,components,vf.type,vf.normalized,stride,data,extent))
            nvn_render_vertex_pointer(i,components,vf.type,vf.normalized,stride,data);
    }
    if(tight_count) {
        unsigned packed_stride=0,offsets[16];
        for(unsigned k=0;k<tight_count;k++){offsets[k]=packed_stride;packed_stride+=(tight[k].element+3)&~3U;}
        size_t bytes=(size_t)(maximum+1)*packed_stride;
        if(bytes>tight_capacity){uint8_t *grown=realloc(tight_scratch,bytes);if(!grown)stop("NVN_TIGHT_PACK_MEMORY",nvn_caller,bytes,21);tight_scratch=grown;tight_capacity=bytes;}
        for(unsigned k=0;k<tight_count;k++) {
            const uint8_t *src=tight[k].data;uint8_t *dst=tight_scratch+offsets[k];unsigned e=tight[k].element,ss=tight[k].stride;
            if(e==16)for(uint64_t v=0;v<=maximum;v++)memcpy(dst+v*packed_stride,src+v*ss,16);
            else if(e==8)for(uint64_t v=0;v<=maximum;v++)memcpy(dst+v*packed_stride,src+v*ss,8);
            else if(e==4)for(uint64_t v=0;v<=maximum;v++)memcpy(dst+v*packed_stride,src+v*ss,4);
            else for(uint64_t v=0;v<=maximum;v++)memcpy(dst+v*packed_stride,src+v*ss,e);
            nvn_render_vertex_pointer(tight[k].index,tight[k].components,tight[k].type,tight[k].normalized,packed_stride,tight_scratch+offsets[k]);
        }
    }
    nvn_megaman_wave_upload(p,&wave,units);
    nvn_phase_end(NPH_VERTEX);
    unsigned cadence_full=0;
    if(megaman_frame_cadence_enabled()) {
        cadence_full=mfc_full_triangle(p->sources[0],p->sources[1],primitive,indices,count,
            nvn_render.color,nvn_render.depth_state[0],nvn_render.polygon,effective_color_mask,
            nvn_low.viewport_known,nvn_low.viewport,nvn_low.scissor_known,nvn_low.scissor,
            cadence_position_valid,cadence_position);
        if(nvn_mfc_omitting_draw()) {
            nvn_mfc_change(nvn_render.target,effective_color_mask,0);
            if((nvn_render.depth_state[0]&3U)==3U)nvn_mfc_change(nvn_render.depth,MFC_DEPTH,0);
            if(cadence_stencil && mfc_stencil_preserve_geometry(primitive,indices,count,maximum)) {
                nvn_mfc_require(nvn_render.depth,MFC_STENCIL,0);
                nvn_render_color_mask(0);nvn_render_depth_mask(GL_FALSE);
                glDrawElements(primitive,count,GL_UNSIGNED_SHORT,indices);
                nvn_render_color_mask(effective_color_mask);
                nvn_render_depth_mask((nvn_render.depth_state[0]>>1)&1);
                uint64_t preserved=++megaman_frame_cadence.stencil_preserved_draws;
                if(preserved<=16)printf("NVN_LC2_CADENCE_STENCIL_PRESERVED count=%"PRIu64" id=%"PRIu64" color-id=%"PRIu64" boundary=%"PRIu64" sources=24/23 color-writes=0 depth-writes=0 full-coverage=0 pending=0x%x pixel-proof=0\n",
                    preserved,nvn_render.depth->id,nvn_render.target->id,megaman_frame_cadence.boundaries,
                    megaman_frame_cadence.resources[nvn_mfc_slot(nvn_render.depth)].pending);
                return;
            }
            if(mfc_stencil_may_write(nvn_render.depth_state[0],nvn_render.depth_state[1],nvn_render.stencil_write))
                nvn_mfc_stencil_mark(nvn_render.depth,MFC_STENCIL,&(struct mfc_stencil_origin){.kind=1,
                    .vs=p->sources[0],.fs=p->sources[1],.primitive=primitive,.count=count,.base=base,.maximum=maximum,
                    .indices=indices,.position_valid=cadence_position_valid,.position=cadence_position});
            megaman_frame_cadence.draws_omitted++;return;
        }
        nvn_mfc_require(nvn_render.target,MFC_COLOR,cadence_full?MFC_COLOR:0);
        unsigned depth_read=(nvn_render.depth_state[0]&1U)&&((nvn_render.depth_state[0]>>4)&15U)!=8U;
        nvn_mfc_require(nvn_render.depth,(depth_read?MFC_DEPTH:0)|((nvn_render.depth_state[0]&4U)?MFC_STENCIL:0),0);
    }
    unsigned elided=nvn_normal_elide(nvn_render.target,p->sources[0],p->sources[1],nvn_render.color,nvn_render.depth_state[0],nvn_render.mask);
    {   /* MOMO_SKIP_FRAGMENT=<idx>[,<idx>...]: diagnostic only. Drop every draw whose fragment shader is
         * in the list, to price a whole effect instead of arguing about it. The picture is wrong on
         * purpose while this is on; it is never a shipping path. */
        static int cfg;static unsigned list[16],count_skip;
        if(!cfg){const char *o=getenv("MOMO_SKIP_FRAGMENT");
            while(o&&*o&&count_skip<16){char *end=NULL;list[count_skip++]=(unsigned)strtoul(o,&end,0);o=end&&*end==','?end+1:NULL;}
            if(count_skip)printf("MOMO_SKIP_FRAGMENT count=%u scope=diagnostic-picture-is-wrong\n",count_skip);
            cfg=1;}
        for(unsigned k=0;k<count_skip;k++)if(list[k]==p->sources[1])elided=1;
        /* MOMO_TMP_WHITE_ATLAS_SKIP=1 (opt-in, OFF by default, game-specific): drop a TextMeshPro draw
         * whose ATLAS sampler resolved to a 4x4 image - which is Unity's own Texture2D.whiteTexture, i.e.
         * that object's material has no font atlas at all. The fragment does alpha = atlas.a * colour.a,
         * so a white atlas paints the whole quad and that is the white rectangle over the menu
         * (proofs/menu-white-rectangle-20260919.png). A real font atlas is never 4x4, so the test is
         * narrow - but this hides a symptom whose cause is still on the guest side, so it stays opt-in
         * and is never a shipping default. */
        {   static int wcfg,wskip;
            if(!wcfg){const char *o=getenv("MOMO_TMP_WHITE_ATLAS_SKIP");wskip=o&&!strcmp(o,"1");wcfg=1;
                if(wskip)printf("MOMO_TMP_WHITE_ATLAS_SKIP enabled=1 pair=619/776 atlas=4x4 scope=symptom-only\n");}
            if(wskip && p->sources[0]==619 && p->sources[1]==776 && pass_texture_count &&
               pass_sizes[0][0]==4 && pass_sizes[0][1]==4) {
                static unsigned told;
                if(told++<4)printf("MOMO_TMP_WHITE_ATLAS_SKIPPED draw=%u frame=%u atlas=id%" PRIu64 "\n",
                    nvn_replay.draws,nvn_replay.last_frame+1,pass_textures[0]);
                elided=1;
            }
        }
        /* MOMO_DRAWS_OFF=1: drop EVERY draw. Not a picture - a floor. It answers the only question that
         * decides whether any GPU work is worth optimizing: what the frame costs when the GPU draws
         * nothing at all and only the guest and the host translation remain. */
        {   static int acfg,aoff;
            if(!acfg){const char *o=getenv("MOMO_DRAWS_OFF");aoff=o&&!strcmp(o,"1");acfg=1;
                if(aoff)printf("MOMO_DRAWS_OFF enabled=1 scope=floor-measurement-no-picture\n");}
            if(aoff)elided=1;
        }
    }
    if(!elided){nvn_depth_before_draw(nvn_render.target,nvn_render.depth,nvn_render.framebuffer,nvn_render.depth_state[0]);
        struct zero_depth_draw depth_draw=zero_depth_draw_begin(p);
        /* NVN DepthStencilState w0: bit0 depth test, bit1 depth write, bit2 stencil
         * test. A draw can only change the image when depth test+write or the
         * stencil test are enabled; otherwise the uniform proof survives. */
        if(nvn_render.depth) {
            unsigned ds=nvn_render.depth_state[0];
            zero_udd_restore(nvn_render.depth);
            if(!zero_udd_enabled() || ((ds&1U)&&(ds&2U)) || (ds&4U))zero_texture_storage_state(nvn_render.depth)->zero_depth_valid=0;
        }
#ifdef ANIMAL_WELL_SHADERS
        static int tpc;static unsigned tpf;if(!tpc){const char *o=getenv("AW_DRAW_PROBE");tpf=o?(unsigned)strtoul(o,NULL,10):0;tpc=1;}
        uint64_t aw_probe_t0=0;if(tpf && nvn_replay.last_frame+1==tpf){glFinish();aw_probe_t0=nvn_nanoseconds();}
        if(p->aw_noise_location>=0){glActiveTexture(GL_TEXTURE0+7);glBindTexture(GL_TEXTURE_2D,aw_noise.tex);GLint unit=7;nvn_uniform_send(p->aw_noise_location,NVN_UNIFORM_INT,&unit);glActiveTexture(GL_TEXTURE0);}
#endif
        if(!depth_draw.active){GLfloat dr[2];glGetFloatv(GL_DEPTH_RANGE,dr);if(dr[0]==dr[1])glDepthRangef(0.0f,1.0f);}
        {static int vbo_cfg;static unsigned vbo_lim;if(!vbo_cfg){const char *o=getenv("NX_DRAW_STATE_LIMIT");vbo_lim=o?(unsigned)strtoul(o,NULL,10):0;vbo_cfg=1;}
         if(vbo_lim&&nvn_replay.draws<=vbo_lim){GLint vao_now=0,ab_now=0,eb_now=0,no_buf=0,max_attr=0;
          glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao_now);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&ab_now);glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&eb_now);glGetIntegerv(GL_MAX_VERTEX_ATTRIBS,&max_attr);
          for(GLint ai=0;ai<max_attr;ai++){GLint en_ai=0;glGetVertexAttribiv((GLuint)ai,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&en_ai);if(en_ai){GLint b_ai=0;glGetVertexAttribiv((GLuint)ai,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&b_ai);if(!b_ai)no_buf++;}}
          printf("NVN_DRAW_VBO number=%u vao=%d array-buffer=%d element-buffer=%d attribs-without-buffer=%d indexed=%d count=%"PRIu64"\n",nvn_replay.draws,vao_now,ab_now,eb_now,no_buf,(int)(indices!=NULL),(uint64_t)count);}}
        if(nvn_flag_on("NX_FORCE_STATE")){glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_BLEND);glDisable(GL_RASTERIZER_DISCARD);glDisable(GL_DEPTH_TEST);glDisable(GL_SAMPLE_COVERAGE);glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);glDisable(0x8E51);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glViewport(0,0,1280,720);}
        if(nvn_replay.draws<=8){GLint sc[4]={0,0,0,0};glGetIntegerv(GL_SCISSOR_BOX,sc);{GLint vp[4]={0,0,0,0};glGetIntegerv(GL_VIEWPORT,vp);printf("NVN_DRAW_STATE3 number=%u discard=%d cov=%d a2c=%d smask=%d vp=%d,%d,%d,%d\n",nvn_replay.draws,(int)glIsEnabled(GL_RASTERIZER_DISCARD),(int)glIsEnabled(GL_SAMPLE_COVERAGE),(int)glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE),(int)glIsEnabled(0x8E51),vp[0],vp[1],vp[2],vp[3]);}
        printf("NVN_DRAW_STATE2 number=%u cull=%d scissor=%d box=%d,%d,%d,%d blend=%d depthtest=%d\n",nvn_replay.draws,(int)glIsEnabled(GL_CULL_FACE),(int)glIsEnabled(GL_SCISSOR_TEST),sc[0],sc[1],sc[2],sc[3],(int)glIsEnabled(GL_BLEND),(int)glIsEnabled(GL_DEPTH_TEST));}
        if(nvn_flag_on("NX_NO_DEPTH_TEST"))glDisable(GL_DEPTH_TEST);
        for(GLenum setup_error=glGetError();setup_error!=GL_NO_ERROR;setup_error=glGetError()) {
            if(nvn_flag_on("NX_SCENE_AUDIT"))printf("NX_SCENE_SETUP_ERROR frame=%u draw=%u error=0x%x\n",nvn_replay.last_frame+1,nvn_replay.draws,(unsigned)setup_error);
        }
        if(nvn_flag_on("NX_CBUF_DUMP")&&nvn_replay.draws<4){
          printf("NVN_DRAWCALL number=%u prim=0x%x count=%"PRIu64" indexed=%d idx=%u,%u,%u,%u,%u,%u\n",nvn_replay.draws,primitive,(uint64_t)count,(int)(indices!=NULL),
                 indices?(unsigned)((const unsigned short*)indices)[0]:0,indices?(unsigned)((const unsigned short*)indices)[1]:0,indices?(unsigned)((const unsigned short*)indices)[2]:0,
                 indices?(unsigned)((const unsigned short*)indices)[3]:0,indices?(unsigned)((const unsigned short*)indices)[4]:0,indices?(unsigned)((const unsigned short*)indices)[5]:0);

          {void *vp0=NULL;GLint st=0;glGetVertexAttribPointerv(0,GL_VERTEX_ATTRIB_ARRAY_POINTER,&vp0);glGetVertexAttribiv(0,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&st);
           if(vp0&&st>0&&nvn_ptr_mapped(vp0,(size_t)st*3)){const float *fv=(const float*)vp0;const float *m=(const float*)nvn_cb_src[1];
            if(m&&nvn_ptr_mapped(m,64)){for(int vi=0;vi<3;vi++){float px=fv[vi*st/4],py=fv[vi*st/4+1],pz=fv[vi*st/4+2];
              float cx=m[0]*px+m[4]*py+m[8]*pz+m[12],cy=m[1]*px+m[5]*py+m[9]*pz+m[13],cz=m[2]*px+m[6]*py+m[10]*pz+m[14],cw=m[3]*px+m[7]*py+m[11]*pz+m[15];
              printf("NVN_CLIP vi=%d in=%g,%g,%g out=%g,%g,%g,%g\n",vi,px,py,pz,cx,cy,cz,cw);}}}}}

        infernax_cbuf_audit(program,p,"after-upload");
        {GLuint probe_prog=0;
         if(nvn_flag_on("NX_PROBE_FS")){static GLuint c_vs,c_prog;
          if(!c_prog||c_vs!=(GLuint)p->sources[0]){c_vs=(GLuint)p->sources[0];if(c_prog){glDeleteProgram(c_prog);c_prog=0;}
           GLuint fsh=glCreateShader(GL_FRAGMENT_SHADER);
           const char *fss="#version 300 es\nprecision mediump float;\nout vec4 oc;\nvoid main(){oc=vec4(1.0,0.0,1.0,1.0);}\n";
           glShaderSource(fsh,1,&fss,NULL);glCompileShader(fsh);
           GLint cok=0;glGetShaderiv(fsh,GL_COMPILE_STATUS,&cok);
           GLuint pr=glCreateProgram();glAttachShader(pr,(GLuint)p->sources[0]);glAttachShader(pr,fsh);glLinkProgram(pr);
           GLint lok=0;glGetProgramiv(pr,GL_LINK_STATUS,&lok);
           printf("NVN_PROBE_FS compile=%d link=%d vs=%u fsh=%u err=0x%x\n",cok,lok,(unsigned)p->sources[0],fsh,(unsigned)glGetError());
           glDeleteShader(fsh);if(lok)c_prog=pr;}
          if(c_prog){probe_prog=c_prog;glUseProgram(c_prog);}}
         {int q_on=nvn_replay.draws<=8;GLuint q_id=0;if(q_on){glGenQueries(1,&q_id);glBeginQuery(GL_ANY_SAMPLES_PASSED,q_id);}
        infernax_cbuf_audit(program,p,"pre-draw");
        /* Red before the draw: if it survives, the draw writes nothing; if it turns black, the draw writes black. */
        if(nvn_replay.draws<=4 && getenv("NX_DEBUG_CLEAR_RED")) {GLfloat saved_clear[4];glGetFloatv(GL_COLOR_CLEAR_VALUE,saved_clear);
            glClearColor(1.0f,0.0f,0.0f,1.0f);glClear(GL_COLOR_BUFFER_BIT);glClearColor(saved_clear[0],saved_clear[1],saved_clear[2],saved_clear[3]);}
        if(nvn_replay.draws<=4){GLint dfunc=0,dmask=0,cull=0,front=0;glGetIntegerv(GL_DEPTH_FUNC,&dfunc);glGetIntegerv(GL_DEPTH_WRITEMASK,&dmask);
            glGetIntegerv(GL_CULL_FACE_MODE,&cull);glGetIntegerv(GL_FRONT_FACE,&front);
            printf("NVN_DRAW_GEOM number=%u prim=0x%x count=%d indices=%d depth-test=%d func=0x%x mask=%d cull=%d cullmode=0x%x front=0x%x blend=%d scissor=%d\n",
                   nvn_replay.draws,(unsigned)primitive,(int)count,indices!=NULL,(int)glIsEnabled(GL_DEPTH_TEST),(unsigned)dfunc,dmask,(int)glIsEnabled(GL_CULL_FACE),(unsigned)cull,(unsigned)front,(int)glIsEnabled(GL_BLEND),(int)glIsEnabled(GL_SCISSOR_TEST));
            {GLint vp[4]={0},sb[4]={0};glGetIntegerv(GL_VIEWPORT,vp);glGetIntegerv(GL_SCISSOR_BOX,sb);
             printf("NVN_DRAW_RECT number=%u viewport=%d,%d %dx%d scissorbox=%d,%d %dx%d\n",nvn_replay.draws,vp[0],vp[1],vp[2],vp[3],sb[0],sb[1],sb[2],sb[3]);}
            {GLint rb=0,d0=0,d1=0;glGetIntegerv(GL_READ_BUFFER,&rb);glGetIntegerv(GL_DRAW_BUFFER0,&d0);glGetIntegerv(GL_DRAW_BUFFER1,&d1);
             printf("NVN_DRAW_BUFS number=%u read=0x%x draw0=0x%x draw1=0x%x",nvn_replay.draws,(unsigned)rb,(unsigned)d0,(unsigned)d1);
             for(int i=0;i<4;i++){GLint ty=GL_NONE,nm=0;glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0+i,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&ty);
                 if(ty!=GL_NONE)glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0+i,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&nm);
                 printf(" att%d=0x%x/%d",i,(unsigned)ty,nm);}
             printf("\n");}
            if(nvn_replay.draws>=1&&nvn_replay.draws<=3){GLint buf=0,st=0;void*off=0;
                glGetVertexAttribiv(0,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&buf);
                glGetVertexAttribiv(0,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&st);
                glGetVertexAttribPointerv(0,GL_VERTEX_ATTRIB_ARRAY_POINTER,&off);
                if(buf){GLint prev=0;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&prev);glBindBuffer(GL_ARRAY_BUFFER,buf);
                    const unsigned char *m=(const unsigned char*)glMapBufferRange(GL_ARRAY_BUFFER,(GLintptr)off,st?(GLsizeiptr)st*4:12*4,GL_MAP_READ_BIT);
                    if(m){printf("NVN_DRAW_VERTS number=%u stride=%d",nvn_replay.draws,st);
                        for(int v=0;v<4;v++){const float *f=(const float*)(m+(size_t)v*(st?st:12));
                            printf(" [%g %g %g]",(double)f[0],(double)f[1],(double)f[2]);}
                        printf("\n");glUnmapBuffer(GL_ARRAY_BUFFER);}
                    glBindBuffer(GL_ARRAY_BUFFER,prev);}}
            if(nvn_replay.draws==2){{static unsigned long long last_tid;unsigned long long tid=nvn_render.target?nvn_render.target->id:0;
          if(tid!=last_tid){last_tid=tid;if(nvn_render.target){unsigned c=0;unsigned char px[4]={0,0,0,0};unsigned mx=nvn_texture_probe_max(nvn_render.target,&c,px);
            printf("NVN_TARGET_FIRST id=%llu obj=0x%"PRIx64" %ux%u max=%u\n",tid,nvn_render.target->object,(unsigned)nvn_render.target->b.width,(unsigned)nvn_render.target->b.height,mx);}}}
          GLint prog=0;glGetIntegerv(GL_CURRENT_PROGRAM,(void*)&prog);GLuint sh[4]={0};GLsizei cnt=0;
                glGetAttachedShaders((GLuint)prog,4,&cnt,sh);
                for(GLsizei k=0;k<cnt;k++){GLint tl=0;glGetShaderiv(sh[k],GL_SHADER_TYPE,&tl);GLint len=0;glGetShaderiv(sh[k],GL_SHADER_SOURCE_LENGTH,&len);
                    if(len>0&&len<20000){char *b=(char*)malloc((size_t)len+1);GLsizei got=0;glGetShaderSource(sh[k],len,&got,b);b[got]=0;
                        printf("NVN_SHADER_SRC type=%s id=%u len=%d\n%s\nNVN_SHADER_END\n",tl==GL_VERTEX_SHADER?"VERTEX":"FRAGMENT",(unsigned)sh[k],got,b);free(b);}}}
            if(nvn_replay.draws<=3){GLint cm[4]={0};GLint bm=GL_NONE,be=GL_NONE;
                glGetIntegerv(GL_COLOR_WRITEMASK,(void*)cm);glGetIntegerv(GL_BLEND_SRC_RGB,&bm);glGetIntegerv(GL_BLEND_DST_RGB,&be);
                printf("NVN_DRAW_WRITE number=%u mask=%d%d%d%d blend=%d src=0x%x dst=0x%x\n",nvn_replay.draws,
                       (int)cm[0],(int)cm[1],(int)cm[2],(int)cm[3],(int)glIsEnabled(GL_BLEND),(unsigned)bm,(unsigned)be);}
            if(nvn_replay.draws<=3){GLint ab=0,st2=0;void*dp=NULL;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&ab);
                glGetVertexAttribiv(0,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&st2);glGetVertexAttribPointerv(0,GL_VERTEX_ATTRIB_ARRAY_POINTER,&dp);
                printf("NVN_DRAW_SRC number=%u abinding=%d ptr=%p stride=%d",nvn_replay.draws,ab,dp,st2);
                if(ab==0&&dp){const float *f=(const float*)dp;printf(" client:");
                    for(int v=0;v<4;v++){const float *q=(const float*)((const char*)f+(size_t)v*(st2?st2:12));
                        printf(" [%g %g %g]",(double)q[0],(double)q[1],(double)q[2]);}}
                printf("\n");}
            {GLint prog=0;glGetIntegerv(GL_CURRENT_PROGRAM,&prog);printf("NVN_DRAW_PROG number=%u program=%d attribs:",nvn_replay.draws,prog);
             for(int i=0;i<8;i++){GLint en=0,sz=0,ty=0,st=0;glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&en);
                 glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_SIZE,&sz);glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_TYPE,&ty);
                 glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&st);
                 printf(" [%d en=%d sz=%d ty=0x%x st=%d]",i,en,sz,ty,st);}
             printf("\n");}}
        if(getenv("NX_NO_CULL"))glDisable(GL_CULL_FACE);
        if(getenv("NX_NO_DEPTH"))glDisable(GL_DEPTH_TEST);
        if(nvn_stg_pend&&(!nvn_render.target||nvn_render.target->name!=nvn_stg_tex))nvn_flush();
        {const char *tf=getenv("NX_TGT_FIRST");if(tf&&*tf=='1'&&nvn_render.target){static unsigned long long lt;unsigned long long tid=nvn_render.target->id;
          static struct nvn_texture *prev;
          if(tid!=lt){if(prev){unsigned c2=0;unsigned char p2[4]={0,0,0,0};unsigned m2=nvn_texture_probe_max(prev,&c2,p2);
              printf("NVN_TARGET_END id=%"PRIu64" obj=0x%"PRIx64" %ux%u max=%u\n",prev->id,prev->object,(unsigned)prev->b.width,(unsigned)prev->b.height,m2);}
            prev=nvn_render.target;lt=tid;unsigned c=0;unsigned char px[4]={0,0,0,0};unsigned mx=nvn_texture_probe_max(nvn_render.target,&c,px);
            printf("NVN_TARGET_FIRST id=%llu obj=0x%"PRIx64" %ux%u max=%u\n",tid,nvn_render.target->object,(unsigned)nvn_render.target->b.width,(unsigned)nvn_render.target->b.height,mx);}}}
        {const char *fe=getenv("NX_FBO_CHECK");static unsigned ff;if(fe&&*fe=='1'&&ff<40){GLint fb=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,(void*)&fb);GLenum st=glCheckFramebufferStatus(GL_FRAMEBUFFER);printf("NVN_FBO target=%"PRIu64" fbo=%d status=0x%x err=0x%x\n",nvn_render.target?nvn_render.target->id:0,(int)fb,(unsigned)st,(unsigned)glGetError());ff++;}}
        {const char *vb=getenv("NX_VB_CHECK");static unsigned vv;if(vb&&*vb=='1'&&vv<3){GLint ab=0,eb=0,vao=0;glGetIntegerv(GL_VERTEX_ARRAY_BINDING,(void*)&vao);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,(void*)&ab);glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,(void*)&eb);
          for(int a=0;a<2;a++){GLint en=0,sz=0,ty=0,st=0,abuf=0;void *pp=0;glGetVertexAttribiv(a,GL_VERTEX_ATTRIB_ARRAY_ENABLED,(void*)&en);glGetVertexAttribiv(a,GL_VERTEX_ATTRIB_ARRAY_SIZE,(void*)&sz);glGetVertexAttribiv(a,GL_VERTEX_ATTRIB_ARRAY_TYPE,(void*)&ty);glGetVertexAttribiv(a,GL_VERTEX_ATTRIB_ARRAY_STRIDE,(void*)&st);glGetVertexAttribPointerv(a,GL_VERTEX_ATTRIB_ARRAY_POINTER,&pp);glGetVertexAttribiv(a,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,(void*)&abuf);
            float v[8]={0};int stride=st?st:sz*4;int gsz=0,mok=0;if(ab){glBindBuffer(GL_ARRAY_BUFFER,ab);glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,(void*)&gsz);float *m=glMapBufferRange(GL_ARRAY_BUFFER,(GLintptr)pp,stride+sz*4,GL_MAP_READ_BIT);mok=m!=0;if(m){v[0]=m[0];v[1]=m[1];v[2]=m[2];v[3]=m[3];v[4]=m[stride/4];v[5]=m[stride/4+1];v[6]=m[stride/4+2];v[7]=m[stride/4+3];glUnmapBuffer(GL_ARRAY_BUFFER);}}
            {const float *g=(const float *)pp;printf("NVN_GMEM ptr=%p f=%g,%g,%g,%g %g,%g,%g,%g\n",pp,g[0],g[1],g[2],g[3],g[4],g[5],g[6],g[7]);}
            printf("NVN_VB vao=%d a=%d en=%d sz=%d ty=0x%x stride=%d abuf=%d gsz=%d mok=%d err=0x%x ptr=%p v0=(%g,%g,%g,%g) v1=(%g,%g,%g,%g)\n",(int)vao,a,en,sz,ty,st,(int)abuf,gsz,mok,(unsigned)glGetError(),pp,v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7]);}
          unsigned short ix[6]={0};if(eb){glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,eb);unsigned short *im=glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER,0,12,GL_MAP_READ_BIT);if(im){memcpy(ix,im,12);glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);}}printf("NVN_VB idx=%u,%u,%u,%u,%u,%u\n",ix[0],ix[1],ix[2],ix[3],ix[4],ix[5]);vv++;}}
        {static int n;const char *ds=getenv("NX_DRAW_STATE");if(ds&&*ds=='1'&&n<40){GLint cm[4];GLint bf[2],vp[4],sc[4];GLboolean bl=glIsEnabled(GL_BLEND),dt=glIsEnabled(GL_DEPTH_TEST),st=glIsEnabled(GL_STENCIL_TEST),sct=glIsEnabled(GL_SCISSOR_TEST);
          GLint prog=0;glGetIntegerv(GL_CURRENT_PROGRAM,(void*)&prog);glGetIntegerv(GL_COLOR_WRITEMASK,(void*)cm);glGetIntegerv(GL_BLEND_SRC_RGB,(void*)&bf[0]);glGetIntegerv(GL_BLEND_DST_RGB,(void*)&bf[1]);
          GLint df=0,dm=0;glGetIntegerv(GL_DEPTH_FUNC,(void*)&df);glGetIntegerv(GL_DEPTH_WRITEMASK,(void*)&dm);glGetIntegerv(GL_VIEWPORT,(void*)vp);glGetIntegerv(GL_SCISSOR_BOX,(void*)sc);
          printf("NVN_DRAW_STATE target_id=%"PRIu64" target_obj=0x%"PRIx64" %ux%u depth=0x%"PRIx64" pair=%u/%u\n",nvn_render.target?nvn_render.target->id:0,nvn_render.target?nvn_render.target->object:0,nvn_render.target?(unsigned)nvn_render.target->b.width:0,nvn_render.target?(unsigned)nvn_render.target->b.height:0,nvn_render.depth?nvn_render.depth->object:0,nvn_render.program?nvn_render.program->sources[0]:0,nvn_render.program?nvn_render.program->sources[1]:0);
          printf("NVN_DRAW_STATE n=%d prog=%d count=%d mask=%d%d%d%d blend=%d(%d,%d) depth=%d func=0x%x wmask=%d stencil=%d scissor_test=%d vp=%d,%d,%d,%d sc=%d,%d,%d,%d\n",
            n,prog,(int)count,cm[0],cm[1],cm[2],cm[3],bl,bf[0],bf[1],dt,df,dm,st,sct,vp[0],vp[1],vp[2],vp[3],sc[0],sc[1],sc[2],sc[3]);n++;}}
        unsigned scene_audit=infernax_scene_audit_before(p,program,primitive,(GLsizei)count,indices);
        GLuint scene_query=0;
        if(scene_audit){GLint current_query=0;glGetQueryiv(GL_ANY_SAMPLES_PASSED,GL_CURRENT_QUERY,&current_query);if(!current_query){glGenQueries(1,&scene_query);glBeginQuery(GL_ANY_SAMPLES_PASSED,scene_query);}}
        if(indices)glDrawElements(primitive,count,GL_UNSIGNED_SHORT,indices);else glDrawArrays(primitive,0,count);
        if(scene_audit){GLenum draw_error=glGetError();printf("NX_SCENE_DRAW_ERROR frame=%u draw=%u error=0x%x\n",nvn_replay.last_frame+1,nvn_replay.draws,(unsigned)draw_error);}
        if(scene_query){GLuint samples=0;glEndQuery(GL_ANY_SAMPLES_PASSED);glGetQueryObjectuiv(scene_query,GL_QUERY_RESULT,&samples);GLenum query_error=glGetError();printf("NX_SCENE_SAMPLES frame=%u draw=%u samples=%u error=0x%x\n",nvn_replay.last_frame+1,nvn_replay.draws,samples,(unsigned)query_error);glDeleteQueries(1,&scene_query);}
        if(scene_audit)infernax_scene_audit_after();
        if(q_on){GLuint occ=0;GLint db0=-1;glEndQuery(GL_ANY_SAMPLES_PASSED);glGetQueryObjectuiv(q_id,GL_QUERY_RESULT,&occ);glGetIntegerv(GL_DRAW_BUFFER0,&db0);printf("NVN_DRAW_OCCL number=%u samples=%u draw-buffer0=0x%x\n",nvn_replay.draws,occ,(unsigned)db0);glDeleteQueries(1,&q_id);}}
        {GLenum de=glGetError();if(de)printf("NVN_DRAW_ERR number=%u err=0x%x indexed=%d count=%"PRIu64" program=%u\n",nvn_replay.draws,(unsigned)de,(int)(indices!=NULL),(uint64_t)count,(unsigned)p->sources[0]);}
        if(probe_prog)glUseProgram((GLuint)p->name);}
        zero_depth_draw_end(&depth_draw);nvn_draw_finish_profile();
#ifdef ANIMAL_WELL_SHADERS
        nvn_render.target->aw_draws_since_clear++;
        {   /* AW_DRAW_PROBE=<frame>: read back every draw target of that frame (host diagnostic only). */
            static int probe_configured;static unsigned probe_frame;
            if(!probe_configured){const char *o=getenv("AW_DRAW_PROBE");probe_frame=o?(unsigned)strtoul(o,NULL,10):0;probe_configured=1;}
            if(probe_frame && nvn_replay.last_frame+1==probe_frame) {
                glFinish();
                unsigned w=nvn_render.target->b.width,h=nvn_render.target->b.height;if(w>1280)w=1280;if(h>720)h=720;
                uint8_t *px=malloc((size_t)w*h*4);
                if(px){glReadPixels(0,0,(GLsizei)w,(GLsizei)h,GL_RGBA,GL_UNSIGNED_BYTE,px);unsigned nonblack=0,maxv=0,alpha=0;
                    for(size_t i=0;i<(size_t)w*h;i++){unsigned m=px[i*4];if(px[i*4+1]>m)m=px[i*4+1];if(px[i*4+2]>m)m=px[i*4+2];if(m)nonblack++;if(m>maxv)maxv=m;if(px[i*4+3]>alpha)alpha=px[i*4+3];}
                    printf("AW_DRAW_MS %.3f\n",(nvn_nanoseconds()-aw_probe_t0)*1e-6);
                    printf("AW_DRAW_PROBE frame=%u draw=%u pair=%u/%u target=%"PRIu64" fmt=0x%x/%u size=%ux%u blend=%u mask=%x count=%"PRIu64" nonblack=%u max=%u alpha-max=%u\n",
                        probe_frame,nvn_replay.draws,p->sources[0],p->sources[1],nvn_render.target->id,nvn_render.target->b.format,nvn_render.target->aw_float_kind,w,h,!!(nvn_render.color&0x100),nvn_render.mask&15U,count,nonblack,maxv,alpha);
                    free(px);}
            }
        }
#endif
        }
    if(!elided&&megaman_frame_cadence_omitting()&&!nvn_render.target->host_scaled) {
        static unsigned reported;
        if(reported<4)printf("NVN_LC2_NATIVE_TARGET_PRESERVED event=%u id=%"PRIu64" size=%ux%u sources=%u/%u boundary=%"PRIu64" present-omitted=1 draw=real\n",
            ++reported,nvn_render.target->id,nvn_render.target->b.width,nvn_render.target->b.height,
            p->sources[0],p->sources[1],megaman_frame_cadence.boundaries);
    }
    if(cadence_full&&!elided) {
        nvn_mfc_change(nvn_render.target,MFC_COLOR,1);
        if((nvn_render.depth_state[0]&3U)==3U)nvn_mfc_change(nvn_render.depth,MFC_DEPTH,1);
        if(indices) {
            static unsigned reported;
            if(!reported){reported=1;printf("NVN_LC2_CADENCE_INDEXED_OVERWRITE id=%"PRIu64" indices=%u/%u/%u count=3 maximum=2 sources=8/7 color=full depth=%s stencil=unchanged\n",
                nvn_render.target->id,indices[0],indices[1],indices[2],(nvn_render.depth_state[0]&3U)==3U?"full":"unchanged");}
        }
    }
    if(preserve)momo_stencil_probe(nvn_replay.draws+1,p->sources[0],p->sources[1]);
    momo_window_draws_census(nvn_render.target,nvn_replay.last_frame,nvn_replay.draws+1,p->sources[0],p->sources[1]);
    nvn_replay.draws++;nvn_render.target->draws++;
    struct nvn_texture *target_storage=zero_texture_storage_state(nvn_render.target);
    if(target_storage!=nvn_render.target)target_storage->draws++;
    nvn_render_count_program(p,program);p->zero_diagnostic_draws++;
    nvn_index_fast_report(nvn_replay.last_frame);
    nvn_vertex_stream_report(nvn_replay.last_frame);nvn_stream_vbo_report(nvn_replay.last_frame);nvn_stable_vbo_report(nvn_replay.last_frame);
    nvn_normal_report(nvn_replay.last_frame);
    if(diagnose&&elided)printf("NVN_DRAW_OMITTED draw=%u target=%"PRIu64" pixels=unmaterialized proof=none\n",nvn_replay.draws,nvn_render.target->id);
    if(diagnose&&!elided&&nvn_render.target!=&zero_no_color) {
        unsigned w=nvn_low_render_width(nvn_render.target),h=nvn_low_render_height(nvn_render.target);
        size_t bytes=(size_t)w*h*4;uint8_t *pixels=malloc(bytes);if(!pixels)stop("NVN_DRAW_DIAGNOSTIC_MEMORY",nvn_caller,bytes,21);
        glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);unsigned nz=0,alpha_nz=0;
        for(size_t k=0;k<bytes;k+=4){if(pixels[k]>3||pixels[k+1]>3||pixels[k+2]>3)nz++;if(pixels[k+3]>3)alpha_nz++;}
        printf("NVN_DRAW_FBO draw=%u rgb-nonblack=%u alpha-nonzero=%u size=%ux%u\n",nvn_replay.draws,nz,alpha_nz,w,h);free(pixels);
    }
    {   /* MOMO_FBO_FRAME=N: read back every draw of frame N with its pair and target, so the pass that
         * loses the picture is named instead of guessed (the 640x360 scene is full of pixels while the
         * 1280x720 and 1920x1080 targets come out black — MEDIDO run momo-109). */
        static int configured;static unsigned wanted,span;
        /* MOMO_FBO_FRAMES widens the census window. Three frames is enough to read one screen, but to
         * catch the frame a shape first appears in, the window has to cover the interval it appears in. */
        if(!configured){const char *o=getenv("MOMO_FBO_FRAME");wanted=o?(unsigned)strtoul(o,NULL,0):0;
            const char *n=getenv("MOMO_FBO_FRAMES");span=n?(unsigned)strtoul(n,NULL,0):3;if(!span)span=3;configured=1;}
        if((t2_pass_probe_active() || (wanted && nvn_replay.last_frame+1>=wanted && nvn_replay.last_frame+1<wanted+span)) && !elided && nvn_render.target && nvn_render.target!=&zero_no_color) {
            unsigned w=nvn_low_render_width(nvn_render.target),h=nvn_low_render_height(nvn_render.target);
            if(!getenv("MOMO_FBO_FULL")) {
                if(w>640)w=640;
                if(h>360)h=360;
            }
            size_t bytes=(size_t)w*h*4;uint8_t *pixels=malloc(bytes);
            if(pixels) {
                glReadPixels(0,0,(GLsizei)w,(GLsizei)h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
                unsigned nz=0,anz=0;
                unsigned red=0,white=0;
                for(size_t k=0;k<bytes;k+=4){if(pixels[k]>3||pixels[k+1]>3||pixels[k+2]>3)nz++;if(pixels[k+3]>3)anz++;
                    if(pixels[k]>200&&pixels[k+1]<40&&pixels[k+2]<40)red++;
                    if(pixels[k]>250&&pixels[k+1]>250&&pixels[k+2]>250)white++;}
                /* white-pixels: the shapes that are wrong on this port are solid white, so counting them
                 * per draw names the draw that paints one instead of guessing from its geometry. */
                printf("MOMO_PASS_RED draw=%u red-pixels=%u white-pixels=%u\n",nvn_replay.draws,red,white);
                if((getenv("MOMO_PASS_DUMP") || t2_pass_probe_active()) && nz>1000)nvn_private_dump("pass-rgba",nvn_replay.draws*4+(w>640),pixels,bytes);
                printf("MOMO_PASS frame=%u draw=%u pair=%u/%u target=%"PRIu64" size=%ux%u sampled=%ux%u nonblack=%u alpha=%u blend=0x%x\n",
                    nvn_replay.last_frame+1,nvn_replay.draws,p->sources[0],p->sources[1],nvn_render.target->id,
                    nvn_render.target->b.width,nvn_render.target->b.height,w,h,nz,anz,(unsigned)nvn_render.blend[0]);
                {
                    GLint vp[4]={0,0,0,0},sc[4]={0,0,0,0};GLboolean st=0,dt=0,bl=0;GLint mask[4]={0,0,0,0};
                    glGetIntegerv(GL_VIEWPORT,vp);glGetIntegerv(GL_SCISSOR_BOX,sc);
                    st=glIsEnabled(GL_SCISSOR_TEST);dt=glIsEnabled(GL_DEPTH_TEST);bl=glIsEnabled(GL_BLEND);
                    glGetIntegerv(GL_COLOR_WRITEMASK,mask);
                    printf("MOMO_PASS_STENCIL draw=%u depth-state=%08x/%08x ref=%u/%u compare-mask=0x%x/0x%x write-mask=0x%x/0x%x color-mask=%x gl-stencil=%u\n",
                        nvn_replay.draws,nvn_render.depth_state[0],nvn_render.depth_state[1],
                        nvn_render.stencil_ref[0],nvn_render.stencil_ref[1],nvn_render.stencil_compare[0],nvn_render.stencil_compare[1],
                        nvn_render.stencil_write[0],nvn_render.stencil_write[1],nvn_render.mask,(unsigned)glIsEnabled(GL_STENCIL_TEST));
                    printf("MOMO_PASS_STATE draw=%u viewport=%d,%d,%d,%d scissor=%d,%d,%d,%d scissor-test=%u depth-test=%u blend=%u writemask=%d error=0x%x\n",
                        nvn_replay.draws,vp[0],vp[1],vp[2],vp[3],sc[0],sc[1],sc[2],sc[3],(unsigned)st,(unsigned)dt,(unsigned)bl,mask[0]|(mask[1]<<1)|(mask[2]<<2)|(mask[3]<<3),glGetError());
                }
                printf("MOMO_PASS_SAMPLES draw=%u count=%u",nvn_replay.draws,pass_texture_count);
                for(unsigned k=0;k<pass_texture_count;k++)printf(" %s=id%"PRIu64"/%ux%u/slot%u/off0x%x/handle0x%"PRIx64,pass_names[k],pass_textures[k],pass_sizes[k][0],pass_sizes[k][1],pass_slots[k],pass_offsets[k],pass_handles[k]);
                putchar('\n');
                free(pixels);
            }
        }
    }
    {static unsigned trace_limit;static int tcfg;if(!tcfg){const char *tl=getenv("NX_DRAW_TRACE");trace_limit=tl?(unsigned)strtoul(tl,NULL,0):8;tcfg=1;}
    if(nvn_replay.draws<trace_limit){
        if(nvn_flag_on("NX_PROBE_QUAD")) {
            /* One magenta quad straight into the target the guest just drew to: separates "guest geometry
             * produced no pixels" from "our render-to-texture or present path drops them". */
            static GLuint program,vbo,vao;static GLuint tex_names[4];
            if(!program) {
                const char *vs="#version 300 es\nlayout(location=0) in vec2 p;void main(){gl_Position=vec4(p,0.0,1.0);}\n";
                const char *fs="#version 300 es\nprecision mediump float;out vec4 o;void main(){o=vec4(1.0,0.0,1.0,1.0);}\n";
                GLuint shaders[2];program=glCreateProgram();
                shaders[0]=glCreateShader(GL_VERTEX_SHADER);glShaderSource(shaders[0],1,&vs,NULL);glCompileShader(shaders[0]);
                shaders[1]=glCreateShader(GL_FRAGMENT_SHADER);glShaderSource(shaders[1],1,&fs,NULL);glCompileShader(shaders[1]);
                glAttachShader(program,shaders[0]);glAttachShader(program,shaders[1]);glBindAttribLocation(program,0,"p");glLinkProgram(program);
                const float quad[8]={-1.0f,-1.0f,1.0f,-1.0f,-1.0f,1.0f,1.0f,1.0f};
                glGenVertexArrays(1,&vao);glBindVertexArray(vao);
                glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);
                glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,(void *)0);
            }
            (void)tex_names;
            GLint previous_program=0,previous_vao=0;glGetIntegerv(GL_CURRENT_PROGRAM,&previous_program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&previous_vao);
            glUseProgram(program);glBindVertexArray(vao);
            GLboolean stencil_was_on=glIsEnabled(GL_STENCIL_TEST);GLint stencil_ref=0,stencil_mask=0;glGetIntegerv(GL_STENCIL_REF,&stencil_ref);glGetIntegerv(GL_STENCIL_VALUE_MASK,&stencil_mask);
            glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);glDisable(GL_STENCIL_TEST);
            glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
            {GLfloat near_now=0,far_now=0;GLboolean depth_mask_now=GL_TRUE;glGetFloatv(GL_DEPTH_RANGE,&near_now);glGetFloatv(GL_DEPTH_RANGE+1,&far_now);glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_mask_now);
             printf("NVN_DRAW_DEPTHSTATE number=%u range=%g..%g mask=%d\n",nvn_replay.draws,(double)near_now,(double)far_now,(int)depth_mask_now);
             glDepthRangef(0.0f,1.0f);glDepthMask(GL_FALSE);}
            glViewport(0,0,(GLsizei)nvn_render.target->b.width,(GLsizei)nvn_render.target->b.height);
            glDrawArrays(GL_TRIANGLE_STRIP,0,4);
            if(nvn_flag_on("NX_PROBE_NO_DEPTH")){ /* Same quad again with the depth attachment detached. */
                GLint depth_type=0,depth_name=0;
                glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&depth_type);
                glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&depth_name);
                if(depth_type==GL_RENDERBUFFER){
                    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,0);
                    GLenum without=glCheckFramebufferStatus(GL_FRAMEBUFFER);
                    glClearColor(0.0f,1.0f,0.0f,1.0f);glClear(GL_COLOR_BUFFER_BIT);
                    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,(GLuint)depth_name);
                    printf("NVN_PROBE_NO_DEPTH number=%u status-without=0x%x error=0x%x\n",nvn_replay.draws,(unsigned)without,(unsigned)glGetError());}
            }
            glUseProgram((GLuint)previous_program);glBindVertexArray((GLuint)previous_vao);
            GLint bound_fbo=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&bound_fbo);
            GLint attached=0;glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&attached);
            printf("NVN_PROBE_QUAD number=%u target=0x%"PRIx64" fbo=%d attach=%d match=%u stencil=%d ref=%d mask=0x%x error=0x%x\n",nvn_replay.draws,nvn_render.target->object,bound_fbo,attached,(unsigned)(attached==(GLint)nvn_render.target->name),(int)stencil_was_on,stencil_ref,stencil_mask,(unsigned)glGetError());
        }
        {GLint viewport_now[4];glGetIntegerv(GL_VIEWPORT,viewport_now);GLboolean mask_now[4];glGetBooleanv(GL_COLOR_WRITEMASK,mask_now);
         GLint saved_unit=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&saved_unit);unsigned bound_as_texture=0;
         for(int unit=0;unit<16;unit++){GLint bound=0;glActiveTexture(GL_TEXTURE0+unit);glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);if(bound&&(GLuint)bound==nvn_render.target->name)bound_as_texture|=1u<<unit;}
         glActiveTexture((GLenum)saved_unit);
         GLenum bound_status=glCheckFramebufferStatus(GL_FRAMEBUFFER);GLint depth_type=0,depth_name=0;
         glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&depth_type);
         glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&depth_name);
         GLint color_tex_w=(GLint)nvn_render.target->host_width,color_tex_h=(GLint)nvn_render.target->host_height,depth_rb_w=0,depth_rb_h=0;
         if(depth_type==GL_RENDERBUFFER){GLint saved_rb=0;glGetIntegerv(GL_RENDERBUFFER_BINDING,&saved_rb);glBindRenderbuffer(GL_RENDERBUFFER,(GLuint)depth_name);
             glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_WIDTH,&depth_rb_w);glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_HEIGHT,&depth_rb_h);
             glBindRenderbuffer(GL_RENDERBUFFER,(GLuint)saved_rb);}
         printf("NVN_DRAW_SIZES number=%u scaled=%d host=%dx%d depth-rb=%dx%d logical=%ux%u\n",nvn_replay.draws,(int)nvn_render.target->host_scaled,color_tex_w,color_tex_h,depth_rb_w,depth_rb_h,nvn_render.target->b.width,nvn_render.target->b.height);
         printf("NVN_DRAW_FBO number=%u status=0x%x depth-type=0x%x depth-name=%d target=%ux%u\n",nvn_replay.draws,(unsigned)bound_status,(unsigned)depth_type,depth_name,nvn_render.target->b.width,nvn_render.target->b.height);
         printf("NVN_DRAW_STATE number=%u viewport=%d,%d,%d,%d mask=%d,%d,%d,%d discard=%d texture-units=0x%x\n",nvn_replay.draws,viewport_now[0],viewport_now[1],viewport_now[2],viewport_now[3],mask_now[0],mask_now[1],mask_now[2],mask_now[3],(int)glIsEnabled(GL_RASTERIZER_DISCARD),bound_as_texture);}
        {static unsigned dump_frame,dumps;static int dcfg;if(!dcfg){const char *df=getenv("NX_DUMP_FRAME");dump_frame=df?(unsigned)strtoul(df,NULL,0):0;dcfg=1;}
         {static int onc;if(!onc){onc=1;const char *ou=getenv("NX_DUMP_ON_UPLOAD");if(ou&&*ou=='1')dump_frame=1;}
         if(dump_frame==1&&!nvn_upload_seen)goto skip_dump;
        if(dump_frame&&nvn_replay.last_frame>=dump_frame&&dumps<12&&nvn_render.target&&nvn_render.target->name){
            GLint w=(GLint)nvn_render.target->b.width,h=(GLint)nvn_render.target->b.height;
            if(w>0&&h>0&&w<=2048&&h<=2048){unsigned char *buf=(unsigned char*)malloc((size_t)w*h*4);
                if(buf){GLint fbo=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fbo);GLuint tf=0;glGenFramebuffers(1,&tf);glBindFramebuffer(GL_FRAMEBUFFER,tf);
                    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,nvn_render.target->name,0);
                    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE){glPixelStorei(GL_PACK_ALIGNMENT,1);
                        glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,buf);char path[256];
                        snprintf(path,sizeof path,"/mnt/SDCARD/Roms/PORTS/infernax/infernax-tex-%016"PRIx64"-%ux%u.ppm",nvn_render.target->object,(unsigned)w,(unsigned)h);
                        FILE *fp=fopen(path,"wb");if(fp){fprintf(fp,"P6\n%d %d\n255\n",w,h);for(int i=h-1;i>=0;i--)for(int x=0;x<w;x++)fwrite(buf+((size_t)i*w+x)*4,1,3,fp);fclose(fp);dumps++;}}
                    glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)fbo);glDeleteFramebuffers(1,&tf);free(buf);}}}}
         skip_dump:;}
        unsigned complete=0;unsigned char px[4]={0,0,0,0};unsigned max=nvn_texture_probe_max(nvn_render.target,&complete,px);
        printf("NVN_DRAW_PIXELS number=%u target=0x%"PRIx64" complete=0x%x max=%u px=%u,%u,%u,%u readerr=0x%x\n",nvn_replay.draws,nvn_render.target->object,complete,max,px[0],px[1],px[2],px[3],(unsigned)glGetError());}
    if(nvn_replay.draws<trace_limit)printf("NVN_GAME_DRAW number=%u count=%"PRIu64" primitive=%u program=%u target=0x%"PRIx64" emitted=%u\n",nvn_replay.draws,count,primitive,program,nvn_render.target->object,!elided);
    }}
static void nvn_render_draw(struct nvn_record *r) {
    if(!nvn_replay.restart_enabled || r->op==NI_nvnCommandBufferDrawArrays){nvn_render_draw_inner(r);return;}
    unsigned type=r->args[1];uint64_t count=r->args[2];
    if(type>2 || count>1048576)stop("ZERO_RESTART_INDEX_RANGE",nvn_caller,count,24);
    if(!count)return;
    unsigned bytes=1U<<type;const uint8_t *data=nvn_gpu_pointer(r->args[3],count*bytes,PROT_READ);
    uint32_t marker=nvn_replay.restart_index & (type==0?UINT8_MAX:type==1?UINT16_MAX:UINT32_MAX);
    uint64_t begin=0;
    /* GLES2 has no core programmable restart. Each interval is the native
     * independent primitive sequence, with strip parity reset at its start.
     * Compare the original index before base-vertex/rebasing; never upload
     * the restart marker as a vertex or join strips across that marker. */
    for(uint64_t end=0;end<=count;end++) {
        uint32_t value=0;if(end<count)memcpy(&value,data+end*bytes,bytes);
        if(end<count && value!=marker)continue;
        if(end>begin) {
            struct nvn_record part=*r;part.args[2]=end-begin;part.args[3]+=begin*bytes;nvn_render_draw_inner(&part);
        }
        begin=end+1;
    }
}
static unsigned nvn_render_record(struct nvn_record *r) {
    uint64_t *a=r->args;
    switch(r->op) {
    case NI_nvnCommandBufferSaveZCullData:case NI_nvnCommandBufferRestoreZCullData:
        /* Zero bytes of external hierarchical-cull metadata were reported; GLES depth contents persist. */
        if(a[1])stop("NVN_EXTERNAL_ZCULL_STORAGE_REQUIRED",nvn_caller,a[1],20);   /* a[0] address, a[1] size */
        return 1;
    case NI_nvnCommandBufferSetDepthClamp:case NI_nvnCommandBufferSetConservativeRasterEnable:
    case NI_nvnCommandBufferSetRasterizerDiscard:
    case NI_nvnCommandBufferSetDepthBounds:
        /* These disabled states are core GLES2 behavior. Enabling the NVN
         * extensions needs an implemented backend, never a successful no-op. */
        /* Animal Well enables depth clamp: with its orthographic 2D depth ranges nothing reaches the clip planes, so
         * GLES2 (which cannot disable depth clipping) draws the same pixels. Logged once; the other extensions stay closed. */
        if(a[0] && r->op==NI_nvnCommandBufferSetDepthClamp){static unsigned reported;if(!reported++)puts("AW_DEPTH_CLAMP enabled=1 gles2=clip");return 1;}
        if(a[0])stop("ZERO_RASTER_EXTENSION_REQUIRED",nvn_caller,r->op,20);
        return 1;
    case NI_nvnCommandBufferSetBlendColor: {
        float color[4];memcpy(color,r->payload,16);
#ifdef ANIMAL_WELL_SHADERS
        if(nvn_render.target->aw_float_kind==2)aw_float_encode_color(color);
        if(aw_target_frozen(nvn_render.target->id))return 1;
        nvn_render.target->aw_draws_since_clear=0;
#endif
        for(unsigned i=0;i<4;i++)if(!isfinite(color[i]))stop("ZERO_BLEND_COLOR_INVALID",nvn_caller,i,24);
        glBlendColor(color[0],color[1],color[2],color[3]);return 1;
    }
    case NI_nvnCommandBufferCopyTextureToTexture:zero_depth_copy(r);return 1;
    case NI_nvnCommandBufferSetLineWidth:case NI_nvnCommandBufferSetPointSize: {
        float size;memcpy(&size,r->vectors,4);
        if(!isfinite(size) || size<=0)stop("ZERO_RASTER_SIZE_INVALID",nvn_caller,r->op,24);
        if(r->op==NI_nvnCommandBufferSetLineWidth)nvn_render.line_width=size;else nvn_render.point_size=size;
        return 1;
    }
    case NI_nvnCommandBufferSetPrimitiveRestart:
        if(a[0]>1)stop("ZERO_PRIMITIVE_RESTART_FLAG",nvn_caller,a[0],24);
        nvn_replay.restart_enabled=a[0];nvn_replay.restart_index=(uint32_t)a[1];return 1;
    case NI_nvnCommandBufferSetShaderScratchMemory: {
        uint64_t size=(uint32_t)a[2];
        if(!a[0]) {
            if(size)stop("NVN_SHADER_SCRATCH_NULL_RANGE",nvn_caller,size,24);
            nvn_render.scratch_pool=NULL;nvn_render.scratch_offset=nvn_render.scratch_size=0;return 1;
        }
        struct nvn_pool *pool=nvn_pool_find(a[0]);
        if(!size || a[1]%ZERO_SHADER_SCRATCH_ALIGNMENT || size%NVN_SHADER_SCRATCH_GRANULARITY || a[1]>pool->size || size>pool->size-a[1] ||
           (pool->flags&0x38)==8)
            stop("NVN_SHADER_SCRATCH_RANGE",nvn_caller,size,24);
        nvn_render.scratch_pool=pool;nvn_render.scratch_offset=a[1];nvn_render.scratch_size=size;
        /* Pinned graphics shaders execute through real GLES programs. Driver
         * scratch belongs to that backend; this guest allocation remains an
         * opaque reservation, never an emulated GPU write or completion. */
        static unsigned reported;if(!reported++)printf("NVN_LC2_SHADER_SCRATCH pool=0x%"PRIx64" offset=0x%"PRIx64" bytes=0x%"PRIx64" backend=GLES-driver-owned\n",a[0],a[1],size);
        return 1;
    }
    case NI_nvnCommandBufferBindColorState:memcpy(&nvn_render.color,r->payload,4);return 1;
    case NI_nvnCommandBufferBindBlendState:memcpy(nvn_render.blend,r->payload,8);return 1;
    case NI_nvnCommandBufferBindDepthStencilState:memcpy(nvn_render.depth_state,r->payload,8);return 1;
    case NI_nvnCommandBufferBindMultisampleState: {
        uint32_t state[6];memcpy(state,r->payload,sizeof(state));
        if(!nvn_megaman_multisample_supported(state))stop("NVN_LC2_MULTISAMPLE_STATE_REQUIRED",nvn_caller,state[0],20);
        memcpy(nvn_render.multisample,state,sizeof(state));return 1;
    }
    case NI_nvnCommandBufferSetPolygonOffsetClamp: {
        float values[3];for(unsigned i=0;i<3;i++)memcpy(values+i,r->vectors+i,4);
        if(!nvn_megaman_polygon_offset_supported(values[0],values[1],values[2]))
            stop("NVN_LC2_POLYGON_OFFSET_CLAMP_REQUIRED",nvn_caller,r->op,20);
        memcpy(nvn_render.polygon_offset,values,sizeof(values));return 1;
    }
    case NI_nvnCommandBufferSetStencilRef:case NI_nvnCommandBufferSetStencilMask:case NI_nvnCommandBufferSetStencilValueMask:
        for(unsigned face=0;face<2;face++)if(a[0]&(1U<<face)) {
            if(r->op==NI_nvnCommandBufferSetStencilRef){nvn_render.stencil_ref[face]=(uint8_t)a[1];nvn_render.stencil_known[face]|=1;}
            else if(r->op==NI_nvnCommandBufferSetStencilMask){nvn_render.stencil_write[face]=(uint8_t)a[1];nvn_render.stencil_known[face]|=2;}
            else {nvn_render.stencil_compare[face]=(uint8_t)a[1];nvn_render.stencil_known[face]|=4;}
        }return 1;
    case NI_nvnCommandBufferBindPolygonState:memcpy(&nvn_render.polygon,r->payload,4);return 1;
    case NI_nvnCommandBufferBindChannelMaskState:memcpy(&nvn_render.mask,r->payload,4);return 1;
    case NI_nvnCommandBufferBindProgram:
        nvn_program_bind_stages(nvn_render.program_stages,a[0],a[1],a[2]);nvn_render.program=NULL;return 1;
    case NI_nvnCommandBufferBindVertexAttribState:nvn_render.attrib_count=a[0];memcpy(nvn_render.attrib,r->payload,a[0]*4);return 1;
    case NI_nvnCommandBufferBindVertexStreamState:nvn_render.stream_count=a[0];memcpy(nvn_render.streams,r->payload,a[0]*8);return 1;
    case NI_nvnCommandBufferBindVertexBuffer:
        if(a[0]>=16)stop("NVN_VERTEX_BINDING",nvn_caller,a[0],24);
        nvn_render.vertex[a[0]]=(struct nvn_range){a[1],a[2]};return 1;
    case NI_nvnCommandBufferBindVertexBuffers:memcpy(nvn_render.vertex+a[0],r->payload,a[1]*16);return 1;
    case NI_nvnCommandBufferBindUniformBuffer:
        if(a[0]>1||a[1]>=16)stop("NVN_UNIFORM_BINDING",nvn_caller,a[1],24);
        nvn_render.uniform[a[0]][a[1]]=(struct nvn_range){a[2],a[3]};return 1;
    case NI_nvnCommandBufferBindUniformBuffersDeferred: {
        /* The ranges live in guest memory and are read now, at execution, which is the deferred contract. */
        const void *ranges=nvn_pointer(a[3],a[2]*16,PROT_READ);
        memcpy(nvn_render.uniform[a[0]]+a[1],ranges,a[2]*16);
        static unsigned told;
        if(told++<4)printf("T2_UNIFORM_BIND_DEFERRED stage=%"PRIu64" first=%"PRIu64" count=%"PRIu64" ranges=0x%"PRIx64"\n",a[0],a[1],a[2],a[3]);
        return 1;
    }
    case NI_nvnCommandBufferBindUniformBuffers:
        memcpy(nvn_render.uniform[a[0]]+a[1],r->payload,a[2]*16);
        if(getenv("MOMO_TRACE_UNIFORM_BINDS")) {
            static unsigned told;
            if(told++<64) {
                printf("MOMO_UNIFORM_BIND draw=%u stage=%"PRIu64" first=%"PRIu64" count=%"PRIu64,nvn_replay.draws,a[0],a[1],a[2]);
                const struct nvn_range *rg=(const struct nvn_range *)r->payload;
                for(uint64_t k=0;k<a[2];k++)printf(" [%"PRIu64"]=0x%"PRIx64"/%"PRIu64,a[1]+k,rg[k].address,(uint64_t)rg[k].size);
                putchar('\n');
            }
        }
        return 1;
    case NI_nvnCommandBufferBindTexture:
        if(a[0]>1 || a[1]>=128)stop("NVN_TEXTURE_BINDING_REQUIRED",nvn_caller,a[1],20);
#ifdef INFERNAX_CBUF_SAMPLERS
        {uint64_t before=nvn_render.texture_handles[a[0]][a[1]],known=nvn_render.texture_known[a[0]][a[1]/64];
#endif
        nvn_render.texture_handles[a[0]][a[1]]=a[2];nvn_render.texture_known[a[0]][a[1]/64]|=1ULL<<(a[1]%64);
#ifdef INFERNAX_CBUF_SAMPLERS
        infernax_sampler_record("combined",a[0],a[1],before,known);}
#endif
        return 1;
    case NI_nvnCommandBufferBindSeparateTexture:case NI_nvnCommandBufferBindSeparateSampler: {
        if(a[0]>1)return 1;   /* a stage this host does not run */
        if(a[1]>=128)return 1;   /* beyond the table the hardware itself exposes */
        uint64_t *slot=&nvn_render.texture_handles[a[0]][a[1]];
#ifdef INFERNAX_CBUF_SAMPLERS
        uint64_t before=*slot,known=nvn_render.texture_known[a[0]][a[1]/64];
#endif
        if(r->op==NI_nvnCommandBufferBindSeparateTexture) {
            if(a[2]>>32)stop("MOMO_SEPARATE_TEXTURE_HANDLE",nvn_caller,a[2],24);
            *slot=(*slot&0xffffffff00000000ULL)|a[2];
#ifdef INFERNAX_CBUF_SAMPLERS
            nvn_render.separate_textures[a[0]][a[1]]=(uint32_t)a[2];
            nvn_render.separate_texture_known[a[0]][a[1]/64]|=UINT64_C(1)<<(a[1]%64);
#endif
        } else {
            if((uint32_t)a[2])stop("MOMO_SEPARATE_SAMPLER_HANDLE",nvn_caller,a[2],24);
            *slot=(*slot&0xffffffffULL)|a[2];
#ifdef INFERNAX_CBUF_SAMPLERS
            nvn_render.separate_samplers[a[0]][a[1]]=(uint32_t)(a[2]>>32);
            nvn_render.separate_sampler_known[a[0]][a[1]/64]|=UINT64_C(1)<<(a[1]%64);
#endif
        }
        if((uint32_t)*slot && (*slot>>32))nvn_render.texture_known[a[0]][a[1]/64]|=1ULL<<(a[1]%64);
#ifdef INFERNAX_CBUF_SAMPLERS
        infernax_sampler_record(r->op==NI_nvnCommandBufferBindSeparateTexture?"texture":"sampler",a[0],a[1],before,known);
#endif
        return 1;
    }
    case NI_nvnCommandBufferSetRenderTargets: {
{GLenum pre=glGetError();if(pre)printf("NVN_RT_PRE error=0x%x prev-op=%s draws=%u frame=%u target=%"PRIu64" pair=%u/%u\n",(unsigned)pre,nvn_replay_last_op,nvn_replay.draws,nvn_replay.last_frame,nvn_render.target?nvn_render.target->id:0,nvn_render.program?nvn_render.program->sources[0]:0,nvn_render.program?nvn_render.program->sources[1]:0);}
#define NVN_RT_STEP(tag) {GLenum step_error=glGetError();if(step_error)printf("NVN_RT_STEP step=%s error=0x%x\n",tag,(unsigned)step_error);}
        uint64_t targets[2];memcpy(targets,r->payload,16);struct nvn_texture *d=targets[1]?nvn_texture_find(targets[1]):NULL;
#if ZERO_ZX_GAME
        for(unsigned i=0;i<2;i++)if(a[i?4:2]) {
#ifdef MOMO_SHADERS
            if(!targets[i])continue;   /* detached attachment with a zeroed view (nvn_textures.h) */
#endif
            struct nvn_texture *viewed=targets[i]?nvn_texture_find(targets[i]):NULL;
            if(!viewed || !zero_render_target_view_identity(r->payload+16+i*MEGAMAN_TEXTURE_VIEW_BYTES,
                    viewed->b.levels,viewed->b.depth,viewed->aw_native_format?viewed->aw_native_format:viewed->b.format,viewed->b.target))
                stop("ZERO_RENDER_TARGET_VIEW_STALE",nvn_caller,targets[i],24);
        }
#endif
        if(!targets[0]) {
            if(!d || !d->depth_buffer || (d->host_scaled && !zero_render_scale_enabled()))
                stop("ZERO_DEPTH_ONLY_TARGET",nvn_caller,targets[1],20);
            nvn_rt_storage_require(d);
            nvn_depth_leave(nvn_render.target,nvn_render.depth,nvn_render.framebuffer,NULL,d);
            if(nvn_render.depth!=d)zero_udd_leave(nvn_render.depth,nvn_render.framebuffer);
            nvn_render.target=zero_depth_only_target(d);nvn_render.depth=d;
            nvn_render.framebuffer=nvn_framebuffer_bind(nvn_render.target,d);nvn_render.pass_skipped=0;
            zero_udd_restore(d);
            nvn_low_target(nvn_render.target,d);d->composition_target_seen=1;
            zero_texture_storage_state(d)->composition_target_seen=1;return 1;
        }
        struct nvn_texture *t=nvn_texture_find(targets[0]);
        nvn_cpu_image_require_target(t);nvn_cpu_image_require_target(d);
        nvn_rt_storage_target(t,d);NVN_RT_STEP("storage");
        nvn_mfc_target(t);nvn_mfc_target(d);
        t->composition_target_seen=1;t->composition_initial_uniform=0;
        zero_texture_storage_state(t)->composition_target_seen=1;
        zero_texture_storage_state(t)->composition_initial_uniform=0;
        if(d){d->composition_target_seen=1;d->composition_initial_uniform=0;
            zero_texture_storage_state(d)->composition_target_seen=1;
            zero_texture_storage_state(d)->composition_initial_uniform=0;
            if(nvn_low_color(t)<0&&!t->host_scaled)d->composition_other_target=1;}
        nvn_packaged_require_writable(t,"NVN_LC2_PACKAGED_RENDER_TARGET_REQUIRED");
        if(!t->name||(d&&(!d->depth_buffer||d->b.width!=t->b.width||d->b.height!=t->b.height)))stop("NVN_FRAMEBUFFER_ATTACHMENTS",nvn_caller,targets[0],24);
#ifdef ANIMAL_WELL_SHADERS
        if(aw_deferred_pass_enabled() && !d && !nvn_skip.active) {
            if(nvn_render.pass_skipped && nvn_render.aw_deferred_target && nvn_render.pending_color_valid && nvn_render.target){
                aw_clear_stash(nvn_render.target,nvn_render.pending_color,nvn_render.pending_mask);nvn_render.pending_color_valid=0;}
            nvn_render.pass_skipped=1;nvn_render.aw_deferred_target=1;nvn_render.framebuffer=0;nvn_render.target=t;nvn_render.depth=NULL;
            nvn_render.pending_color_valid=nvn_render.pending_depth_valid=0;aw_deferred.passes++;return 1;
        }
#endif
        nvn_depth_leave(nvn_render.target,nvn_render.depth,nvn_render.framebuffer,t,d);NVN_RT_STEP("depth-leave");
        if(nvn_render.depth && (nvn_render.depth!=d || nvn_render.target!=t) && !nvn_render.pass_skipped)zero_udd_leave(nvn_render.depth,nvn_render.framebuffer);
        nvn_normal_discard_target(nvn_render.target,nvn_render.framebuffer,t);NVN_RT_STEP("discard");
        /* Opt-in COTM_DEPTH_DISCARD_ALL: every pass clears its depth on entry,
         * so the depth tiles of the pass being left never need a write-back. */
        static int dd_configured;static unsigned dd_enabled;static uint64_t dd_count;
        if(!dd_configured){const char *o=getenv("COTM_DEPTH_DISCARD_ALL");dd_enabled=o&&!strcmp(o,"1");dd_configured=1;if(dd_enabled)printf("NVN_DEPTH_DISCARD_ALL_EXPERIMENT enabled=1 point=leaving-every-pass-with-depth\n");}
        if(dd_enabled&&nvn_render.framebuffer&&nvn_render.depth&&nvn_render.target!=t&&nvn_normal_discard_resolve()) {
            const GLenum attachment=GL_DEPTH_ATTACHMENT;nvn_normal_discard_state.discard(GL_FRAMEBUFFER,1,&attachment);
            if(++dd_count%3000==0)printf("NVN_DEPTH_DISCARD_ALL_PROFILE discards=%"PRIu64" pass-skips=%"PRIu64" pass-skipped-draws=%"PRIu64"\n",dd_count,nvn_render.pass_skips,nvn_render.pass_skipped_draws);
        }
        /* Opt-in COTM_NORMAL_PASS_SKIP: a target whose draws are all elided
         * (normal maps, guarded after the first elision) is never bound nor
         * cleared on the GPU; every pass clears its own depth on entry. */
        static int skip_configured;static unsigned skip_enabled;
        if(!skip_configured){const char *o=getenv("COTM_NORMAL_PASS_SKIP");skip_enabled=o&&!strcmp(o,"1");skip_configured=1;if(skip_enabled)printf("NVN_NORMAL_PASS_SKIP_EXPERIMENT enabled=1 skipped=bind+clears of guarded normal targets\n");}
        nvn_render.pass_skipped=skip_enabled&&t->normal_guarded&&nvn_low_color(t)<0;
        if(nvn_render.pass_skipped){nvn_render.framebuffer=0;nvn_render.target=t;nvn_render.depth=d;nvn_render.pass_skips++;nvn_render.pending_color_valid=nvn_render.pending_depth_valid=0;return 1;}
        nvn_render.framebuffer=nvn_framebuffer_bind(t,d);NVN_RT_STEP("bind");
        zero_udd_restore(d);
        if(t->host_scaled&&!t->host_scaled_bound){t->host_scaled_bound=1;
            printf("NVN_LC2_SCALED_TARGET object=0x%"PRIx64" id=%"PRIu64" texture=%u pool=0x%"PRIx64" format=0x%x flags=0x%x depth=%"PRIu64" framebuffer=%u logical=%ux%u actual=%ux%u shader-binding=resolved-at-draw\n",
                t->object,t->id,t->name,t->pool->object,t->b.format,t->b.flags,d?d->id:0,nvn_render.framebuffer,t->b.width,t->b.height,t->host_width,t->host_height);}
        nvn_render.target=t;nvn_render.depth=d;nvn_low_target(t,d);
        momo_window_clear_bind(t,nvn_replay.last_frame);
        nvn_framebuffer_report(nvn_replay.last_frame);nvn_normal_discard_report(nvn_replay.last_frame);
        nvn_depth_report(nvn_replay.last_frame);return 1;
    }
    case NI_nvnCommandBufferSetViewport:if(nvn_render.pass_skipped){nvn_low.viewport[0]=(int32_t)a[0];nvn_low.viewport[1]=(int32_t)a[1];nvn_low.viewport[2]=a[2];nvn_low.viewport[3]=a[3];nvn_low.viewport_known=1;return 1;}
        nvn_low_set_rect(nvn_render.target,1,(int32_t)a[0],(int32_t)a[1],a[2],a[3]);return 1;
    case NI_nvnCommandBufferSetScissor:if(nvn_render.pass_skipped){nvn_low.scissor[0]=(int32_t)a[0];nvn_low.scissor[1]=(int32_t)a[1];nvn_low.scissor[2]=a[2];nvn_low.scissor[3]=a[3];nvn_low.scissor_known=1;return 1;}
        nvn_low_set_rect(nvn_render.target,0,(int32_t)a[0],(int32_t)a[1],a[2],a[3]);return 1;
    case NI_nvnCommandBufferSetDepthRange:{float near,far;memcpy(&near,r->vectors,4);memcpy(&far,r->vectors+1,4);glDepthRangef(near,far);return 1;}
    case NI_nvnCommandBufferClearColor: {
        if(a[0]||!nvn_render.target)stop("NVN_CLEAR_TARGET",nvn_caller,a[0],24);
        momo_window_clear_seen(nvn_render.target);
        float color[4];memcpy(color,r->payload,16);
        if(nvn_mfc_omitting_draw()) {
            nvn_mfc_change(nvn_render.target,a[2]&MFC_COLOR,0);
            megaman_frame_cadence.clears_omitted++;return 1;
        }
        {static unsigned told;if(told++<12)printf("MOMO_CLEAR_COLOR target=%"PRIu64" rgba=%g,%g,%g,%g mask=0x%"PRIx64" draws=%u\n",
            nvn_render.target?nvn_render.target->id:0,color[0],color[1],color[2],color[3],a[2],nvn_replay.draws);}
        if(nvn_render.pass_skipped){memcpy(nvn_render.pending_color,color,16);nvn_render.pending_mask=a[2];nvn_render.pending_color_valid=1;}
        if(!nvn_skip.active&&!nvn_render.pass_skipped){nvn_render_state_epoch();nvn_render_color_mask(a[2]);glClearColor(color[0],color[1],color[2],color[3]);glClear(GL_COLOR_BUFFER_BIT);}
        if(!nvn_skip.active&&!nvn_render.pass_skipped&&mfc_full_scissor(nvn_low.scissor_known,nvn_low.scissor))
            nvn_mfc_change(nvn_render.target,a[2]&MFC_COLOR,1);
        nvn_low_clear(nvn_render.target,a[2]);nvn_normal_clear(nvn_render.target,a[2]);return 1;
    }
    case NI_nvnCommandBufferClearDepthStencil: {
        if(nvn_render.pass_skipped){if(a[0]){memcpy(&nvn_render.pending_depth,r->vectors,4);nvn_render.pending_depth_valid=1;}return 1;}
        if(!nvn_render.target || !nvn_render.framebuffer)stop("NVN_CLEAR_TARGET",nvn_caller,0,24);
        /* SDK 4.4.1 ClearDepthStencil: w1 depthMask, w2 stencilValue,
         * w3 stencilWriteMask; stencil fields are consumed as low eight bits. */
        if((a[2]&255)&&(!nvn_render.depth||nvn_render.depth->b.format!=0x35))
            stop("NVN_CLEAR_STENCIL_ATTACHMENT_REQUIRED",nvn_caller,a[2],20);
        if(nvn_mfc_omitting_draw()) {
            nvn_mfc_stencil_mark(nvn_render.depth,(a[0]?MFC_DEPTH:0)|((a[2]&255)?MFC_STENCIL:0),
                &(struct mfc_stencil_origin){.kind=2,.clear_depth=a[0],.clear_value=a[1],.clear_mask=a[2]});
            megaman_frame_cadence.clears_omitted++;return 1;
        }
        if(!nvn_skip.active && !nvn_render.pass_skipped && nvn_render.depth && !mfc_full_scissor(nvn_low.scissor_known,nvn_low.scissor))zero_udd_restore(nvn_render.depth);
        if((a[2]&255) && !nvn_skip.active) {
            if(!nvn_render.depth || nvn_render.depth->b.format!=0x35)
                stop("NVN_CLEAR_STENCIL_ATTACHMENT_REQUIRED",nvn_caller,a[2],20);
            GLint front,back;glGetIntegerv(GL_STENCIL_WRITEMASK,&front);glGetIntegerv(GL_STENCIL_BACK_WRITEMASK,&back);
            glStencilMask((GLuint)a[2]&255U);glClearStencil((GLint)a[1]&255);glClear(GL_STENCIL_BUFFER_BIT);
            glStencilMaskSeparate(GL_FRONT,(GLuint)front);glStencilMaskSeparate(GL_BACK,(GLuint)back);
        }
        if(a[0]&&!nvn_skip.active){float depth;memcpy(&depth,r->vectors,4);
            nvn_depth_before_clear(nvn_render.target,nvn_render.depth,nvn_render.framebuffer);
            nvn_render_state_epoch();nvn_render_depth_mask(GL_TRUE);glClearDepthf(depth);glClear(GL_DEPTH_BUFFER_BIT);
            nvn_depth_after_clear(nvn_render.target,nvn_render.depth,depth);}
        if(!nvn_skip.active){float depth;memcpy(&depth,r->vectors,4);zero_depth_after_clear(nvn_render.depth,a[0],depth,a[1],a[2]);}
        if(!nvn_skip.active&&mfc_full_scissor(nvn_low.scissor_known,nvn_low.scissor))
            nvn_mfc_change(nvn_render.depth,(a[0]?MFC_DEPTH:0)|((a[2]&255)==255?MFC_STENCIL:0),1);
        return 1;
    }
    case NI_nvnCommandBufferBarrier:
        if(a[0]&~UINT64_C(0xff))stop("NVN_BARRIER_BITS",nvn_caller,a[0],20);
        if(zero_gles_barrier_try(a[0]))return 1;
#ifdef ANIMAL_WELL_SHADERS
        {   /* Animal Well issues ~61 barriers per frame (one per render pass, ORDER_FRAGMENTS|INVALIDATE_TEXTURE class).
             * Every admitted operation is an in-order GLES command on one context; CPU visibility only ever comes from
             * fences/syncs, which keep their own waits. A glFinish here cost 240 ms/frame (measured aw088). */
            static uint64_t counts[256];static unsigned reported;
            counts[a[0]&0xff]++;
            if(!reported || (counts[a[0]&0xff]==1)){reported=1;printf("AW_BARRIER_NOOP bits=0x%"PRIx64" count=%"PRIu64" ordering=single-context-in-order completion=fences-only\n",a[0],counts[a[0]&0xff]);}
            return 1;
        }
#endif
#ifdef MOMO_SHADERS
        {   /* Unity 2021 issues 13 barriers per frame here, and a glFinish on each one cost 549 ms of a
             * 570 ms frame (MEDIDO run momo-130, COTM_REPLAY_PHASES). Every operation this host admits is
             * an in-order GLES command on a single context, and CPU visibility only ever comes from the
             * fences/syncs, which do their own waiting, so the ordering a barrier asks for already holds.
             * MOMO_BARRIER=finish|flush|noop keeps the stricter forms one environment variable away. */
            static int cfg;static unsigned mode;static uint64_t counts[256];
            if(!cfg){const char *o=getenv("MOMO_BARRIER");mode=!o||!strcmp(o,"noop")?0:!strcmp(o,"flush")?1:2;cfg=1;
                if(o&&strcmp(o,"noop")&&strcmp(o,"flush")&&strcmp(o,"finish"))stop("MOMO_BARRIER_OPTION",nvn_caller,0,24);}
            if(!counts[a[0]&0xff]++)printf("MOMO_BARRIER bits=0x%"PRIx64" mode=%s ordering=single-context-in-order completion=fences-only\n",
                a[0],mode?(mode==1?"flush":"finish"):"noop");
            if(mode==1)glFlush();else if(mode==2)glFinish();
            return 1;
        }
#endif
        /* Admitted operations are ordered GLES draws/uploads/FBO commands;
         * no compute, SSBO, indirect or mapped GL storage is exposed. Flush
         * submits that prefix without claiming CPU/GPU completion. */
        if(nvn_async_enabled() && !nvn_lc2_async_enabled())glFlush();else glFinish();return 1;
    case NI_nvnCommandBufferDrawArrays:case NI_nvnCommandBufferDrawElements:case NI_nvnCommandBufferDrawElementsBaseVertex:nvn_render_draw(r);return 1;
    default:return 0;
    }
}
