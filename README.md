This guideline is for Enzo-Lync 2013 integration.

I. Function Note
====
Below are of some notes of the major functions of the enzo_h264.h file.

1. pjmedia_codec_enzo_h264_vid_init: Initialize and register Enzo H264 codec factory.

2. enzo_h264_default_attr: Set default attributes for Enzo codec when negotiating with Lync via SDP.

3. enzo_h264_alloc_codec: Allocate Enzo encoder and decoder.

4. enzo_h264_codec_init: Initialize Enzo stuff (VPU, encoder, decoder,...).

5. enzo_h264_codec_open: Called after Enzo-Lync negotiation success. Ready to send/receive packets.

6. enzo_h264_codec_modify: Called when bitrate (resolution, bandwith, frame rate) of an endpoint requires to change.

7. enzo_h264_codec_encode_begin: Start to take frame buffer to encode, the output media frame will be put to the transportation layer to send to the remote endpoint.

8. enzo_h264_codec_encode_write_pacsi, enzo_h264_codec_encode_write_nals: Where to insert more supplemental enhancement information after completing encode to adapt with Lync's spec.

9. enzo_h264_codec_decode: Get frame buffer from the transportation layer to decode.

Note: The Enzo codec team just needs to see indexes: #3, #4, #5, #6, #7, #8, #9 to make sure Enzo stuff are used properly.

II. Issues
====
1. Enzo encoder can not encode frame buffer. FIXED (Josh added more BUFFER feature to encode data from buffer)

2. Need more returned NAL information of bitstream (at least NAL count and NAL length of each NAL are MUST) after encode.
- VPU seems to use only one slice of each frame, therefore the NAL count is 1 and the NAL length is avcData->bufOutSize.
- Josh is investigating how to provide more information since support multiple NALs will make the performance better. WORKING

3. Having problem in send/receive big packets on Enzo device. This causes the pixelated video issue. Don will reproduce and save data to a file later. CHECKING


