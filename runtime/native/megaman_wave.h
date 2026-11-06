/* LC2 VS17/FS16 CPU scanline-factor contract. Pure arithmetic: no GL, loader,
 * global state, allocations, or guest pointers. See docs/megaman-shader-wave.md.
 * The caller verifies original code identities, buffer bounds, four referenced
 * vertices, actual GL viewport/orientation, sample count and resource lifetime.
 * A non-OK status invalidates all output bytes; never submit a partial LUT.
 */
#ifndef MEGAMAN_WAVE_H
#define MEGAMAN_WAVE_H
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define MEGAMAN_WAVE_MAX_HEIGHT 720U
#define MEGAMAN_WAVE_MAX_LUT 1024U
/* Legacy one-column generator above retains its tested 720-row contract.
 * The separately validated two-edge backend uses this explicit layout. */
#define MEGAMAN_WAVE_FOLD_MAX_HEIGHT 1080U
#define MEGAMAN_WAVE_FOLD_WIDTH 2U
#define MEGAMAN_WAVE_FOLD_ROWS 1024U
#define MEGAMAN_WAVE_FOLD_BYTES (MEGAMAN_WAVE_FOLD_WIDTH*MEGAMAN_WAVE_FOLD_ROWS)
enum megaman_wave_status {
    MEGAMAN_WAVE_OK=0,
    MEGAMAN_WAVE_BAD_ARGUMENT,
    MEGAMAN_WAVE_TARGET_HEIGHT,
    MEGAMAN_WAVE_NONFINITE,
    MEGAMAN_WAVE_DENOMINATOR,
    MEGAMAN_WAVE_PERSPECTIVE,
    MEGAMAN_WAVE_QUAD_MAPPING,
    MEGAMAN_WAVE_OUTPUT_CAPACITY,
    MEGAMAN_WAVE_CHROMA_RANGE,
    MEGAMAN_WAVE_FOLD_GEOMETRY
};

struct megaman_wave_mapping {
    double uv_y_at_zero,uv_y_per_pixel; /* absolute GL framebuffer coordinates */
};
struct megaman_wave_quad {
    float position_xy[4][2]; /* fetched signed-byte attr0 converted exactly */
    float texcoord_xy[4][2]; /* fetched unsigned-byte attr1 converted exactly */
    float matrix_rows[4][4]; /* VS17 binding0 offsets0,16,32,48 */
    float tex_crop[4];       /* VS17 binding0 offset64 */
    float viewport[4];       /* actual GL x,y,width,height, after host adaptation */
};
struct megaman_wave_input {
    unsigned target_height,source_width,source_height;
    struct megaman_wave_mapping mapping;
    float varying1[4],varying2[4]; /* effective VS17 buf0[80],buf0[96] */
};
struct megaman_wave_output {
    unsigned texture_height;
    float inverse_rows,chroma_offset;
};

/* Force a separately rounded float32 operation, independent of contraction
 * settings used by the including runtime. CPU sine rounds once with sinf. */
static inline float megaman_wave_add(float a,float b){volatile float v=a+b;return v;}
static inline float megaman_wave_sub(float a,float b){volatile float v=a-b;return v;}
static inline float megaman_wave_mul(float a,float b){volatile float v=a*b;return v;}
static inline float megaman_wave_div(float a,float b){volatile float v=a/b;return v;}

