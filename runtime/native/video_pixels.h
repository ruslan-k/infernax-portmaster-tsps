/* RGB evidence only: alpha and a live audio callback cannot prove an image. */
#ifndef COTM_VIDEO_PIXELS_H
#define COTM_VIDEO_PIXELS_H
#include <stdint.h>
#include <stddef.h>
/* `white` counts fully saturated pixels: the shapes still wrong on this port are solid white, and
 * counting them per presented frame finds the frame one appears in without a per-draw census. */
struct nvn_pixel_measure {unsigned nonblack,maximum,white;};
static inline unsigned nvn_pixel_observe(struct nvn_pixel_measure *result,const uint8_t *pixel) {
    unsigned maximum=pixel[0];if(pixel[1]>maximum)maximum=pixel[1];if(pixel[2]>maximum)maximum=pixel[2];
    if(maximum>result->maximum)result->maximum=maximum;
    if(pixel[0]>250&&pixel[1]>250&&pixel[2]>250)result->white++;
    unsigned nonblack=maximum>3;result->nonblack+=nonblack;return nonblack;
}
static struct nvn_pixel_measure nvn_measure_pixels(const uint8_t *pixels,size_t count) {
    struct nvn_pixel_measure result={0};
    for(size_t i=0;i<count;i++)nvn_pixel_observe(&result,pixels+i*4);
    return result;
}
static unsigned nvn_need_full_readback(unsigned capture,struct nvn_pixel_measure stripe) {
    /* A sparse negative is inconclusive, never BLACK. Read the complete real
     * backbuffer before classifying or rejecting a frame with no stripe proof. */
    return capture || stripe.nonblack<32;
}
#define NVN_PIXEL_BAND_ROWS 16U
/* Only coordinates survive a frame. No previous pixels, brightness or counts
 * may supply evidence for the current physical framebuffer. */
struct nvn_pixel_band {unsigned width,height,y,valid;};
static unsigned nvn_pixel_dimensions(unsigned width,unsigned height) {
    return width && height && width<=4096 && height<=4096;
}
static unsigned nvn_pixel_rows(unsigned height) {return height<NVN_PIXEL_BAND_ROWS?height:NVN_PIXEL_BAND_ROWS;}
static unsigned nvn_pixel_band_y(const struct nvn_pixel_band *band,unsigned width,unsigned height) {
    unsigned rows=nvn_pixel_rows(height);
    if(!nvn_pixel_dimensions(width,height))return 0;
    if(band->valid && band->width==width && band->height==height && band->y<=height-rows)return band->y;
    return (height-rows)/2;
}
static struct nvn_pixel_measure nvn_measure_full_pixels(const uint8_t *pixels,unsigned width,unsigned height,struct nvn_pixel_band *band) {
    struct nvn_pixel_measure result={0};unsigned row_counts[NVN_PIXEL_BAND_ROWS]={0},sum=0,best=0,best_y=0;
    if(!nvn_pixel_dimensions(width,height)){*band=(struct nvn_pixel_band){0};return result;}
    unsigned rows=nvn_pixel_rows(height);
    for(unsigned y=0;y<height;y++) {
        unsigned count=0;
        for(unsigned x=0;x<width;x++)count+=nvn_pixel_observe(&result,pixels+((size_t)y*width+x)*4);
        unsigned slot=y%rows;sum-=row_counts[slot];row_counts[slot]=count;sum+=count;
        if(y+1>=rows && sum>best){best=sum;best_y=y+1-rows;}
    }
    /* The first maximum wins ties in bottom-up GL coordinates. A dark/sparse
     * complete frame drops the hint; its classification still uses result. */
    *band=(struct nvn_pixel_band){width,height,best_y,best>=32};return result;
}
#endif
