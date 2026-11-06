/* Command storage belongs to the game. Arenas are borrowed with pool lifetime
 * references. Typed records preserve arguments now; replay validates execution.
 */
#include "frame_logs.h"
#include "aw_release.h"
#include "nvn_command_logs.h"
#include "megaman_texture_view.h"
#include "zero_render_target_view.h"
#if ZERO_ZX_GAME
static void zero_record_target_view(uint64_t object,const uint8_t *view,unsigned attachment);
static uint64_t zero_texture_record_id(uint64_t object);
#endif
#define NVN_COMMAND_ALIGNMENT 16U
#define NVN_CONTROL_ALIGNMENT 16U
/* `limit` = bytes the recorder may fill (= size unless T2_COMMAND_ARENA_CAP_BYTES caps it on the bench). */
struct nvn_command_arena {uint64_t address,size,used;struct nvn_pool *pool;uint64_t limit;};
/* Expanded host scratch only. The borrowed guest arena stores the compact
 * prefix, optional vector registers and exactly the snapshotted payload. */
struct nvn_record {uint64_t next;uint32_t op,bytes;uint64_t args[7];__uint128_t vectors[4];uint8_t payload[256];};
_Static_assert(sizeof(struct nvn_record)%16==0,"command record alignment");
struct nvn_record_prefix {uint64_t next;uint32_t op,bytes;uint64_t args[7];};
_Static_assert(sizeof(struct nvn_record_prefix)==72 && offsetof(struct nvn_record_prefix,args)==16,"compact record prefix");
struct nvn_command_list {uint64_t handle,first,last,count,control,owner,recorded_bytes,log_lifetime;unsigned live,submitted,completed;
    /* FRENTE-P: contiguous address ranges this list's records occupy (filled at emit). Lets "does list L use
     * arena A" answer without walking every record through nvn_command_storage (MEDIDO run 405: the guest's
     * out-of-memory callback took 16.7 ms median, 361 times in 157 s of gameplay, one hitch each). */
    uint64_t t2_range_first[8],t2_range_end[8];unsigned t2_ranges,t2_range_overflow;};
/* Configured by the LC2 queue opt-in before dispatch, via pthread_once.
 * NULL preserves the standalone/synchronous recorder and legacy policy. */
static void (*nvn_command_pending_poll)(uint64_t handle);
static uint64_t nvn_next_command_handle;
struct nvn_command_buffer {
    uint64_t object,callback,callback_data;struct nvn_command_arena command[16],control[16];
    unsigned command_count,control_count,recording,live;struct nvn_command_list *list;
    /* Terminator 2D: index of the command arena NVN is writing into (the last one added or re-added);
     * == command_count when there is none. Records never go to an older arena: the game may already
     * have handed that chunk to another command buffer (see t2_command_arena_transfer). */
    unsigned current;
    struct {uint64_t first,last;} command_retained[16];
    uint64_t memory_generation,log_lifetime;unsigned callback_active,callback_event,command_callback_disabled;
    /* Animal Well: last recorded render targets / scissor, replayed again after a synthesized ClearTexture. */
    struct nvn_record last_targets,last_scissor;unsigned last_targets_valid,last_scissor_valid;
    struct nvn_record aw_restore_targets;unsigned aw_restore_pending;
};
static struct nvn_command_buffer nvn_command_buffers[32];
/* Terminator 2D (Unity 6, boot.config gfx-threading-mode=4 = native graphics jobs): several guest threads
 * record into their own command buffers at the same time (MEDIDO run t2d-b07-long: handles 171/173/174/175/
 * 177/179/180/185 ended by guest threads 6 and 7 while thread 29 recorded 170/172/176/184/186/188). Guest
 * threads are real pthreads and the NVN bridge holds no lock, so the recorder's shared state - the buffer
 * table, the command list table (`nvn_command_list_allocate` = first `!live` slot) and the handle counter -
 * raced: two BeginRecording picked the same list, and thread 29's `list->last` ended up inside thread 6's
 * arena (STOP NVN_COMMAND_RECORD_STORAGE detail=0x1550841c0 in arena 0x155080000 of buffer 0x14a922028 while
 * recording BindSeparateTexture on buffer 0x149701cd8). This mutex serializes every command-buffer API
 * call; it is released while the guest's out-of-memory callback runs (the callback re-enters through
 * AddCommandMemory and may take engine locks of its own). Replay only reads records of lists whose handle
 * the guest already fenced, so it does not take the lock; `live` is published last with a release store. */
