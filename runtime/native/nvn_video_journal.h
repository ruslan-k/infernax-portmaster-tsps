/* One fresh append-only file per 256-frame group. Every present still writes
 * its own complete JSON line synchronously before Swap (including BLACK).
 * Existing files, symlinks, partial writes and close failures fail closed. */
enum nvn_video_journal_status {NVJ_OK,NVJ_CLOSE,NVJ_OPEN,NVJ_WRITE};
struct nvn_video_journal {int fd;unsigned group;};
static unsigned nvn_video_journal_append(struct nvn_video_journal *journal,int directory,unsigned frame,const char *json,size_t size) {
    unsigned group=frame/256;
    if(journal->fd<0 || group!=journal->group) {
        if(journal->fd>=0 && close(journal->fd))return NVJ_CLOSE;
        journal->fd=-1;
        char name[48];snprintf(name,sizeof(name),"group-%u.jsonl",group);
        int fd=openat(directory,name,O_WRONLY|O_CREAT|O_EXCL|O_APPEND|O_NOFOLLOW|O_CLOEXEC,0600);
        if(fd<0)return NVJ_OPEN;
        journal->fd=fd;journal->group=group;
    }
    if(write(journal->fd,json,size)!=(ssize_t)size)return NVJ_WRITE;
    return NVJ_OK;
}
