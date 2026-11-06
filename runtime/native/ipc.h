/* Bounded HIPC/CMIF subset for the observed LC2 SDK 4.4.1 startup path.
 * Protocol references: switchbrew/libnx sf/{hipc,cmif}.h and services/sm.c.
 * No Linux handle or syscall is exposed to the guest. Unknown requests stop.
 */
#define IPC_BYTES 256U
#define MAX_HANDLES 512U
static _Thread_local uint8_t ipc_pointer_scratch[4096];
#include "megaman_lc2_contract.h"
#if defined(T2_SWITCH_INPUT) && T2_SWITCH_INPUT
#include "aw_release.h"
#endif
enum endpoint { EP_NONE, EP_SM, EP_LM, EP_LOGGER, EP_APM, EP_APM_SESSION,
    EP_APPLET, EP_APP_PROXY, EP_COMMON, EP_SELF, EP_WINDOW, EP_APP_AUDIO,
    EP_APP_DISPLAY, EP_APP_LIBRARY, EP_APP_FUNCTIONS, EP_APP_DEBUG, EP_EVENT, EP_THREAD, EP_AOC,
    EP_PCTL, EP_PCTL_SESSION, EP_TIME, EP_CLOCK_USER, EP_CLOCK_NETWORK, EP_CLOCK_STEADY,
    EP_TIMEZONE, EP_CLOCK_LOCAL, EP_SHARED, EP_FSP, EP_STORAGE_ROMFS, EP_ACCOUNT, EP_PROFILE,
    EP_LAUNCH_STORAGE, EP_LAUNCH_ACCESSOR, EP_ACCOUNT_MANAGER, EP_SAVE_FS, EP_SAVE_FILE, EP_TRANSFER, EP_NVDRV,
    EP_VI_ROOT, EP_VI_DISPLAY, EP_BINDER, EP_NIFM, EP_NIFM_GENERAL, EP_BSD, EP_HID, EP_HID_RESOURCE, EP_NIFM_REQUEST,
    EP_VIBRATION_LIST, EP_SETTINGS, EP_AUDREN_MANAGER, EP_AUDREN, EP_AUDIO_DEVICE,
    EP_FRIENDS_CREATOR, EP_FRIENDS_SERVICE, EP_FRIENDS_NOTIFICATIONS, EP_FRIENDS_DAEMON,
    EP_ZERO_CONTROLLER, EP_ZERO_APP_STORAGE, EP_ZERO_APP_ACCESSOR, EP_AW_STUB, EP_AW_AUDOUT_LIST,
    EP_AUDOUT_MANAGER, EP_AUDOUT, EP_SAVE_DIR };
static void endpoint_destroy(enum endpoint endpoint,unsigned state);
static void nv_destroy(unsigned state);
static void bsd_destroy(unsigned state);
static void nifm_destroy(unsigned state);
static void vibration_destroy(unsigned state);
static void aud_destroy(unsigned state);
static void aout_destroy(unsigned state);
static void zero_friends_destroy(unsigned state);
static void zero_applet_destroy(enum endpoint endpoint,unsigned state);
static void zero_controller_poll(void);
static const uint64_t laboratory_uid[2]={0x434f544d53545544ULL,0x4e4558544f535445ULL};
struct object { enum endpoint endpoint; unsigned initialized; };
struct session {
    enum endpoint endpoint;unsigned initialized,domain,nobjects,capacity;
    unsigned open,references,alias_target,peer_closed;struct object *objects;
};
static struct session handles[MAX_HANDLES];
static unsigned next_handle;
static unsigned handle_generations[MAX_HANDLES];
static uint32_t guest_terminate_result;
/* Guest performance requests are stored as policy metadata. Tegra clock IDs
 * cannot be applied to Amlogic; CPU/GPU execution stays under the host governor. */
