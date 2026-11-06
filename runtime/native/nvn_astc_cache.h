/* nvn_astc_cache.h — M6 side-cache: content-hash keyed ASTC uploads for BC textures.
 *
 * Opt-in via WOL_ASTC_CACHE=<path to cache file> (see evidence/m6-compressed-textures/
 * cache-design.md for the WOL6 format). The runtime decodes BC as usual (the decode is
 * also the hash source), then looks the sha256 of the RGBA8 pixels up in the cache; a hit
 * uploads the ASTC payload with glCompressedTexImage2D instead of the RGBA bytes.
 * No guest-visible change: the same pixels come back through the sampler (device-accepted
 * in evidence/m6-compressed-textures/astc-acceptance.md). Disabled = zero effect.
 */
#ifndef NVN_ASTC_CACHE_H
#define NVN_ASTC_CACHE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

struct nvn_astc_entry {
    uint8_t hash[32];
    uint32_t width, height;
    uint8_t block, levels;
    uint32_t gl_enum;
    uint32_t len;
    uint64_t offset;
};

static struct {
    int configured, enabled;
    uint8_t *data;
    size_t data_size;
    unsigned count;
    struct nvn_astc_entry *entries;
} nvn_astc_cache;

static const uint8_t *nvn_astc_cache_hit_ptr;
static size_t nvn_astc_cache_hit_len;

static void nvn_astc_cache_load(void) {
    if (nvn_astc_cache.configured) return;
    nvn_astc_cache.configured = 1;
    const char *path = getenv("WOL_ASTC_CACHE");
    if (!path || !*path) return;
    FILE *f = fopen(path, "rb");
    if (!f) { printf("ASTC_CACHE_OPEN_FAIL path=%s\n", path); return; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 12) { printf("ASTC_CACHE_BAD_FILE size=%ld\n", size); fclose(f); return; }
    uint8_t *data = malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) {
        printf("ASTC_CACHE_READ_FAIL size=%ld\n", size);
        free(data); fclose(f); return;
    }
    fclose(f);
    if (memcmp(data, "WOL6", 4)) { printf("ASTC_CACHE_BAD_MAGIC\n"); free(data); return; }
    uint32_t version = 0, n = 0;
    memcpy(&version, data + 4, 4);
    memcpy(&n, data + 8, 4);
    if (version != 1 || n > 4096 || (size_t)12 + (size_t)64 * n > (size_t)size) {
        printf("ASTC_CACHE_BAD_TOC version=%u entries=%u size=%ld\n", version, n, size);
        free(data); return;
    }
    nvn_astc_cache.entries = malloc(sizeof(struct nvn_astc_entry) * (n ? n : 1));
    if (!nvn_astc_cache.entries) { printf("ASTC_CACHE_TOC_MEMORY\n"); free(data); return; }
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *rec = data + 12 + (size_t)64 * i;
        struct nvn_astc_entry *e = &nvn_astc_cache.entries[i];
        memcpy(e->hash, rec, 32);
        memcpy(&e->width, rec + 32, 4);
        memcpy(&e->height, rec + 36, 4);
        e->block = rec[40];
        memcpy(&e->gl_enum, rec + 41, 4);
        e->levels = rec[45];
        memcpy(&e->len, rec + 46, 4);
        memcpy(&e->offset, rec + 50, 8);
        if (e->offset + e->len > (uint64_t)size) {
            printf("ASTC_CACHE_ENTRY_RANGE idx=%u\n", i);
            free(nvn_astc_cache.entries); nvn_astc_cache.entries = NULL;
            free(data); return;
        }
    }
    nvn_astc_cache.data = data;
    nvn_astc_cache.data_size = (size_t)size;
    nvn_astc_cache.count = n;
    nvn_astc_cache.enabled = 1;
    printf("ASTC_CACHE_LOADED entries=%u file-bytes=%ld\n", n, size);
}

/* Returns 1 and sets the hit pointer when the RGBA decode is present in the cache.
 * v1 accepts only single-level, non-sRGB textures; everything else stays on the RGBA path. */
static int nvn_astc_cache_try(uint64_t id, const void *rgba, size_t rgba_bytes,
                              unsigned width, unsigned height, unsigned levels) {
    nvn_astc_cache_load();
    if (!nvn_astc_cache.enabled) return 0;
    if (levels != 1) return 0;
    uint8_t hash[32];
    zero_sha256(rgba, rgba_bytes, hash);
    for (unsigned i = 0; i < nvn_astc_cache.count; i++) {
        const struct nvn_astc_entry *e = &nvn_astc_cache.entries[i];
        if (memcmp(e->hash, hash, 32)) continue;
        if (e->width != width || e->height != height || e->levels != 1 || e->gl_enum != 0x93B4U)
            return 0; /* hash collision or size/format mismatch: refuse loudly for the pair */
        nvn_astc_cache_hit_ptr = nvn_astc_cache.data + e->offset;
        nvn_astc_cache_hit_len = e->len;
        { static unsigned told; if (told < 16) { told++; printf("ASTC_CACHE_HIT id=%"PRIu64" bytes=%u w=%u h=%u\n", id, e->len, width, height); } }
        return 1;
    }
    { static unsigned misses; if (misses < 8) { misses++; printf("ASTC_CACHE_MISS id=%"PRIu64" w=%u h=%u rgba-bytes=%zu\n", id, width, height, rgba_bytes); } }
    return 0;
}

#endif
