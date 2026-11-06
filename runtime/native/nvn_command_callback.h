/* Invoke the actual game callback through the existing AArch64 bridge.
 * SDK2d6478 requests command event0/max(required,512); SDK2d6190 requests
 * control event1/256. The callback's nested AddMemory changes the recorder;
 * returning without replenishing storage is never treated as success. */
static _Thread_local unsigned t2p_in_cmdmem_callback; /* FRENTE-P diag: NVN calls made inside the guest callback */
static void nvn_command_request_memory(struct nvn_command_buffer *c,unsigned event,unsigned required,const struct nvn_frame *caller) {
    if(!event && c->command_callback_disabled)
        stop("NVN_COMMAND_MEMORY_EXHAUSTED_CALLBACK_DISABLED",nvn_caller,required,20);
    if(!c->callback)stop(event?"NVN_CONTROL_MEMORY_CALLBACK_REQUIRED":"NVN_COMMAND_MEMORY_CALLBACK_REQUIRED",nvn_caller,required,20);
    if(c->callback_active)stop("NVN_COMMAND_MEMORY_CALLBACK_RECURSION",nvn_caller,c->callback,24);
    nvn_pointer(c->callback,4,PROT_EXEC);
    uint64_t generation=c->memory_generation;struct nvn_command_list *active=c->list,active_before={0};
    if(active)active_before=*active;
    struct nvn_frame call=*caller;memset(call.args,0,sizeof(call.args));
    call.args[0]=c->object;call.args[1]=event;call.args[2]=event?256:(required<512?512:required);call.args[3]=c->callback_data;
    c->callback_active=1;c->callback_event=event;
    printf("NVN_COMMAND_MEMORY_CALLBACK event=%u required=%"PRIu64" active-records=%"PRIu64" active-bytes=%"PRIu64" original=0x%"PRIx64"\n",
        event,call.args[2],active?active->count:0,active?active->recorded_bytes:0,c->callback);
    /* Terminator 2D: the guest callback runs without the recorder mutex (it re-enters through
     * AddCommandMemory and may take engine locks that another recording thread holds while it waits for
     * this mutex). Only this thread touches `c` meanwhile: the buffer is the callback's own. */
    unsigned held=nvn_command_recorder_held;nvn_command_recorder_held=0;pthread_mutex_unlock(&nvn_command_recorder_mutex);
    struct timespec t2p_ts;clock_gettime(CLOCK_MONOTONIC,&t2p_ts);uint64_t t2p_cb0=(uint64_t)t2p_ts.tv_sec*1000000000ULL+t2p_ts.tv_nsec; /* FRENTE-P: callback wall time (diagnostic line only) */
    t2p_in_cmdmem_callback=1;cotm_nvn_call_original(c->callback,&call);t2p_in_cmdmem_callback=0;
    clock_gettime(CLOCK_MONOTONIC,&t2p_ts);uint64_t t2p_cb1=(uint64_t)t2p_ts.tv_sec*1000000000ULL+t2p_ts.tv_nsec;
    pthread_mutex_lock(&nvn_command_recorder_mutex);nvn_command_recorder_held=held;
    {static unsigned t2p_vt_logged;if(!event&&!t2p_vt_logged++&&c->callback_data){const uint64_t *o=nvn_pointer(c->callback_data,8,PROT_READ);if(o[0]){const uint64_t *vt=nvn_pointer(o[0],48,PROT_READ);
        printf("T2P_CMDMEM_CALLBACK_TARGET data=0x%"PRIx64" vtable=0x%"PRIx64" slot4=0x%"PRIx64"\n",c->callback_data,o[0],vt[4]);}}}
    printf("T2P_CMDMEM_CALLBACK_TIME event=%u guest-ms=%.3f relock-ms=%.3f thread=%u\n",event,(t2p_cb1-t2p_cb0)*1e-6,(clock_gettime(CLOCK_MONOTONIC,&t2p_ts),(double)((uint64_t)t2p_ts.tv_sec*1000000000ULL+t2p_ts.tv_nsec-t2p_cb1))*1e-6,guest_thread_index);
    c->callback_active=0;
    if(!c->live || !c->recording || c->list!=active || (active&&memcmp(active,&active_before,sizeof(active_before))))
        stop("NVN_COMMAND_MEMORY_CALLBACK_RECORDING_CHANGED",nvn_caller,c->object,24);
    if(c->memory_generation==generation)
        stop(event?"NVN_CONTROL_MEMORY_CALLBACK_NO_REFILL":"NVN_COMMAND_MEMORY_CALLBACK_NO_REFILL",nvn_caller,c->callback,20);
}
