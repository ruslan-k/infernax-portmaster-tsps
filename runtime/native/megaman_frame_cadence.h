#ifndef MEGAMAN_FRAME_CADENCE_H
#define MEGAMAN_FRAME_CADENCE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Present boundaries are observable; this does not assert that the MM9
 * simulation advances exactly once per boundary. No guest clock is changed. */
#define MFC_RESOURCES 4096U
#define MFC_COLOR 15U
#define MFC_DEPTH 16U
#define MFC_STENCIL 32U
enum mfc_result {MFC_OK,MFC_SLOT,MFC_ID,MFC_PENDING,MFC_STATE,MFC_OVERFLOW};
struct mfc_resource {uint64_t id;unsigned pending;};
/* Diagnostic input borrows only data already validated by the draw. It is
 * consumed synchronously and never retained or used to authorize a draw. */
struct mfc_stencil_origin {
    unsigned kind,vs,fs,primitive,position_valid;
    uint64_t count,base,maximum,clear_depth,clear_value,clear_mask;
    const uint16_t *indices;
    const float *position;
};
struct megaman_frame_cadence_state {
    unsigned enabled,latched,skip,submits,fraction,presenting;
    uint64_t boundaries,rendered,swaps,pixel_proofs,omitted,draws_omitted,clears_omitted,stencil_preserved_draws;
    uint64_t deadline,rebases;
    struct mfc_resource resources[MFC_RESOURCES];
};
static enum mfc_result mfc_latch(struct megaman_frame_cadence_state *s,uint64_t now) {
    if(!s->enabled || s->latched)return MFC_OK;
    s->skip=(unsigned)(s->boundaries&1U);s->latched=1;s->submits=0;
    if(!s->deadline)s->deadline=now;
    return MFC_OK;
}
static enum mfc_result mfc_submit(struct megaman_frame_cadence_state *s,uint64_t now) {
    if(!s->enabled)return MFC_OK;
    if(s->presenting)return MFC_STATE;
    mfc_latch(s,now);
    if(s->submits==UINT32_MAX)return MFC_OVERFLOW;
    s->submits++;return MFC_OK;
}
static enum mfc_result mfc_begin_present(struct megaman_frame_cadence_state *s,uint64_t now) {
    if(!s->enabled)return MFC_OK;
    if(s->presenting)return MFC_STATE;
    mfc_latch(s,now);s->presenting=1;return MFC_OK;
}
static enum mfc_result mfc_resource_get(struct megaman_frame_cadence_state *s,unsigned slot,uint64_t id,
                                        struct mfc_resource **result) {
    if(slot>=MFC_RESOURCES)return MFC_SLOT;
    if(!id)return MFC_ID;
    struct mfc_resource *r=&s->resources[slot];
    /* Slots may be reused only with a fresh native texture identity. */
    if(r->id!=id)*r=(struct mfc_resource){.id=id};
    *result=r;return MFC_OK;
}
static enum mfc_result mfc_mark(struct megaman_frame_cadence_state *s,unsigned slot,uint64_t id,unsigned bits) {
    struct mfc_resource *r;enum mfc_result status=mfc_resource_get(s,slot,id,&r);
    if(status)return status;
    r->pending|=bits;return MFC_OK;
}
static enum mfc_result mfc_require(struct megaman_frame_cadence_state *s,unsigned slot,uint64_t id,
                                  unsigned consumed,unsigned overwrites) {
    struct mfc_resource *r;enum mfc_result status=mfc_resource_get(s,slot,id,&r);
    if(status)return status;
    return r->pending&consumed&~overwrites?MFC_PENDING:MFC_OK;
}
static enum mfc_result mfc_commit(struct megaman_frame_cadence_state *s,unsigned slot,uint64_t id,unsigned bits) {
    struct mfc_resource *r;enum mfc_result status=mfc_resource_get(s,slot,id,&r);
    if(status)return status;
    r->pending&=~bits;return MFC_OK;
}
/* Original SDK layout: each face packs func/fail/zpass/zfail in successive
 * nibbles. KEEP and writeMask=0 leave stencil untouched. Consider every op
 * of both faces, including unreachable/culled branches, conservatively.
 * Existing renderer validation still rejects invalid operations. Reading
 * remains conservative: every enabled stencil test requires current data.
 * This predicate never commits pending writes, including masked REPLACE. */
static unsigned mfc_stencil_may_write(unsigned depth,unsigned state,const uint8_t write_mask[2]) {
    if(!(depth&4U))return 0;
    for(unsigned face=0;face<2;face++) {
        unsigned word=(state>>(16*face))&65535U;
        if(write_mask[face]&&(word&0xfff0U)!=0x1110U)return 1;
    }
    return 0;
}
/* Physical3403's original24/23 producer: front ALWAYS/KEEP/REPLACE/KEEP,
 * back ALWAYS/KEEP with writeMask0; depth ALWAYS; alpha ALWAYS. The pinned
 * fragment program exports color only and its TEX cannot discard fragments.
 * Preserve this draw's actual stencil coverage with color/depth masks zero.
 * This does NOT prove full coverage or authorize committing any pending bit.
 * Dynamic state and rectangles deliberately admit only the observed cases:
 * pause ref16 and physical3702's nested button-configuration ref8. The actual
 * reference is retained by the normal stencil binding, never replaced here. */
