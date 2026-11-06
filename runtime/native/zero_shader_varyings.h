/* Logical NVN locations are names, not physical GLES varying registers.
 * Keep all native calculations. Outputs unread by the selected fragment
 * become private GLSL variables, so only the live interface consumes Mali
 * varyings. This also preserves any reads of an output inside the vertex.
 */
static char *zero_shader_vertex_varyings(const char *source,const unsigned *reads,unsigned slots) {
    size_t n=strlen(source);if(n>65536 || slots>16)return NULL;
    char *out=malloc(n+1);if(!out)return NULL;memcpy(out,source,n+1);
    for(unsigned i=0;i<slots;i++)if(!reads[i]) {
        char needle[64];snprintf(needle,sizeof(needle),"varying vec4 zero_vary%u;",i);
        char *at=strstr(out,needle);
        if(!at)continue;
        if(strstr(at+strlen(needle),needle)){free(out);return NULL;}
        /* Equal-sized spaces avoid touching expressions, IDs or line count. */
        memset(at,' ',8);
    }
    return out;
}
