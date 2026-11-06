/* Pure decode of the pinned LC2 SDK depth/stencil state and dynamic commands.
 * No GL calls or implicit dynamic defaults. See docs/megaman-stencil.md.
 */
#ifndef MEGAMAN_STENCIL_H
#define MEGAMAN_STENCIL_H
#include <stdint.h>

struct megaman_stencil_face {
    unsigned compare,stencil_fail,depth_fail,depth_pass;
};
struct megaman_depth_stencil {
    unsigned depth_test,depth_write,depth_compare,stencil_test;
    struct megaman_stencil_face face[2]; /* 0 front, 1 back */
};
struct megaman_stencil_dynamic {
    uint8_t reference[2],value_mask[2],write_mask[2];
};
enum megaman_stencil_dynamic_command {
    MEGAMAN_STENCIL_REFERENCE,
    MEGAMAN_STENCIL_VALUE_MASK,
    MEGAMAN_STENCIL_WRITE_MASK
};

static inline int megaman_stencil_enum_valid(unsigned value){return value>=1&&value<=8;}

static inline int megaman_stencil_decode(const uint32_t words[2],struct megaman_depth_stencil *out){
    if(!words||!out)return 0;
    struct megaman_depth_stencil d={0};
    d.depth_test=words[0]&1;d.depth_write=(words[0]>>1)&1;
    d.stencil_test=(words[0]>>2)&1;d.depth_compare=(words[0]>>4)&15;
    if(d.depth_test&&!megaman_stencil_enum_valid(d.depth_compare))return 0;
    for(unsigned i=0;i<2;i++){
        unsigned value=words[1]>>(16*i);
        d.face[i].compare=value&15;
        d.face[i].stencil_fail=(value>>4)&15;
        d.face[i].depth_fail=(value>>12)&15;
        d.face[i].depth_pass=(value>>8)&15;
        if(d.stencil_test&&(!megaman_stencil_enum_valid(d.face[i].compare)||
           !megaman_stencil_enum_valid(d.face[i].stencil_fail)||
           !megaman_stencil_enum_valid(d.face[i].depth_fail)||
           !megaman_stencil_enum_valid(d.face[i].depth_pass)))return 0;
    }
    *out=d;return 1;
}

/* Numeric values are standard GLES2 GLenum constants, kept independent of GL
 * headers so the decoder is usable in host fixtures. Return status separately:
 * GL_ZERO is a valid stencil operation whose GLenum is zero. */
static inline int megaman_stencil_compare_gl(unsigned nvn,uint32_t *gl){
    if(!gl||!megaman_stencil_enum_valid(nvn))return 0;
    *gl=0x0200U+nvn-1;return 1; /* NEVER, LESS, EQUAL, LEQUAL, GREATER, NOTEQUAL, GEQUAL, ALWAYS */
}
static inline int megaman_stencil_op_gl(unsigned nvn,uint32_t *gl){
    static const uint32_t operations[]={0x1e00,0,0x1e01,0x1e02,0x1e03,0x150a,0x8507,0x8508};
    if(!gl||!megaman_stencil_enum_valid(nvn))return 0;
    *gl=operations[nvn-1];return 1; /* KEEP, ZERO, REPLACE, INCR, DECR, INVERT, INCR_WRAP, DECR_WRAP */
}
static inline int megaman_stencil_dynamic_apply(struct megaman_stencil_dynamic *state,
        enum megaman_stencil_dynamic_command command,uint32_t faces,uint32_t value){
    if(!state)return 0;
    uint8_t *selected;
    switch(command){
    case MEGAMAN_STENCIL_REFERENCE:selected=state->reference;break;
    case MEGAMAN_STENCIL_VALUE_MASK:selected=state->value_mask;break;
    case MEGAMAN_STENCIL_WRITE_MASK:selected=state->write_mask;break;
    default:return 0;
    }
    /* SDK tests face bits independently and truncates every value to eight bits.
     * face=0 does nothing; unknown higher face bits have no native side effect. */
    if(faces&1)selected[0]=(uint8_t)value;
    if(faces&2)selected[1]=(uint8_t)value;
    return 1;
}
#endif
