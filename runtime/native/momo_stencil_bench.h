/* MOMO_STENCIL_SELFTEST=1: does this driver honour a stencil mask on a texture FBO with a packed
 * D24S8 renderbuffer, exactly as the UI mask path uses it?
 *
 * Unity's UI hides the menu behind a stencil mask: one draw writes REPLACE 1 with the colour mask
 * closed, the children then draw with EQUAL 1. On this port the children are discarded (MEDIDO run
 * momo-163: disabling only the stencil test brings the text back), so the question is whether the
 * write lands at all. This bench answers it without any game state. */
#ifndef MOMO_STENCIL_BENCH_H
#define MOMO_STENCIL_BENCH_H
static void momo_stencil_selftest(void) {
    static int done;
    if(done || !getenv("MOMO_STENCIL_SELFTEST"))return;
    done=1;
    static const char *vs="attribute vec2 p;void main(){gl_Position=vec4(p,0.,1.);}";
    static const char *fs="precision mediump float;uniform vec4 c;void main(){gl_FragColor=c;}";
    GLuint program=glCreateProgram(),shaders[2];
    for(unsigned i=0;i<2;i++) {
        const char *src=i?fs:vs;
        shaders[i]=glCreateShader(i?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);
        glShaderSource(shaders[i],1,&src,NULL);glCompileShader(shaders[i]);
        GLint ok=0;glGetShaderiv(shaders[i],GL_COMPILE_STATUS,&ok);
        if(!ok){printf("MOMO_STENCIL_SELFTEST status=shader-compile-failed stage=%u\n",i);return;}
        glAttachShader(program,shaders[i]);
    }
    glBindAttribLocation(program,0,"p");glLinkProgram(program);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
    if(!linked){printf("MOMO_STENCIL_SELFTEST status=link-failed\n");return;}
    while(glGetError()!=GL_NO_ERROR){}   /* start from a clean error state */
    GLint previous=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&previous);
    GLuint texture=0,depth=0,fbo=0;
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glGenRenderbuffers(1,&depth);glBindRenderbuffer(GL_RENDERBUFFER,depth);
    glRenderbufferStorage(GL_RENDERBUFFER,0x88f0,64,64);   /* GL_DEPTH24_STENCIL8_OES, as the game's D24S8 */
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_RENDERBUFFER,depth);
    GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
    GLenum setup_error=glGetError();
    GLint stencil_bits=0;glGetIntegerv(GL_STENCIL_BITS,&stencil_bits);
    glViewport(0,0,64,64);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);
    glClearColor(0,0,0,1);glClearStencil(0);glClear(GL_COLOR_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    glUseProgram(program);
    GLint colour=glGetUniformLocation(program,"c");
    static const GLfloat left[]={-1,-1, 0,-1, -1,1, 0,1};      /* left half writes the mask */
    static const GLfloat full[]={-1,-1, 1,-1, -1,1, 1,1};
    glEnableVertexAttribArray(0);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS,1,0xff);glStencilMask(0xff);glStencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);
    glColorMask(0,0,0,0);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,left);glUniform4f(colour,1,1,1,1);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    glColorMask(1,1,1,1);
    glStencilFunc(GL_EQUAL,1,0xff);glStencilMask(0);glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,full);glUniform4f(colour,1,0,0,1);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    GLenum draw_error=glGetError();
    uint8_t pixels[64*64*4];memset(pixels,0,sizeof(pixels));glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    unsigned left_red=0,right_red=0;
    for(unsigned y=0;y<64;y++)for(unsigned x=0;x<64;x++) {
        const uint8_t *p=pixels+(y*64+x)*4;
        if(p[0]>200&&p[1]<40){if(x<32)left_red++;else right_red++;}
    }
    printf("MOMO_STENCIL_SELFTEST status=%s fbo-status=0x%x stencil-bits=%d masked-half-red=%u unmasked-half-red=%u error=0x%x expected=1024/0\n",
        (left_red>1000&&!right_red)?"PASS":"FAIL",status,stencil_bits,left_red,right_red,glGetError());
    printf("MOMO_STENCIL_SELFTEST_DETAIL setup-error=0x%x draw-error=0x%x first-pixels=%u,%u,%u,%u %u,%u,%u,%u\n",
        setup_error,draw_error,pixels[0],pixels[1],pixels[2],pixels[3],pixels[4*33],pixels[4*33+1],pixels[4*33+2],pixels[4*33+3]);
    glDisable(GL_STENCIL_TEST);glDisableVertexAttribArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER,previous);
    glDeleteFramebuffers(1,&fbo);glDeleteRenderbuffers(1,&depth);glDeleteTextures(1,&texture);
    for(unsigned i=0;i<2;i++){glDetachShader(program,shaders[i]);glDeleteShader(shaders[i]);}
    glDeleteProgram(program);
    nvn_render_state.valid=0;
}

