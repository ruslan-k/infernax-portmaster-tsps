/* Port-local lossless index tiles. No encoded colour, eviction or guest writes.
 * Core metadata/parser is also exercised by the host GLES comparison. */
#ifndef T2_PAGE_COMPACT_H
#define T2_PAGE_COMPACT_H
#define T2_PAGE_MODE_COMPACT 3U
#define T2_COMPACT_CAPACITY 12U
static unsigned t2_compact_nibble_format; /* set once by t2_compact_enabled(); read by the shader generator */
struct t2_compact_entry {
    char name[24];unsigned w,h,tile,width,height,count;
    unsigned nibble,byte_tiles,nibble_tiles,nibble_row,remap_row,identity_row; /* T2COMPACT4 only (Frente B) */
    uint8_t fingerprint[2][32],index_hash[32],map_hash[32],palette_hash[32];
};
static inline int t2_compact_manifest_load(const char *dir,struct t2_compact_entry entries[T2_COMPACT_CAPACITY]) {
    char path[1024],line[640];unsigned count=0;
    if(!dir || snprintf(path,sizeof path,"%s/compact-pages.txt",dir)>=(int)sizeof path)return 0;
    FILE *f=fopen(path,"r");if(!f)return 0;
    int ok=fgets(line,sizeof line,f) && !strcmp(line,"T2COMPACT1\n");
    memset(entries,0,sizeof(*entries)*T2_COMPACT_CAPACITY);
    while(ok && fgets(line,sizeof line,f)) {
        if(count==T2_COMPACT_CAPACITY){ok=0;break;}
        struct t2_compact_entry *e=&entries[count];char hex[5][65],tail;
        int n=sscanf(line,"%23s %u %u %u %u %u %u %64s %64s %64s %64s %64s %c",e->name,&e->w,&e->h,&e->tile,&e->width,&e->height,&e->count,
            hex[0],hex[1],hex[2],hex[3],hex[4],&tail);
        unsigned number=0;char canonical[24];
        if(n!=12 || sscanf(e->name,"TexturePack%u",&number)!=1 || number>=12){ok=0;break;}
        snprintf(canonical,sizeof canonical,"TexturePack%u",number);
        if(strcmp(e->name,canonical) || e->w!=4096 || e->h!=4096 || e->tile!=16 || e->width!=4096 ||
           !e->height || e->height>4096 || e->height%16 || !e->count || e->count>65536 || e->height!=((e->count+255)/256)*16){ok=0;break;}
        for(unsigned i=0;i<count;i++)if(!strcmp(entries[i].name,e->name))ok=0;
        uint8_t *hashes[]={e->fingerprint[0],e->fingerprint[1],e->index_hash,e->map_hash,e->palette_hash};
        for(unsigned i=0;i<5;i++)if(strlen(hex[i])!=64 || !t2_page_unhex(hex[i],hashes[i]))ok=0;
        count++;
    }
    if(ferror(f))ok=0;
    fclose(f);return ok && count==T2_COMPACT_CAPACITY;
}
/* T2COMPACT4 (tools/textures/t2_compact4_pages.py): same identities, 4-bit tiles + remap + identity row. */
static inline int t2_compact4_manifest_load(const char *dir,struct t2_compact_entry entries[T2_COMPACT_CAPACITY]) {
    char path[1024],line[700];unsigned count=0;
    if(!dir || snprintf(path,sizeof path,"%s/compact4-pages.txt",dir)>=(int)sizeof path)return 0;
    FILE *f=fopen(path,"r");if(!f)return 0;
    int ok=fgets(line,sizeof line,f) && !strcmp(line,"T2COMPACT4\n");
    memset(entries,0,sizeof(*entries)*T2_COMPACT_CAPACITY);
    while(ok && fgets(line,sizeof line,f)) {
        if(count==T2_COMPACT_CAPACITY){ok=0;break;}
        struct t2_compact_entry *e=&entries[count];char hex[5][65],tail;
        int n=sscanf(line,"%23s %u %u %u %u %u %u %u %u %u %u %64s %64s %64s %64s %64s %c",e->name,&e->w,&e->h,&e->tile,&e->width,&e->height,
            &e->byte_tiles,&e->nibble_tiles,&e->nibble_row,&e->remap_row,&e->identity_row,hex[0],hex[1],hex[2],hex[3],hex[4],&tail);
        unsigned number=0;char canonical[24];
        if(n!=16 || sscanf(e->name,"TexturePack%u",&number)!=1 || number>=12){ok=0;break;}
        snprintf(canonical,sizeof canonical,"TexturePack%u",number);
        unsigned hb=(e->byte_tiles+255)/256*16,hn=(e->nibble_tiles+511)/512*16,hr=(e->nibble_tiles+255)/256;
        if(strcmp(e->name,canonical) || e->w!=4096 || e->h!=4096 || e->tile!=16 || e->width!=4096 || e->byte_tiles>65536 ||
           e->nibble_tiles>65536 || !(e->byte_tiles+e->nibble_tiles) || e->nibble_row!=hb || e->remap_row!=hb+hn ||
           e->identity_row!=hb+hn+hr || e->height!=e->identity_row+1 || e->height>4096){ok=0;break;}
        e->nibble=1;e->count=e->byte_tiles+e->nibble_tiles;
        for(unsigned i=0;i<count;i++)if(!strcmp(entries[i].name,e->name))ok=0;
        uint8_t *hashes[]={e->fingerprint[0],e->fingerprint[1],e->index_hash,e->map_hash,e->palette_hash};
        for(unsigned i=0;i<5;i++)if(strlen(hex[i])!=64 || !t2_page_unhex(hex[i],hashes[i]))ok=0;
        count++;
    }
    if(ferror(f))ok=0;
    fclose(f);return ok && count==T2_COMPACT_CAPACITY;
}
static inline const struct t2_compact_entry *t2_compact_find(const struct t2_compact_entry entries[T2_COMPACT_CAPACITY],const struct t2_page_entry *page) {
    if(!page || page->flipped>1)return NULL;
    for(unsigned i=0;i<T2_COMPACT_CAPACITY;i++) {
        const struct t2_compact_entry *e=&entries[i];
        if(e->w==page->w && e->h==page->h && !strcmp(e->name,page->name) && !memcmp(e->fingerprint[page->flipped],page->fp,32))return e;
    }
    return NULL;
}
#endif
