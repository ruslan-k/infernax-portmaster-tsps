/* NVNTexture is an opaque, byte-copyable SDK 5.4 object (0xc0 bytes).
 * Unity copies a swapchain texture into its render-buffer wrapper at
 * main+0x16063e0 and main+0x160a42c. Keep resource identity in those bytes;
 * the original object's address alone cannot identify a copied value.
 * Only guest addresses and monotonically increasing IDs cross this ABI.
 */
#ifndef MESSENGER_NVN_TEXTURE_IDENTITY_H
#define MESSENGER_NVN_TEXTURE_IDENTITY_H
#include <stdint.h>
#include <string.h>
#define MESSENGER_TEXTURE_OBJECT_BYTES 192U
struct messenger_texture_identity { uint64_t words[24]; };
static struct messenger_texture_identity messenger_texture_identity_make(uint64_t id,uint64_t origin) {
    struct messenger_texture_identity token={{UINT64_C(0x315845544e53454d),id,origin,~id}};
    return token;
}
static int messenger_texture_identity_matches(const void *bytes,uint64_t id,uint64_t origin) {
    struct messenger_texture_identity token=messenger_texture_identity_make(id,origin);
    return id && !memcmp(bytes,&token,sizeof(token));
}
_Static_assert(sizeof(struct messenger_texture_identity)==MESSENGER_TEXTURE_OBJECT_BYTES,"SDK 5.4 texture size");
#endif
