/* HLE NVDEC movie decode: real H.264 via libavcodec, loaded with dlopen so the
 * port still starts on a device that ships no ffmpeg.
 *
 * The Switch SDK hands the decoder one Annex-B access unit per submit without
 * in-band SPS/PPS; those parameters live in the game's own MP4 (avcC) and are
 * carried here verbatim (Main profile, level 4.1). */
#ifndef NVDEC264_H
#define NVDEC264_H

#include <dlfcn.h>
#include <unistd.h>
#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>

static const uint8_t nvd_sps[] = {
    0x67, 0x4d, 0x40, 0x29, 0x95, 0x90, 0x07, 0x80, 0x22, 0x7e, 0x5c,
    0x04, 0x40, 0x00, 0x00, 0xfa, 0x00, 0x00, 0x3a, 0x98, 0x21};
static const uint8_t nvd_pps[] = {0x68, 0xeb, 0x8f, 0x20};

typedef const AVCodec *(*nvd_find_decoder)(enum AVCodecID);
typedef AVCodecContext *(*nvd_alloc_ctx)(const AVCodec *);
typedef int (*nvd_open2)(AVCodecContext *, const AVCodec *, AVDictionary **);
typedef AVFrame *(*nvd_alloc_frame)(void);
typedef AVPacket *(*nvd_alloc_pkt)(void);
typedef int (*nvd_new_packet)(AVPacket *, int);
typedef void (*nvd_free_pkt)(AVPacket **);
typedef int (*nvd_send)(AVCodecContext *, const AVPacket *);
typedef int (*nvd_receive)(AVCodecContext *, AVFrame *);

static struct {
    nvd_find_decoder find_decoder;
    nvd_alloc_ctx alloc_ctx;
    nvd_open2 open2;
    nvd_alloc_frame alloc_frame;
    nvd_alloc_pkt alloc_pkt;
    nvd_new_packet new_packet;
    nvd_free_pkt free_pkt;
    nvd_send send;
    nvd_receive receive;
} nvd_api;

static AVCodecContext *nvd_ctx;
static AVFrame *nvd_pic;
static int nvd_ready, nvd_decoded, nvd_w, nvd_h, nvd_send_rc, nvd_recv_rc;
static uint64_t nvd_serial;
static int nvd_seq_done;

static int nvd_symbols(void) {
    char path[512] = "libavcodec.so.58";
    char exe[400];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = 0;
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = 0;
            snprintf(path, sizeof(path), "%s/ff/libavcodec.so.58", exe);
        }
    }
    void *h = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if (!h) h = dlopen("libavcodec.so.58", RTLD_NOW | RTLD_GLOBAL);
    if (!h) h = dlopen("libavcodec.so", RTLD_NOW | RTLD_GLOBAL);
    if (!h) return 0;
    nvd_api.find_decoder = (nvd_find_decoder)dlsym(h, "avcodec_find_decoder");
    nvd_api.alloc_ctx    = (nvd_alloc_ctx)dlsym(h, "avcodec_alloc_context3");
    nvd_api.open2        = (nvd_open2)dlsym(h, "avcodec_open2");
    nvd_api.alloc_frame  = (nvd_alloc_frame)dlsym(h, "av_frame_alloc");
    nvd_api.alloc_pkt    = (nvd_alloc_pkt)dlsym(h, "av_packet_alloc");
    nvd_api.new_packet   = (nvd_new_packet)dlsym(h, "av_new_packet");
    nvd_api.free_pkt     = (nvd_free_pkt)dlsym(h, "av_packet_free");
    nvd_api.send         = (nvd_send)dlsym(h, "avcodec_send_packet");
    nvd_api.receive      = (nvd_receive)dlsym(h, "avcodec_receive_frame");
    return nvd_api.find_decoder && nvd_api.alloc_ctx && nvd_api.open2 &&
           nvd_api.alloc_frame && nvd_api.alloc_pkt && nvd_api.new_packet &&
           nvd_api.free_pkt && nvd_api.send && nvd_api.receive;
}

static int nvd_open(void) {
    if (nvd_ready) return nvd_ready > 0;
    if (!nvd_symbols()) { nvd_ready = -1; return 0; }
    const AVCodec *c = nvd_api.find_decoder(AV_CODEC_ID_H264);
    if (!c) { nvd_ready = -1; return 0; }
    nvd_ctx = nvd_api.alloc_ctx(c);
    nvd_pic = nvd_api.alloc_frame();
    if (!nvd_ctx || !nvd_pic || nvd_api.open2(nvd_ctx, c, NULL) < 0) {
        nvd_ready = -1;
        return 0;
    }
    nvd_ready = 1;
    return 1;
}

/* Decode one access unit (SPS+PPS prepended). Returns 1 when a picture came out. */
static int nvd_decode(const uint8_t *slice, unsigned len) {
    if (!nvd_open()) return 0;
    static uint8_t au[256 * 1024];
    static const uint8_t sc[4] = {0, 0, 0, 1};
    if (len > sizeof(au) - 64) len = sizeof(au) - 64;
    unsigned n = 0;
    memcpy(au + n, sc, 4); n += 4;
    memcpy(au + n, nvd_sps, sizeof(nvd_sps)); n += sizeof(nvd_sps);
    memcpy(au + n, sc, 4); n += 4;
    memcpy(au + n, nvd_pps, sizeof(nvd_pps)); n += sizeof(nvd_pps);
    memcpy(au + n, slice, len); n += len;

    AVPacket *pkt = nvd_api.alloc_pkt();
    if (!pkt) return 0;
    if (nvd_api.new_packet(pkt, (int)n) < 0) { nvd_api.free_pkt(&pkt); return 0; }
    memcpy(pkt->data, au, n);
    nvd_send_rc = nvd_api.send(nvd_ctx, pkt);
    nvd_api.free_pkt(&pkt);
    if (nvd_send_rc < 0) return 0;
    nvd_recv_rc = nvd_api.receive(nvd_ctx, nvd_pic);
    if (nvd_recv_rc < 0) return 0;
    nvd_w = nvd_pic->width;
    nvd_h = nvd_pic->height;
    nvd_decoded++;nvd_serial++;
    if(!(nvd_decoded%40)){char p[64];FILE*f;
        snprintf(p,sizeof p,"/tmp/nvdD_%d.ppm",nvd_decoded);
        if((f=fopen(p,"wb"))){fprintf(f,"P5\n%d %d\n255\n",nvd_w,nvd_h);
            for(int y=0;y<nvd_h;y++){fwrite(nvd_pic->data[0]+(size_t)y*nvd_pic->linesize[0],1,nvd_w,f);}
            fclose(f);}}
    return 1;
}

/* Copy the decoded picture into one contiguous NV12 surface (Y then interleaved UV). */
static void nvd_blit_nv12(uint8_t *dst, unsigned w, unsigned h) {
    if (!nvd_decoded || !w || !h) return;
    const unsigned cw = w, ch = h / 2;
    for (unsigned y = 0; y < h; y++)
        memcpy(dst + (size_t)y * w, nvd_pic->data[0] + (size_t)y * nvd_pic->linesize[0], cw);
    uint8_t *uv = dst + (size_t)w * h;
    for (unsigned y = 0; y < ch; y++)
        memcpy(uv + (size_t)y * w, nvd_pic->data[1] + (size_t)y * nvd_pic->linesize[1], w);
}

#endif
