/* Unity 2018.1.9f1's original SystemInfo ICalls. Report the actual Mali-450
 * floating texture absence so original post-processing selects its RGBA32
 * curves/LUT fallback. No scene, effect enable flag or game entry is changed.
 * All other format queries replay the exact original first instruction.
 * Sources: main NSO c2e1a4b3..., ICall table +0x3335280/288/290 and complete
 * 24-byte wrappers below. The original data and rtld relocations stay intact.
 */
static const struct {uint64_t offset;uint32_t words[6];const char *name;} messenger_caps_sites[]={
    {0x15cd110,{0xa9bf7bfd,0x910003fd,0x97f5ed2e,0x12000000,0xa8c17bfd,0xd65f03c0},"HasRenderTextureNative"},
    {0x15cd130,{0xa9bf7bfd,0x910003fd,0x97f5ed42,0x12000000,0xa8c17bfd,0xd65f03c0},"SupportsBlendingOnRenderTextureFormatNative"},
    {0x15cd150,{0xa9bf7bfd,0x910003fd,0x97f5ed5a,0x12000000,0xa8c17bfd,0xd65f03c0},"SupportsTextureFormatNative"}
};
static unsigned messenger_caps_site(uint64_t pc) {
    for(unsigned i=0;i<3;i++)if(pc==LC2_MAIN_BASE+messenger_caps_sites[i].offset)return i;
    return 3;
}
static unsigned messenger_caps_entry(uint64_t pc,uint32_t opcode) {
    unsigned site=messenger_caps_site(pc);if(site==3)return 0;
    if(opcode!=messenger_caps_sites[site].words[0] ||
       memcmp((void *)(uintptr_t)pc,messenger_caps_sites[site].words,24))
        stop("MESSENGER_CAPS_WRAPPER_IDENTITY",pc,opcode,24);
    return 1;
}
static unsigned messenger_caps_float_format(unsigned site,uint32_t format) {
    /* Unity TextureFormat R/RG/RGBA Half/Float and RGB9e5Float;
     * RenderTextureFormat ARGBHalf, DefaultHDR, R/RG/ARGB Float/Half,
     * RGB111110Float. Original mapping tables independently confirm enums. */
    return site==2?((format>=15&&format<=20)||format==22):
        (format==2||format==9||(format>=11&&format<=15)||format==22);
}
static unsigned messenger_caps_handle(uint64_t pc,uint32_t opcode,ucontext_t *ctx) {
    unsigned site=messenger_caps_site(pc);if(site==3)return 0;
    if(opcode!=0xa9bf7bfd)stop("MESSENGER_CAPS_INSTRUCTION",pc,opcode,24);
    uint32_t format=(uint32_t)ctx->uc_mcontext.regs[0];
    if(messenger_caps_float_format(site,format)) {
        if(!nvn_device.live || !nvn_device.float_formats_absent)
            stop("MESSENGER_CAPS_PHYSICAL_PROFILE",pc,format,24);
        struct line log={0};text(&log,"MESSENGER_GLES_CAPABILITY query=");text(&log,messenger_caps_sites[site].name);
        text(&log," format=");hex(&log,format);text(&log," supported=0 physical=no-float-texture caller=");
        location(&log,ctx->uc_mcontext.regs[30]);emit(&log);
        ctx->uc_mcontext.regs[0]=0;ctx->uc_mcontext.pc=ctx->uc_mcontext.regs[30];return 1;
    }
    /* STP x29,x30,[sp,#-16]!; retain the original wrapper and query result. */
    uint64_t sp=ctx->uc_mcontext.sp;
    if(sp<16 || (sp&15) || !accessible(sp-16,16,PROT_WRITE))stop("MESSENGER_CAPS_STACK",pc,sp,24);
    uint64_t pair[2]={ctx->uc_mcontext.regs[29],ctx->uc_mcontext.regs[30]};
    memcpy((void *)(uintptr_t)(sp-16),pair,16);ctx->uc_mcontext.sp=sp-16;ctx->uc_mcontext.pc=pc+4;
    return 1;
}
