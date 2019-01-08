#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

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

static int open_codec_context(AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
    int ret, stream_index; // 整型的返回值、流索引
    AVStream *st; // AV 流的指针
    AVCodec *dec = NULL; // AV 解码器的指针

    // 从视频文件中找到“适合的流”的索引，作为选择解码器的依据
    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not find %s stream\n",
                av_get_media_type_string(type));
        return ret;
    } else {
        stream_index = ret;
        st = fmt_ctx->streams[stream_index];

        /* 获得该 stream 类型对应的解码器的句柄 */
        dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!dec) {
            av_log(NULL, AV_LOG_ERROR, "Failed to find %s codec\n",
                    av_get_media_type_string(type));
            return AVERROR(EINVAL);
        }

        /* 为解码器分配上下文的指针 */
        *dec_ctx = avcodec_alloc_context3(dec);
        if (!*dec_ctx) {
            av_log(NULL, AV_LOG_ERROR, "Failed to allocate the %s codec context\n",
                    av_get_media_type_string(type));
            return AVERROR(ENOMEM);
        }

        // 将选中的 stream 的 参数　拷贝到 解码器的上下文中
        if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Failed to copy %s codec parameters to decoder context\n",
                    av_get_media_type_string(type));
            return ret;
        }

        // 初始化解码器
        if ((ret = avcodec_open2(*dec_ctx, dec, NULL)) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Failed to open %s codec\n",
                    av_get_media_type_string(type));
            return ret;
        }
    }

    return 0;
}

int gen_gif(const char* filename, const int src_framerate, const int rotate, void* data, int data_size)
{
    const AVCodec *codec = NULL;
    AVFormatContext *fmt_ctx = NULL; // AV 格式上下文
    // AVCodecParserContext *parser = NULL;
    AVCodecContext *c = NULL;
    AVFrame *frame = NULL;
    AVFrame *filt_frame = NULL;
    int ret = -1, video_stream_idx = -1;
    AVPacket *pkt = NULL;
    void* mctx = NULL;
    void* fctx = NULL;
    unsigned char *indata = NULL; // avcodec 的输入的指针
    int *stream_mapping = NULL;
    int stream_mapping_size = 0;
    int stream_index = 0;

    // 分配相关的内存
    fmt_ctx = avformat_alloc_context();
    if (NULL == fmt_ctx) {
        av_log(NULL, AV_LOG_ERROR, "Could not alloc format context\n");
        goto clean0;
    }
    indata = (unsigned char *)av_malloc(data_size); // ffmpeg 分配内存的方法
    if (NULL == indata) {
        av_log(NULL, AV_LOG_ERROR, "Could not alloc indata\n");
        goto clean0;
    }
    memcpy(indata, data, data_size); // 复制待处理的文件数据

    // 给 av format context 分配 io 操作的句柄，必要传参：输入数据的指针、数据大小、write_flag＝０表明buffer不可写，其他参数忽略，置 NULL
    fmt_ctx->pb = avio_alloc_context(indata, data_size, 0, NULL, NULL, NULL, NULL);
    if (NULL == fmt_ctx->pb) {
        av_log(NULL, AV_LOG_ERROR, "Could not alloc io context\n");
        av_free(indata);
        goto clean0;
    }

    // 打开输入的数据流，读取 header 的格式内容，注意必须的后续处理 avformat_close_input()
    if (avformat_open_input(&fmt_ctx, NULL, NULL, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not open input data\n");
        goto clean0;
    }

    /* retrieve stream information */
    // 为了防止某些文件格式没有 header，于是从数据流中读取文件格式
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not find stream information\n");
        goto clean0;
    }

    // 找到第一个视频流的索引，获得解码器ID
    if (open_codec_context(&c, fmt_ctx, AVMEDIA_TYPE_VIDEO) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not open codec context\n");
        goto clean0;
    }


    av_log(NULL, AV_LOG_INFO, "fmt_ctx->nb_streams=%d", fmt_ctx->nb_streams);
    // find video stream
    stream_mapping_size = fmt_ctx->nb_streams;
    stream_mapping = av_mallocz_array(stream_mapping_size, sizeof(*stream_mapping));
    if (!stream_mapping) {
        ret = AVERROR(ENOMEM);
        goto clean1;
    }
    for (int i = 0; i < fmt_ctx->nb_streams; i++) {
        AVStream *out_stream;
        AVStream *in_stream = fmt_ctx->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;
        if (in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
            stream_mapping[i] = -1;
            continue;
        }
        av_log(NULL, AV_LOG_INFO, "set stream_mapping %d:%d", i, stream_index);
        stream_mapping[i] = stream_index++;
    }


    c->framerate.num = src_framerate;
    c->framerate.den = 1;
    int skip_step = c->framerate.num / c->framerate.den / k_gif_framerate;
    if (skip_step == 0)
        skip_step = 1;

    // 分配压缩数据包的内存，返回指针
    pkt = av_packet_alloc();
    if (!pkt)
        goto clean2;

    // 分配 AV 帧 的内存，返回指针
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

    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->size) {
            av_log(NULL, AV_LOG_INFO, "read frame stream index=%d, map=%d", pkt->stream_index, stream_mapping[pkt->stream_index]);
            if(stream_mapping[pkt->stream_index]<0){
                av_packet_unref(pkt);
                continue;
            }
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
    //av_parser_close(parser);
clean3:
    av_packet_free(&pkt);
clean2:
    av_freep(&stream_mapping);
clean1:
    avcodec_free_context(&c);
clean0:
    if(NULL != fmt_ctx->pb->buffer)
        av_freep(&fmt_ctx->pb->buffer);
    if(NULL != fmt_ctx->pb)
        av_freep(&fmt_ctx->pb);
    avformat_close_input(&fmt_ctx);
end:
    return ret;
}
