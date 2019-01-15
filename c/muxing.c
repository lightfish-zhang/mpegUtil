/* begin 从 github.com/FFmpeg/FFmpeg/doc/examples/muxing.c 摘抄的代码 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavutil/timestamp.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

// a wrapper around a single output AVStream
typedef struct OutputStream {
    AVStream *st;
    AVCodecContext *enc;

    /* pts of the next frame that will be generated */
    int64_t next_pts;
    int samples_count;

    AVFrame *frame;
    AVFrame *tmp_frame;
    AVPacket *pkt;

    struct SwsContext *sws_ctx;
} OutputStream;

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt) {
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    av_log(NULL, AV_LOG_INFO, "pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}

static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt) {
    /* rescale output packet timestamp values from codec to stream timebase */
    av_packet_rescale_ts(pkt, *time_base, st->time_base);
    pkt->stream_index = st->index;

    /* Write the compressed frame to the media file. */
    //log_packet(fmt_ctx, pkt);
    return av_interleaved_write_frame(fmt_ctx, pkt);
}

/* Add an output stream. */
static void add_stream(OutputStream *ost, AVFormatContext *oc,
                       AVCodec **codec,
                       enum AVCodecID codec_id, const int framerate, const int w, const int h) {
    AVCodecContext *c;
    int i;

    av_log(NULL, AV_LOG_INFO, "find encoder for %d,'%s'\n", codec_id, 
            avcodec_get_name(codec_id));
    /* find the encoder */
    *codec = avcodec_find_encoder(codec_id);
    if (!(*codec)) {
        av_log(NULL, AV_LOG_ERROR, "Could not find encoder for '%s'\n",
                avcodec_get_name(codec_id));
        return;
    }
    // 分配输出的流
    ost->st = avformat_new_stream(oc, NULL);
    if (!ost->st) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate stream\n");
        return;
    }
    ost->st->id = oc->nb_streams - 1;
    c = avcodec_alloc_context3(*codec);
    if (!c) {
        av_log(NULL, AV_LOG_ERROR, "Could not alloc an encoding context\n");
        return;
    }
    ost->enc = c;

    switch ((*codec)->type) {
    case AVMEDIA_TYPE_AUDIO:
        c->sample_fmt = (*codec)->sample_fmts ? (*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        c->bit_rate = 64000;
        c->sample_rate = 44100;
        if ((*codec)->supported_samplerates) {
            c->sample_rate = (*codec)->supported_samplerates[0];
            for (i = 0; (*codec)->supported_samplerates[i]; i++) {
                if ((*codec)->supported_samplerates[i] == 44100)
                    c->sample_rate = 44100;
            }
        }
        c->channels = av_get_channel_layout_nb_channels(c->channel_layout);
        c->channel_layout = AV_CH_LAYOUT_STEREO;
        if ((*codec)->channel_layouts) {
            c->channel_layout = (*codec)->channel_layouts[0];
            for (i = 0; (*codec)->channel_layouts[i]; i++) {
                if ((*codec)->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
                    c->channel_layout = AV_CH_LAYOUT_STEREO;
            }
        }
        c->channels = av_get_channel_layout_nb_channels(c->channel_layout);
        ost->st->time_base = (AVRational){1, c->sample_rate};
        break;

    case AVMEDIA_TYPE_VIDEO:
        c->codec_id = codec_id;

        //c->bit_rate = 100000;
        /* Resolution must be a multiple of two. */
        c->width = w;
        c->height = h;
        /* timebase: This is the fundamental unit of time (in seconds) in terms
         * of which frame timestamps are represented. For fixed-fps content,
         * timebase should be 1/framerate and timestamp increments should be
         * identical to 1. */
        ost->st->time_base = (AVRational){1, framerate};
        c->time_base = ost->st->time_base;

        //c->gop_size      = 12; /* emit one intra frame every twelve frames at most */
        if (c->codec_id == AV_CODEC_ID_GIF) {
            c->pix_fmt = AV_PIX_FMT_RGB8;
        }
        else if (c->codec_id == AV_CODEC_ID_MJPEG) {
            c->pix_fmt = AV_PIX_FMT_YUVJ420P;
        }
        else if (c->codec_id == AV_CODEC_ID_PNG) {
            c->pix_fmt = AV_PIX_FMT_RGB24;
        }
        else {
            c->pix_fmt = AV_PIX_FMT_YUV420P;
        }
        if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
            /* just for testing, we also add B-frames */
            c->max_b_frames = 2;
        }
        if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
            /* Needed to avoid using macroblocks in which some coeffs overflow.
             * This does not happen with normal video, it just happens here as
             * the motion of the chroma plane does not match the luma plane. */
            c->mb_decision = 2;
        }
        break;

    default:
        break;
    }

    /* Some formats want stream headers to be separate. */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
}

