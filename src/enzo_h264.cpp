#include <pjmedia-codec/enzo_h264.h>
#include <pjmedia-codec/enzo_h264_packetizer.h>
#include <pjmedia/vid_codec_util.h>
#include <pjmedia/errno.h>
#include <pj/log.h>
#include <pj/math.h>

#if defined(PJ_CONFIG_ANDROID) && PJ_CONFIG_ANDROID
#include <jni.h>
#endif

#if defined(PJMEDIA_HAS_ENZO_H264_CODEC) && \
    PJMEDIA_HAS_ENZO_H264_CODEC == 1 && \
    defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)

#if defined(ENZO_TEST_LINUX) && (ENZO_TEST_LINUX == 1)
#else
#include <enzo_utils.h>
#endif

/*
 * Constants
 */
#define THIS_FILE "enzo_h264.cpp"

// #if defined(PJ_DARWINOS) && PJ_DARWINOS != 0 && TARGET_OS_IPHONE
#define DEFAULT_WIDTH 640
#define DEFAULT_HEIGHT 480
#define DEFAULT_FPS 25

#define MAX_RX_WIDTH 1200
#define MAX_RX_HEIGHT 800

#define MAX_TX_WIDTH 1200
#define MAX_TX_HEIGHT 800

#define DEFAULT_BITRATE 1500
#define DEFAULT_GOP_SIZE 1

#define DEFAULT_AVG_BITRATE 512000
#define DEFAULT_MAX_BITRATE 1024000

#define INVALID_PRID 0xFF

#define VIDEO_INFO_FULL_LOG 1

#if defined(PJMEDIA_MEASURE_VIDEO_INFO) && (PJMEDIA_MEASURE_VIDEO_INFO == 1)
#define COUNTER_INTERVAL 20
static pj_time_val start_encoded_timer = {0};
static pj_int32_t encoded_frames_counter = 0;
static pj_time_val start_decoded_timer = {0};
static pj_int32_t decoded_frames_counter = 0;
static pj_int32_t lost_decoded_frames_counter = 0;
static pj_bool_t start_encoding = false;
static pj_bool_t start_decoding = false;
#endif

typedef struct enzo_h264_vid_res {
    unsigned width;
    unsigned height;
    unsigned min_br;
    unsigned max_br;
    pjmedia_ratio fps;
} enzo_h264_vid_res;

static enzo_h264_vid_res avai_vid_res_4x3[] = {
    { 212, 160, 15000,  85500 , {15, 2} },  /* fps 7.5 */
    { 212, 160, 85500,  132500, {25, 2} },  /* fps 12.5 */
    { 212, 160, 132500, 156000, {15, 1} },  /* fps 15 */
    { 212, 160, 156000, 250000, {25, 1} },  /* fps 25 */

    { 320, 240, 100000, 175000, {15, 2} },  /* fps 7.5 */
    { 320, 240, 175000, 225000, {25, 2} },  /* fps 12.5 */
    { 320, 240, 225000, 250000, {15, 1} },  /* fps 15 */
    { 320, 240, 250000, 350000, {25, 1} },  /* fps 25 */

    { 424, 320, 200000, 275000, {15, 2} },  /* fps 7.5 */
    { 424, 320, 275000, 325000, {25, 2} },  /* fps 12.5 */
    { 424, 320, 325000, 350000, {15, 1} },  /* fps 15 */
    { 424, 320, 350000, 450000, {25, 1} },  /* fps 25 */

    { 640, 480, 300000, 450000, {15, 2} },  /* fps 7.5 */
    { 640, 480, 450000, 550000, {25, 2} },  /* fps 12.5 */
    { 640, 480, 550000, 600000, {15, 1} },  /* fps 15 */
    { 640, 480, 600000, 800000, {25, 1} },  /* fps 25 */
};
static const int avai_vid_res_4x3_cnt = sizeof(avai_vid_res_4x3) / sizeof(avai_vid_res_4x3[0]);
static enzo_h264_vid_res avai_vid_res_3x4[] = {
    { 160, 212, 15000,  85500 , {15, 2} },  /* fps 7.5 */
    { 160, 212, 85500,  132500, {25, 2} },  /* fps 12.5 */
    { 160, 212, 132500, 156000, {15, 1} },  /* fps 15 */
    { 160, 212, 156000, 250000, {25, 1} },  /* fps 25 */

    { 240, 320, 100000, 175000, {15, 2} },  /* fps 7.5 */
    { 240, 320, 175000, 225000, {25, 2} },  /* fps 12.5 */
    { 240, 320, 225000, 250000, {15, 1} },  /* fps 15 */
    { 240, 320, 250000, 350000, {25, 1} },  /* fps 25 */

    { 320, 424, 200000, 275000, {15, 2} },  /* fps 7.5 */
    { 320, 424, 275000, 325000, {25, 2} },  /* fps 12.5 */
    { 320, 424, 325000, 350000, {15, 1} },  /* fps 15 */
    { 320, 424, 350000, 450000, {25, 1} },  /* fps 25 */

    { 480, 640, 300000, 450000, {15, 2} },  /* fps 7.5 */
    { 480, 640, 450000, 550000, {25, 2} },  /* fps 12.5 */
    { 480, 640, 550000, 600000, {15, 1} },  /* fps 15 */
    { 480, 640, 600000, 800000, {25, 1} },  /* fps 25 */
};
static const int avai_vid_res_3x4_cnt = sizeof(avai_vid_res_3x4) / sizeof(avai_vid_res_3x4[0]);

static const enzo_h264_vid_res *avai_vid_res = avai_vid_res_4x3;
static int avai_vid_res_cnt = avai_vid_res_4x3_cnt;

/*
 * Factory operations.
 */
static pj_status_t enzo_h264_test_alloc(pjmedia_vid_codec_factory *factory,
                                        const pjmedia_vid_codec_info *info );
static pj_status_t enzo_h264_default_attr(pjmedia_vid_codec_factory *factory,
                                          const pjmedia_vid_codec_info *info,
                                          pjmedia_vid_codec_param *attr );
static pj_status_t enzo_h264_enum_info(pjmedia_vid_codec_factory *factory,
                                       unsigned *count,
                                       pjmedia_vid_codec_info codecs[]);
static pj_status_t enzo_h264_alloc_codec(pjmedia_vid_codec_factory *factory,
                                         const pjmedia_vid_codec_info *info,
                                         pjmedia_vid_codec **p_codec);
static pj_status_t enzo_h264_dealloc_codec(pjmedia_vid_codec_factory *factory,
                                           pjmedia_vid_codec *codec );

/*
 * Codec operations
 */
static pj_status_t enzo_h264_codec_init(pjmedia_vid_codec *codec,
                                        pj_pool_t *pool );
static pj_status_t enzo_h264_codec_open(pjmedia_vid_codec *codec,
                                        pjmedia_vid_codec_param *param );
static pj_status_t enzo_h264_codec_close(pjmedia_vid_codec *codec);
static pj_status_t enzo_h264_codec_modify(pjmedia_vid_codec *codec,
                                          const pjmedia_vid_codec_param *param);
static pj_status_t enzo_h264_codec_get_param(pjmedia_vid_codec *codec,
                                             pjmedia_vid_codec_param *param);
static pj_status_t enzo_h264_codec_encode_begin(pjmedia_vid_codec *codec,
                                                const pjmedia_vid_encode_opt *opt,
                                                const pjmedia_frame *input,
                                                unsigned out_size,
                                                pjmedia_frame *output,
                                                pj_bool_t *has_more);
static pj_status_t enzo_h264_codec_encode_more(pjmedia_vid_codec *codec,
                                               unsigned out_size,
                                               pjmedia_frame *output,
                                               pj_bool_t *has_more);

static pj_status_t enzo_h264_codec_encode_write_pacsi(pjmedia_vid_codec *codec,
                                                      #if defined(ENZO_TEST_LINUX) && (ENZO_TEST_LINUX == 1)
                                                      #else
                                                      mediaBuffer *avc_data,
                                                      #endif
                                                      pj_uint8_t *buf,
                                                      unsigned buf_size,
                                                      unsigned *buf_pos,
                                                      unsigned *bit_flags);
static pj_status_t enzo_h264_codec_encode_write_nals(pjmedia_vid_codec *codec,
                                                     #if defined(ENZO_TEST_LINUX) && (ENZO_TEST_LINUX == 1)
                                                     #else
                                                     mediaBuffer *avc_data,
                                                     #endif
                                                     pj_uint8_t *buf,
                                                     unsigned buf_size,
                                                     unsigned *buf_pos);

static pj_status_t enzo_h264_codec_decode(pjmedia_vid_codec *codec,
                                          pj_size_t count,
                                          pjmedia_frame packets[],
                                          unsigned out_size,
                                          pjmedia_frame *output);

static pj_status_t enzo_h264_codec_apply_bandwidth(
        const pjmedia_vid_codec *codec,
        unsigned bandwidth,
        unsigned maxsize,
        pjmedia_format *fmt);

/* Definition for Enzo H264 codecs operations. */
static pjmedia_vid_codec_op enzo_h264_codec_op =
{
    &enzo_h264_codec_init,
    &enzo_h264_codec_open,
    &enzo_h264_codec_close,
    &enzo_h264_codec_modify,
    &enzo_h264_codec_get_param,
    &enzo_h264_codec_encode_begin,
    &enzo_h264_codec_encode_more,
    &enzo_h264_codec_decode,
    NULL, /* recover */
    &enzo_h264_codec_apply_bandwidth
};

/* Definition for Enzo H264 codecs factory operations. */
static pjmedia_vid_codec_factory_op enzo_h264_factory_op =
{
    &enzo_h264_test_alloc,
    &enzo_h264_default_attr,
    &enzo_h264_enum_info,
    &enzo_h264_alloc_codec,
    &enzo_h264_dealloc_codec
};

static struct enzo_h264_factory
{
    pjmedia_vid_codec_factory base;
    pjmedia_vid_codec_mgr *mgr;
    pj_pool_factory *pf;
    pj_pool_t *pool;
} enzo_h264_factory;

