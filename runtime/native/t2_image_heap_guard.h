/* A randomized host brk can overlap the fixed guest NSO image. Re-execute the
 * same ELF once without ASLR only after observing that exact heap collision. */
#ifndef T2_IMAGE_HEAP_GUARD_H
#define T2_IMAGE_HEAP_GUARD_H
#include <sys/personality.h>
static int t2_image_heap_overlap(FILE *maps,uint64_t *heap_start,uint64_t *heap_end) {
    char line[512],name[256];uint64_t start,end;
    while(fgets(line,sizeof line,maps)) {
        if(sscanf(line,"%"SCNx64"-%"SCNx64" %*s %*s %*s %*s %255[^\n]",&start,&end,name)!=3 || strcmp(name,"[heap]"))continue;
        for(size_t i=0;i<sizeof(images)/sizeof(images[0]);i++) {
            if(start<images[i].base+images[i].map_size && images[i].base<end) {
                *heap_start=start;*heap_end=end;return 1;
            }
        }
    }
    return ferror(maps)?-1:0;
}
static int t2_image_heap_reexec(char **argv,uint64_t start,uint64_t end) {
    int previous=personality(0xffffffffUL);
    if(previous<0){perror("T2_IMAGE_HEAP_PERSONALITY_QUERY");return 1;}
    if((unsigned)previous&ADDR_NO_RANDOMIZE) {
        fprintf(stderr,"T2_IMAGE_HEAP_COLLISION_RETRY_BLOCKED heap=0x%"PRIx64"..0x%"PRIx64"\n",start,end);return 1;
    }
    if(personality((unsigned long)(unsigned)previous|ADDR_NO_RANDOMIZE)<0) {
        perror("T2_IMAGE_HEAP_PERSONALITY_SET");return 1;
    }
    fprintf(stderr,"T2_IMAGE_HEAP_COLLISION heap=0x%"PRIx64"..0x%"PRIx64" action=same-elf-reexec-aslr-off\n",start,end);
    execv("/proc/self/exe",argv);
    perror("T2_IMAGE_HEAP_REEXEC_FAILED");return 1;
}
static int t2_image_heap_guard(unsigned enabled,char **argv) {
    if(!enabled)return 0; /* fbdev/Mali-450, host diagnostics and benchmarks stay unchanged. */
    FILE *maps=fopen("/proc/self/maps","r");
    if(!maps){perror("T2_IMAGE_HEAP_MAPS");return 1;}
    uint64_t start=0,end=0;int collision=t2_image_heap_overlap(maps,&start,&end);fclose(maps);
    if(collision<0){fprintf(stderr,"T2_IMAGE_HEAP_MAPS_READ_FAILED\n");return 1;}
    return collision?t2_image_heap_reexec(argv,start,end):0;
}
#endif
