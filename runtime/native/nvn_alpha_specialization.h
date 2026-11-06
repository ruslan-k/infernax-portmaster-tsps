/* Specialize only our appended alpha wrapper. Pinned guest sources and the
 * complete nvnOriginalMain prefix (including its own discards) stay unchanged. */
static const char nvn_alpha_wrapper[]=
    "\nuniform float nvnAlphaRef;uniform int nvnAlphaFunc;\n"
    "void main(){nvnOriginalMain();float a=gl_FragColor.a;float r=nvnAlphaRef;int f=nvnAlphaFunc;bool pass=f==8 || (f==2&&a<r) || (f==3&&a==r) || (f==4&&a<=r) || (f==5&&a>r) || (f==6&&a!=r) || (f==7&&a>=r);if(!pass)discard;}\n";
static const char nvn_alpha5_wrapper[]="\nvoid main(){nvnOriginalMain();if(!(gl_FragColor.a>0.0))discard;}\n";
static char *nvn_alpha_source(const char *source) {
    size_t size=strlen(source),tail=sizeof(nvn_alpha_wrapper)-1;
    if(size>32768 || size<tail || memcmp(source+size-tail,nvn_alpha_wrapper,tail))return NULL;
    size_t prefix=size-tail;char *result=malloc(prefix+sizeof(nvn_alpha5_wrapper));
    if(!result)return NULL;
    memcpy(result,source,prefix);memcpy(result+prefix,nvn_alpha5_wrapper,sizeof(nvn_alpha5_wrapper));return result;
}
static unsigned nvn_alpha_selected(const struct nvn_program *p,unsigned function,float reference) {
    uint32_t bits;memcpy(&bits,&reference,sizeof(bits));
    /* The guest FPCR can enable flush-to-zero; bit inspection must not admit
     * a nonzero subnormal reference through a floating-point comparison. */
    return p->alpha5.name && function==5 && !(bits&0x7fffffffU);
}
static void nvn_alpha_build(struct nvn_program *p,GLuint vertex_shader) {
    const char *option=getenv("COTM_ALPHA_SPECIALIZE");
    if(option && strcmp(option,"0") && strcmp(option,"1"))stop("NVN_ALPHA_OPTION",nvn_caller,0,24);
    if(!option || !strcmp(option,"0"))return;
    const char *reason="source-or-memory";char *source=nvn_alpha_source(nvn_shader_sources[p->sources[1]].gles);
    GLuint fragment=0,name=0;unsigned attached=0;struct nvn_alpha_program variant={0};
    if(!source)goto fallback;
    reason="create-fragment";fragment=glCreateShader(GL_FRAGMENT_SHADER);if(!fragment)goto fallback;
    const char *input=source;glShaderSource(fragment,1,&input,NULL);glCompileShader(fragment);
    GLint status=0;glGetShaderiv(fragment,GL_COMPILE_STATUS,&status);reason="compile";if(!status)goto fallback;
    reason="create-program";name=glCreateProgram();if(!name)goto fallback;
    glAttachShader(name,vertex_shader);glAttachShader(name,fragment);attached=1;
    for(unsigned i=0;i<nvn_compilation.attribute_count;i++)
        glBindAttribLocation(name,nvn_compilation.attributes[i].location,nvn_compilation.attributes[i].name);
    glLinkProgram(name);glGetProgramiv(name,GL_LINK_STATUS,&status);reason="link";if(!status)goto fallback;
    for(unsigned i=0;i<nvn_compilation.attribute_count;i++) {
        GLint actual=glGetAttribLocation(name,nvn_compilation.attributes[i].name);reason="attribute-location";
        if(actual>=0 && (unsigned)actual!=nvn_compilation.attributes[i].location)goto fallback;
    }
    for(unsigned i=0;i<p->uniform_count;i++) {
        variant.locations[i]=glGetUniformLocation(name,p->uniforms[i].name);reason="uniform-activity";
        /* Keep the original guest-read/descriptor validation contract even if
         * a driver optimizes the alternate interface differently. */
        if((variant.locations[i]<0)!=(p->uniforms[i].location<0))goto fallback;
    }
    variant.clip_control=glGetUniformLocation(name,"nvnClipControl");reason="clip-control-interface";
    if(variant.clip_control<0)goto fallback;
    reason="alpha-interface";
    if(glGetUniformLocation(name,"nvnAlphaRef")>=0 || glGetUniformLocation(name,"nvnAlphaFunc")>=0)goto fallback;
    glDetachShader(name,vertex_shader);glDetachShader(name,fragment);glDeleteShader(fragment);free(source);
    variant.name=name;p->alpha5=variant;
    printf("NVN_ALPHA_SPECIALIZATION program=%u alternate=%u sources=%u/%u status=ready condition=func5-refzero original-main=preserved locations=resolved\n",
           p->name,name,p->sources[0],p->sources[1]);return;
fallback:
    if(attached){glDetachShader(name,vertex_shader);glDetachShader(name,fragment);}
    if(name)glDeleteProgram(name);
    if(fragment)glDeleteShader(fragment);
    free(source);
    printf("NVN_ALPHA_SPECIALIZATION program=%u sources=%u/%u status=canonical-fallback reason=%s\n",p->name,p->sources[0],p->sources[1],reason);
}
static void nvn_alpha_delete(struct nvn_program *p) {
    if(p->alpha5.name)glDeleteProgram(p->alpha5.name);
    p->alpha5=(struct nvn_alpha_program){0};
}
