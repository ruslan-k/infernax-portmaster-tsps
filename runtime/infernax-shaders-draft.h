/* GENERATED DRAFT catalogue - our own translations, not game data.
 * Source: tools/build_catalogue.py over a workspace of translated programs.
 * Every entry is a translation of the title's program; entries with an error string are
 * NOT translated and the runtime stops on them by design. */
#include <stddef.h>
#include <stdint.h>
#define ZERO_ZX_SHADER_PACKAGES 15
#define ZERO_SHADER_VARYING_SLOTS 8
#define INFERNAX_SEPARATE_SAMPLERS 1
#ifndef MOMO_SHADERS
#define MOMO_SHADERS 1
#endif
static const struct {const char *original,*gles;unsigned stage;} nvn_shader_sources[]={
	/* 0 cap11-0 fragment */
	{"", ""
	 "#version 310 es\n"
	 "precision highp int;\n"
	 "precision mediump float;\n"
	 "\n"
	 "uniform highp sampler2D fs_c3;\n"
	 "\n"
	 "uniform sampler2D fs_tex2_168;\n"
	 "\n"
	 "in vec4 varying_0;\n"
	 "layout(location = 0) out vec4 frag_color0;\n"
	 "\n"
	 "void main()\n"
	 "{\n"
	 "    float _59 = 1.0 / gl_FragCoord.w;\n"
	 "    vec4 _64 = texture(fs_tex2_168, vec2(_59 * (varying_0.x * gl_FragCoord.w), _59 * (varying_0.y * gl_FragCoord.w)));\n"
	 "    frag_color0.x = _64.x * texelFetch(fs_c3, ivec2((int(4u) >> 2) & 1023, int(4u) >> 12), 0)[int(4u) & 3];\n"
	 "    frag_color0.y = _64.y * texelFetch(fs_c3, ivec2((int(5u) >> 2) & 1023, int(5u) >> 12), 0)[int(5u) & 3];\n"
	 "    frag_color0.z = _64.z * texelFetch(fs_c3, ivec2((int(6u) >> 2) & 1023, int(6u) >> 12), 0)[int(6u) & 3];\n"
	 "    frag_color0.w = _64.w * texelFetch(fs_c3, ivec2((int(7u) >> 2) & 1023, int(7u) >> 12), 0)[int(7u) & 3];\n"
	 "}\n"
	 "", 1},
	/* 1 cap9-0 fragment */
	{"", ""
	 "#version 310 es\n"
	 "precision highp int;\n"
	 "precision mediump float;\n"
	 "#define unpackFloat2x16(a) unpackHalf2x16(a)\n"
	 "#define packFloat2x16(a) packHalf2x16(a)\n"
	 "#define fma(a, b, c) ((a) * (b) + (c))\n"
	 "#if defined(GL_AMD_gpu_shader_half_float)\n"
	 "#elif defined(GL_EXT_shader_explicit_arithmetic_types_float16)\n"
	 "#else\n"
	 "\n"
	 "#endif\n"
	 "\n"
	 "\n"
	 "\n"
	 "uniform highp sampler2D fs_c3_1;\n"
	 "\n"
	 "uniform sampler2D fs_tex2_168;\n"
	 "uniform sampler2D fs_tex2_170;\n"
	 "uniform sampler2D fs_tex2_178;\n"
	 "uniform sampler2D fs_tex2_180;\n"
	 "uniform sampler2D fs_tex2_188;\n"
	 "uniform sampler2D fs_tex2_190;\n"
	 "uniform sampler2D fs_tex2_198;\n"
	 "uniform sampler2D fs_tex2_1a0;\n"
	 "\n"
	 "in vec4 varying_0;\n"
	 "layout(location = 0) out vec4 frag_color0;\n"
	 "\n"
	 "void main()\n"
	 "{\n"
	 "    float _78 = float((-float(unpackFloat2x16(0u).x)) + uintBitsToFloat(0x3d44a000u));\n"
	 "    float _87 = float((-float(unpackFloat2x16(0u).x)) + uintBitsToFloat(0x3ce1c000u));\n"
	 "    float _91 = varying_0.x + (-texelFetch(fs_c3_1, ivec2((int(1u) >> 2) & 1023, int(1u) >> 12), 0)[int(1u) & 3]);\n"
	 "    float _99 = _91 * texelFetch(fs_c3_1, ivec2((int(0u) >> 2) & 1023, int(0u) >> 12), 0)[int(0u) & 3];\n"
	 "    float _103 = (varying_0.y + (-texelFetch(fs_c3_1, ivec2((int(2u) >> 2) & 1023, int(2u) >> 12), 0)[int(2u) & 3])) * texelFetch(fs_c3_1, ivec2((int(3u) >> 2) & 1023, int(3u) >> 12), 0)[int(3u) & 3];\n"
	 "    float _107 = _99 + (-floor(_99));\n"
	 "    float _110 = _107 * texelFetch(fs_c3_1, ivec2((int(3u) >> 2) & 1023, int(3u) >> 12), 0)[int(3u) & 3];\n"
	 "    float _117 = fma(_107, -(1.0 / texelFetch(fs_c3_1, ivec2((int(0u) >> 2) & 1023, int(0u) >> 12), 0)[int(0u) & 3]), _91) * texelFetch(fs_c3_1, ivec2((int(3u) >> 2) & 1023, int(3u) >> 12), 0)[int(3u) & 3];\n"
	 "    float _144 = fma(_117, uintBitsToFloat(0x3c123fb3u), fma(_103, uintBitsToFloat(0x3f0cc8efu), _110 * 0.390404999256134033203125)) * texelFetch(fs_c3_1, ivec2((int(4u) >> 2) & 1023, int(4u) >> 12), 0)[int(4u) & 3];\n"
	 "    float _147 = fma(_117, uintBitsToFloat(0x3ab1f688u), fma(_103, uintBitsToFloat(0x3f769271u), _110 * 0.070841602981090545654296875)) * texelFetch(fs_c3_1, ivec2((int(5u) >> 2) & 1023, int(5u) >> 12), 0)[int(5u) & 3];\n"
	 "    float _151 = fma(_117, uintBitsToFloat(0x3f6fadc1u), fma(_103, uintBitsToFloat(0x3e0317f0u), _110 * 0.02310819923877716064453125)) * texelFetch(fs_c3_1, ivec2((int(6u) >> 2) & 1023, int(6u) >> 12), 0)[int(6u) & 3];\n"
	 "    float _303 = exp2(log2(max(0.0, float(float(float(float(exp2(fma(float(float(fma(log2(max(0.0, float(float(fma(fma(_151, uintBitsToFloat(0xbccbe836u), fma(_147, uintBitsToFloat(0xbfd07c31u), _144 * 2.85846996307373046875)), uintBitsToFloat(0x40b1c000u), float(_78)))))), uintBitsToFloat(0x3d968000u), -float(_87)))), texelFetch(fs_c3_1, ivec2((int(26u) >> 2) & 1023, int(26u) >> 12), 0)[int(26u) & 3], 0.0275523960590362548828125) * uintBitsToFloat(0x4159c000u)) + uintBitsToFloat(0xbd44a000u))) * uintBitsToFloat(0x3e386000u))) * texelFetch(fs_c3_1, ivec2((int(8u) >> 2) & 1023, int(8u) >> 12), 0)[int(8u) & 3])) * 0.4545454680919647216796875);\n"
	 "    float _305 = exp2(log2(max(0.0, float(float(float(float(exp2(fma(float(float(fma(log2(max(0.0, float(float(fma(fma(_151, uintBitsToFloat(0x39aa0442u), fma(_147, uintBitsToFloat(0x3f943fe6u), _144 * (-0.21018199622631072998046875))), uintBitsToFloat(0x40b1c000u), float(_78)))))), uintBitsToFloat(0x3d968000u), -float(_87)))), texelFetch(fs_c3_1, ivec2((int(26u) >> 2) & 1023, int(26u) >> 12), 0)[int(26u) & 3], 0.0275523960590362548828125) * uintBitsToFloat(0x4159c000u)) + uintBitsToFloat(0xbd44a000u))) * uintBitsToFloat(0x3e386000u))) * texelFetch(fs_c3_1, ivec2((int(9u) >> 2) & 1023, int(9u) >> 12), 0)[int(9u) & 3])) * 0.4545454680919647216796875);\n"
	 "    float _306 = sqrt(_303);\n"
	 "    float _308 = exp2(log2(max(0.0, float(float(float(float(exp2(fma(float(float(fma(log2(max(0.0, float(float(fma(fma(_151, uintBitsToFloat(0x3f88ca2eu), fma(_147, uintBitsToFloat(0xbdf20297u), _144 * (-0.0418119989335536956787109375))), uintBitsToFloat(0x40b1c000u), float(_78)))))), uintBitsToFloat(0x3d968000u), -float(_87)))), texelFetch(fs_c3_1, ivec2((int(26u) >> 2) & 1023, int(26u) >> 12), 0)[int(26u) & 3], 0.0275523960590362548828125) * uintBitsToFloat(0x4159c000u)) + uintBitsToFloat(0xbd44a000u))) * uintBitsToFloat(0x3e386000u))) * texelFetch(fs_c3_1, ivec2((int(10u) >> 2) & 1023, int(10u) >> 12), 0)[int(10u) & 3])) * 0.4545454680919647216796875);\n"
	 "    float _310 = _303 * 2.0;\n"
	 "    float _325 = (-unpackFloat2x16(0u).x) + unpackFloat2x16(939538432u).x;\n"
	 "    float _337 = float(float((-float(_325)) + texelFetch(fs_c3_1, ivec2((int(56u) >> 2) & 1023, int(56u) >> 12), 0)[int(56u) & 3]));\n"
	 "    float _342 = clamp(fma(min(_308, 1.0), uintBitsToFloat(0x3d93d07du), fma(min(_305, 1.0), uintBitsToFloat(0x3f371437u), min(_303, 1.0) * 0.21267290413379669189453125)) + texelFetch(fs_c3_1, ivec2((int(59u) >> 2) & 1023, int(59u) >> 12), 0)[int(59u) & 3], 0.0, 1.0);\n"
	 "    float _345 = _303 * _303;\n"
	 "    float _347 = fma(_337, -_342, _337) + 0.5;\n"
	 "    float _362 = float(float((-float(_325)) + texelFetch(fs_c3_1, ivec2((int(57u) >> 2) & 1023, int(57u) >> 12), 0)[int(57u) & 3]));\n"
	 "    float _374 = float(float((-float(_325)) + texelFetch(fs_c3_1, ivec2((int(58u) >> 2) & 1023, int(58u) >> 12), 0)[int(58u) & 3]));\n"
	 "    float _376 = sqrt(_305);\n"
	 "    float _382 = _305 * _305;\n"
	 "    float _385 = fma(_362, -_342, _362) + 0.5;\n"
	 "    float _393 = (-unpackFloat2x16(0u).x) + unpackFloat2x16(939538432u).x;\n"
	 "    float _395 = fma(fma(_347, -_310, _310) + fma(_347 * _306, 2.0, -_306), uintBitsToFloat((_347 >= 0.5) ? 1065353216u : 0u), fma(_347, _310, fma(_347 * (-_345), 2.0, _345)) * uintBitsToFloat((!(_347 >= 0.5)) ? 1065353216u : 0u));\n"
	 "    float _396 = sqrt(_308);\n"
	 "    float _399 = _308 * _308;\n"
	 "    float _400 = fma(_374, -_342, _374) + 0.5;\n"
	 "    float _407 = float(float((-float(_393)) + texelFetch(fs_c3_1, ivec2((int(57u) >> 2) & 1023, int(57u) >> 12), 0)[int(57u) & 3]));\n"
	 "    float _410 = _305 * 2.0;\n"
	 "    float _417 = _308 * 2.0;\n"
	 "    float _428 = sqrt(_395);\n"
	 "    float _443 = _395 * _395;\n"
	 "    float _450 = fma(_342, float(float((-float(_393)) + texelFetch(fs_c3_1, ivec2((int(60u) >> 2) & 1023, int(60u) >> 12), 0)[int(60u) & 3])), uintBitsToFloat(0x3f000000u));\n"
	 "    float _460 = fma(fma(_385, -_410, _410) + fma(_385 * _376, 2.0, -_376), uintBitsToFloat((_385 >= 0.5) ? 1065353216u : 0u), fma(_385, _410, fma(_385 * (-_382), 2.0, _382)) * uintBitsToFloat((!((fma(_407, -_342, _407) + 0.5) >= 0.5)) ? 1065353216u : 0u));\n"
	 "    float _470 = _395 * 2.0;\n"
	 "    float _480 = sqrt(_460);\n"
	 "    float _482 = fma(fma(_400, -_417, _417) + fma(_400 * _396, 2.0, -_396), uintBitsToFloat((_400 >= 0.5) ? 1065353216u : 0u), fma(_400, _417, fma(_400 * (-_399), 2.0, _399)) * uintBitsToFloat((!(_400 >= 0.5)) ? 1065353216u : 0u));\n"
	 "    float _485 = fma(_342, float(float((-float(_393)) + texelFetch(fs_c3_1, ivec2((int(61u) >> 2) & 1023, int(61u) >> 12), 0)[int(61u) & 3])), uintBitsToFloat(0x3f000000u));\n"
	 "    float _490 = _460 * _460;\n"
	 "    float _493 = fma(_342, float(float((-float(_393)) + texelFetch(fs_c3_1, ivec2((int(62u) >> 2) & 1023, int(62u) >> 12), 0)[int(62u) & 3])), uintBitsToFloat(0x3f000000u));\n"
	 "    float _495 = _482 * _482;\n"
	 "    float _496 = _460 * 2.0;\n"
	 "    float _498 = sqrt(_482);\n"
	 "    float _514 = _482 * 2.0;\n"
	 "    float _553 = exp2(log2(abs(fma(fma(_450, -_470, _470) + fma(_450 * _428, 2.0, -_428), uintBitsToFloat((_450 >= 0.5) ? 1065353216u : 0u), fma(_450, _470, fma(_450 * (-_443), 2.0, _443)) * uintBitsToFloat((!(_450 >= 0.5)) ? 1065353216u : 0u)))) * 2.2000000476837158203125);\n"
	 "    float _555 = exp2(log2(abs(fma(fma(_485, -_496, _496) + fma(_485 * _480, 2.0, -_480), uintBitsToFloat((_485 >= 0.5) ? 1065353216u : 0u), fma(_485, _496, fma(_485 * (-_490), 2.0, _490)) * uintBitsToFloat((!(_485 >= 0.5)) ? 1065353216u : 0u)))) * 2.2000000476837158203125);\n"
	 "    float _556 = exp2(log2(abs(fma(fma(_493, -_514, _514) + fma(_493 * _498, 2.0, -_498), uintBitsToFloat((_493 >= 0.5) ? 1065353216u : 0u), fma(_493, _514, fma(_493 * (-_495), 2.0, _495)) * uintBitsToFloat((!(_493 >= 0.5)) ? 1065353216u : 0u)))) * 2.2000000476837158203125);\n"
	 "    float _577 = fma(_556, texelFetch(fs_c3_1, ivec2((int(14u) >> 2) & 1023, int(14u) >> 12), 0)[int(14u) & 3], fma(_555, texelFetch(fs_c3_1, ivec2((int(13u) >> 2) & 1023, int(13u) >> 12), 0)[int(13u) & 3], _553 * texelFetch(fs_c3_1, ivec2((int(12u) >> 2) & 1023, int(12u) >> 12), 0)[int(12u) & 3]));\n"
	 "    float _580 = fma(_556, texelFetch(fs_c3_1, ivec2((int(18u) >> 2) & 1023, int(18u) >> 12), 0)[int(18u) & 3], fma(_555, texelFetch(fs_c3_1, ivec2((int(17u) >> 2) & 1023, int(17u) >> 12), 0)[int(17u) & 3], _553 * texelFetch(fs_c3_1, ivec2((int(16u) >> 2) & 1023, int(16u) >> 12), 0)[int(16u) & 3]));\n"
	 "    float _584 = fma(_556, texelFetch(fs_c3_1, ivec2((int(22u) >> 2) & 1023, int(22u) >> 12), 0)[int(22u) & 3], fma(_555, texelFetch(fs_c3_1, ivec2((int(21u) >> 2) & 1023, int(21u) >> 12), 0)[int(21u) & 3], _553 * texelFetch(fs_c3_1, ivec2((int(20u) >> 2) & 1023, int(20u) >> 12), 0)[int(20u) & 3]));\n"
	 "    float _601 = _584 * texelFetch(fs_c3_1, ivec2((int(42u) >> 2) & 1023, int(42u) >> 12), 0)[int(42u) & 3];\n"
	 "    float _608 = fma(_584, uintBitsToFloat(0x3d93d07du), fma(_580, uintBitsToFloat(0x3f371437u), _577 * 0.21267290413379669189453125));\n"
	 "    float _627 = clamp((_608 + (-texelFetch(fs_c3_1, ivec2((int(52u) >> 2) & 1023, int(52u) >> 12), 0)[int(52u) & 3])) * (1.0 / ((-texelFetch(fs_c3_1, ivec2((int(52u) >> 2) & 1023, int(52u) >> 12), 0)[int(52u) & 3]) + texelFetch(fs_c3_1, ivec2((int(53u) >> 2) & 1023, int(53u) >> 12), 0)[int(53u) & 3])), 0.0, 1.0);\n"
	 "    float _629 = clamp((_608 + (-texelFetch(fs_c3_1, ivec2((int(54u) >> 2) & 1023, int(54u) >> 12), 0)[int(54u) & 3])) * (1.0 / ((-texelFetch(fs_c3_1, ivec2((int(54u) >> 2) & 1023, int(54u) >> 12), 0)[int(54u) & 3]) + texelFetch(fs_c3_1, ivec2((int(55u) >> 2) & 1023, int(55u) >> 12), 0)[int(55u) & 3])), 0.0, 1.0);\n"
	 "    float _638 = _580 * texelFetch(fs_c3_1, ivec2((int(41u) >> 2) & 1023, int(41u) >> 12), 0)[int(41u) & 3];\n"
	 "    float _639 = fma(_629, -2.0, 3.0);\n"
	 "    float _641 = fma(_627, -2.0, 3.0) * (-(_627 * _627));\n"
	 "    float _642 = _629 * _629;\n"
	 "    float _646 = _577 * texelFetch(fs_c3_1, ivec2((int(40u) >> 2) & 1023, int(40u) >> 12), 0)[int(40u) & 3];\n"
	 "    float _650 = fma(_639, -_642, -_641);\n"
	 "    float _656 = _639 * _642;\n"
	 "    float _682 = fma(_656, _580 * texelFetch(fs_c3_1, ivec2((int(49u) >> 2) & 1023, int(49u) >> 12), 0)[int(49u) & 3], fma(_650, _580 * texelFetch(fs_c3_1, ivec2((int(45u) >> 2) & 1023, int(45u) >> 12), 0)[int(45u) & 3], fma(_641, _638, _638))) * texelFetch(fs_c3_1, ivec2((int(37u) >> 2) & 1023, int(37u) >> 12), 0)[int(37u) & 3];\n"
	 "    float _686 = fma(_656, _584 * texelFetch(fs_c3_1, ivec2((int(50u) >> 2) & 1023, int(50u) >> 12), 0)[int(50u) & 3], fma(_650, _584 * texelFetch(fs_c3_1, ivec2((int(46u) >> 2) & 1023, int(46u) >> 12), 0)[int(46u) & 3], fma(_641, _601, _601))) * texelFetch(fs_c3_1, ivec2((int(38u) >> 2) & 1023, int(38u) >> 12), 0)[int(38u) & 3];\n"
	 "    float _734 = fma(_656, _577 * texelFetch(fs_c3_1, ivec2((int(48u) >> 2) & 1023, int(48u) >> 12), 0)[int(48u) & 3], fma(_650, _577 * texelFetch(fs_c3_1, ivec2((int(44u) >> 2) & 1023, int(44u) >> 12), 0)[int(44u) & 3], fma(_641, _646, _646))) * texelFetch(fs_c3_1, ivec2((int(36u) >> 2) & 1023, int(36u) >> 12), 0)[int(36u) & 3];\n"
	 "    float _764 = float(int(bitfieldExtract(uint(abs(int(uint(bitfieldExtract(int((_682 > (-texelFetch(fs_c3_1, ivec2((int(29u) >> 2) & 1023, int(29u) >> 12), 0)[int(29u) & 3])) ? 4294967295u : 0u), int(0u), int(32u)))))), int(0u), int(32u)) + (-bitfieldExtract(uint(abs(int(uint(bitfieldExtract(int((_682 < (-texelFetch(fs_c3_1, ivec2((int(29u) >> 2) & 1023, int(29u) >> 12), 0)[int(29u) & 3])) ? 4294967295u : 0u), int(0u), int(32u)))))), int(0u), int(32u)))));\n"
	 "    float _773 = float(int(bitfieldExtract(uint(abs(int(uint(bitfieldExtract(int((_686 > (-texelFetch(fs_c3_1, ivec2((int(30u) >> 2) & 1023, int(30u) >> 12), 0)[int(30u) & 3])) ? 4294967295u : 0u), int(0u), int(32u)))))), int(0u), int(32u)) + (-bitfieldExtract(uint(abs(int(uint(bitfieldExtract(int((_686 < (-texelFetch(fs_c3_1, ivec2((int(30u) >> 2) & 1023, int(30u) >> 12), 0)[int(30u) & 3])) ? 4294967295u : 0u), int(0u), int(32u)))))), int(0u), int(32u)))));\n"
	 "    float _777 = exp2(log2(abs(_686 + texelFetch(fs_c3_1, ivec2((int(30u) >> 2) & 1023, int(30u) >> 12), 0)[int(30u) & 3])) * texelFetch(fs_c3_1, ivec2((int(34u) >> 2) & 1023, int(34u) >> 12), 0)[int(34u) & 3]);\n"
	 "    float _780 = exp2(log2(abs(_682 + texelFetch(fs_c3_1, ivec2((int(29u) >> 2) & 1023, int(29u) >> 12), 0)[int(29u) & 3])) * texelFetch(fs_c3_1, ivec2((int(33u) >> 2) & 1023, int(33u) >> 12), 0)[int(33u) & 3]);\n"
	 "    float _781 = float(int(bitfieldExtract(uint(abs(int(uint(bitfieldExtract(int((_734 > (-texelFetch(fs_c3_1, ivec2((int(28u) >> 2) & 1023, int(28u) >> 12), 0)[int(28u) & 3])) ? 4294967295u : 0u), int(0u), int(32u)))))), int(0u), int(32u)) + (-bitfieldExtract(uint(abs(int(uint(bitfieldExtract(int((_734 < (-texelFetch(fs_c3_1, ivec2((int(28u) >> 2) & 1023, int(28u) >> 12), 0)[int(28u) & 3])) ? 4294967295u : 0u), int(0u), int(32u)))))), int(0u), int(32u)))));\n"
	 "    float _782 = _773 * _777;\n"
	 "    float _783 = exp2(log2(abs(_734 + texelFetch(fs_c3_1, ivec2((int(28u) >> 2) & 1023, int(28u) >> 12), 0)[int(28u) & 3])) * texelFetch(fs_c3_1, ivec2((int(32u) >> 2) & 1023, int(32u) >> 12), 0)[int(32u) & 3]);\n"
	 "    float _784 = _764 * _780;\n"
	 "    uint _788 = (_784 >= _782) ? 1065353216u : 0u;\n"
	 "    float _793 = _781 * _783;\n"
	 "    float _794 = fma(_773, _777, fma(_764, _780, -_782) * uintBitsToFloat(_788));\n"
	 "    uint _798 = (_793 >= _794) ? 1065353216u : 0u;\n"
	 "    float _803 = fma(_764, _780, fma(_773, _777, -_784) * uintBitsToFloat(_788));\n"
	 "    float _806 = fma(_794 + (-_793), uintBitsToFloat(_798), _793);\n"
	 "    float _809 = float(fma(uintBitsToFloat(_798), fma(_781, _783, -_794), _794));\n"
	 "    float _822 = float((-uintBitsToFloat(_788)) + uintBitsToFloat(0x3f2aa000u));\n"
	 "    float _847 = float(float(_809) + (-min(_803, _806)));\n"
	 "    float _869 = float(float(float(_847) * (1.0 / float(float(float(_809) + uintBitsToFloat(0x38d1c000u))))));\n"
	 "    float _874 = float(float(fma(float(float((-_803) + _806)), 1.0 / float(fma(_847, unpackFloat2x16(1174423040u).x, float((-float(unpackFloat2x16(0u).x)) + uintBitsToFloat(0x38d1c000u)))), float(float(fma(uintBitsToFloat(_798), float(float(uintBitsToFloat(_788) + float(unpackFloat2x16(3154164736u).y)) + (-_822)), float(_822)))))));\n"
	 "    float _879 = abs(_874) + texelFetch(fs_c3_1, ivec2((int(24u) >> 2) & 1023, int(24u) >> 12), 0)[int(24u) & 3];\n"
	 "    vec4 _885 = texture(fs_tex2_188, vec2(_879, 0.0));\n"
	 "    vec4 _889 = texture(fs_tex2_198, vec2(_869, 0.0));\n"
	 "    vec4 _893 = texture(fs_tex2_190, vec2(abs(_874) + (-0.0), 0.0));\n"
	 "    vec4 _897 = texture(fs_tex2_1a0, vec2(fma(_782, uintBitsToFloat(0x3d93d07du), fma(_784, uintBitsToFloat(0x3f371437u), _793 * 0.21267290413379669189453125)), 0.0));\n"
	 "    float _905 = (-unpackFloat2x16(0u).x) + unpackFloat2x16(1107313152u).y;\n"
	 "    float _918 = float(float(clamp(_885.x + (-0.0), 0.0, 1.0) + float(unpackFloat2x16(3087054848u).x)));\n"
	 "    float _920 = _879 + _918;\n"
	 "    bool _924 = _920 > 1.0;\n"
	 "    uint _930;\n"
	 "    if (_924)\n"
	 "    {\n"
	 "        _930 = floatBitsToUint(_920 + (-1.0));\n"
	 "    }\n"
	 "    else\n"
	 "    {\n"
	 "        _930 = floatBitsToUint(_918);\n"
	 "    }\n"
	 "    uint _932;\n"
	 "    if (!_924)\n"
	 "    {\n"
	 "        _932 = floatBitsToUint(_920);\n"
	 "    }\n"
	 "    else\n"
	 "    {\n"
	 "        _932 = _930;\n"
	 "    }\n"
	 "    uint _934;\n"
	 "    if (!(_879 < (-_918)))\n"
	 "    {\n"
	 "        _934 = _932;\n"
	 "    }\n"
	 "    else\n"
	 "    {\n"
	 "        _934 = floatBitsToUint(_920 + 1.0);\n"
	 "    }\n"
	 "    float _941 = float(uintBitsToFloat(_934) + (-float(unpackFloat2x16(0u).x)));\n"
	 "    float _948 = _941 + unpackFloat2x16(1006648320u).x;\n"
	 "    float _953 = float(float(_941) + uintBitsToFloat(0x3f2aa000u));\n"
	 "    float _966 = (clamp(_889.x + (-0.0), 0.0, 1.0) * 2.0) * clamp(_893.x + (-0.0), 0.0, 1.0);\n"
	 "    float _983 = float(float(_941) + uintBitsToFloat(0x3eaaa000u));\n"
	 "    vec2 _985 = unpackFloat2x16(((bitfieldExtract(packFloat2x16(vec2(floor(_953), float(0.0))), int(0u), int(16u)) * 1u) << 16u) + bitfieldExtract(packFloat2x16(vec2(floor(_948), float(0.0))), int(0u), int(16u)));\n"
	 "    vec2 _992 = unpackFloat2x16(1174423040u);\n"
	 "    vec2 _1001 = unpackFloat2x16(3154164736u);\n"
	 "    vec2 _1029 = unpackFloat2x16(1006648320u);\n"
	 "    float _1032 = float(fma(_869, float(clamp(abs(fma(_948 + (-_985.x), _992.x, -_905)) + _1001.x, float(0.0), float(1.0))), -_869)) + _1029.x;\n"
	 "    float _1033 = float(fma(_869, float(clamp(abs(fma(_953 + (-_985.y), _992.y, -_905)) + _1001.y, float(0.0), float(1.0))), -_869)) + _1029.y;\n"
	 "    float _1043 = float(fma(_869, float(clamp(abs(fma(_983 + (-floor(_983)), unpackFloat2x16(1174423040u).y, -_905)) + unpackFloat2x16(3154164736u).y, float(0.0), float(1.0))), -_869)) + unpackFloat2x16(1006648320u).y;\n"
	 "    float _1053 = ((_966 + _966) * float(float(clamp(_897.x + (-0.0), 0.0, 1.0) * float(unpackFloat2x16(1073758208u).x)))) * texelFetch(fs_c3_1, ivec2((int(25u) >> 2) & 1023, int(25u) >> 12), 0)[int(25u) & 3];\n"
	 "    float _1068 = float(float(fma(float(_809 * _1043), uintBitsToFloat(0x3d93e000u), float(float(fma(float(_809 * _1033), uintBitsToFloat(0x3f372000u), float(float(float(_809 * _1032) * uintBitsToFloat(0x3e59c000u)))))))));\n"
	 "    frag_color0.x = clamp(texture(fs_tex2_170, vec2(float(float(clamp(texture(fs_tex2_168, vec2(fma(float(float(fma(float(_809), float(_1032), -_1068))), _1053, _1068) + 0.00390625, 0.0)).x + (-0.0), 0.0, 1.0) + float(unpackFloat2x16(469769216u).x))), 0.0)).x + (-0.0), 0.0, 1.0);\n"
	 "    frag_color0.y = clamp(texture(fs_tex2_178, vec2(float(float(clamp(texture(fs_tex2_168, vec2(fma(float(float(fma(float(_809), float(_1033), -_1068))), _1053, _1068) + 0.00390625, 0.0)).x + (-0.0), 0.0, 1.0) + float(unpackFloat2x16(469769216u).x))), 0.0)).x + (-0.0), 0.0, 1.0);\n"
	 "    frag_color0.z = clamp(texture(fs_tex2_180, vec2(float(float(clamp(texture(fs_tex2_168, vec2(fma(float(float(fma(float(_809), float(_1043), -_1068))), _1053, _1068) + 0.00390625, 0.0)).x + (-0.0), 0.0, 1.0) + float(unpackFloat2x16(469769216u).x))), 0.0)).x + (-0.0), 0.0, 1.0);\n"
	 "    frag_color0.w = 1.0;\n"
	 "}\n"
	 "", 1},
	/* 2 cap9-1 vertex */
	{"", ""
	 "#version 310 es\n"
	 "precision highp int;\n"
	 "precision highp float;\n"
	 "#define fma(a, b, c) ((a) * (b) + (c))\n"
	 "\n"
	 "uniform highp sampler2D vs_c3;\n"
	 "\n"
	 "uniform highp sampler2D vs_c4_1;\n"
	 "\n"
	 "uniform highp sampler2D vs_c5_1;\n"
	 "\n"
	 "layout(location = 0) in vec4 in_attr0;\n"
	 "layout(location = 1) in vec4 in_attr1;\n"
	 "out vec4 varying_0;\n"
	 "\n"
	 "void main()\n"
	 "{\n"
	 "    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);\n"
	 "    varying_0 = vec4(0.0, 0.0, 0.0, 1.0);\n"
	 "    float _90 = fma(in_attr0.z, texelFetch(vs_c5_1, ivec2((int(9u) >> 2) & 1023, int(9u) >> 12), 0)[int(9u) & 3], fma(in_attr0.x, texelFetch(vs_c5_1, ivec2((int(1u) >> 2) & 1023, int(1u) >> 12), 0)[int(1u) & 3], in_attr0.y * texelFetch(vs_c5_1, ivec2((int(5u) >> 2) & 1023, int(5u) >> 12), 0)[int(5u) & 3])) + texelFetch(vs_c5_1, ivec2((int(13u) >> 2) & 1023, int(13u) >> 12), 0)[int(13u) & 3];\n"
	 "    float _94 = fma(in_attr0.z, texelFetch(vs_c5_1, ivec2((int(8u) >> 2) & 1023, int(8u) >> 12), 0)[int(8u) & 3], fma(in_attr0.x, texelFetch(vs_c5_1, ivec2((int(0u) >> 2) & 1023, int(0u) >> 12), 0)[int(0u) & 3], in_attr0.y * texelFetch(vs_c5_1, ivec2((int(4u) >> 2) & 1023, int(4u) >> 12), 0)[int(4u) & 3])) + texelFetch(vs_c5_1, ivec2((int(12u) >> 2) & 1023, int(12u) >> 12), 0)[int(12u) & 3];\n"
	 "    float _108 = fma(in_attr0.z, texelFetch(vs_c5_1, ivec2((int(10u) >> 2) & 1023, int(10u) >> 12), 0)[int(10u) & 3], fma(in_attr0.x, texelFetch(vs_c5_1, ivec2((int(2u) >> 2) & 1023, int(2u) >> 12), 0)[int(2u) & 3], in_attr0.y * texelFetch(vs_c5_1, ivec2((int(6u) >> 2) & 1023, int(6u) >> 12), 0)[int(6u) & 3])) + texelFetch(vs_c5_1, ivec2((int(14u) >> 2) & 1023, int(14u) >> 12), 0)[int(14u) & 3];\n"
	 "    varying_0.x = fma(in_attr1.x, texelFetch(vs_c3, ivec2((int(0u) >> 2) & 1023, int(0u) >> 12), 0)[int(0u) & 3], texelFetch(vs_c3, ivec2((int(2u) >> 2) & 1023, int(2u) >> 12), 0)[int(2u) & 3]);\n"
	 "    varying_0.y = fma(in_attr1.y, texelFetch(vs_c3, ivec2((int(1u) >> 2) & 1023, int(1u) >> 12), 0)[int(1u) & 3], texelFetch(vs_c3, ivec2((int(3u) >> 2) & 1023, int(3u) >> 12), 0)[int(3u) & 3]);\n"
	 "    gl_Position.x = fma(_108, texelFetch(vs_c4_1, ivec2((int(8u) >> 2) & 1023, int(8u) >> 12), 0)[int(8u) & 3], fma(_94, texelFetch(vs_c4_1, ivec2((int(0u) >> 2) & 1023, int(0u) >> 12), 0)[int(0u) & 3], _90 * texelFetch(vs_c4_1, ivec2((int(4u) >> 2) & 1023, int(4u) >> 12), 0)[int(4u) & 3])) + texelFetch(vs_c4_1, ivec2((int(12u) >> 2) & 1023, int(12u) >> 12), 0)[int(12u) & 3];\n"
	 "    gl_Position.y = fma(_108, texelFetch(vs_c4_1, ivec2((int(9u) >> 2) & 1023, int(9u) >> 12), 0)[int(9u) & 3], fma(_94, texelFetch(vs_c4_1, ivec2((int(1u) >> 2) & 1023, int(1u) >> 12), 0)[int(1u) & 3], _90 * texelFetch(vs_c4_1, ivec2((int(5u) >> 2) & 1023, int(5u) >> 12), 0)[int(5u) & 3])) + texelFetch(vs_c4_1, ivec2((int(13u) >> 2) & 1023, int(13u) >> 12), 0)[int(13u) & 3];\n"
	 "    gl_Position.z = fma(_108, texelFetch(vs_c4_1, ivec2((int(10u) >> 2) & 1023, int(10u) >> 12), 0)[int(10u) & 3], fma(_94, texelFetch(vs_c4_1, ivec2((int(2u) >> 2) & 1023, int(2u) >> 12), 0)[int(2u) & 3], _90 * texelFetch(vs_c4_1, ivec2((int(6u) >> 2) & 1023, int(6u) >> 12), 0)[int(6u) & 3])) + texelFetch(vs_c4_1, ivec2((int(14u) >> 2) & 1023, int(14u) >> 12), 0)[int(14u) & 3];\n"
	 "    gl_Position.w = fma(_108, texelFetch(vs_c4_1, ivec2((int(11u) >> 2) & 1023, int(11u) >> 12), 0)[int(11u) & 3], fma(_94, texelFetch(vs_c4_1, ivec2((int(3u) >> 2) & 1023, int(3u) >> 12), 0)[int(3u) & 3], _90 * texelFetch(vs_c4_1, ivec2((int(7u) >> 2) & 1023, int(7u) >> 12), 0)[int(7u) & 3])) + texelFetch(vs_c4_1, ivec2((int(15u) >> 2) & 1023, int(15u) >> 12), 0)[int(15u) & 3];\n"
	 "}\n"
	 "", 0},
	/* 3 cap9-2 fragment */
	{"", ""
	 "#version 310 es\n"
	 "precision highp int;\n"
	 "precision mediump float;\n"
	 "#define unpackFloat2x16(a) unpackHalf2x16(a)\n"
	 "#define packFloat2x16(a) packHalf2x16(a)\n"
	 "#define fma(a, b, c) ((a) * (b) + (c))\n"
	 "#if defined(GL_AMD_gpu_shader_half_float)\n"
	 "#elif defined(GL_EXT_shader_explicit_arithmetic_types_float16)\n"
	 "#else\n"
	 "\n"
	 "#endif\n"
	 "\n"
	 "\n"
	 "\n"
	 "uniform highp sampler2D fs_c3_1;\n"
	 "\n"
	 "uniform sampler2D fs_tex2_168;\n"
	 "uniform sampler2D fs_tex2_170;\n"
	 "uniform sampler2D fs_tex2_178;\n"
	 "\n"
	 "in vec4 varying_0;\n"
	 "layout(location = 0) out vec4 frag_color0;\n"
	 "\n"
	 "void main()\n"
	 "{\n"
	 "    vec4 _91 = texture(fs_tex2_168, vec2(varying_0.x, varying_0.y));\n"
	 "    float _92 = _91.x;\n"
	 "    float _93 = _91.y;\n"
	 "    float _95 = _91.z;\n"
	 "    bool _110 = _93 <= uintBitsToFloat(0x3d25aee6u);\n"
	 "    float _127 = log2(abs((_95 + 0.054999999701976776123046875) * uintBitsToFloat(0x3f72a000u)));\n"
	 "    float _133 = log2(abs((_93 + 0.054999999701976776123046875) * uintBitsToFloat(0x3f72a000u)));\n"
	 "    uint _142;\n"
	 "    if (_110)\n"
	 "    {\n"
	 "        _142 = floatBitsToUint(_93 * 0.077399380505084991455078125);\n"
	 "    }\n"
	 "    else\n"
	 "    {\n"
	 "        _142 = floatBitsToUint(_93);\n"
	 "    }\n"
	 "    uint _158;\n"
	 "    if (!_110)\n"
	 "    {\n"
	 "        _158 = floatBitsToUint(exp2(_133 * uintBitsToFloat(0x4019a000u)));\n"
	 "    }\n"
	 "    else\n"
	 "    {\n"
	 "        _158 = _142;\n"
	 "    }\n"
	 "    uint _162;\n"
	 "    if (!(_95 <= uintBitsToFloat(0x3d25aee6u)))\n"
	 "    {\n"
	 "        _162 = floatBitsToUint(exp2(_127 * uintBitsToFloat(0x4019a000u)));\n"
	 "    }\n"
	 "    else\n"
	 "    {\n"
	 "        _162 = floatBitsToUint(_95 * 0.077399380505084991455078125);\n"
	 "    }\n"
	 "    uint _164;\n"
	 "    if (!(_92 <= uintBitsToFloat(0x3d25aee6u)))\n"
	 "    {\n"
	 "        _164 = floatBitsToUint(exp2(log2(abs((_92 + 0.054999999701976776123046875) * uintBitsToFloat(0x3f72a000u))) * uintBitsToFloat(0x4019a000u)));\n"
	 "    }\n"
	 "    else\n"
	 "    {\n"
	 "        _164 = floatBitsToUint(_92 * 0.077399380505084991455078125);\n"
	 "    }\n"
	 "    uint _256;\n"
	 "    uint _257;\n"
	 "    uint _258;\n"
	 "    uint _259;\n"
	 "    uint _260;\n"
	 "    if (0.0 < texelFetch(fs_c3_1, ivec2((int(14u) >> 2) & 1023, int(14u) >> 12), 0)[int(14u) & 3])\n"
	 "    {\n"
	 "        float _183 = float((-float(unpackFloat2x16(0u).x)) + uintBitsToFloat(0x3f800000u));\n"
	 "        float _196 = ((abs(varying_0.x + (-texelFetch(fs_c3_1, ivec2((int(12u) >> 2) & 1023, int(12u) >> 12), 0)[int(12u) & 3])) + (-0.0)) * texelFetch(fs_c3_1, ivec2((int(14u) >> 2) & 1023, int(14u) >> 12), 0)[int(14u) & 3]) * texelFetch(fs_c3_1, ivec2((int(11u) >> 2) & 1023, int(11u) >> 12), 0)[int(11u) & 3];\n"
	 "        float _199 = (abs(varying_0.y + (-texelFetch(fs_c3_1, ivec2((int(13u) >> 2) & 1023, int(13u) >> 12), 0)[int(13u) & 3])) + (-0.0)) * texelFetch(fs_c3_1, ivec2((int(14u) >> 2) & 1023, int(14u) >> 12), 0)[int(14u) & 3];\n"
	 "        float _227 = exp2(log2(max(0.0, (-fma(_199, _199, _196 * _196)) + 1.0)) * texelFetch(fs_c3_1, ivec2((int(15u) >> 2) & 1023, int(15u) >> 12), 0)[int(15u) & 3]);\n"
	 "        float _238 = fma(float(float(float(_183) + (-texelFetch(fs_c3_1, ivec2((int(9u) >> 2) & 1023, int(9u) >> 12), 0)[int(9u) & 3]))), _227, texelFetch(fs_c3_1, ivec2((int(9u) >> 2) & 1023, int(9u) >> 12), 0)[int(9u) & 3]);\n"
	 "        float _245 = fma(float(float(float(_183) + (-texelFetch(fs_c3_1, ivec2((int(10u) >> 2) & 1023, int(10u) >> 12), 0)[int(10u) & 3]))), _227, texelFetch(fs_c3_1, ivec2((int(10u) >> 2) & 1023, int(10u) >> 12), 0)[int(10u) & 3]);\n"
	 "        _256 = floatBitsToUint(_245);\n"
	 "        _257 = floatBitsToUint(_238);\n"
	 "        _258 = floatBitsToUint(uintBitsToFloat(_162) * _245);\n"
	 "        _259 = floatBitsToUint(_238 * uintBitsToFloat(_158));\n"
	 "        _260 = floatBitsToUint(fma(float(float(float(_183) + (-texelFetch(fs_c3_1, ivec2((int(8u) >> 2) & 1023, int(8u) >> 12), 0)[int(8u) & 3]))), _227, texelFetch(fs_c3_1, ivec2((int(8u) >> 2) & 1023, int(8u) >> 12), 0)[int(8u) & 3]) * uintBitsToFloat(_164));\n"
	 "    }\n"
	 "    else\n"
	 "    {\n"
	 "        _256 = floatBitsToUint(_127);\n"
	 "        _257 = floatBitsToUint(_133);\n"
	 "        _258 = _162;\n"
	 "        _259 = _158;\n"
	 "        _260 = _164;\n"
	 "    }\n"
	 "    float _265 = clamp(uintBitsToFloat(_260) * texelFetch(fs_c3_1, ivec2((int(3u) >> 2) & 1023, int(3u) >> 12), 0)[int(3u) & 3], 0.0, 1.0);\n"
	 "    float _271 = clamp(uintBitsToFloat(_259) * texelFetch(fs_c3_1, ivec2((int(3u) >> 2) & 1023, int(3u) >> 12), 0)[int(3u) & 3], 0.0, 1.0);\n"
	 "    float _277 = clamp(uintBitsToFloat(_258) * texelFetch(fs_c3_1, ivec2((int(3u) >> 2) & 1023, int(3u) >> 12), 0)[int(3u) & 3], 0.0, 1.0);\n"
	 "    uint _488;\n"
	 "    uint _489;\n"
	 "    uint _490;\n"
	 "    if (0.0 < texelFetch(fs_c3_1, ivec2((int(7u) >> 2) & 1023, int(7u) >> 12), 0)[int(7u) & 3])\n"
	 "    {\n"
	 "        bool _282 = _277 <= uintBitsToFloat(0x3b4d2000u);\n"
	 "        float _291 = float((-float(unpackFloat2x16(0u).x)) + uintBitsToFloat(0x3d614000u));\n"
	 "        bool _295 = _271 <= uintBitsToFloat(0x3b4d2000u);\n"
	 "        uint _314;\n"
	 "        if (_282)\n"
	 "        {\n"
	 "            _314 = floatBitsToUint(_277 * uintBitsToFloat(0x414ec000u));\n"
	 "        }\n"
	 "        else\n"
	 "        {\n"
	 "            _314 = _257;\n"
	 "        }\n"
	 "        uint _319;\n"
	 "        if (_295)\n"
	 "        {\n"
	 "            _319 = floatBitsToUint(_271 * uintBitsToFloat(0x414ec000u));\n"
	 "        }\n"
	 "        else\n"
	 "        {\n"
	 "            _319 = _256;\n"
	 "        }\n"
	 "        uint _344;\n"
	 "        if (_265 <= uintBitsToFloat(0x3b4d2000u))\n"
	 "        {\n"
	 "            _344 = floatBitsToUint(_265 * uintBitsToFloat(0x414ec000u));\n"
	 "        }\n"
	 "        else\n"
	 "        {\n"
	 "            _344 = floatBitsToUint(float(float(fma(exp2(log2(_265) * uintBitsToFloat(0x3ed56000u)), uintBitsToFloat(0x3f870000u), -float(_291)))));\n"
	 "        }\n"
	 "        uint _351;\n"
	 "        if (!_282)\n"
	 "        {\n"
	 "            _351 = floatBitsToUint(float(float(fma(exp2(log2(_277) * uintBitsToFloat(0x3ed56000u)), uintBitsToFloat(0x3f870000u), -float(_291)))));\n"
	 "        }\n"
	 "        else\n"
	 "        {\n"
	 "            _351 = _314;\n"
	 "        }\n"
	 "        uint _371;\n"
	 "        if (!_295)\n"
	 "        {\n"
	 "            _371 = floatBitsToUint(float(float(fma(exp2(log2(_271) * uintBitsToFloat(0x3ed56000u)), uintBitsToFloat(0x3f870000u), -float(_291)))));\n"
	 "        }\n"
	 "        else\n"
	 "        {\n"
	 "            _371 = _319;\n"
	 "        }\n"
	 "        float _376 = floor(uintBitsToFloat(_351) * texelFetch(fs_c3_1, ivec2((int(6u) >> 2) & 1023, int(6u) >> 12), 0)[int(6u) & 3]);\n"
	 "        float _383 = fma(uintBitsToFloat(_371) * texelFetch(fs_c3_1, ivec2((int(6u) >> 2) & 1023, int(6u) >> 12), 0)[int(6u) & 3], texelFetch(fs_c3_1, ivec2((int(5u) >> 2) & 1023, int(5u) >> 12), 0)[int(5u) & 3], 0.5 * texelFetch(fs_c3_1, ivec2((int(5u) >> 2) & 1023, int(5u) >> 12), 0)[int(5u) & 3]);\n"
	 "        float _386 = fma(_376, texelFetch(fs_c3_1, ivec2((int(5u) >> 2) & 1023, int(5u) >> 12), 0)[int(5u) & 3], fma(uintBitsToFloat(_344) * texelFetch(fs_c3_1, ivec2((int(6u) >> 2) & 1023, int(6u) >> 12), 0)[int(6u) & 3], texelFetch(fs_c3_1, ivec2((int(4u) >> 2) & 1023, int(4u) >> 12), 0)[int(4u) & 3], 0.5 * texelFetch(fs_c3_1, ivec2((int(4u) >> 2) & 1023, int(4u) >> 12), 0)[int(4u) & 3]));\n"
	 "        vec4 _392 = textureLod(fs_tex2_178, vec2(_386, _383), 0.0);\n"
	 "        float _393 = _392.x;\n"
	 "        float _394 = _392.y;\n"
	 "        float _395 = _392.z;\n"
	 "        vec4 _398 = textureLod(fs_tex2_178, vec2(_386 + texelFetch(fs_c3_1, ivec2((int(5u) >> 2) & 1023, int(5u) >> 12), 0)[int(5u) & 3], _383), 0.0);\n"
	 "        float _406 = fma(uintBitsToFloat(_351), texelFetch(fs_c3_1, ivec2((int(6u) >> 2) & 1023, int(6u) >> 12), 0)[int(6u) & 3], -_376);\n"
	 "        float _428 = fma(fma(_406, (-_395) + _398.z, _395) + (-uintBitsToFloat(_351)), texelFetch(fs_c3_1, ivec2((int(7u) >> 2) & 1023, int(7u) >> 12), 0)[int(7u) & 3], uintBitsToFloat(_351));\n"
	 "        float _432 = fma(fma(_406, (-_394) + _398.y, _394) + (-uintBitsToFloat(_371)), texelFetch(fs_c3_1, ivec2((int(7u) >> 2) & 1023, int(7u) >> 12), 0)[int(7u) & 3], uintBitsToFloat(_371));\n"
	 "        float _436 = fma(fma(_406, (-_393) + _398.x, _393) + (-uintBitsToFloat(_344)), texelFetch(fs_c3_1, ivec2((int(7u) >> 2) & 1023, int(7u) >> 12), 0)[int(7u) & 3], uintBitsToFloat(_344));\n"
	 "        uint _483;\n"
	 "        if (_428 <= uintBitsToFloat(0x3d25aee6u))\n"
	 "        {\n"
	 "            _483 = floatBitsToUint(_428 * 0.077399380505084991455078125);\n"
	 "        }\n"
	 "        else\n"
	 "        {\n"
	 "            _483 = floatBitsToUint(exp2(log2(abs((_428 + 0.054999999701976776123046875) * uintBitsToFloat(0x3f72a000u))) * uintBitsToFloat(0x4019a000u)));\n"
	 "        }\n"
	 "        uint _484;\n"
	 "        if (_432 <= uintBitsToFloat(0x3d25aee6u))\n"
	 "        {\n"
	 "            _484 = floatBitsToUint(_432 * 0.077399380505084991455078125);\n"
	 "        }\n"
	 "        else\n"
	 "        {\n"
	 "            _484 = floatBitsToUint(exp2(log2(abs((_432 + 0.054999999701976776123046875) * uintBitsToFloat(0x3f72a000u))) * uintBitsToFloat(0x4019a000u)));\n"
	 "        }\n"
	 "        uint _487;\n"
	 "        if (_436 <= uintBitsToFloat(0x3d25aee6u))\n"
	 "        {\n"
	 "            _487 = floatBitsToUint(_436 * 0.077399380505084991455078125);\n"
	 "        }\n"
	 "        else\n"
	 "        {\n"
	 "            _487 = floatBitsToUint(exp2(log2(abs((_436 + 0.054999999701976776123046875) * uintBitsToFloat(0x3f72a000u))) * uintBitsToFloat(0x4019a000u)));\n"
	 "        }\n"
	 "        _488 = _484;\n"
	 "        _489 = _487;\n"
	 "        _490 = _483;\n"
	 "    }\n"
	 "    else\n"
	 "    {\n"
	 "        _488 = floatBitsToUint(_271);\n"
	 "        _489 = floatBitsToUint(_265);\n"
	 "        _490 = floatBitsToUint(_277);\n"
	 "    }\n"
	 "    float _499 = floor(uintBitsToFloat(_490) * texelFetch(fs_c3_1, ivec2((int(2u) >> 2) & 1023, int(2u) >> 12), 0)[int(2u) & 3]);\n"
	 "    float _515 = fma(uintBitsToFloat(_488) * texelFetch(fs_c3_1, ivec2((int(2u) >> 2) & 1023, int(2u) >> 12), 0)[int(2u) & 3], texelFetch(fs_c3_1, ivec2((int(1u) >> 2) & 1023, int(1u) >> 12), 0)[int(1u) & 3], 0.5 * texelFetch(fs_c3_1, ivec2((int(1u) >> 2) & 1023, int(1u) >> 12), 0)[int(1u) & 3]);\n"
	 "    float _518 = fma(_499, texelFetch(fs_c3_1, ivec2((int(1u) >> 2) & 1023, int(1u) >> 12), 0)[int(1u) & 3], fma(uintBitsToFloat(_489) * texelFetch(fs_c3_1, ivec2((int(2u) >> 2) & 1023, int(2u) >> 12), 0)[int(2u) & 3], texelFetch(fs_c3_1, ivec2((int(0u) >> 2) & 1023, int(0u) >> 12), 0)[int(0u) & 3], 0.5 * texelFetch(fs_c3_1, ivec2((int(0u) >> 2) & 1023, int(0u) >> 12), 0)[int(0u) & 3]));\n"
	 "    vec4 _521 = textureLod(fs_tex2_170, vec2(_518, _515), 0.0);\n"
	 "    float _522 = _521.x;\n"
	 "    float _523 = _521.y;\n"
	 "    float _524 = _521.z;\n"
	 "    vec4 _530 = textureLod(fs_tex2_170, vec2(_518 + texelFetch(fs_c3_1, ivec2((int(1u) >> 2) & 1023, int(1u) >> 12), 0)[int(1u) & 3], _515), 0.0);\n"
	 "    float _538 = fma(uintBitsToFloat(_490), texelFetch(fs_c3_1, ivec2((int(2u) >> 2) & 1023, int(2u) >> 12), 0)[int(2u) & 3], -_499);\n"
	 "    float _552 = float((-float(unpackFloat2x16(0u).x)) + uintBitsToFloat(0x3d614000u));\n"
	 "    float _553 = fma(_538, (-_524) + _530.z, _524);\n"
	 "    float _554 = fma(_538, (-_522) + _530.x, _522);\n"
	 "    float _557 = fma(_538, (-_523) + _530.y, _523);\n"
	 "    uint _607;\n"
	 "    if (!(_553 <= uintBitsToFloat(0x3b4d2e1cu)))\n"
	 "    {\n"
	 "        _607 = floatBitsToUint(float(float(fma(exp2(log2(abs(_553)) * uintBitsToFloat(0x3ed56000u)), uintBitsToFloat(0x3f870000u), -float(_552)))));\n"
	 "    }\n"
	 "    else\n"
	 "    {\n"
	 "        _607 = floatBitsToUint(_553 * 12.9200000762939453125);\n"
	 "    }\n"
	 "    uint _609;\n"
	 "    if (!(_554 <= uintBitsToFloat(0x3b4d2e1cu)))\n"
	 "    {\n"
	 "        _609 = floatBitsToUint(float(float(fma(exp2(log2(abs(_554)) * uintBitsToFloat(0x3ed56000u)), uintBitsToFloat(0x3f870000u), -float(_552)))));\n"
	 "    }\n"
	 "    else\n"
	 "    {\n"
	 "        _609 = floatBitsToUint(_554 * 12.9200000762939453125);\n"
	 "    }\n"
	 "    uint _619;\n"
	 "    if (!(_557 <= uintBitsToFloat(0x3b4d2e1cu)))\n"
	 "    {\n"
	 "        _619 = floatBitsToUint(float(float(fma(exp2(log2(abs(_557)) * uintBitsToFloat(0x3ed56000u)), uintBitsToFloat(0x3f870000u), -float(_552)))));\n"
	 "    }\n"
	 "    else\n"
	 "    {\n"
	 "        _619 = floatBitsToUint(_557 * 12.9200000762939453125);\n"
	 "    }\n"
	 "    frag_color0.x = uintBitsToFloat(_609);\n"
	 "    frag_color0.y = uintBitsToFloat(_619);\n"
	 "    frag_color0.z = uintBitsToFloat(_607);\n"
	 "    frag_color0.w = 1.0;\n"
	 "}\n"
	 "", 1},
	/* 4 main-acb200 vertex */
	{"", ""
	 "#version 310 es\n"
	 "precision highp int;\n"
	 "precision highp float;\n"
	 "layout(location = 0) in vec4 in_attr0;\n"
	 "layout(location = 1) in vec4 in_attr1;\n"
	 "out vec4 varying_0;\n"
	 "\n"
	 "void main()\n"
	 "{\n"
	 "    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);\n"
	 "    varying_0 = vec4(0.0, 0.0, 0.0, 1.0);\n"
	 "    gl_Position.x = in_attr0.x;\n"
	 "    gl_Position.y = in_attr0.y;\n"
	 "    varying_0.x = in_attr1.x;\n"
	 "    varying_0.y = in_attr1.y;\n"
	 "}\n"
	 "", 0},
	/* 5 main-acbdc0 vertex */
	{"", ""
	 "#version 310 es\n"
	 "precision highp int;\n"
	 "precision highp float;\n"
	 "\n"
	 "layout(location = 0) in vec4 in_attr0;\n"
	 "layout(location = 1) in vec4 in_attr1;\n"
	 "out vec4 varying_0;\n"
	 "out vec4 varying_1;\n"
	 "\n"
	 "void main()\n"
	 "{\n"
	 "    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);\n"
	 "    varying_0 = vec4(0.0, 0.0, 0.0, 1.0);\n"
	 "    varying_1 = vec4(0.0, 0.0, 0.0, 1.0);\n"
	 "    uint _52 = uint(gl_InstanceIndex) - uint(gl_BaseInstanceARB);\n"
	 "    gl_Position.x = in_attr0.x;\n"
	 "    gl_Position.y = in_attr0.y;\n"
	 "    varying_0.x = in_attr1.x;\n"
	 "    varying_0.y = in_attr1.y;\n"
	 "    gl_Layer = int(floatBitsToUint(uintBitsToFloat(_52)));\n"
	 "    varying_1.x = uintBitsToFloat(_52);\n"
	 "}\n"
	 "", 0},
	/* 6 main-acca80 fragment */
	{"", ""
	 "#version 310 es\n"
	 "precision mediump float;\n"
	 "out vec4 frag_color0;\n"
	 "uniform sampler2D fs_tex2_168;\n"
	 "uniform vec2 fs_tex2_168_size;\n"
	 "in vec4 in_attr0;\n"
	 "/* deviation (documented): the original samples several mip levels with texelFetch; ESSL100 has no explicit-LOD fetch, so this is the base level only, sampled at the same integer texel coordinate. */\n"
	 "void main() {\n"
	 "    vec2 texel = floor(in_attr0.xy * fs_tex2_168_size);\n"
	 "    frag_color0 = texture(fs_tex2_168, (texel + 0.5) / fs_tex2_168_size);\n"
	 "}\n"
	 "", 1},
	/* 7 main-acd740 fragment */
	{"", ""
	 "#version 310 es\n"
	 "precision mediump float;\n"
	 "out vec4 frag_color0;\n"
	 "uniform sampler2D fs_tex2_168;\n"
	 "uniform vec2 fs_tex2_168_size;\n"
	 "in vec4 in_attr0;\n"
	 "/* deviation (documented): the original samples several mip levels with texelFetch; ESSL100 has no explicit-LOD fetch, so this is the base level only, sampled at the same integer texel coordinate. */\n"
	 "void main() {\n"
	 "    vec2 texel = floor(in_attr0.xy * fs_tex2_168_size);\n"
	 "    frag_color0 = texture(fs_tex2_168, (texel + 0.5) / fs_tex2_168_size);\n"
	 "}\n"
	 "", 1},
	/* 8 main-ace500 fragment */
	{"", ""
	 "#version 310 es\n"
	 "precision mediump float;\n"
	 "out vec4 frag_color0;\n"
	 "uniform sampler2D fs_tex2_168;\n"
	 "uniform vec2 fs_tex2_168_size;\n"
	 "in vec4 in_attr0;\n"
	 "/* deviation (documented): the original samples several mip levels with texelFetch; ESSL100 has no explicit-LOD fetch, so this is the base level only, sampled at the same integer texel coordinate. */\n"
	 "void main() {\n"
	 "    vec2 texel = floor(in_attr0.xy * fs_tex2_168_size);\n"
	 "    frag_color0 = texture(fs_tex2_168, (texel + 0.5) / fs_tex2_168_size);\n"
	 "}\n"
	 "", 1},
	/* 9 main-acf3c0 fragment */
	{"", ""
	 "#version 310 es\n"
	 "precision mediump float;\n"
	 "out vec4 frag_color0;\n"
	 "uniform sampler2D fs_tex2_168;\n"
	 "uniform vec2 fs_tex2_168_size;\n"
	 "in vec4 in_attr0;\n"
	 "/* deviation (documented): the original samples several mip levels with texelFetch; ESSL100 has no explicit-LOD fetch, so this is the base level only, sampled at the same integer texel coordinate. */\n"
	 "void main() {\n"
	 "    vec2 texel = floor(in_attr0.xy * fs_tex2_168_size);\n"
	 "    frag_color0 = texture(fs_tex2_168, (texel + 0.5) / fs_tex2_168_size);\n"
	 "}\n"
	 "", 1},
	/* 10 main-ad0080 fragment */
	{"", ""
	 "#version 310 es\n"
	 "precision mediump float;\n"
	 "out vec4 frag_color0;\n"
	 "uniform sampler2D fs_tex2_168;\n"
	 "uniform vec2 fs_tex2_168_size;\n"
	 "in vec4 in_attr0;\n"
	 "/* deviation (documented): the original samples several mip levels with texelFetch; ESSL100 has no explicit-LOD fetch, so this is the base level only, sampled at the same integer texel coordinate. */\n"
	 "void main() {\n"
	 "    vec2 texel = floor(in_attr0.xy * fs_tex2_168_size);\n"
	 "    frag_color0 = texture(fs_tex2_168, (texel + 0.5) / fs_tex2_168_size);\n"
	 "}\n"
	 "", 1},
	/* 11 main-ad0e40 fragment */
	{"", ""
	 "#version 310 es\n"
	 "precision mediump float;\n"
	 "out vec4 frag_color0;\n"
	 "uniform sampler2D fs_tex2_168;\n"
	 "uniform vec2 fs_tex2_168_size;\n"
	 "in vec4 in_attr0;\n"
	 "/* deviation (documented): the original samples several mip levels with texelFetch; ESSL100 has no explicit-LOD fetch, so this is the base level only, sampled at the same integer texel coordinate. */\n"
	 "void main() {\n"
	 "    vec2 texel = floor(in_attr0.xy * fs_tex2_168_size);\n"
	 "    frag_color0 = texture(fs_tex2_168, (texel + 0.5) / fs_tex2_168_size);\n"
	 "}\n"
	 "", 1},
	/* 12 main-ad1d00 vertex */
	{"", ""
	 "#version 310 es\n"
	 "precision highp int;\n"
	 "precision highp float;\n"
	 "layout(location = 0) in vec4 in_attr0;\n"
	 "layout(location = 1) in vec4 in_attr1;\n"
	 "out vec4 varying_0;\n"
	 "\n"
	 "void main()\n"
	 "{\n"
	 "    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);\n"
	 "    varying_0 = vec4(0.0, 0.0, 0.0, 1.0);\n"
	 "    gl_Position.x = in_attr0.x;\n"
	 "    gl_Position.y = in_attr0.y;\n"
	 "    varying_0.x = in_attr1.x;\n"
	 "    varying_0.y = in_attr1.y;\n"
	 "}\n"
	 "", 0},
	/* 13 main-ad28c0 fragment */
	{"", ""
	 "#version 310 es\n"
	 "precision highp int;\n"
	 "precision mediump float;\n"
	 "uniform sampler2D fs_tex2_168;\n"
	 "\n"
	 "in vec4 varying_0;\n"
	 "layout(location = 0) out vec4 frag_color0;\n"
	 "\n"
	 "void main()\n"
	 "{\n"
	 "    float _53 = 1.0 / gl_FragCoord.w;\n"
	 "    vec4 _58 = texture(fs_tex2_168, vec2(_53 * (varying_0.x * gl_FragCoord.w), _53 * (varying_0.y * gl_FragCoord.w)));\n"
	 "    frag_color0.x = _58.x;\n"
	 "    frag_color0.y = _58.y;\n"
	 "    frag_color0.z = _58.z;\n"
	 "    frag_color0.w = _58.w;\n"
	 "}\n"
	 "", 1},
	/* 14 new vertex */
	{"", ""
	 "#version 310 es\n"
	 "precision highp int;\n"
	 "precision highp float;\n"
	 "#define fma(a, b, c) ((a) * (b) + (c))\n"
	 "\n"
	 "uniform highp sampler2D vs_c4;\n"
	 "\n"
	 "uniform highp sampler2D vs_c5_1;\n"
	 "\n"
	 "layout(location = 0) in vec4 in_attr0;\n"
	 "layout(location = 1) in vec4 in_attr1;\n"
	 "out vec4 varying_0;\n"
	 "\n"
	 "void main()\n"
	 "{\n"
	 "    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);\n"
	 "    varying_0 = vec4(0.0, 0.0, 0.0, 1.0);\n"
	 "    varying_0.x = in_attr1.x;\n"
	 "    varying_0.y = in_attr1.y;\n"
	 "    float _95 = fma(in_attr0.z, texelFetch(vs_c5_1, ivec2((int(9u) >> 2) & 1023, int(9u) >> 12), 0)[int(9u) & 3], fma(in_attr0.x, texelFetch(vs_c5_1, ivec2((int(1u) >> 2) & 1023, int(1u) >> 12), 0)[int(1u) & 3], in_attr0.y * texelFetch(vs_c5_1, ivec2((int(5u) >> 2) & 1023, int(5u) >> 12), 0)[int(5u) & 3])) + texelFetch(vs_c5_1, ivec2((int(13u) >> 2) & 1023, int(13u) >> 12), 0)[int(13u) & 3];\n"
	 "    float _103 = fma(in_attr0.z, texelFetch(vs_c5_1, ivec2((int(8u) >> 2) & 1023, int(8u) >> 12), 0)[int(8u) & 3], fma(in_attr0.x, texelFetch(vs_c5_1, ivec2((int(0u) >> 2) & 1023, int(0u) >> 12), 0)[int(0u) & 3], in_attr0.y * texelFetch(vs_c5_1, ivec2((int(4u) >> 2) & 1023, int(4u) >> 12), 0)[int(4u) & 3])) + texelFetch(vs_c5_1, ivec2((int(12u) >> 2) & 1023, int(12u) >> 12), 0)[int(12u) & 3];\n"
	 "    float _121 = fma(in_attr0.z, texelFetch(vs_c5_1, ivec2((int(10u) >> 2) & 1023, int(10u) >> 12), 0)[int(10u) & 3], fma(in_attr0.x, texelFetch(vs_c5_1, ivec2((int(2u) >> 2) & 1023, int(2u) >> 12), 0)[int(2u) & 3], in_attr0.y * texelFetch(vs_c5_1, ivec2((int(6u) >> 2) & 1023, int(6u) >> 12), 0)[int(6u) & 3])) + texelFetch(vs_c5_1, ivec2((int(14u) >> 2) & 1023, int(14u) >> 12), 0)[int(14u) & 3];\n"
	 "    float _137 = fma(in_attr0.z, texelFetch(vs_c5_1, ivec2((int(11u) >> 2) & 1023, int(11u) >> 12), 0)[int(11u) & 3], fma(in_attr0.x, texelFetch(vs_c5_1, ivec2((int(3u) >> 2) & 1023, int(3u) >> 12), 0)[int(3u) & 3], in_attr0.y * texelFetch(vs_c5_1, ivec2((int(7u) >> 2) & 1023, int(7u) >> 12), 0)[int(7u) & 3])) + texelFetch(vs_c5_1, ivec2((int(15u) >> 2) & 1023, int(15u) >> 12), 0)[int(15u) & 3];\n"
	 "    gl_Position.x = fma(_137, texelFetch(vs_c4, ivec2((int(12u) >> 2) & 1023, int(12u) >> 12), 0)[int(12u) & 3], fma(_121, texelFetch(vs_c4, ivec2((int(8u) >> 2) & 1023, int(8u) >> 12), 0)[int(8u) & 3], fma(_103, texelFetch(vs_c4, ivec2((int(0u) >> 2) & 1023, int(0u) >> 12), 0)[int(0u) & 3], _95 * texelFetch(vs_c4, ivec2((int(4u) >> 2) & 1023, int(4u) >> 12), 0)[int(4u) & 3])));\n"
	 "    gl_Position.y = fma(_137, texelFetch(vs_c4, ivec2((int(13u) >> 2) & 1023, int(13u) >> 12), 0)[int(13u) & 3], fma(_121, texelFetch(vs_c4, ivec2((int(9u) >> 2) & 1023, int(9u) >> 12), 0)[int(9u) & 3], fma(_103, texelFetch(vs_c4, ivec2((int(1u) >> 2) & 1023, int(1u) >> 12), 0)[int(1u) & 3], _95 * texelFetch(vs_c4, ivec2((int(5u) >> 2) & 1023, int(5u) >> 12), 0)[int(5u) & 3])));\n"
	 "    gl_Position.z = fma(_137, texelFetch(vs_c4, ivec2((int(14u) >> 2) & 1023, int(14u) >> 12), 0)[int(14u) & 3], fma(_121, texelFetch(vs_c4, ivec2((int(10u) >> 2) & 1023, int(10u) >> 12), 0)[int(10u) & 3], fma(_103, texelFetch(vs_c4, ivec2((int(2u) >> 2) & 1023, int(2u) >> 12), 0)[int(2u) & 3], _95 * texelFetch(vs_c4, ivec2((int(6u) >> 2) & 1023, int(6u) >> 12), 0)[int(6u) & 3])));\n"
	 "    gl_Position.w = fma(_137, texelFetch(vs_c4, ivec2((int(15u) >> 2) & 1023, int(15u) >> 12), 0)[int(15u) & 3], fma(_121, texelFetch(vs_c4, ivec2((int(11u) >> 2) & 1023, int(11u) >> 12), 0)[int(11u) & 3], fma(_103, texelFetch(vs_c4, ivec2((int(3u) >> 2) & 1023, int(3u) >> 12), 0)[int(3u) & 3], _95 * texelFetch(vs_c4, ivec2((int(7u) >> 2) & 1023, int(7u) >> 12), 0)[int(7u) & 3])));\n"
	 "}\n"
	 "", 0},
};

