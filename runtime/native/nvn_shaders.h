/* Offline shader identity: native ProgramSetShaders selects the stages. */
#include "nvn_shader_sources.h"
#if ZERO_ZX_SHADER_PACKAGES
#include "zero_sha256.h"
#endif
#include "t2_shader_class.h"
#include "nvn_clip_control.h"
#include "megaman_program_stages.h"
#include "nvn_wave_fold_shader.h"
#include "zero_depth_shader.h"
#include "zero_shader_precision.h"
#include "zero_advent_mali_contract.h"
#include "zero_advent_mali_shader.h"
#include "zero_mip_clamp_shader.h"
#include "zero_r8_shader.h"
#include "zero_shader_varyings.h"
#ifndef ZERO_SHADER_VARYING_SLOTS
#define ZERO_SHADER_VARYING_SLOTS 8
#endif
#ifndef LC2_MAIN_BASE
#error "The verified LC2 image contract must define LC2_MAIN_BASE"
#endif
struct nvn_uniform {char name[64];uint32_t stage,binding,offset,size,kind,texel_fetch_bits,cpu_view,word_int,separate_sampler,sampler_offset;GLint location,size_location;
    GLint zero_mip_mask,zero_mip_last,zero_r8_mode,zero_r8_add,zero_r8_use,swizzle_location;unsigned zero_mip_slot;
    GLint t2_palette,t2_blockmap,t2_compact,t2_compact4;unsigned t2_sampler;   /* palette/map/compact-domain uniforms, per fragment sampler */};
