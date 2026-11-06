/* GLES2 storage must be complete. A same-sized 0/1 mip mask lets the GPU's
 * own LOD/filter calculation detect selection of missing tail levels.
 * Any such selection clamps to the original last supplied mip.
 * No CPU LOD estimate, generated game mip, or texture-coordinate change. */
#ifndef ZERO_MIP_CLAMP_SHADER_H
#define ZERO_MIP_CLAMP_SHADER_H
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static char *zero_mip_clamp_source(const char *source,const char *const names[],unsigned count,unsigned medium) {
    if(!source || strncmp(source,"#version 100\n",13) || count>8)return NULL;
    for(unsigned i=0;i<count;i++) {
        if(!names[i] || !*names[i] || strlen(names[i])>=64)return NULL;
        for(const char *s=names[i];*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='_'))return NULL;
    }
    size_t capacity=strlen(source)*8+8192;char *out=malloc(capacity);if(!out)return NULL;
    const char *p=medium?"mediump":"highp";
    size_t at=(size_t)snprintf(out,capacity,"#version 100\n#extension GL_EXT_shader_texture_lod : require\n");
    for(unsigned i=0;i<count;i++)at+=(size_t)snprintf(out+at,capacity-at,
        "uniform %s sampler2D zero_mip_mask_%u;\nuniform %s float zero_mip_last_%u;\n",p,i,p,i);
    at+=(size_t)snprintf(out+at,capacity-at,
        "%s vec4 zero_mip_sample(%s sampler2D src,%s sampler2D mask,%s float last,%s vec2 uv){\n"
        " %s vec4 base=texture2D(src,uv);\n if(last<0.0)return base;\n"
        " %s float missing=texture2D(mask,uv).r;\n"
        " return missing>0.0?texture2DLodEXT(src,uv,last):base;\n}\n",p,p,p,p,p,p,p);
    /* A biased sample keeps its bias: Unity's translated fragments call texture2D(s,uv,bias) through
     * their swizzle helper, and rewriting only the two-argument form left the call unmatched
     * (S0001 "No matching overload for zero_mip_sample", MEDIDO run momo-099). */
    at+=(size_t)snprintf(out+at,capacity-at,
        "%s vec4 zero_mip_sample(%s sampler2D src,%s sampler2D mask,%s float last,%s vec2 uv,%s float bias){\n"
        " %s vec4 base=texture2D(src,uv,bias);\n if(last<0.0)return base;\n"
        " %s float missing=texture2D(mask,uv,bias).r;\n"
        " return missing>0.0?texture2DLodEXT(src,uv,last):base;\n}\n",p,p,p,p,p,p,p,p);
    for(const char *s=source+13;*s;) {
        unsigned matched=0;
        if(!strncmp(s,"texture2D(",10))for(unsigned i=0;i<count;i++) {
            const char *name=s+10;while(*name==' '||*name=='\t')name++;
            size_t n=strlen(names[i]);
            if(strncmp(name,names[i],n))continue;
            const char *comma=name+n;while(*comma==' '||*comma=='\t')comma++;
            if(*comma!=',')continue;
            at+=(size_t)snprintf(out+at,capacity-at,"zero_mip_sample(%s,zero_mip_mask_%u,zero_mip_last_%u,",names[i],i,i);
            s=comma+1;matched=1;break;
        }
        if(!matched)out[at++]=*s++;
    }
    out[at]=0;return out;
}
#endif
