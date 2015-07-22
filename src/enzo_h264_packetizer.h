#ifndef __PJMEDIA_ENZO_H264_PACKETIZER_H__
#define __PJMEDIA_ENZO_H264_PACKETIZER_H__

#include <pj/types.h>
#include <pjmedia/rtp.h>
#include <pjmedia-codec/enzo_h264.h>

#if defined(PJMEDIA_HAS_ENZO_H264_CODEC) && \
            PJMEDIA_HAS_ENZO_H264_CODEC == 1 && \
    defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO == 1)

PJ_BEGIN_DECL

#pragma pack(1)
/* PACSI header */
struct enzo_h264_pacsi_hdr {
#if defined(PJ_IS_BIG_ENDIAN) && (PJ_IS_BIG_ENDIAN != 0)
    pj_uint8_t f:1;
    pj_uint8_t nri:2;
    pj_uint8_t type:5;

    pj_uint8_t r:1;
    pj_uint8_t i:1;
    pj_uint8_t prid:6;

    pj_uint8_t n:1;
    pj_uint8_t did:3;
    pj_uint8_t qid:4;

    pj_uint8_t tid:3;
    pj_uint8_t u:1;
    pj_uint8_t d:1;
    pj_uint8_t o:1;
    pj_uint8_t rr:2;

    pj_uint8_t x:1;
    pj_uint8_t y:1;
    pj_uint8_t t:1;
    pj_uint8_t a:1;
    pj_uint8_t p:1;
    pj_uint8_t c:1;
    pj_uint8_t s:1;
    pj_uint8_t e:1;
#else
    pj_uint8_t type:5;
    pj_uint8_t nri:2;
    pj_uint8_t f:1;

    pj_uint8_t prid:6;
    pj_uint8_t i:1;
    pj_uint8_t r:1;

    pj_uint8_t qid:4;
    pj_uint8_t did:3;
    pj_uint8_t n:1;

    pj_uint8_t rr:2;
    pj_uint8_t o:1;
    pj_uint8_t d:1;
    pj_uint8_t u:1;
    pj_uint8_t tid:3;

    pj_uint8_t e:1;
    pj_uint8_t s:1;
    pj_uint8_t c:1;
    pj_uint8_t p:1;
    pj_uint8_t a:1;
    pj_uint8_t t:1;
    pj_uint8_t y:1;
    pj_uint8_t x:1;
#endif
};
#pragma pack()

#pragma pack(1)
/* SEI message header */
struct enzo_h264_sei_hdr {
#if defined(PJ_IS_BIG_ENDIAN) && (PJ_IS_BIG_ENDIAN != 0)
    pj_uint8_t f:1;
    pj_uint8_t nri:2;
    pj_uint8_t type:5;
#else
    pj_uint8_t type:5;
    pj_uint8_t nri:2;
    pj_uint8_t f:1;
#endif
    pj_uint8_t pt;        /* payload type */
    pj_uint8_t ps;        /* payload size */
    pj_uint8_t uuid[16];
};
#pragma pack()

#pragma pack(1)
struct enzo_h264_layer_desc {
    pj_uint16_t coded_width;   /* in network byte order */
    pj_uint16_t coded_height;  /* in network byte order */
    pj_uint16_t disp_width;    /* in network byte order */
    pj_uint16_t disp_height;   /* in network byte order */
    pj_uint32_t bitrate;       /* in network byte order */
#if defined(PJ_IS_BIG_ENDIAN) && (PJ_IS_BIG_ENDIAN != 0)
    pj_uint8_t fpsidx:5;
    pj_uint8_t lt:3;

    pj_uint8_t prid:6;
    pj_uint8_t cb:1;
    pj_uint8_t r:1;
#else
    pj_uint8_t lt:3;      /* layer type: 0 - base layer, 1 - temporal layer */
    pj_uint8_t fpsidx:5;  /* framerate index */

    pj_uint8_t r:1;       /* Reserved */
    pj_uint8_t cb:1;      /* 1 - constrained baseline profile */
    pj_uint8_t prid:6;    /* priority ID */
#endif
    pj_uint16_t r2;  /* Reserved */
};
#pragma pack()

PJ_DECL(int) enzo_h264_write_s(pj_uint8_t *stream, pj_uint16_t val);
PJ_DECL(int) enzo_h264_write_l(pj_uint8_t *stream, pj_uint32_t val);

/**
  * Print info of packet into log
  */
PJ_DECL(void) pjmedia_enzo_h264_unpacketize_debug(
	const pj_uint8_t *payload,
	pj_size_t payload_len,
        const char *trace_group);

/**
  * Packetize encoded NALs into bitstream that can send via RTP
  */
PJ_DECL(pj_status_t) pjmedia_enzo_h264_packetize(
    const pj_uint8_t *enc_buf,
    unsigned enc_buf_len,
    int mtu,
    pj_bool_t send_pacsi,
    unsigned *enc_processed,
    unsigned *fua_processed,
    pj_uint8_t *buf,
    unsigned buf_size,
    unsigned *buf_len);

/**
  * Unpacketize received packets into bitstream that OpenH264 can decode
  */
PJ_DECL(pj_status_t) pjmedia_enzo_h264_unpacketize(
        struct enzo_h264_codec_data *dec,
        const pj_uint8_t *payload,
        pj_size_t payload_len,
        pj_uint8_t *bits,
        pj_size_t   bits_len,
        unsigned   *bits_pos,
        pj_uint32_t *bits_flags,
        unsigned *layer_prid);

PJ_END_DECL

#endif  /* PJMEDIA_HAS_ENZO_H264_CODEC */

#endif  /* __PJMEDIA_ENZO_H264_PACKETIZER_H__ */