/* kind: 1 mat4, 2 vec4, 3 handle, 4 float, 5 vec2, 6 native uint32 != 0, 7 vec4 array (Animal Well dynamic cbuffer range). */
static struct {unsigned attribute_count;struct {char name[64];uint32_t location;} attributes[16];} nvn_compilation;
struct nvn_alpha_program {GLuint name;GLint locations[64],clip_control;};
struct nvn_program {
    uint64_t object,id;GLuint name;unsigned live,linked,sources[2],stage_mask,compute_only;
    GLint view_proj,view,mod_color,add_color,texture,alpha_ref,alpha_func,clip_control;
    GLint wave_lookup,wave_inverse_rows,wave_chroma,wave_viewport,wave_target_height;unsigned wave_contract;
    GLint momo_tmp_sprite;
    struct nvn_uniform uniforms[64];unsigned uniform_count;
    unsigned attribute_kind[16],attribute_used[16],color_write_mask,mediump_contract,zero_depth_bank,zero_diagnostic_draws,zero_mediump;
    unsigned zero_mip_wrapper,zero_r8_wrapper,aw_float_key;GLint zero_scale_xy,aw_float_range_location,aw_light_tiles_location,aw_noise_location;
    unsigned t2_page_key,t2_page_mode;   /* Frente F: bit j = fragment sampler j reads a palettized page; mode 1 idx8 / 2 idx16 */
    struct nvn_alpha_program alpha5;
};
static struct nvn_program nvn_programs[256];
#include "momo_swizzle_flatten.h"
#include "momo_tmp_sprite.h"
#include "nvn_alpha_specialization.h"
static struct nvn_program *nvn_program_find(uint64_t object) {
    for(unsigned i=0;i<256;i++)if(nvn_programs[i].live&&nvn_programs[i].object==object)return &nvn_programs[i];
    stop("NVN_PROGRAM_INVALID",nvn_caller,object,24);return NULL;
}
#include "diagnostic_dump.h"
static uint64_t nvn_read64(uint64_t address) {uint64_t value;memcpy(&value,nvn_pointer(address,8,PROT_READ),8);return value;}
static void *nvn_gpu_pointer(uint64_t address,uint64_t size,unsigned perm);
static unsigned nvn_megaman_shader_match(uint64_t code,uint64_t control) {
#if defined(ZERO_ZX_SHADER_DISCOVERY) && ZERO_ZX_SHADER_DISCOVERY
    const uint8_t *header=nvn_pointer(control,0x20,PROT_READ);
    printf("ZERO_ZX_SHADER_DISCOVERY code=0x%"PRIx64" control=0x%"PRIx64" header=",code,control);
    for(unsigned i=0;i<0x20;i++)printf("%02x",header[i]);
    printf("\n");
    nvn_private_dump("zero-shader-control",0,nvn_pointer(control,0x100,PROT_READ),0x100);
    nvn_private_dump("zero-shader-code",0,nvn_gpu_pointer(code,0x200,PROT_READ),0x200);
    stop("ZERO_ZX_SHADER_TRANSLATION_REQUIRED",nvn_caller,control,20);
    return UINT32_MAX;
#elif ZERO_ZX_SHADER_PACKAGES
#if defined(MOMO_SHADERS)
    /* Momodora (Unity 2021.3 / NVN 18.3.2): GLSLC control section 0x880 bytes (size at +0x20, version 16),
     * GPU code size at +0x704. MEASURED, docs/SHADERS.md section 1.
     * Terminator's NVN 19.3.9 uses +0x70c for varying values, including 0xc on GRAPHICS.
     * ProgramSetShaders classifies by SPH / exact compute identity below, never by that field. */
    const uint8_t *head=nvn_pointer(control,0x880,PROT_READ);
    unsigned control_size=read32(head+0x20),code_size=read32(head+0x704);
    if(read32(head)!=0x98761234 || control_size!=0x880 || code_size<0x80 || code_size>131072)
        stop("ZERO_SHADER_CONTROL_LAYOUT",nvn_caller,control,24);
#elif defined(ANIMAL_WELL_SHADERS)
    /* NVN 17.5.3 (Animal Well BNSH): control section 0x880 bytes (size at +0x20), GPU code size at +0x704 as before. */
    const uint8_t *head=nvn_pointer(control,0x880,PROT_READ);
    unsigned control_size=read32(head+0x20),code_size=read32(head+0x704);
    if(read32(head)!=0x98761234 || control_size!=0x880 || code_size<0x80 || code_size>131072)
        stop("ZERO_SHADER_CONTROL_LAYOUT",nvn_caller,control,24);
#else
    const uint8_t *head=nvn_pointer(control,0x850,PROT_READ);
    unsigned control_size=read32(head+0x20),code_size=read32(head+0x704);
    if(read32(head)!=0x98761234 || control_size!=0x850 || code_size<0x80 || code_size>32768)
        stop("ZERO_SHADER_CONTROL_LAYOUT",nvn_caller,control,24);
#endif
    uint8_t code_hash[32],control_hash[32];
    zero_sha256(head,control_size,control_hash);
    zero_sha256(nvn_gpu_pointer(code,code_size,PROT_READ),code_size,code_hash);
    for(unsigned i=0;i<sizeof(nvn_megaman_shader_bindings)/sizeof(nvn_megaman_shader_bindings[0]);i++) {
        const struct mm_shader_binding *m=&nvn_megaman_shader_bindings[i];
#ifdef MOMO_SHADERS
        unsigned exact_control=1;
#ifdef INFERNAX_STRICT_SHADER_FIRST
        if(i>=INFERNAX_STRICT_SHADER_FIRST)exact_control=!memcmp(control_hash,m->control_hash,32);
#endif
        if(m->code_size==code_size && m->control_size==control_size && !memcmp(code_hash,m->code_hash,32) && exact_control) {
#ifdef INFERNAX_STRICT_SHADER_FIRST
            if(i>=INFERNAX_STRICT_SHADER_FIRST) {
                static unsigned char receipt[sizeof(nvn_megaman_shader_bindings)/sizeof(nvn_megaman_shader_bindings[0])];
                if(!receipt[i]) {
                    printf("INFERNAX_EXACT_SHADER index=%u stage=%u code-sha256=",i,nvn_shader_sources[i].stage);
                    for(unsigned k=0;k<32;k++)printf("%02x",code_hash[k]);
                    printf(" control-sha256=");for(unsigned k=0;k<32;k++)printf("%02x",control_hash[k]);putchar('\n');
                    receipt[i]=1;
                }
            }
#endif
            /* MOMO_CONTROL_DUMP=<index>: the control section of every program that resolves to that
             * catalogue entry. Two Unity programs can share identical machine code and differ only in
             * their control section, and the entry is matched by the code hash alone; the constants of
             * one of them are read one vec4 early (MEDIDO run momo-174). */
            static unsigned dumped;
            const char *capture_all=getenv("NX_SHADER_DUMP_ALL");
            static unsigned char matched_dumped[sizeof(nvn_megaman_shader_bindings)/sizeof(nvn_megaman_shader_bindings[0])];
            if(capture_all && !strcmp(capture_all,"1") && !matched_dumped[i]) {
                nvn_private_dump("matched-control",i,head,control_size);
                nvn_private_dump("matched-code",i,nvn_gpu_pointer(code,code_size,PROT_READ),code_size);
                printf("INFERNAX_MATCHED_CAPTURE index=%u code-size=%u control-size=%u code-sha256=",i,code_size,control_size);
                for(unsigned k=0;k<32;k++)printf("%02x",code_hash[k]);
                printf(" control-sha256=");for(unsigned k=0;k<32;k++)printf("%02x",control_hash[k]);putchar('\n');
                matched_dumped[i]=1;
            }
            const char *want=getenv("MOMO_CONTROL_DUMP");
            if(want && (unsigned)strtoul(want,NULL,0)==i && dumped<4) {
                nvn_private_dump("control",i*16+dumped,head,control_size);
                printf("MOMO_CONTROL_DUMP index=%u sequence=%u control-hash=%02x%02x%02x%02x\n",
                    i,dumped,control_hash[0],control_hash[1],control_hash[2],control_hash[3]);
                dumped++;
            }
            return i;
        }
#else
        if(m->code_size==code_size && m->control_size==control_size &&
           !memcmp(code_hash,m->code_hash,32) && !memcmp(control_hash,m->control_hash,32))return i;
#endif
    }
    {const char *all=getenv("NX_SHADER_DUMP_ALL");
     if(all&&!strcmp(all,"1")) {
        static unsigned seq;
        nvn_private_dump("program-control",seq,head,control_size);
        nvn_private_dump("program-code",seq,nvn_gpu_pointer(code,code_size,PROT_READ),code_size);
        printf("ZERO_SHADER_CAPTURED seq=%u code-bytes=%u control-bytes=%u\n",seq++,code_size,control_size);
        return UINT32_MAX;
     }}
    printf("ZERO_SHADER_UNRECOGNIZED code-bytes=%u control-bytes=%u code-hash=",code_size,control_size);
    for(unsigned i=0;i<32;i++)printf("%02x",code_hash[i]);
    printf(" control-hash=");for(unsigned i=0;i<32;i++)printf("%02x",control_hash[i]);putchar('\n');
#if MEGAMAN_HOST_DIAGNOSTIC
    nvn_private_dump("zero-shader-control",0,head,control_size);
    nvn_private_dump("zero-shader-code",0,nvn_gpu_pointer(code,code_size,PROT_READ),code_size);
#endif
    {   /* No catalogue entry: hand the exact program bytes to the translator. */
        const char *want=getenv("NX_SHADER_DUMP");
        if(want && !strcmp(want,"1")) {
            nvn_private_dump("program-control",0,head,control_size);
            nvn_private_dump("program-code",0,nvn_gpu_pointer(code,code_size,PROT_READ),code_size);
        }
    }
    stop("ZERO_SHADER_NOT_PINNED",nvn_caller,control,20);return UINT32_MAX;
#else
    const uint8_t *head=nvn_pointer(control,0x20,PROT_READ);
    if(read32(head)!=0x98761234)stop("NVN_LC2_SHADER_CONTROL_MAGIC",nvn_caller,control,24);
    unsigned selected=UINT32_MAX;
    for(unsigned i=0;i<sizeof(nvn_megaman_shader_bindings)/sizeof(nvn_megaman_shader_bindings[0]);i++) {
        const struct mm_shader_binding *m=&nvn_megaman_shader_bindings[i];
        const uint8_t *expected=nvn_pointer(LC2_MAIN_BASE+m->control_module_offset,m->control_size,PROT_READ);
        if(memcmp(head,expected,0x20))continue;
        if(memcmp(nvn_pointer(control,m->control_size,PROT_READ),expected,m->control_size))continue;
        if(memcmp(nvn_gpu_pointer(code,m->code_size,PROT_READ),
                  nvn_pointer(LC2_MAIN_BASE+m->code_module_offset,m->code_size,PROT_READ),m->code_size))continue;
        const void *original=nvn_pointer(LC2_MAIN_BASE+m->source_module_offset,m->source_size,PROT_READ);
        if(strlen(nvn_shader_sources[i].original)!=m->source_size || memcmp(original,nvn_shader_sources[i].original,m->source_size))
            stop("NVN_LC2_SHADER_SOURCE_CHANGED",nvn_caller,i,24);
        /* Duplicate strings may also have identical copied controls. Their
         * stage/source/translation identity must agree. Prefer native address. */
        if(selected!=UINT32_MAX && (strcmp(nvn_shader_sources[selected].original,nvn_shader_sources[i].original) ||
                                   nvn_shader_sources[selected].stage!=nvn_shader_sources[i].stage))
            stop("NVN_LC2_SHADER_MATCH_AMBIGUOUS",nvn_caller,i,24);
        if(selected==UINT32_MAX || control==LC2_MAIN_BASE+m->control_module_offset)selected=i;
    }
    if(selected==UINT32_MAX)stop("NVN_LC2_SHADER_NOT_PINNED",nvn_caller,control,20);
    const struct mm_shader_binding *m=&nvn_megaman_shader_bindings[selected];
    if(!nvn_shader_sources[selected].gles || m->error) {
        printf("NVN_LC2_SHADER_UNSUPPORTED index=%u stage=%u reason=%s\n",selected,nvn_shader_sources[selected].stage,m->error?m->error:"missing translation");
        stop("NVN_LC2_SHADER_TRANSLATION_REQUIRED",nvn_caller,selected,20);
    }
    return selected;
#endif
}
static void nvn_megaman_uniforms(struct nvn_program *p,unsigned source) {
    const struct mm_shader_binding *m=&nvn_megaman_shader_bindings[source];
    if(m->uniform_count>64 || m->sampler_count>8 || m->uniform_count+m->sampler_count>64-p->uniform_count)
        stop("NVN_LC2_UNIFORM_CAPACITY",nvn_caller,source,24);
    for(unsigned i=0;i<m->uniform_count;i++) {
        const struct mm_shader_uniform *v=&m->uniforms[i];struct nvn_uniform *u=&p->uniforms[p->uniform_count++];
        if(v->stage>1 || v->binding>=16 || (v->components!=1&&v->components!=2&&v->components!=4) || (v->size!=v->components*4 && !(v->word_array && v->components==4 && v->size==v->word_array*16 && v->word_array<=4096)) || strlen(v->name)>=sizeof(u->name))
            stop("NVN_LC2_UNIFORM_METADATA",nvn_caller,source,24);
        strcpy(u->name,v->name);u->stage=v->stage;u->binding=v->binding;u->offset=v->offset;u->size=v->size;
        u->kind=v->components==4?2:v->components==2?5:4;u->size_location=-1;
        /* A vec4 array is uploaded as a whole range, not as one vec4: LC2 titles declare 64 KB
         * constant banks this way, and the draw path validates kind 7 against u->size. */
        if(v->word_array)u->kind=7;
#ifdef ANIMAL_WELL_SHADERS
        u->word_int=v->word_int;if(u->word_int && u->kind!=2)stop("AW_WORD_INT_UNIFORM_LAYOUT",nvn_caller,source,24);
#endif
#if defined(ZERO_WORD_BOOLEAN_UNIFORMS) && ZERO_WORD_BOOLEAN_UNIFORMS
        if(v->word_boolean) {
            unsigned bool_binding=source==135?2u:3u;
            unsigned bool_limit=(source==132||source==135)?28u:16u;
            if(v->word_boolean!=1 || (source!=163&&source!=93&&source!=132&&source!=135) || v->stage!=1 || v->binding!=bool_binding || v->offset>bool_limit || v->offset%4 || v->components!=1)
                stop("ZERO_BOOLEAN_UNIFORM_CONTRACT",nvn_caller,source,24);
            u->kind=6;
        }
#endif
    }
    for(unsigned i=0;i<m->sampler_count;i++) {
        const struct mm_shader_sampler *v=&m->samplers[i];struct nvn_uniform *u=&p->uniforms[p->uniform_count++];
        if(v->stage!=1 || v->binding>=16 || strlen(v->name)>=sizeof(u->name) ||
           (v->texel_fetch_bits!=0&&v->texel_fetch_bits!=8&&v->texel_fetch_bits!=16) || (v->texel_fetch_bits&&!v->size_query) ||
           v->cpu_view>3 || (v->cpu_view && (v->texel_fetch_bits || v->size_query)))
            stop("NVN_LC2_SAMPLER_METADATA",nvn_caller,source,24);
        strcpy(u->name,v->name);u->stage=v->stage;u->binding=v->binding;u->offset=v->offset;u->size=8;u->kind=3;u->size_location=-1;
        u->texel_fetch_bits=v->texel_fetch_bits;u->cpu_view=v->cpu_view;
#ifdef INFERNAX_SEPARATE_SAMPLERS
        if(v->separate_sampler>1 || (v->separate_sampler && (v->binding!=14 ||
           v->offset<0x148 || (v->offset-0x148)%8 || (v->offset-0x148)/8>=128 ||
           v->sampler_offset<0x568 || (v->sampler_offset-0x568)%8 || (v->sampler_offset-0x568)/8>=128)))
            stop("NVN_LC2_SEPARATE_SAMPLER_METADATA",nvn_caller,source,24);
        u->separate_sampler=v->separate_sampler;u->sampler_offset=v->sampler_offset;
#endif
        u->zero_mip_mask=u->zero_mip_last=-1;u->zero_mip_slot=i;
        u->t2_palette=u->t2_blockmap=u->t2_compact=u->t2_compact4=-1;u->t2_sampler=i;
        if(p->t2_page_key&(1u<<i)) {
            char name[96];snprintf(name,sizeof(name),"t2_palette_%s",v->name);u->t2_palette=glGetUniformLocation(p->name,name);
            snprintf(name,sizeof(name),"t2_blockmap_%s",v->name);u->t2_blockmap=glGetUniformLocation(p->name,name);
            if(p->t2_page_mode==T2_PAGE_MODE_COMPACT) {
                snprintf(name,sizeof name,"t2_compact_%s",v->name);u->t2_compact=glGetUniformLocation(p->name,name);
                if(u->t2_compact<0)stop("T2_COMPACT_UNIFORM",nvn_caller,source,24);
                if(t2_compact_nibble_format) {
                    snprintf(name,sizeof name,"t2_compact4_%s",v->name);u->t2_compact4=glGetUniformLocation(p->name,name);
                    if(u->t2_compact4<0)stop("T2_COMPACT4_UNIFORM",nvn_caller,source,24);
                }
            }
            if(u->t2_palette<0 || (p->t2_page_mode!=T2_PAGE_MODE_IDX16 && u->t2_blockmap<0))stop("T2_PAGE_VARIANT_UNIFORMS",nvn_caller,source,24);
        }
        /* The ESSL lowering routes every sample through ms_apply_swizzle(c,ms_swizzle_<name>). Without
         * this upload the selector vector stays at its default (0,0,0,0), which means "ZERO in every
         * channel": every textured fragment came out transparent black and the whole game rendered as a
         * black screen (MEDIDO run momo-124: forcing the fragment colour showed the geometry was there). */
        {char name[80];snprintf(name,sizeof(name),"ms_swizzle_%s",v->name);u->swizzle_location=glGetUniformLocation(p->name,name);}
        u->zero_r8_mode=-1;u->zero_r8_add=-1;u->zero_r8_use=-1;
        if(p->zero_r8_wrapper) {
            char name[40];snprintf(name,sizeof(name),"zero_r8_mul_%u",i);u->zero_r8_mode=glGetUniformLocation(p->name,name);
            snprintf(name,sizeof(name),"zero_r8_add_%u",i);u->zero_r8_add=glGetUniformLocation(p->name,name);
            snprintf(name,sizeof(name),"zero_r8_use_%u",i);u->zero_r8_use=glGetUniformLocation(p->name,name);
        }
        if(p->zero_mip_wrapper) {
            char name[40];snprintf(name,sizeof(name),"zero_mip_mask_%u",i);u->zero_mip_mask=glGetUniformLocation(p->name,name);
            snprintf(name,sizeof(name),"zero_mip_last_%u",i);u->zero_mip_last=glGetUniformLocation(p->name,name);
        }
        if(v->size_query) {
            char name[80];snprintf(name,sizeof(name),"%s_size",v->name);u->size_location=glGetUniformLocation(p->name,name);
        }
    }
}

