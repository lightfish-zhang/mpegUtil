#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "muxing.h"

static int decode(void** mctx, const char *outformatname, const int width, AVCodecContext *dec_ctx, AVFrame *frame, AVPacket *pkt)
{
    int ret;

    // 拷贝 原始压缩数据 packet 到解码器上下文 
    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error sending a packet for decoding, ret:%d\n", ret);
        return -1;
    }

    while (ret >= 0) {
        // 将原始的 raw data ( packet ) 解码，输出 frame
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            ret = 1;
            break;
        } else if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error during decoding, ret:%d\n", ret);
            break;
        }

        // 将 帧的时间戳 改为 解码计数
        frame->pts = dec_ctx->frame_number;

        // mctx 只设置一次
        if (NULL == *mctx) {
            // 音视频的解复用，而当前逻辑只处理视频，这里主要是做视频解码相关的内存分配、参数设置工作
            *mctx = muxing_begin(outformatname, NULL, 1, width, width*frame->height/frame->width);
        }
        // 将 frame 按自定义尺寸缩放，再压缩数据 packet，写入到 mctx 的输出流
        ret = muxing_write_video(*mctx, frame);
        if (ret < 0)
            break;
    }
    return ret;
}

/* 
获得解码器
从官方示例 github.com/FFmpeg/FFmepg/doc/example 摘抄的函数代码
实际输入: fmt_ctx，type
其他参数传引用，实际是输出
stream_idx　缩略图在视频中的流索引
dec_ctx 解码器上下文，找到的合适的解码器codec后，分配解码器上下文，解码器句柄赋值于 dec_ctx->codec
 */
static int open_codec_context(int *stream_idx,
                              AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type)
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
        *stream_idx = stream_index;
    }

    return 0;
}
/* 
给图片生成缩略图
 */
int gen_thumbnail(const char* formatname, const int width, void* data, int data_size, void* outbuff, int outbufflen, int *outsz)
{
    AVCodecContext *video_dec_ctx = NULL; // 解码器上下文
    AVFormatContext *fmt_ctx = NULL; // AV 格式上下文
    AVIOContext *io_ctx = NULL; // AV 基本IO 的上下文
    AVFrame *frame = NULL; // AV 帧
    int ret = -1, video_stream_idx = -1; // 整型的 返回值、视频流的索引
    AVPacket *pkt = NULL; // AV 视频的压缩数据包的指针
    void* mctx = NULL; // 多路复用相关处理的指针，ffmpeg 中 视频文件输入后，会被"解复用 demux"为音频流与视频流，两者同时处理
    unsigned char *indata = NULL; // avcodec 的输入的指针

    // 分配相关的内存
    fmt_ctx = avformat_alloc_context();
    if (NULL == fmt_ctx) {
        av_log(NULL, AV_LOG_ERROR, "Could not alloc format context\n");
        goto end;
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
        goto clean2;
    }

    /* retrieve stream information */
    // 为了防止某些文件格式没有 header，于是从数据流中读取文件格式
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not find stream information\n");
        goto clean2;
    }

    // 获得解码器，找到视频流的索引
    if (open_codec_context(&video_stream_idx, &video_dec_ctx, fmt_ctx, AVMEDIA_TYPE_VIDEO) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not open codec context\n");
        goto clean3;
    }

    // 分配压缩数据包的内存，返回指针
    pkt = av_packet_alloc();
    if (!pkt)
        goto clean3;

    // 分配 AV 帧 的内存，返回指针
    frame = av_frame_alloc();
    if (!frame) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate video frame\n");
        goto clean4;
    }

    // 读取音视频文件流，获得下一帧数据的（一个原始的压缩数据包 packet）
    // get the next frame of a stream.
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->size) {
            // 由于音视频文件是按时序来排列音频帧或者视频帧，此时读取的帧有可能是音频的，与当前逻辑所需的视频帧要求不符，需要过滤
            // 若不过滤，会出现将音频流当做图片的 bug
            if (video_stream_idx != pkt->stream_index) {
                av_packet_unref(pkt);
                continue;
            }
            ret = decode(&mctx, formatname, width, video_dec_ctx, frame, pkt);
            av_frame_unref(frame);
            av_packet_unref(pkt);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error while decoding,err:%d\n", ret);
                goto clean5;
            }
            // 成功解码一帧，跳出
            if (video_dec_ctx->frame_number == 1) {
                goto clean5;
            }
        }
    }
    
    // 缩略图片失败，flush output stream
    // packet = NULL 输入，触发解码器进行 flush interleaving queue
    // 题外话: 解码过程中，解出的 frame 可能是乱序的，解码器会确保它排序正确
    decode(&mctx, formatname, width, video_dec_ctx, frame, NULL);

// 清理工作，设置不同阶段的tag, 以便 goto 跳转
clean5:
    // 结束视频解码工作，将缩略图的数据 memcpy 到 用户分配的 outbuff 指针上
    ret = muxing_end(mctx, outbuff, outbufflen, outsz);
    // 回收 tmp frame 内存
    av_frame_free(&frame);
clean4:
    // 回收 tmp packet 内存
    av_packet_free(&pkt);
clean3:
    // 回收解码器的上下文
    if (NULL != video_dec_ctx) {
        avcodec_free_context(&video_dec_ctx);
    }
clean2:
    // 回收 AV pointer
    av_freep(&fmt_ctx->pb->buffer);
    av_freep(&fmt_ctx->pb);
clean1:
    // 回收 AV format 的信息
    avformat_close_input(&fmt_ctx);
end:
    return ret;
}

