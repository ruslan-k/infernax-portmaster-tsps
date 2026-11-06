/* Immutable address index, sorted before entering any guest thread. */
static int bridge_compare(const void *left,const void *right) {
    const struct bridge *a=left,*b=right;return a->pc>b->pc?1:a->pc<b->pc?-1:0;
}
static uint32_t bridge_instruction(uint64_t pc) {
    size_t lo=0,hi=nbridges;
    while(lo<hi) {
        size_t mid=lo+(hi-lo)/2;
        if(bridges[mid].pc<pc)lo=mid+1;else hi=mid;
    }
    return lo<nbridges&&bridges[lo].pc==pc?bridges[lo].opcode:0;
}
