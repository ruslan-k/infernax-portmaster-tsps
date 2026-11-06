/* Adapter-only clip convention after the pinned translated vertex program.
 * x: +1 lower-left, -1 upper-left; y: 0 [-w,+w] Z, 1 [0,+w] Z.
 * The renderer validates these two enums and uploads before every draw.
 * Guest sources, varyings, UVs, native pairing and storage are untouched.
 */
#ifndef NVN_MEGAMAN_CLIP_CONTROL_H
#define NVN_MEGAMAN_CLIP_CONTROL_H
#include <stdlib.h>
#include <string.h>
static inline char *nvn_clip_vertex_source(const char *source) {
    static const char renamed[]="void nvnOriginalVertexMain()";
    static const char wrapper[]=
        "\nuniform highp vec2 nvnClipControl;\n"
        "void main(){nvnOriginalVertexMain();gl_Position.y*=nvnClipControl.x;"
        "if(nvnClipControl.y>0.5)gl_Position.z=2.0*gl_Position.z-gl_Position.w;}\n";
    if(!source)return NULL;
    static const char token[]="void main()";
    size_t size=strlen(source);if(size>32768 || strstr(source,"nvnClipControl") || strstr(source,"nvnOriginalVertexMain"))return NULL;
    const char *at=strstr(source,token);if(!at)return NULL;
    /* The generated GLSL may put the body brace on the next line, so only the token must be exact. */
    const char *brace=at+sizeof(token)-1;
    while(*brace==' '||*brace=='\t'||*brace=='\r'||*brace=='\n')brace++;
    if(*brace!='{' || strstr(brace,token))return NULL;
    size_t prefix=(size_t)(at-source),suffix=size-prefix-(sizeof(token)-1);
    size_t total=prefix+sizeof(renamed)-1+suffix+sizeof(wrapper);
    char *result=malloc(total);if(!result)return NULL;
    memcpy(result,source,prefix);memcpy(result+prefix,renamed,sizeof(renamed)-1);
    memcpy(result+prefix+sizeof(renamed)-1,at+sizeof(token)-1,suffix);
    memcpy(result+prefix+sizeof(renamed)-1+suffix,wrapper,sizeof(wrapper));
    return result;
}
#endif
