/* R8 GPU storage is canonical (R,0,0,1). Apply the native texture swizzle
 * after sampling, so framebuffer writes and destination-alpha blending keep
 * the semantics of an attachment without G/B/A channels. */
#ifndef ZERO_R8_SHADER_H
#define ZERO_R8_SHADER_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *zero_r8_source(const char *source,const char *const names[],unsigned count,unsigned medium) {
    /* ESSL100 titles emit `texture2D`, ESSL310 `texture`; the wrapper only changes the sample
     * result, so both version lines are carried over verbatim and the body scan starts after it.
     * shortcut: textureLod/proj calls are left unwrapped; only plain texture/2D samples are. */
    static const char version100[]="#version 100\n",version310[]="#version 310 es\n";
    size_t header_len;
    if(!source)return NULL;
    if(!strncmp(source,version100,sizeof version100-1))header_len=sizeof version100-1;
    else if(!strncmp(source,version310,sizeof version310-1))header_len=sizeof version310-1;
    else return NULL;
    if(count>8)return NULL;
    for(unsigned i=0;i<count;i++) {
        if(!names[i] || !*names[i] || strlen(names[i])>=64)return NULL;
        for(const char *s=names[i];*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='_'))return NULL;
    }
    size_t capacity=strlen(source)*8+4096;char *out=malloc(capacity);if(!out)return NULL;
    const char *precision=medium?"mediump":"highp";
    /* #extension must stay ahead of every declaration, so the helper lands after the leading
     * preprocessor block instead of directly under #version. */
    size_t insert_at=header_len;
    for(;;) {
        const char *line=source+insert_at,*nl=strchr(line,'\n');
        if(!nl)break;
        size_t len=(size_t)(nl-line);
        if(len && line[0]!='#')break;
        insert_at+=(size_t)(nl-source)+1-insert_at;
    }
    memcpy(out,source,insert_at);size_t at=insert_at;
    for(unsigned i=0;i<count;i++)at+=(size_t)snprintf(out+at,capacity-at,
        "uniform %s vec4 zero_r8_mul_%u;\nuniform %s vec4 zero_r8_add_%u;\nuniform %s float zero_r8_use_%u;\n",
        precision,i,precision,i,precision,i);
    /* The selector arithmetic is done on the CPU, once per draw, and arrives as two vectors: an R8 image
     * has no G/B (they read ZERO) and no A (it reads ONE), so every NVN selector is either "take R" or a
     * constant, and the whole unswizzle is mul*v.r + add. `use` is 0 for anything that is not an R8 image
     * and mix() then returns the sample byte for byte.
     * Why it matters: the shape this replaced asked the question per channel INSIDE the fragment, and on
     * this driver that cost 163 ms of a 606 ms frame (MEDIDO, runs momo-190 against momo-196) because
     * every texture2D of every shader pays it - the post-processing Uber pass samples fifteen times. */
    at+=(size_t)snprintf(out+at,capacity-at,
        "%s vec4 zero_r8_channels(%s vec4 v,%s vec4 m,%s vec4 a,%s float use){\n"
        " return mix(v,m*v.r+a,use);\n}\n",precision,precision,precision,precision,precision);
    struct {const char *end;unsigned sampler;} pending[32];unsigned depth=0;
    for(const char *s=source+insert_at;*s;s++) {
        size_t sample_len=!strncmp(s,"texture2D(",10)?10:(!strncmp(s,"texture(",8)?8:0);
        if(sample_len)for(unsigned i=0;i<count;i++) {
            const char *name=s+sample_len;while(*name==' '||*name=='\t')name++;
            size_t n=strlen(names[i]);if(strncmp(name,names[i],n))continue;
            const char *comma=name+n;while(*comma==' '||*comma=='\t')comma++;
            if(*comma!=',')continue;
            unsigned brackets=1;const char *end=s+sample_len;
            while(*end && brackets){if(*end=='(')brackets++;if(*end==')')brackets--;if(brackets)end++;}
            if(brackets || depth==32){free(out);return NULL;}
            pending[depth].end=end;pending[depth++].sampler=i;
            memcpy(out+at,"zero_r8_channels(",17);at+=17;break;
        }
        out[at++]=*s;
        if(depth && s==pending[depth-1].end) {
            at+=(size_t)snprintf(out+at,capacity-at,",zero_r8_mul_%u,zero_r8_add_%u,zero_r8_use_%u)",pending[depth-1].sampler,pending[depth-1].sampler,pending[depth-1].sampler),depth--;
        }
    }
    if(depth){free(out);return NULL;}
    out[at]=0;return out;
}
#endif
