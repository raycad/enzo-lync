#include <pjmedia-codec/enzo_h264_packetizer.h>
#include <pj/log.h>
#include <pj/string.h>

#if defined(PJMEDIA_HAS_ENZO_H264_CODEC) && \
            PJMEDIA_HAS_ENZO_H264_CODEC == 1 && \
    defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO == 1)

#define THIS_FILE "enzo_h264_packetizer.c"

#define TRACE_(x) PJ_LOG(5, x)

#ifdef MAX_NAL_UNITS_IN_LAYER
#define MAX_NALS MAX_NAL_UNITS_IN_LAYER
#else
#define MAX_NALS 128
#endif

/* Enumeration of H.264 NAL unit types */
enum
{
    NAL_TYPE_SINGLE_NAL_MIN = 1,
    NAL_TYPE_SINGLE_NAL_MAX = 23,
    NAL_TYPE_STAP_A         = 24,
    NAL_TYPE_FU_A           = 28,
    NAL_TYPE_PACSI          = 30,
};

static void unpacketize_pacsi_debug(
        const pj_uint8_t *payload,
        pj_size_t payload_len,
        const char *trace_group)
{
#if defined(PJMEDIA_LOG_VIDEO_DETAILS_INFO) && (PJMEDIA_LOG_VIDEO_DETAILS_INFO == 1)
    /* Payload Content Scalability Information (PACSI) NAL Unit */
    struct enzo_h264_pacsi_hdr psi_hdr = {0};
    int off = 0;
    pj_uint8_t TL0PICIDX;
    pj_uint16_t IDRPICID, DONC, nal_unit_size;
    const pj_uint8_t *q, *q_end;

    /* Get PACSI header */
    pj_memcpy(&psi_hdr, payload, sizeof(psi_hdr));
    off += sizeof(psi_hdr);

    PJ_LOG(1, (trace_group, "PACSI unit header: R=%u, I=%u, PRID=%u, N=%u, DID=%u, "
               "QID=%u, TID=%u, U=%u, D=%u, O=%u, RR=%u "
               "X=%u, Y=%u, T=%u, A=%u, P=%u, C=%u, S=%u, E=%u",
               psi_hdr.r, psi_hdr.i, psi_hdr.prid,
               psi_hdr.n, psi_hdr.did, psi_hdr.qid,
               psi_hdr.tid, psi_hdr.u, psi_hdr.d, psi_hdr.o, psi_hdr.rr,
               psi_hdr.x, psi_hdr.y, psi_hdr.t, psi_hdr.a, psi_hdr.p, psi_hdr.c, psi_hdr.s, psi_hdr.e));

    if (psi_hdr.y) {
        TL0PICIDX = *(payload + off);
        off++;
        IDRPICID = pj_ntohs(*((pj_uint16_t *)(payload + off)));  // (*(payload + off) << 8) | *(payload + off + 1);
        off += 2;

        PJ_LOG(1, (trace_group, "TL0PICIDX = %u, IDRPICID = %u",
                   TL0PICIDX, IDRPICID));
    }
    if (psi_hdr.t) {
        DONC = pj_ntohs(*((pj_uint16_t *)(payload + off)));  // (*(payload + off) << 8) | *(payload + off + 1);
        off += 2;

        PJ_LOG(1, (trace_group, "DONC = %d", DONC));
    }

    q = payload + off;
    q_end = payload + payload_len;
    while (q < q_end) {
        const pj_uint8_t *sei_end;
        struct enzo_h264_sei_hdr hdr = {0};

        /* Get NAL unit size */
        nal_unit_size = pj_ntohs(*(pj_uint16_t *)q);  // ((pj_uint16_t)*q << 8) | *(q + 1);
        q += 2;
        sei_end = q + nal_unit_size;
        if (q + nal_unit_size > q_end) {
            /* Invalid bitstream, discard the rest of the payload */
            PJ_LOG(4, (trace_group, "Invalid PACSI unit"));
            break;
        }

        pj_memcpy(&hdr, q, sizeof(hdr));
        q += sizeof(hdr);

        PJ_LOG(1, (trace_group, "SEI header: F=%u, NRI=%u, TYPE=%u, PAYLOAD_TYPE=%u, "
            "PAYLOAD_SIZE=%u", hdr.f, hdr.nri, hdr.type, hdr.pt, hdr.ps));

        if (hdr.uuid[0] == 0x13) {  /* Stream layout */
            pj_uint8_t LBP[8], R, P;

            pj_memcpy(LBP, q, sizeof(LBP));
            q += sizeof(LBP);
            R = (*q >> 1);
            P = *q++ & 0x01;

            PJ_LOG(1, (trace_group, "Stream layout SEI: LBP=[%u,%u,%u,%u,%u,%u,%u,%u], R=%u, P=%u",
                LBP[0], LBP[1], LBP[2], LBP[3], LBP[4], LBP[5], LBP[6], LBP[7], R, P));
            if (P) {
                /* Layer description is present */
                pj_uint8_t ldsize;
                struct enzo_h264_layer_desc ld;

                ldsize = *q++;
                while (q < sei_end && ldsize) {
                    pj_memcpy(&ld, q, sizeof(ld));
                    q += sizeof(ld);

                    PJ_LOG(1, (trace_group, "Layer presentation: Coded width:%u, Coded height=%u, "
                        "Display widht=%u, Display height=%u, Bitrate=%u, FPSIdx=%u, LT=%u, PRID=%u, CB=%u, R=%u, R2=%u",
                               pj_htons(ld.coded_width), pj_htons(ld.coded_height),
                               pj_htons(ld.disp_width), pj_htons(ld.disp_height),
                               pj_htons(ld.bitrate),
                               ld.fpsidx, ld.lt, ld.prid, ld.cb, ld.r, ld.r2));
                }
            }
        } else if (hdr.uuid[0] == 0xBB) {  /* Cropping info */
            PJ_LOG(1, (trace_group, "Cropping Info SEI"));
            q += nal_unit_size - 19;
        } else if (hdr.uuid[0] == 0x05) {  /* Bitstream Info */
            pj_uint8_t ref_frm_cnt, num_of_nal_units;

            ref_frm_cnt = *q++;
            num_of_nal_units = *q++;

            PJ_LOG(1, (trace_group, "Bitstream Info: ref_frm_cnt=%u, num_of_nal_units=%u",
                ref_frm_cnt, num_of_nal_units));
        }
    }
#endif  /* TRACE_ENABLE */
}

