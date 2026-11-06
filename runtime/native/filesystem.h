/* Read-only Program RomFS from the fully IVFC-verified owner-supplied section.
 * Remote transport identity: evidence/romfs-device-stage.json.
 * Filesystem parsing remains in the original SDK; this endpoint supplies bytes.
 */
static int romfs_fd=-1;
#include "t2_romfs_overlay_leanmt.h"
/* Frente A (27/09): optional boot.config variant served as a byte overlay of the pinned section (see
 * tools/bootcfg/t2_romfs_overlay.py). T2_BOOTCFG_OVERLAY=leanmt enables it; unset/0 = the section as is.
 * Every original byte of every range is checked against the file first; any mismatch keeps the base. */
static uint64_t romfs_size=ROMFS_SIZE;
static unsigned romfs_overlay_on;
static void romfs_overlay_select(void) {
    const char *o=getenv("T2_BOOTCFG_OVERLAY");
    if(!o || !*o || !strcmp(o,"0"))return;
    const char *why=NULL;
    if(strcmp(o,T2_ROMFS_OVERLAY_NAME))why="unknown-variant";
    else if(ROMFS_SIZE!=T2_ROMFS_OVERLAY_BASE_SIZE)why="base-size";
    else for(size_t i=0;!why && i<sizeof(t2_romfs_overlay_ranges)/sizeof(t2_romfs_overlay_ranges[0]);i++) {
        const struct t2_romfs_overlay_range *r=&t2_romfs_overlay_ranges[i];uint8_t got[1024];
        if(r->length>sizeof(got) || r->offset<ROMFS_OFFSET || r->offset-ROMFS_OFFSET+r->length>T2_ROMFS_OVERLAY_SIZE ||
           pread(romfs_fd,got,r->length,(off_t)r->offset)!=(ssize_t)r->length || memcmp(got,r->before,r->length))why="base-bytes";
    }
    if(why){printf("T2_ROMFS_OVERLAY refused=%s requested=%s section-unchanged=1\n",why,o);return;}
    romfs_overlay_on=1;romfs_size=T2_ROMFS_OVERLAY_SIZE;
    printf("T2_ROMFS_OVERLAY variant=%s ranges=%zu romfs-size=0x%llx sha256=%s section-file-unchanged=1\n",T2_ROMFS_OVERLAY_NAME,
        sizeof(t2_romfs_overlay_ranges)/sizeof(t2_romfs_overlay_ranges[0]),(unsigned long long)romfs_size,T2_ROMFS_OVERLAY_SHA256);
}
static void romfs_overlay_apply(uint8_t *dest,uint64_t offset,uint64_t size) {
    if(!romfs_overlay_on)return;
    uint64_t lo=ROMFS_OFFSET+offset,hi=lo+size;
    for(size_t i=0;i<sizeof(t2_romfs_overlay_ranges)/sizeof(t2_romfs_overlay_ranges[0]);i++) {
        const struct t2_romfs_overlay_range *r=&t2_romfs_overlay_ranges[i];
        uint64_t a=r->offset>lo?r->offset:lo,b=r->offset+r->length<hi?r->offset+r->length:hi;
        if(a<b)memcpy(dest+(a-lo),r->after+(a-r->offset),b-a);
    }
}
static int laboratory_save_fd=-1;
static void save_open(uint64_t pc) {
    if(laboratory_save_fd>=0)return;
    /* FRENTE-C: T2_SAVE_DIR (absolute, diagnostic runs only) keeps measurement runs off the owner's save. */
    const char *dir=getenv("T2_SAVE_DIR");
    if(!dir||dir[0]!='/')dir="save-laboratory";
    if(mkdir(dir,0700) && errno!=EEXIST)stop("SAVE_DIRECTORY_CREATE_FAILED",pc,errno,21);
    laboratory_save_fd=open(dir,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(laboratory_save_fd<0)stop("SAVE_DIRECTORY_OPEN_FAILED",pc,errno,21);
    struct line b={0};text(&b,"SAVE_OPEN isolated-directory=");text(&b,dir);emit(&b);
}
static unsigned auto_save_creation=1;
static void romfs_open(uint64_t pc) {
    if(romfs_fd>=0)return;
    romfs_fd=open(ROMFS_SECTION_NAME,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    struct stat st;
    if(romfs_fd<0 || fstat(romfs_fd,&st) || !S_ISREG(st.st_mode) || (uint64_t)st.st_size!=ROMFS_SECTION_SIZE)
        stop("VERIFIED_ROMFS_SECTION_UNAVAILABLE",pc,errno,21);
    struct line b={0};text(&b,"ROMFS_OPEN verified-section=");text(&b,LC2_ROMFS_SHA256);text(&b," size=");hex(&b,ROMFS_SIZE);emit(&b);
    romfs_overlay_select();
}
static int filesystem_dispatch(uint8_t *buffer,struct session *s,struct ipc_request *req,enum endpoint endpoint,uint64_t pc) {
    if(endpoint==EP_FSP && req->command==1005) {
        uint32_t mode=0;ipc_reply(buffer,req->token,0,&mode,4,0,req->domain);return 1;
    }
    if(endpoint==EP_FSP && req->command==1003) {
        auto_save_creation=0;ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
    }
    if(endpoint==EP_FSP && req->command==200) {
        romfs_open(pc);ipc_child(buffer,s,req,EP_STORAGE_ROMFS,pc);return 1;
    }
    if(endpoint==EP_STORAGE_ROMFS && req->command==4) {
        uint64_t size=romfs_size;ipc_reply(buffer,req->token,0,&size,8,0,req->domain);return 1;
    }
    if(endpoint==EP_STORAGE_ROMFS && req->command==0) {
        if(req->data_size<16)stop("INVALID_STORAGE_READ_ARGUMENTS",pc,req->data_size,24);
        uint64_t offset,size;memcpy(&offset,req->data,8);memcpy(&size,req->data+8,8);
        if(offset>romfs_size || size>romfs_size-offset || size>(64ULL<<20) ||
           (size && (req->num_outputs!=1 || size>req->outputs[0].size)))stop("INVALID_ROMFS_READ_RANGE",pc,offset,24);
        struct line b={0};text(&b,"ROMFS_READ offset=");hex(&b,offset);text(&b," size=");hex(&b,size);emit(&b);
        uint64_t done=0;uint8_t *dest=size?(uint8_t *)(uintptr_t)req->outputs[0].address:NULL;
        t2_gzram_touch((uintptr_t)dest,size); /* Frente J: pread would EFAULT on a compressed block */
        while(done<size) {
            ssize_t got=pread(romfs_fd,dest+done,size-done,ROMFS_OFFSET+offset+done);
            if(got<0 && errno==EINTR)continue;
            if(got<=0)stop("ROMFS_READ_FAILED",pc,offset+done,21);
            done+=(uint64_t)got;
        }
        romfs_overlay_apply(dest,offset,size);
        ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
    }
    return 0;
}