static uint32_t performance_configs[2];
static unsigned performance_configured[2];
/* One foreground application, no shell/library applet launched. */
static struct {
    unsigned foreground,notified,exit_lock,op_notify,perf_notify,suspend,cpu_boost,resume_notify;
    uint8_t focus_mode[3];uint32_t message_event,suspend_event,gpu_error_event,aoc_event,launchable_event;
    unsigned focus_message;uint32_t idle_extension;
} app_state={.foreground=1,.focus_message=1};
static uint32_t read32(const void *p) { uint32_t v;memcpy(&v,p,4);return v; }
static void put32(void *p,uint32_t v) { memcpy(p,&v,4); }
static struct session *session_slot(uint64_t handle,uint64_t pc) {
    uint64_t value=handle-0x400;unsigned index=value&0x7ff;
    if(handle<0x400 || handle>=0x40000000 || index>=next_handle ||
       value>>11!=handle_generations[index] || !handles[index].open)
        stop("INVALID_SESSION_HANDLE",pc,handle,24);
    return &handles[index];
}
static struct session *session_get(uint64_t handle,uint64_t pc) {
    struct session *s=session_slot(handle,pc);
    if(s->alias_target)s=&handles[s->alias_target-1];
    if(!s->endpoint || !s->references)stop("INVALID_SESSION_OBJECT",pc,handle,24);
    return s;
}
static uint32_t session_new(enum endpoint endpoint,uint64_t pc) {
    unsigned index=0;
    while(index<next_handle && (handles[index].endpoint || handles[index].open || handles[index].references || handles[index].alias_target))index++;
    if(index==MAX_HANDLES)stop("HANDLE_TABLE_FULL",pc,index,21);
    if(index==next_handle)next_handle++;
    else if(++handle_generations[index]>=0x80000)stop("HANDLE_GENERATION_EXHAUSTED",pc,index,21);
    handles[index]=(struct session){.endpoint=endpoint,.open=1,.references=1};
    return 0x400+index+(handle_generations[index]<<11);
}
static uint32_t session_clone(uint64_t handle,uint64_t pc) {
    struct session *s=session_get(handle,pc);
    uint32_t copy=session_new(s->endpoint,pc);struct session *slot=session_slot(copy,pc);
    slot->references=0;slot->alias_target=(unsigned)(s-handles)+1;s->references++;return copy;
}
static void session_close(uint64_t handle,uint64_t pc) {
    struct session *s=session_get(handle,pc),*slot=session_slot(handle,pc);
    s->references--;slot->open=0;
    if(slot!=s)memset(slot,0,sizeof(*slot));
    if(!s->references){
        if(s->domain)for(unsigned i=0;i<s->nobjects;i++)endpoint_destroy(s->objects[i].endpoint,s->objects[i].initialized);
        else endpoint_destroy(s->endpoint,s->initialized);
        free(s->objects);memset(s,0,sizeof(*s));
    }
}
static uint32_t connect_port(uint64_t address,uint64_t pc) {
    char name[13]={0};unsigned i;
    for(i=0;i<12;i++) {
        if(!accessible(address+i,1,PROT_READ))stop("INVALID_PORT_NAME",pc,address,24);
        name[i]=*(char *)(uintptr_t)(address+i);if(!name[i])break;
    }
    if(i==12)stop("PORT_NAME_TOO_LONG",pc,address,24);
    struct line b={0};text(&b,"CONNECT name=");text(&b,name);emit(&b);
    if(strcmp(name,"sm:"))stop("UNIMPLEMENTED_NAMED_PORT",pc,address,20);
    return session_new(EP_SM,pc);
}
struct ipc_span { uint64_t address,size; };
struct ipc_request {
    unsigned type,command,token,send_pid;size_t data_size;const uint8_t *data;
    unsigned num_inputs;struct ipc_span inputs[16];
    unsigned num_outputs;struct ipc_span outputs[16];
    unsigned domain,object_id,domain_close;
    unsigned copies;uint32_t copy_handles[15];
    unsigned input_objects;uint32_t object_ids[4];
};
static struct ipc_request ipc_parse(const uint8_t *buffer,uint64_t pc,unsigned domain) {
    uint32_t h0=read32(buffer),h1=read32(buffer+4),special=0;
    size_t cursor=8;
    unsigned x=(h0>>16)&15,a=(h0>>20)&15,b=(h0>>24)&15,w=(h0>>28)&15;
    if(h1>>31){special=read32(buffer+cursor);cursor+=4;if(special&1)cursor+=8;}
    unsigned copies=(special>>1)&15,moves=(special>>5)&15;
    cursor+=4*(copies+moves)+8*x+12*(a+b+w);
    size_t raw_size=(h1&1023)*4,aligned=(cursor+15)&~15U;
    if(cursor>IPC_BYTES || raw_size>IPC_BYTES-cursor || aligned+16>cursor+raw_size)
        stop("MALFORMED_IPC_LENGTH",pc,h1,24);
    unsigned receive_mode=(h1>>10)&15;
    size_t receive_count=receive_mode>2?receive_mode-2:receive_mode==2?1:0;
    if(receive_count*8>IPC_BYTES-cursor-raw_size)stop("MALFORMED_IPC_RECEIVE_LIST",pc,h1,24);
    if(w||moves||receive_mode==1||x+a>16||b+receive_count>16|| (special&~0x1ffU))
        stop("UNIMPLEMENTED_IPC_DESCRIPTORS",pc,h0,20);
    struct ipc_request req={.type=h0&65535,.send_pid=special&1};
    size_t payload_end=cursor+raw_size;
    if(domain && (req.type==4 || req.type==6)) {
        req.domain=1;req.object_id=read32(buffer+aligned+4);
        unsigned kind=buffer[aligned],objects=buffer[aligned+1],size=read32(buffer+aligned)>>16;
        if(kind==2){req.domain_close=1;return req;}
        if(kind!=1 || objects>4 || size<16 || size>payload_end-aligned-16 ||
           objects*4>payload_end-aligned-16-size)
            stop("INVALID_DOMAIN_HEADER",pc,read32(buffer+aligned),24);
        req.input_objects=objects;
        for(unsigned i=0;i<objects;i++)req.object_ids[i]=read32(buffer+aligned+16+size+i*4);
        aligned+=16;payload_end=aligned+size;
    }
    if(read32(buffer+aligned)!=0x49434653 || read32(buffer+aligned+4)>1)
        stop("INVALID_CMIF_HEADER",pc,read32(buffer+aligned),24);
    req.command=read32(buffer+aligned+8);req.token=read32(buffer+aligned+12);
    req.data_size=payload_end-aligned-16;req.data=buffer+aligned+16;
    size_t desc=8+((h1>>31)?4+((special&1)?8:0):0);
    req.copies=copies;
    for(unsigned i=0;i<copies;i++) {
        uint32_t handle=read32(buffer+desc);desc+=4;
        if(handle!=0xffff8001U && session_get(handle,pc)->endpoint!=EP_TRANSFER && session_get(handle,pc)->endpoint!=EP_ZERO_APP_STORAGE)
            stop("UNIMPLEMENTED_IPC_COPY_HANDLE",pc,handle,20);
        req.copy_handles[i]=handle;
    }
    size_t pointer_used=0;
    for(unsigned i=0;i<x+a;i++) {
        uint32_t d0=read32(buffer+desc),d1=read32(buffer+desc+4);
        uint64_t address,size;
        if(i<x) {address=d1|((uint64_t)((d0>>12)&15)<<32)|((uint64_t)((d0>>6)&63)<<36);size=d0>>16;desc+=8;}
        else {
            uint32_t d2=read32(buffer+desc+8);desc+=12;
            address=d1|((uint64_t)(d2>>28)<<32)|((uint64_t)((d2>>2)&0x3fffff)<<36);
            size=d0|((uint64_t)((d2>>24)&15)<<32);
        }
        if(size) {
            if(!accessible(address,size,PROT_READ))stop("INVALID_IPC_INPUT_BUFFER",pc,address,24);
            if(i<x) {
                if(size>sizeof(ipc_pointer_scratch)-pointer_used)stop("IPC_POINTER_BUFFER_TOO_SMALL",pc,size,24);
                memcpy(ipc_pointer_scratch+pointer_used,(void *)(uintptr_t)address,size);
                address=(uintptr_t)(ipc_pointer_scratch+pointer_used);pointer_used+=size;
            }
            req.inputs[req.num_inputs++]=(struct ipc_span){address,size};
        }
    }
    for(unsigned i=0;i<b;i++) {
        uint32_t d0=read32(buffer+desc),d1=read32(buffer+desc+4),d2=read32(buffer+desc+8);desc+=12;
        uint64_t address=d1|((uint64_t)(d2>>28)<<32)|((uint64_t)((d2>>2)&0x3fffff)<<36);
        uint64_t size=d0|((uint64_t)((d2>>24)&15)<<32);
        if(size) {
            if(!accessible(address,size,PROT_WRITE))stop("INVALID_IPC_OUTPUT_BUFFER",pc,address,24);
            req.outputs[req.num_outputs++]=(struct ipc_span){address,size};
        }
    }
    desc=cursor+raw_size;
    for(unsigned i=0;i<receive_count;i++,desc+=8) {
        uint32_t d0=read32(buffer+desc),d1=read32(buffer+desc+4);
        uint64_t address=d0|((uint64_t)(d1&65535)<<32),size=d1>>16;
        if(size) {
            if(!accessible(address,size,PROT_WRITE))stop("INVALID_IPC_RECEIVE_BUFFER",pc,address,24);
            req.outputs[req.num_outputs++]=(struct ipc_span){address,size};
        }
    }
    return req;
}
static void ipc_reply(uint8_t *buffer,unsigned token,uint32_t result,const void *data,size_t size,uint32_t move,unsigned domain) {
    /* CMIF reserves 16 bytes of alignment slack inside raw-data size. */
    if(size>128)stop("IPC_REPLY_TOO_LARGE",0,size,24);
    memset(buffer,0,IPC_BYTES);put32(buffer+4,(32+size+(domain?16+(move?4:0):0)+3)/4 | (move&&!domain?0x80000000U:0));
    size_t cmif=16;
    if(domain){put32(buffer+16,move?1:0);cmif+=16;}
    else if(move){put32(buffer+8,1U<<5);put32(buffer+12,move);}
    put32(buffer+cmif,0x4f434653);put32(buffer+cmif+8,result);put32(buffer+cmif+12,token);
    if(size)memcpy(buffer+cmif+16,data,size);
    if(domain && move)put32(buffer+cmif+16+size,move);
}
static unsigned log_varint(const uint8_t *p,size_t size,size_t *offset,uint64_t pc) {
    unsigned out=0;
    for(unsigned shift=0;shift<28;shift+=7) {
        if(*offset>=size)stop("INVALID_LOG_VARINT",pc,*offset,24);
        unsigned v=p[(*offset)++];out|=(v&127)<<shift;if(!(v&128))return out;
    }
    stop("LOG_VARINT_OVERFLOW",pc,*offset,24);return 0;
}
static unsigned *ipc_child(uint8_t *buffer,struct session *s,struct ipc_request *req,enum endpoint child,uint64_t pc) {
    uint32_t moved;
    if(req->domain) {
        if(s->nobjects==s->capacity) {
            if(s->capacity>=4096)stop("DOMAIN_OBJECTS_FULL",pc,s->nobjects,21);
            unsigned capacity=s->capacity*2;
            struct object *objects=realloc(s->objects,capacity*sizeof(*objects));
            if(!objects)stop("DOMAIN_OBJECT_ALLOCATION_FAILED",pc,capacity,21);
            memset(objects+s->capacity,0,(capacity-s->capacity)*sizeof(*objects));s->objects=objects;s->capacity=capacity;
        }
        s->objects[s->nobjects++]=(struct object){child,0};moved=s->nobjects;
    } else moved=session_new(child,pc);
    ipc_reply(buffer,req->token,0,NULL,0,moved,req->domain);
    return req->domain?&s->objects[moved-1].initialized:&session_get(moved,pc)->initialized;
}
static void ipc_event(uint8_t *buffer,struct ipc_request *req,uint32_t *slot,unsigned ready,uint64_t pc) {
    if(!*slot){*slot=session_new(EP_EVENT,pc);session_get(*slot,pc)->initialized=ready;}
    ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);
    put32(buffer+4,read32(buffer+4)|0x80000000U);put32(buffer+8,2);put32(buffer+12,*slot);
}
static void ipc_copy_events(uint8_t *buffer,struct ipc_request *req,const uint32_t *events,unsigned count,uint64_t pc) {
    if(!count || count>4)stop("INVALID_EVENT_COPY_COUNT",pc,count,24);
    memset(buffer,0,IPC_BYTES);put32(buffer+4,(32+(req->domain?16:0))/4|0x80000000U);put32(buffer+8,count<<1);
    for(unsigned i=0;i<count;i++)put32(buffer+12+i*4,session_clone(events[i],pc));
    size_t cmif=(12+count*4+15)&~15U;if(req->domain)cmif+=16;
    put32(buffer+cmif,0x4f434653);put32(buffer+cmif+12,req->token);
}
#include "clocks.h"
#include "filesystem.h"
#include "save.h"
#include "account.h"
#include "zero_friends.h"
#include "nvdrv.h"
#include "display.h"
#include "network.h"
#include "bsd.h"
#include "input.h"
#include "audren.h"
#include "audout.h"
#include "zero_controller_applet.h"
/* Terminator 2D, SDK 19.3.9: 35 additive command ids over 18.3.2 (docs/ipc-delta-vs-18.3.2.json). Frente D
 * delivers native/t2_horizon19.h with the table and a `t2_horizon19_dispatch(endpoint,&req,buffer,pc)`
 * that returns 1 when it answered; it is consulted just before UNIMPLEMENTED_IPC_COMMAND. Until it
 * exists the include and the call stay commented out. */
