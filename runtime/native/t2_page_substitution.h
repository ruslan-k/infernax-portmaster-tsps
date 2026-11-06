/* t2_page_substitution.h — Frente F: the 4096x4096 RGBA8 texture pages of Terminator 2D:
 * NO FATE are stored in the host as the lossless idx8+block-palette (Frente C mode 2,
 * 16.3 MiB) or idx16 (mode 1, 32.25 MiB) form instead of 64 MiB RGBA8, and every fragment
 * program that samples one of them gets a variant whose sampler read goes through the
 * palette. docs/PAGINAS-NO-HOST.md has the measurements.
 *
 * Two sections:
 *   CORE  (always): fingerprint, manifest, catalogue files, CPU reference decoders and the
 *         fragment-source variant generator. Standalone C11 + <GLES2/gl2.h>; also compiled
 *         natively by tests/t2_page_substitution_test.c.
 *   HOST  (#ifdef T2_PAGE_HOST_IMPL, included a second time from nvn_textures.h once
 *         struct nvn_texture exists): the hooks — deferral at TextureInitialize, the
 *         CopyBufferToTexture interception, RGBA8 fallback, sampler binding.
 *
 * Identification (MEDIDO shape, HOST.md 10 / PAGINAS-NO-HOST.md 1): Unity decodes the PNG
 * on the CPU and hands the page to the host as one CopyBufferToTexture of the whole level 0.
 * Hashing 64 MiB per page on an A53 is a boot-time cost the port cannot pay, so the page is
 * identified by a SPARSE fingerprint over 64 KiB (2048 chunks of 32 bytes, deterministic
 * offsets, plus the dimensions) — the same function as tools/textures/t2_page_manifest.py,
 * which also proves the 24 fingerprints (12 pages x 2 orientations) are distinct.
 *
 * Environment:
 *   T2_PAGES_DIR      catalogue directory (t2_pages.txt + idx8block/ + idx16/); unset = off
 *   T2_PAGE_MODE      idx8 (default) | idx16 | off
 *   T2_PAGE_STREAM=1  opt-in sequential index uploads in 64-row slabs; default off
 *   T2_PAGE_MIRROR=1  also mirror the RGBA8 bytes into the guest pool as the RGBA8 path does
 *                     (default off: the pool pages of a substituted page are never touched)
 *   T2_PAGE_MADVISE   1 = MADV_DONTNEED on the page's pool range only if the pool is
 *                     CPU_NO_ACCESS; 2 = force (unsafe, measurement only). Default off.
 */
#ifndef T2_PAGE_SUBSTITUTION_H
#define T2_PAGE_SUBSTITUTION_H
#include <GLES2/gl2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t2_texture_index.h"
#include "t2_page_wrap.h"
#include "zero_sha256.h"

#define T2_PAGE_FP_CHUNKS 2048u
#define T2_PAGE_FP_CHUNK 32u
#define T2_PAGE_MANIFEST_MAX 64
#define T2_PAGE_MODE_OFF 0
#define T2_PAGE_MODE_IDX8 1
#define T2_PAGE_MODE_IDX16 2

/* Per-texture state, embedded in struct nvn_texture by the host. */
struct t2_page_state {
    unsigned deferred;      /* eligible page: no GL storage until its pixels arrive */
    unsigned mode;          /* 0 = RGBA8 (not substituted), 1 = idx8-block, 2 = idx16 */
    unsigned flipped;       /* the rows of the catalogue files were reversed on load */
    unsigned linear_logged,strips;
    uint8_t *staging;       /* transient RGBA8 of the strips received so far (w*h*4), freed when the page completes */
    uint8_t *rows_covered;  /* bitmap, one bit per row */
    uint32_t rows_done;
    GLuint blockmap_name,palette_name;
    struct t2_page_wrap_cache blockmap_wrap;
    uint32_t block,palettes,entries;
    size_t gl_bytes;
    char name[24];
    const struct t2_page_entry *early; /* ROOT: identified when the copy was RECORDED (staging already dropped) */
    uint64_t early_id;
    const struct t2_page_entry *pending_entry; /* matched catalogue, not resident yet; never RGBA fallback */
    unsigned compact_height; /* mode3 packed L8 rows; logical dimensions stay4096 */
    unsigned compact_rows[3]; /* T2COMPACT4: nibble, remap and identity rows (Frente B) */
};

struct t2_page_entry {
    char name[24];uint32_t w,h,block,palettes,entries;unsigned flipped;uint8_t fp[32];
};

/* Fingerprint of a w x h RGBA8 image whose rows start `stride` bytes apart (stride >= w*4).
 * Chunk i (32 bytes) starts at linear byte offset i*S + (i*7919) % (S-32), S = w*h*4/2048.
 * A chunk may straddle a row boundary, so the bytes are gathered one by one through the
 * stride. sha256("T2PAGE1" + le32 w + le32 h + le32 bytes + chunks). */
static inline void t2_page_fingerprint(const uint8_t *src,uint64_t stride,uint32_t w,uint32_t h,uint8_t out[32]) {
    uint32_t bytes=w*h*4u,row=w*4u,S=bytes/T2_PAGE_FP_CHUNKS;
    static const char magic[8]="T2PAGE1";
    size_t total=7+12+(size_t)T2_PAGE_FP_CHUNKS*T2_PAGE_FP_CHUNK;
    uint8_t *m=(uint8_t *)malloc(total);
    if(!m || S<=T2_PAGE_FP_CHUNK){memset(out,0,32);free(m);return;}
    memcpy(m,magic,7);
    uint32_t le[3]={w,h,bytes};
    for(unsigned k=0;k<3;k++)for(unsigned b=0;b<4;b++)m[7+k*4+b]=(uint8_t)(le[k]>>(8*b));
    uint8_t *p=m+19;
    for(uint32_t i=0;i<T2_PAGE_FP_CHUNKS;i++) {
        uint32_t off=i*S+(uint32_t)(((uint64_t)i*7919u)%(S-T2_PAGE_FP_CHUNK));
        for(uint32_t b=0;b<T2_PAGE_FP_CHUNK;b++,off++)
            *p++=src[(uint64_t)(off/row)*stride+(off%row)];
    }
    zero_sha256(m,total,out);
    free(m);
}
static inline void t2_page_hex(const uint8_t *fp,char hex[65]) {
    static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<32;i++){hex[2*i]=digits[fp[i]>>4];hex[2*i+1]=digits[fp[i]&15];}
    hex[64]=0;
}
static inline int t2_page_unhex(const char *hex,uint8_t fp[32]) {
    for(unsigned i=0;i<64;i++) {
        char c=hex[i];unsigned v;
        if(c>='0'&&c<='9')v=(unsigned)(c-'0');else if(c>='a'&&c<='f')v=(unsigned)(c-'a'+10);else return 0;
        if(i&1)fp[i/2]|=(uint8_t)v;else fp[i/2]=(uint8_t)(v<<4);
    }
    return hex[64]==0||hex[64]==' ';
}

/* Manifest: "<hex64> <name> <w> <h> <png|flipped> <block> <palettes> <idx16 entries>" per line. */
struct t2_page_manifest {struct t2_page_entry entries[T2_PAGE_MANIFEST_MAX];unsigned count;};
static inline int t2_page_manifest_load(const char *dir,struct t2_page_manifest *m) {
    char path[1024];FILE *f;char line[512];
    memset(m,0,sizeof *m);
    if(!dir || snprintf(path,sizeof path,"%s/t2_pages.txt",dir)>=(int)sizeof path)return 0;
    f=fopen(path,"r");if(!f)return 0;
    while(fgets(line,sizeof line,f)) {
        char hex[65],name[24],orient[16];unsigned w,h,block,palettes,entries;
        if(line[0]=='#'||line[0]=='\n')continue;
        if(sscanf(line,"%64s %23s %u %u %15s %u %u %u",hex,name,&w,&h,orient,&block,&palettes,&entries)!=8)continue;
        if(m->count>=T2_PAGE_MANIFEST_MAX)break;
        struct t2_page_entry *e=&m->entries[m->count];
        if(strlen(hex)!=64 || !t2_page_unhex(hex,e->fp))continue;
        strcpy(e->name,name);e->w=w;e->h=h;e->block=block;e->palettes=palettes;e->entries=entries;
        e->flipped=!strcmp(orient,"flipped");
        m->count++;
    }
    fclose(f);
    return m->count>0;
}
static inline const struct t2_page_entry *t2_page_manifest_find(const struct t2_page_manifest *m,const uint8_t fp[32],uint32_t w,uint32_t h) {
    for(unsigned i=0;i<m->count;i++)if(m->entries[i].w==w && m->entries[i].h==h && !memcmp(m->entries[i].fp,fp,32))return &m->entries[i];
    return NULL;
}

/* Read a whole catalogue file of exactly `bytes` bytes; with flip_rows the rows of `row`
 * bytes are stored bottom-up (matches a `flipped` fingerprint, i.e. Unity's row order). */
