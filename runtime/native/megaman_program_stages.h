/* NVN stage selection is independent of GLES2's linked-program object.
 * Native object generations remain part of every binding and recorded command.
 */
#ifndef MEGAMAN_PROGRAM_STAGES_H
#define MEGAMAN_PROGRAM_STAGES_H
#include <stdint.h>
struct megaman_program_stage {uint64_t object,id;unsigned source;};
static unsigned megaman_program_bind(struct megaman_program_stage bound[2],
    uint64_t object,uint64_t id,unsigned available,const unsigned sources[2],unsigned mask) {
    if(mask&~3U || available&~3U || (object && (!id || !available)) || (!object && (id || available)))return 0;
    if(!mask)return 1;
    struct megaman_program_stage next[2]={bound[0],bound[1]};
    /* SDK copies every present stage's words, then disables requested stages
     * absent from that program. Its mask==0 path is an explicit no-op. */
    for(unsigned i=0;i<2;i++)if((mask|available)&(1U<<i)) {
        next[i]=(object && (available&(1U<<i)))?
            (struct megaman_program_stage){object,id,sources[i]}:(struct megaman_program_stage){0};
    }
    bound[0]=next[0];bound[1]=next[1];return 1;
}
static unsigned megaman_program_stage_valid(const struct megaman_program_stage *bound,
    uint64_t object,uint64_t id,unsigned available,const unsigned sources[2],unsigned stage) {
    return stage<2 && bound->object && bound->object==object && bound->id==id &&
        (available&(1U<<stage)) && bound->source==sources[stage];
}
#endif
