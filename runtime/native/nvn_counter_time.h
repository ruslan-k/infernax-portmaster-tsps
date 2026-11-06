/* Explicit approximation experiment: the optional value is CPU wall time at
 * command replay, not a GPU timestamp or evidence that the GPU has completed.
 * Submit retains its separate, real completion operation and lifetime guards. */
static struct {unsigned configured,enabled;} nvn_counter_time_state;
static unsigned nvn_counter_time_enabled(void) {
    if(!nvn_counter_time_state.configured) {
        const char *option=getenv("COTM_COUNTER_CPU_TIME");
        if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("NVN_COUNTER_TIME_OPTION",nvn_caller,0,24);
        nvn_counter_time_state.enabled=option&&!strcmp(option,"1");nvn_counter_time_state.configured=1;
        if(nvn_counter_time_state.enabled)
            printf("NVN_COUNTER_TIME_EXPERIMENT enabled=1 timestamp=cpu-wall-enqueue-approximate gpu-timestamp=0 submit-completion=unchanged\n");
    }
    return nvn_counter_time_state.enabled;
}
static uint64_t nvn_counter_timestamp(void) {
    if(!nvn_counter_time_enabled())glFinish();
    return nvn_nanoseconds();
}