PJ_DEF(void) pjmedia_enzo_h264_unpacketize_debug(
        const pj_uint8_t *payload,
        pj_size_t payload_len,
        const char *trace_group)
{
    const pj_size_t MIN_PAYLOAD_SIZE = 2;
    pj_uint8_t F, NRI, TYPE;  /* NAL unit header */

    if (!payload) {
        PJ_LOG(3, (trace_group, "NULL payload"));
        return;
    }
    if (payload_len < MIN_PAYLOAD_SIZE) {
        PJ_LOG(3, (trace_group, "Empty payload"));
        return;
    }

    /* NAL unit header */
    F    = (*payload >> 7);
    NRI  = (*payload >> 5) & 0x03;
    TYPE = *payload & 0x1F;
    TRACE_((trace_group, "NAL payload header: F=%u, NRI=%u, TYPE=%u",
               F, NRI, TYPE));

    if (TYPE == NAL_TYPE_PACSI) {
        unpacketize_pacsi_debug(payload, payload_len, trace_group);
    } else if (TYPE == NAL_TYPE_STAP_A) {
        /* Aggregation packet */
        const pj_uint8_t *q, *q_end;
        pj_uint16_t tmp_nal_size, cnt = 0;

        TRACE_((trace_group, "Start aggregation packet"));

        /* Fill bitstream */
        q = payload + 1;
        q_end = payload + payload_len;

        /* Get first NAL unit size */
        tmp_nal_size = pj_ntohs(*(pj_uint16_t *)q);  // (*q << 8) | *(q + 1);
        q += 2;
        if (q + tmp_nal_size > q_end) {
            /* Invalid bitstream, discard the rest of the payload */
            PJ_LOG(3, (trace_group, "Invalid STAP-A payload"));
            return;
        }
        if ((*q & 0x1F) == NAL_TYPE_PACSI) {
            unpacketize_pacsi_debug(q, tmp_nal_size, trace_group);
            q += tmp_nal_size;
        } else {
            q -= 2;
        }

        while (q < q_end) {
            /* Get NAL unit size */
            tmp_nal_size = pj_ntohs(*(pj_uint16_t *)q);  // (*q << 8) | *(q + 1);
            q += 2;
            if (q + tmp_nal_size > q_end) {
                /* Invalid bitstream, discard the rest of the payload */
                PJ_LOG(3, (trace_group, "Invalid STAP-A payload"));
                return;
            }

            cnt++;

            F    = (*q >> 7);
            NRI  = (*q >> 5) & 0x03;
            TYPE = *q & 0x1F;
            TRACE_((trace_group, "NAL(%d) TYPE=%u, size=%u", cnt, TYPE, tmp_nal_size));

            q += tmp_nal_size;
        }

        TRACE_((trace_group, "End aggregation packet: %d units", cnt));
    } else if (TYPE == NAL_TYPE_FU_A) {
        /* Fragmentation packet */
        const pj_uint8_t *q = payload;
        pj_uint8_t S, E;

        /* Get info */
        S = *(q+1) & 0x80;    /* Start bit flag */
        E = *(q+1) & 0x40;    /* End bit flag   */
        TYPE = *(q+1) & 0x1f;
        NRI = (*q >> 5) & 0x03;

        /* Fill bitstream */
        if (S) {
            TRACE_((trace_group, "Start fragmented NAL unit TYPE=%u", TYPE));
        }
        if (E) {
            TRACE_((trace_group, "End fragmented NAL unit TYPE=%u", TYPE));
        }
    } else {
        TRACE_((trace_group, "Single NAL unit TYPE=%d", TYPE));
    }
}

