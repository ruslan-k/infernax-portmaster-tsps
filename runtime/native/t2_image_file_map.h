/* Terminator 2D, frente A (RAM do convidado, 27/09/2026): read-only guest image segments backed by their file.
 *
 * The loader used to fread every NSO segment into an anonymous mapping. text+rodata of rtld/main/subsdk0/sdk
 * are 62 MiB that the guest never writes; as anonymous memory they can only leave RAM through swap (on the
 * dArkOS device they were 62 MiB of the process's zram use in gameplay, MEDIDO smaps). Mapped MAP_PRIVATE from
 * the verified .seg file they are clean page cache: the kernel drops cold pages for free and re-reads them.
 * Pages the host patches (bridges/traps, TLS stubs in the tail padding, lazy-mesh thunks: ~80 pages) become
 * private copy-on-write pages exactly as before. Bytes seen by the guest are identical: the same file bytes
 * followed by zero to the page end (the kernel zero-fills the tail of the last file page).
 *
 * Eligible: perm without write, file_size>0, map_size == file_size rounded up to a page (no zero pages past
 * EOF, which would SIGBUS in a file mapping), regular file of exactly file_size bytes.
 * Executable segments additionally need the file to be mappable PROT_EXEC (a noexec mount, or a policy that
 * refuses exec file mappings, would make the later mprotect fail and abort the loader): probed per segment
 * with a one-page PROT_READ|PROT_EXEC mapping; refused = that segment keeps the anonymous copy.
 * T2_IMAGE_FILE_MAP=0 restores the anonymous copy for every segment. Any other value (or unset) = on.
 * Limit: replacing a .seg file IN PLACE while the game runs changes not-yet-copied pages (install/update
 * never runs with the game open; the ZIP installer writes new files). */
#ifndef T2_IMAGE_FILE_MAP_H
#define T2_IMAGE_FILE_MAP_H
static struct {int configured,enabled;unsigned mapped,exec_refused;uint64_t bytes;} t2_image_fm;
/* 1 when fd can back an executable mapping (measured, not assumed from the mount or firmware name). */
static int t2_image_file_exec_mappable(int fd) {
    void *q=mmap(NULL,4096,PROT_READ|PROT_EXEC,MAP_PRIVATE,fd,0);
    if(q==MAP_FAILED)return 0;
    munmap(q,4096);return 1;
}
static int t2_image_file_map_enabled(void) {
    if(!t2_image_fm.configured) {
        const char *o=getenv("T2_IMAGE_FILE_MAP");
        t2_image_fm.enabled=!(o&&!strcmp(o,"0"));t2_image_fm.configured=1;
    }
    return t2_image_fm.enabled;
}
static int t2_image_file_map(const struct image_segment *s) {
    if(!t2_image_file_map_enabled() || (s->perm&PROT_WRITE) || !s->file_size ||
       s->map_size!=((s->file_size+PAGE-1)&~(uint64_t)(PAGE-1)))return 0;
    int fd=open(s->name,O_RDONLY|O_CLOEXEC); /* follows links like the fopen path it replaces (lab dirs link the installed .seg files) */
    if(fd<0){fprintf(stderr,"segment open failed %s\n",s->name);exit(1);}
    struct stat st;
    if(fstat(fd,&st) || !S_ISREG(st.st_mode) || (uint64_t)st.st_size!=s->file_size){
        fprintf(stderr,"segment size mismatch %s\n",s->name);exit(1);}
    if((s->perm&PROT_EXEC) && !t2_image_file_exec_mappable(fd)) {
        printf("T2_IMAGE_FILE_MAP exec-mmap-refused segment=%s errno=%d fallback=anonymous-copy\n",s->name,errno);
        t2_image_fm.exec_refused++;close(fd);return 0;
    }
    map_region_fd(s->base,s->map_size,s->perm,s->state,s->name,fd);
    close(fd); /* the mapping keeps its own reference */
    t2_image_fm.mapped++;t2_image_fm.bytes+=s->map_size;
    return 1;
}
static void t2_image_file_map_report(void) {
    printf("T2_IMAGE_FILE_MAP %s segments=%u bytes=%" PRIu64 " exec-refused=%u kind=private-file-cow writable-segments=anonymous\n",
        t2_image_file_map_enabled()?"on":"off (T2_IMAGE_FILE_MAP=0)",t2_image_fm.mapped,t2_image_fm.bytes,t2_image_fm.exec_refused);
}
#endif