static inline enum megaman_wave_status megaman_wave_map_quad(
        const struct megaman_wave_quad *q,struct megaman_wave_mapping *out) {
    if(!q||!out)return MEGAMAN_WAVE_BAD_ARGUMENT;
    for(unsigned i=0;i<4;i++){
        for(unsigned j=0;j<4;j++)if(!isfinite(q->matrix_rows[i][j]))return MEGAMAN_WAVE_NONFINITE;
        for(unsigned j=0;j<2;j++)if(!isfinite(q->position_xy[i][j])||!isfinite(q->texcoord_xy[i][j]))
            return MEGAMAN_WAVE_NONFINITE;
        if(!isfinite(q->tex_crop[i])||!isfinite(q->viewport[i]))return MEGAMAN_WAVE_NONFINITE;
    }
    if(q->viewport[2]<=0||q->viewport[3]<=0)return MEGAMAN_WAVE_QUAD_MAPPING;
    if(q->matrix_rows[3][0]!=0||q->matrix_rows[3][1]!=0||q->matrix_rows[3][3]<=0)
        return MEGAMAN_WAVE_PERSPECTIVE;
    if(q->matrix_rows[0][1]!=0||q->matrix_rows[1][0]!=0||
       q->matrix_rows[0][0]==0||q->matrix_rows[1][1]==0)return MEGAMAN_WAVE_QUAD_MAPPING;
    double x[4],y[4];float uv[4];
    for(unsigned i=0;i<4;i++){
        float cx=megaman_wave_add(megaman_wave_mul(q->position_xy[i][0],q->matrix_rows[0][0]),q->matrix_rows[0][3]);
        float cy=megaman_wave_add(megaman_wave_mul(q->position_xy[i][1],q->matrix_rows[1][1]),q->matrix_rows[1][3]);
        /* z=0,w=1 are the original VS17 constants. Same positive clip w
         * guarantees affine interpolation; no extrapolated perspective model. */
        x[i]=q->viewport[0]+((double)cx/q->matrix_rows[3][3]+1.0)*q->viewport[2]*0.5;
        y[i]=q->viewport[1]+((double)cy/q->matrix_rows[3][3]+1.0)*q->viewport[3]*0.5;
        uv[i]=megaman_wave_add(megaman_wave_mul(q->texcoord_xy[i][1],q->tex_crop[1]),q->tex_crop[3]);
        if(!isfinite(x[i])||!isfinite(y[i])||!isfinite(uv[i]))return MEGAMAN_WAVE_NONFINITE;
    }
    double minx=x[0],maxx=x[0],miny=y[0],maxy=y[0];
    for(unsigned i=1;i<4;i++){
        if(x[i]<minx)minx=x[i];
        if(x[i]>maxx)maxx=x[i];
        if(y[i]<miny)miny=y[i];
        if(y[i]>maxy)maxy=y[i];
    }
    if(minx==maxx||miny==maxy)return MEGAMAN_WAVE_QUAD_MAPPING;
    unsigned seen=0,uv_seen=0;float row_uv[2]={0,0};
    for(unsigned i=0;i<4;i++){
        if((x[i]!=minx&&x[i]!=maxx)||(y[i]!=miny&&y[i]!=maxy))return MEGAMAN_WAVE_QUAD_MAPPING;
        unsigned ix=x[i]==maxx,iy=y[i]==maxy,bit=1U<<(ix+2*iy);
        if(seen&bit)return MEGAMAN_WAVE_QUAD_MAPPING;
        seen|=bit;
        if((uv_seen&(1U<<iy))&&uv[i]!=row_uv[iy])return MEGAMAN_WAVE_QUAD_MAPPING;
        row_uv[iy]=uv[i];uv_seen|=1U<<iy;
    }
    if(seen!=15)return MEGAMAN_WAVE_QUAD_MAPPING;
    out->uv_y_per_pixel=((double)row_uv[1]-row_uv[0])/(maxy-miny);
    out->uv_y_at_zero=row_uv[0]-miny*out->uv_y_per_pixel;
    if(!isfinite(out->uv_y_at_zero)||!isfinite(out->uv_y_per_pixel))return MEGAMAN_WAVE_NONFINITE;
    return MEGAMAN_WAVE_OK;
}

static inline enum megaman_wave_status megaman_wave_factor(
        float uv_y,float source_height,const float a[4],const float b[4],float *factor) {
    if(!a||!b||!factor)return MEGAMAN_WAVE_BAD_ARGUMENT;
    if(!isfinite(uv_y)||!isfinite(source_height))return MEGAMAN_WAVE_NONFINITE;
    for(unsigned i=0;i<4;i++)if(!isfinite(a[i])||!isfinite(b[i]))return MEGAMAN_WAVE_NONFINITE;
    if(source_height<=0||b[1]==0||b[3]==0)return MEGAMAN_WAVE_DENOMINATOR;
    float ratio=megaman_wave_mul(megaman_wave_div(a[0],b[3]),0.5f);
    if(!isfinite(ratio))return MEGAMAN_WAVE_NONFINITE;
    float fraction=megaman_wave_sub(ratio,floorf(ratio));
    float shift=fraction<0.5f?megaman_wave_div(b[2],source_height):0.0f;
    float pixels=megaman_wave_mul(megaman_wave_sub(uv_y,shift),source_height);
    float phase=megaman_wave_mul(megaman_wave_div(pixels,b[1]),6.28318548f);
    if(!isfinite(phase))return MEGAMAN_WAVE_NONFINITE;
    float result=megaman_wave_add(megaman_wave_mul(sinf(phase),b[0]),1.0f);
    if(!isfinite(result))return MEGAMAN_WAVE_NONFINITE;
    *factor=result<0?0:result>1?1:result;
    return MEGAMAN_WAVE_OK;
}

