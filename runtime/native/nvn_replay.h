/* Ordered replay. Default submission completes synchronously; the optional
 * asynchronous path retires lists only after a real GPU fence signal.
 */
static struct {float alpha_ref;uint64_t sampler_pool,texture_pool,row_stride,image_stride;
    unsigned draws,last_frame,black_after_proof,restart_enabled,restart_index;} nvn_replay;
/* Opt-in frame skip (COTM_FRAMESKIP=N renders 1 of every N submits). The game
 * logic is frame-locked at 60 Hz on the original hardware, so skipping the
 * GPU work of some frames raises the logic rate. Skipped frames still replay
 * every record (uniform updates, texture copies, counters, state) but omit
 * draws, clears and the physical present. */
static struct {int configured;unsigned every,active;uint64_t counter,skipped,rendered,draws_skipped;} nvn_skip;
static unsigned nvn_skip_every(void) {
    if(!nvn_skip.configured){const char *o=getenv("COTM_FRAMESKIP");nvn_skip.every=o?(unsigned)strtoul(o,NULL,10):0;if(nvn_skip.every>8)nvn_skip.every=8;nvn_skip.configured=1;
        if(nvn_skip.every>1)printf("NVN_FRAMESKIP_EXPERIMENT enabled=1 render-one-of=%u skipped=draws+clears+present replayed=state+uniforms+copies+counters\n",nvn_skip.every);}
    return nvn_skip.every;
}
static void nvn_skip_begin_submit(void) {
    unsigned every=nvn_skip_every();nvn_skip.counter++;
    nvn_skip.active=every>1 && (nvn_skip.counter%every)!=0;
    if(nvn_skip.active)nvn_skip.skipped++;else nvn_skip.rendered++;
}
static struct {uint64_t start,replay_ns,present_ns,readback_ns,counter_ns,finish_ns,receipt_ns,swap_ns,replay_cpu_ns;
    unsigned frames,replay_cpu_samples,replay_submits;} nvn_profile;
#define NVN_COUNTER_PROFILE_SLOTS 4U
struct nvn_counter_profile_slot {
    uint64_t count,wall_ns,wall_min,wall_max,cpu_ns,cpu_count,draws,zero_draws,swap_ns,swap_count;
    uint64_t destination,target,state_changes;
    unsigned framebuffer,width,height;
};
static struct {
    struct nvn_counter_profile_slot slots[NVN_COUNTER_PROFILE_SLOTS];
    uint64_t lists,overflow,overflow_wall_ns,overflow_cpu_ns,overflow_cpu_count,last_swap_ns;
    unsigned ordinal,previous_draws;
} nvn_counter_profile;
static void nvn_counter_profile_reset(void) {
    uint64_t swap=nvn_counter_profile.last_swap_ns;
    nvn_counter_profile=(typeof(nvn_counter_profile)){.last_swap_ns=swap};
}
static void nvn_counter_begin_list(void) {
    nvn_counter_profile.ordinal=0;nvn_counter_profile.previous_draws=nvn_replay.draws;nvn_counter_profile.lists++;
}
static unsigned nvn_cpu_nanoseconds(uint64_t *value) {
    struct timespec now;if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&now))return 0;
    *value=(uint64_t)now.tv_sec*1000000000ULL+now.tv_nsec;return 1;
}
static void *nvn_gpu_pointer(uint64_t address,uint64_t size,unsigned perm) {
    if(nvn_redirect_current)return nvn_pointer(address,size,perm);
    normal_guard_require_clear(address,size,nvn_caller);
    /* Last matching pool is rechecked in full before any other slot is scanned. */
    static _Thread_local unsigned last_pool;
    for(unsigned k=0;k<257;k++) {
        unsigned i=k?k-1:last_pool;if(k&&i==last_pool)continue;
        struct nvn_pool *p=&nvn_pools[i];
        if((p->live||p->retired) && !(p->flags&8) && address>=p->address && address-p->address<=p->size && size<=p->size-(address-p->address))
            {zero_texture_require_cpu(p,address-p->address,size);last_pool=i;return nvn_pointer(address,size,perm);}
    }
    stop("NVN_GPU_ADDRESS_RANGE",nvn_caller,address,24);return NULL;
}
#ifdef MOMO_SHADERS
/* Same lookup without the stop: for reads the hardware would happily do (a constant slot past the
 * bound size), where "not mapped" simply means "no data", not a contract violation. */
