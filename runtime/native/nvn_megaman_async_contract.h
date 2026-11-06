/* LC2 opt-in: replay stays on the calling guest thread. Only GPU completion
 * is deferred, and only an observed EGL fence signal can publish it. */
#ifndef NVN_MEGAMAN_ASYNC_CONTRACT_H
#define NVN_MEGAMAN_ASYNC_CONTRACT_H
#define NVN_LC2_ASYNC_PENDING_LIMIT 8U
static unsigned nvn_lc2_async_option(unsigned queue_enabled) {
    const char *option=getenv("MEGAMAN_ASYNC_QUEUE");
    if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("NVN_LC2_ASYNC_OPTION",nvn_caller,0,24);
    if(!option||strcmp(option,"1"))return 0;
#ifndef LC2_MAIN_BASE
    stop("NVN_LC2_ASYNC_PORT",nvn_caller,0,24);
#endif
    if(!queue_enabled)stop("NVN_LC2_ASYNC_DEPENDENCY",nvn_caller,0,24);
    const char *incompatible[]={"COTM_EARLY_FENCE","COTM_GL_THREAD","COTM_ASYNC_FRAME",
        "COTM_SINGLE_CONTEXT","MEGAMAN_SINGLE_CONTEXT_TRANSFER"};
    for(unsigned i=0;i<sizeof(incompatible)/sizeof(incompatible[0]);i++) {
        const char *value=getenv(incompatible[i]);
        if(value&&strcmp(value,"0"))stop("NVN_LC2_ASYNC_INCOMPATIBLE",nvn_caller,i,24);
    }
    return 1;
}
#endif