/**************************************************************/
/* video output */

static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height) {
    AVFrame *picture;
    int ret;

    picture = av_frame_alloc();
    if (!picture)
        return NULL;

    picture->format = pix_fmt;
    picture->width = width;
    picture->height = height;

    /* allocate the buffers for the frame data */
    ret = av_frame_get_buffer(picture, 32);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate frame data.\n");
        av_frame_free(&picture);
        picture = NULL;
    }

    return picture;
}

static void open_video(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg) {
    int ret;
    AVCodecContext *c = ost->enc;
    AVDictionary *opt = NULL;

    av_dict_copy(&opt, opt_arg, 0);

    /* open the codec */
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not open video codec: %s\n", av_err2str(ret));
        return;
    }

    /* allocate and init a re-usable frame */
    ost->frame = alloc_picture(c->pix_fmt, c->width, c->height);
    if (!ost->frame) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate video frame\n");
        return;
    }

    ost->tmp_frame = NULL;
    if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
        ost->tmp_frame = alloc_picture(AV_PIX_FMT_YUV420P, c->width, c->height);
        if (!ost->tmp_frame) {
            av_log(NULL, AV_LOG_ERROR, "Could not allocate temporary picture\n");
            return;
        }
    }
    ost->pkt = av_packet_alloc();
    if (!ost->pkt) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate video packet\n");
        return;
    }

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not copy the stream parameters\n");
        return;
    }
}

static AVFrame *get_video_frame(OutputStream *ost, AVFrame *frame) {
    AVCodecContext *c = ost->enc;

    if (c->pix_fmt != frame->format || c->width != frame->width) {
        /* when we pass a frame to the encoder, it may keep a reference to it
         * internally; make sure we do not overwrite it here */
        if (av_frame_make_writable(ost->frame) < 0)
            return 0;

        if (!ost->sws_ctx) {
            ost->sws_ctx = sws_getContext(frame->width, frame->height,
                                          frame->format,
                                          c->width, c->height,
                                          c->pix_fmt,
                                          SWS_FAST_BILINEAR, NULL, NULL, NULL);
            if (!ost->sws_ctx) {
                av_log(NULL, AV_LOG_ERROR,
                        "Could not initialize the conversion context\n");
                return 0;
            }
        }
        sws_scale(ost->sws_ctx, (const uint8_t *const *)frame->data,
                  frame->linesize, 0, frame->height, ost->frame->data,
                  ost->frame->linesize);
        ost->frame->pts = ost->next_pts++;
        return ost->frame;
    } else {
        frame->pts = ost->next_pts++;
        return frame;
    }
}

/*
 * encode one video frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
static int write_video_frame(AVFormatContext *oc, OutputStream *ost, AVFrame *frame) {
    int ret;
    AVCodecContext *c;

    c = ost->enc;

    frame = get_video_frame(ost, frame);

    /* encode the image */
    ret = avcodec_send_frame(c, frame);
    while (ret >= 0) {
        ret = avcodec_receive_packet(c, ost->pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            ret = 1;
            break;
        }
        else if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error during encoding\n");
            break;
        }
        ret = write_frame(oc, &c->time_base, ost->st, ost->pkt);
        av_packet_unref(ost->pkt);

        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error while writing video frame: %s\n", av_err2str(ret));
            break;
        }
    }

    return ret;
}

