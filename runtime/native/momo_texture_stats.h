/* MOMO_TEXTURE_STATS=1: once per texture id, the per-channel min/max/mean of the image as the
 * DRIVER actually holds it, read straight out of GL. "The fragment computes the wrong colour" has
 * two halves - the code and the image it samples - and this names which half is wrong without
 * touching the guest: no upload, no repair, one read-back per texture. */
#ifndef MOMO_TEXTURE_STATS_H
#define MOMO_TEXTURE_STATS_H
static uint64_t momo_texture_stats_seen[256];
static unsigned momo_texture_stats_count;
static void momo_texture_stats(struct nvn_texture *t,unsigned vs,unsigned fs,unsigned unit,unsigned frame) {
    static int cfg,on,thumb;static unsigned after;
    if(!cfg){on=getenv("MOMO_TEXTURE_STATS")!=NULL;thumb=getenv("MOMO_TEXTURE_THUMB")!=NULL;
        const char *a=getenv("MOMO_TEXTURE_STATS_AFTER");after=a?(unsigned)strtoul(a,NULL,0):0;cfg=1;}
    if(!on||frame<after)return;
    if(!t||!t->b.width||!t->b.height||!t->name) {
        static unsigned told;
        if(told++<8)printf("MOMO_TEXTURE_STATS_SKIP texture=%p gl-name=%u size=%ux%u\n",
            (void *)t,t?t->name:0,t?t->b.width:0,t?t->b.height:0);
        return;
    }
    for(unsigned i=0;i<momo_texture_stats_count;i++)if(momo_texture_stats_seen[i]==t->id)return;
    if(momo_texture_stats_count>=256)return;
    momo_texture_stats_seen[momo_texture_stats_count++]=t->id;
    unsigned w=t->b.width,h=t->b.height;
    if(w>512)w=512;
    if(h>512)h=512;
    size_t bytes=(size_t)w*h*4;uint8_t *pixels=malloc(bytes);
    if(!pixels)return;
    GLint previous,pack;GLuint framebuffer=0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&previous);glGetIntegerv(GL_PACK_ALIGNMENT,&pack);
    while(glGetError()!=GL_NO_ERROR){}
    glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t->name,0);
    GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
    unsigned ok=status==GL_FRAMEBUFFER_COMPLETE;
    if(ok){glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,(GLsizei)w,(GLsizei)h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);}
    GLenum error=glGetError();
    glPixelStorei(GL_PACK_ALIGNMENT,pack);glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)previous);glDeleteFramebuffers(1,&framebuffer);
    if(ok&&!error) {
        unsigned lo[4]={255,255,255,255},hi[4]={0,0,0,0};unsigned long long sum[4]={0,0,0,0};
        for(size_t k=0;k<bytes;k+=4)for(unsigned c=0;c<4;c++){
            unsigned v=pixels[k+c];if(v<lo[c])lo[c]=v;if(v>hi[c])hi[c]=v;sum[c]+=v;}
        size_t n=bytes/4;
        printf("MOMO_TEXTURE_STATS id=%" PRIu64 " pair=%u/%u unit=%u format=0x%x size=%ux%u sampled=%ux%u levels=%u"
               " swizzle=%u/%u/%u/%u target-seen=%u r=%u..%u/%llu g=%u..%u/%llu b=%u..%u/%llu a=%u..%u/%llu\n",
            t->id,vs,fs,unit,t->b.format,t->b.width,t->b.height,w,h,t->b.levels,
            t->b.swizzle[0],t->b.swizzle[1],t->b.swizzle[2],t->b.swizzle[3],t->composition_target_seen,
            lo[0],hi[0],sum[0]/n,lo[1],hi[1],sum[1]/n,lo[2],hi[2],sum[2]/n,lo[3],hi[3],sum[3]/n);
    }
    else printf("MOMO_TEXTURE_STATS id=%" PRIu64 " pair=%u/%u unit=%u format=0x%x size=%ux%u unreadable status=0x%x error=0x%x\n",
        t->id,vs,fs,unit,t->b.format,t->b.width,t->b.height,(unsigned)status,(unsigned)error);
    /* MOMO_TEXTURE_THUMB=1: a 64-wide RGB thumbnail in base64 on the same line, so the image the
     * shader samples can actually be LOOKED AT from the log instead of described by three numbers. */
    if(thumb&&ok&&!error) {
        unsigned tw=w<64?w:64,th=(unsigned)((uint64_t)h*tw/w);if(!th)th=1;if(th>64)th=64;
        size_t rgb_bytes=(size_t)tw*th*3;uint8_t *rgb=malloc(rgb_bytes);
        if(rgb) {
            for(unsigned y=0;y<th;y++)for(unsigned x=0;x<tw;x++) {
                unsigned sx=x*w/tw,sy=y*h/th;const uint8_t *src=pixels+((size_t)sy*w+sx)*4;
                rgb[((size_t)y*tw+x)*3]=src[0];rgb[((size_t)y*tw+x)*3+1]=src[1];rgb[((size_t)y*tw+x)*3+2]=src[2];
            }
            static const char b64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            /* One write, not one putchar per character: several host threads share this stdout and a long
             * line printed piecemeal comes back interleaved with IPC traces (MEDIDO run momo-193, where the
             * big thumbnails were the ones that got cut). */
            size_t head=128,text=head+4*((rgb_bytes+2)/3)+2;char *line=malloc(text);
            if(line) {
                int at=snprintf(line,head,"MOMO_TEXTURE_THUMB id=%" PRIu64 " size=%ux%u rgb=",t->id,tw,th);
                for(size_t k=0;k<rgb_bytes;k+=3) {
                    unsigned v=(unsigned)rgb[k]<<16|(unsigned)(k+1<rgb_bytes?rgb[k+1]:0)<<8|(k+2<rgb_bytes?rgb[k+2]:0);
                    line[at++]=b64[v>>18&63];line[at++]=b64[v>>12&63];
                    line[at++]=k+1<rgb_bytes?b64[v>>6&63]:'=';line[at++]=k+2<rgb_bytes?b64[v&63]:'=';
                }
                line[at++]='\n';fwrite(line,1,(size_t)at,stdout);free(line);
            }
            free(rgb);
        }
    }
    free(pixels);
}
#endif