static inline uint8_t *t2_page_read_file(const char *dir,const char *rel,size_t bytes,unsigned flip_rows,size_t row) {
    char path[1024];FILE *f;uint8_t *data;
    if(snprintf(path,sizeof path,"%s/%s",dir,rel)>=(int)sizeof path)return NULL;
    f=fopen(path,"rb");if(!f)return NULL;
    data=(uint8_t *)malloc(bytes);
    if(!data){fclose(f);return NULL;}
    if(!flip_rows || !row || bytes%row) {
        if(fread(data,1,bytes,f)!=bytes || fgetc(f)!=EOF){free(data);fclose(f);return NULL;}
    } else {
        size_t rows=bytes/row;
        for(size_t y=0;y<rows;y++)if(fread(data+(rows-1-y)*row,1,row,f)!=row){free(data);fclose(f);return NULL;}
        if(fgetc(f)!=EOF){free(data);fclose(f);return NULL;}
    }
    fclose(f);
    return data;
}
/* idx8-block triple from <dir>/idx8block/<name>.{idx8.l,blockmap.l,pal256.rgba}. */
static inline int t2_page_load_idx8(const char *dir,const struct t2_page_entry *e,uint8_t **index,uint8_t **blockmap,uint8_t **palette) {
    char rel[96];uint32_t bw=e->w/e->block,bh=e->h/e->block;
    *index=*blockmap=*palette=NULL;
    if(!e->block || (e->block&(e->block-1)) || e->w%e->block || e->h%e->block || e->palettes>256)return 0;
    snprintf(rel,sizeof rel,"idx8block/%s.idx8.l",e->name);
    *index=t2_page_read_file(dir,rel,(size_t)e->w*e->h,e->flipped,e->w);
    snprintf(rel,sizeof rel,"idx8block/%s.blockmap.l",e->name);
    *blockmap=t2_page_read_file(dir,rel,(size_t)bw*bh,e->flipped,bw);
    snprintf(rel,sizeof rel,"idx8block/%s.pal256.rgba",e->name);
    *palette=t2_page_read_file(dir,rel,T2_IDX16_PALETTE_BYTES,0,0);
    if(*index && *blockmap && *palette)return 1;
    free(*index);free(*blockmap);free(*palette);*index=*blockmap=*palette=NULL;return 0;
}
/* idx16 pair from <dir>/idx16/<name>.{idx16.la,pal.rgba}. */
static inline int t2_page_load_idx16(const char *dir,const struct t2_page_entry *e,uint8_t **index,uint8_t **palette) {
    char rel[96];
    *index=*palette=NULL;
    snprintf(rel,sizeof rel,"idx16/%s.idx16.la",e->name);
    *index=t2_page_read_file(dir,rel,(size_t)e->w*e->h*2,e->flipped,(size_t)e->w*2);
    snprintf(rel,sizeof rel,"idx16/%s.pal.rgba",e->name);
    *palette=t2_page_read_file(dir,rel,T2_IDX16_PALETTE_BYTES,0,0);
    if(*index && *palette)return 1;
    free(*index);free(*palette);*index=*palette=NULL;return 0;
}
#include "t2_page_compact.h"

/* CPU reference of the GLSL helpers (exact integer form of the NEAREST fetches):
 *   idx8-block: p = blockmap[(y/B)*(w/B) + x/B], k = index[y*w+x], colour = atlas row p entry k
 *   idx16:      i = la[2t] | la[2t+1]<<8,        colour = palette entry i (x = i&255, y = i>>8)
 * tests/t2_page_substitution_test.c compares these with the RGBA8 source: 0 mismatches. */
static inline void t2_page_decode_idx8(const uint8_t *index,const uint8_t *blockmap,const uint8_t *palette,
                                       uint32_t w,uint32_t h,uint32_t block,uint8_t *rgba) {
    uint32_t bw=w/block;
    for(uint32_t y=0;y<h;y++) {
        const uint8_t *brow=blockmap+(size_t)(y/block)*bw;const uint8_t *irow=index+(size_t)y*w;uint8_t *out=rgba+(size_t)y*w*4;
        for(uint32_t x=0;x<w;x++,out+=4)memcpy(out,palette+((size_t)brow[x/block]*256+irow[x])*4,4);
    }
}
static inline void t2_page_decode_idx16(const uint8_t *la,const uint8_t *palette,uint32_t w,uint32_t h,uint8_t *rgba) {
    size_t texels=(size_t)w*h;
    for(size_t t=0;t<texels;t++){unsigned i=la[2*t]|((unsigned)la[2*t+1]<<8);memcpy(rgba+t*4,palette+(size_t)i*4,4);}
}

static inline unsigned t2_page_mode_from_env(void) {
    const char *o=getenv("T2_PAGE_MODE");
    if(!o || !*o || !strcmp(o,"idx8"))return T2_PAGE_MODE_IDX8;
    if(!strcmp(o,"idx16"))return T2_PAGE_MODE_IDX16;
    return T2_PAGE_MODE_OFF;
}

/* ---------------------------------------------------------------------------
 * Fragment variant. The catalogue (tools/shaders, Frente B) routes every sample of sampler
 * NAME through one generated helper pair — MEDIDO in nvn_shader_sources.h for the three game
 * fragments (67 NormalShader, 188 ColorTransform, 120 CRT):
 *   uniform sampler2D NAME;
 *   uniform vec4 ms_swizzle_NAME;
 *   vec4 ms_sample_NAME(vec2 p) {vec4 c=texture2D(NAME,p);return ms_apply_swizzle(c,ms_swizzle_NAME);}
 *   vec4 ms_sample_NAME(vec2 p,float bias) {vec4 c=texture2D(NAME,p,bias);return ...}
 * For every sampler j whose bit is set in `key` the two reads `vec4 c=texture2D(NAME,p)` and
 * `vec4 c=texture2D(NAME,p,bias)` are replaced by the palette helper (the bias is dropped:
 * the page has one level and NEAREST), and `uniform mediump sampler2D t2_palette_NAME;`
 * (+ t2_blockmap_NAME in idx8 mode) are declared before the first sampler declaration
 * together with the helper function. The helper text is Frente C's
 * T2_IDX8_BLOCK_GLSL_FUNCTION / T2_IDX16_GLSL_FUNCTION with the uv parameter at `precision`
 * (mediump under the Mali fragment contract, where highp does not exist; highp otherwise).
 * The swizzle helper stays: an uploaded page samples with the identity selector.
 * Like the R8/mip wrappers, this rewrite happens BEFORE them, so their pattern
 * `texture2D(NAME,` no longer matches the page sampler and they leave it alone; the
 * texture2D calls inside the helper use the parameter names, which they never match.
 * Returns a malloc'd source or NULL when a keyed sampler's read is not found. */
/* Palette-coordinate formula (t2_page_calibrate picks it on the running GPU):
 *   0  V*(255/256) + 0.5/256          Frente C, exact when the L8 fetch returns k/255 (llvmpipe: MEDIDO)
 *   1  V + 0.5/256                    exact when the fetch returns k/256 (8-bit fixed point unit)
 *   2  (floor(V*255 + 0.5) + 0.5)/256 integer recovery, exact for any fetch within +-0.5/255 of k/255
 * T2_PAGE_FORMULA=<n> forces one; T2_PAGE_EMULATE_K256=1 (bench only) makes the fetch return k/256. */
static unsigned t2_page_formula,t2_page_emulate_k256;
static inline void t2_page_coord_text(char *out,size_t size,const char *v) {
    if(t2_page_formula==1)snprintf(out,size,"%s + (0.5 / 256.0)",v);
    else if(t2_page_formula==2)snprintf(out,size,"(floor(%s * 255.0 + 0.5) + 0.5) / 256.0",v);
    else snprintf(out,size,"%s * (255.0 / 256.0) + (0.5 / 256.0)",v);
}
static inline char *t2_page_replace_all(const char *source,const char *from,const char *to,unsigned *count) {
    size_t lf=strlen(from),lt=strlen(to),n=0;const char *s;
    for(s=source;(s=strstr(s,from));s+=lf)n++;
    char *out=(char *)malloc(strlen(source)+n*(lt>lf?lt-lf:0)+1);
    if(!out)return NULL;
    char *o=out;const char *p=source;
    while((s=strstr(p,from))){memcpy(o,p,(size_t)(s-p));o+=s-p;memcpy(o,to,lt);o+=lt;p=s+lf;}
    strcpy(o,p);
    if(count)*count=(unsigned)n;
    return out;
}
/* Compact addressing performs texel arithmetic. Preserve the UV interpolation
 * and function argument before that arithmetic; casting a rounded fp16 value
 * to highp cannot recover the missing4096-page coordinate bits. Both linked
 * stages use this only for the independently admitted compact variant. */