static pthread_mutex_t nvn_command_recorder_mutex=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local unsigned nvn_command_recorder_held;
static _Thread_local unsigned nvn_command_current_id;
static struct {uint64_t locks,contended;uint64_t threads[2];} nvn_command_recorder_stats;
static void nvn_command_recorder_lock(void) {
    if(nvn_command_recorder_held++)return;
    if(pthread_mutex_trylock(&nvn_command_recorder_mutex)){nvn_command_recorder_stats.contended++;pthread_mutex_lock(&nvn_command_recorder_mutex);}
    nvn_command_recorder_stats.locks++;
    if(guest_thread_index<128)nvn_command_recorder_stats.threads[guest_thread_index/64]|=UINT64_C(1)<<(guest_thread_index%64);
}
static void nvn_command_recorder_unlock(void) {
    if(--nvn_command_recorder_held)return;
    pthread_mutex_unlock(&nvn_command_recorder_mutex);
}
static unsigned nvn_command_recorder_threads(void) {
    return (unsigned)(__builtin_popcountll(nvn_command_recorder_stats.threads[0])+__builtin_popcountll(nvn_command_recorder_stats.threads[1]));
}
static uint64_t aw_clear_texture_count;
static void aw_clear_texture_admit(uint64_t object,uint64_t view,const int32_t *region);
#include "nvn_command_lists.h"
static struct nvn_command_buffer *nvn_command_find(uint64_t object) {
    for(unsigned i=0;i<32;i++)if(nvn_command_buffers[i].live&&nvn_command_buffers[i].object==object)return &nvn_command_buffers[i];
    stop("NVN_COMMAND_BUFFER_INVALID",nvn_caller,object,24);return NULL;
}
static unsigned nvn_command_shape(const struct nvn_record *r,unsigned *vectors,unsigned *payload) {
    const uint64_t *a=r->args;*vectors=0;*payload=0;
    switch(r->op) {
    case NI_nvnCommandBufferSetAlphaRef:case NI_nvnCommandBufferSetDepthRange:case NI_nvnCommandBufferClearDepthStencil:
    case NI_nvnCommandBufferSetPolygonOffsetClamp:case NI_nvnCommandBufferSetLineWidth:case NI_nvnCommandBufferSetPointSize:
    case NI_nvnCommandBufferSetDepthBounds:
        *vectors=sizeof(r->vectors);break;
    case NI_nvnCommandBufferBindColorState:case NI_nvnCommandBufferBindChannelMaskState:case NI_nvnCommandBufferBindPolygonState:*payload=4;break;
    case NI_nvnCommandBufferBindBlendState:case NI_nvnCommandBufferBindDepthStencilState:*payload=8;break;
    case NI_nvnCommandBufferBindMultisampleState:*payload=24;break;
    case NI_nvnCommandBufferClearColor:case NI_nvnCommandBufferSetBlendColor:*payload=16;break;
    case NI_nvnCommandBufferUpdateUniformBuffer:
        if(a[2]>a[1] || a[3]>a[1]-a[2] || a[3]>sizeof(r->payload) || (a[0]|a[2]|a[3])%4)
            stop("NVN_UNIFORM_UPDATE_RANGE",nvn_caller,a[3],24);
        *payload=a[3];break;
    case NI_nvnCommandBufferCopyBufferToBuffer:
        /* Source, destination and size live in the args; nothing has to be snapshotted. Bit 1 is the same
         * engine hint CopyBufferToTexture carries and does not change the copied bytes. */
        if(a[3]&~UINT64_C(2))stop("NVN_COPY_BUFFER_FLAGS_REQUIRED",nvn_caller,a[3],20);
        *payload=0;break;
    case NI_nvnCommandBufferCopyBufferToTexture:
        /* Bit 1 is the engine hint (copy engine vs 3D); it does not change the copied bytes, and the
         * Messenger host admits it the same way (nvn_commands.h:50 @ 90cbe57). Unity 2021 always sets
         * it for its staging uploads (MEDIDO run momo-031). */
        if(a[4]&~UINT64_C(2))stop("NVN_COPY_TEXTURE_VIEW_FLAGS_REQUIRED",nvn_caller,a[4],20);
        *payload=24+(a[2]?MEGAMAN_TEXTURE_VIEW_BYTES:0);break;
#if ZERO_ZX_GAME
    case NI_nvnCommandBufferCopyTextureToBuffer:
        if(a[4])stop("ZERO_READBACK_FLAGS_REQUIRED",nvn_caller,a[4],20);
        *payload=24+(a[1]?MEGAMAN_TEXTURE_VIEW_BYTES:0);break;
#endif
    case NI_nvnCommandBufferCopyTextureToTexture:
        *payload=48+(a[1]?MEGAMAN_TEXTURE_VIEW_BYTES:0)+(a[4]?MEGAMAN_TEXTURE_VIEW_BYTES:0);break;
    case NI_nvnCommandBufferBindVertexAttribState:case NI_nvnCommandBufferBindVertexStreamState:
        if(a[0]>16)stop("NVN_VERTEX_STATE_COUNT",nvn_caller,a[0],24);
        *payload=a[0]*(r->op==NI_nvnCommandBufferBindVertexAttribState?4:8);break;
    case NI_nvnCommandBufferBindVertexBuffers:
        if(a[0]>16 || a[1]>16-a[0])stop("NVN_VERTEX_BUFFER_COUNT",nvn_caller,a[1],24);
        *payload=a[1]*16;break;
    case NI_nvnCommandBufferBindUniformBuffers:
        if(a[0]>1 || a[1]>16 || a[2]>16-a[1])stop("NVN_UNIFORM_BUFFER_COUNT",nvn_caller,a[2],24);
        *payload=a[2]*16;break;
    case NI_nvnCommandBufferBindUniformBuffersDeferred:
        /* Terminator 2D: same (stage, first, count, ranges) shape as BindUniformBuffers, but the ranges
         * are read when the command executes, so the record keeps only the guest pointer (a[3]). */
        if(a[0]>1 || a[1]>16 || a[2]>16-a[1])stop("NVN_UNIFORM_BUFFER_COUNT",nvn_caller,a[2],24);
        *payload=0;break;
    case NI_nvnCommandBufferBindTexture:
    /* Unity 2021 binds textures and samplers separately (MEDIDO run momo-058): the separate handles
     * carry the texture index in the low word and the sampler index in the high word, so the pair
     * rebuilds exactly the combined handle this host already understands. */
    case NI_nvnCommandBufferBindSeparateTexture:case NI_nvnCommandBufferBindSeparateSampler:
        /* Stages above fragment (geometry/tessellation/compute) do not exist on this host and cannot
         * affect a GLES2 draw; Unity still binds to the compute stage (MEDIDO run momo-059). */
        if(a[0]>5 || a[1]>=128)stop("NVN_TEXTURE_BINDING_REQUIRED",nvn_caller,a[1],20);
        break;
    case NI_nvnCommandBufferSetRenderTargets:
#if ZERO_ZX_GAME
        if(a[0]>1)stop("NVN_RENDER_TARGET_COUNT_REQUIRED",nvn_caller,a[0],20);
        *payload=16+2*MEGAMAN_TEXTURE_VIEW_BYTES;break;
#else
        if(a[0]>1 || a[4])stop("NVN_RENDER_TARGET_VIEWS_REQUIRED",nvn_caller,a[0],20);
        *payload=16;break;
#endif
    case NI_nvnCommandBufferReportCounter:case NI_nvnCommandBufferSetSamplerPool:case NI_nvnCommandBufferSetTexturePool:
    case NI_nvnCommandBufferSetViewport:case NI_nvnCommandBufferSetScissor:case NI_nvnCommandBufferBarrier:
    case NI_nvnCommandBufferSetCopyRowStride:case NI_nvnCommandBufferSetCopyImageStride:case NI_nvnCommandBufferBindProgram:
    case NI_nvnCommandBufferBindVertexBuffer:case NI_nvnCommandBufferBindUniformBuffer:
    case NI_nvnCommandBufferDrawArrays:case NI_nvnCommandBufferDrawElements:case NI_nvnCommandBufferDrawElementsBaseVertex:
    case NI_nvnCommandBufferSetStencilRef:case NI_nvnCommandBufferSetStencilMask:case NI_nvnCommandBufferSetStencilValueMask:
    case NI_nvnCommandBufferSetShaderScratchMemory:case NI_nvnCommandBufferSetPrimitiveRestart:break;
    /* ZCull save/restore copies only Tegra hierarchical-cull metadata; this backend reports zero bytes of
     * external storage, so the call carries size 0 and nothing is copied. Shape from
     * the-messenger-switch-study/native/nvn_commands.h:80 @ 90cbe57; Momodora passes a non-null unused
     * address with size 0 (MEDIDO run momo-029). */
    case NI_nvnCommandBufferSaveZCullData:case NI_nvnCommandBufferRestoreZCullData:
        if(a[1])stop("NVN_EXTERNAL_ZCULL_STORAGE_REQUIRED",nvn_caller,a[1],20);   /* a[0] address, a[1] size */
        break;
    case NI_nvnCommandBufferSetDepthClamp:case NI_nvnCommandBufferSetConservativeRasterEnable:
    case NI_nvnCommandBufferSetRasterizerDiscard:break;
    default:stop("NVN_COMMAND_RECORD_OPCODE",nvn_caller,r->op,24);
    }
    return (sizeof(struct nvn_record_prefix)+*vectors+*payload+15)&~15U;
}
static void *nvn_command_storage(struct nvn_command_buffer *c,uint64_t address,unsigned bytes,unsigned permission) {
    if(nvn_redirect_current){if(address%16)stop("NVN_COMMAND_RECORD_ALIGNMENT",nvn_caller,address,24);return nvn_pointer(address,bytes,permission);}
    if(address%16)stop("NVN_COMMAND_RECORD_ALIGNMENT",nvn_caller,address,24);
    for(unsigned i=0;i<c->command_count;i++) {
        struct nvn_command_arena *a=&c->command[i];
        if(a->used>a->size || !a->pool || !(a->pool->live||a->pool->retired) || !a->pool->references)continue;
        unsigned retained=address>=c->command_retained[i].first && address<=c->command_retained[i].last &&
            bytes<=c->command_retained[i].last-address;
        if((address>=a->address && address-a->address<=a->used && bytes<=a->used-(address-a->address)) || retained)
            return nvn_pointer(address,bytes,permission);
    }
    /* Terminator 2D: full picture of the recorder at the failure, then the census in stop(). */
    printf("T2_COMMAND_STORAGE_MISS buffer=0x%"PRIx64" thread=%u call=%s address=0x%"PRIx64" bytes=%u recording=%u list=%"PRIu64" first=0x%"PRIx64" last=0x%"PRIx64" count=%"PRIu64" arenas=%u controls=%u callback=0x%"PRIx64"\n",
        c->object,guest_thread_index,nvn_command_current_id<NI_COUNT?nvn_functions[nvn_command_current_id].name:"?",address,bytes,c->recording,
        c->list?c->list->handle:0,c->list?c->list->first:0,c->list?c->list->last:0,c->list?c->list->count:0,c->command_count,c->control_count,c->callback);
    for(unsigned i=0;i<c->command_count;i++) {
        const struct nvn_command_arena *a=&c->command[i];
        printf("T2_COMMAND_ARENA index=%u address=0x%"PRIx64" size=%"PRIu64" used=%"PRIu64" pool=0x%"PRIx64" pool-live=%u pool-retired=%u pool-references=%u retained=0x%"PRIx64"..0x%"PRIx64"\n",
            i,a->address,a->size,a->used,a->pool?a->pool->address:0,a->pool?a->pool->live:0,a->pool?a->pool->retired:0,a->pool?a->pool->references:0,
            c->command_retained[i].first,c->command_retained[i].last);
    }
    for(unsigned i=0;i<c->control_count;i++)
        printf("T2_CONTROL_ARENA index=%u address=0x%"PRIx64" size=%"PRIu64" used=%"PRIu64"\n",i,c->control[i].address,c->control[i].size,c->control[i].used);
    for(unsigned b=0;b<32;b++) {
        const struct nvn_command_buffer *o=&nvn_command_buffers[b];if(!o->live)continue;
        for(unsigned i=0;i<o->command_count;i++)
            if(address>=o->command[i].address && address-o->command[i].address<o->command[i].size)
                printf("T2_COMMAND_STORAGE_OWNER address=0x%"PRIx64" belongs-to-buffer=0x%"PRIx64" arena=%u used=%"PRIu64" same-buffer=%u\n",
                    address,o->object,i,o->command[i].used,o==c);
    }
    stop("NVN_COMMAND_RECORD_STORAGE",nvn_caller,address,24);return NULL;
}
static void nvn_command_decode(struct nvn_command_list *list,uint64_t address,struct nvn_record *r) {
    if(!list->live)stop("NVN_COMMAND_HANDLE_INVALID",nvn_caller,list->handle,24);
    struct nvn_command_buffer *c=nvn_redirect_current?NULL:nvn_command_find(list->owner);struct nvn_record_prefix prefix;
    /* Validate the fixed prefix before reading its variable-size tail. */
    memcpy(&prefix,nvn_command_storage(c,address,sizeof(prefix),PROT_READ),sizeof(prefix));
    if(prefix.bytes<80 || prefix.bytes>sizeof(*r) || prefix.bytes%16)
        stop("NVN_COMMAND_RECORD_SIZE",nvn_caller,prefix.bytes,24);
    memset(r,0,sizeof(*r));r->next=prefix.next;r->op=prefix.op;r->bytes=prefix.bytes;memcpy(r->args,prefix.args,sizeof(r->args));
    unsigned vectors,payload,bytes=nvn_command_shape(r,&vectors,&payload);
    if(bytes!=prefix.bytes)stop("NVN_COMMAND_RECORD_SIZE",nvn_caller,prefix.bytes,24);
    const uint8_t *wire=nvn_command_storage(c,address,bytes,PROT_READ);
    if(r->next) {
        if(r->next==address)stop("NVN_COMMAND_RECORD_CYCLE",nvn_caller,address,24);
        nvn_command_storage(c,r->next,sizeof(prefix),PROT_READ);
    }
    if(vectors)memcpy(r->vectors,wire+sizeof(prefix),vectors);
    if(payload)memcpy(r->payload,wire+sizeof(prefix)+vectors,payload);
}
static unsigned nvn_command_list_uses_arena_walk(struct nvn_command_buffer *c,const struct nvn_command_list *list,const struct nvn_command_arena *arena);
/* FRENTE-P: T2_LIST_RANGES=0 -> always walk (old behaviour); =verify -> compute both, stop on any difference. */
static unsigned t2_list_ranges_mode(void) {
    static int m=-1;if(m<0){const char *o=getenv("T2_LIST_RANGES");m=o&&!strcmp(o,"0")?0:o&&!strcmp(o,"verify")?2:1;
        printf("T2_LIST_RANGES mode=%s\n",m==0?"walk":m==2?"verify":"ranges");}
    return (unsigned)m;
}
static unsigned nvn_command_list_uses_arena(struct nvn_command_buffer *c,const struct nvn_command_list *list,const struct nvn_command_arena *arena) {
    unsigned mode=t2_list_ranges_mode();
    if(!mode || list->t2_range_overflow || nvn_redirect_current)return nvn_command_list_uses_arena_walk(c,list,arena);
    unsigned used=list->control>=arena->address && list->control-arena->address<arena->size;
    for(unsigned i=0;i<list->t2_ranges && !used;i++)
        used=list->t2_range_first[i]<arena->address+arena->size && arena->address<list->t2_range_end[i];
    if(mode==2 && used!=nvn_command_list_uses_arena_walk(c,list,arena))stop("T2_LIST_RANGES_MISMATCH",nvn_caller,list->handle,24);
    return used;
}
static unsigned nvn_command_list_uses_arena_walk(struct nvn_command_buffer *c,const struct nvn_command_list *list,const struct nvn_command_arena *arena) {
    if(list->control>=arena->address && list->control-arena->address<arena->size)return 1;
    uint64_t next=list->first;
    for(uint64_t i=0;i<list->count;i++) {
        if(next>=arena->address && next-arena->address<arena->size)return 1;
        uint64_t following;memcpy(&following,nvn_command_storage(c,next,8,PROT_READ),8);next=following;
    }
    return 0;
}
static void nvn_command_reuse_arena(struct nvn_command_buffer *c,struct nvn_command_arena *arena) {
    /* A caller reuses only this byte range. Completed lists in other arenas
     * remain replayable; an unsubmitted list can be discarded by its owner.
     * Pending execution must still finish or own an independent snapshot. */
    struct nvn_command_list *active=c->recording?c->list:NULL;
    unsigned command=0;while(command<c->command_count && &c->command[command]!=arena)command++;
    uint64_t retained_first=UINT64_MAX,retained_last=0;
    if(active && nvn_command_list_uses_arena(c,active,arena)) {
        if(command==c->command_count)stop("NVN_CONTROL_CALLBACK_ACTIVE_OVERLAP",nvn_caller,active->control,24);
        uint64_t next=active->first;
        for(uint64_t i=0;i<active->count;i++) {
            struct nvn_record record;nvn_command_decode(active,next,&record);
            if(next>=arena->address && next-arena->address<arena->size) {
                if(next<retained_first)retained_first=next;
                if(next+record.bytes>retained_last)retained_last=next+record.bytes;
            }
            next=record.next;
        }
    }
    NVN_FOR_EACH_COMMAND_LIST(list)if(list!=active && list->live && list->owner==c->object &&
        nvn_command_list_uses_arena(c,list,arena) && list->submitted && !list->completed) {
        if(nvn_command_pending_poll)nvn_command_pending_poll(list->handle);
        if(!list->completed)stop("NVN_COMMAND_REUSE_BEFORE_COMPLETION",nvn_caller,list->handle,24);
    }
    NVN_FOR_EACH_COMMAND_LIST(list)if(list!=active && list->live && list->owner==c->object &&
        nvn_command_list_uses_arena(c,list,arena))list->live=0;
    if(c->list && !c->list->live)c->list=NULL;
    if(command<c->command_count) {
        c->command_retained[command].first=retained_last?retained_first:0;
        c->command_retained[command].last=retained_last;
    }
    arena->used=0;
    c->memory_generation++;
}
/* Terminator 2D (MEDIDO device run 202, runtime.log 12366/15118/17904): Unity 6 keeps ONE ring of 0x80000
 * command chunks in a pool and hands a chunk to whichever command buffer runs out next - chunk 0x154e80000
 * went to buffer 0x14a30e7c8 and later to buffer 0x14b8f5338. NVN itself only writes into a buffer's
 * current chunk and forgets the older ones; this recorder kept every chunk as an arena of its first
 * owner, together with that owner's completed lists. When the first owner's callback re-added one of its
 * chunks, nvn_command_reuse_arena walked those lists through records the second buffer had overwritten
 * and read `next`=0xa0 (STOP NVN_COMMAND_RECORD_STORAGE detail=0xa0). Handing a chunk to another buffer
 * now takes it away from the old owner: the owner's lists that live in it are dropped (after the same
 * completion rule as a reuse), and the arena leaves the owner's table. */