static pj_status_t unpacketize_pacsi(
        enzo_h264_codec_data *dec,
        const pj_uint8_t *payload,
        pj_size_t payload_len,
        pj_uint32_t *bit_flags,
        unsigned *layer_prid)
{
    /* Payload Content Scalability Information (PACSI) NAL Unit */
    struct enzo_h264_pacsi_hdr psi_hdr = {0};
    int off = 0;
    pj_uint8_t TL0PICIDX;
    pj_uint16_t IDRPICID, DONC, nal_unit_size;

    PJ_UNUSED_ARG(TL0PICIDX);
    PJ_UNUSED_ARG(IDRPICID);
    PJ_UNUSED_ARG(DONC);

    const pj_uint8_t *q, *q_end;

    pj_memcpy(&psi_hdr, payload, sizeof(psi_hdr));
    off += sizeof(psi_hdr);

    *layer_prid = psi_hdr.prid;
#if defined(PJMEDIA_LOG_VIDEO_DETAILS_INFO) && (PJMEDIA_LOG_VIDEO_DETAILS_INFO == 1)
    PJ_LOG(1, (THIS_FILE, "PACSI unit header: R=%u, I=%u, PRID=%u, N=%u, DID=%u, "
               "QID=%u, TID=%u, U=%u, D=%u, O=%u, RR=%u "
               "X=%u, Y=%u, T=%u, A=%u, P=%u, C=%u, S=%u, E=%u",
               psi_hdr.r, psi_hdr.i, psi_hdr.prid,
               psi_hdr.n, psi_hdr.did, psi_hdr.qid,
               psi_hdr.tid, psi_hdr.u, psi_hdr.d, psi_hdr.o, psi_hdr.rr,
               psi_hdr.x, psi_hdr.y, psi_hdr.t, psi_hdr.a, psi_hdr.p, psi_hdr.c, psi_hdr.s, psi_hdr.e));
#endif
    if (psi_hdr.tid /*|| psi_hdr.prid*/)
        *bit_flags |= PJMEDIA_VID_FRM_PRID_TID;
    if (psi_hdr.i)  /* IDR frame */
        *bit_flags |= PJMEDIA_VID_FRM_KEYFRAME;

    if (psi_hdr.y) {
        TL0PICIDX = *(payload + off);
        off++;
        IDRPICID = pj_ntohs(*(pj_uint16_t *)(payload + off));  // (*(payload + off) << 8) | *(payload + off + 1);
        off += 2;
#if defined(PJMEDIA_LOG_VIDEO_DETAILS_INFO) && (PJMEDIA_LOG_VIDEO_DETAILS_INFO == 1)
        PJ_LOG(1, (THIS_FILE, "TL0PICIDX = %u, IDRPICID = %u",
                   TL0PICIDX, IDRPICID));
#endif
    }
    if (psi_hdr.t) {
        DONC = pj_ntohs(*(pj_uint16_t *)(payload + off));  // (*(payload + off) << 8) | *(payload + off + 1);
        off += 2;
#if defined(PJMEDIA_LOG_VIDEO_DETAILS_INFO) && (PJMEDIA_LOG_VIDEO_DETAILS_INFO == 1)
        PJ_LOG(1, (THIS_FILE, "DONC = %d", DONC));
#endif
    }

    q = payload + off;
    q_end = payload + payload_len;
    while (q < q_end) {
        const pj_uint8_t *sei_end;
        struct enzo_h264_sei_hdr hdr;

        /* Get NAL unit size */
        nal_unit_size = pj_ntohs(*(pj_uint16_t *)q);  // ((pj_uint16_t)*q << 8) | *(q + 1);
        q += 2;
        sei_end = q + nal_unit_size;
        if (q + nal_unit_size > q_end) {
            /* Invalid bitstream, discard the rest of the payload */
            PJ_LOG(4, (THIS_FILE, "Invalid PACSI unit"));
            break;
        }

        pj_memcpy(&hdr, q, sizeof(hdr));
        q += sizeof(hdr);
#if defined(PJMEDIA_LOG_VIDEO_DETAILS_INFO) && (PJMEDIA_LOG_VIDEO_DETAILS_INFO == 1)
        PJ_LOG(1, (THIS_FILE, "SEI header: F=%u, NRI=%u, TYPE=%u, PAYLOAD_TYPE=%u, "
            "PAYLOAD_SIZE=%u", hdr.f, hdr.nri, hdr.type, hdr.pt, hdr.ps));
#endif
        if (hdr.uuid[0] == 0x13) {  /* Stream layout */
            pj_uint8_t LBP[8], R, P;
            PJ_UNUSED_ARG(LBP);
            PJ_UNUSED_ARG(R);
            PJ_UNUSED_ARG(P);

            pj_memcpy(LBP, q, sizeof(LBP));
            q += sizeof(LBP);
            R = (*q >> 1);
            P = *q++ & 0x01;
#if defined(PJMEDIA_LOG_VIDEO_DETAILS_INFO) && (PJMEDIA_LOG_VIDEO_DETAILS_INFO == 1)
            PJ_LOG(1, (THIS_FILE, "Stream layout SEI: LBP=[%u,%u,%u,%u,%u,%u,%u,%u], R=%u, P=%u",
                LBP[0], LBP[1], LBP[2], LBP[3], LBP[4], LBP[5], LBP[6], LBP[7], R, P));
#endif
            if (P) {
                /* Layer description is present */
                pj_uint8_t ldsize;
                struct enzo_h264_layer_desc ld;
                pj_uint16_t dw, dh;
                int layer_count = 0;

                ldsize = *q++;
                while (q < sei_end && ldsize) {
                    pj_memcpy(&ld, q, sizeof(ld));
                    q += sizeof(ld);

                    if (LBP[ld.prid / 8] & (1 << (ld.prid % 8))) {
                        dw = pj_ntohs(ld.disp_width);
                        dh = pj_ntohs(ld.disp_height);
                        if (dw != dec->disp_width ||
                            dh != dec->disp_height) {
                            dec->disp_width = dw;
                            dec->disp_height = dh;
                            dec->is_disp_changed = PJ_TRUE;
                        }

                        dec->decoded_prid[layer_count++] = ld.prid;
                    }
#if defined(PJMEDIA_LOG_VIDEO_DETAILS_INFO) && (PJMEDIA_LOG_VIDEO_DETAILS_INFO == 1)
                    PJ_LOG(1, (THIS_FILE, "Layer presentation: Coded width:%u, Coded height=%u, "
                        "Display widht=%u, Display height=%u, Bitrate=%u, FPSIdx=%u, LT=%u, PRID=%u, CB=%u, R=%u, R2=%u",
                               pj_htons(ld.coded_width), pj_htons(ld.coded_height),
                               pj_htons(ld.disp_width), pj_htons(ld.disp_height),
                               pj_htonl(ld.bitrate),
                               ld.fpsidx, ld.lt, ld.prid, ld.cb, ld.r, ld.r2));
#endif
                }
            }
        } else if (hdr.uuid[0] == 0xBB) {  /* Cropping info */
#if defined(PJMEDIA_LOG_VIDEO_DETAILS_INFO) && (PJMEDIA_LOG_VIDEO_DETAILS_INFO == 1)
            PJ_LOG(1, (THIS_FILE, "Cropping Info SEI"));
#endif
            q += nal_unit_size - 19;
        } else if (hdr.uuid[0] == 0x05) {  /* Bitstream Info */
            pj_uint8_t ref_frm_cnt, num_of_nal_units;
            PJ_UNUSED_ARG(ref_frm_cnt);

            ref_frm_cnt = *q++;
            num_of_nal_units = *q++;
#if defined(PJMEDIA_LOG_VIDEO_DETAILS_INFO) && (PJMEDIA_LOG_VIDEO_DETAILS_INFO == 1)
            PJ_LOG(1, (THIS_FILE, "Bitstream Info: ref_frm_cnt=%u, num_of_nal_units=%u",
                ref_frm_cnt, num_of_nal_units));
#endif

            dec->num_of_nal_units = num_of_nal_units;
        }
    }

    return PJ_SUCCESS;
}

