/* Opt-in upload representation, immutable for each texture identity. Called
 * inside the existing GL/completion lock; no worker or guest data conversion. */
#ifndef NVN_CPU_PACKED_H
#define NVN_CPU_PACKED_H
#include <pthread.h>
static pthread_once_t nvn_cpu_packed_once=PTHREAD_ONCE_INIT;
static unsigned nvn_cpu_packed_enabled;
static void nvn_cpu_packed_configure(void) {
    const char *value=getenv("MEGAMAN_CPU_PACKED16");
    if(value&&strcmp(value,"0")&&strcmp(value,"1"))stop("NVN_LC2_CPU_PACKED_OPTION",nvn_caller,0,24);
    nvn_cpu_packed_enabled=value&&!strcmp(value,"1");
    if(nvn_cpu_packed_enabled)printf("NVN_LC2_CPU_PACKED16 enabled=1 family=39/97 pool=0x22 flags=0x10 snapshot=original-cpu upload-bytes-per-pixel=2\n");
}
static unsigned nvn_cpu_packed_candidate(const struct nvn_texture *t) {
    pthread_once(&nvn_cpu_packed_once,nvn_cpu_packed_configure);
    if(!nvn_cpu_packed_enabled||!nvn_cpu_image_family(t))return 0;
    if(t->b.target!=1||t->b.depth!=1||t->b.levels!=1||t->b.samples||
       !t->b.width||!t->b.height||t->b.width>4096||t->b.height>4096||
       t->b.packaged||t->b.packaged_layout||t->b.gl_name||t->host_half||t->host_scaled||t->packaged_lazy||
       t->composition_target_seen||t->b.swizzle[0]!=2||t->b.swizzle[1]!=3||t->b.swizzle[2]!=4||t->b.swizzle[3]!=5)
        stop("NVN_LC2_CPU_PACKED_CONTRACT",nvn_caller,t->id,20);
    return nvn_texture_packed_type(t->b.format);
}
static unsigned nvn_cpu_image_upload_type(const struct nvn_texture *t) {
    if(zero_texture_float_channels(t->b.format) || zero_texture_unorm16_channels(t->b.format) || zero_bc_float(t->b.format))return GL_FLOAT;
    if(zero_texture_half_channels(t->b.format))return nvn_texture_upload_gl_type(0x8d61U,nvn_texture_context_major());
    if(!t->host_upload_type)return GL_UNSIGNED_BYTE;
    if(!nvn_cpu_image_family(t)||t->host_upload_type!=nvn_texture_packed_type(t->b.format))
        stop("NVN_LC2_CPU_PACKED_TYPE",nvn_caller,t->id,24);
    return t->host_upload_type;
}
#endif