static const pj_str_t lync2013_h264_name = {(char *)"X-H264UC", 8};  /* Lync 2013 H264 codec name */

PJ_DEF(pj_status_t) pjmedia_codec_enzo_h264_vid_init(pjmedia_vid_codec_mgr *mgr,
                                                     pj_pool_factory *pf)
{
    pj_status_t status;

    if (enzo_h264_factory.pool != NULL) {
        /* Already initialized. */
        return PJ_SUCCESS;
    }

    if (!mgr) mgr = pjmedia_vid_codec_mgr_instance();
    PJ_ASSERT_RETURN(mgr, PJ_EINVAL);

    /* Create Enzo H264 codec factory. */
    enzo_h264_factory.base.op = &enzo_h264_factory_op;
    enzo_h264_factory.base.factory_data = NULL;
    enzo_h264_factory.mgr = mgr;
    enzo_h264_factory.pf = pf;
    enzo_h264_factory.pool = pj_pool_create(pf, "enzo_h264_factory", 256, 256, NULL);
    if (!enzo_h264_factory.pool)
        return PJ_ENOMEM;

    /* Registering format match for SDP negotiation */
    status = pjmedia_sdp_neg_register_fmt_match_cb(
                &lync2013_h264_name,
                &pjmedia_vid_codec_h264_match_sdp);
    pj_assert(status == PJ_SUCCESS);

    /* Register codec factory to codec manager. */
    status = pjmedia_vid_codec_mgr_register_factory(mgr,
                                                    &enzo_h264_factory.base);

    PJ_LOG(4, (THIS_FILE, "Enzo H264 codec initialized with status: %d", status));

    return PJ_SUCCESS;
}

/*
 * Unregister Enzo H264 codecs factory from pjmedia endpoint.
 */
PJ_DEF(pj_status_t) pjmedia_codec_enzo_h264_vid_deinit(void)
{
    pj_status_t status = PJ_SUCCESS;

    if (enzo_h264_factory.pool == NULL) {
        /* Already deinitialized */
        return PJ_SUCCESS;
    }

    /* Unregister format match for SDP negotiation */
    status = pjmedia_sdp_neg_register_fmt_match_cb(
                &lync2013_h264_name,
                NULL);

    /* Unregister Enzo H264 codecs factory. */
    status = pjmedia_vid_codec_mgr_unregister_factory(enzo_h264_factory.mgr,
                                                      &enzo_h264_factory.base);

    /* Destroy pool. */
    pj_pool_release(enzo_h264_factory.pool);
    enzo_h264_factory.pool = NULL;

#if defined(ENZO_TEST_LINUX) && (ENZO_TEST_LINUX == 1)
#else
    /* Deinitialize VPU */
    vpuDeinit();
#endif

    PJ_LOG(4, (THIS_FILE, "Deinitialized Enzo codec..."));

    return status;
}

static pj_status_t enzo_h264_test_alloc(pjmedia_vid_codec_factory *factory,
                                        const pjmedia_vid_codec_info *info )
{
    PJ_ASSERT_RETURN(factory == &enzo_h264_factory.base, PJ_EINVAL);

    if (info->fmt_id == PJMEDIA_FORMAT_H264 &&
            info->pt != 0) {
        return PJ_SUCCESS;
    }

    return PJMEDIA_CODEC_EUNSUP;
}

static pj_status_t enzo_h264_default_attr(pjmedia_vid_codec_factory *factory,
                                          const pjmedia_vid_codec_info *info,
                                          pjmedia_vid_codec_param *attr )
{
    PJ_ASSERT_RETURN(factory == &enzo_h264_factory.base, PJ_EINVAL);
    PJ_ASSERT_RETURN(info && attr, PJ_EINVAL);

    PJ_LOG(4, (THIS_FILE, "Set default attributes for Enzo codec..."));

    pj_bzero(attr, sizeof(pjmedia_vid_codec_param));

    attr->dir = PJMEDIA_DIR_ENCODING_DECODING;
    attr->packing = PJMEDIA_VID_PACKING_PACKETS;  /* For RTP transmission */

    /* Encoded format */
#if defined(ENZO_TEST_OPENH264) && (ENZO_TEST_OPENH264 == 1)
    pjmedia_format_init_video(&attr->enc_fmt, PJMEDIA_FORMAT_H264,
                              avai_vid_res[0].width, avai_vid_res[0].height,
            avai_vid_res[0].fps.num, avai_vid_res[0].fps.denum);
#else
    pjmedia_format_init_video(&attr->enc_fmt, PJMEDIA_FORMAT_H264,
                              DEFAULT_WIDTH, DEFAULT_HEIGHT,
                              DEFAULT_FPS, 1);
#endif

    /* Decoded format */
    pjmedia_format_init_video(&attr->dec_fmt, PJMEDIA_FORMAT_I420,
                              DEFAULT_WIDTH, DEFAULT_HEIGHT,
                              DEFAULT_FPS, 1);

    /* Decoding fmtp */
    attr->dec_fmtp.cnt = 2;
    attr->dec_fmtp.param[0].name = pj_str((char*)"packetization-mode");
    attr->dec_fmtp.param[0].val = pj_str((char*)"1");
    attr->dec_fmtp.param[1].name = pj_str((char*)"mst-mode");
    attr->dec_fmtp.param[1].val = pj_str((char*)"NI-TC");

#if defined(ENZO_TEST_OPENH264) && (ENZO_TEST_OPENH264 == 1)
    attr->enc_fmt.det.vid.avg_bps = (avai_vid_res[0].min_br + avai_vid_res[0].max_br) / 2;
    attr->enc_fmt.det.vid.max_bps = avai_vid_res[0].max_br;
#else
    attr->enc_fmt.det.vid.avg_bps = DEFAULT_AVG_BITRATE;
    attr->enc_fmt.det.vid.max_bps = DEFAULT_MAX_BITRATE;
#endif

    /* Encoding MTU */
    attr->enc_mtu = PJMEDIA_MAX_VID_PAYLOAD_SIZE;

    return PJ_SUCCESS;
}

static pj_status_t enzo_h264_enum_info(pjmedia_vid_codec_factory *factory,
                                       unsigned *count,
                                       pjmedia_vid_codec_info info[])
{
    PJ_ASSERT_RETURN(info && *count > 0, PJ_EINVAL);
    PJ_ASSERT_RETURN(factory == &enzo_h264_factory.base, PJ_EINVAL);

    *count = 1;
    info->fmt_id = PJMEDIA_FORMAT_H264;
    info->pt = PJMEDIA_RTP_PT_H264;
    info->encoding_name = lync2013_h264_name;
    info->encoding_desc = pj_str((char*)"Enzo H264 Codec");
    info->clock_rate = 90000;
    info->dir = PJMEDIA_DIR_ENCODING_DECODING;
    info->dec_fmt_id_cnt = 1;
    info->dec_fmt_id[0] = PJMEDIA_FORMAT_I420;
    info->packings = PJMEDIA_VID_PACKING_PACKETS;

    info->fps_cnt = 3;
    info->fps[0].num = 15;
    info->fps[0].denum = 1;
    info->fps[1].num = 25;
    info->fps[1].denum = 1;
    info->fps[2].num = 30;
    info->fps[2].denum = 1;

    return PJ_SUCCESS;
}

static pj_status_t enzo_h264_alloc_codec(pjmedia_vid_codec_factory *factory,
                                         const pjmedia_vid_codec_info *info,
                                         pjmedia_vid_codec **p_codec)
{
    pj_pool_t *pool;
    pjmedia_vid_codec *codec;
    enzo_h264_codec_data *enzo_h264_data;

    PJ_ASSERT_RETURN(factory == &enzo_h264_factory.base && info && p_codec,
                     PJ_EINVAL);

    *p_codec = NULL;

    pool = pj_pool_create(enzo_h264_factory.pf, "enzo_h264_%p", 512, 512, NULL);
    if (!pool)
        return PJ_ENOMEM;

    PJ_LOG(4, (THIS_FILE, "Allocating Enzo codec..."));

    /* Codec instance */
    codec = PJ_POOL_ZALLOC_T(pool, pjmedia_vid_codec);
    codec->factory = factory;
    codec->op = &enzo_h264_codec_op;

    /* Codec data */
    enzo_h264_data = PJ_POOL_ZALLOC_T(pool, enzo_h264_codec_data);
    enzo_h264_data->pool = pool;
    enzo_h264_data->decoded_prid[0] = INVALID_PRID;
    enzo_h264_data->decoded_prid[1] = INVALID_PRID;
    codec->codec_data = enzo_h264_data;

#if defined(ENZO_TEST_LINUX) && (ENZO_TEST_LINUX == 1)
#else
    /* Encoder allocation */
    enzo_h264_data->avc_enc = (struct encoderInstance *)calloc(1, sizeof(struct encoderInstance));
    /* Set properties for H264 AVC encoder */
    enzo_h264_data->avc_enc->type = H264AVC;
    enzo_h264_data->avc_enc->width = DEFAULT_WIDTH;
    enzo_h264_data->avc_enc->height = DEFAULT_HEIGHT;
    enzo_h264_data->avc_enc->fps = DEFAULT_FPS;
    enzo_h264_data->avc_enc->bitRate = DEFAULT_BITRATE;
    enzo_h264_data->avc_enc->gopSize = DEFAULT_GOP_SIZE;
    enzo_h264_data->avc_enc->colorSpace = YUV420P;
    //    enzo_h264_data->avc_enc->colorSpace = YUV422P;

    /* Decoder allocation */
    enzo_h264_data->avc_dec = (struct decoderInstance *)calloc(1, sizeof(struct decoderInstance));
    /* Set properties for H264 AVC decoder */
    enzo_h264_data->avc_dec->type = H264AVC;

#if defined(ENZO_TEST_CAM) && (ENZO_TEST_CAM == 1)
    enzo_h264_data->mjpg_dec = (struct decoderInstance *)calloc(1, sizeof(struct decoderInstance));
    /* Set properties for H264 AVC decoder */
    enzo_h264_data->mjpg_dec->type = MJPEG;

    enzo_h264_data->usb_cam = (struct cameraInstance  *)calloc(1, sizeof(struct cameraInstance));
    /* Set properties for USB camera */
    enzo_h264_data->usb_cam->type = MJPEG;
    enzo_h264_data->usb_cam->width = DEFAULT_WIDTH;
    enzo_h264_data->usb_cam->height = DEFAULT_HEIGHT;
    enzo_h264_data->usb_cam->fps = DEFAULT_FPS;
    strcpy(enzo_h264_data->usb_cam->deviceName,"/dev/video0");
#endif
#endif

#if defined(ENZO_TEST_OPENH264) && (ENZO_TEST_OPENH264 == 1)
    int rc = WelsCreateSVCEncoder(&enzo_h264_data->enc);
    if (rc != 0) {
        PJ_LOG(4, (THIS_FILE, "Can not initialize OpenH264 codec..."));
        goto on_error;
    }

    enzo_h264_data->esrc_pic = PJ_POOL_ZALLOC_T(pool, SSourcePicture);
#endif

    *p_codec = codec;
    return PJ_SUCCESS;

on_error:
    enzo_h264_dealloc_codec(factory, codec);
    return PJMEDIA_CODEC_EFAILED;
}