struct mm_shader_uniform {const char *name;unsigned stage,binding,offset,size,components,word_boolean,word_int,word_array;};
struct mm_shader_sampler {const char *name;unsigned stage,binding,offset,size_query,texel_fetch_bits,cpu_view,separate_sampler,sampler_offset;};
struct mm_shader_attribute {const char *name;unsigned location,kind;};
struct mm_shader_binding {
 const char *sha256,*error;unsigned control_size,code_size;
 unsigned uniform_count,sampler_count,attribute_count,color_write_mask,requires_fragment_highp,requires_fragment_mediump,wave_contract;
 unsigned varying_read_masks[8],varying_write_masks[8];
 uint8_t code_hash[32],control_hash[32];
 struct mm_shader_uniform uniforms[64];struct mm_shader_sampler samplers[8];struct mm_shader_attribute attributes[16];
};
static const struct mm_shader_binding nvn_megaman_shader_bindings[]={
 /* 0 cap11-0 fragment */
 {.sha256="210a6f83b1c5ce8f90abdd28a0fb9e72",.error=NULL,.control_size=2176,.code_size=512,.sampler_count=1,.attribute_count=1,.color_write_mask=15,.requires_fragment_highp=0,.requires_fragment_mediump=1,.wave_contract=0,
  .uniform_count=1,.uniforms={{.name="fs_c3",.stage=1,.binding=0,.offset=0,.size=65536,.components=4,.word_boolean=0,.word_int=0,.word_array=4096}},
  .varying_read_masks={3,0,0,0,0,0,0,0},.varying_write_masks={15,0,0,0,0,0,0,0},
  .code_hash={0x21,0x0a,0x6f,0x83,0xb1,0xc5,0xce,0x8f,0x90,0xab,0xdd,0x28,0xa0,0xfb,0x9e,0x72,0x9d,0x70,0x64,0xbb,0xc0,0x05,0x05,0x00,0x2c,0x8a,0x9b,0xd1,0x22,0x8f,0x05,0x92},.control_hash={0xa4,0x52,0xc7,0x35,0x30,0x85,0x8d,0xc0,0xad,0x9e,0x7f,0x2d,0x34,0xad,0x0a,0x29,0x2f,0xf3,0x3e,0xdb,0xed,0x82,0x27,0x85,0x58,0x98,0xab,0xa3,0x29,0x9f,0x18,0x1f},
  .samplers={{.name="fs_tex2_168",.stage=1,.binding=14,.offset=328,.size_query=1,.texel_fetch_bits=0,.cpu_view=0,.separate_sampler=1,.sampler_offset=1384}},
  .attributes={{.name="in_attr0",.location=0,.kind=0}}},
 /* 1 cap9-0 fragment */
 {.sha256="67400db286eebca247eb1e386ce53079",.error=NULL,.control_size=2176,.code_size=5120,.sampler_count=8,.attribute_count=1,.color_write_mask=15,.requires_fragment_highp=0,.requires_fragment_mediump=1,.wave_contract=0,
  .uniform_count=1,.uniforms={{.name="fs_c3_1",.stage=1,.binding=0,.offset=0,.size=65536,.components=4,.word_boolean=0,.word_int=0,.word_array=4096}},
  .varying_read_masks={3,0,0,0,0,0,0,0},.varying_write_masks={7,0,0,0,0,0,0,0},
  .code_hash={0x67,0x40,0x0d,0xb2,0x86,0xee,0xbc,0xa2,0x47,0xeb,0x1e,0x38,0x6c,0xe5,0x30,0x79,0xaf,0x75,0x42,0x19,0x92,0x2b,0x70,0x9a,0x46,0x76,0x3c,0x1b,0x7a,0xcb,0x54,0x6e},.control_hash={0x08,0x3a,0x6d,0xeb,0xbc,0xdf,0x58,0xcf,0x60,0xa8,0xe4,0x25,0xf6,0x4f,0x01,0xec,0xba,0x8e,0xf6,0x29,0x0e,0x56,0x30,0xfa,0xd3,0xdb,0x2d,0x1f,0x10,0x84,0x21,0xa4},
  .samplers={{.name="fs_tex2_168",.stage=1,.binding=14,.offset=328,.size_query=1,.texel_fetch_bits=0,.cpu_view=0,.separate_sampler=1,.sampler_offset=1384},{.name="fs_tex2_170",.stage=1,.binding=14,.offset=336,.size_query=1,.texel_fetch_bits=0,.cpu_view=0,.separate_sampler=1,.sampler_offset=1384},{.name="fs_tex2_178",.stage=1,.binding=14,.offset=344,.size_query=1,.texel_fetch_bits=0,.cpu_view=0,.separate_sampler=1,.sampler_offset=1384},{.name="fs_tex2_180",.stage=1,.binding=14,.offset=352,.size_query=1,.texel_fetch_bits=0,.cpu_view=0,.separate_sampler=1,.sampler_offset=1384},{.name="fs_tex2_188",.stage=1,.binding=14,.offset=360,.size_query=1,.texel_fetch_bits=0,.cpu_view=0,.separate_sampler=1,.sampler_offset=1384},{.name="fs_tex2_190",.stage=1,.binding=14,.offset=368,.size_query=1,.texel_fetch_bits=0,.cpu_view=0,.separate_sampler=1,.sampler_offset=1384},{.name="fs_tex2_198",.stage=1,.binding=14,.offset=376,.size_query=1,.texel_fetch_bits=0,.cpu_view=0,.separate_sampler=1,.sampler_offset=1384},{.name="fs_tex2_1a0",.stage=1,.binding=14,.offset=384,.size_query=1,.texel_fetch_bits=0,.cpu_view=0,.separate_sampler=1,.sampler_offset=1384}},
  .attributes={{.name="in_attr0",.location=0,.kind=0}}},
 /* 2 cap9-1 vertex */
 {.sha256="e51acd87e13b03c147442a9f59c68966",.error=NULL,.control_size=2176,.code_size=768,.sampler_count=0,.attribute_count=2,.color_write_mask=15,.requires_fragment_highp=0,.requires_fragment_mediump=1,.wave_contract=0,
  .uniform_count=3,.uniforms={{.name="vs_c3",.stage=0,.binding=0,.offset=0,.size=65536,.components=4,.word_boolean=0,.word_int=0,.word_array=4096},{.name="vs_c4_1",.stage=0,.binding=1,.offset=0,.size=65536,.components=4,.word_boolean=0,.word_int=0,.word_array=4096},{.name="vs_c5_1",.stage=0,.binding=2,.offset=0,.size=65536,.components=4,.word_boolean=0,.word_int=0,.word_array=4096}},
  .varying_read_masks={7,3,0,0,0,0,0,0},.varying_write_masks={3,0,0,0,0,0,0,0},
  .code_hash={0xe5,0x1a,0xcd,0x87,0xe1,0x3b,0x03,0xc1,0x47,0x44,0x2a,0x9f,0x59,0xc6,0x89,0x66,0xa9,0x2c,0x95,0x63,0x9f,0xad,0x0b,0xe3,0xea,0x5f,0x50,0x8f,0xe9,0x3a,0x3e,0x7c},.control_hash={0x17,0x77,0x27,0x7a,0x31,0x38,0x61,0xf8,0xf5,0x89,0x7b,0x94,0x0e,0xf8,0xec,0x53,0xf8,0x35,0xd9,0x7d,0x26,0x2b,0x05,0x4c,0x02,0x62,0x0a,0x8c,0x0b,0x30,0xa9,0x1b},
  .samplers={},
  .attributes={{.name="in_attr0",.location=0,.kind=0},{.name="in_attr1",.location=1,.kind=0}}},
 /* 3 cap9-2 fragment */
 {.sha256="f93985ea0e0e4aec57139a378f98d7b7",.error=NULL,.control_size=2176,.code_size=2560,.sampler_count=3,.attribute_count=1,.color_write_mask=15,.requires_fragment_highp=0,.requires_fragment_mediump=1,.wave_contract=0,
  .uniform_count=1,.uniforms={{.name="fs_c3_1",.stage=1,.binding=0,.offset=0,.size=65536,.components=4,.word_boolean=0,.word_int=0,.word_array=4096}},
  .varying_read_masks={3,0,0,0,0,0,0,0},.varying_write_masks={7,0,0,0,0,0,0,0},
  .code_hash={0xf9,0x39,0x85,0xea,0x0e,0x0e,0x4a,0xec,0x57,0x13,0x9a,0x37,0x8f,0x98,0xd7,0xb7,0x44,0x9d,0xcf,0xb9,0x7d,0x72,0xf1,0x8f,0xfd,0xe2,0x47,0xf3,0xb9,0x75,0xaf,0xe7},.control_hash={0x01,0xdf,0x00,0xac,0xfa,0x97,0xea,0x13,0x88,0x47,0xb1,0x60,0x6f,0x75,0xef,0x03,0x60,0x97,0x58,0x85,0x10,0x53,0xde,0xe8,0xb1,0x0a,0xb6,0xa0,0xb7,0x42,0xe0,0x31},
  .samplers={{.name="fs_tex2_168",.stage=1,.binding=14,.offset=328,.size_query=1,.texel_fetch_bits=0,.cpu_view=0,.separate_sampler=1,.sampler_offset=1384},{.name="fs_tex2_170",.stage=1,.binding=14,.offset=336,.size_query=1,.texel_fetch_bits=0,.cpu_view=0,.separate_sampler=1,.sampler_offset=1384},{.name="fs_tex2_178",.stage=1,.binding=14,.offset=344,.size_query=1,.texel_fetch_bits=0,.cpu_view=0,.separate_sampler=1,.sampler_offset=1384}},
  .attributes={{.name="in_attr0",.location=0,.kind=0}}},
 /* 4 main-acb200 vertex */
 {.sha256="b3913c5cf5773c0e3a8d943f162b9d6d",.error=NULL,.control_size=2176,.code_size=256,.sampler_count=0,.attribute_count=2,.color_write_mask=15,.requires_fragment_highp=0,.requires_fragment_mediump=1,.wave_contract=0,
  .uniform_count=0,.uniforms={},
  .varying_read_masks={3,3,0,0,0,0,0,0},.varying_write_masks={3,0,0,0,0,0,0,0},
  .code_hash={0xb3,0x91,0x3c,0x5c,0xf5,0x77,0x3c,0x0e,0x3a,0x8d,0x94,0x3f,0x16,0x2b,0x9d,0x6d,0x94,0x29,0x7f,0x26,0x94,0x02,0x69,0x03,0x06,0x57,0x51,0x86,0xa5,0x6e,0x88,0xff},.control_hash={0xc1,0xe1,0xe8,0xff,0xdd,0x84,0x58,0xcc,0xec,0x23,0x5f,0x69,0x13,0xdf,0xf7,0x71,0xd8,0x4e,0x0b,0xd8,0x0c,0xe5,0x2a,0xb6,0xea,0x5f,0x0e,0x75,0x88,0x9f,0x59,0xba},
  .samplers={},
  .attributes={{.name="in_attr0",.location=0,.kind=0},{.name="in_attr1",.location=1,.kind=0}}},
 /* 5 main-acbdc0 vertex */
 {.sha256="3112cc6fd2f25e049173a91b15463d9b",.error=NULL,.control_size=2176,.code_size=512,.sampler_count=0,.attribute_count=2,.color_write_mask=15,.requires_fragment_highp=0,.requires_fragment_mediump=1,.wave_contract=0,
  .uniform_count=0,.uniforms={},
  .varying_read_masks={3,3,0,0,0,0,0,0},.varying_write_masks={3,1,0,0,0,0,0,0},
  .code_hash={0x31,0x12,0xcc,0x6f,0xd2,0xf2,0x5e,0x04,0x91,0x73,0xa9,0x1b,0x15,0x46,0x3d,0x9b,0x4b,0xc1,0xd7,0xa3,0x10,0x64,0x97,0x9d,0xd0,0x78,0xb3,0x35,0xe4,0xff,0x2e,0x06},.control_hash={0x3b,0x50,0x30,0x89,0x7a,0xe2,0xef,0x85,0xfc,0xba,0x13,0x9a,0x6e,0xc8,0xac,0x51,0x92,0x9b,0x74,0xe8,0xe7,0x0d,0xa7,0x3d,0xbf,0x28,0xb0,0xbe,0x9f,0x45,0xe0,0x18},
  .samplers={},
  .attributes={{.name="in_attr0",.location=0,.kind=0},{.name="in_attr1",.location=1,.kind=0}}},
 /* 6 main-acca80 fragment */
 {.sha256="5db5e878fff0c50d7d0e42c8f791a927",.error=NULL,.control_size=2176,.code_size=512,.sampler_count=1,.attribute_count=1,.color_write_mask=15,.requires_fragment_highp=0,.requires_fragment_mediump=1,.wave_contract=0,
  .uniform_count=0,.uniforms={},
  .varying_read_masks={3,0,0,0,0,0,0,0},.varying_write_masks={15,0,0,0,0,0,0,0},
  .code_hash={0x5d,0xb5,0xe8,0x78,0xff,0xf0,0xc5,0x0d,0x7d,0x0e,0x42,0xc8,0xf7,0x91,0xa9,0x27,0xf1,0x9d,0xb6,0xd4,0x02,0x5f,0xf3,0xce,0xc8,0x28,0xe7,0x5d,0x9d,0x7e,0xa1,0x72},.control_hash={0xff,0x49,0xaa,0x72,0x67,0xfd,0xcc,0xcf,0x14,0x16,0x6d,0xaf,0xfe,0x08,0x38,0x11,0xfc,0xee,0x76,0x98,0xf1,0x26,0xd6,0x8f,0xba,0x17,0x3f,0x20,0xa3,0xd2,0x71,0x2a},
  .samplers={{.name="fs_tex2_168",.stage=1,.binding=14,.offset=328,.size_query=1,.texel_fetch_bits=0,.cpu_view=0,.separate_sampler=1,.sampler_offset=1384}},
  .attributes={{.name="in_attr0",.location=0,.kind=0}}},
 /* 7 main-acd740 fragment */
 {.sha256="43094543406aad3d77e20928b483ce7b",.error=NULL,.control_size=2176,.code_size=768,.sampler_count=1,.attribute_count=1,.color_write_mask=15,.requires_fragment_highp=0,.requires_fragment_mediump=1,.wave_contract=0,
  .uniform_count=0,.uniforms={},
  .varying_read_masks={3,0,0,0,0,0,0,0},.varying_write_masks={15,0,0,0,0,0,0,0},
  .code_hash={0x43,0x09,0x45,0x43,0x40,0x6a,0xad,0x3d,0x77,0xe2,0x09,0x28,0xb4,0x83,0xce,0x7b,0x4b,0x07,0xfc,0x3e,0x79,0x7c,0x76,0xaf,0x8d,0xb8,0xf6,0xe3,0x53,0x7a,0x76,0xcb},.control_hash={0x7e,0xe6,0x2b,0xc0,0xa7,0x51,0x77,0xa2,0x56,0x65,0x1f,0x8d,0x67,0x5d,0x99,0x79,0x1f,0x29,0xc0,0xc0,0xf1,0x15,0xc9,0xa6,0xaf,0x9a,0xf1,0x52,0x45,0x72,0xd4,0xbd},
  .samplers={{.name="fs_tex2_168",.stage=1,.binding=14,.offset=328,.size_query=1,.texel_fetch_bits=0,.cpu_view=0,.separate_sampler=1,.sampler_offset=1384}},
  .attributes={{.name="in_attr0",.location=0,.kind=0}}},
 /* 8 main-ace500 fragment */
 {.sha256="f6ef73e109cd6f75f4d604acef5971ab",.error=NULL,.control_size=2176,.code_size=1024,.sampler_count=1,.attribute_count=1,.color_write_mask=15,.requires_fragment_highp=0,.requires_fragment_mediump=1,.wave_contract=0,
  .uniform_count=0,.uniforms={},
  .varying_read_masks={3,0,0,0,0,0,0,0},.varying_write_masks={15,0,0,0,0,0,0,0},
  .code_hash={0xf6,0xef,0x73,0xe1,0x09,0xcd,0x6f,0x75,0xf4,0xd6,0x04,0xac,0xef,0x59,0x71,0xab,0x8a,0x7e,0x4f,0xb1,0xb2,0x6c,0x3e,0xe7,0x3f,0xf3,0xea,0x65,0x8c,0xe7,0xb0,0x9a},.control_hash={0x85,0xe3,0x08,0x2e,0xeb,0xfb,0x9e,0x01,0xbb,0x67,0x2a,0xd8,0xa6,0x04,0x77,0x46,0xee,0x65,0xc2,0xf0,0x1c,0x82,0x19,0x00,0xe1,0x7a,0x45,0x70,0x66,0x58,0x48,0xd9},
  .samplers={{.name="fs_tex2_168",.stage=1,.binding=14,.offset=328,.size_query=1,.texel_fetch_bits=0,.cpu_view=0,.separate_sampler=1,.sampler_offset=1384}},
  .attributes={{.name="in_attr0",.location=0,.kind=0}}},
 /* 9 main-acf3c0 fragment */
 {.sha256="ad89da88c92d3503e04db0d2acf09608",.error=NULL,.control_size=2176,.code_size=512,.sampler_count=2,.attribute_count=1,.color_write_mask=15,.requires_fragment_highp=0,.requires_fragment_mediump=1,.wave_contract=0,
  .uniform_count=0,.uniforms={},
  .varying_read_masks={3,0,0,0,0,0,0,0},.varying_write_masks={15,0,0,0,0,0,0,0},
  .code_hash={0xad,0x89,0xda,0x88,0xc9,0x2d,0x35,0x03,0xe0,0x4d,0xb0,0xd2,0xac,0xf0,0x96,0x08,0x46,0x6b,0xb7,0xea,0x25,0xa1,0x15,0x31,0x0f,0xf3,0x20,0x07,0xf9,0xed,0xf5,0x90},.control_hash={0x88,0xd4,0x66,0x55,0xc2,0xef,0x36,0x0d,0x2a,0x5b,0x8b,0xc6,0x80,0x49,0x29,0x78,0x7f,0xfa,0x05,0xda,0xc7,0x34,0x04,0xde,0xec,0xeb,0x63,0x5f,0x4e,0xa8,0xe7,0x4d},
  .samplers={{.name="fs_tex2_168",.stage=1,.binding=14,.offset=328,.size_query=1,.texel_fetch_bits=0,.cpu_view=0,.separate_sampler=1,.sampler_offset=1384},{.name="fs_tex2_168_1",.stage=1,.binding=14,.offset=328,.size_query=1,.texel_fetch_bits=0,.cpu_view=0,.separate_sampler=1,.sampler_offset=1384}},
  .attributes={{.name="in_attr0",.location=0,.kind=0}}},
 /* 10 main-ad0080 fragment */
 {.sha256="68d5e0b3aa2dd6d368902eec137e75d0",.error=NULL,.control_size=2176,.code_size=768,.sampler_count=2,.attribute_count=1,.color_write_mask=15,.requires_fragment_highp=0,.requires_fragment_mediump=1,.wave_contract=0,
  .uniform_count=0,.uniforms={},
  .varying_read_masks={3,0,0,0,0,0,0,0},.varying_write_masks={15,0,0,0,0,0,0,0},
  .code_hash={0x68,0xd5,0xe0,0xb3,0xaa,0x2d,0xd6,0xd3,0x68,0x90,0x2e,0xec,0x13,0x7e,0x75,0xd0,0xb9,0xaf,0x20,0x10,0xf9,0xe5,0x0e,0x39,0xdd,0x5a,0x4e,0xe1,0xd1,0x00,0x35,0xc1},.control_hash={0xee,0xf7,0xd0,0x9e,0xaa,0x67,0x97,0xc4,0xcc,0x4a,0x54,0x5a,0x95,0x8d,0x4a,0x26,0x0d,0xf3,0xea,0x66,0x43,0xa8,0xf3,0xa5,0xcf,0xa4,0x9d,0xf3,0xc6,0xa8,0xd9,0x07},
  .samplers={{.name="fs_tex2_168",.stage=1,.binding=14,.offset=328,.size_query=1,.texel_fetch_bits=0,.cpu_view=0,.separate_sampler=1,.sampler_offset=1384},{.name="fs_tex2_168_1",.stage=1,.binding=14,.offset=328,.size_query=1,.texel_fetch_bits=0,.cpu_view=0,.separate_sampler=1,.sampler_offset=1384}},
  .attributes={{.name="in_attr0",.location=0,.kind=0}}},
 /* 11 main-ad0e40 fragment */
 {.sha256="b8fa73d443d804f147920d78d6b488fa",.error=NULL,.control_size=2176,.code_size=1024,.sampler_count=2,.attribute_count=1,.color_write_mask=15,.requires_fragment_highp=0,.requires_fragment_mediump=1,.wave_contract=0,
  .uniform_count=0,.uniforms={},
  .varying_read_masks={3,0,0,0,0,0,0,0},.varying_write_masks={15,0,0,0,0,0,0,0},
  .code_hash={0xb8,0xfa,0x73,0xd4,0x43,0xd8,0x04,0xf1,0x47,0x92,0x0d,0x78,0xd6,0xb4,0x88,0xfa,0x44,0x47,0x6e,0xe0,0xf3,0x06,0xc4,0xc4,0xbf,0x5e,0x85,0xfb,0x96,0x19,0x5c,0xd4},.control_hash={0xcd,0x11,0x36,0xb6,0xd1,0xc7,0xe3,0x5e,0xb1,0x35,0xa7,0x8e,0x21,0x80,0x0c,0x47,0x56,0x0e,0x8d,0xfe,0x10,0x5a,0x0e,0x46,0x84,0x4d,0x86,0xc5,0xe1,0x5d,0x58,0x50},
  .samplers={{.name="fs_tex2_168",.stage=1,.binding=14,.offset=328,.size_query=1,.texel_fetch_bits=0,.cpu_view=0,.separate_sampler=1,.sampler_offset=1384},{.name="fs_tex2_168_1",.stage=1,.binding=14,.offset=328,.size_query=1,.texel_fetch_bits=0,.cpu_view=0,.separate_sampler=1,.sampler_offset=1384}},
  .attributes={{.name="in_attr0",.location=0,.kind=0}}},
 /* 12 main-ad1d00 vertex */
 {.sha256="b3913c5cf5773c0e3a8d943f162b9d6d",.error=NULL,.control_size=2176,.code_size=256,.sampler_count=0,.attribute_count=2,.color_write_mask=15,.requires_fragment_highp=0,.requires_fragment_mediump=1,.wave_contract=0,
  .uniform_count=0,.uniforms={},
  .varying_read_masks={3,3,0,0,0,0,0,0},.varying_write_masks={3,0,0,0,0,0,0,0},
  .code_hash={0xb3,0x91,0x3c,0x5c,0xf5,0x77,0x3c,0x0e,0x3a,0x8d,0x94,0x3f,0x16,0x2b,0x9d,0x6d,0x94,0x29,0x7f,0x26,0x94,0x02,0x69,0x03,0x06,0x57,0x51,0x86,0xa5,0x6e,0x88,0xff},.control_hash={0xc1,0x24,0xa5,0x82,0xbf,0x1b,0xba,0xb9,0x4f,0x5d,0xaf,0x2e,0xa8,0x3d,0xbc,0x01,0x62,0x8f,0x87,0x5a,0xb3,0xbf,0xab,0x61,0xdd,0x01,0x89,0x14,0xd1,0x27,0x93,0x28},
  .samplers={},
  .attributes={{.name="in_attr0",.location=0,.kind=0},{.name="in_attr1",.location=1,.kind=0}}},
 /* 13 main-ad28c0 fragment */
 {.sha256="4a6099f2435b06bcfa9ae3687b720950",.error=NULL,.control_size=2176,.code_size=256,.sampler_count=1,.attribute_count=1,.color_write_mask=15,.requires_fragment_highp=0,.requires_fragment_mediump=1,.wave_contract=0,
  .uniform_count=0,.uniforms={},
  .varying_read_masks={3,0,0,0,0,0,0,0},.varying_write_masks={15,0,0,0,0,0,0,0},
  .code_hash={0x4a,0x60,0x99,0xf2,0x43,0x5b,0x06,0xbc,0xfa,0x9a,0xe3,0x68,0x7b,0x72,0x09,0x50,0x01,0xf1,0x2d,0xfe,0x0c,0x6a,0x09,0xe1,0xd0,0x29,0xfa,0x4a,0x70,0xa8,0xe4,0xa7},.control_hash={0x15,0x8d,0x95,0x18,0x20,0x68,0x10,0x39,0x46,0xfe,0x39,0xe0,0xf2,0x06,0x81,0x6c,0x87,0x90,0xa7,0xb7,0x75,0xbc,0x96,0x16,0xbe,0x10,0x51,0xd3,0xb9,0xb1,0xa0,0xc8},
  .samplers={{.name="fs_tex2_168",.stage=1,.binding=14,.offset=328,.size_query=1,.texel_fetch_bits=0,.cpu_view=0,.separate_sampler=1,.sampler_offset=1384}},
  .attributes={{.name="in_attr0",.location=0,.kind=0}}},
 /* 14 new vertex */
 {.sha256="ef070e6fa1a82193ade305c971f7c5a3",.error=NULL,.control_size=2176,.code_size=768,.sampler_count=0,.attribute_count=2,.color_write_mask=15,.requires_fragment_highp=0,.requires_fragment_mediump=1,.wave_contract=0,
  .uniform_count=2,.uniforms={{.name="vs_c4",.stage=0,.binding=1,.offset=0,.size=65536,.components=4,.word_boolean=0,.word_int=0,.word_array=4096},{.name="vs_c5_1",.stage=0,.binding=2,.offset=0,.size=65536,.components=4,.word_boolean=0,.word_int=0,.word_array=4096}},
  .varying_read_masks={7,3,0,0,0,0,0,0},.varying_write_masks={3,0,0,0,0,0,0,0},
  .code_hash={0xef,0x07,0x0e,0x6f,0xa1,0xa8,0x21,0x93,0xad,0xe3,0x05,0xc9,0x71,0xf7,0xc5,0xa3,0x21,0xe6,0x00,0xcb,0x48,0xd0,0x83,0xd5,0xfa,0xf3,0x5d,0x35,0x75,0x90,0xe9,0xd5},.control_hash={0x54,0x2a,0xee,0x39,0x50,0xab,0x2c,0x39,0x48,0x52,0x92,0x40,0x04,0x89,0x0a,0xcf,0xfd,0xd3,0xb3,0x99,0xd6,0x58,0x43,0x81,0xfb,0x98,0xdc,0xad,0xe7,0xc8,0x43,0xf3},
  .samplers={},
  .attributes={{.name="in_attr0",.location=0,.kind=0},{.name="in_attr1",.location=1,.kind=0}}},
};
