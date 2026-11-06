/* t2_texture_index.h — lossless 16-bit indexed storage for the 4096x4096 texture
 * pages of Terminator 2D: NO FATE (Frente C). Standalone: only <GLES2/gl2.h> and
 * the C library. Nothing here touches the host's NVN structures; the hook that
 * feeds it (where Unity's RGBA8 upload of a page is intercepted) is described in
 * docs/TEXTURAS.md and belongs to Frente A.
 *
 * DATA LAYOUT (byte for byte the same as tools/textures/t2_index16.py, proved by
 * tests/palette_roundtrip_test.py: 12 pages, mismatched=0):
 *
 *   index plane   w*h*2 bytes, GL_LUMINANCE_ALPHA / GL_UNSIGNED_BYTE, row-major,
 *                 byte 0 = index & 0xFF  (sampled as .r/.g/.b = L)
 *                 byte 1 = index >> 8    (sampled as .a)
 *   palette       256x256 GL_RGBA / GL_UNSIGNED_BYTE, entry i at (x = i & 255, y = i >> 8),
 *                 entries = the set of packed colours r|g<<8|b<<16|a<<24 sorted ascending,
 *                 entry 0 always 0x00000000 (fully transparent black), unused entries zero.
 *
 * Both textures: GL_NEAREST min/mag, GL_CLAMP_TO_EDGE, ONE level, no mips. An index
 * must never be filtered (a blend of two indices is a colour that is not in the
 * palette), and a partial mip chain samples black on Mali-400/450 (house lesson), so
 * level 0 alone is the only correct shape.
 *
 * COST per 4096x4096 page: 32 MiB index + 256 KiB palette = 32.25 MiB, against 64 MiB
 * RGBA8. 12 pages: 768 MiB -> 387 MiB. Exact: every texel reconstructs to its source
 * RGBA8 value, including RGB under alpha 0 (nothing is collapsed).
 *
 * PRECISION (why the GLSL below is safe on an fp16 fragment pipe):
 *   The page UV is NOT computed here: it arrives as a highp varying from the vertex
 *   shader and goes straight into texture2D(). On a 4096-wide page one texel is
 *   1/4096 = 2^-12 of the coordinate; fp16 near 1.0 has an ulp of 2^-11 (two texels),
 *   so any fragment-side arithmetic on the page UV would land on the wrong texel.
 *   The palette coordinate IS computed here, and it is exact in fp16:
 *     the fetched byte k gives i.r = k/255 (rounded to fp16, error <= 2^-12 relative);
 *     x = i.r * (255/256) + 0.5/256 targets (2k+1)/512, which needs 9 mantissa bits and
 *     is therefore REPRESENTABLE in fp16 (10-bit mantissa). 255/256 (8 bits) and 2^-9
 *     are exact constants. Each of the three roundings is <= half an ulp (2^-12 in
 *     [0.5,1)), total <= 3*2^-12 = 0.00073, and the distance from (2k+1)/512 to the
 *     nearest texel edge is 2^-9 = 0.00195: even a truncating (non-rounding) ALU with
 *     3 full-ulp errors (3*2^-11 = 0.00146) still selects entry k. Measured in
 *     tests/palette_roundtrip_test.py with every op rounded to fp16: the coordinate
 *     lands exactly on (2k+1)/512 for all 256 byte values (margin 0.5000 texel).
 *   The palette texture is 256x256, so NEAREST maps coordinate*256 to entry k with
 *   the texel centre exactly at the coordinate. The samplers are declared mediump on
 *   purpose (the ES 1.00 default for sampler2D in the fragment stage is lowp).
 *
 * What stays INFERIDO until a device trace exists: that Unity uploads these pages as
 * one full-level RGBA8 copy (nvnCommandBufferCopyBufferToTexture, format 0x25,
 * levels 1) and that the game's 3 shaders sample them with NEAREST. See docs/TEXTURAS.md.
 */
#ifndef T2_TEXTURE_INDEX_H
#define T2_TEXTURE_INDEX_H
#include <GLES2/gl2.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define T2_IDX16_PALETTE_SIDE 256u
#define T2_IDX16_PALETTE_ENTRIES (T2_IDX16_PALETTE_SIDE * T2_IDX16_PALETTE_SIDE)
#define T2_IDX16_PALETTE_BYTES (T2_IDX16_PALETTE_ENTRIES * 4u)