static unsigned mfc_stencil_preserve_state(unsigned vs,unsigned fs,unsigned color,unsigned depth,
        unsigned stencil,unsigned polygon,unsigned mask,const uint8_t known[2],
        const uint8_t write_mask[2],const uint8_t compare[2],const uint8_t ref[2],
        unsigned viewport_known,const int32_t viewport[4],unsigned scissor_known,const int32_t scissor[4]) {
    static const int32_t full[4]={0,0,1920,1080};
    return vs==24 && fs==23 && color==0x08030100U && depth==0x87U && stencil==0x11181318U &&
        polygon==0x8010U && mask==15 && known[0]==7 && known[1]==7 &&
        write_mask[0]==255 && !write_mask[1] && compare[0]==255 && !compare[1] &&
        (ref[0]==8 || ref[0]==16) && !ref[1] &&
        viewport_known && scissor_known && !memcmp(viewport,full,sizeof(full)) && !memcmp(scissor,full,sizeof(full));
}
static unsigned mfc_stencil_preserve_geometry(unsigned primitive,const uint16_t *indices,uint64_t count,uint64_t maximum) {
    /* Indices/attributes have already passed the normal renderer's bounds
     * validation. Submit exactly that strip, with no replacement geometry. */
    return primitive==5 && indices && count==4 && maximum==3;
}
/* Both logical rectangles must be full. The shader8/7 pair passes these XY
 * positions with z=w=1 and writes one RGBA uniform without discard or TEX.
 * Depth ALWAYS is independent of old depth; stencil must be disabled.
 * Indexed draws qualify only when their validated/rebased u16 stream uses
 * each of vertices0,1,2 exactly once. Culling is off, so all six orders cover
 * the same triangle. count is checked before reading any index. */
static unsigned mfc_triangle_indices(const uint16_t *indices,uint64_t count) {
    if(count!=3)return 0;
    if(!indices)return 1; /* DrawArrays already starts at its validated base. */
    unsigned vertices=0;
    for(unsigned i=0;i<3;i++) {
        uint16_t index;memcpy(&index,indices+i,sizeof(index));
        if(index>2)return 0;
        vertices|=1U<<index;
    }
    return vertices==7;
}
static unsigned mfc_full_triangle(unsigned vs,unsigned fs,unsigned primitive,const uint16_t *indices,uint64_t count,
                                  unsigned color,unsigned depth,unsigned polygon,unsigned mask,
                                  unsigned viewport_known,const int32_t viewport[4],
                                  unsigned scissor_known,const int32_t scissor[4],
                                  unsigned position_valid,const float position[6]) {
    static const int32_t full[4]={0,0,1920,1080};
    static const float triangle[6]={-1,-1,3,-1,-1,3};
    return vs==8 && fs==7 && primitive==4 && mfc_triangle_indices(indices,count) &&
        color==0x08030000U && !(depth&4U) && (!(depth&1U)||((depth>>4)&15U)==8U) &&
        polygon==0x00008010U && mask==15 && viewport_known && scissor_known &&
        !memcmp(viewport,full,sizeof(full)) && !memcmp(scissor,full,sizeof(full)) &&
        position_valid && !memcmp(position,triangle,sizeof(triangle));
}
static unsigned mfc_full_scissor(unsigned known,const int32_t scissor[4]) {
    static const int32_t full[4]={0,0,1920,1080};
    return known&&!memcmp(scissor,full,sizeof(full));
}
/* Return an absolute monotonic deadline. After a long delay retain at most
 * one boundary of debt: the next short omitted boundary can compensate once,
 * then a third short boundary must wait. Do not add a fresh full period after
 * an expensive rendered boundary when the pair is already over budget.
 * Fractional nanoseconds accumulate, so 60 periods equal exactly one second. */
static enum mfc_result mfc_finish(struct megaman_frame_cadence_state *s,uint64_t now,unsigned swapped,
                                 unsigned pixel_proof,uint64_t *wait_until) {
    *wait_until=0;if(!s->enabled)return MFC_OK;
    if(!s->latched || !s->presenting || (s->skip&&(swapped||pixel_proof)) || (!s->skip&&!swapped) ||
       (pixel_proof&&!swapped))return MFC_STATE;
    if(s->boundaries==UINT64_MAX)return MFC_OVERFLOW;
    s->boundaries++;if(s->skip)s->omitted++;else s->rendered++;
    s->swaps+=!!swapped;s->pixel_proofs+=!!pixel_proof;
    uint64_t period=16666666;s->fraction+=40;if(s->fraction>=60){s->fraction-=60;period++;}
    if(s->deadline>UINT64_MAX-period)return MFC_OVERFLOW;
    s->deadline+=period;
    if(now>s->deadline && now-s->deadline>16666667){s->deadline=now-period;s->rebases++;}
    if(s->deadline>now)*wait_until=s->deadline;
    s->latched=0;s->skip=0;s->submits=0;return MFC_OK;
}
#endif