static pj_status_t enzo_h264_dealloc_codec(pjmedia_vid_codec_factory *factory,
                                           pjmedia_vid_codec *codec )
{
    PJ_ASSERT_RETURN(codec, PJ_EINVAL);

    PJ_LOG(4, (THIS_FILE, "Deallocating Enzo codec..."));

    enzo_h264_codec_data *enzo_h264_data;
    enzo_h264_data = (enzo_h264_codec_data*)codec->codec_data;

#if defined(ENZO_TEST_LINUX) && (ENZO_TEST_LINUX == 1)
#else
    /* Release Enzo codec data */
    if (enzo_h264_data->avc_enc) {
        encoderDeinit(enzo_h264_data->avc_enc);
        free(enzo_h264_data->avc_enc);
        enzo_h264_data->avc_enc = NULL;
    }

    if (enzo_h264_data->avc_dec) {
        decoderDeinit(enzo_h264_data->avc_dec);
        free(enzo_h264_data->avc_dec);
        enzo_h264_data->avc_dec = NULL;
    }

#if defined(ENZO_TEST_CAM) && (ENZO_TEST_CAM == 1)
    if (enzo_h264_data->mjpg_dec) {
        decoderDeinit(enzo_h264_data->mjpg_dec);
        free(enzo_h264_data->mjpg_dec);
        enzo_h264_data->mjpg_dec = NULL;
    }

    if (enzo_h264_data->usb_cam) {
        cameraDeinit(enzo_h264_data->usb_cam);
        free(enzo_h264_data->usb_cam);
        enzo_h264_data->usb_cam = NULL;
    }
#endif
#endif

#if defined(ENZO_TEST_OPENH264) && (ENZO_TEST_OPENH264 == 1)
    if (enzo_h264_data->enc) {
        WelsDestroySVCEncoder(enzo_h264_data->enc);
        enzo_h264_data->enc = NULL;
    }
#endif

    pj_pool_release(enzo_h264_data->pool);
    return PJ_SUCCESS;
}

static pj_status_t enzo_h264_codec_init(pjmedia_vid_codec *codec,
                                        pj_pool_t *pool )
{
    PJ_ASSERT_RETURN(codec && pool, PJ_EINVAL);
    PJ_UNUSED_ARG(pool);

    PJ_LOG(4, (THIS_FILE, "Initializing Enzo codec..."));

#if defined(ENZO_TEST_LINUX) && (ENZO_TEST_LINUX == 1)
#else
    /* Init the VPU. This must be done before a codec can be used.
           If this fails, we need to bail. */
    int ret = vpuInit();
    if (ret < 0) {
        PJ_LOG(4, (THIS_FILE, "[Enzo Error]: failed to initialize the VPU, status: %d", ret));
        return PJ_FAILED;
    }

    enzo_h264_codec_data *enzo_h264_data;
    enzo_h264_data = (enzo_h264_codec_data*)codec->codec_data;

    struct mediaBuffer *avcData = NULL;
    avcData = (struct mediaBuffer *)calloc(1, sizeof(struct mediaBuffer));

    ret = encoderInit(enzo_h264_data->avc_enc, avcData);
    if (ret < 0) {
        PJ_LOG(4, (THIS_FILE, "[Enzo Error]: failed to initialize the Enzo encoder, status: %d", ret));
        if (avcData)
            free(avcData);
        return PJ_FAILED;
    }

#if defined(PJMEDIA_SAVE_ENCODE_VIDEO_STREAM_TO_FILE) && (PJMEDIA_SAVE_ENCODE_VIDEO_STREAM_TO_FILE == 1)
    enzo_h264_data->encode_strm_file = fopen("/sdcard/data/com.wync.me/h264_encode_stream.dat", "wb");
    /* Write AVC SPS and PPS headers to output file */
    if (enzo_h264_data->encode_strm_file)
        fwrite(avcData->vBufOut, avcData->bufOutSize, 1, enzo_h264_data->encode_strm_file);
#endif

    ret = decoderInit(enzo_h264_data->avc_dec, avcData);
    if (ret < 0) {
        PJ_LOG(4, (THIS_FILE, "[Enzo Error]: failed to initialize the Enzo decoder, status: %d", ret));
        if (avcData)
            free(avcData);
        return PJ_FAILED;
    }

#if defined(PJMEDIA_SAVE_DECODE_VIDEO_STREAM_TO_FILE) && (PJMEDIA_SAVE_DECODE_VIDEO_STREAM_TO_FILE == 1)
    enzo_h264_data->decode_strm_file = fopen("/sdcard/data/com.wync.me/h264_decode_stream.dat", "wb");
#endif

#if defined(ENZO_TEST_CAM) && (ENZO_TEST_CAM == 1)
    struct mediaBuffer *camData = NULL;
    camData = (struct mediaBuffer *)calloc(1, sizeof(struct mediaBuffer));
    ret = cameraInit(enzo_h264_data->usb_cam);
    if (ret < 0)
        PJ_LOG(4, (THIS_FILE, "[Enzo Error]: could not init camera"));

    /* In order to init mjpg decoder, it must be supplied with bitstream
           parse */
    ret = cameraGetFrame(enzo_h264_data->usb_cam, camData);
    if (ret < 0)
        PJ_LOG(4, (THIS_FILE, "[Enzo Error]: could not get camera frame"));

    ret = decoderInit(enzo_h264_data->mjpg_dec, camData);
    if (ret < 0)
        PJ_LOG(4, (THIS_FILE, "[Enzo Error]: could not init MJPG decoder"));

    if (camData)
        free(camData);
#endif

    PJ_LOG(4, (THIS_FILE, "Successfully to initialize the VPU"));

    if (avcData)
        free(avcData);
#endif

    return PJ_SUCCESS;
}

