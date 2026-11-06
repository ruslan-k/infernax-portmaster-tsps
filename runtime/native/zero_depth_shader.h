/* The two pinned depth-copy fragments write RGBA=0 and
 * depth=sample(depthTexture, perspectiveUV).r + one scalar constant.
 * Their depth output may be lowered only at DRAW time with a proven uniform
 * depth source, or when no depth attachment observes that output. GLES2 then
 * executes the original geometry with a constant depth range. Other shaders
 * and nonuniform inputs never receive this lowering. */
static inline unsigned zero_shader_hash_equal(const uint8_t hash[32],const char *hexadecimal) {
    static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<32;i++)if(digits[hash[i]>>4]!=hexadecimal[i*2] || digits[hash[i]&15]!=hexadecimal[i*2+1])return 0;
    return 1;
}
/* The contracts below index the Mega Man Zero/ZX catalogue; Terminator 2D has its own catalogue, so they
 * are compiled out here (an index collision would otherwise stop the run with ZERO_DEPTH_SHADER_IDENTITY). */
static unsigned zero_depth_shader_bank(unsigned index) {
#if ZERO_ZX_SHADER_PACKAGES && !defined(ANIMAL_WELL_SHADERS) && !defined(TERMINATOR2D)
    static const struct {unsigned index,bank;const char *code,*control,*source;} contracts[]={
        {108,2,"47768bac904185830ce969115b9bf29ac8f227aa1e48d53897b7f969a67b8f81","2c92fa462dca601be292cd7dcaf423da4d5bba535ce96e8929de6dc14a197f52","158c1fe1f28377d9ad66de666bc454a37007e24ab3477e42d3eed3e0336f7e80"},
        {141,3,"b851bc22b47c1887c559d74936e2cff2b3f5aaede4a290afe67de2326a3eca43","ca8789ec672976fdbc9ab008ca1c9d95b2d1934c0000638c31f9b0ed0cab4f72","6224442b9dfe4ddabee56476622c214bb02f1415d7d5bcb66ac78dc268eedb50"}};
    for(unsigned i=0;i<2;i++)if(index==contracts[i].index) {
        const struct mm_shader_binding *m=&nvn_megaman_shader_bindings[index];
        const char *source=nvn_shader_sources[index].gles;uint8_t hash[32];
        if(!source)stop("ZERO_DEPTH_SHADER_SOURCE_MISSING",nvn_caller,index,24);
        zero_sha256(source,strlen(source),hash);
        if(!zero_shader_hash_equal(m->code_hash,contracts[i].code) || !zero_shader_hash_equal(m->control_hash,contracts[i].control) ||
           !zero_shader_hash_equal(hash,contracts[i].source))stop("ZERO_DEPTH_SHADER_IDENTITY",nvn_caller,index,24);
        return contracts[i].bank;
    }
#else
    (void)index;
#endif
    return 0;
}
static const char zero_depth_fragment[]="#version 100\nprecision mediump float;\nvoid main(){gl_FragColor=vec4(0.0);}\n";
