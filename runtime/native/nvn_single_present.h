/* Preserve the producer state consumed before the next Draw. Program,
 * fixed-state and vertex bindings are rebuilt by the existing cold epoch;
 * textures/uniform guest data continue to be validated on every draw. */
struct nvn_single_present_state {
    GLint framebuffer,viewport[4],scissor[4];
    GLboolean scissor_enabled;
    unsigned active;
};
static struct {int configured;unsigned enabled;uint64_t lean_saves,skipped_receipts;} nvn_present_lean;
static unsigned nvn_present_lean_enabled(void) {
    if(!nvn_present_lean.configured){const char *o=getenv("COTM_PRESENT_LEAN");nvn_present_lean.enabled=o&&!strcmp(o,"1");nvn_present_lean.configured=1;
        if(nvn_present_lean.enabled)printf("NVN_PRESENT_LEAN_EXPERIMENT enabled=1 save=tracked-state-no-glGet receipts=sampled-only\n");}
    return nvn_present_lean.enabled;
}
static void nvn_single_present_save(struct nvn_single_present_state *state) {
    *state=(struct nvn_single_present_state){0};
    if(!nvn_single_enabled())return;
    nvn_single_require_owner();
    if(nvn_present_lean_enabled()) {
        /* Tracked producer state: FBO from the cache, rects reapplied through
         * the LOW helper for the current target (which owns the GL values). */
        state->framebuffer=(GLint)nvn_framebuffers.active;state->active=2;nvn_present_lean.lean_saves++;return;
    }
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&state->framebuffer);
    glGetIntegerv(GL_VIEWPORT,state->viewport);glGetIntegerv(GL_SCISSOR_BOX,state->scissor);
    state->scissor_enabled=glIsEnabled(GL_SCISSOR_TEST);
    GLenum error=glGetError();if(error)stop("NVN_SINGLE_PRESENT_SAVE",nvn_caller,error,21);
    state->active=1;
}
static void nvn_single_present_quad(GLuint program,GLuint vertices) {
    if(!nvn_single_enabled())return;
    nvn_single_require_owner();
    if(!program || !vertices || nvn_device.attributes<2)stop("NVN_SINGLE_PRESENT_OBJECT",nvn_caller,program,24);
    glUseProgram(program);glBindBuffer(GL_ARRAY_BUFFER,vertices);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
    unsigned limit=nvn_device.attributes<16?(unsigned)nvn_device.attributes:16;
    for(unsigned i=2;i<limit;i++)glDisableVertexAttribArray(i);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,16,0);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,16,(void *)8);
    glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
}
static void nvn_single_present_restore(struct nvn_single_present_state *state) {
    if(!state->active)return;
    nvn_single_require_owner();
    glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)state->framebuffer);
    if(state->active==2) {
        if(nvn_render.target){if(nvn_low.viewport_known)nvn_low_apply_rect(nvn_render.target,1);if(nvn_low.scissor_known)nvn_low_apply_rect(nvn_render.target,0);}
        GLenum error=glGetError();if(error)stop("NVN_SINGLE_PRESENT_RESTORE",nvn_caller,error,21);
        state->active=0;nvn_single_state.brackets++;return;
    }
    glViewport(state->viewport[0],state->viewport[1],state->viewport[2],state->viewport[3]);
    glScissor(state->scissor[0],state->scissor[1],state->scissor[2],state->scissor[3]);
    if(state->scissor_enabled)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
    GLenum error=glGetError();if(error)stop("NVN_SINGLE_PRESENT_RESTORE",nvn_caller,error,21);
    state->active=0;nvn_single_state.brackets++;
}