static pj_status_t enzo_h264_codec_open(pjmedia_vid_codec *codec,
                                        pjmedia_vid_codec_param *codec_param )
{
    PJ_LOG(4, (THIS_FILE, "OPENING CODEC..."));

    PJ_ASSERT_RETURN(codec && codec_param, PJ_EINVAL);

    pjmedia_vid_codec_param	*param;
    pjmedia_h264_packetizer_cfg pktz_cfg;
    pjmedia_vid_codec_h264_fmtp h264_fmtp;

    enzo_h264_codec_data *enzo_h264_data;
    enzo_h264_data = (enzo_h264_codec_data*) codec->codec_data;
    enzo_h264_data->prm = pjmedia_vid_codec_param_clone( enzo_h264_data->pool,
                                                         codec_param);
    param = enzo_h264_data->prm;
    param->ignore_fmtp = PJ_TRUE;

    /* Parse remote fmtp */
    pj_bzero(&h264_fmtp, sizeof(h264_fmtp));
    pj_status_t status = pjmedia_vid_codec_h264_parse_fmtp(&param->enc_fmtp, &h264_fmtp);
    if (status != PJ_SUCCESS)
        return status;

    /* Apply SDP fmtp to format in codec param */
    if (!param->ignore_fmtp) {
        status = pjmedia_vid_codec_h264_apply_fmtp(param);
        if (status != PJ_SUCCESS)
            return status;
    }

    pj_bzero(&pktz_cfg, sizeof(pktz_cfg));
    pktz_cfg.mtu = param->enc_mtu;

    /* Packetization mode */
    if (h264_fmtp.packetization_mode == 0)
        pktz_cfg.mode = PJMEDIA_H264_PACKETIZER_MODE_SINGLE_NAL;
    else if (h264_fmtp.packetization_mode == 1)
        pktz_cfg.mode = PJMEDIA_H264_PACKETIZER_MODE_NON_INTERLEAVED;
    else
        return PJ_ENOTSUP;

    status = pjmedia_h264_packetizer_create(enzo_h264_data->pool, &pktz_cfg,
                                            &enzo_h264_data->pktz);
    if (status != PJ_SUCCESS)
        return status;

    enzo_h264_data->whole = (param->packing == PJMEDIA_VID_PACKING_WHOLE);

#if defined(ENZO_TEST_OPENH264) && (ENZO_TEST_OPENH264 == 1)
    /* Init encoder parameters */
    SEncParamExt eprm;
    enzo_h264_data->enc->GetDefaultParams(&eprm);

    eprm.iUsageType = CAMERA_VIDEO_REAL_TIME;

    eprm.iPicWidth = param->enc_fmt.det.vid.size.w;
    eprm.iPicHeight = param->enc_fmt.det.vid.size.h;
    eprm.iTargetBitrate = param->enc_fmt.det.vid.avg_bps;
    eprm.iRCMode = RC_QUALITY_MODE;
    eprm.fMaxFrameRate = (param->enc_fmt.det.vid.fps.num * 1.0 /
                          param->enc_fmt.det.vid.fps.denum);

    eprm.iTemporalLayerNum = 1;  // 1 for only IDR
    eprm.iSpatialLayerNum = 1;

    eprm.iComplexityMode = MEDIUM_COMPLEXITY;
    eprm.uiIntraPeriod = 1; // GOP, 1 for only IDR
    eprm.iNumRefFrame = -1;  // AUTO_REF_PIC_COUNT
    //    eprm.bEnableSpsPpsIdAddition	= (enzo_h264_data->whole? false : true);
    eprm.eSpsPpsIdStrategy = CONSTANT_ID;
    eprm.bPrefixNalAddingCtrl = false;
    eprm.bEnableSSEI = false;
    eprm.bSimulcastAVC = false;  //  false: use SVC syntax for higher spatial layers; true: use Simulcast AVC
    eprm.iPaddingFlag = 0;
    eprm.iEntropyCodingModeFlag = 0;  // 0:CAVLC (baseline profile) 1:CABAC (high profile).

    /* rc control */
    eprm.bEnableFrameSkip = true;  // true;
    eprm.iMaxBitrate = param->enc_fmt.det.vid.max_bps;
    eprm.uiMaxNalSize = 0;

    /*LTR settings*/
    eprm.bEnableLongTermReference = false;
    //    eprm.iLTRRefNum = 0;  // Initializer will set this value
    eprm.iLtrMarkPeriod = 30;

    /* Multi-thread settings*/
    eprm.iMultipleThreadIdc             = 0;  // auto

    /* Deblocking loop filter */
    eprm.iLoopFilterDisableIdc = 1;  // off
    eprm.iLoopFilterAlphaC0Offset = 0;
    eprm.iLoopFilterBetaOffset = 0;

    /* Pre-processing feature*/
    eprm.bEnableDenoise = false;
    eprm.bEnableBackgroundDetection = true;
    eprm.bEnableAdaptiveQuant = false;
    eprm.bEnableFrameCroppingFlag = true;
    eprm.bEnableSceneChangeDetect = true;

    eprm.bIsLosslessLink = false;

    eprm.sSpatialLayers[0].iVideoWidth = param->enc_fmt.det.vid.size.w;
    eprm.sSpatialLayers[0].iVideoHeight = param->enc_fmt.det.vid.size.h;
    eprm.sSpatialLayers[0].fFrameRate = eprm.fMaxFrameRate;
    eprm.sSpatialLayers[0].iSpatialBitrate = eprm.iTargetBitrate;
    eprm.sSpatialLayers[0].iMaxSpatialBitrate = eprm.iMaxBitrate;
    eprm.sSpatialLayers[0].uiProfileIdc = PRO_BASELINE;
    eprm.sSpatialLayers[0].uiLevelIdc = LEVEL_3_1;

    eprm.sSpatialLayers[0].sSliceCfg.uiSliceMode = (enzo_h264_data->whole ? SM_SINGLE_SLICE : SM_DYN_SLICE);
    if (eprm.sSpatialLayers[0].sSliceCfg.uiSliceMode == SM_DYN_SLICE) {
        eprm.sSpatialLayers[0].sSliceCfg.sSliceArgument.uiSliceSizeConstraint = param->enc_mtu;
        eprm.uiMaxNalSize = param->enc_mtu;
    }

    /* Initialize encoder */
    int rc = enzo_h264_data->enc->InitializeExt (&eprm);
    if (rc != cmResultSuccess) {
        PJ_LOG(4,(THIS_FILE, "SVC encoder Initialize failed, rc=%d", rc));
        //return PJMEDIA_CODEC_EFAILED;
    }

    /* Init input picture */
    enzo_h264_data->esrc_pic->iColorFormat = videoFormatI420;
    enzo_h264_data->esrc_pic->uiTimeStamp = 0;
    enzo_h264_data->esrc_pic->iPicWidth	= eprm.iPicWidth;
    enzo_h264_data->esrc_pic->iPicHeight = eprm.iPicHeight;
    enzo_h264_data->esrc_pic->iStride[0] = enzo_h264_data->esrc_pic->iPicWidth;
    enzo_h264_data->esrc_pic->iStride[1] =
            enzo_h264_data->esrc_pic->iStride[2] =
            enzo_h264_data->esrc_pic->iStride[0] >> 1;

    enzo_h264_data->enc_input_size = enzo_h264_data->esrc_pic->iPicWidth *
            enzo_h264_data->esrc_pic->iPicHeight * 3 >> 1;

    int videoFormat = videoFormatI420;
    enzo_h264_data->enc->SetOption (ENCODER_OPTION_DATAFORMAT, &videoFormat);
#endif

    enzo_h264_data->dec_buf_size = (MAX_RX_WIDTH * MAX_RX_HEIGHT * 3 >> 1) +
            (MAX_RX_WIDTH);
    enzo_h264_data->dec_buf = (pj_uint8_t*)pj_pool_alloc(enzo_h264_data->pool,
                                                         enzo_h264_data->dec_buf_size);

    enzo_h264_data->enc_buf_size = (MAX_TX_WIDTH * MAX_TX_HEIGHT * 3 >> 1) + MAX_TX_WIDTH;
    enzo_h264_data->enc_buf = (pj_uint8_t *)pj_pool_alloc(enzo_h264_data->pool,
                                                          enzo_h264_data->enc_buf_size);

    /* Need to update param back after values are negotiated */
    pj_memcpy(codec_param, param, sizeof(*codec_param));

#if defined(PJMEDIA_MEASURE_VIDEO_INFO) && (PJMEDIA_MEASURE_VIDEO_INFO == 1)
    PJ_LOG(1, (THIS_FILE, "[VIDEO_MEASUREMENT]: Enzo video encode info: w = %d, h = %d, fps = %d/%d; "
                          "Enzo video decode info: w = %d, h = %d, fps = %d/%d", param->enc_fmt.det.vid.size.w,
               param->enc_fmt.det.vid.size.h, param->enc_fmt.det.vid.fps.num, param->enc_fmt.det.vid.fps.denum,
               param->dec_fmt.det.vid.size.w, param->dec_fmt.det.vid.size.h,
               param->dec_fmt.det.vid.fps.num, param->dec_fmt.det.vid.fps.denum));
#endif

    start_encoding = true;
    start_decoding = true;

    return PJ_SUCCESS;
}

static pj_status_t enzo_h264_codec_close(pjmedia_vid_codec *codec)
{
    PJ_ASSERT_RETURN(codec, PJ_EINVAL);

#if defined(PJMEDIA_SAVE_ENCODE_VIDEO_STREAM_TO_FILE) && (PJMEDIA_SAVE_ENCODE_VIDEO_STREAM_TO_FILE == 1)
    FILE *encode_strm_file = ((enzo_h264_codec_data *)codec->codec_data)->encode_strm_file;
    if (encode_strm_file)
        fclose(encode_strm_file);
#endif

#if defined(PJMEDIA_SAVE_DECODE_VIDEO_STREAM_TO_FILE) && (PJMEDIA_SAVE_DECODE_VIDEO_STREAM_TO_FILE == 1)
    FILE *decode_strm_file = ((enzo_h264_codec_data *)codec->codec_data)->decode_strm_file;
    if (decode_strm_file)
        fclose(decode_strm_file);
#endif

    return PJ_SUCCESS;
}

static pj_status_t enzo_h264_codec_modify(pjmedia_vid_codec *codec,
                                          const pjmedia_vid_codec_param *param)
{
    PJ_LOG(4, (THIS_FILE, "Begin enzo_h264_codec_modify..."));

    PJ_ASSERT_RETURN(codec && param, PJ_EINVAL);

#if defined(ENZO_TEST_OPENH264) && (ENZO_TEST_OPENH264 == 1)
    if (param->dir & PJMEDIA_DIR_ENCODING) {
        struct enzo_h264_codec_data *enzo_h264_data = (enzo_h264_codec_data *)codec->codec_data;
        const pjmedia_format *fmt = &param->enc_fmt;
        SEncParamExt eprm;
        int ret = enzo_h264_data->enc->GetOption(ENCODER_OPTION_SVC_ENCODE_PARAM_EXT, &eprm);
        if (ret) {
            PJ_LOG(4, (THIS_FILE, "enzo_h264_codec_modify: GetOption failed with code %d", ret));
            return PJ_FAILED;
        }

        PJ_LOG(4, (THIS_FILE, "BEFORE: Width = %d, Height = %d, TargetBitrate = %d, MaxBitrate = %d,"
                              "MaxFrameRate = %f. AFTER: Width = %d, Height = %d, TargetBitrate = %d, MaxBitrate = %d,"
                              "MaxFrameRate = %d/%d", eprm.iPicWidth, eprm.iPicHeight, eprm.iTargetBitrate, eprm.iMaxBitrate,
                   eprm.fMaxFrameRate, fmt->det.vid.size.w, fmt->det.vid.size.h, fmt->det.vid.avg_bps, fmt->det.vid.max_bps,
                   fmt->det.vid.fps.num, fmt->det.vid.fps.denum));

        eprm.iPicWidth = fmt->det.vid.size.w;
        eprm.iPicHeight = fmt->det.vid.size.h;
        eprm.iTargetBitrate = fmt->det.vid.avg_bps;
        eprm.iMaxBitrate = fmt->det.vid.max_bps;
        eprm.fMaxFrameRate = (float)fmt->det.vid.fps.num / fmt->det.vid.fps.denum;

        eprm.sSpatialLayers[0].iVideoWidth = eprm.iPicWidth;
        eprm.sSpatialLayers[0].iVideoHeight = eprm.iPicHeight;
        eprm.sSpatialLayers[0].iSpatialBitrate = eprm.iTargetBitrate;
        eprm.sSpatialLayers[0].iMaxSpatialBitrate = eprm.iMaxBitrate;
        eprm.sSpatialLayers[0].fFrameRate = eprm.fMaxFrameRate;

        ret = enzo_h264_data->enc->SetOption(ENCODER_OPTION_SVC_ENCODE_PARAM_EXT, &eprm);
        if (ret) {
            PJ_LOG(4, (THIS_FILE, "enzo_h264_codec_modify: SetOption failed with code %d", ret));
            return PJ_FAILED;
        }

        enzo_h264_data->esrc_pic->iPicWidth	= fmt->det.vid.size.w;
        enzo_h264_data->esrc_pic->iPicHeight = fmt->det.vid.size.h;
        enzo_h264_data->esrc_pic->iStride[0] = enzo_h264_data->esrc_pic->iPicWidth;
        enzo_h264_data->esrc_pic->iStride[1] =
                enzo_h264_data->esrc_pic->iStride[2] =
                enzo_h264_data->esrc_pic->iStride[0] >> 1;
        enzo_h264_data->enc_input_size = enzo_h264_data->esrc_pic->iPicWidth *
                enzo_h264_data->esrc_pic->iPicHeight * 3 >> 1;

        return PJ_SUCCESS;
    }
    return PJ_EINVALIDOP;
#endif

    return PJ_SUCCESS;
}