PJ_DEF(pj_status_t) pjmedia_enzo_h264_unpacketize(
        enzo_h264_codec_data *dec,
        const pj_uint8_t *payload,
        pj_size_t payload_len,
        pj_uint8_t *bits,
        pj_size_t   bits_len,
        unsigned   *bits_pos,
        pj_uint32_t *bits_flags,
        unsigned *layer_prid)
{
    const pj_uint8_t nal_start_code[3] = {0, 0, 1};
    const pj_size_t MIN_PAYLOAD_SIZE = 2;
    pj_uint8_t F, NRI, TYPE;  /* NAL unit header */
    PJ_UNUSED_ARG(F);
    pj_status_t status = PJ_SUCCESS;

    if (!payload) {
        PJ_LOG(4, (THIS_FILE, "NULL payload"));
        return PJ_FAILED;
    }
    if (payload_len < MIN_PAYLOAD_SIZE) {
        PJ_LOG(4, (THIS_FILE, "Empty payload"));
        return PJ_FAILED;
    }

    /* NAL unit header */
    F    = (*payload >> 7);
    NRI  = (*payload >> 5) & 0x03;
    TYPE = *payload & 0x1F;
#if defined(PJMEDIA_LOG_VIDEO_DETAILS_INFO) && (PJMEDIA_LOG_VIDEO_DETAILS_INFO == 1)
    PJ_LOG(1, (THIS_FILE, "NAL payload header: F=%u, NRI=%u, TYPE=%u",
               F, NRI, TYPE));
#endif

    if (TYPE == NAL_TYPE_PACSI) {
        status = unpacketize_pacsi(dec, payload, payload_len, bits_flags, layer_prid);
        dec->wait_to_finish_fu_a = PJ_FALSE;
    } else if (TYPE == NAL_TYPE_STAP_A) {
        /* Aggregation packet */
        const pj_uint8_t *q, *q_end;
        pj_uint16_t tmp_nal_size, cnt = 0;
        pj_uint8_t *p, *p_end;
#if defined(PJMEDIA_LOG_VIDEO_DETAILS_INFO) && (PJMEDIA_LOG_VIDEO_DETAILS_INFO == 1)
        PJ_LOG(1, (THIS_FILE, "Start aggregation packet"));
#endif
        /* Fill bitstream */
        q = payload + 1;
        q_end = payload + payload_len;

        /* Get first NAL unit size */
        tmp_nal_size = pj_ntohs(*(pj_uint16_t *)q);  // (*q << 8) | *(q + 1);
        q += 2;
        if (q + tmp_nal_size > q_end) {
            /* Invalid bitstream, discard the rest of the payload */
            PJ_LOG(4, (THIS_FILE, "Invalid STAP-A payload"));
            return PJ_FAILED;
        }
        if ((*q & 0x1F) == NAL_TYPE_PACSI) {
            status = unpacketize_pacsi(dec, q, tmp_nal_size, bits_flags, layer_prid);
            q += tmp_nal_size;
        } else {
            q -= 2;
        }

        p = bits + *bits_pos;
        p_end = bits + bits_len;

        while (q < q_end && p < p_end) {
            /* Get NAL unit size */
            tmp_nal_size = pj_ntohs(*(pj_uint16_t *)q);  // (*q << 8) | *(q + 1);
            q += 2;
            if (q + tmp_nal_size > q_end) {
                /* Invalid bitstream, discard the rest of the payload */
                PJ_LOG(4, (THIS_FILE, "Invalid STAP-A payload"));
                return PJ_FAILED;
            }

            cnt++;

            F    = (*q >> 7);
            NRI  = (*q >> 5) & 0x03;
            TYPE = *q & 0x1F;
#if defined(PJMEDIA_LOG_VIDEO_DETAILS_INFO) && (PJMEDIA_LOG_VIDEO_DETAILS_INFO == 1)
            PJ_LOG(1, (THIS_FILE, "NAL(%d) TYPE=%u, size=%u", cnt, TYPE,  tmp_nal_size));
#endif

            /* Write NAL unit start code */
            pj_memcpy(p, &nal_start_code, sizeof(nal_start_code));
            p += sizeof(nal_start_code);

            /* Write NAL unit */
            pj_memcpy(p, q, tmp_nal_size);
            p += tmp_nal_size;
            q += tmp_nal_size;

            /* Update the bitstream writing offset */
            *bits_pos = p - bits;

            if (TYPE == 5) {
                int trap = 1;
                trap ++;
            }
        }
#if defined(PJMEDIA_LOG_VIDEO_DETAILS_INFO) && (PJMEDIA_LOG_VIDEO_DETAILS_INFO == 1)
        PJ_LOG(1, (THIS_FILE, "End aggregation packet: %d units", cnt));
#endif
        if (cnt == 2) {
            int trap = 1;
            trap++;
        }

        dec->wait_to_finish_fu_a = PJ_FALSE;
    } else if (TYPE == NAL_TYPE_FU_A) {
        /* Fragmentation packet */
        const pj_uint8_t *q = payload;
        pj_uint8_t S, E;
        pj_uint8_t *p = bits + *bits_pos;

        /* Get info */
        S = *(q+1) & 0x80;    /* Start bit flag */
        E = *(q+1) & 0x40;    /* End bit flag   */
        TYPE = *(q+1) & 0x1f;
        NRI = (*q >> 5) & 0x03;
        q += 2;

        /* Fill bitstream */
        if (S) {
#if defined(PJMEDIA_LOG_VIDEO_DETAILS_INFO) && (PJMEDIA_LOG_VIDEO_DETAILS_INFO == 1)
            PJ_LOG(1, (THIS_FILE, "Start fragmented NAL unit TYPE=%u, size=%u", TYPE, payload_len - 2));
#endif

            /* This is the first part, write NAL unit start code */
            pj_memcpy(p, &nal_start_code, PJ_ARRAY_SIZE(nal_start_code));
            p += PJ_ARRAY_SIZE(nal_start_code);

            /* Write NAL unit octet */
            *p++ = (NRI << 5) | TYPE;

            dec->wait_to_finish_fu_a = PJ_TRUE;
        }

        /* Write NAL unit */
        pj_memcpy(p, q, payload_len - 2);
        p += (payload_len - 2);

        /* Update the bitstream writing offset */
        *bits_pos = p - bits;

        if (E) {
#if defined(PJMEDIA_LOG_VIDEO_DETAILS_INFO) && (PJMEDIA_LOG_VIDEO_DETAILS_INFO == 1)
            PJ_LOG(1, (THIS_FILE, "End fragmented NAL unit TYPE=%u, size=%u", TYPE, payload_len - 2));
#endif

            dec->wait_to_finish_fu_a = PJ_FALSE;

            if (TYPE == 5) {
                int trap = 1;
                trap++;
            }
        }
    } else if (NAL_TYPE_SINGLE_NAL_MIN <= TYPE &&
               TYPE <= NAL_TYPE_SINGLE_NAL_MAX) {
        pj_uint8_t *p = bits + *bits_pos;
#if defined(PJMEDIA_LOG_VIDEO_DETAILS_INFO) && (PJMEDIA_LOG_VIDEO_DETAILS_INFO == 1)
        PJ_LOG(1, (THIS_FILE, "Single NAL unit TYPE=%d, size=%u", TYPE, payload_len));
#endif

        /* Write NAL unit start code */
        pj_memcpy(p, &nal_start_code, PJ_ARRAY_SIZE(nal_start_code));
        p += PJ_ARRAY_SIZE(nal_start_code);

        /* Write NAL unit */
        pj_memcpy(p, payload, payload_len);
        p += payload_len;

        /* Update the bitstream writing offset */
        *bits_pos = p - bits;

        dec->wait_to_finish_fu_a = PJ_FALSE;
    } else {
        PJ_LOG(3, (THIS_FILE, "Unpacketize unknown NAL (TYPE = %d)", TYPE));
    }

    return status;
}

