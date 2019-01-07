#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>

#include "muxing.h"
#include "filtering_video.h"

static const int k_gif_framerate = 5;

static int decode(void** mctx, void** fctx, const int rotate, const char* outfilename, const int skip_step, AVCodecContext *dec_ctx, AVFrame *frame, AVFrame *filt_frame, AVPacket *pkt)
{
    int ret;

    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error sending a packet for decoding, outfilename:%s, ret:%d\n", outfilename, ret);
        return -1;
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            ret = 1;
            break;
        } else if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error during decoding, outfilename:%s, ret:%d\n", outfilename, ret);
            break;
        }

        frame->pts = dec_ctx->frame_number;

        if (dec_ctx->frame_number == 1) {
            if (rotate != 0) {
                char filters_descr[64];
                snprintf(filters_descr, sizeof(filters_descr), "rotate='%d*PI/180:ow=rotw(%d*PI/180):oh=roth(%d*PI/180)'", rotate, rotate, rotate);
                av_log(NULL, AV_LOG_INFO, "%s filters_descr:%s\n", outfilename, filters_descr);
                *fctx = init_filters(filters_descr, dec_ctx, dec_ctx->pix_fmt);
            }
            if (*fctx) {
                filtering(*fctx, frame, filt_frame);
                frame = filt_frame;
            }

            *mctx = muxing_begin(NULL, outfilename, k_gif_framerate, 320, 320*frame->height/frame->width);
            ret = muxing_write_video(*mctx, frame);
            if (ret < 0)
                break;
        }
        else if (dec_ctx->frame_number % skip_step == 1)
        {
            if (*fctx) {
                filtering(*fctx, frame, filt_frame);
                frame = filt_frame;
            }
            ret = muxing_write_video(*mctx, frame);
            if (ret < 0)
                break;
        }
    }
    return ret;
}

int gen_gif(const char* filename, const int src_framerate, const int rotate, void* data, int data_size)
{
    const AVCodec *codec = NULL;
    AVCodecParserContext *parser = NULL;
    AVCodecContext *c = NULL;
    AVFrame *frame = NULL;
    AVFrame *filt_frame = NULL;
    int ret = -1;
    AVPacket *pkt = NULL;
    void* mctx = NULL;
    void* fctx = NULL;

    pkt = av_packet_alloc();
    if (!pkt)
        goto end;

    /* find the H264 video decoder */
    codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        av_log(NULL, AV_LOG_ERROR, "Codec not found\n");
        goto end;
    }

    parser = av_parser_init(codec->id);
    if (!parser) {
        av_log(NULL, AV_LOG_ERROR, "parser not found\n");
        goto clean1;
    }

    c = avcodec_alloc_context3(codec);
    if (!c) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate video codec context\n");
        goto clean2;
    }
    c->framerate.num = src_framerate;
    c->framerate.den = 1;
    int skip_step = c->framerate.num / c->framerate.den / k_gif_framerate;
    if (skip_step == 0)
        skip_step = 1;

    /* For some codecs, such as msmpeg4 and mpeg4, width and height
       MUST be initialized there because this information is not
       available in the bitstream. */

    /* open it */
    if (avcodec_open2(c, codec, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not open codec\n");
        goto clean3;
    }

    frame = av_frame_alloc();
    if (!frame) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate video frame\n");
        goto clean3;
    }

    filt_frame = av_frame_alloc();
    if (!filt_frame) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate video frame\n");
        goto clean4;
    }

    while (data_size > 0) {
        ret = av_parser_parse2(parser, c, &pkt->data, &pkt->size, data,
                               data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error while parsing\n");
            goto clean5;
        }
        data += ret;
        data_size -= ret;

        if (pkt->size) {
            ret = decode(&mctx, &fctx, rotate, filename, skip_step, c, frame, filt_frame, pkt);
            av_frame_unref(frame);
            av_frame_unref(filt_frame);
            av_packet_unref(pkt);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error while decoding,err:%d\n", ret);
                goto clean5;
            }
        }
    }

    /* flush the decoder */
    decode(&mctx, &fctx, rotate, filename, skip_step, c, frame, filt_frame, NULL);
clean5:
    free_filters(fctx);
    ret = muxing_end(mctx, NULL, 0, NULL);
    av_frame_free(&filt_frame);
clean4:
    av_frame_free(&frame);
clean3:
    avcodec_free_context(&c);
clean2:
    av_parser_close(parser);
clean1:
    av_packet_free(&pkt);
end:
    return ret;
}