static pj_status_t enzo_h264_codec_get_param(pjmedia_vid_codec *codec,
                                             pjmedia_vid_codec_param *param)
{
    PJ_LOG(4, (THIS_FILE, "Enzo codec getting parameters..."));

    PJ_ASSERT_RETURN(codec && param, PJ_EINVAL);

    struct enzo_h264_codec_data *enzo_h264_data;
    enzo_h264_data = (enzo_h264_codec_data*) codec->codec_data;
    pj_memcpy(param, enzo_h264_data->prm, sizeof(*param));

    return PJ_SUCCESS;
}

static pj_status_t enzo_h264_codec_encode_begin(pjmedia_vid_codec *codec,
                                                const pjmedia_vid_encode_opt *opt,
                                                const pjmedia_frame *input,
                                                unsigned out_size,
                                                pjmedia_frame *output,
                                                pj_bool_t *has_more)
{
    PJ_LOG(4, (THIS_FILE, "Enzo encode starting. input size = %d", input->size));

    PJ_ASSERT_RETURN(codec && input && out_size && output && has_more,
                     PJ_EINVAL);

#if defined(PJMEDIA_MEASURE_VIDEO_INFO) && (PJMEDIA_MEASURE_VIDEO_INFO == 1)
    pj_time_val start_encode;
    pj_gettimeofday(&start_encode);
    if (encoded_frames_counter == 0)
        pj_gettimeofday(&start_encoded_timer);
#endif

    struct enzo_h264_codec_data *enzo_h264_data;
    enzo_h264_data = (enzo_h264_codec_data*)codec->codec_data;

#if defined(ENZO_TEST_OPENH264) && (ENZO_TEST_OPENH264 == 1)
    PJ_ASSERT_RETURN(input->size == enzo_h264_data->enc_input_size,
                     PJMEDIA_CODEC_EFRMINLEN);

    pj_uint8_t *buf = 0;
    unsigned buf_size = 0, buf_pos = 0;

    enzo_h264_data->esrc_pic->pData[0] = (pj_uint8_t*)input->buf;
    enzo_h264_data->esrc_pic->pData[1] = enzo_h264_data->esrc_pic->pData[0] +
            (enzo_h264_data->esrc_pic->iPicWidth *
             enzo_h264_data->esrc_pic->iPicHeight);
    enzo_h264_data->esrc_pic->pData[2] = enzo_h264_data->esrc_pic->pData[1] +
            (enzo_h264_data->esrc_pic->iPicWidth *
             enzo_h264_data->esrc_pic->iPicHeight >> 2);

    pj_memset (&enzo_h264_data->bsi, 0, sizeof (SFrameBSInfo));
    int rc = enzo_h264_data->enc->EncodeFrame( enzo_h264_data->esrc_pic, &enzo_h264_data->bsi);
    if (rc != cmResultSuccess) {
        PJ_LOG(4, (THIS_FILE, "EncodeFrame() error, ret: %d", rc));
        return PJMEDIA_CODEC_EFAILED;
    }

    if (enzo_h264_data->bsi.eFrameType == videoFrameTypeSkip ||
            enzo_h264_data->bsi.eFrameType == videoFrameTypeInvalid) {
        PJ_LOG(5, (THIS_FILE, "Skip or invalid frame: %d", (int)enzo_h264_data->bsi.eFrameType));
        output->size = 0;
        output->type = PJMEDIA_FRAME_TYPE_NONE;
        output->timestamp = input->timestamp;
        output->bit_info = 0;
        return PJ_SUCCESS;
    }

    enzo_h264_data->enc_frame_size = enzo_h264_data->enc_processed = enzo_h264_data->fua_processed = 0;

    buf = enzo_h264_data->enc_buf;
    buf_size = enzo_h264_data->enc_buf_size;
    buf_pos = 0;

    enzo_h264_codec_encode_write_pacsi(codec, NULL, buf, buf_size, &buf_pos, &output->bit_info);
    enzo_h264_codec_encode_write_nals(codec, NULL, buf, buf_size, &buf_pos);

    enzo_h264_data->enc_buf_len = buf_pos;
#else
    int ret = 0;
    struct mediaBuffer *yuvData, *avcData;
    yuvData = (struct mediaBuffer *)calloc(1, sizeof(struct mediaBuffer));
    avcData = (struct mediaBuffer *)calloc(1, sizeof(struct mediaBuffer));

#if defined(ENZO_TEST_CAM) && (ENZO_TEST_CAM == 1)
    struct mediaBuffer *camData;
    camData = (struct mediaBuffer *)calloc(1, sizeof(struct mediaBuffer));

    /* Get MJPG frame from the camera */
    ret = cameraGetFrame(enzo_h264_data->usb_cam, camData);
    if (ret < 0) {
        PJ_LOG(4, (THIS_FILE, "[Enzo Error]: failed to get frame from the camera."));

        if (yuvData)
            free(yuvData);
        if (avcData)
            free(avcData);
        if (camData)
            free(camData);

        return PJ_FAILED;
    }

    /* Convert MJPG data to YUV data */
    ret = decoderDecodeFrame(enzo_h264_data->mjpg_dec, camData, yuvData);
    if (ret < 0) {
        PJ_LOG(4, (THIS_FILE, "[Enzo Error]: failed to convert data from mjpg to yuv."));

        if (yuvData)
            free(yuvData);
        if (avcData)
            free(avcData);
        if (camData)
            free(camData);

        return PJ_FAILED;
    }

    if (camData)
        free(camData);
#else
    yuvData->vBufOut = (unsigned char*)input->buf;
    yuvData->bufOutSize = input->size;
    yuvData->dataSource = BUFFER;
    yuvData->colorSpace = YUV420P;
#endif

    /* Encode frame */
    ret = encoderEncodeFrame(enzo_h264_data->avc_enc, yuvData, avcData);
    if (ret < 0) {
        PJ_LOG(4, (THIS_FILE, "[Enzo Error]: failed to encode frame."));
        return PJ_FAILED;
    }

    PJ_LOG(4, (THIS_FILE, "Enzo encode successful. Frame width = %d, frame height = %d, frame type = %d, "
                          "nalNumber = %d, nalLength = %d, nalType = %d, avcData->bufOutSize = %d",
               avcData->imageWidth, avcData->imageHeight,
               avcData->frameType, avcData->nalInfo.nalNumber,
               avcData->nalInfo.nalLength, avcData->nalInfo.nalType, avcData->bufOutSize));

#if defined(PJMEDIA_SAVE_ENCODE_VIDEO_STREAM_TO_FILE) && (PJMEDIA_SAVE_ENCODE_VIDEO_STREAM_TO_FILE == 1)
    if (enzo_h264_data->encode_strm_file)
        fwrite(avcData->vBufOut, avcData->bufOutSize, 1, enzo_h264_data->encode_strm_file);
#endif

#if defined(PJMEDIA_MEASURE_VIDEO_INFO) && (PJMEDIA_MEASURE_VIDEO_INFO == 1)
    pj_time_val current_time;
    pj_gettimeofday(&current_time);
    PJ_TIME_VAL_SUB(current_time, start_encoded_timer);

    encoded_frames_counter++;

    // Trace the encode information here to reduce logs
    if (start_encoding || (current_time.sec > COUNTER_INTERVAL)) {
        if (start_encoding)
            start_encoding = false;
        else
            PJ_LOG(1, (THIS_FILE, "[VIDEO_MEASUREMENT]: {Stream} Encoded frames in %d(s) are: %d, codec = %p",
                       COUNTER_INTERVAL, encoded_frames_counter, codec));

#if defined(VIDEO_INFO_FULL_LOG) && (VIDEO_INFO_FULL_LOG != 1)
        pj_time_val stop_encode;
        pj_gettimeofday(&stop_encode);
        PJ_TIME_VAL_SUB(stop_encode, start_encode);
        // Convert to msec
        pj_uint32_t msec = PJ_TIME_VAL_MSEC(stop_encode);
        PJ_LOG(1, (THIS_FILE, "[VIDEO_MEASUREMENT]: Frame is encoded in %d(ms),"
                              " width = %d, height = %d, frame type = %d, codec = %p", msec,
                   avcData->width, avcData->height, avcData->frameType, codec));
#endif
        // Reset encoded frames counter
        encoded_frames_counter = 0;
    }

#if defined(VIDEO_INFO_FULL_LOG) && (VIDEO_INFO_FULL_LOG == 1)
    pj_time_val stop_encode;
    pj_gettimeofday(&stop_encode);
    PJ_TIME_VAL_SUB(stop_encode, start_encode);
    // Convert to msec
    pj_uint32_t msec = PJ_TIME_VAL_MSEC(stop_encode);
    PJ_LOG(1, (THIS_FILE, "[VIDEO_MEASUREMENT]: Frame is encoded in %d(ms),"
                          " width = %d, height = %d, frame type = %d, encoded size = %d, codec = %p", msec,
               avcData->imageWidth, avcData->imageHeight, avcData->frameType, avcData->bufOutSize, codec));
#endif
#endif

    pj_uint8_t *buf = 0;
    unsigned buf_size = 0, buf_pos = 0;
    buf = enzo_h264_data->enc_buf;
    buf_size = enzo_h264_data->enc_buf_size;
    buf_pos = 0;

    enzo_h264_data->enc_frame_size = enzo_h264_data->enc_processed = enzo_h264_data->fua_processed = 0;

    enzo_h264_codec_encode_write_pacsi(codec, avcData, buf, buf_size, &buf_pos, &output->bit_info);
    enzo_h264_codec_encode_write_nals(codec, avcData, buf, buf_size, &buf_pos);

    enzo_h264_data->enc_buf_len = buf_pos;

    if (yuvData)
        free(yuvData);

    if (avcData)
        free(avcData);
#endif

    return enzo_h264_codec_encode_more(codec, out_size, output, has_more);
}

