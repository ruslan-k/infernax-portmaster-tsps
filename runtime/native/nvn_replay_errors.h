/* B12 preserves an error boundary before a list can be submitted as complete.
 * Explicit draw diagnostics retain the finer per-record attribution. */
static unsigned nvn_replay_errors_per_list;
static const char *nvn_replay_last_op="start";
static pthread_once_t nvn_replay_errors_once=PTHREAD_ONCE_INIT;
static void nvn_replay_errors_configure(void) {
    const char *option=getenv("COTM_GL_ERROR_PER_LIST");
    if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("NVN_GL_ERROR_OPTION",nvn_caller,0,24);
    nvn_replay_errors_per_list=option&&!strcmp(option,"1");
    if(nvn_replay_errors_per_list)printf("NVN_GL_ERROR_EXPERIMENT cadence=list diagnostic-cadence=record errors=terminal-before-submit\n");
}
static unsigned nvn_replay_errors_batched(void) {
    if(pthread_once(&nvn_replay_errors_once,nvn_replay_errors_configure))stop("NVN_GL_ERROR_ONCE",nvn_caller,0,21);
    return nvn_replay_errors_per_list;
}
static void nvn_replay_error_boundary(void) {
    GLenum error=glGetError();if(error){printf("NVN_GL_ERROR op=%s error=0x%x\n",nvn_replay_last_op,(unsigned)error);stop("NVN_REPLAY_GL_ERROR",nvn_caller,error,21);}
}
