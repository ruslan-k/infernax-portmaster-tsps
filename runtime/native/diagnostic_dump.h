/* Lossless compression is confined to diagnostic image files. Guest bytes,
 * rendering, pixel measurements and receipts remain unchanged. */
#include <zlib.h>
#include "aw_release.h"
static void nvn_private_dump(const char *kind,unsigned sequence,const void *data,size_t size) {
    /* FRENTE-C: T2_RELEASE_CAPTURE=1 keeps the release profile (AW_RELEASE=1) for timing runs and admits
     * only the explicitly requested frame image, so a measured optimisation can be checked by eye. */
    if(aw_release_mode()) {
        static int allow=-1;if(allow<0){const char *o=getenv("T2_RELEASE_CAPTURE");allow=o&&!strcmp(o,"1");}
        if(!allow||strcmp(kind,"frame-ppm"))return;
    }
    const char *run=getenv("COTM_RUN_ID");char path[128];
    if(!run || !*run || strspn(run,"0123456789")!=strlen(run) || strlen(run)>12)
        stop("NVN_CAPTURE_RUN_ID",nvn_caller,0,24);
    unsigned compressed=!strcmp(kind,"frame-ppm") || !strcmp(kind,"sample-rgba-slot") ||
        !strcmp(kind,"draw-indices") || !strcmp(kind,"draw-attr-slot");
    int length=snprintf(path,sizeof(path),"nvn-%s-%s-%u.bin%s",kind,run,sequence,compressed?".gz":"");
    if(length<0 || length>=(int)sizeof(path))stop("NVN_CAPTURE_PATH",nvn_caller,0,24);
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0)stop("NVN_CAPTURE_OPEN",nvn_caller,errno,21);
    if(compressed) {
        gzFile stream=gzdopen(fd,"wb1");
        if(!stream){close(fd);stop("NVN_CAPTURE_GZIP_OPEN",nvn_caller,0,21);}
        size_t done=0;
        while(done<size) {
            unsigned chunk=size-done>(1U<<20)?1U<<20:(unsigned)(size-done);
            int written=gzwrite(stream,(const uint8_t *)data+done,chunk);
            if(written!=(int)chunk) {
                int cause=errno,code=0;const char *message=gzerror(stream,&code);
                fprintf(stderr,"NVN_CAPTURE_GZIP_ERROR code=%d errno=%d message=%s\n",code,cause,message);
                gzclose(stream);stop("NVN_CAPTURE_WRITE",nvn_caller,cause?cause:EIO,21);
            }
            done+=chunk;
        }
        int result=gzclose(stream);
        if(result!=Z_OK)stop("NVN_CAPTURE_GZIP_CLOSE",nvn_caller,(uint64_t)(int64_t)result,21);
    } else {
        size_t done=0;
        while(done<size) {
            ssize_t written=write(fd,(const uint8_t *)data+done,size-done);
            if(written<=0)stop("NVN_CAPTURE_WRITE",nvn_caller,errno,21);
            done+=(size_t)written;
        }
        if(close(fd))stop("NVN_CAPTURE_CLOSE",nvn_caller,errno,21);
    }
}
