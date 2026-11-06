#ifndef MEGAMAN_HEAP_MAPPING_H
#define MEGAMAN_HEAP_MAPPING_H
#include <sys/mman.h>

/* Only the guest SVC1 private heap calls this helper. Zero/ZX selects demand
 * commitment after the isolated .26 allocation proof for0xbac00000 bytes.
 * Reserve address space without up-front swap commitment; physical pages are
 * allocated on access. Native zeroing is unchanged and LC2 hooks stay off.
 * This does not guarantee available RAM for subsequent writes.
 * Keep the original hint semantics: never MAP_FIXED, never replace mappings.
 */
static int megaman_heap_mapping_flags(unsigned demand_commit) {
    return MAP_PRIVATE|MAP_ANONYMOUS|(demand_commit?MAP_NORESERVE:0);
}
#endif