static inline char *t2_page_compact_varying_source(const char *source) {
    return t2_page_replace_all(source,"varying mediump ","varying highp ",NULL);
}
static inline char *t2_page_fragment_source(const char *source,const char *const names[],unsigned count,unsigned key,unsigned mode,unsigned medium) {
    if(!source || strncmp(source,"#version 100\n",13) || count>8 || !key || (mode!=T2_PAGE_MODE_IDX8 && mode!=T2_PAGE_MODE_IDX16 && mode!=T2_PAGE_MODE_COMPACT))return NULL;
    const char *precision=medium?"mediump":"highp";
    /* 1. the reads */
    char *current=mode==T2_PAGE_MODE_COMPACT?t2_page_compact_varying_source(source):(char *)malloc(strlen(source)+1);
    if(!current)return NULL;
    if(mode!=T2_PAGE_MODE_COMPACT)strcpy(current,source);
    for(unsigned j=0;j<count;j++) {
        if(!(key&(1u<<j)))continue;
        if(!names[j] || !*names[j] || strlen(names[j])>=64){free(current);return NULL;}
        char from[160],to[512];unsigned n1=0,n2=0;
        if(mode==T2_PAGE_MODE_COMPACT) {
            snprintf(from,sizeof from,"ms_sample_%s(vec2 p",names[j]);
            snprintf(to,sizeof to,"ms_sample_%s(highp vec2 p",names[j]);
            unsigned changed=0;char *next=t2_page_replace_all(current,from,to,&changed);
            free(current);current=next;if(!current || !changed){free(current);return NULL;}
        }
        for(unsigned pass=0;pass<2;pass++) {
            snprintf(from,sizeof from,"vec4 c=texture2D(%s,p%s);",names[j],pass?",bias":"");
            if(mode==T2_PAGE_MODE_COMPACT && t2_compact_nibble_format)snprintf(to,sizeof to,"vec4 c=t2_compact_sample(%s,t2_blockmap_%s,t2_palette_%s,p,t2_compact_%s,t2_compact4_%s);",names[j],names[j],names[j],names[j],names[j]);
            else if(mode==T2_PAGE_MODE_COMPACT)snprintf(to,sizeof to,"vec4 c=t2_compact_sample(%s,t2_blockmap_%s,t2_palette_%s,p,t2_compact_%s);",names[j],names[j],names[j],names[j]);
            else if(mode==T2_PAGE_MODE_IDX8)snprintf(to,sizeof to,"vec4 c=t2_idx8_block_sample(%s,t2_blockmap_%s,t2_palette_%s,p);",names[j],names[j],names[j]);
            else snprintf(to,sizeof to,"vec4 c=t2_idx16_sample(%s,t2_palette_%s,p);",names[j],names[j]);
            char *next=t2_page_replace_all(current,from,to,pass?&n2:&n1);
            free(current);current=next;if(!current)return NULL;
        }
        if(!n1){free(current);return NULL;}   /* the unbiased read is the one every draw uses */
    }
    /* 2. declarations + helper before the first sampler declaration */
    const char *at=strstr(current,"uniform sampler2D ");
    if(!at){free(current);return NULL;}
    size_t head=(size_t)(at-current),cap=strlen(current)+4096+(size_t)count*288;
    char *out=(char *)malloc(cap);if(!out){free(current);return NULL;}
    memcpy(out,current,head);size_t o=head;
    for(unsigned j=0;j<count;j++) {
        if(!(key&(1u<<j)))continue;
        o+=(size_t)snprintf(out+o,cap-o,"uniform mediump sampler2D t2_palette_%s;\n",names[j]);
        if(mode!=T2_PAGE_MODE_IDX16)o+=(size_t)snprintf(out+o,cap-o,"uniform mediump sampler2D t2_blockmap_%s;\n",names[j]);
        if(mode==T2_PAGE_MODE_COMPACT)o+=(size_t)snprintf(out+o,cap-o,"uniform highp vec4 t2_compact_%s;\n",names[j]);
        if(mode==T2_PAGE_MODE_COMPACT && t2_compact_nibble_format)o+=(size_t)snprintf(out+o,cap-o,"uniform highp vec4 t2_compact4_%s;\n",names[j]);
    }
    /* Palette coordinate: formula chosen by t2_page_calibrate (0 = Frente C's, exact for an L8 fetch
     * that returns k/255). The fetch may be wrapped to EMULATE a k/256 texture unit (PC only). */
    char kx[160],ky[160],fk[96],fp[96];
    const char *fetch_k=mode==T2_PAGE_MODE_IDX8?"texture2D(page, uv).r":"i.r",*fetch_p=mode==T2_PAGE_MODE_IDX8?"texture2D(blockmap, uv).r":"i.a";
    if(t2_page_emulate_k256){snprintf(fk,sizeof fk,"(floor(%s * 255.0 + 0.5) / 256.0)",fetch_k);snprintf(fp,sizeof fp,"(floor(%s * 255.0 + 0.5) / 256.0)",fetch_p);}
    else {snprintf(fk,sizeof fk,"%s",fetch_k);snprintf(fp,sizeof fp,"%s",fetch_p);}
    t2_page_coord_text(kx,sizeof kx,mode==T2_PAGE_MODE_IDX8?"k":"i.r");t2_page_coord_text(ky,sizeof ky,mode==T2_PAGE_MODE_IDX8?"p":"i.a");
    if(mode==T2_PAGE_MODE_COMPACT) {
        /* All new arithmetic stays highp even when the surrounding original
         * shader uses mediump. Capability admission excludes Mali450/fp16.
         * Wrap is applied in the original4096 domain before PNG orientation;
         * packed storage and tilemap themselves are always NEAREST/CLAMP. */
        t2_page_coord_text(kx,sizeof kx,"k");t2_page_coord_text(ky,sizeof ky,"m.b");
        if(t2_compact_nibble_format)o+=(size_t)snprintf(out+o,cap-o,
            "highp float t2_compact_axis(highp float v,highp float w) {\n"
            " highp float p=floor(v*4096.0);\n"
            " if(w>1.5){p=mod(p,8192.0);return p<4096.0?p:8191.0-p;}\n"
            " if(w>0.5)return mod(p,4096.0);\n"
            " return clamp(p,0.0,4095.0);\n}\n"
            /* Frente B: T2COMPACT4. rows.x nibble region, rows.y remap region, rows.z identity row.
             * Byte tiles: one fetch. Nibble tiles: packed byte, then its remap entry (branch; the identity row stays in the data but is unused). */
            "mediump vec4 t2_compact_sample(mediump sampler2D page,mediump sampler2D tilemap,mediump sampler2D palette,highp vec2 uv,highp vec4 params,highp vec4 rows) {\n"
            " highp vec2 p=vec2(t2_compact_axis(uv.x,params.z),t2_compact_axis(uv.y,params.w));\n"
            " if(params.y>0.5)p.y=4095.0-p.y;\n"
            /* m and the fetched byte go to highp temporaries first: an expression with only
             * mediump operands is evaluated at mediump (fp16 on Mali, lowered by llvmpipe too),
             * where tile numbers above 2048 are not representable (MEDIDO: 3055 became 3056). */
            " highp vec4 m=texture2D(tilemap,(floor(p/16.0)+0.5)/256.0);\n"
            " highp float lo=floor(m.r*255.0+0.5),hi=floor(m.g*255.0+0.5);\n"
            " highp vec2 f=mod(p,16.0);\n"
            " mediump float k;\n"
            " if(m.a>0.5) {\n"
            "  k=texture2D(page,(vec2(lo,hi)*16.0+f+0.5)*vec2(1.0/4096.0,params.x)).r;\n"
            " } else {\n"
            "  highp float h2=floor(hi*0.5);\n"
            "  highp vec2 qn=vec2((lo+256.0*(hi-2.0*h2))*8.0+floor(f.x*0.5),rows.x+h2*16.0+f.y);\n"
            "  highp float t=texture2D(page,(qn+0.5)*vec2(1.0/4096.0,params.x)).r;\n"
            "  highp float v=floor(t*255.0+0.5);\n"
            "  highp float nib=mix(mod(v,16.0),floor(v*0.0625),mod(f.x,2.0));\n"
            "  k=texture2D(page,(vec2(lo*16.0+nib,rows.y+hi)+0.5)*vec2(1.0/4096.0,params.x)).r;\n"
            " }\n"
            " mediump float x=%s;mediump float y=%s;\n"
            " return texture2D(palette,vec2(x,y));\n}\n",kx,ky);
        else o+=(size_t)snprintf(out+o,cap-o,
            "highp float t2_compact_axis(highp float v,highp float w) {\n"
            " highp float p=floor(v*4096.0);\n"
            " if(w>1.5){p=mod(p,8192.0);return p<4096.0?p:8191.0-p;}\n"
            " if(w>0.5)return mod(p,4096.0);\n"
            " return clamp(p,0.0,4095.0);\n}\n"
            "mediump vec4 t2_compact_sample(mediump sampler2D page,mediump sampler2D tilemap,mediump sampler2D palette,highp vec2 uv,highp vec4 params) {\n"
            " highp vec2 p=vec2(t2_compact_axis(uv.x,params.z),t2_compact_axis(uv.y,params.w));\n"
            " if(params.y>0.5)p.y=4095.0-p.y;\n"
            " mediump vec4 m=texture2D(tilemap,(floor(p/16.0)+0.5)/256.0);\n"
            " highp vec2 slot=floor(vec2(m.rg)*255.0+0.5);\n"
            " highp vec2 q=slot*16.0+mod(p,16.0)+0.5;\n"
            " mediump float k=texture2D(page,q*vec2(1.0/4096.0,params.x)).r;\n"
            " mediump float x=%s;mediump float y=%s;\n"
            " return texture2D(palette,vec2(x,y));\n}\n",kx,ky);
    } else if(mode==T2_PAGE_MODE_IDX8)o+=(size_t)snprintf(out+o,cap-o,
        "mediump vec4 t2_idx8_block_sample(mediump sampler2D page, mediump sampler2D blockmap, mediump sampler2D palette, %s vec2 uv) {\n"
        "    mediump float k = %s;\n"
        "    mediump float p = %s;\n"
        "    mediump float x = %s;\n"
        "    mediump float y = %s;\n"
        "    return texture2D(palette, vec2(x, y));\n"
        "}\n",precision,fk,fp,kx,ky);
    else o+=(size_t)snprintf(out+o,cap-o,
        "mediump vec4 t2_idx16_sample(mediump sampler2D page, mediump sampler2D palette, %s vec2 uv) {\n"
        "    mediump vec4 i = texture2D(page, uv);\n"
        "%s"
        "    mediump float x = %s;\n"
        "    mediump float y = %s;\n"
        "    return texture2D(palette, vec2(x, y));\n"
        "}\n",precision,t2_page_emulate_k256?"    i = floor(i * 255.0 + 0.5) / 256.0;\n":"",kx,ky);
    if(o+strlen(current+head)+1>cap){free(out);free(current);return NULL;}
    strcpy(out+o,current+head);
    free(current);
    return out;
}
/* Extra texture units of the variant: the real samplers take 0..samplers_total-1 (they grow
 * from 0 in the draw), the palettized sampler that is the pi-th set bit of the key takes
 * samplers_total + 2*pi for its palette and +1 for its block map (idx8 only). Mali-450 has 8
 * fragment units: the game's fragments have one sampler, so a page costs units 1 and 2. */
