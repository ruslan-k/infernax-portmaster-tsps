/* MOMO_TMP_SPRITE_RGB=1: in the TextMeshPro/Bitmap fragment, take the COLOUR from the atlas when the
 * atlas is a colour image, and from the face texture when it is a single-channel font page.
 *
 * Why: one program serves both cases here (MEDIDO run momo-234: a single control section resolves to that
 * catalogue entry), and the translated fragment always takes colour from the face texture - which the
 * game binds as Unity's 4x4 white. For a bitmap FONT that is right: the page is R8, it carries only
 * coverage, and the colour belongs to the vertex. For an inline SPRITE it is wrong: the atlas is RGBA8
 * and carries the picture, so using only its alpha paints the sprite's silhouette in flat white - which
 * is exactly the two white squares of the title (one of them is SaveAnimation-Sheet, the autosave icon;
 * MEDIDO runs momo-230/231).
 *
 * The test is the texture's own format, not the game: an R8 page cannot carry colour and an RGBA8 atlas
 * does. The choice is a uniform sent per draw, so one compiled program still serves both.
 * Opt-in until it is proven on the device. */
#ifndef MOMO_TMP_SPRITE_H
#define MOMO_TMP_SPRITE_H
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MOMO_TMP_SPRITE_UNIFORM "momo_tmp_sprite"

static unsigned momo_tmp_sprite_enabled(void) {
    static int configured;static unsigned enabled;
    if(!configured){const char *o=getenv("MOMO_TMP_SPRITE_RGB");enabled=o&&!strcmp(o,"1");configured=1;
        if(enabled)printf("MOMO_TMP_SPRITE_RGB enabled=1 colour=atlas-when-not-R8 scope=opt-in\n");}
    return enabled;
}
/* Rewrites the one place the generated fragment reads the face texture. */
static char *momo_tmp_sprite_source(const char *source) {
    static const char face[]="ms_sample_fs_tex2_170( vec2(ms_vary1.x, ms_vary1.y))";
    static const char atlas[]="ms_sample_fs_tex2_168( vec2(ms_vary3.x, ms_vary3.y))";
    static const char decl[]="uniform mediump float " MOMO_TMP_SPRITE_UNIFORM ";\n";
    const char *at=strstr(source,face);
    if(!at || !strstr(source,atlas) || strncmp(source,"#version 100\n",13))return NULL;
    size_t mixed=5+sizeof(face)-1+2+sizeof(atlas)-1+2+sizeof(MOMO_TMP_SPRITE_UNIFORM)-1+2;
    char *out=malloc(strlen(source)+sizeof(decl)+mixed+64);
    if(!out)return NULL;
    size_t head=(size_t)(at-source);
    size_t at_out=13;
    memcpy(out,source,13);
    memcpy(out+at_out,decl,sizeof(decl)-1);at_out+=sizeof(decl)-1;
    memcpy(out+at_out,source+13,head-13);at_out+=head-13;
    at_out+=(size_t)snprintf(out+at_out,mixed+64,"mix(%s,%s," MOMO_TMP_SPRITE_UNIFORM ")",face,atlas);
    strcpy(out+at_out,at+sizeof(face)-1);
    return out;
}

/* MOMO_TMP_ALPHA_PROBE=1 (diagnostic): write the ALPHA the fragment samples from the atlas straight into
 * the colour, opaque. White means the sample really is one - so the image or the coordinate is wrong;
 * black means the sample is zero and whatever paints the square happens after this shader. */
static unsigned momo_tmp_alpha_probe_enabled(void) {
    static int configured;static unsigned enabled;
    if(!configured){const char *o=getenv("MOMO_TMP_ALPHA_PROBE");enabled=o&&!strcmp(o,"1");configured=1;
        if(enabled)printf("MOMO_TMP_ALPHA_PROBE enabled=1 output=sampled-atlas-alpha scope=diagnostic\n");}
    return enabled;
}
static char *momo_tmp_alpha_probe_source(const char *source) {
    static const char line[]="    gl_FragColor.w = ms_sample_fs_tex2_168( vec2(ms_vary3.x, ms_vary3.y)).w * ms_vary0.w;";
    static const char probe[]="    float ms_probe = ms_sample_fs_tex2_168( vec2(ms_vary3.x, ms_vary3.y)).w;\n"
                              "    gl_FragColor = vec4(ms_probe, ms_probe, ms_probe, 1.0);";
    const char *at=strstr(source,line);
    if(!at)return NULL;
    size_t head=(size_t)(at-source),tail=strlen(at+sizeof(line)-1);
    char *out=malloc(head+sizeof(probe)-1+tail+1);
    if(!out)return NULL;
    memcpy(out,source,head);
    memcpy(out+head,probe,sizeof(probe)-1);
    memcpy(out+head+sizeof(probe)-1,at+sizeof(line)-1,tail+1);
    return out;
}

/* MOMO_TMP_PREMULTIPLY=1: multiply the TextMeshPro colour by the alpha it just computed.
 *
 * MEDIDO (run momo-239, proofs/tmp-sampled-alpha-is-zero-20260919.png): the alpha this fragment samples
 * for the two sprite draws is ZERO - the probe paints them black against a white screen - so the sprite
 * is transparent and correct. What paints the opaque square is therefore the COLOUR reaching the
 * framebuffer with alpha zero, which happens when the blend expects a premultiplied source and the
 * fragment hands it a straight one: result = src.rgb + dst*(1-0) = white over everything.
 *
 * Sprites/Default (fragment 1108) does premultiply in its translated form; this one does not.
 *
 * MEDIDO (run momo-240, proofs/title-complete-premultiplied-20260919.png): with the multiply in place
 * BOTH white squares disappear and `Press Any Button` reads in full for the first time, with every other
 * element of the title unchanged. ON by default; MOMO_TMP_PREMULTIPLY=0 goes back.
 *
 * The real fix belongs in the ESSL lowering, which should emit the premultiply for this shader shape;
 * doing it here matches on the generated text rather than on a catalogue index, so it survives a
 * regenerated catalogue and touches only fragments that carry exactly this line. */
static unsigned momo_tmp_premultiply_enabled(void) {
    static int configured;static unsigned enabled;
    if(!configured){const char *o=getenv("MOMO_TMP_PREMULTIPLY");enabled=!o||strcmp(o,"0");configured=1;
        if(enabled)printf("MOMO_TMP_PREMULTIPLY enabled=1 colour=times-alpha scope=opt-in\n");}
    return enabled;
}
static char *momo_tmp_premultiply_source(const char *source) {
    static const char line[]="    gl_FragColor.w = ms_sample_fs_tex2_168( vec2(ms_vary3.x, ms_vary3.y)).w * ms_vary0.w;";
    static const char fixed[]="    gl_FragColor.w = ms_sample_fs_tex2_168( vec2(ms_vary3.x, ms_vary3.y)).w * ms_vary0.w;\n"
                              "    gl_FragColor.xyz = gl_FragColor.xyz * gl_FragColor.w;";
    const char *at=strstr(source,line);
    if(!at)return NULL;
    size_t head=(size_t)(at-source),tail=strlen(at+sizeof(line)-1);
    char *out=malloc(head+sizeof(fixed)-1+tail+1);
    if(!out)return NULL;
    memcpy(out,source,head);
    memcpy(out+head,fixed,sizeof(fixed)-1);
    memcpy(out+head+sizeof(fixed)-1,at+sizeof(line)-1,tail+1);
    return out;
}
#endif