static pj_status_t enzo_h264_codec_encode_more(pjmedia_vid_codec *codec,
                                               unsigned out_size,
                                               pjmedia_frame *output,
                                               pj_bool_t *has_more)
{
    PJ_LOG(4, (THIS_FILE, "Begin enzo_h264_codec_encode_more..."));

    PJ_ASSERT_RETURN(codec && out_size && output && has_more,
                     PJ_EINVAL);

    struct enzo_h264_codec_data *enzo_h264_data;
    enzo_h264_data = (enzo_h264_codec_data*)codec->codec_data;
    int mtu = pjmedia_h264_packetizer_mtu(enzo_h264_data->pktz);

    pj_status_t status;
    unsigned out_len = 0;

    if (enzo_h264_data->enc_processed < enzo_h264_data->enc_frame_size) {
        /* We have outstanding frame in packetizer */
        status = pjmedia_enzo_h264_packetize(enzo_h264_data->enc_buf + enzo_h264_data->enc_processed,
                                             enzo_h264_data->enc_buf_len - enzo_h264_data->enc_processed,
                                             mtu,
                                             PJ_FALSE,
                                             &enzo_h264_data->enc_processed,
                                             &enzo_h264_data->fua_processed,
                                             (pj_uint8_t *)output->buf,
                                             out_size,
                                             &out_len);
        output->size = out_len;
        if (status != PJ_SUCCESS) {
            /* Reset */
            enzo_h264_data->enc_frame_size = enzo_h264_data->enc_processed = enzo_h264_data->fua_processed = 0;
            *has_more = PJ_FAILED;

            PJ_PERROR(4, (THIS_FILE, status, "pjmedia_enzo_h264_packetize() error [1]"));
            return status;
        }

        output->type = PJMEDIA_FRAME_TYPE_VIDEO;
        *has_more = (enzo_h264_data->enc_processed < enzo_h264_data->enc_frame_size) /*||
                                                                                                                                                                                                       (enzo_h264_data->ilayer < enzo_h264_data->bsi.iLayerNum)*/;
        return PJ_SUCCESS;
    }

    enzo_h264_data->enc_frame_size = enzo_h264_data->enc_buf_len;
    enzo_h264_data->enc_processed = 0;
    enzo_h264_data->fua_processed = 0;

    status = pjmedia_enzo_h264_packetize(enzo_h264_data->enc_buf,
                                         enzo_h264_data->enc_buf_len,
                                         mtu,
                                         (output->bit_info & PJMEDIA_VID_FRM_KEYFRAME) ? PJ_TRUE : PJ_FALSE,
                                         &enzo_h264_data->enc_processed,
                                         &enzo_h264_data->fua_processed,
                                         (pj_uint8_t *)output->buf,
                                         out_size,
                                         &out_len);
    output->size = out_len;

    if (status != PJ_SUCCESS) {
        /* Reset */
        enzo_h264_data->enc_frame_size = enzo_h264_data->enc_processed = enzo_h264_data->fua_processed = 0;
        *has_more = PJ_FAILED; /* (enzo_h264_data->ilayer < enzo_h264_data->bsi.iLayerNum); */

        PJ_PERROR(4, (THIS_FILE, status, "pjmedia_enzo_h264_packetize() error [2]"));
        return status;
    }

    output->type = PJMEDIA_FRAME_TYPE_VIDEO;

    *has_more = (enzo_h264_data->enc_processed < enzo_h264_data->enc_frame_size);

    PJ_LOG(4, (THIS_FILE, "End enzo_h264_codec_encode_more. out_size = %d", out_size));

    return PJ_SUCCESS;
}

static pj_status_t enzo_h264_codec_encode_write_nals(pjmedia_vid_codec *codec,
                                                     #if defined(ENZO_TEST_LINUX) && (ENZO_TEST_LINUX == 1)
                                                     #else
                                                     mediaBuffer *avc_data,
                                                     #endif
                                                     pj_uint8_t *buf,
                                                     unsigned buf_size,
                                                     unsigned *buf_pos)
{
    struct enzo_h264_codec_data *enzo_h264_data;
    enzo_h264_data = (struct enzo_h264_codec_data *)codec->codec_data;

    pj_uint8_t *p, *p_end, *q;
    int i, j, len;
    int layer_number = 1;

#if defined(ENZO_TEST_OPENH264) && (ENZO_TEST_OPENH264 == 1)
    SFrameBSInfo *bsi;
    SLayerBSInfo *lsi;
    bsi = &enzo_h264_data->bsi;
    layer_number = bsi->iLayerNum;
#endif

    p = buf + *buf_pos;
    p_end = buf + buf_size;

    for (i = 0; i < layer_number; ++i) {
#if defined(ENZO_TEST_OPENH264) && (ENZO_TEST_OPENH264 == 1)
        lsi = &bsi->sLayerInfo[i];
        q = lsi->pBsBuf;
#else
        q = avc_data->vBufOut;
#endif
        if (*(q + 3) != 1) {
            int trap = 1;
            trap++;
        }

        int nal_count = 1;
#if defined(ENZO_TEST_OPENH264) && (ENZO_TEST_OPENH264 == 1)
        nal_count = lsi->iNalCount;
#endif

        for (j = 0; j < nal_count; ++j) {
#if defined(ENZO_TEST_OPENH264) && (ENZO_TEST_OPENH264 == 1)
            len = lsi->pNalLengthInByte[j] - 4;
#else
            len = avc_data->bufOutSize - 4;
#endif
            *p++ = (len >> 8) & 0xFF;
            *p++ = len & 0xFF;
            pj_memcpy(p, q + 4, len);

#if defined(ENZO_TEST_OPENH264) && (ENZO_TEST_OPENH264 == 1)
            q += lsi->pNalLengthInByte[j];
#else
            q += avc_data->bufOutSize;
#endif
            p += len;
        }
    }

    *buf_pos = p - buf;

    return (p <= p_end ? PJ_SUCCESS : PJ_FAILED);
}

