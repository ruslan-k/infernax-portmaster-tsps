/* Writers own regions_mutex across the entire VM transaction. Readers may
 * reuse only private immutable validation snapshots, never this mutable table.
 * Odd generations retire snapshots before any VM effect; no generation wraps. */
#include <limits.h>
_Static_assert(__atomic_always_lock_free(sizeof(uint64_t),0),"region generation must be lock-free");
static uint64_t region_generation=2;
static unsigned region_write_depth;
static uint64_t region_generation_read(void){return __atomic_load_n(&region_generation,__ATOMIC_ACQUIRE);}
static void region_write_begin(uint64_t pc) {
    if(region_write_depth==UINT_MAX)stop("REGION_WRITER_DEPTH",pc,region_write_depth,24);
    if(!region_write_depth++) {
        uint64_t previous=region_generation_read();
        if((previous&1)||previous>UINT64_MAX-2)stop("REGION_GENERATION_WRAP",pc,previous,24);
        __atomic_store_n(&region_generation,previous+1,__ATOMIC_RELEASE);
        /* Publish retirement before a following syscall changes permissions
         * or lifetime, including on weakly ordered AArch64. */
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
    }
}
static void region_write_end(uint64_t pc) {
    if(!region_write_depth)stop("REGION_WRITER_UNBALANCED",pc,0,24);
    if(!--region_write_depth) {
        uint64_t previous=region_generation_read();
        if(!(previous&1))stop("REGION_WRITER_GENERATION",pc,previous,24);
        __atomic_store_n(&region_generation,previous+1,__ATOMIC_RELEASE);
    }
}
