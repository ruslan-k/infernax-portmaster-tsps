/* BSD socket service registration/lifetime. Calls beyond the implemented
 * interface stop, and no external network operation is synthesized.
 * ABI: switchbrew/libnx nx/source/services/bsd.c.
 */
static struct {uint32_t transfer;unsigned active,monitor_count;uint8_t config[32];} bsd_client;
static void bsd_destroy(unsigned state) {
    if(state==1 && bsd_client.active) {
        session_close(bsd_client.transfer,0);bsd_client.active=0;
    } else if(state==2 && bsd_client.monitor_count)bsd_client.monitor_count--;
}
static int bsd_dispatch(uint8_t *buffer,struct ipc_request *req,enum endpoint endpoint,unsigned *state,uint64_t pc) {
    if(endpoint!=EP_BSD)return 0;
    if(req->command==0) {
        uint64_t size=0;if(req->data_size>=48)memcpy(&size,req->data+40,8);
        if(*state || bsd_client.active || req->data_size<48 || !req->send_pid || req->copies!=1)
            stop("INVALID_BSD_REGISTRATION",pc,req->data_size,24);
        struct session *t=session_get(req->copy_handles[0],pc);
        if(t->endpoint!=EP_TRANSFER || t->initialized>=16 || !transfer_regions[t->initialized].open)
            stop("INVALID_BSD_TRANSFER_HANDLE",pc,req->copy_handles[0],24);
        struct line config_log={0};text(&config_log,"BSD_CONFIG");
        for(unsigned i=0;i<8;i++){text(&config_log," word=");hex(&config_log,read32(req->data+i*4));}
        text(&config_log," transfer_endpoint=");hex(&config_log,t->endpoint);text(&config_log," transfer_size=");hex(&config_log,transfer_regions[t->initialized].size);emit(&config_log);
        if(t->endpoint!=EP_TRANSFER || transfer_regions[t->initialized].size!=size || !read32(req->data) || read32(req->data)>10) /* SDK 19.3.9: version 0xa (MEDIDO pc-001) */
            stop("INVALID_BSD_TRANSFER_CONFIG",pc,size,24);
        uint64_t tx=read32(req->data+12),rx=read32(req->data+16),efficiency=read32(req->data+28);
        if(!tx)tx=read32(req->data+4);
        if(!rx)rx=read32(req->data+8);
        uint64_t minimum=((tx+rx+read32(req->data+20)+read32(req->data+24)+4095)&~4095ULL)*efficiency;
        if(!efficiency || efficiency>8 || minimum>size)stop("BSD_TRANSFER_POOL_TOO_SMALL",pc,minimum,24);
        memcpy(bsd_client.config,req->data,32);bsd_client.transfer=session_clone(req->copy_handles[0],pc);
        bsd_client.active=1;*state=1;
        /* SDK9 stores u32 at sp+0x3c and aborts unless it is zero (e86ac).
         * RegisterClient returns BSD status, not the host PID. */
        uint32_t status=0;ipc_reply(buffer,req->token,0,&status,4,0,req->domain);return 1;
    }
    if(req->command==1) {
        uint64_t pid=0;if(req->data_size>=8)memcpy(&pid,req->data,8);
        if(*state || !bsd_client.active || !req->send_pid || (pid && pid!=main_thread_id))
            stop("INVALID_BSD_MONITOR",pc,pid,24);
        *state=2;bsd_client.monitor_count++;
        ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
    }
    return 0;
}