static pj_status_t enzo_h264_codec_encode_write_pacsi(pjmedia_vid_codec *codec,
                                                      #if defined(ENZO_TEST_LINUX) && (ENZO_TEST_LINUX == 1)
                                                      #else
                                                      mediaBuffer *avc_data,
                                                      #endif
                                                      pj_uint8_t *buf,
                                                      unsigned buf_size,
                                                      unsigned *buf_pos,
                                                      unsigned *bit_flags)
{
    struct enzo_h264_codec_data *enzo_h264_data;
    enzo_h264_data = (struct enzo_h264_codec_data *)codec->codec_data;

    pj_uint8_t *p_start,*p, *p_end, *q;
    pj_uint8_t num_nals;
    struct enzo_h264_pacsi_hdr pacsi_hdr = {0};

    pj_uint16_t donc;
    pj_uint8_t rfc;

    pj_uint16_t w, h;
    pj_uint32_t bitrate;
    pj_uint8_t tmp;
    pj_uint8_t *len_pos;
    int len;

    p = buf + *buf_pos;
    p_start = p;
    p_end = buf + buf_size;

    /* Find number of NALs */
    num_nals = 0;
    int i, j, layer_number = 1;

#if defined(ENZO_TEST_OPENH264) && (ENZO_TEST_OPENH264 == 1)
    SFrameBSInfo *bsi;
    SLayerBSInfo *lsi;
    bsi = &enzo_h264_data->bsi;
    layer_number = bsi->iLayerNum;
#endif

    for (i = 0; i < layer_number; ++i) {
        int nal_count = 1;
#if defined(ENZO_TEST_OPENH264) && (ENZO_TEST_OPENH264 == 1)
        lsi = &bsi->sLayerInfo[i];
        nal_count = lsi->iNalCount;
        q = lsi->pBsBuf + 4;
#else
        q = avc_data->vBufOut + 4;
#endif
        num_nals += nal_count;
        for (j = 0; j < nal_count; ++j) {
            pacsi_hdr.f = (*q >> 7) ? 1 : pacsi_hdr.f;
            tmp = (*q >> 5) & 0x03;
            if (pacsi_hdr.nri < tmp) pacsi_hdr.nri = tmp;
#if defined(ENZO_TEST_OPENH264) && (ENZO_TEST_OPENH264 == 1)
            q += lsi->pNalLengthInByte[j];
#else
            q += avc_data->bufOutSize;
#endif
        }

#if defined(ENZO_TEST_OPENH264) && (ENZO_TEST_OPENH264 == 1)
        pacsi_hdr.prid = lsi->uiTemporalId;
        pacsi_hdr.tid = lsi->uiTemporalId;
#else
        pacsi_hdr.prid = 0;
        pacsi_hdr.tid = 0;
#endif
    }

#if defined(ENZO_TEST_OPENH264) && (ENZO_TEST_OPENH264 == 1)
    pacsi_hdr.i = (bsi->eFrameType == videoFrameTypeIDR) ? 1 : 0;  /* IDR flag */
#else
    pacsi_hdr.i = 1;
#endif

    if (pacsi_hdr.i)
        *bit_flags |= PJMEDIA_VID_FRM_KEYFRAME;
    if (pacsi_hdr.prid)
        *bit_flags |= PJMEDIA_VID_FRM_PRID_TID;

    /* Mark where to write len */
    len_pos = p;
    p += 2;

    pacsi_hdr.type = 30;  /* PACSI NAL type */
    pacsi_hdr.r = 1;
    pacsi_hdr.n = 1;
    pacsi_hdr.o = 1;
    pacsi_hdr.rr = 3;
    pacsi_hdr.t = 1;      /* mark for DONC */
    pacsi_hdr.s = 1;

    pj_memcpy(p, &pacsi_hdr, sizeof(pacsi_hdr));
    p += sizeof(pacsi_hdr);

    if (pacsi_hdr.t) {
        donc = ++enzo_h264_data->donc;
        p += enzo_h264_write_s(p, donc);
    }

    /* Write Stream Layout SEI message */
    if (pacsi_hdr.i) {
        struct enzo_h264_sei_hdr hdr = {
#if defined(PJ_IS_BIG_ENDIAN) && (PJ_IS_BIG_ENDIAN != 0)
            0,  /* f */
            0,  /* nri */
            6,  /* type */
#else  /* Litle endian */
            6,  /* type */
            0,  /* nri */
            0,  /* f */
#endif
            5,                /* payload type - pt */
            29 + 16 * 2 - 3,  /* payload size - ps */
        { 0x13,0x9F,0xB1,0xA9,0x44,0x6A,0x4D,0xEC,0x8C,0xBF,0x65,0xB1,0xE1,0x2D,0x2C,0xFD } /* stream layout UUID */
    };
        const pj_uint8_t ldp[] = { 3,0,0,0,0,0,0,0 };
        struct enzo_h264_layer_desc ld = {0};

        /* Write NAL unit size */
        p += enzo_h264_write_s(p, 29 + 16 * 2);
        /* Write SEI message header */
        pj_memcpy(p, &hdr, sizeof(hdr));
        p += sizeof(hdr);
        /* Write layer presence byte (LPB) */
        pj_memcpy(p, ldp, sizeof(ldp));
        p += sizeof(ldp);
        /* Write layer presence flag */
        *p++ = 0x01;  /* P = 1 */

        *p++ = 16;  /* LDSize = 16 */

        w = enzo_h264_data->prm->enc_fmt.det.vid.size.w;
        h = enzo_h264_data->prm->enc_fmt.det.vid.size.h;
        bitrate = enzo_h264_data->prm->enc_fmt.det.vid.avg_bps;

        ld.coded_width = pj_htons(w);
        ld.coded_height = pj_htons(h);
        ld.disp_width = pj_htons(w);
        ld.disp_height = pj_htons(h);
        ld.bitrate = pj_htonl(bitrate);
        ld.fpsidx = 0;
        ld.lt = 0;      /* base layer */
        ld.prid = 0;
        ld.cb = 1;
        pj_memcpy(p, &ld, sizeof(ld));
        p += sizeof(ld);

        bitrate /= 4;

        ld.bitrate = pj_htonl(bitrate);
        ld.fpsidx = 2;
        ld.lt = 1;      /* temporal layer */
        ld.prid = 1;
        ld.cb = 1;
        pj_memcpy(p, &ld, sizeof(ld));
        p += sizeof(ld);
    }

    /* Write Bitstream SEI message */
    {
        struct enzo_h264_sei_hdr hdr = {
#if defined(PJ_IS_BIG_ENDIAN) && (PJ_IS_BIG_ENDIAN != 0)
            0,  /* f */
            0,  /* nri */
            6,  /* type */
#else
            6,  /* type */
            0,  /* nri */
            0,  /* f */
#endif
            5,  /* pt */
            18, /* pt */
        { 0x05,0xFB,0xC6,0xB9,0x5A,0x80,0x40,0xE5,0xA2,0x2A,0xAB,0x40,0x20,0x26,0x7E,0x26 },  /* uuid */
    };

        p += enzo_h264_write_s(p, 21);  /* NAL unit size */
        /* Write SEI message header */
        pj_memcpy(p, &hdr, sizeof(hdr));
        p += sizeof(hdr);
        /* Write ref_frm_cnt */
        if (pacsi_hdr.tid) rfc = enzo_h264_data->ref_frm_cnt;
        else     rfc = ++enzo_h264_data->ref_frm_cnt;
        *p++ = rfc;
        /* Write NAL count */
        *p++ = num_nals;
    }

    *buf_pos = p - buf;

    /* Write length */
    len = p - p_start - 2;
    enzo_h264_write_s(len_pos, len);

    return (p <= p_end ? PJ_SUCCESS : PJ_FAILED);
}