static inline enum megaman_wave_status megaman_wave_generate(
        const struct megaman_wave_input *in,struct megaman_wave_output *out,
        uint8_t *r8,size_t capacity) {
    if(!in||!out||!r8)return MEGAMAN_WAVE_BAD_ARGUMENT;
    if(!in->target_height||in->target_height>MEGAMAN_WAVE_MAX_HEIGHT)return MEGAMAN_WAVE_TARGET_HEIGHT;
    if(!in->source_width||!in->source_height)return MEGAMAN_WAVE_BAD_ARGUMENT;
    if(!isfinite(in->mapping.uv_y_at_zero)||!isfinite(in->mapping.uv_y_per_pixel))return MEGAMAN_WAVE_NONFINITE;
    unsigned padded=1;while(padded<in->target_height)padded*=2;
    if(capacity<padded)return MEGAMAN_WAVE_OUTPUT_CAPACITY;
    float chroma=megaman_wave_mul(megaman_wave_div(in->varying1[1],(float)in->source_width),0.5f);
    if(!isfinite(chroma)||fabsf(chroma)>65504.0f)return MEGAMAN_WAVE_CHROMA_RANGE;
    for(unsigned y=0;y<in->target_height;y++){
        /* Evaluate the verified affine mapping in double, round interpolated
         * UV once to float32, then use the original fragment arithmetic order. */
        float uv=(float)(in->mapping.uv_y_at_zero+(y+0.5)*in->mapping.uv_y_per_pixel),factor=0;
        enum megaman_wave_status status=megaman_wave_factor(uv,(float)in->source_height,in->varying1,in->varying2,&factor);
        if(status!=MEGAMAN_WAVE_OK)return status;
        r8[y]=(uint8_t)floor((double)factor*255.0+0.5);
    }
    for(unsigned y=in->target_height;y<padded;y++)r8[y]=r8[in->target_height-1];
    out->texture_height=padded;out->inverse_rows=1.0f/(float)padded;out->chroma_offset=chroma;
    return MEGAMAN_WAVE_OK;
}

/* Both distances must reach the fragment independently from vertex highp.
 * Integral, in-target endpoints make their mediump values exact after /1024.
 * This deliberately rejects the fractional [0,1025.5] counterexample rather
 * than rounding guest geometry. q already contains the effective clip Y sign. */
static inline enum megaman_wave_status megaman_wave_map_quad_fold(
        const struct megaman_wave_quad *q,unsigned target_height,struct megaman_wave_mapping *out) {
    if(!q||!out)return MEGAMAN_WAVE_BAD_ARGUMENT;
    if(!target_height||target_height>MEGAMAN_WAVE_FOLD_MAX_HEIGHT)return MEGAMAN_WAVE_TARGET_HEIGHT;
    struct megaman_wave_mapping candidate;
    enum megaman_wave_status status=megaman_wave_map_quad(q,&candidate);
    if(status!=MEGAMAN_WAVE_OK)return status;
    for(unsigned i=0;i<4;i++) {
        float cy=megaman_wave_add(megaman_wave_mul(q->position_xy[i][1],q->matrix_rows[1][1]),q->matrix_rows[1][3]);
        double row=q->viewport[1]+((double)cy/q->matrix_rows[3][3]+1.0)*q->viewport[3]*0.5;
        if(row<0||row>target_height||floor(row)!=row)return MEGAMAN_WAVE_FOLD_GEOMETRY;
    }
    *out=candidate;return MEGAMAN_WAVE_OK;
}

static inline enum megaman_wave_status megaman_wave_generate_fold(
        const struct megaman_wave_input *in,struct megaman_wave_output *out,
        uint8_t *r8,size_t capacity) {
    if(!in||!out||!r8)return MEGAMAN_WAVE_BAD_ARGUMENT;
    if(!in->target_height||in->target_height>MEGAMAN_WAVE_FOLD_MAX_HEIGHT)return MEGAMAN_WAVE_TARGET_HEIGHT;
    if(!in->source_width||!in->source_height)return MEGAMAN_WAVE_BAD_ARGUMENT;
    if(!isfinite(in->mapping.uv_y_at_zero)||!isfinite(in->mapping.uv_y_per_pixel))return MEGAMAN_WAVE_NONFINITE;
    if(capacity<MEGAMAN_WAVE_FOLD_BYTES)return MEGAMAN_WAVE_OUTPUT_CAPACITY;
    float chroma=megaman_wave_mul(megaman_wave_div(in->varying1[1],(float)in->source_width),0.5f);
    if(!isfinite(chroma)||fabsf(chroma)>65504.0f)return MEGAMAN_WAVE_CHROMA_RANGE;
    uint8_t rows[MEGAMAN_WAVE_FOLD_MAX_HEIGHT];
    for(unsigned y=0;y<in->target_height;y++) {
        float uv=(float)(in->mapping.uv_y_at_zero+(y+0.5)*in->mapping.uv_y_per_pixel),factor=0;
        enum megaman_wave_status status=megaman_wave_factor(uv,(float)in->source_height,in->varying1,in->varying2,&factor);
        if(status!=MEGAMAN_WAVE_OK)return status;
        rows[y]=(uint8_t)floor((double)factor*255.0+0.5);
    }
    for(unsigned y=0;y<MEGAMAN_WAVE_FOLD_ROWS;y++) {
        unsigned row=y<in->target_height?y:in->target_height-1;
        r8[2*y]=rows[row];r8[2*y+1]=rows[in->target_height-1-row];
    }
    out->texture_height=MEGAMAN_WAVE_FOLD_ROWS;out->inverse_rows=1.0f/MEGAMAN_WAVE_FOLD_ROWS;out->chroma_offset=chroma;
    return MEGAMAN_WAVE_OK;
}
#endif