/* GLSL ES 1.00 fragment snippet. Paste after the precision statement of the
 * translated fragment shader; then replace `texture2D(<page sampler>, uv)` with
 * `t2_idx16_sample(<page sampler>, t2_palette, uv)` for the sampler that holds a
 * converted page, and leave every other sampler (render textures, the CRT filter's
 * source, UI) on its plain texture2D. `uv` must be the highp varying itself. */
#define T2_IDX16_GLSL_HELPER \
    "uniform mediump sampler2D t2_palette;\n" \
    "mediump vec4 t2_idx16_sample(mediump sampler2D page, mediump sampler2D palette, highp vec2 uv) {\n" \
    "    mediump vec4 i = texture2D(page, uv);\n" \
    "    mediump float x = i.r * (255.0 / 256.0) + (0.5 / 256.0);\n" \
    "    mediump float y = i.a * (255.0 / 256.0) + (0.5 / 256.0);\n" \
    "    return texture2D(palette, vec2(x, y));\n" \
    "}\n"

/* Selector variant, for a shader that draws from converted and unconverted textures
 * through the same sampler slot: t2_mode 0.0 = plain RGBA8, 1.0 = idx16 pair.
 * The branch is uniform (not per-texel), which the Mali-450 compiler resolves cheaply;
 * the Rivals host pays the same shape for nx_mode. */
#define T2_IDX16_GLSL_HELPER_SELECT \
    "uniform mediump sampler2D t2_palette;\n" \
    "uniform mediump float t2_mode;\n" \
    "mediump vec4 t2_idx16_sample(mediump sampler2D page, mediump sampler2D palette, highp vec2 uv) {\n" \
    "    mediump vec4 i = texture2D(page, uv);\n" \
    "    if (t2_mode == 0.0) return i;\n" \
    "    mediump float x = i.r * (255.0 / 256.0) + (0.5 / 256.0);\n" \
    "    mediump float y = i.a * (255.0 / 256.0) + (0.5 / 256.0);\n" \
    "    return texture2D(palette, vec2(x, y));\n" \
    "}\n"

/* Function-only forms (Frente F, t2_page_substitution.h): the shader variant declares one
 * palette/block-map sampler PER page sampler (t2_palette_<name>, t2_blockmap_<name>), so the
 * uniform declarations are emitted by the variant generator and only the maths lives here.
 * The bodies are the same text as the helpers above. */
#define T2_IDX16_GLSL_FUNCTION \
    "mediump vec4 t2_idx16_sample(mediump sampler2D page, mediump sampler2D palette, highp vec2 uv) {\n" \
    "    mediump vec4 i = texture2D(page, uv);\n" \
    "    mediump float x = i.r * (255.0 / 256.0) + (0.5 / 256.0);\n" \
    "    mediump float y = i.a * (255.0 / 256.0) + (0.5 / 256.0);\n" \
    "    return texture2D(palette, vec2(x, y));\n" \
    "}\n"

struct t2_idx16_page {
    GLuint index_name;      /* GL_LUMINANCE_ALPHA w x h */
    GLuint palette_name;    /* GL_RGBA 256 x 256 */
    uint32_t width, height;
    uint32_t entries;       /* palette entries in use (<= 65536), entry 0 = transparent black */
    size_t gl_bytes;        /* w*h*2 + 256*256*4 as handed to the driver */
};