PJ_DEF(pj_status_t) pjmedia_enzo_h264_packetize(
    const pj_uint8_t *enc_buf,
    unsigned enc_buf_len,
    int mtu,
    pj_bool_t send_pacsi,
    unsigned *enc_processed,
    unsigned *fua_processed,
    pj_uint8_t *buf,
    unsigned buf_size,
    unsigned *buf_len)
{
    const pj_uint8_t *nals[MAX_NALS] = {0};
    int nal_sizes[MAX_NALS] = {0};
    int nal_total_size = 0;
    int nal_cnt = 0, i;
    const pj_uint8_t *p, *pend;
    pj_uint16_t len;

    p = enc_buf;
    pend = enc_buf + enc_buf_len;
    while (p < pend) {
        len = (*p << 8) | *(p + 1);
        p += 2;
        if (len != 0) {
            nal_sizes[nal_cnt] = len;
            nal_total_size += len;
            nals[nal_cnt] = p;
            nal_cnt++;

//            PJ_LOG(5, (THIS_FILE, "XXX NAL type=%u", *nals[nal_cnt - 1] & 0x1F));
        }
        if (p + len >= pend)
            break;
        if (nal_cnt >= MAX_NALS)
            break;

        p += len;
    }

    if (nal_cnt == 0)
        return PJ_FAILED;

    assert(nal_cnt > 0);

    if (send_pacsi) {
        if ((*nals[0] & 0x1F) == NAL_TYPE_PACSI) {
            return pjmedia_enzo_h264_packetize(enc_buf,
                                             nal_sizes[0] + 2,
                                             mtu,
                                             PJ_FALSE,
                                             enc_processed,
                                             fua_processed,
                                             buf,
                                             buf_size,
                                             buf_len);
        }
    }

    if (nal_cnt > 1 && (nal_total_size <= mtu)) {
        /* Aggrigate in STAP-A */
        pj_uint8_t f = 0, nri = 0, type = NAL_TYPE_STAP_A;
        pj_uint8_t tmp;

        for (i = 0; i < nal_cnt; ++i) {
            p = nals[i];
            f = (*p >> 7) ? 1 : f;
            tmp = (*p >> 5) & 0x03;
            if (tmp > nri) nri = tmp;
        }

        len = 0;
        /* Write STAP-A header */
        buf[len++] = (f << 7) | (nri << 5) | type;
        for (i = 0; i < nal_cnt; ++i) {
            /* Write NAL size */
            len += enzo_h264_write_s(buf + len, nal_sizes[i]);
            /* buf[len++] = (pj_uint8_t)(nal_sizes[i] >> 8); */
            /* buf[len++] = (pj_uint8_t)(nal_sizes[i] & 0xFF); */

            /* Write NAL content */
            pj_memcpy(buf + len, nals[i], nal_sizes[i]);
            len += nal_sizes[i];
        }

        *buf_len = len;

        *enc_processed += enc_buf_len;  /* Processed all the buffer */
    } else if (nal_cnt > 1) {
        len = 0;
        for (i = 0; i < nal_cnt; ++i) {
            if (len + nal_sizes[i] > mtu)
                break;
            else
                len += nal_sizes[i];
        }
        if (i > 0) {
            len += i * 2;  /* Add 2 bytes for length */
            return pjmedia_enzo_h264_packetize(enc_buf,
                                             len,
                                             mtu,
                                             PJ_FALSE,
                                             enc_processed,
                                             fua_processed,
                                             buf,
                                             buf_size,
                                             buf_len);
        } else {
            return pjmedia_enzo_h264_packetize(enc_buf,
                                             nal_sizes[0] + 2,
                                             mtu,
                                             PJ_FALSE,
                                             enc_processed,
                                             fua_processed,
                                             buf,
                                             buf_size,
                                             buf_len);
        }
    } else {
        /* Single NAL unit */
        if (nal_total_size <= mtu) {
            pj_memcpy(buf, nals[0], nal_sizes[0]);
            *buf_len = nal_sizes[0];

            *enc_processed += enc_buf_len;
        } else {
            /* Fragmented FU-A */
            pj_uint8_t f = 0, nri = 0, type = NAL_TYPE_FU_A;
            pj_uint8_t nal_type;
            int payload_len;

            p = enc_buf + 2;

            nri = (*p >> 5) & 0x03;
            nal_type = *p & 0x1F;

            len = 0;

            /* FU indicator */
            buf[len++] = (f << 7) | (nri << 5) | type;

            /* FU header */
            buf[len] = nal_type;
            if (*fua_processed == 0)
                buf[len] |= (1 << 7);
            else if (nal_total_size - *fua_processed <= mtu)
                buf[len] |= (1 << 6);
            len++;

            if (nal_total_size - *fua_processed > mtu) {
                int size = nal_total_size - *fua_processed;
                int cnt = (size / mtu) + 1;
                payload_len = size / cnt;
            } else {
                payload_len = nal_total_size - *fua_processed;
            }

            pj_memcpy(buf + len, nals[0] + *fua_processed, payload_len);
            len += payload_len;
            *buf_len = len;

            *fua_processed += payload_len;
            if (*fua_processed >= nal_total_size) {
                *fua_processed = 0;
                *enc_processed += nal_total_size + 2;
            }

            return PJ_SUCCESS;
        }
    }

    return PJ_SUCCESS;
}

PJ_DEF(int) enzo_h264_write_s(pj_uint8_t *stream, pj_uint16_t val) {
    pj_uint16_t tmp = pj_htons(val);
    pj_memcpy(stream, &tmp, sizeof(tmp));
    return sizeof(tmp);
}

PJ_DEF(int) enzo_h264_write_l(pj_uint8_t *stream, pj_uint32_t val) {
    pj_uint32_t tmp = pj_htonl(val);
    pj_memcpy(stream, &tmp, sizeof(tmp));
    return sizeof(tmp);
}

#endif  /* PJMEDIA_HAS_ENZO_H264_CODEC */
