#ifndef __PJMEDIA_CODEC_ENZO_H264_H__
#define __PJMEDIA_CODEC_ENZO_H264_H__

#include <pjmedia-codec/types.h>
#include <pjmedia-codec/h264_packetizer.h>
#include <pjmedia/vid_codec.h>

#if defined(PJMEDIA_HAS_ENZO_H264_CODEC) && \
    PJMEDIA_HAS_ENZO_H264_CODEC == 1 && \
    defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)

#define ENZO_TEST_CAM 0
#define ENZO_TEST_OPENH264 0
#define ENZO_TEST_LINUX 0

#if defined(ENZO_TEST_LINUX) && (ENZO_TEST_LINUX == 1)
#else
#include "../../../third_party/enzo/include/enzo-codec/enzo_codec.h"
#endif

#if defined(ENZO_TEST_OPENH264) && (ENZO_TEST_OPENH264 == 1)
#include <wels/codec_api.h>
#include <wels/codec_app_def.h>
#endif

PJ_BEGIN_DECL
typedef struct enzo_h264_codec_data
{
    pj_pool_t *pool;
    pjmedia_vid_codec_param	*prm;
    pj_bool_t whole;
    pjmedia_h264_packetizer	*pktz;

#if defined(ENZO_TEST_OPENH264) && (ENZO_TEST_OPENH264 == 1)
    ISVCEncoder *enc;
    SSourcePicture *esrc_pic;
    SFrameBSInfo bsi;
    unsigned enc_input_size;
#endif

    struct encoderInstance *avc_enc;
    struct decoderInstance *avc_dec;

#if defined(ENZO_TEST_CAM) && (ENZO_TEST_CAM == 1)
    struct decoderInstance *mjpg_dec;
    struct cameraInstance *usb_cam;
#endif
    pj_uint8_t *enc_buf;
    unsigned enc_buf_size;
    unsigned enc_buf_len;
    unsigned enc_frame_size;
    unsigned enc_processed;
    unsigned fua_processed;

    pj_uint8_t *dec_buf;
    unsigned dec_buf_size;
    unsigned dec_buf_len;

    pj_uint8_t ref_frm_cnt;
    pj_uint16_t donc;

    int num_of_nal_units;
    int disp_width;
    int disp_height;
    pj_bool_t is_disp_changed;
    pj_bool_t wait_to_finish_fu_a;

    unsigned decoded_prid[2];

#if defined(PJMEDIA_SAVE_ENCODE_VIDEO_STREAM_TO_FILE) && (PJMEDIA_SAVE_ENCODE_VIDEO_STREAM_TO_FILE == 1)
    FILE *encode_strm_file;
#endif

#if defined(PJMEDIA_SAVE_DECODE_VIDEO_STREAM_TO_FILE) && (PJMEDIA_SAVE_DECODE_VIDEO_STREAM_TO_FILE == 1)
    FILE *decode_strm_file;
#endif
} enzo_h264_codec_data;

/**
 * Initialize and register Enzo H264 codec factory.
 *
 * @param mgr The video codec manager instance where this codec will
 * 		    be registered to. Specify NULL to use default instance
 * 		    (in that case, an instance of video codec manager must
 * 		    have been created beforehand).
 * @param pf Pool factory.
 *
 * @return PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_codec_enzo_h264_vid_init(pjmedia_vid_codec_mgr *mgr,
                                                      pj_pool_factory *pf);

/**
 * Unregister Enzo H264 video codecs factory from the video codec manager and
 * deinitialize the codec library.
 *
 * @return PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_codec_enzo_h264_vid_deinit(void);

PJ_END_DECL
#endif  /* PJMEDIA_HAS_ENZO_H264_CODEC */
#endif	/* __PJMEDIA_CODEC_ENZO_H264_H__ */