static inline uint32_t t2_idx16_pack(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline int t2_idx16_compare_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

/* Build the pair from an RGBA8 image (w*h*4 bytes, row-major, tightly packed).
 *   index_la    out, w*h*2 bytes (caller-allocated)
 *   palette     out, T2_IDX16_PALETTE_BYTES bytes (caller-allocated, fully written)
 *   entries     out, number of palette entries used
 * Returns 1 on success, 0 when the image has more than 65536 distinct RGBA values
 * (the caller then keeps the RGBA8 upload) or on allocation failure.
 * Two passes over the texels: the first assigns provisional indices through an
 * open-addressing hash (131072 slots, load <= 0.5), then the palette is sorted
 * ascending and the second pass remaps, so the output equals t2_index16.py exactly. */
static inline int t2_idx16_build(const uint8_t *rgba, uint32_t w, uint32_t h,
                          uint8_t *index_la, uint8_t *palette, uint32_t *entries) {
    enum { SLOTS = 1u << 17, MASK = SLOTS - 1 };
    size_t texels = (size_t)w * h;
    uint32_t *keys = (uint32_t *)malloc(SLOTS * sizeof *keys);
    uint16_t *values = (uint16_t *)malloc(SLOTS * sizeof *values);
    uint8_t *used = (uint8_t *)calloc(SLOTS, 1);
    uint32_t *colours = (uint32_t *)malloc(65536u * sizeof *colours);
    uint16_t *provisional = (uint16_t *)malloc(texels * sizeof *provisional);
    uint16_t *remap = (uint16_t *)malloc(65536u * sizeof *remap);
    uint32_t count = 0;
    int ok = 0;
    if (!keys || !values || !used || !colours || !provisional || !remap) goto out;
    colours[count++] = 0;                     /* entry 0 reserved: transparent black */
    { uint32_t s = 0; keys[s] = 0; values[s] = 0; used[s] = 1; }
    for (size_t t = 0; t < texels; t++) {
        uint32_t c = t2_idx16_pack(rgba + t * 4);
        uint32_t s = (c * 2654435761u) >> 15;   /* 17 bits */
        for (;;) {
            s &= MASK;
            if (!used[s]) {
                if (count >= 65536u) goto out;   /* more colours than idx16 can hold */
                used[s] = 1; keys[s] = c; values[s] = (uint16_t)count; colours[count++] = c;
                break;
            }
            if (keys[s] == c) break;
            s++;
        }
        provisional[t] = values[s];
    }
    /* Sort ascending; entry 0 (0x00000000) is the minimum so it stays first. */
    {
        uint32_t *sorted = (uint32_t *)malloc(count * sizeof *sorted);
        if (!sorted) goto out;
        memcpy(sorted, colours, count * sizeof *sorted);
        qsort(sorted, count, sizeof *sorted, t2_idx16_compare_u32);
        for (uint32_t i = 0; i < count; i++) {           /* provisional index -> sorted index */
            uint32_t c = colours[i], lo = 0, hi = count - 1;
            while (lo < hi) { uint32_t mid = (lo + hi) / 2; if (sorted[mid] < c) lo = mid + 1; else hi = mid; }
            remap[i] = (uint16_t)lo;
        }
        memset(palette, 0, T2_IDX16_PALETTE_BYTES);
        for (uint32_t i = 0; i < count; i++) {
            uint32_t c = sorted[i];
            palette[i * 4 + 0] = (uint8_t)c; palette[i * 4 + 1] = (uint8_t)(c >> 8);
            palette[i * 4 + 2] = (uint8_t)(c >> 16); palette[i * 4 + 3] = (uint8_t)(c >> 24);
        }
        free(sorted);
    }
    for (size_t t = 0; t < texels; t++) {
        uint16_t i = remap[provisional[t]];
        index_la[t * 2 + 0] = (uint8_t)(i & 0xFF);
        index_la[t * 2 + 1] = (uint8_t)(i >> 8);
    }
    *entries = count;
    ok = 1;
out:
    free(keys); free(values); free(used); free(colours); free(provisional); free(remap);
    return ok;
}

static inline void t2_idx16_parameters(void) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

/* Upload a converted pair into two fresh texture objects (level 0 only, no mips).
 * Caller has the GL context current; the active texture unit is left bound to the
 * palette. Returns the GL error (GL_NO_ERROR on success); on error nothing is kept.
 * Note the driver copies both images: the caller may free index_la/palette after
 * this returns, which is the point (32.25 MiB resident instead of 64). */
static inline GLenum t2_idx16_upload(struct t2_idx16_page *page, const uint8_t *index_la,
                              uint32_t w, uint32_t h, const uint8_t *palette, uint32_t entries) {
    GLuint names[2] = {0, 0};
    GLint alignment = 4;
    GLenum error;
    memset(page, 0, sizeof *page);
    while (glGetError() != GL_NO_ERROR) {}
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(2, names);
    glBindTexture(GL_TEXTURE_2D, names[0]);
    t2_idx16_parameters();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, (GLsizei)w, (GLsizei)h, 0,
                 GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, index_la);
    glBindTexture(GL_TEXTURE_2D, names[1]);
    t2_idx16_parameters();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)T2_IDX16_PALETTE_SIDE, (GLsizei)T2_IDX16_PALETTE_SIDE, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, palette);
    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
    error = glGetError();
    if (error != GL_NO_ERROR) { glDeleteTextures(2, names); return error; }
    page->index_name = names[0]; page->palette_name = names[1];
    page->width = w; page->height = h; page->entries = entries;
    page->gl_bytes = (size_t)w * h * 2 + T2_IDX16_PALETTE_BYTES;
    return GL_NO_ERROR;
}

/* Convert + upload in one go from the RGBA8 the guest handed over. Returns GL_NO_ERROR
 * on success, GL_OUT_OF_MEMORY when the conversion could not allocate, GL_INVALID_VALUE
 * when the image exceeds 65536 colours (caller falls back to RGBA8). */
