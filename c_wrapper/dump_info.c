#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

int dump_info(void* data, int data_size){
    int ret = -1;
    AVFormatContext *fmt_ctx = NULL; // AV 格式上下文
    AVIOContext *avio_ctx = NULL; // AV IO 上下文
    unsigned char *avio_ctx_buffer = NULL; // input buffer

    fmt_ctx = avformat_alloc_context(); // 获得 AV format 句柄
    if (NULL == fmt_ctx) {
        av_log(NULL, AV_LOG_ERROR, "Could not alloc format context\n");
        goto clean1;
    }
    avio_ctx_buffer = (unsigned char *)av_malloc(data_size); // ffmpeg分配内存的方法，给输入分配缓存
    if (NULL == avio_ctx_buffer) {
        av_log(NULL, AV_LOG_ERROR, "Could not alloc indata\n");
        goto clean1;
    }
    memcpy(avio_ctx_buffer, data, data_size); // 复制待处理的文件数据

    // 给 av format context 分配 io 操作的句柄，必要传参：输入数据的指针、数据大小、write_flag＝０表明buffer不可写，其他参数忽略，置 NULL
    avio_ctx = avio_alloc_context(avio_ctx_buffer, data_size, 0, NULL, NULL, NULL, NULL);
    if (NULL == avio_ctx) {
        av_log(NULL, AV_LOG_ERROR, "Could not alloc io context\n");
        av_free(avio_ctx_buffer);
        goto clean1;
    }
    fmt_ctx->pb = avio_ctx;

    // 打开输入的数据流，读取 header 的格式内容，注意必须的后续处理 avformat_close_input()
    if (avformat_open_input(&fmt_ctx, NULL, NULL, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not open input data\n");
        goto clean2;
    }

    /* retrieve stream information, Read packets of a media file to get stream information */
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not find stream information\n");
        goto clean2;
    }

    // av_dump_format(fmt_ctx, 0, NULL, 0);

    // find codec
    int video_stream_idx = -1, audio_stream_idx = -1;
    AVStream *video_stream = NULL, *audio_stream = NULL;
    AVCodecContext *video_decodec_ctx=NULL, *audio_decodec_ctx=NULL;

    // AVFormatContext.nb_stream 记录了该 URL 中包含有几路流
    for(int i=0; i<fmt_ctx->nb_streams; i++){
        AVStream *stream = fmt_ctx->streams[i];
        AVCodecParameters *codec_par = stream->codecpar;
        AVCodec *decodec = NULL;
        AVCodecContext *decodec_ctx = NULL;

        av_log(NULL, AV_LOG_INFO, "find audio stream index=%d, type=%s, codec id=%d", 
                i, av_get_media_type_string(codec_par->codec_type), codec_par->codec_id);

        // 获得解码器
        decodec = avcodec_find_decoder(codec_par->codec_id);
        if(!decodec){
            av_log(NULL, AV_LOG_ERROR, "fail to find decodec\n");
            goto clean2;
        }

        av_log(NULL, AV_LOG_INFO, "find codec name=%s\t%s", decodec->name, decodec->long_name);

        // 分配解码器上下文句柄
        decodec_ctx = avcodec_alloc_context3(decodec);
        if(!decodec_ctx){
            av_log(NULL, AV_LOG_ERROR, "fail to allocate codec context\n");
            goto clean2;
        }

        // 复制流信息到解码器上下文
        if(avcodec_parameters_to_context(decodec_ctx, codec_par) < 0){
            av_log(NULL, AV_LOG_ERROR, "fail to copy codec parameters to decoder context\n");
            avcodec_free_context(&decodec_ctx);
            goto clean2;
        }

        // 初始化解码器
        if ((ret = avcodec_open2(decodec_ctx, decodec, NULL)) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Failed to open %s codec\n", decodec->name);
            return ret;
        }

        if( stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
            // 视频的属性，帧率，这里 av_guess_frame_rate() 非必须，看业务是否需要使用帧率参数
            decodec_ctx->framerate = av_guess_frame_rate(fmt_ctx, stream, NULL);
            av_log(NULL, AV_LOG_INFO, "video framerate=%d/%d", decodec_ctx->framerate.num, decodec_ctx->framerate.den);
            video_stream_idx = i;
            video_stream = stream;
            video_decodec_ctx = decodec_ctx;
        } else if( stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO){
            audio_stream_idx = i;
            audio_stream = stream;
            audio_decodec_ctx = decodec_ctx;
        } 
    }


    av_log(NULL, AV_LOG_INFO, "video_decodec_ctx=%p audio_decodec_ctx=%p", video_decodec_ctx, audio_decodec_ctx);

    /* begin 解码过程 */
    AVPacket *pkt;
    AVFrame *frame;

    // 分配原始文件流packet的缓存
    pkt = av_packet_alloc();
    if (!pkt){
        av_log(NULL, AV_LOG_ERROR, "Could not allocate video frame\n");
        goto clean3;
    }

    // 分配 AV 帧 的内存，返回指针
    frame = av_frame_alloc();
    if (!frame) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate video frame\n");
        goto clean4;
    }

    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if(pkt->size){
            /*
            demux 解复用
            原始流的数据中，不同格式的流会交错在一起（多路复用）
            从原始流中读取的每一个 packet 的流可能是不一样的，需要判断 packet 的流索引，按类型处理
            */
            if(pkt->stream_index == video_stream_idx){
                // 向解码器发送原始压缩数据 packet
                if((ret = avcodec_send_packet(video_decodec_ctx, pkt)) < 0){
                    av_log(NULL, AV_LOG_ERROR, "Error sending a packet for decoding, ret=%d", ret);
                    break;
                }
                /*
                解码输出视频帧
                avcodec_receive_frame()返回 EAGAIN 表示需要更多帧来参与编码
                像 MPEG等格式, P帧(预测帧)需要依赖I帧(关键帧)或者前面的P帧，使用比较或者差分方式编码
                读取frame需要循环，因为读取多个packet后，可能获得多个frame
                */ 
                while(ret >= 0){
                    ret = avcodec_receive_frame(video_decodec_ctx, frame);
                    if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
                        break;
                    }

                    /* 
                    DEBUG 打印出视频的时间
                    pts = display timestamp
                    视频流有基准时间 time_base ，即每 1 pts 的时间间隔(单位秒)
                    使用 pts * av_q2d(time_base) 可知当前帧的显示时间
                    */
                    if(video_decodec_ctx->frame_number%100 == 0){
                        av_log(NULL, AV_LOG_INFO, "read video No.%d frame, pts=%d, timestamp=%f seconds", 
                            video_decodec_ctx->frame_number, frame->pts, frame->pts * av_q2d(video_stream->time_base));
                    }

                    /*
                    在第一个视频帧读取成功时，可以进行：
                    １、若要转码，初始化相应的编码器
                    ２、若要加过滤器，比如水印、旋转等，这里初始化 filter
                    */
                    if (video_decodec_ctx->frame_number == 1) {

                    }else{

                    }


                    av_frame_unref(frame);
                }
            }else if(pkt->stream_index == audio_stream_idx){

            }
        }

        av_packet_unref(pkt);
        av_frame_unref(frame);
    }
    /* end 解码过程 */

    // send NULL packet, flush data
    avcodec_send_packet(video_decodec_ctx, NULL);
    avcodec_send_packet(audio_decodec_ctx, NULL);

clean5:
    av_frame_free(&frame);
    //av_parser_close(parser);
clean4:
    av_packet_free(&pkt);
clean3:
    if(NULL != video_decodec_ctx)
        avcodec_free_context(&video_decodec_ctx);
    if(NULL != audio_decodec_ctx)
        avcodec_free_context(&audio_decodec_ctx);
clean2:
    av_freep(&fmt_ctx->pb->buffer);
    av_freep(&fmt_ctx->pb);
clean1:
    avformat_close_input(&fmt_ctx);
end:
    return ret;
}