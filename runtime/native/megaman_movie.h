/* LC2 v1.0.0, private opt-in omission of the 12 MP4 assets only.
 * Include after line/text/hex/emit, accessible(), stop(), and ucontext_t.
 * Configure before installing traps, from the verified image contract.
 * This does not alter nninitStartup, nnMain, the scene poll, Stop, or cleanup.
 * No footage was decoded or visually approved when this handler is used.
 */
#ifndef MEGAMAN_MOVIE_H
#define MEGAMAN_MOVIE_H

#define LC2_MOVIE_MAIN_SHA256 "18582e801d996a7274475993c1d701095993369455a18fa72575c3527e605f54"
#define LC2_MOVIE_LOAD_OFFSET 0x36d578ULL
#define LC2_MOVIE_LOAD_OPCODE 0xa9bb67faU
#define LC2_MOVIE_VTABLE_OFFSET 0x9a6508ULL

static uint64_t megaman_movie_main_base;
static unsigned megaman_movie_omit_enabled;

/* Exact 12-file inventory, including case. No prefix matching, traversal,
 * unknown movies, or game-data paths are accepted by the omission handler. */
static int megaman_movie_allowed_path(const char *path) {
    static const char *const names[]={
        "CAPCOM15_Multi_0215.mp4", "ROCK8_0_Multi_0215.mp4",
        "ROCK8_1_Multi_0215.mp4", "ROCK8_2_Multi_0215.mp4",
        "Rock8_3_Multi_180206.mp4", "ROCK8_4_Multi_0215.mp4"
    };
    const char *prefix=NULL;
    if(!strncmp(path,"rom:/movie/JP/JP_",17))prefix="rom:/movie/JP/JP_";
    else if(!strncmp(path,"rom:/movie/US/US_",17))prefix="rom:/movie/US/US_";
    if(!prefix)return 0;
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++)
        if(!strcmp(path+strlen(prefix),names[i]))return 1;
    return 0;
}

static void megaman_movie_configure(uint64_t main_base,const char *verified_main_sha256) {
#if defined(LC2_ROMFS_OMITTED_VIDEOS) && LC2_ROMFS_OMITTED_VIDEOS
    const char *required_omission=getenv("MEGAMAN_OMIT_VIDEOS");
    if(!required_omission || strcmp(required_omission,"1"))
        stop("MOVIE_OMISSION_REQUIRED_BY_IMAGE",main_base,0,24);
#endif
    const char *setting=getenv("MEGAMAN_OMIT_VIDEOS");
    megaman_movie_main_base=main_base;
    megaman_movie_omit_enabled=0;
    if(!setting || !strcmp(setting,"0"))return;
    if(strcmp(setting,"1"))stop("MOVIE_OMISSION_INVALID_SETTING",main_base,0,24);
    if(!verified_main_sha256 || strcmp(verified_main_sha256,LC2_MOVIE_MAIN_SHA256))
        stop("MOVIE_OMISSION_NSO_IDENTITY",main_base,0,24);
    /* Guards bind object fields, native scene timing/termination, draw rejection,
     * and destructor null checks to this exact version before code is trapped. */
    static const struct {uint32_t offset,word;} guards[]={
        {0x36d578,LC2_MOVIE_LOAD_OPCODE}, {0x36d57c,0xa9015ff8},
        {0x36d580,0xa90257f6}, {0x36d584,0xa9034ff4},
        {0x36d3e4,0xa911027f}, {0x36d3ec,0xf9009e7f},
        {0x36d3f0,0xa9147e7f}, {0x36d820,0xf940a008},
        {0x36d834,0xb4000040}, {0x36d864,0x3944fc00},
        {0x36d46c,0x1400000f}, {0x36d480,0xb4000140},
        {0x36d4b0,0xb4000040}, {0x36d4dc,0xb40000a0},
        {0x36d4f4,0xb40000a0}, {0x36d50c,0xb40000a0},
        {0x006be0,0x320003e8}, {0x006be4,0x39002268},
        {0x0070d8,0xf9402508}, {0x0070e0,0x37000480},
        {0x163e24,0x7100391f}, {0x163e6c,0x97fa8be7},
        {0x163e70,0x360000a0}, {0x163e78,0x97fa8b8e},
        {0x163eb8,0x7100411f}, {0x163ec0,0x540001a3}
    };
    for(unsigned i=0;i<sizeof(guards)/sizeof(guards[0]);i++) {
        uint64_t address=main_base+guards[i].offset;
        uint32_t actual=0;
        if(!accessible(address,4,PROT_READ))stop("MOVIE_OMISSION_GUARD_ADDRESS",address,0,24);
        memcpy(&actual,(const void *)(uintptr_t)address,4);
        if(actual!=guards[i].word)stop("MOVIE_OMISSION_CODE_IDENTITY",address,actual,24);
    }
    megaman_movie_omit_enabled=1;
    struct line log={0};
    text(&log,"MOVIE_OMISSION_POLICY enabled=1 files=12 scope=MP4-only playback=not-performed");
    emit(&log);
}