/* MOMO_STENCIL_PROBE=1: right after a draw that wrote the stencil, paint the pixels whose stencil now
 * equals 1 and count them. It answers "did the mask write land?" inside the game's own framebuffer,
 * which no state dump can. Diagnostic only: it paints into the frame. */
static struct {GLuint program;GLint colour;unsigned reported;} momo_probe;
static void momo_stencil_probe(unsigned draw,unsigned vertex,unsigned fragment) {
    static int cfg;static unsigned enabled;
    if(!cfg){enabled=getenv("MOMO_STENCIL_PROBE")!=NULL;cfg=1;}
    if(!enabled || momo_probe.reported>=6)return;
    if(!momo_probe.program) {
        static const char *vs="attribute vec2 p;void main(){gl_Position=vec4(p,0.,1.);}";
        static const char *fs="precision mediump float;uniform vec4 c;void main(){gl_FragColor=c;}";
        GLuint program=glCreateProgram(),shaders[2];
        for(unsigned i=0;i<2;i++){const char *src=i?fs:vs;shaders[i]=glCreateShader(i?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);
            glShaderSource(shaders[i],1,&src,NULL);glCompileShader(shaders[i]);glAttachShader(program,shaders[i]);}
        glBindAttribLocation(program,0,"p");glLinkProgram(program);
        GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);
        if(!ok){momo_probe.reported=6;return;}
        momo_probe.program=program;momo_probe.colour=glGetUniformLocation(program,"c");
    }
    static const GLfloat full[]={-1,-1, 1,-1, -1,1, 1,1};
    glUseProgram(momo_probe.program);glUniform4f(momo_probe.colour,1,0,0,1);
    glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glColorMask(1,1,1,1);
    glEnable(GL_STENCIL_TEST);glStencilFunc(GL_EQUAL,1,0xff);glStencilMask(0);glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,full);
    for(unsigned i=1;i<8;i++)glDisableVertexAttribArray(i);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    unsigned w=160,h=90;size_t bytes=(size_t)w*h*4;uint8_t *pixels=malloc(bytes);
    unsigned red=0;GLenum error=0;
    if(pixels) {
        glReadPixels(0,0,(GLsizei)w,(GLsizei)h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        error=glGetError();
        for(size_t k=0;k<bytes;k+=4) {
            if(pixels[k]>200&&pixels[k+1]<40)red++;
        }
        free(pixels);
    }
    printf("MOMO_STENCIL_PROBE draw=%u sources=%u/%u stencil-equal-1-pixels=%u of %u error=0x%x\n",
        draw,vertex,fragment,red,w*h,error);
    /* Second half: can the driver write the stencil of THIS framebuffer at all? Write REPLACE 1 over the
     * whole surface, once with the colour mask closed (as Unity does) and once with it open, and count
     * what the EQUAL test then sees. */
    for(unsigned closed=0;closed<2;closed++) {
        glClearStencil(0);glStencilMask(0xff);glClear(GL_STENCIL_BUFFER_BIT);
        glColorMask(closed?0:1,closed?0:1,closed?0:1,closed?0:1);
        glStencilFunc(GL_ALWAYS,1,0xff);glStencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);
        glUniform4f(momo_probe.colour,0,1,0,1);
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);
        glColorMask(1,1,1,1);glStencilFunc(GL_EQUAL,1,0xff);glStencilMask(0);glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);
        glUniform4f(momo_probe.colour,1,0,0,1);
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);
        unsigned again=0;uint8_t *more=malloc(bytes);
        if(more) {
            glReadPixels(0,0,(GLsizei)w,(GLsizei)h,GL_RGBA,GL_UNSIGNED_BYTE,more);
            for(size_t k=0;k<bytes;k+=4) {
                if(more[k]>200&&more[k+1]<40)again++;
            }
            free(more);
        }
        printf("MOMO_STENCIL_WRITE_TEST draw=%u colour-mask=%s equal-1-pixels=%u of %u error=0x%x\n",
            draw,closed?"closed":"open",again,w*h,glGetError());
    }
    momo_probe.reported++;
    nvn_render_state.valid=0;
}
#endif