static void t2_command_arena_remove(struct nvn_command_buffer *o,unsigned index) {
    struct nvn_pool *p=o->command[index].pool;if(p&&!--p->references)p->retired=0;
    for(unsigned i=index;i+1<o->command_count;i++){o->command[i]=o->command[i+1];o->command_retained[i]=o->command_retained[i+1];}
    unsigned before=o->command_count,current=o->current;
    o->command_count--;o->command[o->command_count]=(struct nvn_command_arena){0};
    o->command_retained[o->command_count].first=o->command_retained[o->command_count].last=0;
    o->current=current>=before||current==index?o->command_count:current>index?current-1:current;
}
/* Drops every list of `o` that has records (or its control header) in arena `index`; returns how many.
 * `eviction`: evicting for capacity must not drop a list the game has not submitted yet. */
static unsigned t2_command_arena_release(struct nvn_command_buffer *o,unsigned index,unsigned eviction) {
    struct nvn_command_arena *arena=&o->command[index];unsigned dropped=0;
    if(o->recording && o->list && nvn_command_list_uses_arena(o,o->list,arena)) {
        if(eviction)return UINT32_MAX;
        stop("T2_COMMAND_ARENA_TRANSFER_WHILE_RECORDING",nvn_caller,arena->address,24);
    }
    NVN_FOR_EACH_COMMAND_LIST(list)if(list->live && list->owner==o->object && nvn_command_list_uses_arena(o,list,arena)) {
        if(eviction && (!list->submitted || !list->completed))return UINT32_MAX;
        if(list->submitted && !list->completed) {
            if(nvn_command_pending_poll)nvn_command_pending_poll(list->handle);
            if(!list->completed)stop("T2_COMMAND_ARENA_TRANSFER_BEFORE_COMPLETION",nvn_caller,list->handle,24);
        }
    }
    NVN_FOR_EACH_COMMAND_LIST(list)if(list->live && list->owner==o->object && (!o->recording || list!=o->list) &&
        nvn_command_list_uses_arena(o,list,arena)){list->live=0;dropped++;}
    if(o->list && !o->list->live)o->list=NULL;
    return dropped;
}
static uint64_t t2_command_transfers;
static void t2_command_arena_transfer(struct nvn_command_buffer *c,uint64_t address,uint64_t size) {
    for(unsigned b=0;b<32;b++) {
        struct nvn_command_buffer *o=&nvn_command_buffers[b];if(!o->live || o==c)continue;
        for(unsigned i=0;i<o->command_count;i++) {
            struct nvn_command_arena *p=&o->command[i];
            if(!(address<p->address+p->size && p->address<address+size))continue;
            if(p->address!=address || p->size!=size)stop("NVN_COMMAND_ARENA_OVERLAP",nvn_caller,address,24);
            uint64_t used=p->used;unsigned dropped=t2_command_arena_release(o,i,0);
            t2_command_arena_remove(o,i);t2_command_transfers++;
            if(t2_command_transfers<=8 || !(t2_command_transfers&(t2_command_transfers-1)))
                printf("T2_COMMAND_ARENA_TRANSFER address=0x%"PRIx64" size=%"PRIu64" from=0x%"PRIx64" to=0x%"PRIx64" old-used=%"PRIu64" lists-dropped=%u count=%"PRIu64"\n",
                    address,size,o->object,c->object,used,dropped,t2_command_transfers);
            break;
        }
    }
}
/* Bench negative control: T2_COMMAND_ARENA_LEGACY=1 restores the pre-fix policy (no transfer, records in any
 * arena with room, sizes summed) so the same binary reproduces the device stop. Never set on the device. */