static void *nvn_gpu_pointer_or_null(uint64_t address,uint64_t size,unsigned perm) {
    if(nvn_redirect_current)return accessible(address,size,perm)?(void *)(uintptr_t)address:NULL;
    for(unsigned i=0;i<257;i++) {
        struct nvn_pool *p=&nvn_pools[i];
        if((p->live||p->retired) && !(p->flags&8) && address>=p->address && address-p->address<=p->size &&
           size<=p->size-(address-p->address) && accessible(address,size,perm))
            return (void *)(uintptr_t)address;
    }
    return NULL;
}
#endif
static uint64_t nvn_nanoseconds(void) {
    struct timespec now;if(clock_gettime(CLOCK_MONOTONIC,&now))stop("NVN_GPU_CLOCK",nvn_caller,errno,21);
    return (uint64_t)now.tv_sec*1000000000ULL+now.tv_nsec;
}
#include "megaman_frame_cadence.h"
static struct megaman_frame_cadence_state megaman_frame_cadence;
static unsigned megaman_frame_cadence_enabled(void) {
    static unsigned configured;
    if(!configured) {
        const char *o=getenv("MEGAMAN_LOGIC60_RENDER30");
        if(o&&strcmp(o,"0")&&strcmp(o,"1"))stop("NVN_LC2_CADENCE_OPTION",nvn_caller,0,24);
        megaman_frame_cadence.enabled=o&&!strcmp(o,"1");configured=1;
        if(megaman_frame_cadence.enabled) {
            if(!nvn_lc2_composition_enabled()||!nvn_rt_storage_enabled())stop("NVN_LC2_CADENCE_DEPENDENCY",nvn_caller,0,24);
            const char *incompatible[]={"COTM_FRAMESKIP","COTM_ASYNC_FRAME","COTM_ASYNC_QUEUE","COTM_GL_THREAD",
                "COTM_LOW_NORMAL_ELISION","COTM_NORMAL_EARLY","COTM_NORMAL_PASS_SKIP","COTM_DEPTH_CONSTANT_DISCARD",
                "COTM_DEPTH_DISCARD_ALL","COTM_NORMAL_DISCARD","COTM_RENDER_CAP_HZ","COTM_THERMAL_CAP_HZ"};
            for(unsigned i=0;i<sizeof(incompatible)/sizeof(incompatible[0]);i++) {
                const char *value=getenv(incompatible[i]);
                if(!strcmp(incompatible[i],"COTM_ASYNC_QUEUE")&&nvn_lc2_async_enabled())continue;
                if(value&&strcmp(value,"0"))stop("NVN_LC2_CADENCE_INCOMPATIBLE",nvn_caller,i,24);
            }
            printf("NVN_LC2_CADENCE enabled=1 boundary=QueuePresentTexture render-one-of=2 submit-latch=whole-boundary clock=real-monotonic target=60-boundaries-30-renders simulation-ticks=unmeasured default=off\n");
        }
    }
    return megaman_frame_cadence.enabled;
}
static unsigned megaman_frame_cadence_omitting(void) {
    return megaman_frame_cadence_enabled()&&megaman_frame_cadence.latched&&megaman_frame_cadence.skip;
}
/* Called after a real swap/proof, or after releasing Window ownership for an
 * omitted boundary. Never increment physical frame/capture counters here. */
static void megaman_frame_cadence_end_present(unsigned swapped,unsigned pixel_proof) {
    if(!megaman_frame_cadence_enabled())return;
    pthread_mutex_lock(&nvn_gl_completion.mutex);
    uint64_t deadline;enum mfc_result result=mfc_finish(&megaman_frame_cadence,nvn_nanoseconds(),swapped,pixel_proof,&deadline);
    if(result)stop("NVN_LC2_CADENCE_PRESENT",nvn_caller,result,24);
    pthread_mutex_unlock(&nvn_gl_completion.mutex);
    if(deadline) {
        struct timespec when={(time_t)(deadline/1000000000ULL),(long)(deadline%1000000000ULL)};
        int error;do{error=clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&when,NULL);}while(error==EINTR);
        if(error)stop("NVN_LC2_CADENCE_CLOCK",nvn_caller,error,21);
    }
    pthread_mutex_lock(&nvn_gl_completion.mutex);
    static uint64_t observed_at,observed_boundary;
    uint64_t observed=nvn_nanoseconds(),count=megaman_frame_cadence.boundaries-observed_boundary;
    if(observed_at&&count>=60) {
        printf("NVN_LC2_CADENCE_PROFILE boundaries=%"PRIu64" rendered=%"PRIu64" omitted=%"PRIu64" swaps=%"PRIu64" pixel-proofs=%"PRIu64" omitted-draws=%"PRIu64" omitted-clears=%"PRIu64" deadline-rebases=%"PRIu64" monotonic-ns=%"PRIu64" observed-boundaries=%"PRIu64" elapsed-ms-per-boundary=%.6f simulation-ticks=unmeasured skipped-health=none\n",
            megaman_frame_cadence.boundaries,megaman_frame_cadence.rendered,megaman_frame_cadence.omitted,
            megaman_frame_cadence.swaps,megaman_frame_cadence.pixel_proofs,megaman_frame_cadence.draws_omitted,
            megaman_frame_cadence.clears_omitted,megaman_frame_cadence.rebases,observed,count,
            (double)(observed-observed_at)*1e-6/count);
    }
    if(!observed_at||count>=60){observed_at=observed;observed_boundary=megaman_frame_cadence.boundaries;}
    megaman_frame_cadence.presenting=0;
    pthread_mutex_unlock(&nvn_gl_completion.mutex);
}
#include "nvn_counter_time.h"
#include "nvn_replay_errors.h"
static void momo_stencil_probe(unsigned draw,unsigned vertex,unsigned fragment);
#include "nvn_render.h"
/* CPU observations only: no GL query, extra finish, or GPU timestamp is
 * introduced. Ordinals restart for each command list; the summary window
 * resets independently, preserving the timestamp of the latest real swap. */
