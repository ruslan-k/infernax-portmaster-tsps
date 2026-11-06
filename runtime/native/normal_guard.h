/* Experimental host-only access guard. Guest QueryMemory permissions remain
 * unchanged. Published intervals are immutable for the process lifetime: a
 * signal may have observed an active entry while Finalize retires it. */
#define NORMAL_GUARD_MAX 4096U
_Static_assert(__atomic_always_lock_free(sizeof(unsigned),0),"signal-safe normal guard publication");
struct normal_guard {uint64_t base,size,id;unsigned active;};
static struct normal_guard normal_guards[NORMAL_GUARD_MAX];
static unsigned normal_guard_count;
static unsigned normal_ranges_overlap(uint64_t a,uint64_t size,uint64_t b,uint64_t other) {
    return size && other && (a<=b?b-a<size:a-b<other);
}
static uint64_t normal_guard_overlap(uint64_t address,uint64_t size) {
    unsigned count=__atomic_load_n(&normal_guard_count,__ATOMIC_ACQUIRE);
    for(unsigned i=0;i<count;i++) {
        const struct normal_guard *g=&normal_guards[i];
        if(__atomic_load_n(&g->active,__ATOMIC_ACQUIRE) && normal_ranges_overlap(address,size,g->base,g->size))return g->id;
    }
    return 0;
}
static void normal_guard_require_clear(uint64_t address,uint64_t size,uint64_t pc) {
    if(normal_guard_overlap(address,size))stop("NVN_NORMAL_PROTECTED_RANGE",pc,address,24);
}
