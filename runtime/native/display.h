/* Display service object model. A display handle is not a rendered frame.
 * Unsupported layers/binder transactions stop until a real provider exists.
 */
static unsigned display_references;
/* VI exposes a display cadence event independently of game submissions.
 * This port models the Switch application's 60 Hz display clock. It is a
 * scheduling event, never evidence that a physical frame was rendered.
 * CMIF ABI: libnx nx/source/services/vi.c, viGetDisplayVsyncEvent (5202). */
static uint32_t display_vsync_event;
static pthread_t display_vsync_thread;
static void *display_vsync_run(void *unused) {
    (void)unused;
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    uint64_t tick=((uint64_t)now.tv_sec*1000000000ULL+now.tv_nsec)*60/1000000000ULL;
    for(;;) {
        uint64_t ns=(++tick*1000000000ULL+59)/60;
        struct timespec at={(time_t)(ns/1000000000ULL),(long)(ns%1000000000ULL)};
        int rc;do {rc=clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&at,NULL);}while(rc==EINTR);
        if(rc)stop("DISPLAY_CADENCE_CLOCK_FAILED",0,rc,21);
        pthread_mutex_lock(&kernel_mutex);
        if(display_references) {
            session_get(display_vsync_event,0)->initialized=1;
            pthread_cond_broadcast(&kernel_changed);
        }
        pthread_mutex_unlock(&kernel_mutex);
        clock_gettime(CLOCK_MONOTONIC,&now);
        uint64_t current=((uint64_t)now.tv_sec*1000000000ULL+now.tv_nsec)*60/1000000000ULL;
        if(current>tick)tick=current; /* missed ticks coalesce into one event */
    }
    return NULL;
}
static struct {unsigned created,opened;int32_t weak_refs,strong_refs;uint32_t queue_event;uint64_t scaling;} layer;
static struct {uint32_t api,app_controlled;unsigned connections;} bq_state;
static void bq_transaction(uint8_t *buffer,struct ipc_request *req,uint64_t pc) {
    if(req->data_size<12 || read32(req->data)!=1 || read32(req->data+8) || !layer.opened ||
       req->num_inputs!=1 || req->num_outputs!=1 || req->inputs[0].size<16 || req->inputs[0].size>4096 || req->outputs[0].size<40)
        stop("BINDER_TRANSACTION_INVALID",pc,req->data_size,24);
    uint32_t code=read32(req->data+4);const uint8_t *parcel=(void *)(uintptr_t)req->inputs[0].address;
    uint32_t size=read32(parcel),offset=read32(parcel+4),objects=read32(parcel+8),object_offset=read32(parcel+12);
    if(offset<16 || offset>req->inputs[0].size || size>req->inputs[0].size-offset || size<8 ||
       objects || object_offset>req->inputs[0].size)stop("BINDER_PARCEL_LAYOUT",pc,size,24);
    const uint8_t *p=parcel+offset;static const char interface[]="android.gui.IGraphicBufferProducer";
    unsigned n=sizeof(interface)-1,token_size=8+((2*(n+1)+3)&~3U);
    if(size<token_size || read32(p+4)!=n)stop("BINDER_INTERFACE_LENGTH",pc,read32(p+4),24);
    for(unsigned i=0;i<=n;i++)if(p[8+i*2]!=(uint8_t)interface[i] || p[9+i*2])stop("BINDER_INTERFACE_NAME",pc,i,24);
    p+=token_size;size-=token_size;
    struct line log={0};text(&log,"BINDER_TRANSACTION code=");hex(&log,code);text(&log," bytes=");hex(&log,size);
    for(unsigned i=0;i+4<=size && i<48;i+=4){text(&log," arg=");hex(&log,read32(p+i));}emit(&log);
    uint32_t result[6]={0},result_size=4;
    if(code==10) {
        if(size<12 || read32(p) || bq_state.api || read32(p+4)<1 || read32(p+4)>3 || read32(p+8)>1)
            stop("BINDER_CONNECT_INVALID",pc,size,24);
        bq_state.api=read32(p+4);bq_state.app_controlled=read32(p+8);bq_state.connections++;
        result[0]=1280;result[1]=720;result[2]=0;result[3]=0;result_size=20;
    } else if(code==11) {
        if(size<4 || read32(p)!=bq_state.api || !bq_state.api)stop("BINDER_DISCONNECT_INVALID",pc,size,24);
        bq_state.api=0;
    } else if(code==9) {
        if(size<4)stop("BINDER_QUERY_INVALID",pc,size,24);
        uint32_t what=read32(p);result_size=8;
        if(what==0 || what==6)result[0]=1280;
        else if(what==1 || what==7)result[0]=720;
        else if(what==2)result[0]=1; /* RGBA8888 */
        else if(what==3)result[0]=1; /* one consumer-owned buffer */
        else if(what==4)result[0]=0; /* generic buffer queue */
        else if(what==5)result[0]=1; /* application display consumer */
        else if(what==8 || what==9 || what==10)result[0]=0; /* no transform/running-behind/extra usage */
        else stop("BINDER_QUERY_UNIMPLEMENTED",pc,what,20);
    } else stop("BINDER_TRANSACTION_UNIMPLEMENTED",pc,code,20);
    uint8_t *out=(void *)(uintptr_t)req->outputs[0].address;memset(out,0,16+result_size);
    put32(out,result_size);put32(out+4,16);put32(out+12,16+result_size);memcpy(out+16,result,result_size);
    ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);
}
static int display_dispatch(uint8_t *buffer,struct session *s,struct ipc_request *req,enum endpoint endpoint,uint64_t pc) {
    if(endpoint==EP_BINDER && (req->command==0 || req->command==3)){bq_transaction(buffer,req,pc);return 1;}
    if(endpoint==EP_SELF && (req->command==40 || req->command==44)) {
        /* 40 CreateManagedDisplayLayer; 44 CreateManagedDisplaySeparableLayer (SDK 10+): the second id is the recording layer. */
        if(layer.created)stop("SECOND_MANAGED_LAYER_UNSUPPORTED",pc,0,20);
        layer.created=1;uint64_t ids[2]={0x100,0x101};ipc_reply(buffer,req->token,0,ids,req->command==44?16:8,0,req->domain);return 1;
    }
    /* cmd 0 GetDisplayService; cmd 1 adds proxy-name exchange on newer SDKs. */
    if(endpoint==EP_VI_ROOT && (req->command==0 || req->command==1)) {
        if(req->data_size<4 || read32(req->data)!=0)stop("INVALID_APPLICATION_DISPLAY_OPEN",pc,0,24);
        ipc_child(buffer,s,req,EP_VI_DISPLAY,pc);return 1;
    }
    /* cmd 4 is the video-player variant of the same open (16-byte argument); it also
       hands back the display session, the policy word is not validated. */
    if(endpoint==EP_VI_ROOT && req->command==4) {
        ipc_child(buffer,s,req,EP_VI_DISPLAY,pc);return 1;
    }
    /* The video player also asks the display session itself for command 4; nothing in it
       changes state that the presentation path depends on, so acknowledge it. */
    if(endpoint==EP_VI_DISPLAY && req->command==4) {
        ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
    }
    if(endpoint==EP_VI_DISPLAY && req->command==100) {
        ipc_child(buffer,s,req,EP_BINDER,pc);return 1;
    }
    if(endpoint==EP_VI_DISPLAY && req->command==1000) {
        /* ListDisplays: one 0x60-byte nn::vi::DisplayInfo per output entry.
         * https://switchbrew.org/wiki/Display_services#DisplayInfo
         * Advertise the single display/layer supported by this adapter.
         * The maximum layer dimensions are independent of its current mode. */
        if(req->num_outputs!=1 || req->num_inputs || req->send_pid || req->copies ||
           req->outputs[0].size%0x60)
            stop("INVALID_DISPLAY_LIST_BUFFER",pc,req->num_outputs,24);
        uint64_t count=0;
        if(req->outputs[0].size>=0x60) {
            uint8_t info[0x60]={0};memcpy(info,"Default",8);info[0x40]=1;
            uint64_t limits[3]={1,1920,1080};memcpy(info+0x48,limits,sizeof(limits));
            memcpy((void *)(uintptr_t)req->outputs[0].address,info,sizeof(info));count=1;
        }
        ipc_reply(buffer,req->token,0,&count,8,0,req->domain);return 1;
    }
    if(endpoint==EP_VI_DISPLAY && req->command==1010) {
        if(req->data_size<64 || memcmp(req->data,"Default\0",8))stop("UNSUPPORTED_DISPLAY_NAME",pc,req->data_size,20);
        uint64_t id=1;display_references++;
        ipc_reply(buffer,req->token,0,&id,8,0,req->domain);return 1;
    }
    if(endpoint==EP_VI_DISPLAY && (req->command==1020 || req->command==1102)) {
        uint64_t id=0;if(req->data_size>=8)memcpy(&id,req->data,8);
        if(id!=1 || !display_references)stop("INVALID_DISPLAY_ID",pc,id,24);
        if(req->command==1020){display_references--;ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);}
        else {uint64_t resolution[2]={1280,720};ipc_reply(buffer,req->token,0,resolution,16,0,req->domain);}
        return 1;
    }
    if(endpoint==EP_VI_DISPLAY && req->command==2020) {
        uint64_t id=0,aruid=0;if(req->data_size>=80){memcpy(&id,req->data+64,8);memcpy(&aruid,req->data+72,8);}
        if(req->data_size<80 || !req->send_pid || memcmp(req->data,"Default\0",8) || id!=0x100 ||
           aruid!=main_thread_id || !layer.created || req->num_outputs!=1 || req->outputs[0].size<64)
            stop("INVALID_DISPLAY_LAYER_OPEN",pc,id,24);
        uint8_t parcel[64]={0};put32(parcel,40);put32(parcel+4,16);put32(parcel+8,4);put32(parcel+12,56);
        put32(parcel+16,2);put32(parcel+20,main_thread_id);put32(parcel+24,1);memcpy(parcel+40,"dispdrv",8);
        memcpy((void *)(uintptr_t)req->outputs[0].address,parcel,sizeof(parcel));layer.opened=1;
        uint64_t size=sizeof(parcel);ipc_reply(buffer,req->token,0,&size,8,0,req->domain);return 1;
    }
    if(endpoint==EP_VI_DISPLAY && req->command==5202) {
        uint64_t id=0;if(req->data_size>=8)memcpy(&id,req->data,8);
        if(id!=1 || !display_references)stop("INVALID_DISPLAY_VSYNC_ID",pc,id,24);
        if(!display_vsync_event) {
            display_vsync_event=session_new(EP_EVENT,pc);
            int rc=pthread_create(&display_vsync_thread,NULL,display_vsync_run,NULL);
            if(rc)stop("DISPLAY_CADENCE_THREAD_FAILED",pc,rc,21);
            pthread_detach(display_vsync_thread);
            printf("DISPLAY_CADENCE_EVENT rate=60Hz source=host-monotonic physical-video-proof=none\n");
        }
        ipc_copy_events(buffer,req,&display_vsync_event,1,pc);return 1;
    }
    if(endpoint==EP_VI_DISPLAY && req->command==2102) {
        /* SDK 6.5 passes its public u32 mode and expects the compositor's
         * u64 enum. ScaleToLayer is 2 -> 1; PreserveAspectRatio is 4 -> 4.
         * ABI corroborated by this SDK's response load at +0x398ff4 and
         * Strato's IApplicationDisplayService::ConvertScalingMode. */
        if(req->data_size<4 || req->num_inputs || req->num_outputs || req->send_pid || req->copies)
            stop("INVALID_SCALING_CONVERSION",pc,req->data_size,24);
        uint32_t input=read32(req->data);
        if(input!=2 && input!=4)stop("UNSUPPORTED_SCALING_CONVERSION",pc,input,20);
        uint64_t output=input==2?1:4;
        ipc_reply(buffer,req->token,0,&output,8,0,req->domain);return 1;
    }
    if(endpoint==EP_VI_DISPLAY && req->command==2101) {
        uint64_t mode=0,id=0;if(req->data_size>=16){memcpy(&mode,req->data,8);memcpy(&id,req->data+8,8);}
        if(req->data_size<16 || id!=0x100 || !layer.opened || (mode!=0&&mode!=1&&mode!=2&&mode!=4))stop("INVALID_LAYER_SCALING",pc,mode,24);
        layer.scaling=mode;ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
    }
    if(endpoint==EP_BINDER && req->command==1) {
        if(req->data_size<12 || read32(req->data)!=1 || read32(req->data+8)>1 || !layer.opened)
            stop("INVALID_BINDER_REFCOUNT",pc,req->data_size,24);
        int32_t delta=(int32_t)read32(req->data+4),*count=read32(req->data+8)?&layer.strong_refs:&layer.weak_refs;
        if((delta!=1&&delta!=-1) || (delta<0&&!*count))stop("BINDER_REFCOUNT_RANGE",pc,(uint32_t)delta,24);
        *count+=delta;ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
    }
    if(endpoint==EP_BINDER && req->command==2) {
        if(req->data_size<8 || read32(req->data)!=1 || read32(req->data+4)!=0xf || !layer.opened)
            stop("INVALID_BINDER_NATIVE_HANDLE",pc,req->data_size,24);
        ipc_event(buffer,req,&layer.queue_event,1,pc);return 1;
    }
    return 0;
}
