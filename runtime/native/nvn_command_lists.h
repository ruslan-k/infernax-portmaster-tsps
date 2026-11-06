/* Stable metadata for handles backed by caller control memory. EndRecording
 * does not consume a global hardware handle. SDK2d6190 reserves control bytes;
 * SDK2d6238 returns that recording's control stream. GPU completion therefore
 * does not invalidate it. Grow only when real caller control storage admits
 * another 32-byte adapter header; never move recorder/snapshot pointers. */
#define NVN_COMMAND_LIST_BLOCK 256U
struct nvn_command_list_block {
    struct nvn_command_list_block *next;
    unsigned count;
    struct nvn_command_list lists[NVN_COMMAND_LIST_BLOCK];
};
static struct nvn_command_list_block nvn_command_list_first={.count=NVN_COMMAND_LIST_BLOCK};
/* Preserve the first-block fixture seam; runtime lookup always visits every
 * block through the helpers below, never a fixed 256-entry search. */
#define nvn_command_lists (nvn_command_list_first.lists)
static uint64_t nvn_command_list_slots=NVN_COMMAND_LIST_BLOCK;
#define NVN_FOR_EACH_COMMAND_LIST(item) \
    for(struct nvn_command_list_block *nvn_list_block=&nvn_command_list_first;nvn_list_block;nvn_list_block=nvn_list_block->next) \
        for(unsigned nvn_list_index=0;nvn_list_index<nvn_list_block->count;nvn_list_index++) \
            for(struct nvn_command_list *item=&nvn_list_block->lists[nvn_list_index];item;item=NULL)
static struct nvn_command_list *nvn_command_list_lookup(uint64_t handle) {
    NVN_FOR_EACH_COMMAND_LIST(list)if(list->live&&list->handle==handle)return list;
    return NULL;
}
static struct nvn_command_list *nvn_command_list_require(uint64_t handle) {
    struct nvn_command_list *list=nvn_command_list_lookup(handle);
    if(!list)stop("NVN_COMMAND_HANDLE_INVALID",nvn_caller,handle,24);
    return list;
}
static struct nvn_command_list *nvn_command_list_allocate(void) {
    NVN_FOR_EACH_COMMAND_LIST(list)if(!list->live)return list;
    uint64_t header_capacity=0;
    for(unsigned i=0;i<32;i++)if(nvn_command_buffers[i].live) {
        struct nvn_command_buffer *c=&nvn_command_buffers[i];
        for(unsigned j=0;j<c->control_count;j++) {
            uint64_t n=c->control[j].size/32;
            if(n>UINT64_MAX-header_capacity)stop("NVN_CONTROL_HEADER_CAPACITY_OVERFLOW",nvn_caller,n,24);
            header_capacity+=n;
        }
    }
    if(nvn_command_list_slots>=header_capacity)
        stop("NVN_COMMAND_METADATA_WITHOUT_CONTROL_STORAGE",nvn_caller,nvn_command_list_slots,24);
    struct nvn_command_list_block *block=calloc(1,sizeof(*block));
    if(!block)stop("NVN_COMMAND_METADATA_MEMORY",nvn_caller,sizeof(*block),21);
    uint64_t available=header_capacity-nvn_command_list_slots;
    block->count=available<NVN_COMMAND_LIST_BLOCK?(unsigned)available:NVN_COMMAND_LIST_BLOCK;
    struct nvn_command_list_block *tail=&nvn_command_list_first;while(tail->next)tail=tail->next;
    tail->next=block;nvn_command_list_slots+=block->count;
    printf("NVN_COMMAND_METADATA_GROW slots=%"PRIu64" caller-control-headers=%"PRIu64" stable-addresses=1 retained-until-caller-reuse=1\n",
        nvn_command_list_slots,header_capacity);
    return &block->lists[0];
}
