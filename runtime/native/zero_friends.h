/* Offline friend-service objects for the explicit local account.
 * Factory/event ABI: switchbrew Friend_services and libnx friends.c.
 * Events remain unsignalled while there are no completed requests/notifications.
 */
static void zero_friends_destroy(unsigned state) {
    if(state)session_close(state,0);
}
static unsigned zero_friends_dispatch(uint8_t *buffer,struct session *session,struct ipc_request *req,
                                     enum endpoint endpoint,unsigned *state,uint64_t pc) {
    if(endpoint==EP_FRIENDS_CREATOR && req->command<=2) {
        if(req->num_inputs || req->num_outputs || req->copies || req->send_pid ||
           (req->command==1 && (req->data_size<16 || memcmp(req->data,laboratory_uid,16))))
            stop("ZERO_FRIENDS_FACTORY_CONTRACT",pc,req->command,24);
        enum endpoint child=req->command==0?EP_FRIENDS_SERVICE:req->command==1?EP_FRIENDS_NOTIFICATIONS:EP_FRIENDS_DAEMON;
        ipc_child(buffer,session,req,child,pc);return 1;
    }
    if(endpoint!=EP_FRIENDS_SERVICE && endpoint!=EP_FRIENDS_NOTIFICATIONS)return 0;
    if(req->command==0) {
        if(req->num_inputs || req->num_outputs || req->copies || req->send_pid)
            stop("ZERO_FRIENDS_EVENT_CONTRACT",pc,req->command,24);
        if(!*state)*state=session_new(EP_EVENT,pc);
        ipc_copy_events(buffer,req,state,1,pc);return 1;
    }
    if(req->command==1) {
        /* Cancel with no outstanding request / Clear an empty notification
         * queue has no remote action and must not create a completion. */
        if(req->num_inputs || req->num_outputs || req->copies || req->send_pid)
            stop("ZERO_FRIENDS_CLEAR_CONTRACT",pc,req->command,24);
        if(*state)session_get(*state,pc)->initialized=0;
        ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
    }
    return 0;
}