static inline GLenum t2_idx16_convert_upload(struct t2_idx16_page *page, const uint8_t *rgba, uint32_t w, uint32_t h) {
    size_t texels = (size_t)w * h;
    uint8_t *index_la = (uint8_t *)malloc(texels * 2);
    uint8_t *palette = (uint8_t *)malloc(T2_IDX16_PALETTE_BYTES);
    uint32_t entries = 0;
    GLenum error = GL_OUT_OF_MEMORY;
    if (index_la && palette) {
        if (t2_idx16_build(rgba, w, h, index_la, palette, &entries))
            error = t2_idx16_upload(page, index_la, w, h, palette, entries);
        else
            error = GL_INVALID_VALUE;
    }
    free(index_la); free(palette);
    return error;
}

/* Bind for a draw: page on `page_unit`, palette on `palette_unit`; the two sampler
 * uniforms of the shader variant get the unit numbers. GL_MAX_TEXTURE_IMAGE_UNITS is
 * 8 on Mali-450: the Rivals host keeps unit 7 for the palette. */
static inline void t2_idx16_bind(const struct t2_idx16_page *page, GLuint page_unit, GLuint palette_unit,
                                 GLint page_location, GLint palette_location) {
    glActiveTexture(GL_TEXTURE0 + page_unit);
    glBindTexture(GL_TEXTURE_2D, page->index_name);
    glActiveTexture(GL_TEXTURE0 + palette_unit);
    glBindTexture(GL_TEXTURE_2D, page->palette_name);
    if (page_location >= 0) glUniform1i(page_location, (GLint)page_unit);
    if (palette_location >= 0) glUniform1i(palette_location, (GLint)palette_unit);
}

/* ---------------------------------------------------------------------------
 * MODE 2: idx8 + block palettes ("idx8-block"), 1 byte per texel.
 *
 * Layout (tools/textures/t2_block_palette.py merge, docs/texture_palette_merge.json):
 *   index plane   w*h bytes, GL_LUMINANCE, entry within the block's palette (.r)
 *   block map     (w/B) x (h/B) bytes, GL_LUMINANCE, NEAREST, palette id 0..255 of
 *                 each B x B block (.r); B is a power of two (128/64/32/16), so
 *                 floor(u*(w/B)) == floor(floor(u*w)/B) and the block map and the
 *                 page agree on which block a texel is in
 *   palette atlas 256 x 256 GL_RGBA8: row p = palette p (256 entries), entry 0 of every
 *                 palette = transparent black
 *   cost          16 MiB + 256 KiB + (block map, <= 64 KiB at B=16) per 4096x4096 page
 *
 * Why 256 palettes and not one per block: the palette coordinate is computed in the
 * fp16 fragment pipe, and the argument above shows 256 texels per axis is the size at
 * which (2k+1)/512 stays exactly representable with a 0.5-texel margin. A 512-tall
 * palette atlas would put the row centres at odd multiples of 2^-10, one fp16 ulp
 * apart in [0.5,1): zero margin. So the palette id must fit in ONE byte and the atlas
 * stays 256x256; blocks share palettes (greedy merge on the PC) to get there. No
 * arithmetic touches the page UV: both the index plane and the block map are fetched
 * with the highp varying as is. Three fetches per texel instead of two.
 * --------------------------------------------------------------------------- */
#define T2_IDX8_BLOCK_GLSL_HELPER \
    "uniform mediump sampler2D t2_palette;\n" \
    "uniform mediump sampler2D t2_blockmap;\n" \
    "mediump vec4 t2_idx8_block_sample(mediump sampler2D page, mediump sampler2D blockmap, mediump sampler2D palette, highp vec2 uv) {\n" \
    "    mediump float k = texture2D(page, uv).r;\n" \
    "    mediump float p = texture2D(blockmap, uv).r;\n" \
    "    mediump float x = k * (255.0 / 256.0) + (0.5 / 256.0);\n" \
    "    mediump float y = p * (255.0 / 256.0) + (0.5 / 256.0);\n" \
    "    return texture2D(palette, vec2(x, y));\n" \
    "}\n"

#define T2_IDX8_BLOCK_GLSL_FUNCTION \
    "mediump vec4 t2_idx8_block_sample(mediump sampler2D page, mediump sampler2D blockmap, mediump sampler2D palette, highp vec2 uv) {\n" \
    "    mediump float k = texture2D(page, uv).r;\n" \
    "    mediump float p = texture2D(blockmap, uv).r;\n" \
    "    mediump float x = k * (255.0 / 256.0) + (0.5 / 256.0);\n" \
    "    mediump float y = p * (255.0 / 256.0) + (0.5 / 256.0);\n" \
    "    return texture2D(palette, vec2(x, y));\n" \
    "}\n"

