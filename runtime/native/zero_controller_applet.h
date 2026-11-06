/* The native ControllerSupport applet protocol for an already connected
 * full-key SDL pad. Common/private/public input storages are consumed in order;
 * completion and output are published only after a real compatible pad exists.
 * No controller input, scene transition or Nintendo dialog is fabricated.
 * ABI source: switchbrew/libnx dbcc1beafc6b47b5ffbeb8ba82463a7d45da40bb,
 * applets/hid_la.{h,c}, services/applet.c and sf/cmif.h. */
struct zero_app_storage {unsigned references,size;uint8_t data[4096];};
static struct zero_app_storage zero_app_storages[32];
struct zero_controller {
    unsigned live,started,complete,consumed,count,size[3],selected;
    uint32_t event;uint8_t input[3][4096],output[12];
};
static struct zero_controller zero_controllers[4];
static struct zero_app_storage *zero_app_storage_get(unsigned state,uint64_t pc) {
    if(!state||state>32||!zero_app_storages[state-1].references)stop("ZERO_APP_STORAGE_IDENTITY",pc,state,24);
    return &zero_app_storages[state-1];
}
static unsigned zero_app_storage_create(unsigned size,uint64_t pc) {
    if(!size||size>4096)stop("ZERO_APP_STORAGE_SIZE",pc,size,24);
    unsigned i=0;while(i<32&&zero_app_storages[i].references)i++;
    if(i==32)stop("ZERO_APP_STORAGE_CAPACITY",pc,i,21);
    zero_app_storages[i]=(struct zero_app_storage){.references=1,.size=size};return i+1;
}
static void zero_applet_destroy(enum endpoint endpoint,unsigned state) {
    if(endpoint==EP_ZERO_APP_STORAGE||endpoint==EP_ZERO_APP_ACCESSOR) {
        struct zero_app_storage *s=zero_app_storage_get(state,0);
        if(!--s->references)memset(s,0,sizeof(*s));
    } else {
        if(!state||state>4||!zero_controllers[state-1].live)stop("ZERO_CONTROLLER_IDENTITY",0,state,24);
        struct zero_controller *c=&zero_controllers[state-1];
        if(c->event)session_close(c->event,0);
        memset(c,0,sizeof(*c));
    }
}
static void zero_controller_poll(void) {
    for(unsigned i=0;i<4;i++) {
        struct zero_controller *c=&zero_controllers[i];
        if(!c->live||!c->started||c->complete||!hid_host.connected)continue;
        /* A full-key pad is provided at the first supported player slot. */
        unsigned selected=8;
        for(unsigned n=0;n<hid_state.num_ids;n++)if(hid_state.ids[n]<8){selected=hid_state.ids[n];break;}
        if(selected==8 || !(read32(c->input[1]+12)&1))continue;
        c->selected=selected;c->output[0]=1;put32(c->output+4,selected);put32(c->output+8,0);c->complete=1;
        if(c->event)session_get(c->event,0)->initialized=1;
        pthread_cond_broadcast(&kernel_changed);
        printf("ZERO_CONTROLLER_SUPPORT_COMPLETED player=%u count=1 provider=%s connected=1 native-storage-result=12 no-synthetic-buttons=1\n",selected,hid_input_provider());
    }
}
static void zero_controller_start(struct zero_controller *c,uint64_t pc) {
    if(c->started||c->count!=3)stop("ZERO_CONTROLLER_INPUT_SEQUENCE",pc,c->count,24);
    const uint8_t *common=c->input[0],*private=c->input[1],*arg=c->input[2];
    printf("ZERO_CONTROLLER_SUPPORT_ARGS sizes=%u/%u/%u common=",c->size[0],c->size[1],c->size[2]);
    for(unsigned i=0;i<c->size[0]&&i<32;i++)printf("%02x",common[i]);
    printf(" private=");for(unsigned i=0;i<c->size[1]&&i<20;i++)printf("%02x",private[i]);
    printf(" header=");for(unsigned i=0;i<c->size[2]&&i<8;i++)printf("%02x",arg[i]);putchar('\n');
    if(c->size[0]!=32||read32(common)!=1||read32(common+4)!=32 ||
       (read32(common+8)!=3&&read32(common+8)!=4&&read32(common+8)!=5&&read32(common+8)!=7) ||
       c->size[1]!=20||read32(private)!=20||read32(private+4)!=c->size[2]||
       (c->size[2]!=0x21c&&c->size[2]!=0x430)||private[8]||private[9]||private[10]||private[11]||
       !(read32(private+12)&1)||read32(private+16)>1)
        stop("ZERO_CONTROLLER_SUPPORT_CONTRACT_REQUIRED",pc,read32(common+8),20);
    for(unsigned i=2;i<7;i++)if(arg[i]>1)stop("ZERO_CONTROLLER_SUPPORT_BOOLEAN",pc,i,24);
    if((!arg[5]&&(arg[0]>1||arg[1]<1||arg[1]>8)) || !arg[2])
        stop("ZERO_CONTROLLER_RECONFIGURATION_UI_REQUIRED",pc,read32(arg),20);
    c->started=1;zero_controller_poll();
    if(!c->complete)printf("ZERO_CONTROLLER_SUPPORT_WAITING compatible-connected-pad-required=1\n");
}
static int zero_applet_dispatch(uint8_t *buffer,struct session *session,struct ipc_request *r,enum endpoint endpoint,unsigned *state,uint64_t pc) {
    if(endpoint==EP_APP_LIBRARY && r->command==0) {
        if(r->data_size!=8||read32(r->data)!=0xc||read32(r->data+4)!=0)return 0;
        unsigned i=0;while(i<4&&zero_controllers[i].live)i++;
        if(i==4)stop("ZERO_CONTROLLER_CAPACITY",pc,i,21);
        zero_controllers[i]=(struct zero_controller){.live=1};
        *ipc_child(buffer,session,r,EP_ZERO_CONTROLLER,pc)=i+1;return 1;
    }
    if(endpoint==EP_APP_LIBRARY && r->command==10) {
        if(r->data_size!=8)stop("ZERO_APP_STORAGE_CREATE_REQUEST",pc,r->data_size,24);
        uint64_t size;memcpy(&size,r->data,8);if(size>4096)stop("ZERO_APP_STORAGE_SIZE",pc,size,24);
        unsigned index=zero_app_storage_create(size,pc);*ipc_child(buffer,session,r,EP_ZERO_APP_STORAGE,pc)=index;return 1;
    }
    if(endpoint==EP_ZERO_APP_STORAGE && r->command==0) {
        unsigned index=*state;struct zero_app_storage *s=zero_app_storage_get(index,pc);s->references++;
        *ipc_child(buffer,session,r,EP_ZERO_APP_ACCESSOR,pc)=index;return 1;
    }
    if(endpoint==EP_ZERO_APP_ACCESSOR) {
        struct zero_app_storage *s=zero_app_storage_get(*state,pc);
        if(r->command==0){uint64_t size=s->size;ipc_reply(buffer,r->token,0,&size,8,0,r->domain);return 1;}
        if(r->command==10||r->command==11) {
            unsigned write=r->command==10;
            if(r->data_size<8 || (write?(r->num_inputs!=1||r->num_outputs):(r->num_outputs!=1||r->num_inputs)))
                stop("ZERO_APP_STORAGE_IO_REQUEST",pc,r->command,24);
            uint64_t offset;memcpy(&offset,r->data,8);struct ipc_span span=write?r->inputs[0]:r->outputs[0];
            if(offset>s->size||span.size>s->size-offset)stop("ZERO_APP_STORAGE_IO_RANGE",pc,offset,24);
            if(write)memcpy(s->data+offset,(const void *)(uintptr_t)span.address,span.size);
            else memcpy((void *)(uintptr_t)span.address,s->data+offset,span.size);
            ipc_reply(buffer,r->token,0,NULL,0,0,r->domain);return 1;
        }
    }
    if(endpoint!=EP_ZERO_CONTROLLER)return 0;
    if(!*state||*state>4||!zero_controllers[*state-1].live)stop("ZERO_CONTROLLER_IDENTITY",pc,*state,24);
    struct zero_controller *c=&zero_controllers[*state-1];zero_controller_poll();
    if(r->command==0) {
        if(!c->event){c->event=session_new(EP_EVENT,pc);session_get(c->event,pc)->initialized=c->complete;}
        ipc_copy_events(buffer,r,&c->event,1,pc);return 1;
    }
    if(r->command==100) {
        unsigned storage=0;
        if(r->domain && r->input_objects==1&&!r->copies) {
            unsigned id=r->object_ids[0];
            if(!id||id>session->nobjects||session->objects[id-1].endpoint!=EP_ZERO_APP_STORAGE)stop("ZERO_CONTROLLER_INPUT_OBJECT",pc,id,24);
            storage=session->objects[id-1].initialized;
        } else if(!r->domain&&!r->input_objects&&r->copies==1) {
            struct session *s=session_get(r->copy_handles[0],pc);
            if(s->endpoint!=EP_ZERO_APP_STORAGE)stop("ZERO_CONTROLLER_INPUT_OBJECT",pc,r->copy_handles[0],24);
            storage=s->initialized;
        } else stop("ZERO_CONTROLLER_INPUT_DESCRIPTOR",pc,r->input_objects,24);
        if(c->started||c->count>=3)stop("ZERO_CONTROLLER_INPUT_SEQUENCE",pc,c->count,24);
        struct zero_app_storage *s=zero_app_storage_get(storage,pc);
        c->size[c->count]=s->size;memcpy(c->input[c->count++],s->data,s->size);
        ipc_reply(buffer,r->token,0,NULL,0,0,r->domain);return 1;
    }
    if(r->command==10){zero_controller_start(c,pc);ipc_reply(buffer,r->token,0,NULL,0,0,r->domain);return 1;}
    if(r->command==30) {
        if(!c->complete)stop("ZERO_CONTROLLER_RESULT_BEFORE_COMPLETION",pc,*state,24);
        ipc_reply(buffer,r->token,0,NULL,0,0,r->domain);return 1;
    }
    if(r->command==101) {
        if(!c->complete||c->consumed){ipc_reply(buffer,r->token,0x680,NULL,0,0,r->domain);return 1;}
        unsigned index=zero_app_storage_create(12,pc);memcpy(zero_app_storage_get(index,pc)->data,c->output,12);c->consumed=1;
        *ipc_child(buffer,session,r,EP_ZERO_APP_STORAGE,pc)=index;return 1;
    }
    return 0;
}
