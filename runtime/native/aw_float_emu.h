/* Animal Well fluid simulation on Mali-450: the GPU has no float/half texture
 * backend at all (no GL_OES_texture_half_float, fragment math is fp16). The
 * five 320x180 RG16F/RGBA16F fluid textures are stored as RGBA8:
 *   kind 2 (RG16F)  : signed range [-R,R] as 8.8 fixed per component, (hi_x,lo_x,hi_y,lo_y);
 *                     decode is linear in the stored channels, so hardware bilinear
 *                     filtering of the encoded texels equals filtering of the values.
 *   kind 4 (RGBA16F): plain RGBA8 (the dye is [0,1]: FluidRender quantizes it to eighths).
 * Fragment programs sampling or writing such textures are re-linked as variants
 * (nvn_program.aw_float_key) whose source decodes on texture2D and encodes the output.
 * Blending into a kind-2 target and CPU copies/readbacks stay fail-closed. */
#ifndef AW_FLOAT_EMU_H
#define AW_FLOAT_EMU_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static inline unsigned aw_float_kind_of_format(unsigned format){return format==0x11?2U:format==0x29?4U:0U;}
static inline float aw_float_range(void) {
    static float range;static int configured;
    if(!configured){const char *o=getenv("AW_FLOAT_RANGE");range=o?(float)atof(o):64.0f;if(range<=0)range=64.0f;configured=1;
        printf("AW_FLOAT_EMU enabled=1 rg16f=rgba8-8.8-signed range=%g rgba16f=rgba8 blend-on-rg=refused\n",range);}
    return range;
}
/* key layout: bits 2j..2j+1 = kind code of fragment sampler j (0 none,1 kind2,2 kind4); bits 16..17 = target kind code. */
static inline unsigned aw_float_code(unsigned kind){return kind==2?1U:kind==4?2U:0U;}
static inline unsigned aw_float_kind_from_code(unsigned code){return code==1?2U:code==2?4U:0U;}
static inline void aw_float_encode_color(float color[4]) {
    float r=aw_float_range(),out[4];
    for(unsigned i=0;i<2;i++) {
        float n=color[i]/(2.0f*r)+0.5f;if(n<0)n=0;if(n>1)n=1;
        float h=floorf(n*255.0f),l=n*255.0f-h;
        out[i*2]=h/255.0f;out[i*2+1]=l;
    }
    memcpy(color,out,16);
}
static inline char *aw_float_source(const char *source,const char *const names[],const unsigned kinds[],unsigned count,unsigned out_kind,unsigned medium) {
    if(!source || strncmp(source,"#version 100\n",13) || count>8)return NULL;
    for(unsigned i=0;i<count;i++) {
        if(!names[i] || !*names[i] || strlen(names[i])>=64)return NULL;
        for(const char *s=names[i];*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='_'))return NULL;
    }
    size_t capacity=strlen(source)*2+8192;char *out=malloc(capacity);if(!out)return NULL;
    const char *precision=medium?"mediump":"highp";
    size_t at=(size_t)snprintf(out,capacity,"#version 100\n");
    at+=(size_t)snprintf(out+at,capacity-at,"uniform %s float aw_float_range;\n",precision);
    at+=(size_t)snprintf(out+at,capacity-at,
        "%s vec4 aw_dec2(sampler2D s,%s vec2 uv){\n %s vec4 t=texture2D(s,uv);\n"
        " return vec4(((t.x*255.0+t.y)/255.0-0.5)*(2.0*aw_float_range),((t.z*255.0+t.w)/255.0-0.5)*(2.0*aw_float_range),0.0,1.0);\n}\n",precision,precision,precision);
    if(out_kind==2)at+=(size_t)snprintf(out+at,capacity-at,
        "%s vec4 aw_out;\n%s vec4 aw_enc2(%s vec4 v){\n %s vec2 n=clamp(v.xy/(2.0*aw_float_range)+0.5,0.0,1.0);\n"
        " %s vec2 h=floor(n*255.0);\n %s vec2 l=n*255.0-h;\n return vec4(h.x/255.0,l.x,h.y/255.0,l.y);\n}\n",precision,precision,precision,precision,precision,precision);
    unsigned main_seen=0;
    for(const char *s=source+13;*s;) {
        if(!strncmp(s,"texture2D(",10)) {
            unsigned matched=0;
            for(unsigned i=0;i<count && !matched;i++) {
                if(kinds[i]!=2)continue;
                const char *name=s+10;while(*name==' '||*name=='\t')name++;
                size_t n=strlen(names[i]);if(strncmp(name,names[i],n))continue;
                const char *comma=name+n;while(*comma==' '||*comma=='\t')comma++;
                if(*comma!=',')continue;
                memcpy(out+at,"aw_dec2(",8);at+=8;s+=10;matched=1;
            }
            if(matched)continue;
        }
        if(out_kind==2 && !strncmp(s,"gl_FragData[0]",14)){memcpy(out+at,"aw_out",6);at+=6;s+=14;continue;}
        if(out_kind==2 && !strncmp(s,"gl_FragColor",12)){memcpy(out+at,"aw_out",6);at+=6;s+=12;continue;}
        if(out_kind==2 && !strncmp(s,"void main()",11)){if(main_seen){free(out);return NULL;}main_seen=1;memcpy(out+at,"void aw_main()",14);at+=14;s+=11;continue;}
        if(at+64>=capacity){free(out);return NULL;}
        out[at++]=*s++;
    }
    if(out_kind==2) {
        if(!main_seen){free(out);return NULL;}
        at+=(size_t)snprintf(out+at,capacity-at,"void main(){\n aw_out=vec4(0.0,0.0,0.0,1.0);\n aw_main();\n gl_FragData[0]=aw_enc2(aw_out);\n}\n");
    }
    out[at]=0;return out;
}
#endif
