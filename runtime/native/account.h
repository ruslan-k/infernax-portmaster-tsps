/* One explicit offline laboratory profile. No Nintendo account or credentials.
 * ABI: switchbrew/libnx services/acc.{c,h}; Account_services acc:u0.
 */
static unsigned launch_parameter_consumed;
static int account_dispatch(uint8_t *buffer,struct session *s,struct ipc_request *req,enum endpoint endpoint,unsigned *state,uint64_t pc) {
    if(endpoint==EP_APP_FUNCTIONS && req->command==20) {
        if(req->data_size<16 || memcmp(req->data,laboratory_uid,16))stop("INVALID_SAVE_USER",pc,0,24);
        save_open(pc);uint64_t required=0;ipc_reply(buffer,req->token,0,&required,8,0,req->domain);return 1;
    }
    if(endpoint==EP_APP_FUNCTIONS && req->command==1) {
        if(req->data_size<4 || read32(req->data)!=2)stop("UNSUPPORTED_LAUNCH_PARAMETER_KIND",pc,req->data_size<4?0:read32(req->data),20);
        if(launch_parameter_consumed){ipc_reply(buffer,req->token,0x680,NULL,0,0,req->domain);return 1;}
        launch_parameter_consumed=1;ipc_child(buffer,s,req,EP_LAUNCH_STORAGE,pc);return 1;
    }
    if(endpoint==EP_LAUNCH_STORAGE && req->command==0) {
        ipc_child(buffer,s,req,EP_LAUNCH_ACCESSOR,pc);return 1;
    }
    if(endpoint==EP_LAUNCH_ACCESSOR) {
        if(req->command==0) {uint64_t size=0x88;ipc_reply(buffer,req->token,0,&size,8,0,req->domain);return 1;}
        if(req->command==11) {
            if(req->data_size<8 || req->num_outputs!=1)stop("INVALID_LAUNCH_STORAGE_READ",pc,req->command,24);
            uint64_t offset;memcpy(&offset,req->data,8);uint64_t size=req->outputs[0].size;
            if(offset>0x88 || size>0x88-offset)stop("INVALID_LAUNCH_STORAGE_RANGE",pc,offset,24);
            uint8_t payload[0x88]={0};put32(payload,0xc79497ca);payload[4]=1;memcpy(payload+8,laboratory_uid,16);
            memcpy((void *)(uintptr_t)req->outputs[0].address,payload+offset,size);
            ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
        }
    }
    if(endpoint==EP_ACCOUNT) {
        if(req->command==100 || (req->command==140 || req->command==160)) {
            if(!req->send_pid || req->data_size<8)stop("INVALID_ACCOUNT_INITIALIZE",pc,req->command,24);
            *state=1;ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
        }
        if(req->command==0) {
            uint32_t count=1;ipc_reply(buffer,req->token,0,&count,4,0,req->domain);return 1;
        }
        if(req->command==1) {
            if(req->data_size<16)stop("INVALID_ACCOUNT_UID",pc,req->command,24);
            uint8_t exists=!memcmp(req->data,laboratory_uid,16);
            ipc_reply(buffer,req->token,0,&exists,1,0,req->domain);return 1;
        }
        if(req->command==2 || req->command==3 || req->command==141) {
            if(req->num_outputs!=1 || req->outputs[0].size<16 || req->outputs[0].size>128)
                stop("INVALID_ACCOUNT_LIST_BUFFER",pc,req->num_outputs,24);
            void *out=(void *)(uintptr_t)req->outputs[0].address;
            memset(out,0,req->outputs[0].size);memcpy(out,laboratory_uid,16);
            ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
        }
        if(req->command==4 || req->command==51) {
            uint64_t selected[2]={0};
            if(req->command==51 && (req->data_size<1 || req->data[0]>1))stop("INVALID_ACCOUNT_SELECTION",pc,0,24);
            if(req->command==4 || !req->data[0])memcpy(selected,laboratory_uid,16);
            ipc_reply(buffer,req->token,0,selected,16,0,req->domain);return 1;
        }
        if(req->command==5 || req->command==101) {
            if(req->data_size<16 || memcmp(req->data,laboratory_uid,16))stop("UNKNOWN_ACCOUNT_PROFILE",pc,req->command,20);
            ipc_child(buffer,s,req,req->command==5?EP_PROFILE:EP_ACCOUNT_MANAGER,pc);return 1;
        }
        if(req->command==150) {
            uint8_t locked=0;ipc_reply(buffer,req->token,0,&locked,1,0,req->domain);return 1;
        }
    }
    if(endpoint==EP_ACCOUNT_MANAGER && req->command==0) {
        /* Generic account-unavailable category: SDK IsNetworkServiceAccountAvailable
         * sdk+0x1218e0 maps module 124 / descriptions 200..269 to false.
         * This explicit laboratory profile has no network service account. */
        ipc_reply(buffer,req->token,(200U<<9)|124U,NULL,0,0,req->domain);return 1;
    }
    if(endpoint==EP_PROFILE && (req->command==0 || req->command==1)) {
        struct {uint64_t uid[2],timestamp;char nickname[32];} profile={.timestamp=1789084800,.nickname="NextOS Test"};
        memcpy(profile.uid,laboratory_uid,16);
        if(req->command==0) {
            if(req->num_outputs!=1 || req->outputs[0].size!=128)stop("INVALID_PROFILE_DATA_BUFFER",pc,0,24);
            uint8_t *out=(uint8_t *)(uintptr_t)req->outputs[0].address;memset(out,0,128);put32(out+4,1);
        }
        ipc_reply(buffer,req->token,0,&profile,sizeof(profile),0,req->domain);return 1;
    }
    return 0;
}