static inline unsigned t2_page_key_rank(unsigned key,unsigned j) {
    unsigned rank=0;for(unsigned i=0;i<j;i++)if(key&(1u<<i))rank++;return rank;
}
static inline unsigned t2_page_key_count(unsigned key){unsigned n=0;while(key){n+=key&1;key>>=1;}return n;}
static inline GLint t2_page_palette_unit(unsigned samplers_total,unsigned key,unsigned j){return (GLint)(samplers_total+2*t2_page_key_rank(key,j));}
static inline GLint t2_page_blockmap_unit(unsigned samplers_total,unsigned key,unsigned j){return (GLint)(samplers_total+2*t2_page_key_rank(key,j)+1);}
#endif /* T2_PAGE_SUBSTITUTION_H */

/* =============================================================================================
 * HOST SECTION — compiled once, from nvn_textures.h, after struct nvn_texture and
 * nvn_texture_copy_cpu are defined (T2_PAGE_HOST_IMPL).
 * ============================================================================================= */
#if defined(T2_PAGE_HOST_IMPL) && !defined(T2_PAGE_SUBSTITUTION_HOST_H)
#define T2_PAGE_SUBSTITUTION_HOST_H
#include <sys/mman.h>
#include <time.h>
#include "t2_texture_stream.h"
static struct {unsigned configured,mode,mirror,madvise;const char *dir;struct t2_page_manifest manifest;
    unsigned substituted,fallback,unmatched_logged,deferred_logged;uint64_t fingerprint_ns,fingerprints;} t2_pages;
static void t2_page_configure(void) {
    if(t2_pages.configured)return;
    t2_pages.configured=1;
    t2_pages.dir=getenv("T2_PAGES_DIR");
    t2_pages.mode=t2_page_mode_from_env();
    {const char *o=getenv("T2_PAGE_MIRROR");t2_pages.mirror=o&&!strcmp(o,"1");}
    {const char *o=getenv("T2_PAGE_MADVISE");t2_pages.madvise=o?(unsigned)strtoul(o,NULL,10):0;}
    if(t2_pages.mode && (!t2_pages.dir || !t2_page_manifest_load(t2_pages.dir,&t2_pages.manifest))) {
        printf("T2_PAGES dir=%s manifest=missing mode=off note=pages-stay-RGBA8\n",t2_pages.dir?t2_pages.dir:"-");
        t2_pages.mode=T2_PAGE_MODE_OFF;return;
    }
    printf("T2_PAGES dir=%s mode=%s entries=%u mirror=%u madvise=%u\n",t2_pages.dir?t2_pages.dir:"-",
        t2_pages.mode==T2_PAGE_MODE_IDX8?"idx8":t2_pages.mode==T2_PAGE_MODE_IDX16?"idx16":"off",t2_pages.manifest.count,t2_pages.mirror,t2_pages.madvise);
}
/* T2_PAGE_KEEP_RGBA8=TexturePack7,TexturePack9 (or 7,9): these pages stay RGBA8 (+47.7 MiB each over idx8),
 * the zero-risk lever for a page the device shows wrong. They are still identified (and logged). */
static unsigned t2_page_keep_rgba8(const char *name) {
    const char *o=getenv("T2_PAGE_KEEP_RGBA8");if(!o||!*o||!name)return 0;
    const char *num=name;while(*num && (*num<'0'||*num>'9'))num++;
    size_t nl=strlen(name),ul=strlen(num);
    for(const char *q=o;*q;) {
        const char *e=q;while(*e && *e!=',' && *e!=':')e++;
        size_t l=(size_t)(e-q);
        if((l==nl && !strncmp(q,name,l)) || (ul && l==ul && !strncmp(q,num,l)))return 1;
        q=*e?e+1:e;
    }
    return 0;
}
/* Eligibility at TextureInitialize: a plain single-level 2D RGBA8 image of at least 512x512
 * with the identity swizzle (the palette holds the bytes as Unity wrote them) and the default
 * layout. Render targets, linear-stride images and small textures keep the RGBA8 path. */
static unsigned t2_page_eligible(const struct nvn_texture_builder *b) {
    t2_page_configure();
    if(!t2_pages.mode)return 0;
    if(b->target!=1 || b->format!=0x25 || b->levels!=1 || b->depth!=1 || b->samples || b->packaged_layout || b->packaged || b->stride)return 0;
    if(b->flags&0x50 || b->gl_name)return 0;
    if(b->swizzle[0]!=2||b->swizzle[1]!=3||b->swizzle[2]!=4||b->swizzle[3]!=5)return 0;
    if(!t2_idx16_eligible(b->width,b->height,b->levels,1,0))return 0;
    /* Only a size the catalogue knows (4096x4096 here): a same-shaped render target (the 1920x1080
     * swapchain images have this exact builder shape, MEDIDO run build-20) never receives a copy and
     * must keep its storage from Initialize. */
    for(unsigned i=0;i<t2_pages.manifest.count;i++)if(t2_pages.manifest.entries[i].w==b->width && t2_pages.manifest.entries[i].h==b->height)return 1;
    return 0;
}
static void t2_page_release_staging(struct nvn_texture *t) {
    free(t->t2_page.staging);free(t->t2_page.rows_covered);
    t->t2_page.staging=NULL;t->t2_page.rows_covered=NULL;t->t2_page.rows_done=0;
}
static uint64_t t2_page_now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000ULL+t.tv_nsec;}
/* RGBA8 fallback for a deferred page: decode the pool bytes (or `src`, when the copy that
 * arrived is unmatched) and allocate the level as the plain path would. */