struct t2_idx8_block_page {
    GLuint index_name;      /* GL_LUMINANCE w x h */
    GLuint blockmap_name;   /* GL_LUMINANCE (w/B) x (h/B) */
    GLuint palette_name;    /* GL_RGBA 256 x 256, one palette per row */
    uint32_t width, height, block;
    uint32_t palettes;      /* rows in use (<= 256) */
    size_t gl_bytes;
};

/* Upload an idx8-block triple (level 0 only, NEAREST, no mips). `block` is the block
 * side in texels (power of two, divides w and h). Returns the GL error. */
static inline GLenum t2_idx8_block_upload(struct t2_idx8_block_page *page, const uint8_t *index_l,
                                          const uint8_t *blockmap_l, const uint8_t *palette_rows,
                                          uint32_t w, uint32_t h, uint32_t block, uint32_t palettes) {
    GLuint names[3] = {0, 0, 0};
    GLint alignment = 4;
    GLenum error;
    uint32_t bw = w / block, bh = h / block;
    memset(page, 0, sizeof *page);
    if (!block || (block & (block - 1)) || w % block || h % block || palettes > 256) return GL_INVALID_VALUE;
    while (glGetError() != GL_NO_ERROR) {}
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(3, names);
    glBindTexture(GL_TEXTURE_2D, names[0]);
    t2_idx16_parameters();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, (GLsizei)w, (GLsizei)h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, index_l);
    glBindTexture(GL_TEXTURE_2D, names[1]);
    t2_idx16_parameters();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, (GLsizei)bw, (GLsizei)bh, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, blockmap_l);
    glBindTexture(GL_TEXTURE_2D, names[2]);
    t2_idx16_parameters();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)T2_IDX16_PALETTE_SIDE, (GLsizei)T2_IDX16_PALETTE_SIDE, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, palette_rows);
    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
    error = glGetError();
    if (error != GL_NO_ERROR) { glDeleteTextures(3, names); return error; }
    page->index_name = names[0]; page->blockmap_name = names[1]; page->palette_name = names[2];
    page->width = w; page->height = h; page->block = block; page->palettes = palettes;
    page->gl_bytes = (size_t)w * h + (size_t)bw * bh + T2_IDX16_PALETTE_BYTES;
    return GL_NO_ERROR;
}

static inline void t2_idx8_block_bind(const struct t2_idx8_block_page *page, GLuint page_unit, GLuint blockmap_unit,
                                      GLuint palette_unit, GLint page_location, GLint blockmap_location,
                                      GLint palette_location) {
    glActiveTexture(GL_TEXTURE0 + page_unit);
    glBindTexture(GL_TEXTURE_2D, page->index_name);
    glActiveTexture(GL_TEXTURE0 + blockmap_unit);
    glBindTexture(GL_TEXTURE_2D, page->blockmap_name);
    glActiveTexture(GL_TEXTURE0 + palette_unit);
    glBindTexture(GL_TEXTURE_2D, page->palette_name);
    if (page_location >= 0) glUniform1i(page_location, (GLint)page_unit);
    if (blockmap_location >= 0) glUniform1i(blockmap_location, (GLint)blockmap_unit);
    if (palette_location >= 0) glUniform1i(palette_location, (GLint)palette_unit);
}

static inline void t2_idx8_block_release(struct t2_idx8_block_page *page) {
    GLuint names[3] = {page->index_name, page->blockmap_name, page->palette_name};
    if (names[0] || names[1] || names[2]) glDeleteTextures(3, names);
    memset(page, 0, sizeof *page);
}

static inline void t2_idx16_release(struct t2_idx16_page *page) {
    GLuint names[2] = {page->index_name, page->palette_name};
    if (names[0] || names[1]) glDeleteTextures(2, names);
    memset(page, 0, sizeof *page);
}

/* Eligibility of an upload for conversion, mirroring what the Rivals host admits:
 * a full level-0 RGBA8 image with real pixels, at least 512x512 and within the
 * 4096 ceiling of the target. Render targets and streamed images stay RGBA8. */
static inline int t2_idx16_eligible(uint32_t w, uint32_t h, uint32_t levels, int has_pixels, int is_render_target) {
    return has_pixels && !is_render_target && levels == 1 && w >= 512 && h >= 512 && w <= 4096 && h <= 4096;
}

#endif /* T2_TEXTURE_INDEX_H */
