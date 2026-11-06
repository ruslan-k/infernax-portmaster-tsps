#ifndef COTM_NVN_FRAME_TIMING_H
#define COTM_NVN_FRAME_TIMING_H
/* Observe successful swap returns, not scanout, GPU completion or logic ticks.
 * Reuse the caller's existing CLOCK_MONOTONIC timestamps. Sorting/reporting
 * occurs once per bounded batch; there is no allocation or per-frame output. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define NVN_FRAME_TIMING_SAMPLES 120U
struct nvn_frame_timing_result {
    unsigned count,first_frame,last_frame,max_frame,over50,over100,sampled,captures,clock_errors;
    unsigned max_was_sampled,max_was_capture;
    unsigned under10,under20,over50_to_under10,over50_to_under20;
    unsigned under10_to_over50,under20_to_over50;
    uint64_t total_ns,p50_ns,p95_ns,p99_ns,max_ns,capture_max_ns,regular_max_ns;
    uint64_t readback_ns,readback_max_ns,dump_ns,dump_max_ns;
};
struct nvn_frame_timing_accumulator {
    uint64_t last_swap_ns,last_gap_ns,gaps[NVN_FRAME_TIMING_SAMPLES];
    unsigned have_last,have_last_gap;
    struct nvn_frame_timing_result batch;
};
static int nvn_frame_timing_compare(const void *left,const void *right) {
    uint64_t a=*(const uint64_t *)left,b=*(const uint64_t *)right;
    return (a>b)-(a<b);
}
static unsigned nvn_frame_timing_push(struct nvn_frame_timing_accumulator *acc,unsigned frame,uint64_t swap_ns,
                                     unsigned sampled,unsigned capture,uint64_t readback_ns,uint64_t dump_ns,
                                     struct nvn_frame_timing_result *result) {
    if(!acc->have_last){acc->have_last=1;acc->last_swap_ns=swap_ns;return 0;}
    if(swap_ns<acc->last_swap_ns){
        acc->last_swap_ns=swap_ns;acc->have_last_gap=0;acc->batch.clock_errors++;return 0;
    }
    uint64_t gap=swap_ns-acc->last_swap_ns;acc->last_swap_ns=swap_ns;
    struct nvn_frame_timing_result *b=&acc->batch;
    if(!b->count)b->first_frame=frame;
    acc->gaps[b->count]=gap;b->count++;b->last_frame=frame;b->total_ns+=gap;
    if(b->count==1 || gap>b->max_ns) {
        b->max_ns=gap;b->max_frame=frame;b->max_was_sampled=!!sampled;b->max_was_capture=!!capture;
    }
    b->over50+=gap>UINT64_C(50000000);b->over100+=gap>UINT64_C(100000000);
    b->under10+=gap<UINT64_C(10000000);b->under20+=gap<UINT64_C(20000000);
    /* Count chronological neighbours before sorting. A pair belongs to the
     * batch containing its second gap, including across report boundaries.
     * Rollback breaks adjacency; a repeated timestamp is a valid zero gap. */
    if(acc->have_last_gap) {
        b->over50_to_under10+=(acc->last_gap_ns>UINT64_C(50000000) && gap<UINT64_C(10000000));
        b->over50_to_under20+=(acc->last_gap_ns>UINT64_C(50000000) && gap<UINT64_C(20000000));
        b->under10_to_over50+=(acc->last_gap_ns<UINT64_C(10000000) && gap>UINT64_C(50000000));
        b->under20_to_over50+=(acc->last_gap_ns<UINT64_C(20000000) && gap>UINT64_C(50000000));
    }
    acc->last_gap_ns=gap;acc->have_last_gap=1;
    b->sampled+=!!sampled;b->captures+=!!capture;
    if(capture){if(gap>b->capture_max_ns)b->capture_max_ns=gap;}
    else if(gap>b->regular_max_ns)b->regular_max_ns=gap;
    b->readback_ns+=readback_ns;if(readback_ns>b->readback_max_ns)b->readback_max_ns=readback_ns;
    b->dump_ns+=dump_ns;if(dump_ns>b->dump_max_ns)b->dump_max_ns=dump_ns;
    if(b->count<NVN_FRAME_TIMING_SAMPLES)return 0;
    /* Nearest rank, calculated on individual gaps in nanoseconds. In-place
     * sorting is safe: the next batch overwrites every slot before reporting. */
    qsort(acc->gaps,b->count,sizeof(acc->gaps[0]),nvn_frame_timing_compare);
    b->p50_ns=acc->gaps[(50U*b->count+99U)/100U-1U];
    b->p95_ns=acc->gaps[(95U*b->count+99U)/100U-1U];
    b->p99_ns=acc->gaps[(99U*b->count+99U)/100U-1U];
    *result=*b;*b=(struct nvn_frame_timing_result){0};return 1;
}
#ifndef NVN_FRAME_TIMING_CORE_ONLY
static struct {unsigned configured,enabled;struct nvn_frame_timing_accumulator accumulator;} nvn_frame_timing;
static unsigned nvn_frame_timing_enabled(void) {
    if(!nvn_frame_timing.configured) {
        const char *option=getenv("COTM_FRAME_TIMING");
        if(option&&strcmp(option,"0")&&strcmp(option,"1"))stop("NVN_FRAME_TIMING_OPTION",nvn_caller,0,24);
        nvn_frame_timing.enabled=option&&!strcmp(option,"1");nvn_frame_timing.configured=1;
        if(nvn_frame_timing.enabled)printf("NVN_FRAME_TIMING_EXPERIMENT enabled=1 batch=%u clock=existing-monotonic-swap-return dump-clocks=capture-only pairs=consecutive-gaps pair-batch=second-gap rollback=break-pair proof=none\n",NVN_FRAME_TIMING_SAMPLES);
    }
    return nvn_frame_timing.enabled;
}
static void nvn_frame_timing_observe(unsigned frame,uint64_t swap_ns,unsigned sampled,unsigned capture,
                                     uint64_t readback_ns,uint64_t dump_ns) {
    struct nvn_frame_timing_result r;
    if(!nvn_frame_timing_push(&nvn_frame_timing.accumulator,frame,swap_ns,sampled,capture,readback_ns,dump_ns,&r))return;
    printf("NVN_FRAME_TIMING first-frame=%u last-frame=%u count=%u mean-ms=%.3f p50-ms=%.3f p95-ms=%.3f p99-ms=%.3f max-ms=%.3f max-frame=%u max-sampled=%u max-capture=%u over50=%u over100=%u sampled=%u captures=%u capture-gap-max-ms=%.3f no-capture-gap-max-ms=%.3f readback-total-ms=%.3f readback-max-ms=%.3f dump-total-ms=%.3f dump-max-ms=%.3f clock-errors=%u under10=%u under20=%u over50-to-under10=%u over50-to-under20=%u under10-to-over50=%u under20-to-over50=%u scope=successful-swap-return-gaps percentiles=nearest-rank tags=ending-frame scanout-proof=none\n",
        r.first_frame,r.last_frame,r.count,r.total_ns*1e-6/r.count,r.p50_ns*1e-6,r.p95_ns*1e-6,r.p99_ns*1e-6,
        r.max_ns*1e-6,r.max_frame,r.max_was_sampled,r.max_was_capture,r.over50,r.over100,r.sampled,r.captures,
        r.capture_max_ns*1e-6,r.regular_max_ns*1e-6,r.readback_ns*1e-6,r.readback_max_ns*1e-6,r.dump_ns*1e-6,r.dump_max_ns*1e-6,r.clock_errors,
        r.under10,r.under20,r.over50_to_under10,r.over50_to_under20,r.under10_to_over50,r.under20_to_over50);
}
#endif
#endif
