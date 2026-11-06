/* Port-local Mali fragment precision adaptation, limited to the original
 * fragments characterized on .26 in fragment-precision-01 through05. The
 * recorded synthetic comparisons against highp have maximum error3/255 (the
 * twelve added menu fragments in05 have maximum3/255 for104 and at most2/255
 * for the other eleven). This is a development
 * capability, not a claim of exact arithmetic or physical gameplay approval.
 * Vertex arithmetic stays highp; its outputs match the fragment precision. */
#if ZERO_ZX_SHADER_PACKAGES
#include "zero_shader_precision_pins.h"
#endif
static unsigned zero_shader_mediump_admitted(unsigned index) {
#if ZERO_ZX_SHADER_PACKAGES && !defined(TERMINATOR2D)   /* Zero/ZX catalogue indices; not this title */
    for(unsigned i=0;i<sizeof(zero_precision_pins)/sizeof(zero_precision_pins[0]);i++)if(index==zero_precision_pins[i].index) {
        const struct mm_shader_binding *m=&nvn_megaman_shader_bindings[index];
        const char *source=nvn_shader_sources[index].gles;uint8_t hash[32];
        if(!source)return 0;
        zero_sha256(source,strlen(source),hash);
        return zero_shader_hash_equal(m->code_hash,zero_precision_pins[i].code) &&
            zero_shader_hash_equal(m->control_hash,zero_precision_pins[i].control) &&
            zero_shader_hash_equal(hash,zero_precision_pins[i].source);
    }
#else
    (void)index;
#endif
    return 0;
}
static char *zero_shader_mediump_source(const char *source,unsigned fragment) {
    size_t n=strlen(source);if(n>(1u<<20))return NULL; /* Animal Well: the lowered Background/MidgroundSDF sources exceed 32 KB */
    char *out=malloc(n*2+1);if(!out)return NULL;size_t j=0;
    for(size_t i=0;i<n;) {
        if(fragment && !strncmp(source+i,"highp ",6) && (!i || source[i-1]==' ' || source[i-1]=='\n')) {
            memcpy(out+j,"mediump ",8);i+=6;j+=8;
        } else if(!fragment && (!i || source[i-1]=='\n') && !strncmp(source+i,"varying ",8)) {
            memcpy(out+j,"varying ",8);i+=8;j+=8;
            if(!strncmp(source+i,"highp ",6))i+=6;
            else if(!strncmp(source+i,"mediump ",8) || !strncmp(source+i,"lowp ",5))continue;
            memcpy(out+j,"mediump ",8);j+=8;
        } else out[j++]=source[i++];
    }
    out[j]=0;return out;
}
