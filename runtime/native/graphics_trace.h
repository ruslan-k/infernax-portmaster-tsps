/* Read-only semantic entry tracing: replay the exact pinned first instruction
 * then resume the original SDK. No API return value or init step is replaced.
 */
#include "megaman_graphics_contract.h"
static const struct {uint64_t pc;uint32_t opcode;const char *name;int string_argument;} graphics_entries[]={
    {LC2_GRAPHICS_INIT_PC,LC2_GRAPHICS_INIT_OPCODE,"nvInitializeGraphics",-1},
    {LC2_GRAPHICS_BOOTSTRAP_PC,LC2_GRAPHICS_BOOTSTRAP_OPCODE,"nvnBootstrapLoader",0},
    {LC2_GRAPHICS_LOOKUP_PC,LC2_GRAPHICS_LOOKUP_OPCODE,"nvnDeviceGetProcAddressInternal",1},
};
static unsigned graphics_trace_entry(uint64_t pc,uint32_t instruction,ucontext_t *ctx) {
    unsigned i=0;while(i<sizeof(graphics_entries)/sizeof(graphics_entries[0]) && graphics_entries[i].pc!=pc)i++;
    if(i==sizeof(graphics_entries)/sizeof(graphics_entries[0]))return 0;
    if(instruction!=graphics_entries[i].opcode)stop("GRAPHICS_ENTRY_IDENTITY",pc,instruction,24);
    if(i==2){ctx->uc_mcontext.pc=(uintptr_t)cotm_nvnDeviceGetProcAddress;return 1;}
    struct line log={0};text(&log,"GRAPHICS_ENTRY name=");text(&log,graphics_entries[i].name);
    text(&log," caller=");location(&log,ctx->uc_mcontext.regs[30]);
    if(graphics_entries[i].string_argument>=0) {
        uint64_t address=ctx->uc_mcontext.regs[graphics_entries[i].string_argument];text(&log," query=");
        unsigned n=0;for(;n<128;n++){
            if(!accessible(address+n,1,PROT_READ))stop("GRAPHICS_QUERY_ADDRESS",pc,address,24);
            char c=*(char *)(uintptr_t)(address+n);if(!c)break;
            if(c<32 || c>126)stop("GRAPHICS_QUERY_CHARACTER",pc,c,24);
            log.data[log.used++]=c;
        }
        if(n==128)stop("GRAPHICS_QUERY_LENGTH",pc,n,24);
    }
    emit(&log);
    if(i<2 && (instruction&0xffc07fffU)==0xa9807bfdU) {
        /* stp x29,x30,[sp,#-N]! : replay the exact pinned prologue store. */
        int64_t imm=(int64_t)((int32_t)(((instruction>>15)&0x7f)<<25)>>25)*8;
        uint64_t sp=ctx->uc_mcontext.sp+imm;
        if(!accessible(sp,16,PROT_WRITE))stop("GRAPHICS_TRACE_STACK",pc,sp,24);
        memcpy((void *)(uintptr_t)sp,&ctx->uc_mcontext.regs[29],16);ctx->uc_mcontext.sp=sp;
    } else if(i<2 && (instruction&0xffc003ffU)==0xd10003ffU) {
        /* sub sp,sp,#imm12 */
        ctx->uc_mcontext.sp-=(instruction>>10)&0xfff;
    } else if(i<2)stop("GRAPHICS_TRACE_PROLOGUE",pc,instruction,24);
    else {
        uint32_t imm=((instruction>>29)&3)|(((instruction>>5)&0x7ffff)<<2);
        int64_t delta=(int64_t)((int32_t)(imm<<11)>>11)*4096;
        ctx->uc_mcontext.regs[8]=(pc&~4095ULL)+delta;
    }
    ctx->uc_mcontext.pc+=4;return 1;
}
