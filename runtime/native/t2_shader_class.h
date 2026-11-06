/* Unity 6 / NVN 19.3.9: control+0x70c is NOT a shader class.
 * ColorTransform is graphics with value 0x0c. Graphics carry the SPH magic;
 * the sole admitted non-graphics program is the inventoried built-in compute.
 * Source identities: docs/SHADERS.md §6 and shaders_embedded/embedded.json.
 */
#ifndef T2_SHADER_CLASS_H
#define T2_SHADER_CLASS_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "zero_sha256.h"
enum t2_shader_class { T2_SHADER_UNKNOWN, T2_SHADER_GRAPHICS, T2_SHADER_COMPUTE };
static inline enum t2_shader_class t2_shader_classify(const uint8_t *code,size_t size) {
    static const uint8_t compute_sha[32]={
        0x29,0x6b,0xf4,0x34,0xe3,0xfd,0x6b,0x8d,0x13,0xc9,0x5c,0x53,0xe2,0xf5,0x8c,0x86,
        0x86,0xa1,0xa6,0xd5,0x75,0x12,0x81,0x57,0xf6,0xa0,0xed,0x7b,0xbf,0x2f,0x2f,0x5a};
    if(!code || size<0x80 || size>131072)return T2_SHADER_UNKNOWN;
    if(code[0]==0x78 && code[1]==0x56 && code[2]==0x34 && code[3]==0x12)return T2_SHADER_GRAPHICS;
    uint8_t sha[32];zero_sha256(code,size,sha);
    return !memcmp(sha,compute_sha,32)?T2_SHADER_COMPUTE:T2_SHADER_UNKNOWN;
}
#endif