static pj_status_t enzo_h264_codec_decode(pjmedia_vid_codec *codec,
                                          pj_size_t count,
                                          pjmedia_frame packets[],
                                          unsigned out_size,
                                          pjmedia_frame *output)
{
#if defined(ENZO_TEST_LINUX) && (ENZO_TEST_LINUX == 1)
    return PJ_SUCCESS;
#else
#if defined(PJMEDIA_MEASURE_VIDEO_INFO) && (PJMEDIA_MEASURE_VIDEO_INFO == 1)
    if (start_decoding)
        PJ_LOG(4, (THIS_FILE, "Enzo decode starting..."));

    pj_time_val start_decode;
    pj_gettimeofday(&start_decode);
    if (decoded_frames_counter == 0)
        pj_gettimeofday(&start_decoded_timer);
#endif

    struct enzo_h264_codec_data *enzo_h264_data;
    const pj_uint8_t nal_start[] = { 0, 0, 1 };
    pj_bool_t has_frame = PJ_FALSE;
    unsigned buf_pos;
    unsigned i;
    int frm_cnt, j;
    pj_status_t status = PJ_SUCCESS;
    pj_uint8_t *start;
    pj_size_t start_len;
    unsigned layer_prid;

    PJ_ASSERT_RETURN(codec && count && packets && out_size && output,
                     PJ_EINVAL);
    PJ_ASSERT_RETURN(output->buf, PJ_EINVAL);

    enzo_h264_data = (enzo_h264_codec_data*)codec->codec_data;
    layer_prid = enzo_h264_data->decoded_prid[0];
    /*
     * Step 1: unpacketize the packets/frames
     */
    enzo_h264_data->dec_buf_len = 0;
    if (enzo_h264_data->whole) {
        for (i=0; i<count; ++i) {
            if (enzo_h264_data->dec_buf_len + packets[i].size > enzo_h264_data->dec_buf_size) {
                PJ_LOG(4, (THIS_FILE, "Decoding buffer overflow [1]"));
                return PJMEDIA_CODEC_EFRMTOOSHORT;
            }

            pj_memcpy( enzo_h264_data->dec_buf + enzo_h264_data->dec_buf_len,
                       (pj_uint8_t*)packets[i].buf,
                       packets[i].size);
            enzo_h264_data->dec_buf_len += packets[i].size;
        }
    } else {
        for (i=0; i<count; ++i) {
            if (enzo_h264_data->dec_buf_len + packets[i].size + sizeof(nal_start) >
                    enzo_h264_data->dec_buf_size) {
                PJ_LOG(4, (THIS_FILE, "Decoding buffer overflow [1]"));
                return PJMEDIA_CODEC_EFRMTOOSHORT;
            }

            status = pjmedia_enzo_h264_unpacketize(enzo_h264_data,
                                                   (pj_uint8_t *)packets[i].buf,
                                                   packets[i].size,
                                                   enzo_h264_data->dec_buf,
                                                   enzo_h264_data->dec_buf_size,
                                                   &enzo_h264_data->dec_buf_len,
                                                   &output->bit_info,
                                                   &layer_prid);
            if (status != PJ_SUCCESS) {
                PJ_PERROR(4, (THIS_FILE, status, "Unpacketize error"));
                continue;
            }
        }
    }

    if (layer_prid != enzo_h264_data->decoded_prid[0] &&
            layer_prid != enzo_h264_data->decoded_prid[1]) {
        if (enzo_h264_data->decoded_prid[0] == INVALID_PRID)
            enzo_h264_data->decoded_prid[0] = layer_prid;
        else if (enzo_h264_data->decoded_prid[1] == INVALID_PRID)
            enzo_h264_data->decoded_prid[1] = layer_prid;
        else {
            PJ_LOG(4, (THIS_FILE, "Not a valid PRID %d (valid is %d, %d)",
                       layer_prid,
                       enzo_h264_data->decoded_prid[0],
                   enzo_h264_data->decoded_prid[1]));
            return PJMEDIA_CODEC_EBADPRID;
        }
    }

    if (enzo_h264_data->dec_buf_len + sizeof(nal_start) > enzo_h264_data->dec_buf_size) {
        PJ_LOG(4, (THIS_FILE, "Decoding buffer overflow [2]"));
        return PJMEDIA_CODEC_EFRMTOOSHORT;
    }

    /* Dummy NAL sentinel */
    pj_memcpy( enzo_h264_data->dec_buf + enzo_h264_data->dec_buf_len, nal_start, sizeof(nal_start));
    if (enzo_h264_data->wait_to_finish_fu_a) {
        return PJ_SUCCESS;
    }

    /*
     * Step 2: parse the individual NAL and give to decoder
     */
    buf_pos = 0;
    for (frm_cnt=0; ; ++frm_cnt) {
        unsigned frm_size;
        for (i = 0; buf_pos + i < enzo_h264_data->dec_buf_len; i++) {
            if (enzo_h264_data->dec_buf[buf_pos + i] == 0 &&
                    enzo_h264_data->dec_buf[buf_pos + i + 1] == 0 &&
                    enzo_h264_data->dec_buf[buf_pos + i + 2] == 1 &&
                    i > 1) {
                break;
            }
        }

        frm_size = i;
        if (buf_pos + frm_size >= enzo_h264_data->dec_buf_len) {
            frm_cnt++;
            break;
        }
        buf_pos += frm_size;
    }

    start = enzo_h264_data->dec_buf;
    start_len = enzo_h264_data->dec_buf_len;
    if (frm_cnt > enzo_h264_data->num_of_nal_units) {
        /* Remove old NALs */
        buf_pos = 0;
        unsigned frm_size;
        for (j = 0; j < frm_cnt - enzo_h264_data->num_of_nal_units; ++j) {
            for (i = 0; buf_pos + i < enzo_h264_data->dec_buf_len; i++) {
                if (enzo_h264_data->dec_buf[buf_pos + i] == 0 &&
                        enzo_h264_data->dec_buf[buf_pos + i + 1] == 0 &&
                        enzo_h264_data->dec_buf[buf_pos + i + 2] == 1 &&
                        i > 1) {
                    break;
                }
            }
            frm_size = i;
            start += frm_size;
            start_len -= frm_size;
            buf_pos += frm_size;
        }
        frm_cnt = enzo_h264_data->num_of_nal_units;
    }

#if defined(PJMEDIA_SAVE_DECODE_VIDEO_STREAM_TO_FILE) && (PJMEDIA_SAVE_DECODE_VIDEO_STREAM_TO_FILE == 1)
    if (enzo_h264_data->decode_strm_file)
        fwrite(start, start_len, 1, enzo_h264_data->decode_strm_file);
#endif

    struct mediaBuffer *avcData = NULL;
    struct mediaBuffer *yuvData = NULL;
    if (frm_cnt == enzo_h264_data->num_of_nal_units) {
        yuvData = (struct mediaBuffer *)calloc(1, sizeof(struct mediaBuffer));
        avcData = (struct mediaBuffer *)calloc(1, sizeof(struct mediaBuffer));
        avcData->vBufOut = (unsigned char*)start;
        avcData->pBufOut = NULL;
        avcData->dataType = H264AVC;
        avcData->dataSource = BUFFER;
        avcData->bufOutSize = start_len;
        PJ_LOG(4, (THIS_FILE, "Ready for Enzo decode. Data = %p, data length = %d, "
                              "yuvData->bufOutSize = %d", start, start_len, yuvData->bufOutSize));
        int ret = decoderDecodeFrame(enzo_h264_data->avc_dec, avcData, yuvData);
        if (ret < 0) {
            PJ_LOG(4, (THIS_FILE, "[Enzo Error]: failed to decode data."));
            if (yuvData)
                free(yuvData);
            if (avcData)
                free(avcData);
            return PJ_FAILED;
        }
        if (yuvData->bufOutSize > 0) {
            output->timestamp = packets[0].timestamp;
            output->size = yuvData->bufOutSize;
            output->type = PJMEDIA_FRAME_TYPE_VIDEO;
            output->buf = yuvData->vBufOut;
            has_frame = PJ_TRUE;
        } else {
            /* Buffer is damaged, reset size */
            output->size = 0;
        }

        PJ_LOG(4, (THIS_FILE, "Enzo decode successfully. yuvData->bufOutSize = %d", yuvData->bufOutSize));
    } else {
        PJ_LOG(4, (THIS_FILE, "[Enzo Error]: Enzo can not decode video data."));
    }

    if (!has_frame) {
        PJ_LOG(4, (THIS_FILE, "[Enzo Error]: decode couldn't produce picture, "
                              "input nframes=%d, concatenated size=%d bytes",
                   count, enzo_h264_data->dec_buf_len));

        output->type = PJMEDIA_FRAME_TYPE_NONE;
        output->size = 0;
        output->timestamp.u64 = 0;  // packets[0].timestamp;
        output->bit_info = 0;

#if defined(PJMEDIA_MEASURE_VIDEO_INFO) && (PJMEDIA_MEASURE_VIDEO_INFO == 1)
        // Increase lost decoded frames
        lost_decoded_frames_counter++;
#endif
    } else {
#if defined(PJMEDIA_MEASURE_VIDEO_INFO) && (PJMEDIA_MEASURE_VIDEO_INFO == 1)
        // Increase decoded frames
        decoded_frames_counter++;
#endif
    }

    /* Count decoded frames in the interval time */
#if defined(PJMEDIA_MEASURE_VIDEO_INFO) && (PJMEDIA_MEASURE_VIDEO_INFO == 1)
    pj_time_val current_time;
    pj_gettimeofday(&current_time);
    PJ_TIME_VAL_SUB(current_time, start_decoded_timer);

    if (start_decoding || (current_time.sec > COUNTER_INTERVAL)) {
        if (start_decoding) {
            if (has_frame)
                start_decoding = false;
        } else
            PJ_LOG(1, (THIS_FILE, "[VIDEO_MEASUREMENT]: Decoded frames in %d(s) are: %d, lost %d, codec = %p",
                       COUNTER_INTERVAL, decoded_frames_counter, lost_decoded_frames_counter, codec));

        // Reset parameters
        decoded_frames_counter = 0;
        lost_decoded_frames_counter = 0;

#if defined(VIDEO_INFO_FULL_LOG) && (VIDEO_INFO_FULL_LOG != 1)
        // Trace the decode information here to reduce logs
        if (has_frame && avcData) {
            pj_time_val stop_decode;
            pj_gettimeofday(&stop_decode);

            PJ_TIME_VAL_SUB(stop_decode, start_decode);
            // Convert to msec
            pj_uint32_t msec = PJ_TIME_VAL_MSEC(stop_decode);
            PJ_LOG(1, (THIS_FILE, "[VIDEO_MEASUREMENT]: Frame is decoded in %d(ms), "
                                  "width = %d, height = %d, frame type = %d, codec = %p", msec,
                       yuvData->imageWidth, yuvData->imageHeight, yuvData->frameType, codec));
        }
#endif
    }

#if defined(VIDEO_INFO_FULL_LOG) && (VIDEO_INFO_FULL_LOG == 1)
    if (has_frame && avcData) {
        pj_time_val stop_decode;
        pj_gettimeofday(&stop_decode);

        PJ_TIME_VAL_SUB(stop_decode, start_decode);
        // Convert to msec
        pj_uint32_t msec = PJ_TIME_VAL_MSEC(stop_decode);
        PJ_LOG(1, (THIS_FILE, "[VIDEO_MEASUREMENT]: Frame is decoded in %d(ms), "
                              "width = %d, height = %d, frame type = %d, size = %d, codec = %p", msec,
                   yuvData->imageWidth, yuvData->imageHeight, yuvData->frameType, start_len, codec));
    }
#endif
#endif

    if (avcData)
        free(avcData);

    if (yuvData)
        free(yuvData);

    return has_frame ? PJ_SUCCESS : PJ_FAILED;
#endif
}

static pj_status_t enzo_h264_codec_apply_bandwidth(
        const pjmedia_vid_codec *codec,
        unsigned bandwidth,
        unsigned maxsize,
        pjmedia_format *fmt)
{
    int i;
    unsigned minbr, maxbr;
    int idx = -1;
    unsigned cfg_min_size;

    /* Find current video config */
    for (i = 0; i < avai_vid_res_cnt; ++i) {
        if (avai_vid_res[i].width == fmt->det.vid.size.w &&
                avai_vid_res[i].height == fmt->det.vid.size.h) {
            idx = i;
            break;
        }
    }
    if (idx < 0)
        idx = 0;

    if (bandwidth > 0) {
        minbr = avai_vid_res[idx].min_br;
        maxbr = avai_vid_res[idx].max_br;
        if (bandwidth > maxbr) {
            while (idx < avai_vid_res_cnt - 1) {
                idx++;
                minbr = avai_vid_res[idx].min_br;
                maxbr = avai_vid_res[idx].max_br;
                if (minbr <= bandwidth && bandwidth <= maxbr)
                    break;
            }
        } else if (bandwidth < minbr) {
            while (idx > 0) {
                idx--;
                minbr = avai_vid_res[idx].min_br;
                maxbr = avai_vid_res[idx].max_br;
                if (minbr <= bandwidth && bandwidth <= maxbr)
                    break;
            }
        }
    }

    if (maxsize > 0) {
        cfg_min_size = PJ_MIN(avai_vid_res[idx].width, avai_vid_res[idx].height);
        while ((cfg_min_size > maxsize) && (idx > 0)) {
            idx--;
            cfg_min_size = PJ_MIN(avai_vid_res[idx].width, avai_vid_res[idx].height);
        }
    }

    fmt->det.vid.size.w = avai_vid_res[idx].width;
    fmt->det.vid.size.h = avai_vid_res[idx].height;
    fmt->det.vid.fps = avai_vid_res[idx].fps;
    if (bandwidth > 0 && bandwidth < avai_vid_res[idx].max_br)
        fmt->det.vid.avg_bps = bandwidth;
    else
        fmt->det.vid.avg_bps = (avai_vid_res[idx].min_br + avai_vid_res[idx].max_br) / 2;
    fmt->det.vid.max_bps = avai_vid_res[idx].max_br;

    return PJ_SUCCESS;
}

#if defined(PJ_CONFIG_ANDROID) && PJ_CONFIG_ANDROID
#ifdef __cplusplus
extern "C" {
#endif
JNIEXPORT void JNICALL Java_com_me_corestack_wrapper_SipCallWrapper_changeEncodeVideoFormat(
        JNIEnv *env, jobject obj, jint as)
{
    /*if (as) {
        avai_vid_res = avai_vid_res_3x4;
        avai_vid_res_cnt = avai_vid_res_3x4_cnt;
    } else {
        avai_vid_res = avai_vid_res_4x3;
        avai_vid_res_cnt = avai_vid_res_4x3_cnt;
    }*/
}
#ifdef __cplusplus
}
#endif
#endif  // PJ_CONFIG_ANDROID

#endif	/* PJMEDIA_HAS_ENZO_H264_CODEC */
