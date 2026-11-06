/* Terminator 2D: EMPTY shader catalogue stub (host builds and boots without translated shaders).
 *
 * Same contract the Momodora host consumes from COTM_SHADERS_DIR/nvn_shader_sources.h, generated there by
 * momodorafarewell-switch-study/tools/prepare_momo_shaders.py @ 08bfe36; the full contract is written
 * down in docs/HOST.md section 5 for Frente B. This stub has ONE unusable entry (code_size 0 never matches
 * a real program, whose control section declares >= 0x80 code bytes), so every ProgramSetShaders resolves
 * to ZERO_SHADER_UNRECOGNIZED / ZERO_SHADER_NOT_PINNED and the run stops at the first real program: the
 * floor run (draws elided) and the PC boot get that far; the image needs Frente B's catalogue. */
#include <stddef.h>
#include <stdint.h>
#define ZERO_ZX_SHADER_PACKAGES 1
#define ZERO_SHADER_VARYING_SLOTS 8
#ifndef MOMO_SHADERS
#define MOMO_SHADERS 1
#endif
#define T2_SHADER_CATALOGUE_STUB 1
static const struct {const char *original,*gles;unsigned stage;} nvn_shader_sources[]={
{"",NULL,1},
};
struct mm_shader_uniform {const char *name;unsigned stage,binding,offset,size,components,word_boolean,word_int,word_array;};
struct mm_shader_sampler {const char *name;unsigned stage,binding,offset,size_query,texel_fetch_bits,cpu_view;};
struct mm_shader_attribute {const char *name;unsigned location,kind;};
struct mm_shader_binding {
 const char *sha256,*error;unsigned control_size,code_size;
 unsigned uniform_count,sampler_count,attribute_count,color_write_mask,requires_fragment_highp,requires_fragment_mediump,wave_contract;
 unsigned varying_read_masks[8],varying_write_masks[8];
 uint8_t code_hash[32],control_hash[32];
 struct mm_shader_uniform uniforms[64];struct mm_shader_sampler samplers[8];struct mm_shader_attribute attributes[16];
};
static const struct mm_shader_binding nvn_megaman_shader_bindings[]={
{.sha256="0000000000000000000000000000000000000000000000000000000000000000",.error="stub catalogue: no translated shaders",.code_size=0,.control_size=0,.code_hash={0},.control_hash={0},.color_write_mask=15,.requires_fragment_mediump=1,.uniform_count=0,.sampler_count=0,.attribute_count=0,.varying_read_masks={0},.varying_write_masks={0}},
};