static void close_stream(AVFormatContext *oc, OutputStream *ost) {
    (void)oc;
    avcodec_free_context(&ost->enc);
    av_frame_free(&ost->frame);
    av_frame_free(&ost->tmp_frame);
    av_packet_free(&ost->pkt);
    sws_freeContext(ost->sws_ctx);
}

/* end 从 github.com/FFmpeg/FFmpeg/doc/examples/muxing.c 摘抄的代码 */

/* 
 自定义封装

 多路复用－解码－上下文
 */
typedef struct muxing_context {
    OutputStream video_st, audio_st; // 音视频的输出流
    AVOutputFormat *fmt; // 输出流的 AV Format
    AVFormatContext *oc; // 出流的 AV Format 的上下文
    AVCodec *audio_codec, *video_codec; // 音视频流的解码器
    AVDictionary *opt;
} muxing_context_t;

static void ensure_file_path(const char *filename) {
    int ix, len;
    len = strlen(filename);
    char *path = (char *)malloc(len + 1);
    memset(path, 0, len + 1);
    strcpy(path, filename);
    for (ix = 0; ix < len; ix++) {
        if (path[ix] == '/') {
            path[ix] = 0;
            if (access(path, F_OK) != 0) {
                mkdir(path, 0755);
            }
            path[ix] = '/';
        }
    }
    free(path);
}

/* 
视频文件，demux 将视频与音频文件分开解码（两者可以同时处理）
 */
void* muxing_begin(const char* formatname, const char* filename, const int dst_framerate, const int dst_width, const int dst_hight)
{
    int ret;
    // 使用 libavutil 提供的内存分配
    muxing_context_t* mctx = (muxing_context_t*)av_mallocz(sizeof(muxing_context_t));
    if (!mctx)
        goto end;
    // 准备输出的 media 文件的上下文，判断格式
    if (NULL != filename) {
        avformat_alloc_output_context2(&mctx->oc, NULL, NULL, filename);
    }
    else if (NULL != formatname) {
        if (strcmp(formatname, "jpg") == 0 || strcmp(formatname, "png") == 0) {
            avformat_alloc_output_context2(&mctx->oc, NULL, "image2", NULL);
        }else{
            avformat_alloc_output_context2(&mctx->oc, NULL, formatname, NULL);
        }
    }
    if (!mctx->oc) {
        av_log(NULL, AV_LOG_ERROR, "Could not deduce output format from file extension.\n");
        goto clean3;
    }

    /*
    当设置了AVFMT_NOFILE标志，
        AVOutputFormat 将不会携带 AVIOContext
        解复用器将调用avio_open函数打开一个调用者提供的未打开的文件
    */
    mctx->fmt = mctx->oc->oformat;
    if (NULL == filename) {
        mctx->fmt->flags = mctx->fmt->flags & ~AVFMT_NOFILE; // 先 not运算 取反标志位，再与运算，使 flag 的 AVFMT_NOFILE标志位 置零
    }
    else {
        mctx->fmt->flags = mctx->fmt->flags | AVFMT_NOFILE; // 或运算，使 flag 的 AVFMT_NOFILE标志位 置一
    }
    // 选择视频解码器id
    if (formatname && strcmp(formatname, "png") == 0) {
        mctx->fmt->video_codec = AV_CODEC_ID_PNG;
    }
    else if (formatname && strcmp(formatname, "jpg") == 0) {
        mctx->fmt->video_codec = AV_CODEC_ID_MJPEG;
    }
    av_log(NULL, AV_LOG_INFO, "out format name:%s", mctx->fmt->name);

    /* Add the audio and video streams using the default format codecs
     * and initialize the codecs. */
    // demux 分开同时处理音频与视频
    if (mctx->fmt->video_codec != AV_CODEC_ID_NONE) {
        add_stream(&mctx->video_st, mctx->oc, &mctx->video_codec, mctx->fmt->video_codec, dst_framerate, dst_width, dst_hight);
    }
    if (mctx->fmt->audio_codec != AV_CODEC_ID_NONE) {
        add_stream(&mctx->audio_st, mctx->oc, &mctx->audio_codec, mctx->fmt->audio_codec, dst_framerate, dst_width, dst_hight);
    }

    /* Now that all the parameters are set, we can open the audio and
     * video codecs and allocate the necessary encode buffers. */
    if (mctx->fmt->video_codec != AV_CODEC_ID_NONE)
        open_video(mctx->oc, mctx->video_codec, &mctx->video_st, mctx->opt);
        

    av_dump_format(mctx->oc, 0, filename, 1);

    /* open the output file, if needed */
    if (mctx->fmt->flags & AVFMT_NOFILE) { // Demuxer will use avio_open, no opened file should be provided by the caller.
            ensure_file_path(filename);
            ret = avio_open(&mctx->oc->pb, filename, AVIO_FLAG_WRITE);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Could not open '%s': %s\n", filename,
                        av_err2str(ret));
                goto clean2;
            }
    }
    else {
        ret = avio_open_dyn_buf(&mctx->oc->pb); // return new IO context
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Could not open dyn buf: %s\n",
                    av_err2str(ret));
            goto clean2;
        }
    }

    /* Write the stream header, if any. */
    ret = avformat_write_header(mctx->oc, &mctx->opt);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error occurred when opening output file: %s\n",
                av_err2str(ret));
        goto clean1;
    }
    goto end;
