/* The idx8 block map addresses the same UV domain as its index page. Its wrap
 * must follow that draw's NVN sampler; the palette remains clamped. This cache
 * is private to the block-map texture, independent of the main texture cache. */
#ifndef T2_PAGE_WRAP_H
#define T2_PAGE_WRAP_H
#include <GLES2/gl2.h>

struct t2_page_wrap_cache { GLenum applied[2]; };

static inline void t2_page_wrap_reset(struct t2_page_wrap_cache *cache) {
    cache->applied[0]=cache->applied[1]=0; /* no valid GL wrap enum is zero */
}

/* The caller has already bound the block-map texture. */
static inline void t2_page_wrap_apply(struct t2_page_wrap_cache *cache,GLenum s,GLenum t) {
    if(cache->applied[0]!=s) {
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,(GLint)s);
        cache->applied[0]=s;
    }
    if(cache->applied[1]!=t) {
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,(GLint)t);
        cache->applied[1]=t;
    }
}
#endif