static void t2_page_materialize_rgba8(struct nvn_texture *t,const uint8_t *src,uint64_t stride,const char *reason) {
    if(!t->t2_page.deferred)return;
    unsigned width=t->b.width,height=t->b.height,bpp=nvn_texture_format_bytes(t->b.format);
    unsigned upload_type=nvn_cpu_image_upload_type(t),upload_bpp=nvn_texture_upload_bytes(upload_type);
    size_t bytes=(size_t)width*height*upload_bpp;uint8_t *rgba=malloc(bytes);
    if(!rgba)stop("T2_PAGE_FALLBACK_MEMORY",nvn_caller,bytes,21);
    /* A page half received: what arrived is in the staging copy (the pool was never written). */
    if(!src && t->t2_page.staging){src=t->t2_page.staging;stride=(uint64_t)width*4;}
    if(!src){stride=(uint64_t)width*bpp;src=nvn_pointer(t->pool->address+t->b.offset,stride*height,PROT_READ);}
    for(unsigned y=0;y<height;y++)if(!nvn_texture_decode_upload(t->b.format,t->b.swizzle,upload_type,rgba+(size_t)y*width*upload_bpp,src+y*stride,width))
        stop("T2_PAGE_FALLBACK_FORMAT",nvn_caller,t->b.format,20);
    GLint alignment;glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);
    glBindTexture(GL_TEXTURE_2D,t->name);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width,height,0,GL_RGBA,upload_type,rgba);
    glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);free(rgba);
    GLenum error=glGetError();if(error)stop("T2_PAGE_FALLBACK_GL",nvn_caller,error,21);
    t->t2_page.deferred=0;t->t2_page.mode=0;
    t->t2_budget_bytes=t->size;t2_texture_budget_add(t->size,nvn_caller);
    t2_pages.fallback++;
    printf("T2_PAGE_FALLBACK id=%"PRIu64" texture=%u size=%ux%u bytes=%"PRIu64" strips=%u rows-received=%u reason=%s\n",t->id,t->name,width,height,t->size,t->t2_page.strips,t->t2_page.rows_done,reason);
    t2_page_release_staging(t);
}
static void t2_page_release_gl(struct nvn_texture *t) {
    t->t2_page.pending_entry=NULL;
    t2_page_wrap_reset(&t->t2_page.blockmap_wrap);
    GLuint names[2]={t->t2_page.blockmap_name,t->t2_page.palette_name};
    if(names[0]||names[1])glDeleteTextures(2,names);
    t->t2_page.blockmap_name=t->t2_page.palette_name=0;
}
/* The CopyBufferToTexture interception. `src`/`stride` are the guest staging rows of the
 * box (validated by the caller). MEDIDO (run build-21): Unity does NOT hand the page over in
 * one copy — it arrives as full-width row strips, several CopyBufferToTexture per page. Each
 * strip is decoded into a transient host RGBA8 staging copy and a row bitmap; when every row
 * has arrived the page is fingerprinted from the staging copy and either substituted or
 * uploaded RGBA8 (unmatched). Returns 1 when the copy was consumed here. */
#include "t2_page_compact_runtime.h"
static unsigned t2_page_mode_for(const struct t2_page_entry *e) {  /* ROOT: per-page layout override */
    const char *list=getenv("T2_PAGE_LA_PAGES");
    size_t n=strlen(e->name);
    for(const char *q=list?list:"";*q;) {
        const char *end=strchr(q,',');size_t len=end?(size_t)(end-q):strlen(q);
        if(len==n && !strncmp(q,e->name,n))return T2_PAGE_MODE_IDX16;
        if(!end)break;
        q=end+1;
    }
    return t2_pages.mode==T2_PAGE_MODE_IDX8 && t2_compact_enabled()?T2_PAGE_MODE_COMPACT:t2_pages.mode;
}
static unsigned t2_page_stream_enabled(void) {
    static int enabled=-1;
    if(enabled<0) {
        const char *option=getenv("T2_PAGE_STREAM");
        if(option && strcmp(option,"0") && strcmp(option,"1"))stop("T2_PAGE_STREAM_OPTION",nvn_caller,0,24);
        enabled=option&&!strcmp(option,"1");
        if(enabled)printf("T2_PAGE_STREAM enabled=1 rows=%u file-order=forward index-staging=bounded resident-format=unchanged\n",T2_TEXTURE_STREAM_ROWS);
    }
    return (unsigned)enabled;
}
static unsigned t2_page_upload_stream(struct nvn_texture *t,const struct t2_page_entry *e,unsigned mode,
                                     const GLuint names[2],size_t *gl_bytes) {
    uint8_t *blockmap=NULL,*palette=NULL;unsigned channels=mode==T2_PAGE_MODE_IDX8?1:2;
    uint32_t bw=0,bh=0;char rel[96],path[1024];GLenum error=GL_NO_ERROR;unsigned ok=0;
    if(mode!=T2_PAGE_MODE_IDX8 && mode!=T2_PAGE_MODE_IDX16)return 0;
    if(!e->w || !e->h || e->w>4096 || e->h>4096)return 0;
    if(mode==T2_PAGE_MODE_IDX8) {
        /* Same catalogue checks as t2_page_load_idx8, before dividing by block. */
        if(!e->block || (e->block&(e->block-1)) || e->w%e->block || e->h%e->block || e->palettes>256)return 0;
        bw=e->w/e->block;bh=e->h/e->block;
        snprintf(rel,sizeof rel,"idx8block/%s.blockmap.l",e->name);
        blockmap=t2_page_read_file(t2_pages.dir,rel,(size_t)bw*bh,e->flipped,bw);
        snprintf(rel,sizeof rel,"idx8block/%s.pal256.rgba",e->name);
        palette=t2_page_read_file(t2_pages.dir,rel,T2_IDX16_PALETTE_BYTES,0,0);
        if(!blockmap || !palette)goto done;
    } else {
        snprintf(rel,sizeof rel,"idx16/%s.pal.rgba",e->name);
        palette=t2_page_read_file(t2_pages.dir,rel,T2_IDX16_PALETTE_BYTES,0,0);
        if(!palette)goto done;
    }
    if(snprintf(path,sizeof path,"%s/%s/%s.%s",t2_pages.dir,channels==1?"idx8block":"idx16",e->name,
                channels==1?"idx8.l":"idx16.la")>=(int)sizeof path)goto done;
    glBindTexture(GL_TEXTURE_2D,t->name);t2_idx16_parameters();
    if(!t2_texture_stream_gl(path,e->w,e->h,channels,e->flipped,&error))goto done;
    if(mode==T2_PAGE_MODE_IDX8) {
        glBindTexture(GL_TEXTURE_2D,names[0]);t2_idx16_parameters();
        glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE,(GLsizei)bw,(GLsizei)bh,0,GL_LUMINANCE,GL_UNSIGNED_BYTE,blockmap);
    }
    glBindTexture(GL_TEXTURE_2D,names[1]);t2_idx16_parameters();
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,T2_IDX16_PALETTE_SIDE,T2_IDX16_PALETTE_SIDE,0,GL_RGBA,GL_UNSIGNED_BYTE,palette);
    error=glGetError();if(error!=GL_NO_ERROR)goto done;
    if(mode==T2_PAGE_MODE_IDX16)glDeleteTextures(1,&names[0]);
    *gl_bytes=(size_t)e->w*e->h*channels+(size_t)bw*bh+T2_IDX16_PALETTE_BYTES;
    t->t2_page.blockmap_name=mode==T2_PAGE_MODE_IDX8?names[0]:0;t->t2_page.palette_name=names[1];
    size_t scratch=(size_t)e->w*channels*(e->h<T2_TEXTURE_STREAM_ROWS?e->h:T2_TEXTURE_STREAM_ROWS);
    size_t full=(size_t)e->w*e->h*channels;
    printf("T2_PAGE_STREAM_UPLOAD page=%s mode=%s channels=%u scratch-bytes=%zu full-index-bytes=%zu saved-temp-bytes=%zu rows=%u orient=%s file-order=forward\n",
        e->name,mode==T2_PAGE_MODE_IDX8?"idx8":"idx16",channels,scratch,full,full-scratch,
        T2_TEXTURE_STREAM_ROWS,e->flipped?"flipped":"png");
    ok=1;
