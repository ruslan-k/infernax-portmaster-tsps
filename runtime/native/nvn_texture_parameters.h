/* Texture parameters are shared-object state. Every writer (initialization,
 * sampler binding and present) passes here; pixel uploads do not reset them.
 * A reused resource slot/name cannot reuse the previous identity's values. */
static pthread_once_t nvn_texture_parameter_once=PTHREAD_ONCE_INIT;
static struct {unsigned enabled,after_frame;uint64_t sent,skipped,resets;} nvn_texture_parameter_state;
static void nvn_texture_parameter_configure(void) {
    const char *option=getenv("COTM_TEXTURE_PARAMETER_CACHE");
    if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("NVN_TEXTURE_PARAMETER_OPTION",nvn_caller,0,24);
    nvn_texture_parameter_state.enabled=option&&!strcmp(option,"1");
}
static void nvn_texture_parameters(struct nvn_texture *t,GLint min,GLint mag,GLint s,GLint v) {
    t=zero_texture_storage_state(t);
    pthread_once(&nvn_texture_parameter_once,nvn_texture_parameter_configure);
    const GLenum names[4]={GL_TEXTURE_MIN_FILTER,GL_TEXTURE_MAG_FILTER,GL_TEXTURE_WRAP_S,GL_TEXTURE_WRAP_T};
    const GLint values[4]={min,mag,s,v};
    unsigned cache=nvn_texture_parameter_state.enabled;
    if(cache) {
        if(!t || !t->id || !t->name)stop("NVN_TEXTURE_PARAMETER_IDENTITY",nvn_caller,t?t->id:0,24);
        if(t->parameter_id!=t->id || t->parameter_name!=t->name || t->parameter_context!=nvn_device.context) {
            t->parameter_known=0;t->parameter_id=t->id;t->parameter_name=t->name;t->parameter_context=nvn_device.context;
            nvn_texture_parameter_state.resets++;
        }
    }
    for(unsigned i=0;i<4;i++) {
        unsigned bit=1U<<i;
        if(cache && (t->parameter_known&bit) && t->parameters[i]==values[i]) {
            nvn_texture_parameter_state.skipped++;continue;
        }
        glTexParameteri(GL_TEXTURE_2D,names[i],values[i]);nvn_texture_parameter_state.sent++;
        if(cache){t->parameters[i]=values[i];t->parameter_known|=bit;}
    }
}
static void nvn_texture_parameter_report(unsigned frame) {
    if(!nvn_texture_parameter_state.enabled || frame<nvn_texture_parameter_state.after_frame || frame-nvn_texture_parameter_state.after_frame<60)return;
    printf("NVN_TEXTURE_PARAMETER_CACHE_PROFILE after-frame=%u sent=%"PRIu64" skipped=%"PRIu64" identity-resets=%"PRIu64
           " scope=cumulative shared-object=1 bindings=always writers=initialize-sampler-present\n",frame,
           nvn_texture_parameter_state.sent,nvn_texture_parameter_state.skipped,nvn_texture_parameter_state.resets);
    nvn_texture_parameter_state.after_frame=frame;
}
