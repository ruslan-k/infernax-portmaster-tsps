#include <dirent.h>
/* Isolated host-backed account save; normal SDK filesystem calls are preserved.
 * Input paths are resolved component by component below an O_NOFOLLOW dirfd.
 * LC2 1.0.0 verified NACP: user data and journal are both 0x2000000.
 * NACP SHA-256: 883f2c6aaacb8879a4cc8ac4f2a45838391827d3405394e746ccf18f15994cbb.
 */
/* Momodora NACP (docs/nacp.json): user account save 1 MiB, journal 1 MiB. */
#define SAVE_DATA_SIZE 0x100000ULL
struct save_file {int fd;unsigned mode,open;};
static struct save_file save_files[64];
/* IDirectory over the host save directory: the game lists its save slots (MEDIDO run momo-062). */
struct save_directory {DIR *dir;unsigned open,filter;};
static struct save_directory save_directories[8];
static void endpoint_destroy(enum endpoint endpoint,unsigned state) {
    if(endpoint==EP_ZERO_CONTROLLER || endpoint==EP_ZERO_APP_STORAGE || endpoint==EP_ZERO_APP_ACCESSOR)zero_applet_destroy(endpoint,state);
    if(endpoint==EP_NVDRV)nv_destroy(state);
    if(endpoint==EP_BSD)bsd_destroy(state);
    if(endpoint==EP_NIFM_REQUEST)nifm_destroy(state);
    if(endpoint==EP_VIBRATION_LIST)vibration_destroy(state);
    if(endpoint==EP_AUDREN)aud_destroy(state);
    if(endpoint==EP_AUDOUT)aout_destroy(state);
    if(endpoint==EP_FRIENDS_SERVICE || endpoint==EP_FRIENDS_NOTIFICATIONS)zero_friends_destroy(state);
    if(endpoint==EP_TRANSFER)transfer_release(state,0);
    if(endpoint==EP_SAVE_FILE && state && state<=64 && save_files[state-1].open) {
        close(save_files[state-1].fd);save_files[state-1].open=0;
    }
    if(endpoint==EP_SAVE_DIR && state && state<=8 && save_directories[state-1].open) {
        closedir(save_directories[state-1].dir);save_directories[state-1].open=0;
    }
}
static uint32_t save_error(int error,uint64_t pc) {
    if(error==ENOENT || error==ENOTDIR)return 0x202;
    if(error==EEXIST)return 0x402;
    stop("UNMAPPED_SAVE_HOST_ERROR",pc,error,21);return 0;
}
static int save_parent(struct ipc_request *req,unsigned index,char leaf[770],uint64_t pc) {
    if(index>=req->num_inputs || req->inputs[index].size>769 || !req->inputs[index].size)
        stop("INVALID_SAVE_PATH_BUFFER",pc,index,24);
    const char *raw=(const char *)(uintptr_t)req->inputs[index].address;
    const char *end=memchr(raw,0,req->inputs[index].size);
    if(!end)stop("UNTERMINATED_SAVE_PATH",pc,index,24);
    char path[770];memcpy(path,raw,end-raw+1);char *at=path;
    while(*at=='/')at++;
    int dir=dup(laboratory_save_fd);if(dir<0)stop("SAVE_DUP_FAILED",pc,errno,21);
    if(!*at){strcpy(leaf,".");return dir;}
    while(*at) {
        char *slash=strchr(at,'/');if(slash)*slash=0;
        if(!*at || !strcmp(at,".") || !strcmp(at,"..") || strchr(at,'\\'))stop("UNSAFE_SAVE_PATH",pc,index,24);
        if(!slash){strcpy(leaf,at);return dir;}
        int next=openat(dir,at,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);int error=errno;close(dir);
        if(next<0){errno=error;return -1;}
        dir=next;at=slash+1;
    }
    strcpy(leaf,".");return dir;
}
static int save_dispatch(uint8_t *buffer,struct session *s,struct ipc_request *req,enum endpoint endpoint,unsigned *state,uint64_t pc) {
    if(endpoint==EP_FSP && req->command==51) {
        uint64_t program=0;if(req->data_size>=16)memcpy(&program,req->data+8,8);
        if(req->data_size<72 || req->data[0]!=1 || (program && program!=LC2_PROGRAM_ID) ||
           memcmp(req->data+16,laboratory_uid,16) || req->data[40]!=1 || req->data[41])
            stop("UNSUPPORTED_SAVE_IDENTITY",pc,program,24);
        save_open(pc);ipc_child(buffer,s,req,EP_SAVE_FS,pc);return 1;
    }
    if(endpoint==EP_SAVE_FS && req->command==16) {
        /* IFileSystem::GetFileSystemAttribute (18.x, MEDIDO run momo-061): the host save filesystem has
         * no optional attribute (no directory name extension, no multi-commit), so the whole record is
         * zero. Size comes from the caller's buffer. */
        unsigned char attribute[0x40]={0};
        unsigned size=req->data_size>=sizeof(attribute)?sizeof(attribute):(unsigned)sizeof(attribute);
        ipc_reply(buffer,req->token,0,attribute,size,0,req->domain);return 1;
    }
    if(endpoint==EP_SAVE_FS && req->command==10) {
        if(fsync(laboratory_save_fd))stop("SAVE_COMMIT_FAILED",pc,errno,21);
        ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
    }
    if(endpoint==EP_SAVE_FS && (req->command<=3 || req->command==7 || req->command==8 || req->command==9)) {
        char leaf[770]={0};int parent=save_parent(req,0,leaf,pc);
        uint32_t result=0;if(parent<0)result=save_error(errno,pc);
        uint32_t type=0;int fd=-1;
        struct line log={0};text(&log,"SAVE_PATH command=");hex(&log,req->command);text(&log," leaf=");text(&log,leaf);emit(&log);
        if(!result && req->command==7) {
            struct stat st;
            if(fstatat(parent,leaf,&st,AT_SYMLINK_NOFOLLOW))result=save_error(errno,pc);
            else if(S_ISREG(st.st_mode))type=1;
            else if(!S_ISDIR(st.st_mode))stop("UNSUPPORTED_SAVE_ENTRY",pc,st.st_mode,24);
        }
        if(!result && req->command==2 && mkdirat(parent,leaf,0700))result=save_error(errno,pc);
        if(!result && (req->command==1 || req->command==3)) {
            struct stat st;
            if(!strcmp(leaf,"."))stop("SAVE_ROOT_DELETE_REJECTED",pc,req->command,24);
            if(fstatat(parent,leaf,&st,AT_SYMLINK_NOFOLLOW))result=save_error(errno,pc);
            else if((req->command==1&&!S_ISREG(st.st_mode)) || (req->command==3&&!S_ISDIR(st.st_mode)))
                stop("SAVE_DELETE_TYPE_MISMATCH",pc,st.st_mode,24);
            else if(unlinkat(parent,leaf,req->command==3?AT_REMOVEDIR:0))result=save_error(errno,pc);
        }
        if(!result && req->command==0) {
            uint64_t size=0;if(req->data_size>=16)memcpy(&size,req->data+8,8);
            if(req->data_size<16 || size>SAVE_DATA_SIZE || read32(req->data))stop("INVALID_SAVE_CREATE",pc,size,24);
            fd=openat(parent,leaf,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
            if(fd<0)result=save_error(errno,pc);
            else {if(ftruncate(fd,size))stop("SAVE_CREATE_SIZE_FAILED",pc,errno,21);close(fd);}
        }
        if(!result && req->command==9) {
            unsigned filter=req->data_size>=4?read32(req->data):0;
            if(!(filter&3) || filter&~7U)stop("INVALID_SAVE_DIRECTORY_FILTER",pc,filter,24);
            int fd=openat(parent,leaf,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
            if(fd<0)result=save_error(errno,pc);
            else {
                DIR *dir=fdopendir(fd);
                if(!dir){close(fd);stop("SAVE_DIRECTORY_OPEN_FAILED",pc,errno,21);}
                unsigned slot=0;while(slot<8&&save_directories[slot].open)slot++;
                if(slot==8){closedir(dir);stop("SAVE_DIRECTORIES_FULL",pc,slot,21);}
                save_directories[slot]=(struct save_directory){dir,1,filter};close(parent);
                *ipc_child(buffer,s,req,EP_SAVE_DIR,pc)=slot+1;return 1;
            }
        }
        if(!result && req->command==8) {
            unsigned mode=req->data_size>=4?read32(req->data):0;
            if(!(mode&3) || mode&~7U)stop("INVALID_SAVE_OPEN_MODE",pc,mode,24);
            fd=openat(parent,leaf,((mode&3)==3?O_RDWR:(mode&2)?O_WRONLY:O_RDONLY)|O_NOFOLLOW|O_CLOEXEC);
            if(fd<0)result=save_error(errno,pc);
            else {
                struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode))stop("SAVE_FILE_NOT_REGULAR",pc,errno,24);
                unsigned slot=0;while(slot<64&&save_files[slot].open)slot++;
                if(slot==64)stop("SAVE_FILES_FULL",pc,slot,21);
                save_files[slot]=(struct save_file){fd,mode,1};close(parent);
                *ipc_child(buffer,s,req,EP_SAVE_FILE,pc)=slot+1;return 1;
            }
        }
        if(parent>=0)close(parent);
        ipc_reply(buffer,req->token,result,req->command==7?&type:NULL,req->command==7?4:0,0,req->domain);return 1;
    }
    if(endpoint==EP_SAVE_DIR) {
        if(!*state || *state>8 || !save_directories[*state-1].open)stop("INVALID_SAVE_DIRECTORY",pc,*state,24);
        struct save_directory *directory=&save_directories[*state-1];
        unsigned wants_directories=directory->filter&1,wants_files=directory->filter&2;
        if(req->command==0) {   /* Read: fills DirectoryEntry[0x310] records */
            if(req->num_outputs!=1)stop("INVALID_SAVE_DIRECTORY_BUFFER",pc,req->num_outputs,24);
            uint8_t *out=(uint8_t *)(uintptr_t)req->outputs[0].address;
            uint64_t capacity=req->outputs[0].size/0x310,count=0;
            for(struct dirent *entry;count<capacity && (entry=readdir(directory->dir));) {
                if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,".."))continue;
                struct stat st;
                if(fstatat(dirfd(directory->dir),entry->d_name,&st,AT_SYMLINK_NOFOLLOW))continue;
                unsigned is_directory=S_ISDIR(st.st_mode);
                if(!S_ISREG(st.st_mode) && !is_directory)continue;
                if(is_directory?!wants_directories:!wants_files)continue;
                uint8_t *record=out+count*0x310;memset(record,0,0x310);
                size_t length=strlen(entry->d_name);
                if(length>0x300)stop("SAVE_DIRECTORY_NAME_LENGTH",pc,length,24);
                memcpy(record,entry->d_name,length);
                record[0x304]=is_directory?0:1;   /* DirectoryEntryType: 0 directory, 1 file */
                int64_t size=is_directory?0:st.st_size;memcpy(record+0x308,&size,8);
                count++;
            }
            ipc_reply(buffer,req->token,0,&count,8,0,req->domain);return 1;
        }
        if(req->command==1) {   /* GetEntryCount */
            long position=telldir(directory->dir);uint64_t count=0;
            rewinddir(directory->dir);
            for(struct dirent *entry;(entry=readdir(directory->dir));) {
                if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,".."))continue;
                struct stat st;
                if(fstatat(dirfd(directory->dir),entry->d_name,&st,AT_SYMLINK_NOFOLLOW))continue;
                unsigned is_directory=S_ISDIR(st.st_mode);
                if(!S_ISREG(st.st_mode) && !is_directory)continue;
                if(is_directory?!wants_directories:!wants_files)continue;
                count++;
            }
            seekdir(directory->dir,position);
            ipc_reply(buffer,req->token,0,&count,8,0,req->domain);return 1;
        }
    }
    if(endpoint==EP_SAVE_FILE) {
        if(!*state || *state>64 || !save_files[*state-1].open)stop("INVALID_SAVE_FILE",pc,*state,24);
        struct save_file *file=&save_files[*state-1];
        if(req->command==2 || req->command==3) {
            if(!(file->mode&2))stop("SAVE_WRITE_PERMISSION_REQUIRED",pc,*state,24);
            if(req->command==3) {
                uint64_t size=0;if(req->data_size==8)memcpy(&size,req->data,8);
                if(req->data_size!=8 || size>SAVE_DATA_SIZE)stop("INVALID_SAVE_RESIZE",pc,size,24);
                if(ftruncate(file->fd,size))stop("SAVE_RESIZE_FAILED",pc,errno,21);
            } else if(fsync(file->fd))stop("SAVE_FLUSH_FAILED",pc,errno,21);
            ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
        }
        if(req->command==1) {
            uint64_t offset=0,size=0;if(req->data_size>=24){memcpy(&offset,req->data+8,8);memcpy(&size,req->data+16,8);}
            unsigned option=req->data_size>=4?read32(req->data):0;
            if(req->data_size<24 || !(file->mode&2) || offset>SAVE_DATA_SIZE || size>SAVE_DATA_SIZE-offset ||
               req->num_inputs!=1 || size>req->inputs[0].size || option&~1U)stop("INVALID_SAVE_WRITE",pc,offset,24);
            struct stat st;if(fstat(file->fd,&st))stop("SAVE_WRITE_SIZE_FAILED",pc,errno,21);
            if(offset+size>(uint64_t)st.st_size && !(file->mode&4))stop("SAVE_EXTENSION_REQUIRES_APPEND",pc,size,24);
            uint64_t done=0;const uint8_t *in=(const uint8_t *)(uintptr_t)req->inputs[0].address;
            t2_gzram_touch((uintptr_t)in,size); /* Frente J: pwrite would EFAULT on a compressed block */
            while(done<size) {
                ssize_t n=pwrite(file->fd,in+done,size-done,offset+done);
                if(n<0 && errno==EINTR)continue;
                if(n<=0)stop("SAVE_WRITE_FAILED",pc,errno,21);
                done+=n;
            }
            if(option && fsync(file->fd))stop("SAVE_WRITE_FLUSH_FAILED",pc,errno,21);
            ipc_reply(buffer,req->token,0,NULL,0,0,req->domain);return 1;
        }
        if(req->command==4) {
            struct stat st;if(fstat(file->fd,&st))stop("SAVE_FILE_SIZE_FAILED",pc,errno,21);
            uint64_t size=st.st_size;ipc_reply(buffer,req->token,0,&size,8,0,req->domain);return 1;
        }
        if(req->command==0) {
            uint64_t offset=0,size=0;if(req->data_size>=24){memcpy(&offset,req->data+8,8);memcpy(&size,req->data+16,8);}
            if(req->data_size<24 || !(file->mode&1) || offset>SAVE_DATA_SIZE || size>SAVE_DATA_SIZE-offset ||
               req->num_outputs!=1 || size>req->outputs[0].size || read32(req->data))stop("INVALID_SAVE_READ",pc,offset,24);
            uint64_t done=0;uint8_t *out=(uint8_t *)(uintptr_t)req->outputs[0].address;
            t2_gzram_touch((uintptr_t)out,size); /* Frente J */
            while(done<size) {
                ssize_t n=pread(file->fd,out+done,size-done,offset+done);
                if(n<0 && errno==EINTR)continue;
                if(n<0)stop("SAVE_READ_FAILED",pc,errno,21);
                if(!n)break;
                done+=n;
            }
            ipc_reply(buffer,req->token,0,&done,8,0,req->domain);return 1;
        }
    }
    return 0;
}