/* ZERO_FILTER_PASSTHROUGH=1 (13/09, fps-first per Felipe): the Type 1 filter
 * fragment 139 (xBRZ-like, 3128 vertex words, never translated) is replaced by
 * a plain texture copy so the game can run with its default filter. The
 * native code/control hashes of entry 139 are kept, so identification is
 * unchanged; only the GLES text and the fragment interface are substituted. */
static void zero_filter_passthrough_install(void) {
    static int configured;if(configured)return;configured=1;
    const char *o=getenv("ZERO_FILTER_PASSTHROUGH");if(!o||strcmp(o,"1"))return;
    static const char gles[]="#version 100\nprecision mediump float;\nuniform mediump sampler2D fs_tex2_20;\nvarying vec4 zero_vary0;\nvoid main(){gl_FragColor=texture2D(fs_tex2_20,zero_vary0.xy);}\n";
    const unsigned index=139;
    if(index>=sizeof(nvn_megaman_shader_bindings)/sizeof(nvn_megaman_shader_bindings[0]))return;
    long page=sysconf(_SC_PAGESIZE);
    void *entries[2]={(void *)&nvn_shader_sources[index],(void *)&nvn_megaman_shader_bindings[index]};
    size_t sizes[2]={sizeof(nvn_shader_sources[index]),sizeof(nvn_megaman_shader_bindings[index])};
    for(unsigned i=0;i<2;i++) {
        uintptr_t start=(uintptr_t)entries[i]&~(uintptr_t)(page-1),end=((uintptr_t)entries[i]+sizes[i]+page-1)&~(uintptr_t)(page-1);
        if(mprotect((void *)start,end-start,PROT_READ|PROT_WRITE))stop("ZERO_FILTER_PASSTHROUGH_MPROTECT",nvn_caller,errno,21);
    }
    struct mm_shader_binding *m=(struct mm_shader_binding *)entries[1];
    m->error=NULL;m->requires_fragment_highp=0;m->requires_fragment_mediump=1;m->uniform_count=0;m->color_write_mask=15;
    m->sampler_count=1;m->samplers[0]=(struct mm_shader_sampler){.name="fs_tex2_20",.stage=1,.binding=14};
    memset(m->varying_read_masks,0,sizeof(m->varying_read_masks));m->varying_read_masks[0]=3;
    const char **gles_slot=(const char **)((char *)entries[0]+offsetof(typeof(nvn_shader_sources[0]),gles));*gles_slot=gles;
    printf("ZERO_FILTER_PASSTHROUGH enabled=1 source=%u replacement=texture-copy filter=Type1-not-emulated\n",index);
}
/* Called only by ordered GL replay, with its context already held. */
static void nvn_program_link(struct nvn_program *p) {
        zero_filter_passthrough_install();
        /* Native initialization also registers unused programs. Translation
         * is required before any real draw executes the selected stage pair. */
        for(unsigned stage=0;stage<2;stage++) {
            unsigned source=p->sources[stage];const struct mm_shader_binding *m=&nvn_megaman_shader_bindings[source];
            if(!nvn_shader_sources[source].gles || m->error) {
                printf("ZERO_SHADER_EXECUTION_UNSUPPORTED pair=%u/%u source=%u reason=%s\n",p->sources[0],p->sources[1],source,m->error?m->error:"missing translation");
                stop("ZERO_SHADER_TRANSLATION_REQUIRED",nvn_caller,source,20);
            }
        }
        const struct mm_shader_binding *vertex=&nvn_megaman_shader_bindings[p->sources[0]],*fragment=&nvn_megaman_shader_bindings[p->sources[1]];
        p->zero_depth_bank=zero_depth_shader_bank(p->sources[1]);
#if ZERO_ZX_GAME
#ifdef ANIMAL_WELL_SHADERS
        /* Animal Well samples only RGBA8 textures (48 of 48, format 0x25 incl. emulated float targets): the R8 channel
         * wrapper (two conditionals around every texture2D) is pure fill cost here; an R8 sample stops fail-closed. */
        p->zero_r8_wrapper=0;
#else
        p->zero_r8_wrapper=fragment->sampler_count && !p->zero_depth_bank;
#endif
#ifdef ANIMAL_WELL_SHADERS
        /* Animal Well textures are single-level (levels==1 measured); the mip mask wrapper would need 2 units per sampler (6-sampler fragments exceed the 8 Mali-450 units). Fail-closed check in nvn_render.h. */
        p->zero_mip_wrapper=0;
#else
        p->zero_mip_wrapper=fragment->sampler_count && !p->zero_depth_bank && !zero_mip_native_limit();
#endif
        if(p->zero_mip_wrapper) {
            const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
            if(!extensions || !strstr(extensions,"GL_EXT_shader_texture_lod"))stop("ZERO_MIP_LOD_BACKEND_REQUIRED",nvn_caller,p->sources[1],20);
        }
#endif
        if(fragment->wave_contract && (p->sources[0]!=17 || p->sources[1]!=16))
            stop("NVN_LC2_WAVE_STAGE_PAIR_REQUIRED",nvn_caller,p->sources[0],20);
        unsigned varying_vectors=0;
        for(unsigned i=0;i<ZERO_SHADER_VARYING_SLOTS;i++)if(fragment->varying_read_masks[i]) {
            varying_vectors++;
            if(fragment->varying_read_masks[i]&~vertex->varying_write_masks[i])
            stop("NVN_LC2_SHADER_UNDEFINED_VARYING",nvn_caller,i,20);
        }
        GLint varying_limit=0;glGetIntegerv(GL_MAX_VARYING_VECTORS,&varying_limit);
        if(varying_vectors>(unsigned)varying_limit)stop("ZERO_SHADER_ACTIVE_VARYING_CAPACITY",nvn_caller,varying_vectors,20);
        if(vertex->attribute_count>16)stop("NVN_LC2_SHADER_ATTRIBUTE_COUNT",nvn_caller,vertex->attribute_count,24);
        nvn_compilation.attribute_count=vertex->attribute_count;
        for(unsigned i=0;i<vertex->attribute_count;i++) {
            const struct mm_shader_attribute *v=&vertex->attributes[i];
            if(v->location>=16 || v->location>=(unsigned)nvn_device.attributes || v->kind>2 || strlen(v->name)>=64)
                stop("NVN_LC2_SHADER_ATTRIBUTE",nvn_caller,v->location,24);
            nvn_compilation.attributes[i].location=v->location;strcpy(nvn_compilation.attributes[i].name,v->name);
            p->attribute_kind[v->location]=v->kind;
        }
        GLuint shaders[2];
        if(fragment->requires_fragment_highp && !p->zero_depth_bank) {
            GLint range[2]={0},precision=0;glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER,GL_HIGH_FLOAT,range,&precision);
            if(precision<23 || range[0]<62 || range[1]<62) {
                /* ZERO_FRAGMENT_MEDIUMP_ALL=1 (13/09, fps-first per Felipe): admit
                 * any highp fragment at mediump on this Mali (no highp exists), logging
                 * the unpinned source so it can be pinned/compared later. */
                static int all_configured;static unsigned all_mediump;
                if(!all_configured){const char *o=getenv("ZERO_FRAGMENT_MEDIUMP_ALL");all_mediump=o&&!strcmp(o,"1");all_configured=1;
                    if(all_mediump)printf("ZERO_FRAGMENT_MEDIUMP_ALL enabled=1 unpinned-fragments=admitted-at-mediump physical-comparison=none\n");}
                if(!zero_shader_mediump_admitted(p->sources[1]) && !zero_advent_mali_admitted(p->sources[1])) {
                    if(!all_mediump)stop("ZERO_FRAGMENT_PRECISION_REQUIRED",nvn_caller,p->sources[1],20);
                    printf("ZERO_FRAGMENT_MEDIUMP source=%u pinned=0 admitted=ZERO_FRAGMENT_MEDIUMP_ALL\n",p->sources[1]);
                }
                p->zero_mediump=1;
            }
        }
        if(fragment->requires_fragment_mediump || p->zero_mediump) {
            GLint range[2]={0},precision=0;glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER,GL_MEDIUM_FLOAT,range,&precision);
            if(precision<10 || range[0]<14 || range[1]<14)stop("NVN_LC2_FRAGMENT_MEDIUMP_REQUIRED",nvn_caller,precision,20);
            p->mediump_contract=1;
            if(p->zero_mediump)printf("ZERO_FRAGMENT_MEDIUMP source=%u pinned=1 precision=%d range=%d,%d vertex-arithmetic=preserved physical-game-validation=pending\n",p->sources[1],precision,range[0],range[1]);
        }
        for(unsigned i=0;i<2;i++) {
            shaders[i]=glCreateShader(i?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);const char *source=nvn_shader_sources[p->sources[i]].gles;
#ifdef MOMO_SHADERS
            /* MOMO_HIDE_ERROR_SHADER=1 (diagnostic only): Unity paints the whole screen with its
             * InternalErrorShader here. Dropping its fragments shows what is drawn underneath, so a
             * run can tell an empty scene from a scene hidden behind the error pass. Never a fix. */
            if(i) {
                static int configured;static unsigned hide;
                if(!configured){const char *o=getenv("MOMO_HIDE_ERROR_SHADER");hide=o&&!strcmp(o,"1");configured=1;}
                const char *sha=nvn_megaman_shader_bindings[p->sources[i]].sha256;
                if(hide && sha && !strncmp(sha,"815bc3875d5e",12)) {
                    source="#version 100\nprecision mediump float;\nvoid main(){discard;}\n";
                    printf("MOMO_ERROR_SHADER_HIDDEN source=%u sha=%.12s\n",p->sources[i],sha);
                }
            }
#endif
            char *interface_source=NULL;
            if(!i && ZERO_SHADER_VARYING_SLOTS>8) {
                interface_source=zero_shader_vertex_varyings(source,fragment->varying_read_masks,ZERO_SHADER_VARYING_SLOTS);
                if(!interface_source)stop("ZERO_SHADER_VARYING_INTERFACE",nvn_caller,p->sources[i],20);
                source=interface_source;
            }
            if(i && p->zero_depth_bank)source=zero_depth_fragment;
            char *wrapped=NULL;
            if(fragment->wave_contract) {
                wrapped=i?nvn_wave_fold_fragment_source(source):nvn_wave_fold_vertex_source(source);
                if(!wrapped)stop("NVN_LC2_WAVE_WRAPPER_SOURCE",nvn_caller,p->sources[i],20);
                source=wrapped;
            } else if(!i){wrapped=nvn_clip_vertex_source(source);if(!wrapped)stop("NVN_LC2_CLIP_VERTEX_SOURCE",nvn_caller,p->sources[i],20);source=wrapped;}
            char *lowered=NULL;
            if(p->zero_mediump){lowered=zero_shader_mediump_source(source,i);if(!lowered)stop("ZERO_PRECISION_WRAPPER",nvn_caller,p->sources[i],21);source=lowered;}
            char *advent=NULL;
            if(i && p->zero_mediump && zero_advent_mali_pin(p->sources[1])) {
                advent=zero_advent_mali_source(source,p->sources[1]);
                if(!advent)stop("ZERO_ADVENT_PRECISION_WRAPPER",nvn_caller,p->sources[1],20);
                source=advent;
            }
            char *sprite=NULL;
            if(i && momo_tmp_sprite_enabled()) {
                sprite=momo_tmp_sprite_source(source);
                if(sprite)source=sprite;
            }
            char *probe=NULL;
            if(i && momo_tmp_alpha_probe_enabled() && p->sources[1]==776) {
                probe=momo_tmp_alpha_probe_source(source);
                if(probe)source=probe;
            }
            char *premul=NULL;
            if(i && momo_tmp_premultiply_enabled()) {
                premul=momo_tmp_premultiply_source(source);
                if(premul)source=premul;
            }
            char *flat=NULL;
            if(momo_swizzle_flat_enabled()) {
                flat=momo_swizzle_flat_source(source);
                if(flat)source=flat;
            }
            /* Frente F: the palette read of every keyed sampler, BEFORE the R8/mip wrappers so their
             * `texture2D(<name>,` pattern no longer matches the page sampler (t2_page_substitution.h). */
            char *t2page=NULL;
            if(i && p->t2_page_key) {
                const char *names[8];if(fragment->sampler_count>8)stop("T2_PAGE_SAMPLER_CAPACITY",nvn_caller,fragment->sampler_count,20);
                for(unsigned j=0;j<fragment->sampler_count;j++)names[j]=fragment->samplers[j].name;
                t2page=t2_page_fragment_source(source,names,fragment->sampler_count,p->t2_page_key,p->t2_page_mode,p->mediump_contract);
                if(!t2page)stop("T2_PAGE_SHADER_VARIANT",nvn_caller,p->sources[i],20);
                source=t2page;
            }
            char *compact_uv=NULL;
            if(!i && p->t2_page_key && p->t2_page_mode==T2_PAGE_MODE_COMPACT) {
                compact_uv=t2_page_compact_varying_source(source);
                if(!compact_uv)stop("T2_COMPACT_VERTEX_UV",nvn_caller,p->sources[i],21);
                source=compact_uv;
            }
            char *r8=NULL,*mipped=NULL;
            if(i && p->zero_r8_wrapper) {
                const char *names[8];if(fragment->sampler_count>8)stop("ZERO_R8_SAMPLER_CAPACITY",nvn_caller,fragment->sampler_count,20);
                for(unsigned j=0;j<fragment->sampler_count;j++)names[j]=fragment->samplers[j].name;
                r8=zero_r8_source(source,names,fragment->sampler_count,p->mediump_contract);
                if(!r8)stop("ZERO_R8_SHADER_WRAPPER",nvn_caller,p->sources[i],20);
                source=r8;
            }
            if(i && p->zero_mip_wrapper) {
                const char *names[8];if(fragment->sampler_count>8)stop("ZERO_MIP_SAMPLER_CAPACITY",nvn_caller,fragment->sampler_count,20);
                for(unsigned j=0;j<fragment->sampler_count;j++)names[j]=fragment->samplers[j].name;
                mipped=zero_mip_clamp_source(source,names,fragment->sampler_count,p->mediump_contract);
                if(!mipped)stop("ZERO_MIP_SHADER_WRAPPER",nvn_caller,p->sources[i],20);
                source=mipped;
            }
            char *awf=NULL;
            if(i && p->aw_float_key) {
                const char *names[8];unsigned kinds[8];if(fragment->sampler_count>8)stop("AW_FLOAT_SAMPLER_CAPACITY",nvn_caller,fragment->sampler_count,20);
                for(unsigned j=0;j<fragment->sampler_count;j++){names[j]=fragment->samplers[j].name;kinds[j]=aw_float_kind_from_code((p->aw_float_key>>(2*j))&3);}
                awf=aw_float_source(source,names,kinds,fragment->sampler_count,aw_float_kind_from_code((p->aw_float_key>>16)&3),p->mediump_contract);
                if(!awf)stop("AW_FLOAT_SHADER_WRAPPER",nvn_caller,p->sources[i],20);
                source=awf;
            }
            char *scaled=NULL;
            if(i && zero_render_scale_enabled()){scaled=zero_render_scale_fragment(source);source=scaled;}
            char *quad=NULL;
            const char *quad_option=getenv("MOMO_DIAG_QUAD");
            /* MOMO_DIAG_QUAD=<vertex index>: overwrite that vertex program's clip position with the raw
             * position attribute mapped to the 1280x720 UI space. It separates "the transform/uniforms are
             * wrong" from "this draw never reaches the screen". Diagnostic only. */
            if(!i && quad_option && (unsigned)strtoul(quad_option,NULL,0)==p->sources[0]) {
                size_t length=strlen(source);const char *last=strrchr(source,'}');
                if(last) {
                    size_t head=(size_t)(last-source);quad=malloc(length+128);
                    if(quad) {
                        memcpy(quad,source,head);
                        strcpy(quad+head," gl_Position=vec4(in_attr0.x/640.0,in_attr0.y/360.0*nvnClipControl.x,0.0,1.0);\n}\n");
                        source=quad;
                        printf("MOMO_DIAG_QUAD_INSTALLED program=%u vertex=%u fragment=%u\n",p->name,p->sources[0],p->sources[1]);
                    }
                }
            }
            char *noclip=NULL;
            const char *noclip_option=getenv("MOMO_DIAG_NOCLIP");
            /* MOMO_DIAG_NOCLIP=<fragment index>: drop the appended alpha test of that fragment, to tell
             * "the alpha test is discarding this pass" from a shading problem. Diagnostic only. */
            if(i && noclip_option && (unsigned)strtoul(noclip_option,NULL,0)==p->sources[1]) {
                static const char *tail="if(!pass)discard;";
                const char *at=strstr(source,tail);
                if(at) {
                    size_t head=(size_t)(at-source);noclip=malloc(strlen(source)+8);
                    if(noclip){memcpy(noclip,source,head);strcpy(noclip+head,"}\n");source=noclip;
                        printf("MOMO_DIAG_NOCLIP_INSTALLED program=%u vertex=%u fragment=%u\n",p->name,p->sources[0],p->sources[1]);}
                }
            }
            {const char *fso=getenv("MOMO_DIAG_FS");
             /* MOMO_DIAG_FS=<fragment index>|1: force this fragment to solid red, to tell "never
                rasterizes" from "shader computes black". Diagnostic only. */
             if(i && fso && (!strcmp(fso,"1") || (unsigned)strtoul(fso,NULL,0)==p->sources[1])) {
                const char *last=strrchr(source,'}');
                if(last){size_t head=(size_t)(last-source);char *fr=malloc(strlen(source)+64);
                    if(fr){memcpy(fr,source,head);strcpy(fr+head,"frag_color0=vec4(1.0,0.0,0.0,1.0);\n}\n");source=fr;
                        printf("MOMO_DIAG_FS_INSTALLED program=%u vertex=%u fragment=%u\n",p->name,p->sources[0],p->sources[1]);}}}}
            char *red=NULL;
            const char *red_option=getenv("MOMO_DIAG_RED");
            /* MOMO_DIAG_RED=<fragment index> paints only that fragment source red, which localizes one
             * family of draws on the screen; 1 paints every fragment. */
            if(i && red_option && (!strcmp(red_option,"1") || (unsigned)strtoul(red_option,NULL,0)==p->sources[1])) {
                /* Diagnostic only: paint every fragment solid red, to tell "the triangle never rasterizes"
                 * apart from "the shader computes black" when a whole frame comes out empty. */
                static const char *tail="if(!pass)discard;";
                const char *at=strstr(source,tail);
                if(at) {
                    size_t head=(size_t)(at-source);red=malloc(strlen(source)+64);
                    if(red){memcpy(red,source,head);strcpy(red+head,"gl_FragColor=vec4(1.0,0.0,0.0,1.0);}\n");source=red;
                        printf("MOMO_DIAG_RED_INSTALLED program=%u vertex=%u fragment=%u\n",p->name,p->sources[0],p->sources[1]);}
                }
            }
            {const char *echo=getenv("NX_SHADER_ECHO");if(echo&&!strcmp(echo,"1")){printf("NVN_SHADER_ECHO stage=%u index=%u bytes=",i,p->sources[i]);for(unsigned k=0;k<32&&source[k];k++)printf("%02x",(unsigned char)source[k]);printf(" head=%.120s\n",source);}}
            glShaderSource(shaders[i],1,&source,NULL);glCompileShader(shaders[i]);GLint status=0;glGetShaderiv(shaders[i],GL_COMPILE_STATUS,&status);
            if(nvn_flag_on("NX_SCENE_AUDIT")&&p->sources[0]==2&&p->sources[1]==0)printf("NX_SCENE_COMPILE pair=2/0 stage=%u shader=%u status=%d\n",i,shaders[i],status);
            free(red);free(quad);free(noclip);free(t2page);free(compact_uv);
            free(premul);free(probe);free(sprite);free(flat);free(scaled);free(awf);free(mipped);free(r8);free(advent);free(lowered);free(wrapped);free(interface_source);
            if(!status){char log[2048]={0};glGetShaderInfoLog(shaders[i],sizeof(log),NULL,log);printf("NVN_SHADER_COMPILE_ERROR %s\n",log);stop("NVN_SHADER_COMPILE_FAILED",nvn_caller,p->sources[i],21);}
            glAttachShader(p->name,shaders[i]);
        }
        for(unsigned i=0;i<vertex->attribute_count;i++)glBindAttribLocation(p->name,vertex->attributes[i].location,vertex->attributes[i].name);
        glLinkProgram(p->name);GLint status=0;glGetProgramiv(p->name,GL_LINK_STATUS,&status);
        if(nvn_flag_on("NX_SCENE_AUDIT")&&p->sources[0]==2&&p->sources[1]==0)printf("NX_SCENE_LINK pair=2/0 program=%u status=%d\n",p->name,status);
        if(!status){char log[2048]={0};glGetProgramInfoLog(p->name,sizeof(log),NULL,log);printf("NVN_PROGRAM_LINK_ERROR %s\n",log);stop("NVN_PROGRAM_LINK_FAILED",nvn_caller,p->name,21);}
        for(unsigned i=0;i<vertex->attribute_count;i++) {
            GLint actual=glGetAttribLocation(p->name,vertex->attributes[i].name);
            if(actual>=0&&(unsigned)actual!=vertex->attributes[i].location)stop("NVN_GL_ATTRIBUTE_LOCATION",nvn_caller,actual,24);
            if(actual>=0)p->attribute_used[actual]=1;
        }
        nvn_megaman_uniforms(p,p->sources[0]);nvn_megaman_uniforms(p,p->sources[1]);
        unsigned has_size_query=0;
        for(unsigned i=0;i<p->uniform_count;i++){p->uniforms[i].location=glGetUniformLocation(p->name,p->uniforms[i].name);has_size_query|=p->uniforms[i].size_location>=0;}
        p->view_proj=p->view=p->mod_color=p->add_color=p->texture=-1;
        p->alpha_ref=glGetUniformLocation(p->name,"nvnAlphaRef");p->alpha_func=glGetUniformLocation(p->name,"nvnAlphaFunc");
        p->clip_control=glGetUniformLocation(p->name,"nvnClipControl");
        p->zero_scale_xy=glGetUniformLocation(p->name,"zero_render_scale_xy");
        p->aw_float_range_location=glGetUniformLocation(p->name,"aw_float_range");
        p->aw_light_tiles_location=glGetUniformLocation(p->name,"aw_light_tiles");
        p->aw_noise_location=glGetUniformLocation(p->name,"aw_noise");
        if(p->clip_control<0)stop("NVN_LC2_CLIP_CONTROL_LOCATION",nvn_caller,p->sources[0],24);
        p->wave_contract=fragment->wave_contract;
        p->momo_tmp_sprite=momo_tmp_sprite_enabled()?glGetUniformLocation(p->name,MOMO_TMP_SPRITE_UNIFORM):-1;
        p->wave_lookup=glGetUniformLocation(p->name,"mm_wave_lookup");
        p->wave_inverse_rows=glGetUniformLocation(p->name,"mm_wave_inverse_rows");
        p->wave_chroma=glGetUniformLocation(p->name,"mm_wave_chroma");
        p->wave_viewport=glGetUniformLocation(p->name,"mm_wave_viewport");
        p->wave_target_height=glGetUniformLocation(p->name,"mm_wave_target_height");
        if(p->wave_contract && (p->sources[1]!=16 || p->wave_lookup<0 || p->wave_chroma<0 || p->wave_viewport<0 || p->wave_target_height<0))
            stop("NVN_LC2_WAVE_SHADER_METADATA",nvn_caller,p->sources[1],24);
        p->color_write_mask=fragment->color_write_mask;
        /* Alternate alpha programs lack size-query locations in their legacy
         * interface; use the complete canonical shader for those draws. */
        if(!has_size_query && p->color_write_mask && !p->wave_contract && !p->zero_mediump && !p->zero_mip_wrapper && !p->zero_r8_wrapper && !zero_render_scale_enabled())nvn_alpha_build(p,shaders[0]);
        for(unsigned i=0;i<2;i++){glDetachShader(p->name,shaders[i]);glDeleteShader(shaders[i]);}
        p->linked=1;
        printf("NVN_LC2_PROGRAM_REAL original-offline-pair=%u/%u attributes=%u uniforms=%u program=%u code-control-source=byte-exact no-draw\n",
               p->sources[0],p->sources[1],vertex->attribute_count,p->uniform_count,p->name);

}
static struct nvn_program nvn_linked_programs[256];
static uint64_t nvn_program_record_id(uint64_t object) {
    return object?nvn_program_find(object)->id:0;
}
static void nvn_program_bind_stages(struct megaman_program_stage bound[2],uint64_t object,uint64_t mask,uint64_t recorded_id) {
    /* Unity 2021 clears the stages it does not use with a null program and mask 0x3c (geometry,
     * tessellation and compute; MEDIDO run momo-048). Those stages never exist on this host, so the
     * call only has to be accepted; a non-null program outside vertex/fragment still stops. */
    if(!object && !(mask&~UINT64_C(0x3c)))return;
    if(mask>3)stop("NVN_PROGRAM_STAGE_MASK_REQUIRED",nvn_caller,mask,20);
    struct nvn_program *p=object?nvn_program_find(object):NULL;
    /* A compute program has no graphics stage (nvn_shaders.h, MEDIDO run momo-096): binding it leaves
     * the vertex/fragment binding untouched. Only a real dispatch would be an error. */
    if(p && p->compute_only)return;
    if((p?p->id:0)!=recorded_id)stop("NVN_PROGRAM_RECORD_STALE",nvn_caller,object,24);
    unsigned empty[2]={0};
    if(!megaman_program_bind(bound,object,p?p->id:0,p?p->stage_mask:0,p?p->sources:empty,(unsigned)mask))
        stop("NVN_PROGRAM_BIND_STATE",nvn_caller,object,24);
}
static struct nvn_program *nvn_program_resolve_key2(const struct megaman_program_stage bound[2],unsigned aw_key,unsigned t2_key,unsigned t2_mode) {
    for(unsigned i=0;i<2;i++) {
        struct nvn_program *native=nvn_program_find(bound[i].object);
        if(!megaman_program_stage_valid(&bound[i],native->object,native->id,native->stage_mask,native->sources,i))
            stop("NVN_PROGRAM_BOUND_STAGE_STALE",nvn_caller,bound[i].object,24);
    }
    unsigned slot=256;
    for(unsigned i=0;i<256;i++) {
        struct nvn_program *p=&nvn_linked_programs[i];
        if(p->live && p->sources[0]==bound[0].source && p->sources[1]==bound[1].source && p->aw_float_key==aw_key && p->t2_page_key==t2_key && (!t2_key || p->t2_page_mode==t2_mode))return p; /* ROOT: variant cache keys the page mode */
        if(!p->live && slot==256)slot=i;
    }
    if(slot==256)stop("NVN_PROGRAM_PAIR_CAPACITY",nvn_caller,slot,21);
    struct nvn_program *p=&nvn_linked_programs[slot];
    GLuint name=glCreateProgram();if(!name)stop("NVN_GL_CREATE_PROGRAM",nvn_caller,0,21);
    *p=(struct nvn_program){.id=zero_next_object_id(),.name=name,.sources={bound[0].source,bound[1].source},.stage_mask=3,.aw_float_key=aw_key,.t2_page_key=t2_key,.t2_page_mode=t2_mode};
    nvn_program_link(p);p->live=1;
    if(aw_key)printf("AW_FLOAT_VARIANT program=%"PRIu64" pair=%u/%u key=0x%x\n",p->id,p->sources[0],p->sources[1],aw_key);
    if(t2_key)printf("T2_PAGE_VARIANT program=%"PRIu64" pair=%u/%u key=0x%x mode=%s gl-program=%u\n",p->id,p->sources[0],p->sources[1],t2_key,t2_mode==T2_PAGE_MODE_COMPACT?"compact":t2_mode==T2_PAGE_MODE_IDX8?"idx8":"idx16",p->name);
    return p;
}
static struct nvn_program *nvn_program_resolve_key(const struct megaman_program_stage bound[2],unsigned aw_key){return nvn_program_resolve_key2(bound,aw_key,0,0);}
static struct nvn_program *nvn_program_resolve(const struct megaman_program_stage bound[2]){return nvn_program_resolve_key(bound,0);}
#ifdef ANIMAL_WELL_SHADERS
/* AW_LINK_ALL=1: link every translated fragment with the single vertex shader at the first draw, so translation
 * or compile failures surface at boot and rooms that first use a pass do not stall on the Mali compiler. */