static void nvn_counter_measure(uint64_t destination,uint64_t start,uint64_t end,
                                uint64_t cpu_start,uint64_t cpu_end,unsigned cpu_valid) {
    unsigned ordinal=nvn_counter_profile.ordinal++,draws=nvn_replay.draws-nvn_counter_profile.previous_draws;
    nvn_counter_profile.previous_draws=nvn_replay.draws;
    uint64_t wall=end-start,cpu=cpu_valid?cpu_end-cpu_start:0;
    if(ordinal>=NVN_COUNTER_PROFILE_SLOTS) {
        nvn_counter_profile.overflow++;nvn_counter_profile.overflow_wall_ns+=wall;
        if(cpu_valid){nvn_counter_profile.overflow_cpu_ns+=cpu;nvn_counter_profile.overflow_cpu_count++;}
        return;
    }
    struct nvn_counter_profile_slot *slot=&nvn_counter_profile.slots[ordinal];
    uint64_t target=nvn_render.target?nvn_render.target->id:0;
    unsigned width=nvn_render.target?nvn_render.target->b.width:0,height=nvn_render.target?nvn_render.target->b.height:0;
    if(slot->count && (slot->destination!=destination || slot->target!=target || slot->framebuffer!=nvn_render.framebuffer ||
                      slot->width!=width || slot->height!=height))slot->state_changes++;
    if(!slot->count || wall<slot->wall_min)slot->wall_min=wall;
    if(wall>slot->wall_max)slot->wall_max=wall;
    slot->count++;slot->wall_ns+=wall;slot->draws+=draws;if(!draws)slot->zero_draws++;
    if(cpu_valid){slot->cpu_ns+=cpu;slot->cpu_count++;}
    if(nvn_counter_profile.last_swap_ns && start>=nvn_counter_profile.last_swap_ns) {
        slot->swap_ns+=start-nvn_counter_profile.last_swap_ns;slot->swap_count++;
    }
    slot->destination=destination;slot->target=target;slot->framebuffer=nvn_render.framebuffer;slot->width=width;slot->height=height;
}
#if ZERO_ZX_GAME
#include "zero_texture_readback.h"
#endif
static void nvn_replay_record(struct nvn_record *r) {
    uint64_t *a=r->args;
    {   /* MOMO_TRACE_FRAME=<frame>: every replayed command of that frame, in order, with its arguments.
         * The per-draw state dumps cannot show a missing or misordered bind; the stream can. */
        static int cfg;static unsigned wanted;
        if(!cfg){const char *o=getenv("MOMO_TRACE_FRAME");wanted=o?(unsigned)strtoul(o,NULL,0):0;cfg=1;}
        if(t2_pass_probe_active() || (wanted && nvn_replay.last_frame+1==wanted))
            printf("MOMO_CMD draw=%u op=%s a0=0x%"PRIx64" a1=0x%"PRIx64" a2=0x%"PRIx64" a3=0x%"PRIx64"\n",
                nvn_replay.draws,r->op<NI_COUNT?nvn_functions[r->op].name:"?",a[0],a[1],a[2],a[3]);
    }
    if(nvn_render_record(r))return;
    if(nvn_redirect_current&&(r->op==NI_nvnCommandBufferUpdateUniformBuffer||r->op==NI_nvnCommandBufferReportCounter))return;
    if(r->op==NI_nvnCommandBufferUpdateUniformBuffer) {
        if(a[2]>a[1] || a[3]>a[1]-a[2] || a[3]>sizeof(r->payload))
            stop("NVN_UNIFORM_REPLAY_UPDATE_RANGE",nvn_caller,a[3],24);
        uint8_t *destination=nvn_gpu_pointer(a[0],a[1],PROT_READ|PROT_WRITE);
        memcpy(destination+a[2],r->payload,a[3]);
        if(getenv("MOMO_TRACE_UNIFORM_UPDATES")) {
            static unsigned told;
            if(told++<48) {
                float head[4];memcpy(head,r->payload,a[3]<16?a[3]:16);
                printf("MOMO_UNIFORM_UPDATE draw=%u buffer=0x%"PRIx64" size=%"PRIu64" offset=%"PRIu64" bytes=%"PRIu64" first=%g %g %g %g\n",
                    nvn_replay.draws,a[0],a[1],a[2],a[3],head[0],head[1],head[2],head[3]);
            }
        }
        return;
    }
    if(r->op==NI_nvnCommandBufferSetAlphaRef){memcpy(&nvn_replay.alpha_ref,r->vectors,4);return;}
    if(r->op==NI_nvnCommandBufferSetSamplerPool || r->op==NI_nvnCommandBufferSetTexturePool) {
        unsigned type=r->op==NI_nvnCommandBufferSetSamplerPool?2:1,found=0;
        for(unsigned i=0;i<32;i++)if(nvn_descriptor_pools[i].live&&nvn_descriptor_pools[i].object==a[0]&&nvn_descriptor_pools[i].type==type)found=1;
        if(!found)stop("NVN_REPLAY_DESCRIPTOR_POOL",nvn_caller,a[0],24);
        if(type==2)nvn_replay.sampler_pool=a[0];else nvn_replay.texture_pool=a[0];return;
    }
    if(r->op==NI_nvnCommandBufferReportCounter) {
        if(a[0])stop("NVN_COUNTER_TYPE_REQUIRED",nvn_caller,a[0],20);
        if(a[1]%16)stop("NVN_COUNTER_ALIGNMENT",nvn_caller,a[1],24);
        uint64_t cpu_start=0,cpu_end=0;unsigned cpu_valid=nvn_cpu_nanoseconds(&cpu_start);
        uint64_t start=nvn_nanoseconds();uint64_t counter[2]={0,nvn_counter_timestamp()};
        cpu_valid=nvn_cpu_nanoseconds(&cpu_end)&&cpu_valid&&cpu_end>=cpu_start;
        nvn_counter_measure(a[1],start,counter[1],cpu_start,cpu_end,cpu_valid);
        nvn_profile.counter_ns+=counter[1]-start;memcpy(nvn_gpu_pointer(a[1],16,PROT_WRITE),counter,16);return;
    }
    if(r->op==NI_nvnCommandBufferSetCopyRowStride){nvn_replay.row_stride=a[0];return;}
    if(r->op==NI_nvnCommandBufferSetCopyImageStride){nvn_replay.image_stride=a[0];return;}
#if ZERO_ZX_GAME
    if(r->op==NI_nvnCommandBufferCopyTextureToBuffer){zero_texture_readback(r);return;}
#endif
    if(r->op==NI_nvnCommandBufferCopyBufferToBuffer) {
        /* Buffer addresses, so the copy is a move inside guest memory and nothing on the GPU side has to
         * be told: everything that reads a buffer here reads it through nvn_pointer at use time. It runs
         * when the command executes, not when it is recorded, because the game may still be filling the
         * source while it records (MEDIDO run momo-251: the first CopyBufferToBuffer of the whole port
         * arrives when a save slot is confirmed and the first area starts loading). */
        uint64_t source=a[0],destination=a[1],bytes=a[2];
        if(bytes)memcpy(nvn_gpu_pointer(destination,bytes,PROT_WRITE),nvn_gpu_pointer(source,bytes,PROT_READ),bytes);
        static uint64_t copies,copied;copies++;copied+=bytes;
        if(copies<=4||copies%512==0)
            printf("NVN_COPY_BUFFER copies=%"PRIu64" bytes=%"PRIu64" total=%"PRIu64" source=0x%"PRIx64" destination=0x%"PRIx64"\n",
                copies,bytes,copied,source,destination);
        return;
    }
    if(r->op==NI_nvnCommandBufferCopyBufferToTexture) {
        if(r->args[6]) {
            /* Frente I (MEDIDO device run 203, t2-203.log 2294/2302/2805): the destination can also be finalized
             * and NOT reused before the list runs - Unity's 2x2 placeholder 0x14a611818 died between recording
             * and submit and nvn_texture_find stopped with NVN_TEXTURE_INVALID. Runs 115-119/202 only survived
             * because the allocator happened to reuse that object first. A dead destination drops the copy,
             * exactly like the reused case below. */
            unsigned alive=0;
            for(unsigned i=0;i<4096 && !alive;i++)alive=nvn_textures[i].live&&nvn_textures[i].object==a[1];
#ifdef MOMO_SHADERS
            if(!alive)alive=momo_texture_stamp_read(a[1])!=0 && momo_texture_alias(a[1],NULL)!=NULL;
#endif
            if(!alive) {
                static unsigned told;
                if(told++<8)printf("NVN_COPY_STALE_OBJECT object=0x%"PRIx64" recorded-id=%"PRIu64" live-id=none action=dropped\n",a[1],r->args[6]);
                return;
            }
        }
        struct nvn_texture *t=nvn_texture_find(a[1]);uint32_t box[6];memcpy(box,r->payload,sizeof(box));
        if(r->args[6] && t->id!=r->args[6]) {
            /* The destination object was finalized and reused between recording and replay (Frente F,
             * nvn_commands.h): on the hardware this copy lands in the dead texture's storage. Dropped. */
            static unsigned told;
            if(told++<8)printf("NVN_COPY_STALE_OBJECT object=0x%"PRIx64" recorded-id=%"PRIu64" live-id=%"PRIu64" box=%u,%u,%ux%u action=dropped\n",a[1],r->args[6],t->id,box[0],box[1],box[3],box[4]);
            return;
        }
        if(t->zero_backend_deferred)stop("ZERO_FLOAT_TEXTURE_COPY_BACKEND_REQUIRED",nvn_caller,t->b.format,20);
        if(t->aw_native_format)stop("AW_FLOAT_TEXTURE_COPY_UNSUPPORTED",nvn_caller,t->aw_native_format,20);
        if(a[2] && !momo_texture_view_single_level(r->payload+24,t->b.format,t->b.target)) {
            printf("MOMO_COPY_VIEW target=%u levels=%u format=0x%x view=",t->b.target,t->b.levels,t->b.format);
            for(unsigned i=0;i<MEGAMAN_TEXTURE_VIEW_BYTES;i++)printf("%02x",r->payload[24+i]);
            putchar('\n');fflush(stdout);
            stop("NVN_COPY_TEXTURE_VIEW_REQUIRED",nvn_caller,a[2],20);
        }
        if(nvn_low_color(t)>=0)stop("NVN_LOW_COPY_DESTINATION_REQUIRED",nvn_caller,t->id,20);
        /* The view selects the destination level (Unity uploads one mip per copy, run momo-053). */
        unsigned copy_level=(a[2] && (r->payload[24+7]&MOMO_VIEW_FLAG_LEVELS))?r->payload[24+4]:0;
        unsigned level_width=t->b.width>>copy_level,level_height=t->b.height>>copy_level;
        if(!level_width)level_width=1;
        if(!level_height)level_height=1;
        if(!t->name || copy_level>=t->b.levels || box[2] || box[5]!=1 || !box[3] || !box[4] ||
           box[0]>level_width || box[3]>level_width-box[0] ||
           box[1]>level_height || box[4]>level_height-box[1])stop("NVN_COPY_REGION_REQUIRED",nvn_caller,a[1],24);
        unsigned bpp=nvn_texture_format_bytes(t->b.format);
        if(!bpp || nvn_texture_format_depth(t->b.format))stop("NVN_COPY_TEXTURE_FORMAT_REQUIRED",nvn_caller,t->b.format,20);
        uint64_t row=(uint64_t)box[3]*bpp,stride=nvn_replay.row_stride?nvn_replay.row_stride:row;
        if(stride<row || stride>64U*1024U*1024U)stop("NVN_COPY_ROW_STRIDE",nvn_caller,stride,24);
        const uint8_t *src=nvn_gpu_pointer(a[0],stride*(box[4]-1)+row,PROT_READ);
        normal_guard_require_clear(t->pool->address+t->b.offset,t->size,nvn_caller);
        /* Frente F: a deferred 4096x4096 page takes the palettized form here (or RGBA8 when unmatched). */
        if(t2_page_copy(t,box,src,stride,copy_level)) {
            /* ROOT (MEDIDO device run 105): Unity 6 keeps each page's 64 MiB CPU_CACHED|GPU_UNCACHED
             * staging pool (flags 0x14) alive for the whole load; 4 resident pools = 256 MiB and the
             * 916 MiB device dies at the 5th page. The page has just been consumed in palettized form,
             * so the upload source is dead data: drop its whole pages (the guest refaults zeros if it
             * ever rewrites the buffer). T2_PAGE_STAGING_DROP=0 keeps the old behaviour. */
            static int drop=-1;if(drop<0){const char *e=getenv("T2_PAGE_STAGING_DROP");drop=!e||*e!='0';}
            if(drop && t->t2_page.mode) {
                uintptr_t lo=((uintptr_t)src+4095)&~(uintptr_t)4095,hi=((uintptr_t)src+stride*(box[4]-1)+row)&~(uintptr_t)4095;
                int rc=hi>lo?madvise((void *)lo,hi-lo,MADV_DONTNEED):-1;
                printf("T2_PAGE_STAGING_DROP texture=%"PRIu64" bytes=%zu rc=%d\n",t->id,(size_t)(hi>lo?hi-lo:0),rc);
            }
            return;
        }
        unsigned upload_type=nvn_cpu_image_upload_type(t);
        size_t rgba_row=(size_t)box[3]*nvn_texture_upload_bytes(upload_type),size=rgba_row*box[4];uint8_t *rgba=malloc(size);if(!rgba)stop("NVN_TEXTURE_UPLOAD_MEMORY",nvn_caller,size,21);
        for(unsigned y=0;y<box[4];y++)
            if(!nvn_texture_decode_upload(t->b.format,t->b.swizzle,upload_type,rgba+y*rgba_row,src+y*stride,box[3]))
                stop("NVN_COPY_TEXTURE_DECODE_REQUIRED",nvn_caller,t->b.format,20);
        nvn_texture_copy_cpu(t,box,src,stride,copy_level);
        glBindTexture(GL_TEXTURE_2D,t->name);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glTexSubImage2D(GL_TEXTURE_2D,(GLint)copy_level,box[0],box[1],box[3],box[4],GL_RGBA,upload_type,rgba);free(rgba);
        return;
    }
    printf("NVN_REPLAY_REQUIRED name=%s\n",nvn_functions[r->op].name);stop("NVN_REPLAY_OPERATION_REQUIRED",nvn_caller,r->op,20);
}
static uint64_t nvn_replay_run_list(struct nvn_command_list *list,unsigned batch_errors,unsigned snapshot) {
    struct nvn_queue *q=NULL;unsigned asynchronous=nvn_async_enabled();(void)q;(void)asynchronous;
    uint64_t next=list->first,consumed=0;
            for(uint64_t j=0;j<list->count;j++) {
                struct nvn_record r;nvn_phase_begin();nvn_command_decode(list,next,&r);nvn_phase_end(NPH_DECODE);
                unsigned is_draw=r.op==NI_nvnCommandBufferDrawArrays||r.op==NI_nvnCommandBufferDrawElements||r.op==NI_nvnCommandBufferDrawElementsBaseVertex;
                uint64_t draw_cpu0=0;if(nvn_phase.enabled&&is_draw)nvn_cpu_nanoseconds(&draw_cpu0);
                if(nvn_phase.enabled&&(nvn_replay.last_frame==3000||nvn_replay.last_frame==3001)) {
                    uint64_t tg[2]={0,0};if(r.op==NI_nvnCommandBufferSetRenderTargets)memcpy(tg,r.payload,16);
                    printf("NVN_OPSEQ frame=%u op=%s a0=0x%"PRIx64" a1=0x%"PRIx64" a2=0x%"PRIx64" targets=0x%"PRIx64"/0x%"PRIx64"\n",nvn_replay.last_frame+1,nvn_functions[r.op].name,r.args[0],r.args[1],r.args[2],tg[0],tg[1]);
                }
                if((j+1==list->count && (next!=list->last || r.next)) ||
                   (j+1<list->count && (next==list->last || !r.next)) ||
                   consumed>list->recorded_bytes || r.bytes>list->recorded_bytes-consumed)
                    stop("NVN_COMMAND_RECORD_CHAIN",nvn_caller,next,24);
                if(is_draw&&nvn_redirect_current)nvn_redirect_advance_draw();
#ifdef ANIMAL_WELL_SHADERS
                {   static int pc;static unsigned pf;if(!pc){const char *o=getenv("AW_DRAW_PROBE");pf=o?(unsigned)strtoul(o,NULL,10):0;pc=1;}
                    if(pf && nvn_replay.last_frame+1==pf){uint64_t tg[2]={0,0};if(r.op==NI_nvnCommandBufferSetRenderTargets)memcpy(tg,r.payload,16);
                        printf("AW_OPSEQ op=%s a0=0x%"PRIx64" a1=0x%"PRIx64" a2=0x%"PRIx64" a3=0x%"PRIx64" target=0x%"PRIx64"\n",nvn_functions[r.op].name,r.args[0],r.args[1],r.args[2],r.args[3],tg[0]);}
                }
#endif
                zero_gpu_cost_context(nvn_functions[r.op].name,0,0,0,0,0,0);
                nvn_replay_record(&r);
                if(nvn_phase.enabled){unsigned k=r.op==NI_nvnCommandBufferCopyBufferToTexture?NPH_COPY:NPH_OTHER;
                    if(is_draw){k=nvn_phase.target_changed?NPH_DRAWFIRST:NPH_DRAWCALL;nvn_phase.target_changed=0;uint64_t c=0;if(nvn_cpu_nanoseconds(&c)&&c>=draw_cpu0)nvn_phase.draw_cpu_ns+=c-draw_cpu0;}
                    if(r.op==NI_nvnCommandBufferSetRenderTargets)nvn_phase.target_changed=1;
                    uint64_t before=nvn_phase.mark;nvn_phase_end(k);if(r.op<NI_COUNT){nvn_phase.op_ns[r.op]+=nvn_phase.mark-before;nvn_phase.op_calls[r.op]++;}}
#ifdef ANIMAL_WELL_SHADERS
                if(!batch_errors||nvn_draw_diagnostic_active()){GLenum e=glGetError();if(e){printf("AW_GL_ERROR op=%s error=0x%x a0=0x%"PRIx64" a1=0x%"PRIx64" a2=0x%"PRIx64" a3=0x%"PRIx64" draws=%u frame=%u\n",nvn_functions[r.op].name,e,r.args[0],r.args[1],r.args[2],r.args[3],nvn_replay.draws,nvn_replay.last_frame);
                    if(nvn_render.program)printf("AW_GL_ERROR_PROGRAM pair=%u/%u target=%"PRIu64"\n",nvn_render.program->sources[0],nvn_render.program->sources[1],nvn_render.target?nvn_render.target->id:0);
                    stop("NVN_REPLAY_GL_ERROR",nvn_caller,e,21);}}
#else
                if(!batch_errors||nvn_draw_diagnostic_active()){GLenum e=glGetError();if(e){printf("NVN_GL_ERROR op=%s error=0x%x draws=%u frame=%u target=%"PRIu64"\n",nvn_functions[r.op].name,e,nvn_replay.draws,nvn_replay.last_frame,nvn_render.target?nvn_render.target->id:0);stop("NVN_REPLAY_GL_ERROR",nvn_caller,e,21);}}
#endif
                zero_gpu_cost_context("outside-record",0,0,0,0,0,0);
                nvn_replay_last_op=nvn_functions[r.op].name;
                consumed+=r.bytes;next=r.next;
            }
            if(next || consumed!=list->recorded_bytes)stop("NVN_COMMAND_RECORD_COUNT",nvn_caller,next,24);
            if(batch_errors)nvn_replay_error_boundary(); /* before fence/completion/publication */
            if(!asynchronous&&!snapshot) {
                /* MOMO_SUBMIT_FINISH=0: end an executed list with a flush instead of a full wait, to tell
                 * real GPU work apart from serialization (this glFinish is 651 ms of a 771 ms frame,
                 * MEDIDO run momo-131). Completion for CPU reads still comes from the paths that wait. */
                static int cfg;static unsigned flush_only;
                if(!cfg){const char *o=getenv("MOMO_SUBMIT_FINISH");flush_only=o&&!strcmp(o,"0");cfg=1;}
                uint64_t finish_started=nvn_nanoseconds();
                if(flush_only)glFlush();else nvn_gl_finish_current();
                nvn_profile.finish_ns+=nvn_nanoseconds()-finish_started;
            }
    return consumed;
}
static unsigned nvn_replay_dispatch(unsigned id,struct nvn_frame *f,uint64_t *result) {
    uint64_t *a=f->args;*result=0;
    if(id==NI_nvnQueueSubmitCommands) {
        uint64_t started=nvn_nanoseconds();
        uint64_t cpu_start=0,cpu_end=0;unsigned cpu_valid=nvn_cpu_nanoseconds(&cpu_start);
        struct nvn_queue *q=nvn_queue_find(a[0]);
        if(a[1]>256)stop("NVN_QUEUE_SUBMIT_COUNT",nvn_caller,a[1],24);
        const uint64_t *handles=nvn_pointer(a[2],a[1]*8,PROT_READ);nvn_gl_acquire();
        unsigned asynchronous=nvn_async_enabled(),batch_errors=nvn_replay_errors_batched();uint64_t submitted_handles[256];
        t2_queue_submits_seen++;
        if(asynchronous)nvn_async_admit_locked(q,a[1]);
        nvn_stream_vbo_begin_submit();
        if(megaman_frame_cadence_enabled()) {
            enum mfc_result status=mfc_submit(&megaman_frame_cadence,nvn_nanoseconds());
            if(status)stop("NVN_LC2_CADENCE_SUBMIT",nvn_caller,status,24);
            nvn_skip.active=0; /* the old early-return path is not this mode */
        } else nvn_skip_begin_submit();
        if(!nvn_skip.active)nvn_stable_vbo_begin_submit();
        uint64_t first_head=q->arena->head,first_submitted=q->arena->submitted;
        for(unsigned i=0;i<a[1];i++) {
            struct nvn_command_list *list=nvn_command_list_require(handles[i]);uint64_t consumed=0;
            submitted_handles[i]=list->handle; /* Never reread a guest array that replay can legitimately update. */
            q->handles[q->arena->head%(q->command_size/8)]=list->handle;q->arena->head++;q->arena->submitted++;list->submitted=1;list->completed=0;
            nvn_counter_begin_list();consumed=nvn_replay_run_list(list,batch_errors,0);
#ifdef ANIMAL_WELL_SHADERS
            aw_clear_flush_all();
#endif
            if(!asynchronous){list->completed=1;q->arena->tail++;q->arena->completed=q->arena->submitted;}
            q->command_used+=consumed;q->control_used+=32;
            static _Thread_local struct frame_log_counter executed_logs;
            static _Thread_local struct nvn_command_log_cache executed_cache;
            uint64_t log_lifetime=list->log_lifetime;
            struct frame_log_event log=nvn_command_log_sample(&executed_cache,&executed_logs,list->owner,list->count,log_lifetime,frame_log_full_commands()||nvn_draw_diagnostic_active());
            if(log.emit) {
                if(nvn_command_log_enabled)printf("NVN_COMMANDS_EXECUTED handle=%"PRIu64" count=%"PRIu64" completed-with-glFinish=%u gpu-pending=%u game-draws=%u occurrence=%"PRIu64" suppressed=%"PRIu64" log-thread=%u log-scope=thread-owner-count-lifetime changed=%u owner=0x%"PRIx64" lifetime=%"PRIu64" channel-calls=%"PRIu64" channel-emitted=%"PRIu64" channel-suppressed=%"PRIu64" cache-overflow=%"PRIu64"\n",
                    list->handle,list->count,!asynchronous,asynchronous,nvn_replay.draws,log.occurrence,log.suppressed,guest_thread_index,log.changed,list->owner,log_lifetime,executed_cache.calls,executed_cache.emitted,executed_cache.suppressed,executed_cache.overflow);
                else printf("NVN_COMMANDS_EXECUTED handle=%"PRIu64" count=%"PRIu64" completed-with-glFinish=%u gpu-pending=%u game-draws=%u occurrence=%"PRIu64" suppressed=%"PRIu64" log-thread=%u log-scope=thread-channel changed=%u\n",
                    list->handle,list->count,!asynchronous,asynchronous,nvn_replay.draws,log.occurrence,log.suppressed,guest_thread_index,log.changed);
            }
        }
        if(asynchronous)nvn_async_submit_locked(q,submitted_handles,a[1],first_head,first_submitted);
        if(asynchronous&&nvn_async_early_enabled()) {
            /* Guest command memory was fully decoded above; allow its reuse now. */
            for(unsigned i=0;i<a[1];i++) {
                struct nvn_command_list *list=nvn_command_list_lookup(submitted_handles[i]);
                if(list)list->completed=1;
            }
        }
        zero_gpu_cost_report(nvn_replay.last_frame);
        nvn_gl_release();nvn_profile.replay_ns+=nvn_nanoseconds()-started;
        nvn_async_report(nvn_replay.last_frame);
        nvn_profile.replay_submits++;
        if(nvn_cpu_nanoseconds(&cpu_end) && cpu_valid && cpu_end>=cpu_start) {
            nvn_profile.replay_cpu_ns+=cpu_end-cpu_start;nvn_profile.replay_cpu_samples++;
        }
        return 1;
    }
    if(id==NI_nvnDeviceGetCurrentTimestampInNanoseconds || id==NI_nvnDeviceGetTimestampInNanoseconds) {
        if(a[0]!=nvn_device.address)stop("NVN_TIMESTAMP_DEVICE",nvn_caller,a[0],24);
        *result=id==NI_nvnDeviceGetCurrentTimestampInNanoseconds?nvn_nanoseconds():nvn_read64(a[1]+8);return 1;
    }
    return 0;
}
