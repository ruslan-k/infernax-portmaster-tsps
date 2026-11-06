/* Pinned native-nearest domains only. The draw adapter must prove the
 * unmodified native threshold=1/255, opacity=1, vertex alpha=1, normalized
 * RGBA8 nearest samplers, and (for93) two 4-bit palette-index channels.
 * These replacements are NOT admitted by shader identity alone. */
static char *zero_advent_replace_one(const char *source,const char *from,const char *to) {
    size_t n=strlen(source),a=strlen(from),b=strlen(to);
    const char *at=strstr(source,from);if(!at||strstr(at+a,from)||n>32768)return NULL;
    char *out=malloc(n-a+b+1);if(!out)return NULL;
    size_t prefix=(size_t)(at-source);memcpy(out,source,prefix);memcpy(out+prefix,to,b);
    memcpy(out+prefix+b,at+a,n-prefix-a+1);return out;
}
static char *zero_advent_mali_source(const char *source,unsigned index) {
    if(index==163)return zero_advent_replace_one(source,
        "if (_463 < zero_f_b1_o0.x)","if (floor(_463 * 255.0 + 0.5) < 1.0)");
    if(index!=93)return NULL;
    char *first=zero_advent_replace_one(source,
        "float _153 = _149.z * 255.0;","float _153 = floor(_149.z * 255.0 + 0.5);");
    if(!first)return NULL;
    char *out=zero_advent_replace_one(first,
        "float _156 = _149.y * 4080.0 + _153;",
        "float _156 = floor(_149.y * 255.0 + 0.5) * 16.0 + _153;");
    free(first);return out;
}
