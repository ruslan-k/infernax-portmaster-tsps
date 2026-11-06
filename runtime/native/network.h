/* Explicitly disconnected virtual Switch network. Host RomFS transport is a
 * separate laboratory channel, never advertised as Nintendo connectivity.
 * Protocol: switchbrew/libnx services/nifm.{c,h}; disconnected status 0xd46ed.
 */
static struct {unsigned open,submitted;uint32_t events[2],options[24];} nifm_requests[8];
static void nifm_destroy(unsigned state) {
    if(state && state<=8 && nifm_requests[state-1].open) {
        for(unsigned i=0;i<2;i++)session_close(nifm_requests[state-1].events[i],0);
        memset(&nifm_requests[state-1],0,sizeof(nifm_requests[0]));
    }
}
static int network_dispatch(uint8_t *buffer,struct session *s,struct ipc_request *req,enum endpoint endpoint,unsigned *state,uint64_t pc) {
    if(endpoint==EP_NIFM && req->command==5) {
        if(!req->send_pid || req->data_size<8)stop("INVALID_NIFM_CREATE",pc,req->data_size,24);
        ipc_child(buffer,s,req,EP_NIFM_GENERAL,pc);return 1;
    }
    if(endpoint==EP_NIFM_REQUEST) {
        if(!*state || *state>8 || !nifm_requests[*state-1].open)stop("INVALID_NIFM_REQUEST",pc,*state,24);
        unsigned at=*state-1;
        if(req->command==0) {uint32_t status=1;ipc_reply(buffer,req->token,0,&status,4,0,req->domain);return 1;}
        if(req->command==1) {ipc_reply(buffer,req->token,0xd46ed,NULL,0,0,req->domain);return 1;}
        if(req->command==2) {ipc_copy_events(buffer,req,nifm_requests[at].events,2,pc);return 1;}
        if(req->command==3 || req->command==4) {
            nifm_requests[at].submitted=req->command==4;
            session_get(nifm_requests[at].events[0],pc)->initialized=1;
            pthread_cond_broadcast(&kernel_changed);
            ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
        }
        if(req->command==10 || req->command==11 || req->command==12 || req->command==13 || req->command==14 || req->command==23) {
            if(req->data_size<1)stop("INVALID_NIFM_OPTION",pc,req->command,24);
            nifm_requests[at].options[req->command]=req->data[0];
            ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
        }
        return 0;
    }
    if(endpoint!=EP_NIFM_GENERAL)return 0;
    if(req->command==4) {
        if(req->data_size<4 || read32(req->data)!=2)stop("UNSUPPORTED_NIFM_REQUEST_TYPE",pc,req->data_size,24);
        unsigned at=0;while(at<8 && nifm_requests[at].open)at++;
        if(at==8)stop("NIFM_REQUESTS_FULL",pc,at,21);
        nifm_requests[at].open=1;
        for(unsigned i=0;i<2;i++)nifm_requests[at].events[i]=session_new(EP_EVENT,pc);
        *ipc_child(buffer,s,req,EP_NIFM_REQUEST,pc)=at+1;return 1;
    }
    if(req->command==18) {
        ipc_reply(buffer,req->token,0xd46ed,NULL,0,0,req->domain);return 1;
    }
    if(req->command==17 || req->command==20 || req->command==21 || req->command==22) {
        uint8_t available=0;ipc_reply(buffer,req->token,0,&available,1,0,req->domain);return 1;
    }
    if(req->command==1) {
        if(req->num_outputs!=1 || req->outputs[0].size<4)stop("INVALID_NIFM_CLIENT_BUFFER",pc,req->num_outputs,24);
        uint32_t id=1;memcpy((void *)(uintptr_t)req->outputs[0].address,&id,4);
        ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
    }
    return 0;
}