clean1:
    if (mctx->fmt->flags & AVFMT_NOFILE)
        /* Close the output file. */
        avio_closep(&mctx->oc->pb);
    else {
        unsigned char * buffer;
        avio_close_dyn_buf(mctx->oc->pb, &buffer);
        av_free(buffer);
    }
clean2:
    /* Close each codec. */
    if (mctx->fmt->video_codec != AV_CODEC_ID_NONE)
        close_stream(mctx->oc, &mctx->video_st);
    if (mctx->fmt->audio_codec != AV_CODEC_ID_NONE)
        close_stream(mctx->oc, &mctx->audio_st);

    /* free the stream */
    avformat_free_context(mctx->oc);
clean3:
    av_free(mctx);
    mctx = 0;
end:
    return (void *)mctx;
}

int muxing_write_video(void *ctx, AVFrame *frame) {
    if (ctx == NULL) {
        return -1;
    }
    muxing_context_t *mctx = ctx;
    if (mctx->fmt->video_codec != AV_CODEC_ID_NONE) {
        return write_video_frame(mctx->oc, &mctx->video_st, frame);
    }
    return 0;
}

int muxing_write_audio(void *ctx, AVFrame *frame) {
    if (ctx == NULL) {
        return -1;
    }
    muxing_context_t *mctx = ctx;
    if (mctx->fmt->audio_codec != AV_CODEC_ID_NONE) {
        //return write_audio_frame(mctx->oc, &mctx->audio_st, frame);
    }
    return 0;
}

int muxing_end(void *ctx, void* outbuff, int outbufflen, int* outsz) {
    unsigned char *buffer;
    if (ctx == NULL) {
        return -1;
    }

    muxing_context_t *mctx = ctx;
    /* Write the trailer, if any. The trailer must be written before you
     * close the CodecContexts open when you wrote the header; otherwise
     * av_write_trailer() may try to use memory that was freed on
     * av_codec_close(). */
    av_write_trailer(mctx->oc);

    /* Close each codec. */
    if (mctx->fmt->video_codec != AV_CODEC_ID_NONE)
        close_stream(mctx->oc, &mctx->video_st);
    if (mctx->fmt->audio_codec != AV_CODEC_ID_NONE)
        close_stream(mctx->oc, &mctx->audio_st);

    if (mctx->fmt->flags & AVFMT_NOFILE) {
        /* Close the output file. */
        avio_closep(&mctx->oc->pb);
    }
    else {
        if (outsz && outbuff) {
            *outsz = avio_close_dyn_buf(mctx->oc->pb, &buffer);
            /* Out of buff len */
            if (outbufflen < *outsz) {
                av_log(NULL, AV_LOG_ERROR, "outsz:%d larger than outbufflen:%d", *outsz, outbufflen);
                *outsz = 0;
            }
            else {
                memcpy(outbuff, buffer, *outsz);
            }
            av_free(buffer);
        }
    }

    /* free the stream */
    avformat_free_context(mctx->oc);

    av_free(mctx);
    return 0;
}