static void aw_link_all(void) {
    static int done;if(done)return;done=1;
    const char *o=getenv("AW_LINK_ALL");if(!o||strcmp(o,"1"))return;
    unsigned vertex=0,count=0,linked=0;
    for(unsigned i=0;i<sizeof(nvn_shader_sources)/sizeof(nvn_shader_sources[0]);i++)if(nvn_shader_sources[i].gles && nvn_shader_sources[i].stage==0)vertex=i;
    for(unsigned i=0;i<sizeof(nvn_shader_sources)/sizeof(nvn_shader_sources[0]);i++) {
        if(!nvn_shader_sources[i].gles || nvn_shader_sources[i].stage!=1 || nvn_megaman_shader_bindings[i].error)continue;
        count++;unsigned exists=0;
        for(unsigned k=0;k<256;k++)if(nvn_linked_programs[k].live && nvn_linked_programs[k].sources[0]==vertex && nvn_linked_programs[k].sources[1]==i && !nvn_linked_programs[k].aw_float_key)exists=1;
        if(exists)continue;
        unsigned slot=256;for(unsigned k=0;k<256;k++)if(!nvn_linked_programs[k].live){slot=k;break;}
        if(slot==256)stop("NVN_PROGRAM_PAIR_CAPACITY",nvn_caller,slot,21);
        struct nvn_program *p=&nvn_linked_programs[slot];GLuint name=glCreateProgram();if(!name)stop("NVN_GL_CREATE_PROGRAM",nvn_caller,0,21);
        *p=(struct nvn_program){.id=zero_next_object_id(),.name=name,.sources={vertex,i},.stage_mask=3};
        nvn_program_link(p);p->live=1;linked++;
    }
    printf("AW_LINK_ALL vertex=%u fragments=%u linked-now=%u\n",vertex,count,linked);
}
#endif
static unsigned nvn_shader_dispatch_locked(unsigned id,struct nvn_frame *f,uint64_t *result) {
    uint64_t *a=f->args;*result=0;
    if(id==NI_nvnProgramFinalize || id==NI_nvnProgramSetDebugLabel) {
        struct nvn_program *p=nvn_program_find(a[0]);
        if(id==NI_nvnProgramSetDebugLabel)nvn_debug_label(a[1]);
        if(id==NI_nvnProgramFinalize){nvn_alpha_delete(p);p->live=0;}
        return 1;
    }
    if(id==NI_nvnProgramInitialize) {
        nvn_pointer(a[0],8,PROT_READ|PROT_WRITE);
        if(!nvn_device.live||a[1]!=nvn_device.address)stop("NVN_PROGRAM_DEVICE",nvn_caller,a[1],24);
        unsigned slot=256;
        for(unsigned i=0;i<256;i++) {
            if(nvn_programs[i].live&&nvn_programs[i].object==a[0])stop("NVN_PROGRAM_ALREADY_LIVE",nvn_caller,a[0],24);
            if(!nvn_programs[i].live&&slot==256)slot=i;
        }
        if(slot==256)stop("NVN_PROGRAM_CAPACITY",nvn_caller,slot,21);
        nvn_programs[slot]=(struct nvn_program){.object=a[0],.id=zero_next_object_id(),.live=1};*result=1;return 1;
    }
    if(id==NI_nvnProgramSetShaders || id==NI_nvnProgramSetShadersExt) {
        struct nvn_program *p=nvn_program_find(a[0]);
        if(!a[1] || a[1]>2 || p->stage_mask)stop("NVN_PROGRAM_STAGE_CONTRACT",nvn_caller,a[1],24);
        /* SetShadersExt (Animal Well): 24-byte elements {data, control, extra}; the extra pointer is not consulted. */
        unsigned stride=id==NI_nvnProgramSetShadersExt?24:16;
        uint64_t inputs[4]={0};const uint8_t *raw=nvn_pointer(a[2],a[1]*stride,PROT_READ);
        for(unsigned i=0;i<a[1];i++)memcpy(inputs+i*2,raw+i*stride,16);
        unsigned matched=0,sources[2]={0};
#ifdef MOMO_SHADERS
        for(unsigned i=0;i<a[1];i++) {
            const uint8_t *head=nvn_pointer(inputs[i*2+1],0x880,PROT_READ);
#ifdef T2_SHADERS
            unsigned size=read32(head+0x704);
            if(read32(head)!=0x98761234 || read32(head+0x20)!=0x880 || size<0x80 || size>131072)
                stop("T2_SHADER_CONTROL_LAYOUT",nvn_caller,inputs[i*2+1],24);
            enum t2_shader_class cls=t2_shader_classify(nvn_gpu_pointer(inputs[i*2],size,PROT_READ),size);
            if(cls==T2_SHADER_UNKNOWN)stop("T2_SHADER_CLASS_UNKNOWN",nvn_caller,inputs[i*2],20);
            if(cls==T2_SHADER_COMPUTE && a[1]!=1)stop("T2_SHADER_MIXED_COMPUTE",nvn_caller,a[1],24);
            if(cls==T2_SHADER_GRAPHICS && read32(head+0x70c)==0xc)
                printf("T2_SHADER_GRAPHICS_SPH object=0x%"PRIx64" control-70c=0xc classification=graphics\n",p->object);
            if(cls==T2_SHADER_COMPUTE) {
#else
            if(read32(head)==0x98761234 && read32(head+0x70c)==0xc) {
#endif
                p->stage_mask=0;p->sources[0]=p->sources[1]=0;p->compute_only=1;
                printf("MOMO_COMPUTE_PROGRAM object=0x%"PRIx64" id=%"PRIu64" stages=%"PRIu64" recorded=no-gles2-compute\n",
                       p->object,p->id,a[1]);
                *result=1;return 1;
            }
        }
#endif
        for(unsigned i=0;i<a[1];i++) {
            unsigned source=nvn_megaman_shader_match(inputs[i*2],inputs[i*2+1]);
            if(source==UINT32_MAX) {
                /* Capture run: the pair is already dumped; carry on with no GL stages so the
                 * remaining programs get dumped too. Drawing such a program is not supported. */
                p->stage_mask=0;p->sources[0]=p->sources[1]=0;p->compute_only=1;
                printf("ZERO_PROGRAM_UNKNOWN_CAPTURED object=0x%"PRIx64" id=%"PRIu64"\n",p->object,p->id);
                *result=1;return 1;
            }
            unsigned stage=nvn_shader_sources[source].stage;
            if(stage>1 || matched&(1U<<stage))stop("NVN_LC2_SHADER_STAGE_DUPLICATE",nvn_caller,source,24);
            matched|=1U<<stage;sources[stage]=source;
        }
        memcpy(p->sources,sources,sizeof(sources));p->stage_mask=matched;
        printf("NVN_LC2_PROGRAM_STAGES object=0x%"PRIx64" id=%"PRIu64" mask=%u sources=%u/%u code-control-source=byte-exact link=on-native-pair-draw\n",
               p->object,p->id,p->stage_mask,p->sources[0],p->sources[1]);
        *result=1;return 1;
    }
    return 0;
}

/* Zero creates its programs concurrently on loading workers. Table mutation
 * is serialized; GL linking still occurs only on the ordered replay thread. */
static pthread_mutex_t zero_shader_mutex=PTHREAD_MUTEX_INITIALIZER;
static unsigned nvn_shader_dispatch(unsigned id,struct nvn_frame *f,uint64_t *result) {
    if(id!=NI_nvnProgramInitialize && id!=NI_nvnProgramSetShaders && id!=NI_nvnProgramSetShadersExt &&
       id!=NI_nvnProgramFinalize && id!=NI_nvnProgramSetDebugLabel)return 0;
    pthread_mutex_lock(&zero_shader_mutex);
    unsigned handled=nvn_shader_dispatch_locked(id,f,result);
    pthread_mutex_unlock(&zero_shader_mutex);return handled;
}