done:
    free(blockmap);free(palette);
    if(!ok)printf("T2_PAGE_STREAM_FAILED page=%s gl-error=0x%x\n",e->name,error);
    return ok;
}
#include "t2_page_lazy.h"
#include "t2_page_cache.h"
#include "t2_page_residency.h" /* Frente B */
static unsigned t2_page_copy(struct nvn_texture *t,const uint32_t box[6],const uint8_t *src,uint64_t stride,unsigned level) {
    if(!t->t2_page.deferred && !t->t2_page.mode)return 0;
    unsigned w=t->b.width,h=t->b.height;
    uint32_t fullbox[6]={0,0,0,w,h,1};
    unsigned full=!level && !box[0] && !box[1] && box[3]==w && box[4]==h;
    if(t->t2_page.mode && !full)stop("T2_PAGE_PARTIAL_COPY_REQUIRED",nvn_caller,t->id,20);
    unsigned early=t->t2_page.early && t->t2_page.early_id==t->id && full; /* ROOT: no host 64 MiB strip staging */
    if(t->t2_page.deferred && !early) {
        if(level || box[0] || box[3]!=w) {
            printf("T2_PAGE_STRIP_SHAPE id=%"PRIu64" level=%u box=%u,%u,%ux%u note=not-a-row-strip\n",t->id,level,box[0],box[1],box[3],box[4]);
            t2_page_materialize_rgba8(t,NULL,0,"non-strip-copy");return 0;
        }
        if(!t->t2_page.staging) {
            t->t2_page.staging=malloc((size_t)w*h*4);t->t2_page.rows_covered=calloc(h/8+1,1);
            if(!t->t2_page.staging || !t->t2_page.rows_covered)stop("T2_PAGE_STAGING_MEMORY",nvn_caller,(uint64_t)w*h*4,21);
        }
        unsigned bpp=nvn_texture_format_bytes(t->b.format);(void)bpp;
        for(unsigned y=0;y<box[4];y++) {
            unsigned row=box[1]+y;
            if(!nvn_texture_decode_upload(t->b.format,t->b.swizzle,GL_UNSIGNED_BYTE,t->t2_page.staging+(size_t)row*w*4,src+y*stride,w))
                stop("T2_PAGE_STRIP_FORMAT",nvn_caller,t->b.format,20);
            if(!(t->t2_page.rows_covered[row/8]&(1u<<(row%8)))){t->t2_page.rows_covered[row/8]|=(uint8_t)(1u<<(row%8));t->t2_page.rows_done++;}
        }
        t->t2_page.strips++;
        static unsigned told;
        if(told++<12)printf("T2_PAGE_STRIP id=%"PRIu64" box=%u,%u,%ux%u stride=%"PRIu64" strips=%u rows-done=%u/%u\n",t->id,box[0],box[1],box[3],box[4],stride,t->t2_page.strips,t->t2_page.rows_done,h);
        if(t->t2_page.rows_done<h)return 1;
        /* complete: the whole page is in the staging copy */
        src=t->t2_page.staging;stride=(uint64_t)w*4;
        box=fullbox;level=0;
    }
    uint8_t fp[32];char hex[65]="early";
    uint64_t t0=t2_page_now(),t1=t0;const struct t2_page_entry *e;
    if(early) {
        /* ROOT: fingerprinted at record time and its staging source already released (zeros now). */
        e=t->t2_page.early;t->t2_page.early=NULL;
    } else {
        t2_page_fingerprint(src,stride,t->b.width,t->b.height,fp);t1=t2_page_now();
        t2_pages.fingerprint_ns+=t1-t0;t2_pages.fingerprints++;t2_page_hex(fp,hex);
        e=t2_page_manifest_find(&t2_pages.manifest,fp,t->b.width,t->b.height);
    }
    if(e && t2_page_keep_rgba8(e->name)) {
        if(early)stop("T2_PAGE_KEEP_RGBA8_EARLY",nvn_caller,t->id,24);   /* record_copy never marks kept pages early */
        printf("T2_PAGE_KEEP_RGBA8 page=%s id=%"PRIu64" note=forced-RGBA8\n",e->name,t->id);
        e=NULL;
    }
    if(!e) {
        if(t2_pages.unmatched_logged++<4)printf("T2_PAGE_UNMATCHED id=%"PRIu64" size=%ux%u fingerprint=%s fingerprint-ns=%"PRIu64" note=RGBA8\n",t->id,t->b.width,t->b.height,hex,t1-t0);
        if(t->t2_page.mode){t2_page_release_gl(t);t2_texture_budget_sub(t->t2_budget_bytes,nvn_caller);t->t2_budget_bytes=0;t->t2_page.mode=0;t->t2_page.deferred=1;}
        nvn_texture_copy_cpu(t,box,src,stride,level);
        t2_page_materialize_rgba8(t,src,stride,"unmatched");
        return 1;
    }
    unsigned pmode=t2_page_mode_for(e);
    /* A resident page stays resident on re-upload. Only first uploads (or
     * replacement of a still-cold catalogue entry) may be deferred. */
    unsigned lazy=t2_page_lazy_enabled() && (pmode==T2_PAGE_MODE_IDX8 || pmode==T2_PAGE_MODE_COMPACT) && !t->t2_budget_bytes;
    /* A second full copy into an already substituted page (the game re-uploading): replace. */
    if(t->t2_page.mode){t2_page_release_gl(t);t2_texture_budget_sub(t->t2_budget_bytes,nvn_caller);t->t2_budget_bytes=0;t->t2_page.mode=0;t->t2_page.deferred=1;}
    uint64_t t2=t2_page_now();
    size_t gl_bytes=0;
    if(lazy){t->t2_page.pending_entry=e;goto catalogue_ready;}
    GLint alignment;glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    while(glGetError()!=GL_NO_ERROR){}
    GLuint names[2]={0,0};glGenTextures(2,names);
    /* Historical opt-in workaround: T2_PAGE_LA_PAGES stores L = entry, A = block palette row
     * in one fetch (tools/textures/t2_idx8_to_la.py). Run 208's different-resolution rounding
     * explanation was a hypothesis. Run 518 proved mismatched index REPEAT / block-map CLAMP
     * reproduced the corrupted enemy; the draw path now propagates the same wrap to both. */
    if(pmode==T2_PAGE_MODE_COMPACT) {
        if(!t2_compact_upload(t,e,names,&gl_bytes)) {
            glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);glDeleteTextures(2,names);
            stop("T2_COMPACT_UPLOAD",nvn_caller,t->id,21);
        }
    } else if(t2_page_stream_enabled()) {
        GLint binding;glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
        if(!t2_page_upload_stream(t,e,pmode,names,&gl_bytes)) {
            glBindTexture(GL_TEXTURE_2D,(GLuint)binding);glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);
            glDeleteTextures(2,names);stop("T2_PAGE_STREAM_UPLOAD",nvn_caller,t->id,21);
        }
    } else if(pmode==T2_PAGE_MODE_IDX8) {
        uint8_t *index,*blockmap,*palette;uint32_t bw=e->w/e->block,bh=e->h/e->block;
        if(!t2_page_load_idx8(t2_pages.dir,e,&index,&blockmap,&palette))stop("T2_PAGE_CATALOGUE_FILES",nvn_caller,t->id,21);
        glBindTexture(GL_TEXTURE_2D,t->name);t2_idx16_parameters();
        glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE,(GLsizei)e->w,(GLsizei)e->h,0,GL_LUMINANCE,GL_UNSIGNED_BYTE,index);
        glBindTexture(GL_TEXTURE_2D,names[0]);t2_idx16_parameters();
        glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE,(GLsizei)bw,(GLsizei)bh,0,GL_LUMINANCE,GL_UNSIGNED_BYTE,blockmap);
        glBindTexture(GL_TEXTURE_2D,names[1]);t2_idx16_parameters();
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,(GLsizei)T2_IDX16_PALETTE_SIDE,(GLsizei)T2_IDX16_PALETTE_SIDE,0,GL_RGBA,GL_UNSIGNED_BYTE,palette);
        free(index);free(blockmap);free(palette);
        gl_bytes=(size_t)e->w*e->h+(size_t)bw*bh+T2_IDX16_PALETTE_BYTES;
        t->t2_page.blockmap_name=names[0];t->t2_page.palette_name=names[1];
    } else {
        uint8_t *index,*palette;
        if(!t2_page_load_idx16(t2_pages.dir,e,&index,&palette))stop("T2_PAGE_CATALOGUE_FILES",nvn_caller,t->id,21);
        glBindTexture(GL_TEXTURE_2D,t->name);t2_idx16_parameters();
        glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE_ALPHA,(GLsizei)e->w,(GLsizei)e->h,0,GL_LUMINANCE_ALPHA,GL_UNSIGNED_BYTE,index);
        glBindTexture(GL_TEXTURE_2D,names[1]);t2_idx16_parameters();
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,(GLsizei)T2_IDX16_PALETTE_SIDE,(GLsizei)T2_IDX16_PALETTE_SIDE,0,GL_RGBA,GL_UNSIGNED_BYTE,palette);
        free(index);free(palette);
        glDeleteTextures(1,&names[0]);
        gl_bytes=(size_t)e->w*e->h*2+T2_IDX16_PALETTE_BYTES;
        t->t2_page.blockmap_name=0;t->t2_page.palette_name=names[1];
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);
    GLenum error=glGetError();if(error)stop("T2_PAGE_UPLOAD_GL",nvn_caller,error,21);
