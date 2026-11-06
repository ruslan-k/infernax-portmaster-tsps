/* HID service with a guest-read-only 0x40000 shared memory object.
 * Controllers are connected only while the physical SDL provider has a pad.
 * ABI: SDK4.5/SDK9 and switchbrew/libnx services/hid.{c,h}.
 */
#include "frame_logs.h"
static struct {unsigned shared_plus_one,active,revision,gesture_active,gesture_revision,touch_active,keyboard_active,mouse_active;uint32_t styles,ids[10],num_ids,events[10],assignment[10];uint64_t hold;} hid_state;
static uint8_t zero_motion_active[10][5][3];
static struct {unsigned open,count;uint32_t handles[128];} vibration_lists[8];
static void vibration_destroy(unsigned state) {
    if(state&&state<=8)memset(&vibration_lists[state-1],0,sizeof(vibration_lists[0]));
}
#include "input_host.h"
static unsigned hid_id(uint32_t id,uint64_t pc) {
    if(id<8)return id;
    if(id==0x20)return 8;
    if(id==0x10)return 9;
    stop("INVALID_NPAD_ID",pc,id,24);return 0;
}
static void hid_aruid(struct ipc_request *req,unsigned offset,uint64_t pc) {
    uint64_t id=0;if(req->data_size>=offset+8)memcpy(&id,req->data+offset,8);
    if(!req->send_pid || id!=main_thread_id)stop("INVALID_HID_ARUID",pc,id,24);
}
static int hid_gesture_dispatch(uint8_t *buffer,struct ipc_request *req,uint64_t pc) {
    if(req->command!=91)return 0;
    /* SDK 232600 ActivateGesture: revision u32@0, ARUID u64@8, PID;
     * main's InitializeGesture path requests revision 1 at SDK2171dc. */
    if(req->data_size<16 || req->data_size>24 || req->copies || req->num_inputs || req->num_outputs)
        stop("INVALID_HID_GESTURE_REQUEST",pc,req->data_size,24);
    hid_aruid(req,8,pc);uint32_t revision=read32(req->data);
    if(revision!=1)stop("HID_GESTURE_REVISION_REQUIRED",pc,revision,20);
    if(!hid_state.shared_plus_one || hid_state.shared_plus_one>nshared)
        stop("HID_GESTURE_SHARED_REQUIRED",pc,hid_state.shared_plus_one,24);
    struct shared_region *shared=&shared_regions[hid_state.shared_plus_one-1];
    if(shared->size!=0x40000 || shared->permission!=PROT_READ || !shared->writer)
        stop("HID_GESTURE_SHARED_INVALID",pc,shared->size,24);
    if(!hid_state.gesture_active) {
        /* SDK217200 uses shared+3ba00. Header32 + 17*(sequence8+state96)
         * is 0x708 bytes. No touch/gesture producer exists: count stays zero,
         * so SDK21704c..217140 returns zero states without synthetic input. */
        uint8_t *ring=(uint8_t *)shared->writer+0x3ba00;
        memset(ring,0,0x708);uint64_t capacity=17;memcpy(ring+8,&capacity,8);
        __atomic_thread_fence(__ATOMIC_RELEASE);
        hid_state.gesture_revision=revision;hid_state.gesture_active=1;
    }
    ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
}
static unsigned hid_vibration_side(uint32_t handle,uint64_t pc) {
    unsigned registered=0;
    for(unsigned i=0;i<8;i++)if(vibration_lists[i].open)
        for(unsigned j=0;j<vibration_lists[i].count;j++)if(vibration_lists[i].handles[j]==handle)registered=1;
    if(!registered||(handle>>16)>1)stop("HID_VIBRATION_ROUTE_REQUIRED",pc,handle,20);
    return handle>>16;
}
static unsigned zero_vibration_mounted(uint32_t handle) {
    if((handle&255)!=3 || !hid_state.active || !(hid_state.styles&1) || !hid_vibration_mounted())return 0;
    for(unsigned i=0;i<hid_state.num_ids;i++)if(hid_state.ids[i]<8)
        return ((handle>>8)&255)==hid_state.ids[i];
    return 0;
}
static void hid_vibration_value(uint32_t handle,const uint8_t *data,uint64_t pc) {
    unsigned side=hid_vibration_side(handle,pc);float values[4];memcpy(values,data,16);
    for(unsigned i=0;i<4;i++)if(!isfinite(values[i])||values[i]<0||(i%2==0&&values[i]>1))stop("HID_VIBRATION_RANGE",pc,i,24);
    if(zero_vibration_mounted(handle))memcpy(hid_host.vibration[side],values,16);
}
static int input_dispatch(uint8_t *buffer,struct session *s,struct ipc_request *req,enum endpoint endpoint,uint64_t pc) {
    if(endpoint==EP_VIBRATION_LIST && req->command==0) {
        if(req->data_size<4 || req->data_size>12 || req->send_pid || req->copies || req->num_inputs || req->num_outputs)
            stop("INVALID_VIBRATION_REGISTRATION",pc,req->data_size,24);
        uint32_t handle=read32(req->data),style=handle&255,player=(handle>>8)&255,device=(handle>>16)&255;
        if((handle>>24) || style<3 || style>7 || (player>7&&player!=0x10&&player!=0x20) || device>1 || (style==6&&device) || (style==7&&device!=1))
            stop("UNSUPPORTED_VIBRATION_HANDLE",pc,handle,20);
        unsigned *state=req->domain?&s->objects[req->object_id-1].initialized:&s->initialized;
        if(!*state) {
            unsigned i=0;while(i<8&&vibration_lists[i].open)i++;
            if(i==8)stop("VIBRATION_LIST_TABLE_FULL",pc,i,21);
            vibration_lists[i].open=1;*state=i+1;
        }
        unsigned index=*state-1;
        if(index>=8 || !vibration_lists[index].open)stop("INVALID_VIBRATION_LIST_STATE",pc,*state,24);
        unsigned i=0;while(i<vibration_lists[index].count&&vibration_lists[index].handles[i]!=handle)i++;
        if(i==vibration_lists[index].count) {
            if(i==128)stop("VIBRATION_REGISTRATION_TABLE_FULL",pc,i,21);
            vibration_lists[index].handles[vibration_lists[index].count++]=handle;
        }
        struct line log={0};text(&log,"VIBRATION_REGISTER logical-handle=");hex(&log,handle);text(&log," physical-output=system-SDL-for-connected-full-key-only");emit(&log);
        ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
    }
    if(endpoint==EP_HID && req->command==0) {
        hid_aruid(req,0,pc);
        if(!hid_state.shared_plus_one)hid_state.shared_plus_one=shared_create(0x40000,1,pc)+1;
        ipc_child(buffer,s,req,EP_HID_RESOURCE,pc);return 1;
    }
    if(endpoint==EP_HID_RESOURCE && req->command==0) {
        if(!hid_state.shared_plus_one)stop("HID_SHARED_NOT_INITIALIZED",pc,0,24);
        uint32_t handle=session_new(EP_SHARED,pc);session_get(handle,pc)->initialized=hid_state.shared_plus_one-1;
        ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);
        put32(buffer+4,read32(buffer+4)|0x80000000U);put32(buffer+8,2);put32(buffer+12,handle);return 1;
    }
    if(endpoint!=EP_HID)return 0;
    if(hid_gesture_dispatch(buffer,req,pc))return 1;
    if(req->command==11) {
        /* SDK 6.5 ActivateTouchScreen: ARUID and PID, no other payload.
         * The current controller provider has no active screen contacts. */
        hid_aruid(req,0,pc);
        if(req->data_size>12 || req->copies || req->num_inputs || req->num_outputs ||
           !hid_state.shared_plus_one || hid_state.shared_plus_one>nshared)
            stop("ZERO_TOUCH_ACTIVATION_CONTRACT",pc,req->data_size,24);
        struct shared_region *shared=&shared_regions[hid_state.shared_plus_one-1];
        if(shared->size!=0x40000 || shared->permission!=PROT_READ || !shared->writer)
            stop("ZERO_TOUCH_SHARED_CONTRACT",pc,shared->size,24);
        if(!hid_state.touch_active) {
            memset((uint8_t *)shared->writer+0x400,0,0x2c38);
            hid_state.touch_active=1;
        }
        ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
    }
    if(req->command==21 || req->command==31) {
        hid_aruid(req,0,pc);
        if(req->data_size>12 || req->copies || req->num_inputs || req->num_outputs ||
           !hid_state.shared_plus_one || hid_state.shared_plus_one>nshared)
            stop("ZERO_AUX_INPUT_ACTIVATION_CONTRACT",pc,req->command,24);
        unsigned *active=req->command==21?&hid_state.mouse_active:&hid_state.keyboard_active;
        /* Neither auxiliary device is provided by this controller adapter.
         * Activate the empty shared LIFO without manufacturing input samples. */
        struct shared_region *shared=&shared_regions[hid_state.shared_plus_one-1];
        if(shared->size!=0x40000 || shared->permission!=PROT_READ || !shared->writer)
            stop("ZERO_AUX_INPUT_SHARED_CONTRACT",pc,shared->size,24);
        if(!*active) {
            uint8_t *ring=(uint8_t *)shared->writer+(req->command==21?0x3400:0x3800);
            memset(ring,0,0x400);uint64_t capacity=17;memcpy(ring+8,&capacity,8);*active=1;
        }
        ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
    }
    if(req->command==66 || req->command==67) {
        hid_aruid(req,8,pc);
        if(req->data_size<16 || req->data_size>24 || req->copies || req->num_inputs || req->num_outputs)
            stop("ZERO_MOTION_ACTIVATION_CONTRACT",pc,req->data_size,24);
        uint32_t handle=read32(req->data),style=handle&255,player=(handle>>8)&255,device=(handle>>16)&255;
        if(handle>>24 || style<3 || style>7 || device>2 ||
           (style<=4 && device!=2) || (style==5 && device>1) || (style==6 && device!=0) || (style==7 && device!=1))
            stop("ZERO_MOTION_HANDLE_CONTRACT",pc,handle,24);
        unsigned index=hid_id(player,pc);
        zero_motion_active[index][style-3][device]=req->command==66;
        /* A controller without a motion provider has no sensor samples.
         * Its shared rings remain empty; do not fabricate orientation data. */
        ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
    }
    if(req->command==200) {
        if(req->data_size<4||req->data_size>12||req->send_pid||req->num_inputs||req->num_outputs)stop("HID_VIBRATION_INFO_INPUT",pc,req->data_size,24);
        uint32_t handle=read32(req->data),side=hid_vibration_side(handle,pc);
        uint32_t mounted=zero_vibration_mounted(handle),info[2]={mounted?1:0,mounted?side+1:0};
        static _Thread_local struct frame_log_counter info_logs[2];
        struct frame_log_event log=frame_log_sample(&info_logs[side],handle,info[0],info[1],0);
        if(log.emit)printf("HID_VIBRATION_INFO handle=0x%x type=%u position=%u transport=SDL-fixed-frequency occurrence=%"PRIu64" suppressed=%"PRIu64" log-thread=%u log-scope=thread-side changed=%u\n",
            handle,info[0],info[1],log.occurrence,log.suppressed,guest_thread_index,log.changed);
        ipc_reply(buffer,req->token,0,info,sizeof(info),0,req->domain);return 1;
    }
    if(req->command==201) {
        hid_aruid(req,24,pc);
        if(req->data_size<32||req->data_size>40||req->copies||req->num_inputs||req->num_outputs)stop("HID_VIBRATION_VALUE_INPUT",pc,req->data_size,24);
#ifdef ANIMAL_WELL_SHADERS
        {   /* AW_RUMBLE_OFF=1: accept the value without waiting for the host actuator (fps measurement; the audio-driven
             * rumble is ~42 requests per frame and each one waits for the previous SDL rumble transfer). */
            static int cfg;static unsigned off;
            if(!cfg){const char *o=getenv("AW_RUMBLE_OFF");off=o&&!strcmp(o,"1");cfg=1;if(off)printf("AW_RUMBLE_OFF enabled=1 vibration-values=accepted actuator=untouched\n");}
            if(off){ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;}
        }
#endif
        hid_rumble_wait();
        hid_vibration_value(read32(req->data),req->data+4,pc);hid_rumble_request(pc);
        ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
    }
    if(req->command==202||req->command==211) {
        if(req->data_size<16||req->data_size>24||req->copies||req->num_inputs||req->num_outputs)stop("HID_VIBRATION_QUERY_INPUT",pc,req->data_size,24);
        hid_aruid(req,8,pc);unsigned side=hid_vibration_side(read32(req->data),pc);
        if(req->command==211) {
            uint8_t mounted=zero_vibration_mounted(read32(req->data));ipc_reply(buffer,req->token,0,&mounted,1,0,req->domain);
        } else {
            float actual[4]={0};if(zero_vibration_mounted(read32(req->data)))hid_vibration_actual(side,actual);ipc_reply(buffer,req->token,0,actual,16,0,req->domain);
        }
        return 1;
    }
    if(req->command==204||req->command==205) {
        unsigned set=req->command==204;
        if(req->data_size<(set?1U:0U)||req->data_size>(set?12U:8U)||req->send_pid||req->copies||req->num_inputs||req->num_outputs||
                (set&&req->data[0]>1))stop("HID_VIBRATION_PERMISSION_INPUT",pc,req->data_size,24);
        if(set) {
            hid_rumble_wait();
            if(!req->data[0]&&hid_vibration_permitted()) {
                /* Stop the real motors before disabling an active output. */
                memset(hid_host.vibration,0,sizeof(hid_host.vibration));hid_rumble_request(pc);
            }
            hid_host.vibration_disabled=!req->data[0];
            printf("HID_VIBRATION_PERMISSION requested=%u effective=%u mounted=%u scope=port-system-setting\n",
                req->data[0],hid_vibration_permitted(),hid_vibration_mounted());
            ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);
        } else {
            uint8_t permitted=hid_vibration_permitted();ipc_reply(buffer,req->token,0,&permitted,1,0,req->domain);
        }
        return 1;
    }
    if(req->command==206) {
        uint64_t aruid=0;if(req->data_size>=8)memcpy(&aruid,req->data,8);
        if(req->data_size<8||req->data_size>16||aruid!=main_thread_id||req->send_pid||req->copies||req->num_inputs!=2||req->num_outputs||req->inputs[0].size>8||req->inputs[0].size%4||req->inputs[1].size!=req->inputs[0].size*4)
            stop("HID_VIBRATION_VALUES_INPUT",pc,req->num_inputs,24);
        hid_rumble_wait();
        for(unsigned i=0;i<req->inputs[0].size/4;i++)hid_vibration_value(read32((void *)(uintptr_t)(req->inputs[0].address+i*4)),(void *)(uintptr_t)(req->inputs[1].address+i*16),pc);
        hid_rumble_request(pc);ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
    }
    if(req->command==203) {
        if(req->send_pid || req->num_inputs || req->num_outputs || req->copies || req->data_size>8)
            stop("INVALID_VIBRATION_LIST_CREATE",pc,req->data_size,24);
        /* Logical registrations are stored per list and routed by the provider. */
        ipc_child(buffer,s,req,EP_VIBRATION_LIST,pc);return 1;
    }
    if(req->command==103 || req->command==104) {
        /* Pre-5.0 ActivateNpad has only ARUID, no revision word. */
        hid_aruid(req,0,pc);
        if(req->data_size>12)stop("INVALID_LEGACY_NPAD_ACTIVATION",pc,req->data_size,24);
        hid_state.active=req->command==103;hid_state.revision=0;
        ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
    }
    if(req->command==109 || req->command==100 || req->command==124 || req->command==122 || req->command==106) {
        hid_aruid(req,8,pc);uint32_t value=read32(req->data);
        if(req->command==109) {
            /* SDK 18.3.2 (Momodora, MEDIDO run momo-013) asks for NpadSystemRevision 5. Revisions
             * above 3 only add trigger/attribute fields the host already zeroes; the npad state
             * layout the game reads is unchanged, and the pad is proved physically before Done. */
            if(value>5)stop("UNSUPPORTED_HID_REVISION",pc,value,20);
            hid_state.active=1;hid_state.revision=value;
        } else if(req->command==100)hid_state.styles=value;
        else if(req->command==106) {
            /* Acquire publishes the current style, even if the provider was
             * connected before the client subscribed. Native main+1812d20
             * waits on this event before discovering its controller. See
             * Ryujinx1df6c07 IHidServer command106 and controller-native-02.
             * Each returned copy owns a handle, not the service's reference. */
            uint32_t *event=&hid_state.events[hid_id(value,pc)];
            if(!*event)*event=session_new(EP_EVENT,pc);
            session_get(*event,pc)->initialized=1;
            ipc_copy_events(buffer,req,event,1,pc);
            pthread_cond_broadcast(&kernel_changed);return 1;
        }
        else hid_state.assignment[hid_id(value,pc)]=req->command==122;
        ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
    }
    if(req->command==102) {
        hid_aruid(req,0,pc);
        if(req->num_inputs!=1 || req->inputs[0].size>40 || req->inputs[0].size%4)stop("INVALID_HID_ID_LIST",pc,req->num_inputs,24);
        hid_state.num_ids=req->inputs[0].size/4;
        memcpy(hid_state.ids,(void *)(uintptr_t)req->inputs[0].address,req->inputs[0].size);
        for(unsigned i=0;i<hid_state.num_ids;i++) {
            hid_id(hid_state.ids[i],pc);
            for(unsigned j=0;j<i;j++)if(hid_state.ids[i]==hid_state.ids[j])stop("DUPLICATE_HID_ID",pc,hid_state.ids[i],24);
        }
        ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
    }
    if(req->command==101) {
        hid_aruid(req,0,pc);ipc_reply(buffer,req->token,0,&hid_state.styles,4,0,req->domain);return 1;
    }
    if(req->command==120) {
        hid_aruid(req,0,pc);uint64_t value=0;if(req->data_size>=16)memcpy(&value,req->data+8,8);
        if(req->data_size<16 || value>1)stop("INVALID_HID_HOLD",pc,value,24);
        hid_state.hold=value;ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
    }
    if(req->command==121) {
        hid_aruid(req,0,pc);ipc_reply(buffer,req->token,0,&hid_state.hold,8,0,req->domain);return 1;
    }
    if(req->command==128) {
        /* SDK 9 SetNpadSystemExtStateEnabled: ARUID@0, enable u32@8, padded to 20 bytes.
         * The flag only selects which npad state layout the client reads afterwards;
         * the provider reports extended fields as zero, so record and acknowledge. */
        hid_aruid(req,0,pc);
        uint32_t enabled=req->data_size>=12?read32(req->data+8):0;
        printf("HID_SYSTEM_EXT_STATE size=%u enabled=%u\n",(unsigned)req->data_size,enabled);
        ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
    }
    return 0;
}
