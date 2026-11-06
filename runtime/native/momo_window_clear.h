/* Is the image the game presents cleared before it is drawn into?
 *
 * MEDIDO (run momo-247): one single capture of /dev/fb0 holds BOTH the live menu list and the title's
 * "Press Any Button" underneath it, so the stale content is inside the presented image itself, not an
 * artefact of alternating buffers at present time (nvn_present.h presents exactly the index the game
 * asked for). The window has three images, so content a game leaves behind reappears every third frame
 * unless something covers or clears it.
 *
 * MOMO_WINDOW_CLEARS=1 counts, per window image, the passes that bind it and the clears that land on it.
 * MOMO_WINDOW_CLEAR_FORCE=1 is the experiment, never a fix: it clears the image to black the first time a
 * frame binds it. If the ghost goes away with it, the cause is a clear the host is dropping, and the fix
 * belongs wherever that clear is lost - not here.
 */
static struct {
    unsigned configured,census,force;
    uint64_t binds[3],clears[3],forced[3];
    unsigned frame,cleared_this_frame[3];
} momo_window_clear;
static void momo_window_clear_configure(void) {
    if(momo_window_clear.configured)return;
    const char *c=getenv("MOMO_WINDOW_CLEARS"),*f=getenv("MOMO_WINDOW_CLEAR_FORCE");
    momo_window_clear.census=c&&!strcmp(c,"1");
    momo_window_clear.force=f&&!strcmp(f,"1");
    momo_window_clear.configured=1;
    if(momo_window_clear.census||momo_window_clear.force)
        printf("MOMO_WINDOW_CLEAR_CONFIG census=%u force=%u images=%u scope=window-images-only\n",
            momo_window_clear.census,momo_window_clear.force,nvn_window.count);
}
static int momo_window_image_index(const struct nvn_texture *t) {
    if(!t||!nvn_window.live)return -1;
    for(unsigned i=0;i<nvn_window.count&&i<3;i++)if(nvn_window.textures[i]==t)return (int)i;
    return -1;
}
/* Called when a pass binds a target, with the framebuffer already bound. */
static void momo_window_clear_bind(const struct nvn_texture *t,unsigned frame) {
    momo_window_clear_configure();
    if(!momo_window_clear.census&&!momo_window_clear.force)return;
    int index=momo_window_image_index(t);
    if(index<0)return;
    if(frame!=momo_window_clear.frame) {
        momo_window_clear.frame=frame;
        for(unsigned i=0;i<3;i++)momo_window_clear.cleared_this_frame[i]=0;
    }
    momo_window_clear.binds[index]++;
    if(momo_window_clear.force&&!momo_window_clear.cleared_this_frame[index]) {
        GLboolean mask[4];glGetBooleanv(GL_COLOR_WRITEMASK,mask);
        GLboolean scissor=glIsEnabled(GL_SCISSOR_TEST);
        GLfloat previous[4];glGetFloatv(GL_COLOR_CLEAR_VALUE,previous);
        if(scissor)glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glClearColor(0.0f,0.0f,0.0f,1.0f);glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(previous[0],previous[1],previous[2],previous[3]);
        glColorMask(mask[0],mask[1],mask[2],mask[3]);
        if(scissor)glEnable(GL_SCISSOR_TEST);
        momo_window_clear.forced[index]++;
    }
    momo_window_clear.cleared_this_frame[index]=1;
    if(momo_window_clear.census&&(momo_window_clear.binds[0]+momo_window_clear.binds[1]+momo_window_clear.binds[2])%180==0)
        printf("MOMO_WINDOW_CLEAR_PROFILE binds=%"PRIu64"/%"PRIu64"/%"PRIu64" clears=%"PRIu64"/%"PRIu64"/%"PRIu64
               " forced=%"PRIu64"/%"PRIu64"/%"PRIu64" frame=%u scope=cumulative\n",
            momo_window_clear.binds[0],momo_window_clear.binds[1],momo_window_clear.binds[2],
            momo_window_clear.clears[0],momo_window_clear.clears[1],momo_window_clear.clears[2],
            momo_window_clear.forced[0],momo_window_clear.forced[1],momo_window_clear.forced[2],frame);
}
/* Called when the game's own ClearColor lands on a target. */
static void momo_window_clear_seen(const struct nvn_texture *t) {
    momo_window_clear_configure();
    if(!momo_window_clear.census)return;
    int index=momo_window_image_index(t);
    if(index>=0)momo_window_clear.clears[index]++;
}
/* MOMO_WINDOW_DRAWS=<frame>: for that frame, name every draw that lands on a window image with the state
 * that decides whether it COVERS what is already there. The game never clears these images (MEDIDO run
 * momo-249: clears=0/0/0 over 960 frames), so on the console something must write the whole image opaquely
 * every frame; whatever that is, if it blends here instead of replacing, the previous frame shows through. */
static void momo_window_draws_census(const struct nvn_texture *t,unsigned frame,unsigned draw,
                                     unsigned vertex_source,unsigned fragment_source) {
    static int configured;static unsigned wanted;
    if(!configured){const char *o=getenv("MOMO_WINDOW_DRAWS");wanted=o?(unsigned)strtoul(o,NULL,0):0;configured=1;}
    if(!wanted||frame!=wanted)return;
    int index=momo_window_image_index(t);
    if(index<0)return;
    GLboolean blend=glIsEnabled(GL_BLEND),mask[4];glGetBooleanv(GL_COLOR_WRITEMASK,mask);
    GLint src_rgb=0,dst_rgb=0,src_a=0,dst_a=0;
    glGetIntegerv(GL_BLEND_SRC_RGB,&src_rgb);glGetIntegerv(GL_BLEND_DST_RGB,&dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA,&src_a);glGetIntegerv(GL_BLEND_DST_ALPHA,&dst_a);
    GLint viewport[4]={0,0,0,0};glGetIntegerv(GL_VIEWPORT,viewport);
    GLboolean scissor=glIsEnabled(GL_SCISSOR_TEST);GLint box[4]={0,0,0,0};glGetIntegerv(GL_SCISSOR_BOX,box);
    printf("MOMO_WINDOW_DRAW frame=%u image=%d draw=%u sources=%u/%u blend=%d src=0x%x/0x%x dst=0x%x/0x%x "
           "mask=%d%d%d%d viewport=%d,%d,%dx%d scissor=%d box=%d,%d,%dx%d size=%ux%u\n",
        frame,index,draw,vertex_source,fragment_source,blend?1:0,src_rgb,src_a,dst_rgb,dst_a,
        mask[0]?1:0,mask[1]?1:0,mask[2]?1:0,mask[3]?1:0,
        viewport[0],viewport[1],viewport[2],viewport[3],scissor?1:0,box[0],box[1],box[2],box[3],
        t->b.width,t->b.height);
}