static unsigned t2_command_arena_legacy(void) {
    static int configured;static unsigned legacy;
    if(!configured){const char *o=getenv("T2_COMMAND_ARENA_LEGACY");legacy=o&&!strcmp(o,"1");
        if(legacy)printf("T2_COMMAND_ARENA_LEGACY enabled=1 scope=bench-negative-control\n");
        configured=1;}
    return legacy;
}
static uint64_t t2_command_arena_cap(void) {
    /* Bench only: T2_COMMAND_ARENA_CAP_BYTES=<n> lets the recorder fill only n bytes of each command chunk, so
     * the game's out-of-memory callback (and the chunk ring) turns over in minutes instead of scenes. */
    static int configured;static uint64_t cap;
    if(!configured){const char *o=getenv("T2_COMMAND_ARENA_CAP_BYTES");cap=o?strtoull(o,NULL,0)&~UINT64_C(15):0;
        if(o&&cap<1024)stop("T2_COMMAND_ARENA_CAP_OPTION",nvn_caller,cap,24);
        if(cap)printf("T2_COMMAND_ARENA_CAP bytes=%"PRIu64" scope=bench-only\n",cap);
        configured=1;}
    return cap;
}
#include "nvn_command_callback.h"
static void nvn_command_record_inner(struct nvn_command_buffer *c,unsigned id,struct nvn_frame *f);
static void nvn_command_record(struct nvn_command_buffer *c,unsigned id,struct nvn_frame *f) {
    /* A uniform update larger than one record is split into adjacent ordered 256-byte records: the
     * same original bytes, in the same order, and no draw can sit between them. Shape from
     * the-messenger-switch-study/native/nvn_commands.h:164 @ 90cbe57 (Unity 2021 updates 272 bytes at
     * once here, MEDIDO run momo-098). */
    if(id==NI_nvnCommandBufferUpdateUniformBuffer && f->args[4]>sizeof(((struct nvn_record *)0)->payload)) {
        const uint64_t *a=f->args;
        if(a[3]>a[2] || a[4]>a[2]-a[3] || a[4]>65536 || (a[1]|a[3]|a[4])%4)
            stop("NVN_UNIFORM_UPDATE_RANGE",nvn_caller,a[4],24);
        (void)nvn_pointer(a[5],a[4],PROT_READ);
        for(uint64_t offset=0;offset<a[4];offset+=256) {
            struct nvn_frame part=*f;uint64_t bytes=a[4]-offset;if(bytes>256)bytes=256;
            part.args[3]=a[3]+offset;part.args[4]=bytes;part.args[5]=a[5]+offset;
            nvn_command_record_inner(c,id,&part);
        }
        static unsigned large_updates;
        if(++large_updates<=8)printf("MOMO_UNIFORM_UPDATE bytes=%"PRIu64" chunks=%"PRIu64" all-original-bytes=1\n",a[4],(a[4]+255)/256);
        return;
    }
    if(!nvn_phase.enabled){nvn_command_record_inner(c,id,f);return;}
    uint64_t t0=nvn_phase_now();nvn_command_record_inner(c,id,f);nvn_record_profile.ns+=nvn_phase_now()-t0;nvn_record_profile.calls++;
}
static void nvn_command_emit(struct nvn_command_buffer *c,unsigned id,const struct nvn_record *snapshot,unsigned size,unsigned vectors,unsigned bytes,struct nvn_frame *f);
/* Lazy ClearTexture restore: the render targets that were current before a synthesized clear are recorded again
 * only if a command other than SetRenderTargets follows (the game sets its next pass targets right after; every
 * skipped restore is one FBO switch fewer on the Mali tiler: 31 clears per frame measured). */
