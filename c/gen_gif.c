#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "muxing.h"
#include "filtering_video.h"

static const int k_gif_framerate = 5; // 默认 gif 的帧率为 5，即每秒 5 帧

static int decode(void** mctx, void** fctx, const int rotate, 
                    const char* outfilename, const int skip_step, AVCodecContext *dec_ctx, 
                    AVFrame *frame, AVFrame *filt_frame, AVPacket *pkt, AVStream *st)
{
    int ret;

    // 向解码器发送原始压缩数据 packet
    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error sending a packet for decoding, outfilename:%s, ret:%d\n", outfilename, ret);
        return -1;
    }

    while (ret >= 0) {
        // 从解码器转码出 frame 视频帧
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_log(NULL, AV_LOG_INFO, "[decode] avcodec_receive_frame ret=%d", ret);
            /*
            返回 EAGAIN 表示需要更多帧来参与编码
            像 MPEG等格式, P帧(预测帧)需要依赖I帧(关键帧)或者前面的P帧，使用比较或者差分方式编码
            */ 
            ret = 1;
            break;
        } else if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error during decoding, outfilename:%s, ret:%d\n", outfilename, ret);
            break;
        }
        
        // PTS（显示时间戳）, 计算一帧在整个视频的时间位置：timestamp(秒) = pts * av_q2d(st->time_base)
        frame->pts = dec_ctx->frame_number;
        av_log(NULL, AV_LOG_INFO, "[decode] frame_number=%d, timestamp=%f", 
                dec_ctx->frame_number, frame->pts * av_q2d(st->time_base));

        if (dec_ctx->frame_number == 1) {
            if (rotate != 0) {
                char filters_descr[64];
                /*
                filter
                    格式：http://ffmpeg.org/ffmpeg-filters.html#frei0r-1
                    参数列表：https://www.mltframework.org/plugins/PluginsFilters/
                */
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

static int open_codec_context(AVCodecContext **dec_ctx, int *stream_index, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
    int ret; // 整型的返回值、流索引
    AVStream *st; // AV 流的指针
    AVCodec *dec = NULL; // AV 解码器的指针

    // 从视频文件中找到“适合的流”的索引，作为选择解码器的依据
    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not find %s stream\n",
                av_get_media_type_string(type));
        return ret;
    } else {
        *stream_index = ret;
        st = fmt_ctx->streams[*stream_index];

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

        // 获取帧率
        (*dec_ctx)->framerate = av_guess_frame_rate(fmt_ctx, st, NULL);
        av_log(NULL, AV_LOG_INFO, "framerate num=%d den=%d", (*dec_ctx)->framerate.num , (*dec_ctx)->framerate.den);
    }

    return 0;
}

int gen_gif(const char* filename, const int rotate, void* data, int data_size)
{
    const AVCodec *codec = NULL; // AV 解码器指针
    AVFormatContext *fmt_ctx = NULL; // AV 格式上下文
    AVCodecContext *c = NULL; // 解码器上下文
    AVFrame *frame = NULL; // 输入文件的帧，缓存用
    AVFrame *filt_frame = NULL; // 
    int ret = -1, video_stream_idx = -1;
    AVPacket *pkt = NULL;
    void* mctx = NULL; // muxing context
    void* fctx = NULL; // filter context
    unsigned char *indata = NULL; // avcodec 的输入的指针
    int video_stream_index = 0;

    // 分配相关的内存
    fmt_ctx = avformat_alloc_context();
    if (NULL == fmt_ctx) {
        av_log(NULL, AV_LOG_ERROR, "Could not alloc format context\n");
        goto clean1;
    }
    indata = (unsigned char *)av_malloc(data_size); // ffmpeg 分配内存的方法
    if (NULL == indata) {
        av_log(NULL, AV_LOG_ERROR, "Could not alloc indata\n");
        goto clean1;
    }
    memcpy(indata, data, data_size); // 复制待处理的文件数据

    // 给 av format context 分配 io 操作的句柄，必要传参：输入数据的指针、数据大小、write_flag＝０表明buffer不可写，其他参数忽略，置 NULL
    fmt_ctx->pb = avio_alloc_context(indata, data_size, 0, NULL, NULL, NULL, NULL);
    if (NULL == fmt_ctx->pb) {
        av_log(NULL, AV_LOG_ERROR, "Could not alloc io context\n");
        av_free(indata);
        goto clean1;
    }

    // 打开输入的数据流，读取 header 的格式内容，注意必须的后续处理 avformat_close_input()
    if (avformat_open_input(&fmt_ctx, NULL, NULL, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not open input data\n");
        goto clean1;
    }

    /* retrieve stream information */
    // 为了防止某些文件格式没有 header，于是从数据流中读取文件格式
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not find stream information\n");
        goto clean1;
    }

    // 找到第一个视频流的索引，获得解码器ID
    if (open_codec_context(&c, &video_stream_index, fmt_ctx, AVMEDIA_TYPE_VIDEO) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not open codec context\n");
        goto clean1;
    }

    // 视频与gif的帧率比，计算转码时跳过帧的间隔数
    // note: num 分子， den 分母
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
            av_log(NULL, AV_LOG_INFO, "read frame stream index=%d", pkt->stream_index);
            // 不同 stream_id 的packet是交错的，在时序上，多路复用
            if(pkt->stream_index != video_stream_index){
                av_packet_unref(pkt);
                continue;
            }

            ret = decode(&mctx, &fctx, rotate, filename, skip_step, c, frame, filt_frame, pkt, fmt_ctx->streams[video_stream_index]);
            av_frame_unref(frame);
            av_frame_unref(filt_frame);
            av_packet_unref(pkt);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error while decoding,err:%d\n", ret);
                goto clean5;
            }
        }
    }

    // flush the decoder 不再传入packet, packet=NULL，将 fmt_ctx 中剩余的帧都处理完
    decode(&mctx, &fctx, rotate, filename, skip_step, c, frame, filt_frame, NULL, fmt_ctx->streams[video_stream_index]);
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
    avcodec_free_context(&c);
clean1:
    if(NULL != fmt_ctx->pb->buffer)
        av_freep(&fmt_ctx->pb->buffer);
    if(NULL != fmt_ctx->pb)
        av_freep(&fmt_ctx->pb);
    avformat_close_input(&fmt_ctx);
end:
    return ret;
}