catalogue_ready:
    /* Both upload paths set the index parameters directly. A full reupload must
     * invalidate the optional main-texture cache, too, before the next sampler. */
    t->parameter_known=0;
    t->t2_page.compact_height=pmode==T2_PAGE_MODE_COMPACT?t2_compact_find(t2_compact.entries,e)->height:0;
    t->t2_page.mode=pmode;t->t2_page.deferred=0;t->t2_page.flipped=e->flipped;
    t->t2_page.block=e->block;t->t2_page.palettes=e->palettes;t->t2_page.entries=e->entries;t->t2_page.gl_bytes=gl_bytes;
    strcpy(t->t2_page.name,e->name);
    t->t2_budget_bytes=gl_bytes;if(gl_bytes)t2_texture_budget_add(gl_bytes,nvn_caller);
    if(lazy)printf("T2_PAGE_LAZY_DEFER page=%s id=%"PRIu64" resident-bytes=0 source=catalogue until=first-sampler\n",e->name,t->id);
    t2_pages.substituted++;
    /* The plain path mirrors the RGBA8 rows into the guest pool (nvn_texture_copy_cpu): 64 MiB
     * of guest pages touched per page. Off by default here — the pool range stays untouched
     * and the CPU views/readback paths stop instead of reading zeros (T2_PAGE_CPU_READBACK). */
    if(t2_pages.mirror)nvn_texture_copy_cpu(t,box,src,stride,level);
    unsigned strips=t->t2_page.strips;t2_page_release_staging(t);
    if(t2_pages.madvise) {
        unsigned no_access=t->pool->flags&1u;
        if(t2_pages.madvise>=2 || no_access) {
            void *range=nvn_pointer(t->pool->address+t->b.offset,t->size,PROT_READ|PROT_WRITE);
            int rc=madvise(range,t->size,MADV_DONTNEED);
            printf("T2_PAGE_MADVISE id=%"PRIu64" pool-flags=0x%x bytes=%"PRIu64" rc=%d forced=%u\n",t->id,t->pool->flags,t->size,rc,t2_pages.madvise>=2);
        } else printf("T2_PAGE_MADVISE_REFUSED id=%"PRIu64" pool-flags=0x%x note=CPU-access-pool\n",t->id,t->pool->flags);
    }
    printf("T2_PAGE_SUBST page=%s id=%"PRIu64" texture=%u mode=%s bytes=%zu orient=%s block=%u palettes=%u entries=%u fingerprint=%.16s "
           "fingerprint-ns=%"PRIu64" load-ns=%"PRIu64" rgba8-bytes=%"PRIu64" pool-flags=0x%x mirror=%u strips=%u count=%u\n",
        e->name,t->id,t->name,t->t2_page.mode==T2_PAGE_MODE_COMPACT?"compact":t->t2_page.mode==T2_PAGE_MODE_IDX8?"idx8":"idx16",gl_bytes,e->flipped?"flipped":"png",e->block,e->palettes,e->entries,hex,
        t1-t0,t2_page_now()-t2,t->size,t->pool->flags,t2_pages.mirror,strips,t2_pages.substituted);
    return 1;
}
/* ROOT (MEDIDO device run 106): Unity 6 decodes several 4096x4096 pages inside ONE frame and records each
 * page's upload before the frame's submit; every page keeps a 64 MiB staging pool resident until the
 * replay, and the 916 MiB device dies at the 5th. At RECORD time the CPU has already written the staging
 * (Unity writes, then records the copy), so the page is identified here and its staging released now; the
 * replay then uploads the palettized catalogue without reading the source. T2_PAGE_EARLY=0 disables. */
static void t2_page_record_copy(struct nvn_texture *t,uint64_t source,const uint8_t *payload,unsigned has_view) {
    static int on=-1;if(on<0){const char *e=getenv("T2_PAGE_EARLY");on=!e||*e!='0';}
    if(!on || !t2_pages.mode || !t->t2_page.deferred || t->t2_page.early)return;
    uint32_t box[6];memcpy(box,payload,sizeof box);
    unsigned level=(has_view && (payload[24+7]&MOMO_VIEW_FLAG_LEVELS))?payload[24+4]:0;
    unsigned w=t->b.width,h=t->b.height;
    if(level || box[0] || box[1] || box[2] || box[3]!=w || box[4]!=h || nvn_texture_format_bytes(t->b.format)!=4)return;
    uint64_t stride=(uint64_t)w*4,bytes=stride*h;
    const uint8_t *src=nvn_gpu_pointer(source,bytes,PROT_READ);
    uint8_t fp[32];t2_page_fingerprint(src,stride,w,h,fp);
    const struct t2_page_entry *e=t2_page_manifest_find(&t2_pages.manifest,fp,w,h);
    char hex[65];t2_page_hex(fp,hex);
    if(!e){printf("T2_PAGE_EARLY id=%"PRIu64" fingerprint=%.16s result=unmatched note=replay-decides\n",t->id,hex);return;}
    if(t2_page_keep_rgba8(e->name)){printf("T2_PAGE_EARLY id=%"PRIu64" page=%s result=keep-RGBA8 staging=kept\n",t->id,e->name);return;}
    t->t2_page.early=e;t->t2_page.early_id=t->id;
    /* ROOT (MEDIDO device runs 115/118): two 64 MiB decoded-PNG buffers can be alive at once and the
     * load peak varies run to run. The decoded buffer of THIS page is found among the last guest maps of
     * 64 MiB + 4 KiB and released only when a 64-row sample proves it holds exactly these pixels (either
     * row order, header 0 or 4 KiB). Unity already copied it into the staging being recorded now and
     * unmaps it itself shortly after. T2_PAGE_DECODE_DROP=0 disables. */
    static int ddrop=-1;if(ddrop<0){const char *o=getenv("T2_PAGE_DECODE_DROP");ddrop=o&&*o=='1';} /* MEDIDO run 121: the decoded buffer is NOT a byte copy of the staging (other channel order/layout): off by default */
    for(unsigned k=0;ddrop && k<4 && k<t2_decode_next;k++) {
        uint64_t base=t2_decode_maps[(t2_decode_next-1-k)%4];if(!base)continue;
        int found=-1;
        for(int variant=0;variant<4 && found<0;variant++) {
            uint64_t off=(variant&1)?4096:0;unsigned flip=variant>>1,ok=1,nonzero=0;
            const uint8_t *d=nvn_pointer(base,0x4001000,PROT_READ);
            for(unsigned r=0;r<64 && ok;r++) {
                unsigned row=r*64+7,col=(r*131)%(unsigned)(stride-64),drow=flip?h-1-row:row;
                const uint8_t *a=src+(uint64_t)row*stride+col,*b=d+off+(uint64_t)drow*stride+col;
                if(memcmp(a,b,64))ok=0;
                for(unsigned q=0;q<64;q++)nonzero|=a[q];
            }
            if(ok && nonzero)found=variant;
        }
        if(found>=0) {
            int rc2=madvise(nvn_pointer(base,0x4001000,PROT_READ),0x4001000,MADV_DONTNEED);
            printf("T2_PAGE_DECODE_DROP id=%"PRIu64" page=%s decoded=0x%"PRIx64" layout=%s,%s rc=%d\n",t->id,e->name,base,(found&1)?"hdr4k":"hdr0",(found>>1)?"flipped":"same",rc2);
            t2_decode_maps[(t2_decode_next-1-k)%4]=0;break;
        }
        if(k==3 || k+1==t2_decode_next)printf("T2_PAGE_DECODE_DROP id=%"PRIu64" page=%s result=no-proven-match note=kept\n",t->id,e->name);
    }
    uintptr_t lo=((uintptr_t)src+4095)&~(uintptr_t)4095,hi=((uintptr_t)src+bytes)&~(uintptr_t)4095;
    int rc=hi>lo?madvise((void *)lo,hi-lo,MADV_DONTNEED):-1;
    printf("T2_PAGE_EARLY id=%"PRIu64" page=%s fingerprint=%.16s staging-dropped=%zu rc=%d\n",t->id,e->name,hex,(size_t)(hi>lo?hi-lo:0),rc);
}
/* Runtime calibration of the palette-coordinate formula ON THE RUNNING GPU (Frente F, 25/09 evening).
 * Device run 117/202 (Mali-450) showed whole blocks with the wrong palette and glyph texels turning
 * transparent while every PC check (llvmpipe, where mediump is fp32) is exact. The coordinate maths
 * depend on how the texture unit returns an L8 texel (k/255, k/256, ...) and on the fp16 ALU, which
 * no PC run can measure. So the host measures it: a 256x256 probe (index = x, block palette = y,
 * identity palette) is drawn with each candidate formula through the SAME generated variant text, read
 * back, and the first formula that selects all 65536 (k,p) exactly is used for every variant.
 * If none is exact, pages are not trusted: T2_PAGE_CALIBRATION_FAILED and the best one is kept (logged).
 * GL state touched here is saved and restored; the caller invalidates the host's state cache. */
static const char *t2_page_probe_vs="#version 100\nattribute vec2 in_pos;\nattribute vec2 in_uv;\nvarying mediump vec4 ms_vary0;\n"
    "void main(){ gl_Position=vec4(in_pos,0.0,1.0); ms_vary0=vec4(in_uv,0.0,1.0); }\n";
static const char *t2_page_probe_fs="#version 100\nprecision mediump float;\n"
    "uniform sampler2D fs_tex2_168;\n"
    "vec4 ms_sample_fs_tex2_168(vec2 p) {vec4 c=texture2D(fs_tex2_168,p);return c;}\n"
    "varying mediump vec4 ms_vary0;\nvoid main() { gl_FragColor = ms_sample_fs_tex2_168(ms_vary0.xy); }\n";
