/* Host-only lossless compact tiles. No guest writes or runtime eviction. */
#ifndef T2_PAGE_COMPACT_RUNTIME_H
#define T2_PAGE_COMPACT_RUNTIME_H
#include "nxinput_sha256.h"
static struct {unsigned configured,enabled,nibble;const char *dir;struct t2_compact_entry entries[T2_COMPACT_CAPACITY];} t2_compact;
static unsigned t2_compact_enabled(void) {
    if(t2_compact.configured)return t2_compact.enabled;
    t2_compact.configured=1;
    const char *o=getenv("T2_PAGE_COMPACT");
    if(o && strcmp(o,"0") && strcmp(o,"1") && strcmp(o,"4"))stop("T2_COMPACT_OPTION",nvn_caller,0,24);
    if(!o || !strcmp(o,"0"))return 0;
    unsigned want_nibble=!strcmp(o,"4"); /* Frente B: 4-bit tiles, catalogue compact4-pages.txt */
    /* Admission is based on the active GLES context and authenticated data,
     * never on a distribution name or on a launcher-only environment value.
     * Providers without adequate fragment highp keep the original idx8 path. */
    GLint range[2]={0},precision=0;glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER,GL_HIGH_FLOAT,range,&precision);
    GLenum error=glGetError();
    if(error || precision<16 || range[0]<16 || range[1]<16) {
        printf("T2_COMPACT fallback=idx8 reason=fragment-highp precision=%d range=%d/%d error=0x%x\n",precision,range[0],range[1],error);return 0;
    }
    t2_compact.dir=getenv("T2_COMPACT_DIR");
    if(!t2_compact.dir || !*t2_compact.dir) {printf("T2_COMPACT fallback=idx8 reason=no-catalogue\n");return 0;}
    if(want_nibble?!t2_compact4_manifest_load(t2_compact.dir,t2_compact.entries):!t2_compact_manifest_load(t2_compact.dir,t2_compact.entries))
        stop("T2_COMPACT_MANIFEST",nvn_caller,want_nibble,24);
    t2_compact.nibble=t2_compact_nibble_format=want_nibble;
    for(unsigned i=0;i<t2_pages.manifest.count;i++)if(!t2_compact_find(t2_compact.entries,&t2_pages.manifest.entries[i]))
        stop("T2_COMPACT_SOURCE_IDENTITY",nvn_caller,i,24);
    t2_compact.enabled=1;
    printf("T2_COMPACT enabled=1 tile=16 index=L8%s map=RGBA8 colours=original eviction=never highp=%d\n",t2_compact.nibble?"+nibble4-remap":"",precision);
    return 1;
}
static int t2_compact_hash(const uint8_t *bytes,size_t length,const uint8_t expected[32]) {
    nxinput_sha256 ctx;uint8_t digest[32];nxinput_sha256_init(&ctx);
    nxinput_sha256_update(&ctx,bytes,length);nxinput_sha256_final(&ctx,digest);
    return !memcmp(digest,expected,32);
}
struct t2_compact_stream {struct t2_texture_stream_gl_upload upload;nxinput_sha256 hash;};
static int t2_compact_rows(void *opaque,unsigned y,unsigned rows,const uint8_t *pixels) {
    struct t2_compact_stream *s=opaque;
    nxinput_sha256_update(&s->hash,pixels,(size_t)s->upload.width*rows);
    return t2_texture_stream_gl_rows(&s->upload,y,rows,pixels);
}
static unsigned t2_compact_upload(struct nvn_texture *t,const struct t2_page_entry *page,const GLuint names[2],size_t *bytes) {
    const struct t2_compact_entry *e=t2_compact_find(t2_compact.entries,page);
    if(!e)return 0;
    char rel[96],path[1024];uint8_t *map=NULL,*palette=NULL;unsigned ok=0;
    snprintf(rel,sizeof rel,e->nibble?"%s.tilemap4.rgba":"%s.tilemap.rgba",e->name);
    map=t2_page_read_file(t2_compact.dir,rel,256*256*4,0,0);
    snprintf(rel,sizeof rel,"idx8block/%s.pal256.rgba",e->name);
    palette=t2_page_read_file(t2_pages.dir,rel,T2_IDX16_PALETTE_BYTES,0,0);
    if(!map || !palette || !t2_compact_hash(map,256*256*4,e->map_hash) ||
       !t2_compact_hash(palette,T2_IDX16_PALETTE_BYTES,e->palette_hash))goto out;
    for(unsigned i=0;i<65536;i++) {
        unsigned n=(unsigned)map[i*4+1]*256+map[i*4],kind=map[i*4+3];
        if(map[i*4+2]>=page->palettes)goto out;
        if(!e->nibble?(n>=e->count || kind!=255):kind==255?n>=e->byte_tiles:(kind!=0 || n>=e->nibble_tiles))goto out;
    }
    if(snprintf(path,sizeof path,e->nibble?"%s/%s.compact4.l":"%s/%s.compact.l",t2_compact.dir,e->name)>=(int)sizeof path)goto out;
    FILE *file=fopen(path,"rb");if(!file)goto out;
    glBindTexture(GL_TEXTURE_2D,t->name);t2_idx16_parameters();
    struct t2_compact_stream stream={.upload={e->width,e->height,GL_LUMINANCE,GL_NO_ERROR,0}};
    nxinput_sha256_init(&stream.hash);
    int streamed=t2_texture_stream_file(file,e->width,e->height,1,0,t2_compact_rows,&stream);
    if(fclose(file))streamed=0;
    uint8_t digest[32];nxinput_sha256_final(&stream.hash,digest);
    if(!streamed || memcmp(digest,e->index_hash,32))goto out;
    glBindTexture(GL_TEXTURE_2D,names[0]);t2_idx16_parameters();
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,256,256,0,GL_RGBA,GL_UNSIGNED_BYTE,map);
    glBindTexture(GL_TEXTURE_2D,names[1]);t2_idx16_parameters();
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,256,256,0,GL_RGBA,GL_UNSIGNED_BYTE,palette);
    if(glGetError()!=GL_NO_ERROR)goto out;
    t->t2_page.blockmap_name=names[0];t->t2_page.palette_name=names[1];t->t2_page.compact_height=e->height;
    t->t2_page.compact_rows[0]=e->nibble_row;t->t2_page.compact_rows[1]=e->remap_row;t->t2_page.compact_rows[2]=e->identity_row;
    *bytes=(size_t)e->width*e->height+2*T2_IDX16_PALETTE_BYTES;ok=1;
    printf("T2_COMPACT_RESIDENT page=%s logical=4096x4096 packed=%ux%u tiles=%u bytes=%zu index-sha=verified map-sha=verified palette-sha=verified\n",
        e->name,e->width,e->height,e->count,*bytes);
out:
    free(map);free(palette);return ok;
}
#endif