/* The loader includes this predicate in its normal trap installation scan.
 * Return 0 when disabled, leaving the native entry byte-for-byte untouched. */
static unsigned megaman_movie_entry(uint64_t pc,uint32_t opcode) {
    if(!megaman_movie_omit_enabled || pc!=megaman_movie_main_base+LC2_MOVIE_LOAD_OFFSET)return 0;
    if(opcode!=LC2_MOVIE_LOAD_OPCODE)stop("MOVIE_OMISSION_ENTRY_IDENTITY",pc,opcode,24);
    return 1;
}

static unsigned megaman_movie_handle(uint64_t pc,uint32_t opcode,ucontext_t *ctx) {
    if(!megaman_movie_entry(pc,opcode))return 0;
    uint64_t object=ctx->uc_mcontext.regs[0],path_address=ctx->uc_mcontext.regs[1];
    if(!accessible(object,0x150,PROT_READ|PROT_WRITE))
        stop("MOVIE_OMISSION_OBJECT_ADDRESS",pc,object,24);
    unsigned char *movie=(unsigned char *)(uintptr_t)object;
    uint64_t vtable=0;memcpy(&vtable,movie,8);
    if(vtable!=megaman_movie_main_base+LC2_MOVIE_VTABLE_OFFSET)
        stop("MOVIE_OMISSION_OBJECT_TYPE",pc,vtable,24);
    if(movie[8])stop("MOVIE_OMISSION_ALREADY_STARTED",pc,object,24);
    /* The constructor creates the optional mutex at +0x118. All playback
     * resources must still be empty: worker, three textures, player, frame. */
    static const unsigned resource_offsets[]={0x110,0x120,0x128,0x130,0x138,0x140,0x148};
    for(unsigned i=0;i<sizeof(resource_offsets)/sizeof(resource_offsets[0]);i++) {
        uint64_t value=0;memcpy(&value,movie+resource_offsets[i],8);
        if(value)stop("MOVIE_OMISSION_NOT_FRESH",pc,resource_offsets[i],24);
    }
    char path[128];unsigned length=0;
    for(;length<sizeof(path);length++) {
        if(!accessible(path_address+length,1,PROT_READ))
            stop("MOVIE_OMISSION_PATH_ADDRESS",pc,path_address+length,24);
        path[length]=*(const char *)(uintptr_t)(path_address+length);
        if(!path[length])break;
    }
    if(length==sizeof(path) || !megaman_movie_allowed_path(path))
        stop("MOVIE_OMISSION_PATH_NOT_ALLOWED",pc,path_address,24);
    /* Native CHMovie::Load is void. Preserve x0..x30/SP, return to its caller.
     * A completed, intentionally omitted item owns no decoder or texture.
     * The unmodified MM8 poll then calls native Stop and post-stop cleanup. */
    memcpy(movie+9,path,length+1);
    movie[8]=1;
    movie[0x13f]=1;
    struct line log={0};text(&log,"MOVIE_OMITTED_INTENTIONALLY path=");text(&log,path);
    text(&log," object=");hex(&log,object);
    text(&log," playback=not-performed native-scene-transition=retained");emit(&log);
    ctx->uc_mcontext.pc=ctx->uc_mcontext.regs[30];
    return 1;
}
#endif