static void aw_restore_flush(struct nvn_command_buffer *c,unsigned next_id,struct nvn_frame *f) {
    if(!c->aw_restore_pending)return;
    c->aw_restore_pending=0;
    if(next_id==NI_nvnCommandBufferSetRenderTargets)return;
    struct nvn_record r=c->aw_restore_targets;unsigned v,b;unsigned size=nvn_command_shape(&r,&v,&b);nvn_command_emit(c,r.op,&r,size,v,b,f);
}
static void nvn_command_emit(struct nvn_command_buffer *c,unsigned id,const struct nvn_record *snapshot,unsigned size,unsigned vectors,unsigned bytes,struct nvn_frame *f) {
    aw_restore_flush(c,id,f);
    if(id==NI_nvnCommandBufferSetRenderTargets){c->last_targets=*snapshot;c->last_targets_valid=1;}
    if(id==NI_nvnCommandBufferSetScissor){c->last_scissor=*snapshot;c->last_scissor_valid=1;}
    unsigned active_overlap=0;
    for(unsigned attempt=0;attempt<2;attempt++) {
      active_overlap=0;
      unsigned legacy=t2_command_arena_legacy();
      for(unsigned i=legacy?0:c->current;i<c->command_count && (legacy || i==c->current);i++) {
        struct nvn_command_arena *a=&c->command[i];
        if(a->used>a->limit)stop("NVN_COMMAND_ARENA_USED",nvn_caller,a->used,24);
        if(size>a->limit-a->used)continue;
        uint64_t start=a->address+a->used;
        if(start<c->command_retained[i].last && c->command_retained[i].first<start+size){active_overlap=1;continue;}
        uint8_t *record=nvn_pointer(a->address+a->used,size,PROT_WRITE);memset(record,0,size);
        struct nvn_record_prefix prefix={.op=id,.bytes=size};memcpy(prefix.args,snapshot->args,sizeof(prefix.args));
        memcpy(record,&prefix,sizeof(prefix));
        if(vectors)memcpy(record+sizeof(prefix),snapshot->vectors,vectors);
        if(bytes)memcpy(record+sizeof(prefix)+vectors,snapshot->payload,bytes);
        a->used+=size;uint64_t address=(uintptr_t)record;
        {   struct nvn_command_list *l=c->list;unsigned n=l->t2_ranges; /* FRENTE-P: record range of the list */
            if(n && l->t2_range_end[n-1]==address)l->t2_range_end[n-1]=address+size;
            else if(n<8){l->t2_range_first[n]=address;l->t2_range_end[n]=address+size;l->t2_ranges=n+1;}
            else l->t2_range_overflow=1;}
        if(c->list->last)memcpy(nvn_command_storage(c,c->list->last,8,PROT_WRITE),&address,8);
        else c->list->first=address;
        c->list->last=address;c->list->count++;c->list->recorded_bytes+=size;return;
      }
      if(!attempt)nvn_command_request_memory(c,0,size,f);
    }
    printf("NVN_COMMAND_STORAGE_EXHAUSTED records=%"PRIu64" used=%"PRIu64" next-bytes=%u arenas=%u callback=0x%"PRIx64"\n",
        c->list->count,c->list->recorded_bytes,size,c->command_count,c->callback);
    stop(active_overlap?"NVN_COMMAND_CALLBACK_ACTIVE_OVERLAP":"NVN_COMMAND_MEMORY_CALLBACK_INSUFFICIENT",nvn_caller,size,20);
}
static void nvn_command_record_inner(struct nvn_command_buffer *c,unsigned id,struct nvn_frame *f) {
    if(!c->recording || !c->list)stop("NVN_COMMAND_NOT_RECORDING",nvn_caller,c->object,24);
    struct nvn_record snapshot={.op=id,.bytes=sizeof(snapshot)};
    memcpy(snapshot.args,f->args+1,sizeof(snapshot.args));memcpy(snapshot.vectors,f->vectors,sizeof(snapshot.vectors));
    unsigned vectors,bytes,size=nvn_command_shape(&snapshot,&vectors,&bytes);uint64_t source=f->args[1];
    switch(id) {
    case NI_nvnCommandBufferBindProgram:
        if(f->args[2]>3 && !(!f->args[1] && !(f->args[2]&~UINT64_C(0x3c))))
            stop("NVN_PROGRAM_STAGE_MASK_REQUIRED",nvn_caller,f->args[2],20);
        snapshot.args[2]=nvn_program_record_id(f->args[1]);break;
    case NI_nvnCommandBufferClearColor:source=f->args[2];break;
    case NI_nvnCommandBufferUpdateUniformBuffer:
        /* SDK copies the update bytes into the command stream immediately;
         * the GPU-visible buffer changes only when that command executes. */
        source=f->args[5];break;
    case NI_nvnCommandBufferCopyBufferToTexture:
        memcpy(snapshot.payload,nvn_pointer(f->args[4],24,PROT_READ),24);
        if(f->args[3])memcpy(snapshot.payload+24,nvn_pointer(f->args[3],MEGAMAN_TEXTURE_VIEW_BYTES,PROT_READ),MEGAMAN_TEXTURE_VIEW_BYTES);
        /* Frente F (MEDIDO run build-22): Unity records a 2x2 upload into a placeholder texture, finalizes it
         * and reuses the same NVNtexture object for the 4096x4096 page before the list runs; resolving the
         * copy by object address at replay then writes the 2x2 into the page. The identity recorded here
         * lets the replay drop a copy whose destination object was reused (nvn_replay.h). */
        snapshot.args[6]=zero_texture_record_id(f->args[2]);
        t2_page_record_copy(nvn_texture_find(f->args[2]),f->args[1],snapshot.payload,f->args[3]!=0); /* ROOT */
        break;
#if ZERO_ZX_GAME
    case NI_nvnCommandBufferCopyTextureToBuffer:
        memcpy(snapshot.payload,nvn_pointer(f->args[3],24,PROT_READ),24);
        if(f->args[2]) {
            memcpy(snapshot.payload+24,nvn_pointer(f->args[2],MEGAMAN_TEXTURE_VIEW_BYTES,PROT_READ),MEGAMAN_TEXTURE_VIEW_BYTES);
            zero_record_target_view(f->args[1],snapshot.payload+24,0);
        }
        snapshot.args[1]=!!f->args[2];snapshot.args[5]=zero_texture_record_id(f->args[1]);snapshot.args[6]=0;
        break;
#endif
    case NI_nvnCommandBufferCopyTextureToTexture: {
        memcpy(snapshot.payload,nvn_pointer(f->args[3],24,PROT_READ),24);
        memcpy(snapshot.payload+24,nvn_pointer(f->args[6],24,PROT_READ),24);
        unsigned at=48;
        if(f->args[2]){memcpy(snapshot.payload+at,nvn_pointer(f->args[2],MEGAMAN_TEXTURE_VIEW_BYTES,PROT_READ),MEGAMAN_TEXTURE_VIEW_BYTES);at+=MEGAMAN_TEXTURE_VIEW_BYTES;}
        if(f->args[5])memcpy(snapshot.payload+at,nvn_pointer(f->args[5],MEGAMAN_TEXTURE_VIEW_BYTES,PROT_READ),MEGAMAN_TEXTURE_VIEW_BYTES);
        break;
    }
    case NI_nvnCommandBufferBindVertexAttribState:case NI_nvnCommandBufferBindVertexStreamState:
        source=f->args[2];break;
    case NI_nvnCommandBufferBindVertexBuffers:
        source=f->args[3];break;
    case NI_nvnCommandBufferBindUniformBuffers:
        source=f->args[4];break;
    case NI_nvnCommandBufferSetRenderTargets: {
#if !ZERO_ZX_GAME
        if(f->args[1] && f->args[3] && *(uint64_t *)nvn_pointer(f->args[3],8,PROT_READ))
            stop("NVN_RENDER_TARGET_VIEWS_REQUIRED",nvn_caller,f->args[1],20);
#endif
        uint64_t attachments[2]={0,f->args[4]};if(f->args[1])memcpy(attachments,nvn_pointer(f->args[2],8,PROT_READ),8);
        memcpy(snapshot.payload,attachments,16);
#if ZERO_ZX_GAME
        uint64_t views[2]={0,f->args[5]};
        if(f->args[1] && f->args[3])memcpy(views,nvn_pointer(f->args[3],8,PROT_READ),8);
        for(unsigned i=0;i<2;i++) {
            snapshot.args[i?4:2]=!!views[i];
            if(!views[i])continue;
            const uint8_t *view=nvn_pointer(views[i],MEGAMAN_TEXTURE_VIEW_BYTES,PROT_READ);
            memcpy(snapshot.payload+16+i*MEGAMAN_TEXTURE_VIEW_BYTES,view,MEGAMAN_TEXTURE_VIEW_BYTES);
            zero_record_target_view(attachments[i],snapshot.payload+16+i*MEGAMAN_TEXTURE_VIEW_BYTES,i);
        }
#endif
        break;
    }
    default:break;
    }
    if(bytes && id!=NI_nvnCommandBufferSetRenderTargets && id!=NI_nvnCommandBufferCopyBufferToTexture && id!=NI_nvnCommandBufferCopyTextureToTexture && id!=NI_nvnCommandBufferCopyTextureToBuffer) {
        memcpy(snapshot.payload,nvn_pointer(source,bytes,PROT_READ),bytes);
    }
    nvn_command_emit(c,id,&snapshot,size,vectors,bytes,f);
}
static unsigned nvn_command_dispatch_locked(unsigned id,struct nvn_frame *f,uint64_t *result);
static unsigned nvn_command_dispatch(unsigned id,struct nvn_frame *f,uint64_t *result) {
    /* Every command-buffer API call runs under the recorder mutex (see nvn_command_recorder_mutex); the
     * ids this function does not handle return 0 quickly, so the lock is cheap for the rest of the API. */
    nvn_command_recorder_lock();
    unsigned saved_id=nvn_command_current_id;nvn_command_current_id=id;
    unsigned handled=nvn_command_dispatch_locked(id,f,result);
    nvn_command_current_id=saved_id;
    nvn_command_recorder_unlock();
    return handled;
}
static unsigned nvn_command_dispatch_locked(unsigned id,struct nvn_frame *f,uint64_t *result) {
    uint64_t *a=f->args;*result=0;
    if(id==NI_nvnCommandBufferInitialize) {
        nvn_pointer(a[0],8,PROT_READ|PROT_WRITE);
        if(!nvn_device.live||a[1]!=nvn_device.address)stop("NVN_COMMAND_DEVICE",nvn_caller,a[1],24);
        unsigned slot=32;
        for(unsigned i=0;i<32;i++) {
            if(nvn_command_buffers[i].live && nvn_command_buffers[i].object==a[0])stop("NVN_COMMAND_ALREADY_LIVE",nvn_caller,a[0],24);
            if(!nvn_command_buffers[i].live && slot==32)slot=i;
        }
        if(slot==32)stop("NVN_COMMAND_CAPACITY",nvn_caller,slot,21);
        nvn_command_buffers[slot]=(struct nvn_command_buffer){.object=a[0],.log_lifetime=nvn_command_log_lifetime()};
        __atomic_store_n(&nvn_command_buffers[slot].live,1,__ATOMIC_RELEASE); /* replay finds buffers without the lock */
        static unsigned told;
        if(told++<8)printf("T2_COMMAND_BUFFER_INITIALIZED object=0x%"PRIx64" slot=%u thread=%u recording-threads=%u\n",a[0],slot,guest_thread_index,nvn_command_recorder_threads());
        *result=1;return 1;
    }
    if(id==NI_nvnCommandBufferBeginRecording || id==NI_nvnCommandBufferEndRecording) {
        struct nvn_command_buffer *c=nvn_command_find(a[0]);
        if(id==NI_nvnCommandBufferEndRecording)aw_restore_flush(c,0,f);
        if(id==NI_nvnCommandBufferBeginRecording) {
            /* Unity 2021 begins recording with no arena at all and relies on the memory callback to
             * supply both command and control storage on demand (MEDIDO run momo-022); the loop below
             * already asks for control memory, and the command path asks when a record needs room. */
            if(c->recording || (!c->callback && (!c->command_count || !c->control_count)))
                stop("NVN_COMMAND_BEGIN_STATE",nvn_caller,a[0],24);
            c->recording=1;c->list=NULL;
            uint64_t control=0;
            for(unsigned attempt=0;attempt<2&&!control;attempt++) {
                for(unsigned i=0;i<c->control_count;i++) {
                    if(c->control[i].used>c->control[i].size)stop("NVN_CONTROL_ARENA_USED",nvn_caller,c->control[i].used,24);
                    if(c->control[i].size-c->control[i].used>=32) {
                        control=c->control[i].address+c->control[i].used;c->control[i].used+=32;break;
                    }
                }
                if(!control&&!attempt)nvn_command_request_memory(c,1,256,f);
            }
            if(!control)stop("NVN_CONTROL_MEMORY_CALLBACK_INSUFFICIENT",nvn_caller,32,20);
            c->list=nvn_command_list_allocate();*c->list=(struct nvn_command_list){.handle=++nvn_next_command_handle,.control=control,.owner=a[0],.log_lifetime=c->log_lifetime,.live=1};c->recording=1;
        } else {
            if(!c->recording || !c->list)stop("NVN_COMMAND_END_STATE",nvn_caller,a[0],24);
            *result=c->list->handle;c->recording=0;
            uint64_t header[4]={c->list->handle,c->list->first,c->list->last,c->list->count};
            memcpy(nvn_pointer(c->list->control,sizeof(header),PROT_WRITE),header,sizeof(header));
            static _Thread_local struct frame_log_counter recorded_logs;
            static _Thread_local struct nvn_command_log_cache recorded_cache;
            struct frame_log_event log=nvn_command_log_sample(&recorded_cache,&recorded_logs,c->list->owner,c->list->count,c->log_lifetime,frame_log_full_commands());
            if(log.emit&&!aw_release_mode()) {
                if(nvn_command_log_enabled)printf("NVN_COMMANDS_RECORDED handle=%"PRIu64" count=%"PRIu64" replay-pending=1 occurrence=%"PRIu64" suppressed=%"PRIu64" log-thread=%u log-scope=thread-owner-count-lifetime changed=%u owner=0x%"PRIx64" lifetime=%"PRIu64" channel-calls=%"PRIu64" channel-emitted=%"PRIu64" channel-suppressed=%"PRIu64" cache-overflow=%"PRIu64"\n",
                    c->list->handle,c->list->count,log.occurrence,log.suppressed,guest_thread_index,log.changed,c->list->owner,c->log_lifetime,recorded_cache.calls,recorded_cache.emitted,recorded_cache.suppressed,recorded_cache.overflow);
                else printf("NVN_COMMANDS_RECORDED handle=%"PRIu64" count=%"PRIu64" replay-pending=1 occurrence=%"PRIu64" suppressed=%"PRIu64" log-thread=%u log-scope=thread-channel changed=%u\n",
                    c->list->handle,c->list->count,log.occurrence,log.suppressed,guest_thread_index,log.changed);
            }
        }
        return 1;
    }
    switch(id) {
    case NI_nvnCommandBufferSetAlphaRef:case NI_nvnCommandBufferReportCounter:
    case NI_nvnCommandBufferSetBlendColor:
    case NI_nvnCommandBufferSetLineWidth:case NI_nvnCommandBufferSetPointSize:
    case NI_nvnCommandBufferSetSamplerPool:case NI_nvnCommandBufferSetTexturePool:
    case NI_nvnCommandBufferBindColorState:case NI_nvnCommandBufferBindChannelMaskState:case NI_nvnCommandBufferBindPolygonState:
    case NI_nvnCommandBufferBindBlendState:case NI_nvnCommandBufferBindDepthStencilState:
    case NI_nvnCommandBufferBindMultisampleState:case NI_nvnCommandBufferSetPolygonOffsetClamp:
    case NI_nvnCommandBufferSetViewport:case NI_nvnCommandBufferSetScissor:case NI_nvnCommandBufferSetDepthRange:
    case NI_nvnCommandBufferClearColor:case NI_nvnCommandBufferClearDepthStencil:case NI_nvnCommandBufferBarrier:
    case NI_nvnCommandBufferCopyBufferToTexture:case NI_nvnCommandBufferSetCopyRowStride:case NI_nvnCommandBufferSetCopyImageStride:
    case NI_nvnCommandBufferCopyTextureToTexture:case NI_nvnCommandBufferCopyBufferToBuffer:
#if ZERO_ZX_GAME
    case NI_nvnCommandBufferCopyTextureToBuffer:
#endif
    case NI_nvnCommandBufferSetRenderTargets:case NI_nvnCommandBufferBindProgram:
    case NI_nvnCommandBufferBindVertexAttribState:case NI_nvnCommandBufferBindVertexStreamState:
    case NI_nvnCommandBufferBindVertexBuffer:case NI_nvnCommandBufferBindVertexBuffers:
    case NI_nvnCommandBufferBindUniformBuffer:case NI_nvnCommandBufferBindUniformBuffers:
    case NI_nvnCommandBufferBindUniformBuffersDeferred:
    case NI_nvnCommandBufferBindTexture:
    case NI_nvnCommandBufferBindSeparateTexture:case NI_nvnCommandBufferBindSeparateSampler:
    case NI_nvnCommandBufferUpdateUniformBuffer:
    case NI_nvnCommandBufferDrawArrays:case NI_nvnCommandBufferDrawElements:case NI_nvnCommandBufferDrawElementsBaseVertex:
    case NI_nvnCommandBufferSetStencilRef:case NI_nvnCommandBufferSetStencilMask:case NI_nvnCommandBufferSetStencilValueMask:
    case NI_nvnCommandBufferSetShaderScratchMemory:case NI_nvnCommandBufferSetPrimitiveRestart:
    case NI_nvnCommandBufferSaveZCullData:case NI_nvnCommandBufferRestoreZCullData:
    case NI_nvnCommandBufferSetDepthClamp:case NI_nvnCommandBufferSetConservativeRasterEnable:
    case NI_nvnCommandBufferSetRasterizerDiscard:
    case NI_nvnCommandBufferSetDepthBounds:
        nvn_command_record(nvn_command_find(a[0]),id,f);return 1;
    /* Animal Well (NVN 17.x array forms): a single viewport/scissor/depth range at index 0 is the singular command;
     * the sample mask is only ever all-ones without multisampling. Anything else stays fail-closed. */
    case NI_nvnCommandBufferSetViewports:case NI_nvnCommandBufferSetScissors:case NI_nvnCommandBufferSetDepthRanges: {
        if(a[1] || a[2]!=1)stop("NVN_COMMAND_ARRAY_RANGE_REQUIRED",nvn_caller,a[2],20);
        struct nvn_frame g=*f;
        if(id==NI_nvnCommandBufferSetViewports) {
            float v[4];memcpy(v,nvn_pointer(a[3],16,PROT_READ),16);
            for(unsigned i=0;i<4;i++){if(!isfinite(v[i])||v[i]<-65536.0f||v[i]>65536.0f)stop("NVN_VIEWPORT_RANGE",nvn_caller,i,20);g.args[1+i]=(uint64_t)(int64_t)lrintf(v[i]);}
            nvn_command_record(nvn_command_find(a[0]),NI_nvnCommandBufferSetViewport,&g);return 1;
        }
        if(id==NI_nvnCommandBufferSetScissors) {
            int32_t v[4];memcpy(v,nvn_pointer(a[3],16,PROT_READ),16);
            for(unsigned i=0;i<4;i++)g.args[1+i]=(uint64_t)(int64_t)v[i];
            nvn_command_record(nvn_command_find(a[0]),NI_nvnCommandBufferSetScissor,&g);return 1;
        }
        {
            uint32_t v[2];memcpy(v,nvn_pointer(a[3],8,PROT_READ),8);
            g.vectors[0]=v[0];g.vectors[1]=v[1];
            nvn_command_record(nvn_command_find(a[0]),NI_nvnCommandBufferSetDepthRange,&g);return 1;
        }
    }
    case NI_nvnCommandBufferSetSampleMask:
        if((uint32_t)a[1]!=0xffffffffU)stop("NVN_SAMPLE_MASK_REQUIRED",nvn_caller,a[1],20);
        *result=0;return 1;
    case NI_nvnCommandBufferInsertCheckpointStatic:case NI_nvnCommandBufferInsertCheckpointDynamic: {
        /* Terminator 2D: checkpoints are GPU-side debug markers for Nintendo's crash reports; this replay
         * has no such stream. Nothing is recorded (MEDIDO: 2 static sites in main, docs/HOST.md 4). */
        (void)nvn_command_find(a[0]);
        static unsigned told;
        if(told++<2)printf("T2_NVN_CHECKPOINT kind=%s data=0x%"PRIx64" size=%"PRIu64" action=no-op\n",
            id==NI_nvnCommandBufferInsertCheckpointStatic?"static":"dynamic",a[1],a[2]);
        *result=0;return 1;
    }
    case NI_nvnCommandBufferClearTexture: {
        /* Animal Well: ClearTexture(cb, texture, view, region, color, mask) becomes the proven sequence
         * SetRenderTargets(texture) + SetScissor(region) + ClearColor(color, mask), then the previously recorded
         * render targets and scissor of this command buffer are recorded again. Only identity views are admitted. */
        struct nvn_command_buffer *c=nvn_command_find(a[0]);
        int32_t region[6];memcpy(region,nvn_pointer(a[3],24,PROT_READ),24);
        aw_clear_texture_admit(a[1],a[2],region);
        struct nvn_record saved_targets=c->last_targets,saved_scissor=c->last_scissor;unsigned had_targets=c->last_targets_valid,had_scissor=c->last_scissor_valid;
        struct nvn_record s1={.op=NI_nvnCommandBufferSetRenderTargets};s1.args[0]=1;uint64_t attachments[2]={a[1],0};memcpy(s1.payload,attachments,16);
        unsigned v,b;unsigned size=nvn_command_shape(&s1,&v,&b);nvn_command_emit(c,s1.op,&s1,size,v,b,f);
        struct nvn_record s2={.op=NI_nvnCommandBufferSetScissor};s2.args[0]=(uint64_t)region[0];s2.args[1]=(uint64_t)region[1];s2.args[2]=(uint64_t)region[3];s2.args[3]=(uint64_t)region[4];
        size=nvn_command_shape(&s2,&v,&b);nvn_command_emit(c,s2.op,&s2,size,v,b,f);
        struct nvn_record s3={.op=NI_nvnCommandBufferClearColor};s3.args[0]=0;s3.args[2]=a[5]&15;memcpy(s3.payload,nvn_pointer(a[4],16,PROT_READ),16);
        size=nvn_command_shape(&s3,&v,&b);nvn_command_emit(c,s3.op,&s3,size,v,b,f);
        if(had_scissor){size=nvn_command_shape(&saved_scissor,&v,&b);nvn_command_emit(c,saved_scissor.op,&saved_scissor,size,v,b,f);}
        if(had_targets){c->aw_restore_targets=saved_targets;c->aw_restore_pending=1;}
        c->last_targets=saved_targets;c->last_targets_valid=had_targets;c->last_scissor=saved_scissor;c->last_scissor_valid=had_scissor;
        aw_clear_texture_count++;
        if(aw_clear_texture_count<=4||!(aw_clear_texture_count&(aw_clear_texture_count-1)))
            printf("AW_CLEAR_TEXTURE texture=0x%"PRIx64" region=%d,%d %dx%d mask=%u restore-targets=%u restore-scissor=%u count=%"PRIu64"\n",a[1],region[0],region[1],region[3],region[4],(unsigned)(a[5]&15),had_targets,had_scissor,aw_clear_texture_count);
        *result=0;return 1;
    }
    default:break;
    }
    switch(id) {
    case NI_nvnCommandBufferAddCommandMemory:case NI_nvnCommandBufferAddControlMemory:
    case NI_nvnCommandBufferGetCommandMemoryFree:case NI_nvnCommandBufferGetCommandMemoryUsed:case NI_nvnCommandBufferGetCommandMemorySize:
    case NI_nvnCommandBufferGetControlMemoryFree:case NI_nvnCommandBufferGetControlMemoryUsed:case NI_nvnCommandBufferGetControlMemorySize:
    case NI_nvnCommandBufferGetMemoryCallback:case NI_nvnCommandBufferGetMemoryCallbackData:
    case NI_nvnCommandBufferSetMemoryCallback:case NI_nvnCommandBufferSetMemoryCallbackData:
    case NI_nvnCommandBufferSetCommandMemoryCallbackEnabled:
    case NI_nvnCommandBufferIsRecording:case NI_nvnCommandBufferFinalize:case NI_nvnCommandBufferSetDebugLabel:break;
    default:return 0;
    }
    struct nvn_command_buffer *c=nvn_command_find(a[0]);
    if(id==NI_nvnCommandBufferSetCommandMemoryCallbackEnabled) {
        if(a[1]>1)stop("NVN_COMMAND_CALLBACK_FLAG_INVALID",nvn_caller,a[1],24);
        c->command_callback_disabled=!a[1];return 1;
    }
    if(id==NI_nvnCommandBufferAddCommandMemory || id==NI_nvnCommandBufferAddControlMemory) {
        unsigned command=id==NI_nvnCommandBufferAddCommandMemory;struct nvn_pool *pool=command?nvn_pool_find(a[1]):NULL;
        if(c->callback_active && c->callback_event!=(command?0U:1U))
            stop("NVN_COMMAND_MEMORY_CALLBACK_CLASS",nvn_caller,id,24);
        uint64_t address=a[1],size=a[2];unsigned *count=command?&c->command_count:&c->control_count;
        struct nvn_command_arena *arena=command?c->command:c->control;
        if(command) {
            if((pool->flags&1) || a[2]>pool->size || a[3]>pool->size-a[2])stop("NVN_COMMAND_POOL_RANGE",nvn_caller,a[2],24);
            address=pool->address+a[2];size=a[3];
        }
        if(!size || (address|size)%16)stop("NVN_COMMAND_ARENA_RANGE",nvn_caller,size,24);
        nvn_pointer(address,size,PROT_READ|PROT_WRITE);
        if(command && !t2_command_arena_legacy())t2_command_arena_transfer(c,address,size);
        for(unsigned i=0;i<c->command_count+c->control_count;i++) {
            struct nvn_command_arena *p=i<c->command_count?&c->command[i]:&c->control[i-c->command_count];
            if(p->address==address && p->size==size && !p->used && !c->recording && !c->list){if(i<c->command_count)c->current=i;return 1;}
            if(p->address==address && p->size==size) {
                if((i<c->command_count)!=command)stop("NVN_COMMAND_ARENA_TYPE",nvn_caller,address,24);
                nvn_command_reuse_arena(c,p);if(command)c->current=i;return 1;
            }
            if(address<p->address+p->size && p->address<address+size)stop("NVN_COMMAND_ARENA_OVERLAP",nvn_caller,address,24);
        }
        if(command && *count==16 && !t2_command_arena_legacy()) {
            /* Full table: forget the oldest non-current chunk whose lists all ran (NVN keeps no older chunks). */
            for(unsigned i=0;i<c->command_count && *count==16;i++)
                if(i!=c->current && t2_command_arena_release(c,i,1)!=UINT32_MAX)t2_command_arena_remove(c,i);
        }
        if(*count==16)stop("NVN_COMMAND_ARENA_RANGE",nvn_caller,size,24);
        uint64_t cap=command?t2_command_arena_cap():0;
        arena[*count]=(struct nvn_command_arena){.address=address,.size=size,.pool=pool,.limit=cap&&cap<size?cap:size};
        if(command)c->current=*count;
        (*count)++;if(pool)pool->references++;
        c->memory_generation++;
        printf("NVN_COMMAND_STORAGE type=%s address=0x%"PRIx64" size=%"PRIu64" borrowed=1\n",command?"command":"control",address,size);
    }
    if(id==NI_nvnCommandBufferSetMemoryCallback){if(a[1])nvn_pointer(a[1],4,PROT_EXEC);c->callback=a[1];}
    if(id==NI_nvnCommandBufferSetMemoryCallbackData)c->callback_data=a[1];
    if(id==NI_nvnCommandBufferGetMemoryCallback)*result=c->callback;
    if(id==NI_nvnCommandBufferGetMemoryCallbackData)*result=c->callback_data;
    if(id==NI_nvnCommandBufferIsRecording)*result=c->recording;
    if(id==NI_nvnCommandBufferSetDebugLabel)nvn_debug_label(a[1]);
    if(id==NI_nvnCommandBufferFinalize) {
        if(nvn_command_pending_poll)NVN_FOR_EACH_COMMAND_LIST(list)if(list->live&&list->owner==c->object&&list->submitted&&!list->completed) {
            nvn_command_pending_poll(list->handle);
            if(!list->completed)stop("NVN_COMMAND_FINALIZE_BEFORE_COMPLETION",nvn_caller,list->handle,24);
        }
        if(c->recording || c->list)stop("NVN_COMMAND_FINALIZE_RECORDING",nvn_caller,a[0],24);
        for(unsigned i=0;i<c->command_count;i++){struct nvn_pool *p=c->command[i].pool;if(!--p->references)p->retired=0;}c->live=0;
    }
    unsigned commands=id==NI_nvnCommandBufferGetCommandMemoryFree || id==NI_nvnCommandBufferGetCommandMemorySize || id==NI_nvnCommandBufferGetCommandMemoryUsed;
    unsigned controls=id==NI_nvnCommandBufferGetControlMemoryFree || id==NI_nvnCommandBufferGetControlMemorySize || id==NI_nvnCommandBufferGetControlMemoryUsed;
    if(commands || controls) {
        struct nvn_command_arena *v=commands?c->command:c->control;unsigned count=commands?c->command_count:c->control_count;
        uint64_t size=0,used=0;
        /* NVN reports the CURRENT command chunk (older chunks are forgotten); control keeps the sum. */
        if(commands && !t2_command_arena_legacy()){if(c->current<count){size=v[c->current].limit;used=v[c->current].used;}}
        else for(unsigned i=0;i<count;i++){size+=v[i].size;used+=v[i].used;}
        if(id==NI_nvnCommandBufferGetCommandMemoryFree || id==NI_nvnCommandBufferGetControlMemoryFree)*result=size-used;
        else if(id==NI_nvnCommandBufferGetCommandMemoryUsed || id==NI_nvnCommandBufferGetControlMemoryUsed)*result=used;
        else *result=size;
    }
    return 1;
}
