/* LC2 SDK format table 0x99f2b8, stride 0x34; native declarations for
 * position/texcoord at main+0x35ecf8..0x35ed1c. Integer RG8 values are passed
 * numerically, without normalization, into the translated highp vertex stage. */
struct nvn_vertex_format {unsigned components,bytes,kind;GLenum type;GLboolean normalized;};
static struct nvn_vertex_format nvn_vertex_format_decode(unsigned format) {
    switch(format) {
    case 0x0a:return (struct nvn_vertex_format){1,4,0,GL_FLOAT,GL_FALSE};
    case 0x16:return (struct nvn_vertex_format){2,8,0,GL_FLOAT,GL_FALSE};
    case 0x22:return (struct nvn_vertex_format){3,12,0,GL_FLOAT,GL_FALSE};
    case 0x2e:return (struct nvn_vertex_format){4,16,0,GL_FLOAT,GL_FALSE};
    case 0x25:return (struct nvn_vertex_format){4,4,0,GL_UNSIGNED_BYTE,GL_TRUE};
    /* SDK table word 0x11e00000: NVIDIA R16_G16, numerical UNORM. */
    case 0x12:return (struct nvn_vertex_format){2,4,0,GL_UNSIGNED_SHORT,GL_TRUE};
    case 0x10:return (struct nvn_vertex_format){2,2,1,GL_BYTE,GL_FALSE};
    case 0x0f:return (struct nvn_vertex_format){2,2,2,GL_UNSIGNED_BYTE,GL_FALSE};
    /* Zero/ZX integer sprite attributes; original NVN Format enum values.
     * GLES2 converts these8/16-bit fetches exactly into highp numeric floats. */
    case 0x14:return (struct nvn_vertex_format){2,4,2,GL_UNSIGNED_SHORT,GL_FALSE};
    case 0x15:return (struct nvn_vertex_format){2,4,1,GL_SHORT,GL_FALSE};
    case 0x27:return (struct nvn_vertex_format){4,4,2,GL_UNSIGNED_BYTE,GL_FALSE};
    case 0x28:return (struct nvn_vertex_format){4,4,1,GL_BYTE,GL_FALSE};
    case 0x2c:return (struct nvn_vertex_format){4,8,2,GL_UNSIGNED_SHORT,GL_FALSE};
    case 0x2d:return (struct nvn_vertex_format){4,8,1,GL_SHORT,GL_FALSE};
    default:stop("NVN_VERTEX_FORMAT_REQUIRED",nvn_caller,format,20);
    }
    return (struct nvn_vertex_format){0};
}