static GLuint t2_page_probe_program(unsigned *ok) {
    const char *names[1]={"fs_tex2_168"};*ok=0;
    char *fs=t2_page_fragment_source(t2_page_probe_fs,names,1,1u,T2_PAGE_MODE_IDX8,1);if(!fs)return 0;
    GLuint p=glCreateProgram(),sh[2];const char *src[2]={t2_page_probe_vs,fs};GLint status=0;
    for(unsigned i=0;i<2;i++){sh[i]=glCreateShader(i?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);glShaderSource(sh[i],1,&src[i],NULL);glCompileShader(sh[i]);
        glGetShaderiv(sh[i],GL_COMPILE_STATUS,&status);if(!status){char log[1024]={0};glGetShaderInfoLog(sh[i],sizeof log,NULL,log);printf("T2_PAGE_CALIBRATE_COMPILE %s\n",log);}
        glAttachShader(p,sh[i]);}
    glBindAttribLocation(p,0,"in_pos");glBindAttribLocation(p,1,"in_uv");glLinkProgram(p);glGetProgramiv(p,GL_LINK_STATUS,&status);
    for(unsigned i=0;i<2;i++){glDetachShader(p,sh[i]);glDeleteShader(sh[i]);}
    free(fs);*ok=status!=0;return p;
}
static void t2_page_calibrate(void) {
    static unsigned done;if(done)return;done=1;
    {const char *o=getenv("T2_PAGE_EMULATE_K256");t2_page_emulate_k256=o&&!strcmp(o,"1");}
    const char *forced=getenv("T2_PAGE_FORMULA");
    if(forced){t2_page_formula=(unsigned)strtoul(forced,NULL,10)%3;printf("T2_PAGE_CALIBRATE forced-formula=%u emulate-k256=%u\n",t2_page_formula,t2_page_emulate_k256);return;}
    /* save */
    GLint fbo,viewport[4],program,active,bound[3],abuf,align,mask_c[4];GLboolean cmask[4];
    GLint aen[2],asize[2],atype[2],anorm[2],astride[2],abind[2];void *aptr[2];
    const GLenum caps[6]={GL_BLEND,GL_DEPTH_TEST,GL_STENCIL_TEST,GL_SCISSOR_TEST,GL_CULL_FACE,GL_DITHER};GLboolean capon[6];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fbo);glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_CURRENT_PROGRAM,&program);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&abuf);glGetIntegerv(GL_UNPACK_ALIGNMENT,&align);
    glGetBooleanv(GL_COLOR_WRITEMASK,cmask);(void)mask_c;
    for(unsigned i=0;i<3;i++){glActiveTexture(GL_TEXTURE0+i);glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound[i]);}
    for(unsigned i=0;i<2;i++){glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&aen[i]);glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_SIZE,&asize[i]);
        glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_TYPE,&atype[i]);glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,&anorm[i]);
        glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&astride[i]);glGetVertexAttribiv(i,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&abind[i]);
        glGetVertexAttribPointerv(i,GL_VERTEX_ATTRIB_ARRAY_POINTER,&aptr[i]);}
    for(unsigned i=0;i<6;i++)capon[i]=glIsEnabled(caps[i]);
    /* probe resources */
    uint8_t *ix=malloc(65536),*iy=malloc(65536),*id=malloc(65536*4),*out=malloc(65536*4);
    if(!ix||!iy||!id||!out)stop("T2_PAGE_CALIBRATE_MEMORY",nvn_caller,0,21);
    for(unsigned y=0;y<256;y++)for(unsigned x=0;x<256;x++){ix[y*256+x]=(uint8_t)x;iy[y*256+x]=(uint8_t)y;
        uint8_t *q=id+(y*256+x)*4;q[0]=(uint8_t)x;q[1]=(uint8_t)y;q[2]=0;q[3]=255;}
    GLuint tex[4];glGenTextures(4,tex);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    const GLenum fmt[4]={GL_LUMINANCE,GL_LUMINANCE,GL_RGBA,GL_RGBA};const uint8_t *data[4]={ix,iy,id,NULL};
    for(unsigned i=0;i<4;i++){glBindTexture(GL_TEXTURE_2D,tex[i]);t2_idx16_parameters();glTexImage2D(GL_TEXTURE_2D,0,fmt[i],256,256,0,fmt[i],GL_UNSIGNED_BYTE,data[i]);}
    GLuint fb;glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex[3],0);
    unsigned complete=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    glViewport(0,0,256,256);for(unsigned i=0;i<6;i++)glDisable(caps[i]);glColorMask(1,1,1,1);glBindBuffer(GL_ARRAY_BUFFER,0);
    static const float quad[]={-1,-1,0,0, 1,-1,1,0, -1,1,0,1, 1,1,1,1};
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,16,quad);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,16,quad+2);
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
    const unsigned order[3]={0,2,1};unsigned chosen=UINT32_MAX,best=0,best_bad=UINT32_MAX;
    for(unsigned c=0;c<3 && complete;c++) {
        t2_page_formula=order[c];unsigned ok;GLuint prog=t2_page_probe_program(&ok);
        unsigned bad_k=65536,bad_p=65536;int first_k=-2,first_p=-2;
        if(ok) {
            glUseProgram(prog);
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,tex[0]);glUniform1i(glGetUniformLocation(prog,"fs_tex2_168"),0);
            glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,tex[2]);glUniform1i(glGetUniformLocation(prog,"t2_palette_fs_tex2_168"),1);
            glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,tex[1]);glUniform1i(glGetUniformLocation(prog,"t2_blockmap_fs_tex2_168"),2);
            glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
            glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,out);
            bad_k=bad_p=0;first_k=first_p=-1;
            for(unsigned y=0;y<256;y++)for(unsigned x=0;x<256;x++){const uint8_t *o=out+(y*256+x)*4;
                if(o[0]!=x){bad_k++;if(first_k<0)first_k=(int)x;} if(o[1]!=y){bad_p++;if(first_p<0)first_p=(int)y;}}
        }
        if(prog)glDeleteProgram(prog);
        printf("T2_PAGE_CALIBRATE formula=%u emulate-k256=%u compiled=%u index-wrong=%u row-wrong=%u first-bad-k=%d first-bad-p=%d\n",
            t2_page_formula,t2_page_emulate_k256,ok,bad_k,bad_p,first_k,first_p);
        if(bad_k+bad_p<best_bad){best_bad=bad_k+bad_p;best=t2_page_formula;}
        if(!bad_k && !bad_p){chosen=t2_page_formula;break;}
    }
    t2_page_formula=chosen!=UINT32_MAX?chosen:best;
    if(chosen==UINT32_MAX)printf("T2_PAGE_CALIBRATION_FAILED fbo-complete=%u best-formula=%u wrong=%u note=palettized-pages-not-exact-on-this-GPU\n",complete,best,best_bad);
    else printf("T2_PAGE_CALIBRATED formula=%u renderer=%s\n",chosen,(const char *)glGetString(GL_RENDERER));
    /* restore */
    glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)fbo);glDeleteFramebuffers(1,&fb);glDeleteTextures(4,tex);free(ix);free(iy);free(id);free(out);
    glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);glUseProgram((GLuint)program);glPixelStorei(GL_UNPACK_ALIGNMENT,align);
    glColorMask(cmask[0],cmask[1],cmask[2],cmask[3]);
    for(unsigned i=0;i<6;i++)if(capon[i])glEnable(caps[i]);
    for(unsigned i=0;i<2;i++){glBindBuffer(GL_ARRAY_BUFFER,(GLuint)abind[i]);glVertexAttribPointer(i,asize[i],(GLenum)atype[i],(GLboolean)anorm[i],astride[i],aptr[i]);
        if(aen[i])glEnableVertexAttribArray(i);else glDisableVertexAttribArray(i);}
    glBindBuffer(GL_ARRAY_BUFFER,(GLuint)abuf);
    for(unsigned i=0;i<3;i++){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,(GLuint)bound[i]);}
    glActiveTexture((GLenum)active);
    GLenum error=glGetError();if(error)printf("T2_PAGE_CALIBRATE_GL_ERROR 0x%x\n",error);
}
static void t2_page_finalize(struct nvn_texture *t) {
    t2_page_release_gl(t);t2_page_release_staging(t);
    memset(&t->t2_page,0,sizeof t->t2_page);
}
/* Sampler binding: a page has one level and holds indices, so it is NEAREST whatever the
 * game's sampler says (a filtered index is a colour that is not in the palette). */
static void t2_page_sampler_filter(struct nvn_texture *t,GLint *min,GLint *mag) {
    if(!t->t2_page.mode)return;
    if((*min!=GL_NEAREST || *mag!=GL_NEAREST) && !t->t2_page.linear_logged++)
        printf("T2_PAGE_LINEAR_REQUEST page=%s id=%"PRIu64" min=0x%x mag=0x%x forced=NEAREST\n",t->t2_page.name,t->id,(unsigned)*min,(unsigned)*mag);
    *min=*mag=GL_NEAREST;
}
static void t2_page_require_rgba8(const struct nvn_texture *t,const char *what) {
    if(t && t->t2_page.mode)stop(what,nvn_caller,t->id,20);
}
#endif /* T2_PAGE_HOST_IMPL */
