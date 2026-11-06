/* Host CLOCK_MONOTONIC + CLOCK_REALTIME expressed through the Horizon time ABI.
 * Shared offsets and layouts are documented by libnx services/time.{c,h}.
 * Time-zone service is UTC in this laboratory; unsupported rule operations stop.
 */
struct steady_context {int64_t base;uint8_t source[16];};
struct system_context {int64_t offset,time_point;uint8_t source[16];};
static unsigned clock_shared_plus_one;
static struct system_context clock_context;
/* Horizon TimeZoneRule, one UTC type, no transitions or DST. The zero
 * ttinfo has gmtoff=0, abbrind=0 and defaulttype=0. This is a valid rule,
 * usable by both service conversions and the SDK's local tzcode routines.
 * Layout: https://switchbrew.org/wiki/Glue_services#TimeZoneRule */
static const uint8_t utc_rule[0x4000]={[4]=1,[8]=4,[0x2b38]='U',[0x2b39]='T',[0x2b3a]='C'};
static void clocks_initialize(uint64_t pc) {
    if(clock_shared_plus_one)return;
    unsigned id=shared_create(PAGE,PROT_READ,pc);uint8_t *p=shared_regions[id].writer;
    struct timespec wall,mono;
    if(clock_gettime(CLOCK_REALTIME,&wall)||clock_gettime(CLOCK_MONOTONIC,&mono))stop("CLOCK_INIT_FAILED",pc,errno,21);
    struct steady_context steady={0};memcpy(steady.source,entropy,16);
    clock_context=(struct system_context){.offset=wall.tv_sec-mono.tv_sec,.time_point=mono.tv_sec};
    memcpy(clock_context.source,steady.source,16);
    memcpy(p+8,&steady,sizeof(steady));memcpy(p+8+sizeof(steady),&steady,sizeof(steady));
    for(unsigned at=0x38;at<=0x80;at+=0x48) {
        memcpy(p+at+8,&clock_context,sizeof(clock_context));memcpy(p+at+8+sizeof(clock_context),&clock_context,sizeof(clock_context));
    }
    clock_shared_plus_one=id+1;
}
static int time_dispatch(uint8_t *buffer,struct session *s,struct ipc_request *req,enum endpoint endpoint,uint64_t pc) {
    if(endpoint<EP_TIME || endpoint>EP_CLOCK_LOCAL)return 0;
    clocks_initialize(pc);
    if(endpoint==EP_TIME && req->command<=4) {
        static const enum endpoint children[]={EP_CLOCK_USER,EP_CLOCK_NETWORK,EP_CLOCK_STEADY,EP_TIMEZONE,EP_CLOCK_LOCAL};
        ipc_child(buffer,s,req,children[req->command],pc);return 1;
    }
    if(endpoint==EP_TIME && req->command==20) {
        uint32_t handle=session_new(EP_SHARED,pc);session_get(handle,pc)->initialized=clock_shared_plus_one-1;
        ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);
        put32(buffer+4,read32(buffer+4)|0x80000000U);put32(buffer+8,2);put32(buffer+12,handle);return 1;
    }
    if(endpoint==EP_TIME && req->command==300) {
        /* CotM SDK 4.5 GetCurrentTime at sdk+0x182de0 adds this base
         * to counter-derived seconds (not nanoseconds). Our steady clock
         * and guest counter share CLOCK_MONOTONIC with zero internal offset. */
        if(req->data_size!=sizeof(struct system_context))stop("INVALID_MONOTONIC_CONTEXT",pc,req->data_size,24);
        struct system_context context;memcpy(&context,req->data,sizeof(context));
        if(memcmp(context.source,clock_context.source,16))stop("MONOTONIC_CLOCK_SOURCE_MISMATCH",pc,0,24);
        ipc_reply(buffer,req->token,0,&context.offset,sizeof(context.offset),0,req->domain);return 1;
    }
    if(endpoint==EP_CLOCK_STEADY && req->command==0) {
        struct {int64_t seconds;uint8_t source[16];} out={.seconds=counter_ticks()/19200000ULL};
        memcpy(out.source,clock_context.source,16);ipc_reply(buffer,req->token,0,&out,sizeof(out),0,req->domain);return 1;
    }
    if(endpoint==EP_CLOCK_STEADY && req->command==200) {
        int64_t offset=0;ipc_reply(buffer,req->token,0,&offset,8,0,req->domain);return 1;
    }
    if(endpoint==EP_CLOCK_USER || endpoint==EP_CLOCK_NETWORK || endpoint==EP_CLOCK_LOCAL) {
        if(req->command==0) {
            int64_t current=clock_context.offset+counter_ticks()/19200000ULL;
            ipc_reply(buffer,req->token,0,&current,8,0,req->domain);return 1;
        }
        if(req->command==2){ipc_reply(buffer,req->token,0,&clock_context,sizeof(clock_context),0,req->domain);return 1;}
    }
    if(endpoint==EP_TIMEZONE && req->command==0) {
        char zone[0x24]="Etc/UTC";ipc_reply(buffer,req->token,0,zone,sizeof(zone),0,req->domain);return 1;
    }
    if(endpoint==EP_TIMEZONE && req->command==4) {
        char name[0x24]="Etc/UTC";
        if(req->data_size<sizeof(name) || memcmp(req->data,name,sizeof(name)) ||
           req->num_outputs!=1 || req->outputs[0].size!=sizeof(utc_rule) || req->num_inputs)
            stop("UNSUPPORTED_TIMEZONE_RULE",pc,req->data_size,20);
        memcpy((void *)(uintptr_t)req->outputs[0].address,utc_rule,sizeof(utc_rule));
        ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
    }
    if(endpoint==EP_TIMEZONE && (req->command==100 || req->command==101)) {
        if(req->command==100 && (req->num_inputs!=1 || req->inputs[0].size!=sizeof(utc_rule) ||
            memcmp((const void *)(uintptr_t)req->inputs[0].address,utc_rule,sizeof(utc_rule))))
            stop("UNSUPPORTED_CALENDAR_RULE",pc,req->num_inputs,20);
        if(req->data_size<8)stop("INVALID_CALENDAR_TIME",pc,req->command,24);
        int64_t timestamp;memcpy(&timestamp,req->data,8);time_t raw=timestamp;struct tm calendar;
        if(!gmtime_r(&raw,&calendar))stop("CALENDAR_RANGE_ERROR",pc,timestamp,24);
        struct {uint16_t year;uint8_t month,day,hour,minute,second,pad;uint32_t wday,yday;char zone[8];uint32_t dst;int32_t offset;} out={0};
        out.year=calendar.tm_year+1900;out.month=calendar.tm_mon+1;out.day=calendar.tm_mday;
        out.hour=calendar.tm_hour;out.minute=calendar.tm_min;out.second=calendar.tm_sec;
        out.wday=calendar.tm_wday;out.yday=calendar.tm_yday;memcpy(out.zone,"UTC",3);
        ipc_reply(buffer,req->token,0,&out,sizeof(out),0,req->domain);return 1;
    }
    return 0;
}