/* #include "t2_horizon19.h" */
static void logger_write(struct ipc_request *req,uint64_t pc) {
    if(req->num_inputs!=1)stop("INVALID_LOG_BUFFER_COUNT",pc,req->num_inputs,24);
    struct ipc_span span=req->inputs[0];const uint8_t *p=(const uint8_t *)(uintptr_t)span.address;
    if(span.size<24 || span.size>65536 || read32(p+20)>span.size-24)stop("INVALID_LOG_PACKET",pc,span.size,24);
    size_t end=24+read32(p+20),offset=24;
    while(offset<end) {
        unsigned key=log_varint(p,end,&offset,pc),size=log_varint(p,end,&offset,pc);
        if(size>end-offset)stop("INVALID_LOG_CHUNK",pc,size,24);
        struct line b={0};text(&b,"GUEST_LOG severity=");hex(&b,p[18]);text(&b," field=");hex(&b,key);text(&b," text=");
        if(key==2 || (key>=4 && key<=7) || key==10) {
            for(unsigned i=0;i<size && b.used<sizeof(b.data)-2;i++) {
                char c=p[offset+i];b.data[b.used++]=(c>=32 && c<127)||c=='\n'?c:'.';
            }
            emit(&b);
        }
        offset+=size;
    }
}
static _Thread_local uint64_t nifm_trace_counts[24];
static _Thread_local uint64_t audren_update_trace_count;
#if defined(T2_SWITCH_INPUT) && T2_SWITCH_INPUT
static _Thread_local uint64_t t2_audout_poll_trace_counts[2];
#endif
static enum endpoint sm_lookup(const uint8_t *name) {
    enum endpoint found=EP_NONE;
    if(!memcmp(name,"lm\0\0\0\0\0\0",8))found=EP_LM;
    if(!memcmp(name,"apm\0\0\0\0\0",8))found=EP_APM;
    if(!memcmp(name,"appletOE",8))found=EP_APPLET;
    if(!memcmp(name,"aoc:u\0\0\0",8))found=EP_AOC;
    if(!memcmp(name,"pctl\0\0\0\0",8) || !memcmp(name,"pctl:s\0\0",8) || !memcmp(name,"pctl:a\0\0",8) || !memcmp(name,"pctl:r\0\0",8))found=EP_PCTL;
    if(!memcmp(name,"time:u\0\0",8))found=EP_TIME;
    if(!memcmp(name,"fsp-srv\0",8))found=EP_FSP;
    if(!memcmp(name,"acc:u0\0\0",8))found=EP_ACCOUNT;
    if(!memcmp(name,"friend:u",8))found=EP_FRIENDS_CREATOR;
    if(!memcmp(name,"nvdrv\0\0\0",8))found=EP_NVDRV;
    if(!memcmp(name,"vi:u\0\0\0\0",8))found=EP_VI_ROOT;
    if(!memcmp(name,"nifm:u\0\0",8))found=EP_NIFM;
    if(!memcmp(name,"bsd:u\0\0\0",8))found=EP_BSD;
    if(!memcmp(name,"hid\0\0\0\0\0",8))found=EP_HID;
    if(!memcmp(name,"set\0\0\0\0\0",8))found=EP_SETTINGS;
    if(!memcmp(name,"audren:u",8))found=EP_AUDREN_MANAGER;
    /* SDK 17 services with no laboratory behaviour: every command succeeds with no output; command 0 yields a child object. */
    if(!memcmp(name,"ectx:aw\0",8))found=EP_AW_STUB;
    /* MOMODORA: Unity 2021 calls nn::ro::Initialize unconditionally (main+0x7c5e44, MEDIDO). No module is
     * ever loaded here: the title has no rom:/.nrr/dynamic.nrr, so the fsp OpenFile below returns PathNotFound. */
    if(!memcmp(name,"ldr:ro\0\0",8))found=EP_AW_STUB;
    /* MOMODORA: Unity's networking probes these; the port is offline, so they answer honestly with the
     * stub (no output, no session state). Measured one by one as the boot asked for them. */
    if(!memcmp(name,"ssl\0\0\0\0\0",8))found=EP_AW_STUB;
    /* MOMODORA: FMOD plays through AudioOut (docs/AUDIO.md), so the real audout:u is wired, not the
     * device-list stub the Animal Well trunk needed. */
    if(!memcmp(name,"audout:u",8))found=EP_AUDOUT_MANAGER;
    /* MOVIE: multimedia.nss asks for mm:u immediately after opening /dev/nvhost-nvdec (run 0170f).
     * The service is undocumented, so the stub answers and logs every command; probe only. */
    if(!memcmp(name,"mm:u\0\0\0\0",8) && getenv("NX_NVDEC_PROBE"))found=EP_AW_STUB;
    return found;
}
static unsigned sm_permitted(const char *name) {
    for(size_t i=0;i<sizeof(allowed_services)/sizeof(allowed_services[0]);i++)if(!strcmp(name,allowed_services[i]))return 1;
    /* SDK 17 asks for the system variant before the application one and does not tolerate the denial here. */
    if(!strcmp(name,"pctl:s") || !strcmp(name,"pctl:a") || !strcmp(name,"pctl:r")){struct line l={0};text(&l,"AW_ACL_EXTRA name=");text(&l,name);emit(&l);return 1;}
    return 0;
}
/* SDK >= 12 talks TIPC to sm: (no CMIF header). word0 = 16+command; word1 = raw words | pid<<10 | copies<<12 | moves<<16. */
static void tipc_reply(uint8_t *buffer,uint32_t result,const void *data,size_t size,uint32_t move) {
    /* TIPC = HIPC descriptors without CMIF: word1 = raw words | special<<31; special word = pid | copies<<1 | moves<<5. */
    memset(buffer,0,IPC_BYTES);
    size_t cursor=8;
    if(move){put32(buffer+8,1U<<5);put32(buffer+12,move);cursor=16;}
    size_t words=(4+size+3)/4;
    put32(buffer+4,(uint32_t)words | (move?0x80000000U:0));
    put32(buffer+cursor,result);
    if(size)memcpy(buffer+cursor+4,data,size);
}
static unsigned tipc_sm_dispatch(uint8_t *buffer,struct session *s,uint64_t handle,uint64_t pc) {
    uint32_t h0=read32(buffer),h1=read32(buffer+4);
    unsigned type=h0&65535;
    if(s->endpoint!=EP_SM || type<16)return 0;
    if((h0>>16)&4095)stop("TIPC_SM_BUFFERS_UNSUPPORTED",pc,h0,20);
    unsigned command=type-16,words=h1&1023,special=h1>>31,send_pid=0,copies=0,moves=0;
    size_t cursor=8;
    if(special){uint32_t w=read32(buffer+8);cursor=12;send_pid=w&1;copies=(w>>1)&15;moves=(w>>5)&15;if(send_pid)cursor+=8;cursor+=4*(copies+moves);}
    if(cursor+words*4>IPC_BYTES)stop("TIPC_SM_LENGTH",pc,h1,24);
    const uint8_t *data=buffer+cursor;
    struct line line={0};text(&line,"TIPC_SM command=");hex(&line,command);text(&line," words=");hex(&line,words);text(&line," h0=");hex(&line,h0);text(&line," h1=");hex(&line,h1);emit(&line);
    if(command==0) {
        /* RegisterClient: the kernel-provided pid is not needed by this laboratory. */
        (void)send_pid;s->initialized=1;tipc_reply(buffer,0,NULL,0,0);return 1;
    }
    if(command==1) {
        if(!s->initialized || words<2)stop("TIPC_SM_GET_SERVICE",pc,handle,24);
        char name[9]={0};memcpy(name,data,8);
        struct line get={0};text(&get,"GET_SERVICE name=");text(&get,name);text(&get," protocol=tipc");emit(&get);
        if(!sm_permitted(name)) {
            struct line denied={0};text(&denied,"SM_DENIED_BY_NPDM name=");text(&denied,name);emit(&denied);
            /* The SDK 17 client always reads handle at +0xc and result at +0x10 for this command. */
            tipc_reply(buffer,0x1015,NULL,0,0);put32(buffer+4,read32(buffer+4)|0x80000000U);put32(buffer+8,1U<<5);put32(buffer+12,0);put32(buffer+16,0x1015);return 1;
        }
        enum endpoint found=sm_lookup(data);
        if(!found)stop("UNIMPLEMENTED_SERVICE",pc,command,20);
        uint32_t moved=session_new(found,pc);tipc_reply(buffer,0,NULL,0,moved);
        struct line ok={0};text(&ok,"TIPC_SM_HANDLE name=");text(&ok,name);text(&ok," handle=");hex(&ok,moved);text(&ok," endpoint=");hex(&ok,found);emit(&ok);return 1;
    }
    if(command==4){tipc_reply(buffer,0,NULL,0,0);return 1;} /* DetachClient */
    stop("TIPC_SM_UNIMPLEMENTED_COMMAND",pc,command,20);return 1;
}
static void ipc_dispatch(uint64_t handle,uint64_t pc) {
retry_audio: ;
    struct session *s=session_get(handle,pc);
    uint8_t *buffer=(uint8_t *)(uintptr_t)guest_tls_base;
    if(!accessible((uintptr_t)buffer,IPC_BYTES,3))stop("INVALID_IPC_TLS",pc,0,24);
    if(tipc_sm_dispatch(buffer,s,handle,pc))return;
    struct ipc_request req=ipc_parse(buffer,pc,s->domain);
    enum endpoint endpoint=s->endpoint;unsigned *state=&s->initialized;
    struct object *object=NULL;
    if(req.domain) {
        if(!req.object_id || req.object_id>s->nobjects || !s->objects[req.object_id-1].endpoint)
            stop("INVALID_DOMAIN_OBJECT",pc,req.object_id,24);
        object=&s->objects[req.object_id-1];endpoint=object->endpoint;state=&object->initialized;
    }
    unsigned storage_inputs=req.input_objects;
    for(unsigned i=0;i<req.copies;i++)if(req.copy_handles[i]!=0xffff8001U && session_get(req.copy_handles[i],pc)->endpoint==EP_ZERO_APP_STORAGE)storage_inputs++;
    if(storage_inputs && (endpoint!=EP_ZERO_CONTROLLER || req.command!=100 || storage_inputs!=1))
        stop("ZERO_IPC_INPUT_OBJECT_SCOPE",pc,req.command,24);
    /* Only audio IPC waits for a block commit. A wait releases kernel_mutex,
     * so discard parsed pointers/session state and validate the whole request
     * again afterwards. No stale descriptor crosses that scheduling point. */
    if((endpoint==EP_AUDREN_MANAGER||endpoint==EP_AUDREN||endpoint==EP_AUDIO_DEVICE)&&aud_snapshot_wait_locked())goto retry_audio;
    if((endpoint==EP_AUDOUT_MANAGER||endpoint==EP_AUDOUT)&&aout_wait_operation())goto retry_audio;
    guest_profile_ipc_call(endpoint,req.command,req.domain_close,req.type,endpoint==EP_HID?"HID":
        endpoint==EP_AUDREN||endpoint==EP_AUDREN_MANAGER||endpoint==EP_AUDIO_DEVICE?"AUDIO":"OTHER");
    if(req.domain_close){endpoint_destroy(endpoint,*state);memset(object,0,sizeof(*object));ipc_reply(buffer,0,0,NULL,0,0,1);return;}
    unsigned trace=1,hid_trace=0;uint64_t trace_count=0;uint32_t vibration_handle=0;
    struct frame_log_event frame_log={0};
    if(endpoint==EP_NIFM_REQUEST && req.command<24) {
        trace_count=++nifm_trace_counts[req.command];trace=trace_count<=16 || !(trace_count&(trace_count-1));
    }
    if(endpoint==EP_AUDREN && (req.command==4 || req.command==10)) {
        trace_count=++audren_update_trace_count;trace=trace_count<=4 || !(trace_count&(trace_count-1));
    }
#if defined(T2_SWITCH_INPUT) && T2_SWITCH_INPUT
    /* Release-buffer polling can produce thousands of identical audio logs.
     * Sample only the trace; dispatch, buffer ownership and errors stay intact. */
    if(aw_release_mode() && endpoint==EP_AUDOUT && (req.command==7 || req.command==8)) {
        trace_count=++t2_audout_poll_trace_counts[req.command-7];
        trace=trace_count<=4 || !(trace_count&(trace_count-1));
    }
    /* GetNpadJoyHoldType repeats every frame. Sample only its valid, unchanged
     * request trace; changed hold/handle and malformed requests remain visible. */
    if(aw_release_mode() && endpoint==EP_HID && req.command==121 &&
       (req.type==4 || req.type==6) && req.data_size==12 && req.send_pid &&
       !req.num_inputs && !req.num_outputs && !req.copies) {
        uint64_t aruid;memcpy(&aruid,req.data,sizeof aruid);
        if(aruid==main_thread_id) {
            static _Thread_local struct frame_log_counter t2_hold_query_logs;
            frame_log=frame_log_sample(&t2_hold_query_logs,handle,
                ((uint64_t)req.type<<48)|((uint64_t)req.domain<<32)|req.object_id,hid_state.hold,0);
            trace_count=frame_log.occurrence;trace=frame_log.emit;
        }
    }
#endif
    /* Only the known benign HID query is sampled. Other services' cmd200 and
     * unusual request shapes keep their full pre-dispatch trace and errors. */
    if(endpoint==EP_HID && req.command==200 && (req.type==4 || req.type==6) &&
       req.data_size>=4 && req.data_size<=12 && !req.send_pid && !req.num_inputs && !req.num_outputs && !req.copies) {
        vibration_handle=read32(req.data);
        if(vibration_handle==3 || vibration_handle==0x10003) {
            static _Thread_local struct frame_log_counter hid_info_logs[2];
            frame_log=frame_log_sample(&hid_info_logs[vibration_handle>>16],handle,
                ((uint64_t)req.type<<48)|((uint64_t)req.data_size<<32)|req.object_id,
                ((uint64_t)req.domain<<32)|hid_vibration_mounted(),0);
            hid_trace=1;trace=frame_log.emit;
        }
    }
#if ZERO_ZX_GAME
    struct zero_hot_log_event zero_vibration_log={0};unsigned zero_vibration_trace=0;
    if(endpoint==EP_HID && req.command==206 && (req.type==4 || req.type==6) &&
       req.data_size>=8 && req.data_size<=16 && !req.send_pid && !req.copies &&
       req.num_inputs==2 && !req.num_outputs && req.inputs[0].size<=8 && !(req.inputs[0].size%4) &&
       req.inputs[1].size==req.inputs[0].size*4) {
        /* ipc_parse already validated input ranges. Capture exactly the trace
         * identity and vibration values; dispatch still reads and validates
         * the actual request, performs rumble, and returns its original reply. */
        struct {uint64_t meta[10];uint8_t data[16],handles[8],values[32];} key={0};
        key.meta[0]=handle;key.meta[1]=req.type;key.meta[2]=req.data_size;
        key.meta[3]=req.object_id;key.meta[4]=req.domain;key.meta[5]=req.inputs[0].address;
        key.meta[6]=req.inputs[0].size;key.meta[7]=req.inputs[1].address;key.meta[8]=req.inputs[1].size;
        key.meta[9]=((uint64_t)hid_vibration_mounted()<<1)|hid_vibration_permitted();
        memcpy(key.data,req.data,req.data_size);
        memcpy(key.handles,(const void *)(uintptr_t)req.inputs[0].address,req.inputs[0].size);
        memcpy(key.values,(const void *)(uintptr_t)req.inputs[1].address,req.inputs[1].size);
        static _Thread_local struct zero_hot_log_cache vibration_request_logs;
        zero_vibration_log=zero_hot_log_sample(&vibration_request_logs,&key,sizeof(key),0);
        zero_vibration_trace=1;trace=zero_vibration_log.emit;
    }
#endif
#ifdef ANIMAL_WELL_SHADERS
    {   /* Animal Well drives rumble from the audio capture effect: ~42 SendVibrationValue per frame. Log the first 16 only. */
        static unsigned aw_vibration_logged;
        if(trace && endpoint==EP_HID && req.command==201 && ++aw_vibration_logged>16)trace=0;
    }
#endif
    if(trace) {
        struct line b={0};text(&b,"IPC handle=");hex(&b,handle);text(&b," endpoint=");hex(&b,endpoint);
        text(&b," type=");hex(&b,req.type);text(&b," command=");hex(&b,req.command);
        text(&b," data_bytes=");hex(&b,req.data_size);text(&b," pid=");hex(&b,req.send_pid);text(&b," object=");hex(&b,req.object_id);
        if(trace_count){text(&b," occurrence=");hex(&b,trace_count);}
        if(hid_trace) {
            text(&b," occurrence=");hex(&b,frame_log.occurrence);text(&b," suppressed=");hex(&b,frame_log.suppressed);
            text(&b," vibration-handle=");hex(&b,vibration_handle);text(&b," log-thread=");hex(&b,guest_thread_index);
            text(&b," log-scope=thread-side changed=");hex(&b,frame_log.changed);
        }
#if ZERO_ZX_GAME
        if(zero_vibration_trace) {
            text(&b," occurrence=");hex(&b,zero_vibration_log.occurrence);
            text(&b," suppressed=");hex(&b,zero_vibration_log.suppressed);
            text(&b," log-scope=thread-exact-vibration changed=");hex(&b,zero_vibration_log.changed);
        }
#endif
        emit(&b);
    }
    if((req.type==5 || req.type==7) && req.command==3) {
        uint16_t pointer_size=(s->endpoint==EP_FSP || s->endpoint==EP_HID || s->endpoint==EP_NIFM ||
                               s->endpoint==EP_ACCOUNT || s->endpoint==EP_PROFILE)?sizeof(ipc_pointer_scratch):0;
        ipc_reply(buffer,req.token,0,&pointer_size,2,0,0);return;
    }
    if((req.type==5 || req.type==7) && req.command==0) {
        if(s->domain)stop("DOMAIN_ALREADY_CONVERTED",pc,handle,24);
        s->objects=calloc(16,sizeof(*s->objects));if(!s->objects)stop("DOMAIN_ALLOCATION_FAILED",pc,handle,21);
        s->capacity=16;s->domain=1;s->nobjects=1;s->objects[0]=(struct object){s->endpoint,s->initialized};
        uint32_t object=1;ipc_reply(buffer,req.token,0,&object,4,0,0);return;
    }
    if((req.type==5 || req.type==7) && (req.command==2 || req.command==4)) {
        if(req.command==4 && req.data_size<4)stop("INVALID_SESSION_CLONE_TAG",pc,handle,24);
        uint32_t copy=session_clone(handle,pc);
        ipc_reply(buffer,req.token,0,NULL,0,copy,0);return;
    }
    if(req.type!=4 && req.type!=6)stop("UNIMPLEMENTED_IPC_TYPE",pc,req.type,20);
    if(endpoint==EP_SM && req.command==0) {
        if(!req.send_pid || req.data_size<8 || *state)stop("INVALID_SM_INITIALIZE",pc,handle,24);
        *state=1;ipc_reply(buffer,req.token,0,NULL,0,0,req.domain);return;
    }
    if(endpoint==EP_SM && req.command==1) {
        if(!*state || req.data_size<8)stop("INVALID_SM_GET_SERVICE",pc,handle,24);
        char name[9]={0};memcpy(name,req.data,8);
        struct line line={0};text(&line,"GET_SERVICE name=");text(&line,name);emit(&line);
        unsigned permitted=0;
        for(size_t i=0;i<sizeof(allowed_services)/sizeof(allowed_services[0]);i++)
            if(!strcmp(name,allowed_services[i])){permitted=1;break;}
        if(!permitted) {
            struct line denied={0};text(&denied,"SM_DENIED_BY_NPDM name=");text(&denied,name);emit(&denied);
            ipc_reply(buffer,req.token,0x1015,NULL,0,0,req.domain);return;
        }
        enum endpoint found=sm_lookup(req.data);
        if(found) {
            if(req.domain)stop("SM_DOMAIN_UNSUPPORTED",pc,handle,20);
            uint32_t moved=session_new(found,pc);ipc_reply(buffer,req.token,0,NULL,0,moved,0);return;
        }
        stop("UNIMPLEMENTED_SERVICE",pc,req.command,20);
    }
    if(endpoint==EP_AW_AUDOUT_LIST) {
        if(req.domain)stop("AUDOUT_DOMAIN_UNSUPPORTED",pc,req.command,20);
        if(req.command==0 || req.command==2) {
            if(req.num_outputs!=1 || req.outputs[0].size<256)stop("AUDOUT_LIST_BUFFER",pc,0,24);
            memset((void *)(uintptr_t)req.outputs[0].address,0,256);memcpy((void *)(uintptr_t)req.outputs[0].address,"DeviceOut",9);
            struct line l={0};text(&l,"AUDOUT_LIST devices=1 name=DeviceOut");emit(&l);
            uint32_t count=1;ipc_reply(buffer,req.token,0,&count,4,0,0);return;
        }
        stop("AUDOUT_OPEN_NOT_IN_THIS_GAME",pc,req.command,20);
    }
    if(endpoint==EP_AW_STUB) {
        struct line stub={0};text(&stub,"AW_STUB_SERVICE command=");hex(&stub,req.command);text(&stub," data=");hex(&stub,req.data_size);emit(&stub);
        /* Lab probe: the media manager's poll payloads show what the player is
           waiting on. Bounded to 6 calls per command so a spin cannot flood. */
        static unsigned mm_seen[16];
        if(getenv("NX_NVDEC_PROBE") && req.command<16 && mm_seen[req.command]<6 && req.data && req.data_size) {
            mm_seen[req.command]++;
            struct line pd={0};text(&pd,"AW_STUB_PAYLOAD command=");hex(&pd,req.command);text(&pd," bytes=");hex(&pd,(unsigned)req.data_size);text(&pd," hex=");
            for(size_t i=0;i<req.data_size && i<16;i++){char hx[3];hx[0]="0123456789abcdef"[req.data[i]>>4];hx[1]="0123456789abcdef"[req.data[i]&15];hx[2]=0;text(&pd,hx);}
            emit(&pd);
        }
        if(req.command==0){ipc_child(buffer,s,&req,EP_AW_STUB,pc);return;}
        ipc_reply(buffer,req.token,0,NULL,0,0,req.domain);return;
    }
    if(aud_dispatch(buffer,s,&req,endpoint,state,pc))return;
    if(aout_dispatch(buffer,s,&req,endpoint,state,pc))return;
    if(zero_friends_dispatch(buffer,s,&req,endpoint,state,pc))return;
    if(endpoint==EP_SETTINGS && req.command==8) {
        /* libnx setGetQuestFlag: output u8. This is a normal application,
         * not a retail demonstration/Quest kiosk. */
        if(req.num_inputs || req.num_outputs || req.send_pid || req.copies)
            stop("INVALID_QUEST_FLAG_QUERY",pc,req.data_size,24);
        uint8_t quest=0;ipc_reply(buffer,req.token,0,&quest,1,0,req.domain);return;
    }
    if(endpoint==EP_SETTINGS && (req.command<=3 || req.command==5 || req.command==6)) {
        /* Language conversion is a settings ABI operation. The laboratory's
         * current language is en-US; this does not change any host settings. */
        static const char codes[][8]={"ja","en-US","fr","de","it","es","zh-CN","ko","nl","pt","ru","zh-TW","en-GB","fr-CA","es-419","zh-Hans","zh-Hant"};
        if(req.command==1 || req.command==5) {
            if(req.num_outputs!=1 || req.outputs[0].size%8)stop("INVALID_LANGUAGE_LIST_BUFFER",pc,req.num_outputs,24);
            uint64_t capacity=req.outputs[0].size/8;
            uint32_t count=capacity>sizeof(codes)/sizeof(codes[0])?sizeof(codes)/sizeof(codes[0]):capacity;
            memcpy((void *)(uintptr_t)req.outputs[0].address,codes,count*8);
            ipc_reply(buffer,req.token,0,&count,4,0,req.domain);return;
        }
        if(req.command==3 || req.command==6) {
            uint32_t count=sizeof(codes)/sizeof(codes[0]);ipc_reply(buffer,req.token,0,&count,4,0,req.domain);return;
        }
        if(req.command==2 && req.data_size<4)stop("INVALID_LANGUAGE_ARGUMENT",pc,req.data_size,24);
        uint32_t index=req.command==0?1:read32(req.data);
        if(index>=sizeof(codes)/sizeof(codes[0]))stop("UNSUPPORTED_LANGUAGE_INDEX",pc,index,20);
        ipc_reply(buffer,req.token,0,codes[index],8,0,req.domain);return;
    }
    if((endpoint==EP_LM || endpoint==EP_APM) && req.command==0) {
        if(endpoint==EP_LM && req.data_size<8)stop("INVALID_LOGGER_OPEN",pc,handle,24);
        enum endpoint child=endpoint==EP_LM?EP_LOGGER:EP_APM_SESSION;
        ipc_child(buffer,s,&req,child,pc);return;
    }
    if(endpoint==EP_APM && req.command==1) {
        /* This laboratory exposes Normal mode; no Switch boost-clock emulation. */
        uint32_t mode=0;ipc_reply(buffer,req.token,0,&mode,4,0,req.domain);return;
    }
    if(endpoint==EP_APM_SESSION && (req.command==0 || req.command==1)) {
        if(req.data_size<(req.command==0?8U:4U) || read32(req.data)>1)stop("INVALID_PERFORMANCE_MODE",pc,req.command,24);
        unsigned mode=read32(req.data);
        if(req.command==0) {
            performance_configs[mode]=read32(req.data+4);performance_configured[mode]=1;
            struct line policy={0};text(&policy,"APM_GUEST_POLICY mode=");hex(&policy,mode);
            text(&policy," config=");hex(&policy,performance_configs[mode]);text(&policy," host=Amlogic-governor");emit(&policy);
            ipc_reply(buffer,req.token,0,NULL,0,0,req.domain);return;
        }
        if(!performance_configured[mode])stop("INITIAL_PERFORMANCE_PROFILE_UNKNOWN",pc,mode,20);
        ipc_reply(buffer,req.token,0,&performance_configs[mode],4,0,req.domain);return;
    }
    if(endpoint==EP_LOGGER && req.command==0) {
        logger_write(&req,pc);ipc_reply(buffer,req.token,0,NULL,0,0,req.domain);return;
    }
    if(endpoint==EP_LOGGER && req.command==1) {
        if(req.data_size<4)stop("INVALID_LOG_DESTINATION",pc,handle,24);
        *state=read32(req.data);ipc_reply(buffer,req.token,0,NULL,0,0,req.domain);return;
    }
    if(endpoint==EP_APPLET && req.command==0) {
        if(!req.send_pid || req.data_size<8 || req.copies!=1 || req.copy_handles[0]!=0xffff8001U)
            stop("INVALID_APPLICATION_PROXY_OPEN",pc,handle,24);
        ipc_child(buffer,s,&req,EP_APP_PROXY,pc);return;
    }
    if(endpoint==EP_APP_PROXY) {
        enum endpoint child=EP_NONE;
        switch(req.command) {
        case 0:child=EP_COMMON;break;case 1:child=EP_SELF;break;case 2:child=EP_WINDOW;break;
        case 3:child=EP_APP_AUDIO;break;case 4:child=EP_APP_DISPLAY;break;case 11:child=EP_APP_LIBRARY;break;
        case 20:child=EP_APP_FUNCTIONS;break;case 1000:child=EP_APP_DEBUG;break;
        }
        if(child){ipc_child(buffer,s,&req,child,pc);return;}
    }
    if(endpoint==EP_WINDOW && req.command==1) {
        uint64_t aruid=main_thread_id;ipc_reply(buffer,req.token,0,&aruid,8,0,req.domain);return;
    }
    if(endpoint==EP_WINDOW && req.command==10) {
        app_state.foreground=1;ipc_reply(buffer,req.token,0,NULL,0,0,req.domain);return;
    }
    if(endpoint==EP_COMMON) {
        if(req.command==66) {
            if(req.data_size<4 || read32(req.data)>1)stop("INVALID_CPU_BOOST_MODE",pc,req.data_size,24);
            app_state.cpu_boost=read32(req.data);
            struct line policy={0};text(&policy,"APM_CPU_POLICY fast-load=");hex(&policy,app_state.cpu_boost);text(&policy," host=Amlogic-governor");emit(&policy);
            ipc_reply(buffer,req.token,0,NULL,0,0,req.domain);return;
        }
        uint32_t value=0;size_t size=0;
        if(req.command==0){ipc_event(buffer,&req,&app_state.message_event,app_state.focus_message,pc);return;}
        if(req.command==1) {
            uint32_t result=app_state.focus_message?0:0x680;value=15;app_state.focus_message=0;
            if(app_state.message_event)session_get(app_state.message_event,pc)->initialized=0;
            ipc_reply(buffer,req.token,result,&value,4,0,req.domain);return;
        }
        if(req.command==900) {
            /* SDK 12+: SetRequestExitToLibraryAppletAtExecuteNextProgramEnabled; no library applet runs here. */
            ipc_reply(buffer,req.token,0,NULL,0,0,req.domain);return;
        }
        if(req.command==5){value=0;size=1;} /* Portable operation mode. */
        if(req.command==6){value=0;size=4;} /* Normal performance mode. */
        if(req.command==9){value=app_state.foreground?1:2;size=1;}
        if(size){ipc_reply(buffer,req.token,0,&value,size,0,req.domain);return;}
    }
    if(endpoint==EP_SELF) {
        if(req.command==62 || req.command==63) {
            /* SDK 15b8e0/15b938 and c6fd0: enum0/1/2, uint32 input.
             * Get63 returns this application's stored mode (default0).
             * Foreground policy only: no host sleep timer or event changes. */
            size_t expected=req.command==62?4U:0U;
            if(req.data_size!=expected || req.send_pid || req.copies || req.num_inputs || req.num_outputs)
                stop("INVALID_IDLE_EXTENSION_REQUEST",pc,req.data_size,24);
            if(req.command==62) {
                uint32_t extension=read32(req.data);
                if(extension>2)stop("INVALID_IDLE_EXTENSION_VALUE",pc,extension,24);
                app_state.idle_extension=extension;
                ipc_reply(buffer,req.token,0,NULL,0,0,req.domain);return;
            }
            ipc_reply(buffer,req.token,0,&app_state.idle_extension,sizeof(app_state.idle_extension),0,req.domain);return;
        }
        if(req.command==9) {
            /* ISelfController::GetLibraryAppletLaunchableEvent. There is no
             * active library applet in this isolated foreground session.
             * Launch requests still require their own implemented protocol. */
            if(req.data_size)stop("INVALID_APPLET_LAUNCHABLE_REQUEST",pc,req.data_size,24);
            if(!app_state.launchable_event) {
                app_state.launchable_event=session_new(EP_EVENT,pc);
                session_get(app_state.launchable_event,pc)->initialized=1;
            }
            ipc_copy_events(buffer,&req,&app_state.launchable_event,1,pc);return;
        }
        if(req.command==91){ipc_event(buffer,&req,&app_state.suspend_event,0,pc);return;}
        if(req.command==90){uint64_t ticks=0;ipc_reply(buffer,req.token,0,&ticks,8,0,req.domain);return;}
        unsigned *flag=NULL;
        if(req.command==11)flag=&app_state.op_notify;
        if(req.command==12)flag=&app_state.perf_notify;
        /* SDK 4.4.1 SetResumeNotificationEnabled -> SetRestartMessageEnabled.
         * Store the subscription; this foreground session has no resume event. */
        if(req.command==14)flag=&app_state.resume_notify;
        if(req.command==16)flag=&app_state.suspend;
        if(flag) {
            if(req.data_size<1 || req.data[0]>1)stop("INVALID_APPLET_BOOLEAN",pc,req.command,24);
            *flag=req.data[0];ipc_reply(buffer,req.token,0,NULL,0,0,req.domain);return;
        }
        if(req.command==13) {
            if(req.data_size<3)stop("INVALID_FOCUS_HANDLING",pc,req.command,24);
            memcpy(app_state.focus_mode,req.data,3);ipc_reply(buffer,req.token,0,NULL,0,0,req.domain);return;
        }
        if(req.command==1 || req.command==2) {
            if(req.command==1)app_state.exit_lock++;
            else {if(!app_state.exit_lock)stop("EXIT_LOCK_UNDERFLOW",pc,0,24);app_state.exit_lock--;}
            ipc_reply(buffer,req.token,0,NULL,0,0,req.domain);return;
        }
    }
    if(endpoint==EP_APP_FUNCTIONS && req.command==40) {
        app_state.notified=1;uint8_t ready=1;ipc_reply(buffer,req.token,0,&ready,1,0,req.domain);return;
    }
    if(endpoint==EP_APP_FUNCTIONS && req.command==21) {
        /* IApplicationFunctions::GetDesiredLanguage, u64 language-code output.
         * The isolated laboratory profile uses English (US), not host locale. */
        if(req.data_size)stop("INVALID_DESIRED_LANGUAGE_REQUEST",pc,req.data_size,24);
        const char language[8]={'e','n','-','U','S',0,0,0};
        ipc_reply(buffer,req.token,0,language,sizeof(language),0,req.domain);return;
    }
    if(endpoint==EP_APP_FUNCTIONS && req.command==22) {
        if(req.data_size<4)stop("INVALID_TERMINATE_RESULT",pc,req.data_size,24);
        struct line fatal={0};text(&fatal,"GUEST_TERMINATE_RESULT result=");hex(&fatal,read32(req.data));emit(&fatal);
        guest_terminate_result=read32(req.data);trace_frames(current_guest_context);
        ipc_reply(buffer,req.token,0,NULL,0,0,req.domain);return;
    }
    if(endpoint==EP_APP_FUNCTIONS && req.command==130) {
        ipc_event(buffer,&req,&app_state.gpu_error_event,0,pc);return;
    }
    if(endpoint==EP_AOC && req.command==8) {
        ipc_event(buffer,&req,&app_state.aoc_event,0,pc);return;
    }
    if(endpoint==EP_AOC && req.command==2) {
        /* Supplied NSP is the base application; no DLC is installed. */
        if(!req.send_pid || req.data_size<8)stop("INVALID_AOC_COUNT_REQUEST",pc,handle,24);
        uint32_t count=0;ipc_reply(buffer,req.token,0,&count,4,0,req.domain);return;
    }
    if(endpoint==EP_PCTL && (req.command==0 || req.command==1)) {
        if(!req.send_pid || req.data_size<8)stop("INVALID_PCTL_CREATE",pc,handle,24);
        ipc_child(buffer,s,&req,EP_PCTL_SESSION,pc);return;
    }
    if(endpoint==EP_PCTL_SESSION && req.command==1) {
        *state=1;ipc_reply(buffer,req.token,0,NULL,0,0,req.domain);return;
    }
    if(time_dispatch(buffer,s,&req,endpoint,pc))return;
    if(endpoint==EP_FSP && req.command==1) {
        if(!req.send_pid || req.data_size<8)stop("INVALID_FSP_INITIALIZE",pc,handle,24);
        *state=1;ipc_reply(buffer,req.token,0,NULL,0,0,req.domain);return;
    }
    if(filesystem_dispatch(buffer,s,&req,endpoint,pc))return;
    if(save_dispatch(buffer,s,&req,endpoint,state,pc))return;
    if(account_dispatch(buffer,s,&req,endpoint,state,pc))return;
    if(nv_dispatch(buffer,&req,endpoint,state,pc))return;
    if(display_dispatch(buffer,s,&req,endpoint,pc))return;
    if(network_dispatch(buffer,s,&req,endpoint,state,pc))return;
    if(bsd_dispatch(buffer,&req,endpoint,state,pc))return;
    if(input_dispatch(buffer,s,&req,endpoint,pc))return;
    if(zero_applet_dispatch(buffer,s,&req,endpoint,state,pc))return;
    if(endpoint==EP_APP_LIBRARY && req.command==0) {
        if(req.data_size!=8)stop("INVALID_CREATE_LIBRARY_APPLET_INPUT",pc,req.data_size,24);
        struct line detail={0};text(&detail,"CREATE_LIBRARY_APPLET_REQUEST applet_id=");hex(&detail,read32(req.data));
        text(&detail," mode=");hex(&detail,read32(req.data+4));emit(&detail);
    }
    if(endpoint==EP_AOC && req.command==50) {
        /* SDK 12+: CheckAddOnContentMountStatus; this base has no add-on content. */
        struct line l={0};text(&l,"AW_AOC_STUB command=");hex(&l,req.command);emit(&l);
        ipc_reply(buffer,req.token,0,NULL,0,0,req.domain);return;
    }
    if(endpoint==EP_HID && req.command==1004) {
        /* SDK 17 touch-screen initialization sends 1004 (applet resource user id + configuration); no touch producer here. */
        struct line l={0};text(&l,"AW_HID_STUB command=");hex(&l,req.command);text(&l," data=");hex(&l,req.data_size);emit(&l);
        ipc_reply(buffer,req.token,0,NULL,0,0,req.domain);return;
    }
    /* if(t2_horizon19_dispatch(endpoint,&req,buffer,pc))return;   Frente D hook point (see the include above). */
    printf("UNIMPLEMENTED_IPC_DETAIL endpoint=%u command=%u data=%zu handle=0x%"PRIx64" send_pid=%u\n",endpoint,req.command,(size_t)req.data_size,handle,req.send_pid);
    stop("UNIMPLEMENTED_IPC_COMMAND",pc,req.command,20);
}
