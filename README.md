# ffmpeg音视频编程入门

## 前言

本文以 ffmpeg 工具，讲述如何认识音视频编程，你可以了解到常见视频格式的大概样子，一步步学会如何使用 ffmpeg 的 C 语言 API

本文重于动手实践，代码仓库：[mpegUtil](https://github.com/lightfish-zhang/mpegUtil)

笔者的开发环境：Arch Linux 4.19.12, ffmpeg version n4.1

## 解码过程总览

以下是解码流程图，逆向即是编码流程

![](https://raw.githubusercontent.com/lightfish-zhang/mpegUtil/master/doc/decode_process.png)

本文是音视频编程入门篇，先略过传输协议层，主要讲格式层与编解码层的编程例子。

### 写在最前面的日志处理

边编程边执行，查看日志输出，是最直接的反馈，以感受学习的进度。对于 ffmpeg 的日志，需要提前这样处理：

```c
/* log.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavutil/log.h>

// 定义输出日志的函数，留白给使用者实现
extern void Ffmpeglog(int , char*);

static void log_callback(void *avcl, int level, const char *fmt, va_list vl) 
{
    (void) avcl;
    char log[1024] = {0};
    int n = vsnprintf(log, 1024, fmt, vl);
    if (n > 0 && log[n - 1] == '\n')
        log[n - 1] = 0;
    if (strlen(log) == 0)
        return;
    Ffmpeglog(level, log);
}

void set_log_callback()
{
    // 给 av 解码器注册日志回调函数
    av_log_set_callback(log_callback);
}
```

```c
/* main.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void Ffmpeglog(int l, char* t) {
    if(l <= AV_LOG_INFO)
        fprintf(stderr, "%s\n", t);
}
```

ffmpeg 有不同等级的日志，本文只需使用 `AV_LOG_INFO`  即可。


### 第一步，查看音视频格式信息

料理食材的第一步，得先懂得食材的来源和特性。

- 来源，互联网在线观看（http/rtmp）、播放设备上存储的视频文件（file）。
- 格式，如何查看视频文件的格式呢，以下有 unix 命令行示例，至于 windows 系统，查看文件属性即可。


```shell
# linux 上查看视频文件信息
ffmpeg -i example.mp4
```

以某个mp4文件为例，输出：

```
Input #0, mov,mp4,m4a,3gp,3g2,mj2, from 'example.mp4':
  Metadata:
    major_brand     : isom
    minor_version   : 512
    compatible_brands: isomiso2avc1mp41
    encoder         : Wxmm_900012345
  Duration: 00:00:58.21, start: 0.000000, bitrate: 541 kb/s
    Stream #0:0(und): Video: h264 (High) (avc1 / 0x31637661), yuv420p, 368x640, 487 kb/s, 24 fps, 24 tbr, 12288 tbn, 48 tbc (default)
    Metadata:
      handler_name    : VideoHandler
    Stream #0:1(und): Audio: aac (LC) (mp4a / 0x6134706D), 44100 Hz, stereo, fltp, 48 kb/s (default)
    Metadata:
      handler_name    : SoundHandler
```

根据命令输出信息，视频文件中有两个 stream, 即 video 与 audio，视频流与音频流。

- stream 0, 是视频数据，编码格式为h264，24 fps 意为 24 frame per second，即每秒24帧，比特率487 kb/s,
- stream 1, 是音频数据，编码格式为acc，采样率44100 Hz，比特率48 kb/s

#### 【编程实操】读取音视频流的格式信息

在互联网场景中，在线观看视频才是常见需求，那么，计算机如何读取视频流的信息呢，下面以 ffmpeg 代码讲述

```c
    /* C代码例子，省略了处理错误的逻辑 */

    AVFormatContext *fmt_ctx = NULL; // AV 格式上下文
    AVIOContext *avio_ctx = NULL; // AV IO 上下文
    unsigned char *avio_ctx_buffer = NULL; // input buffer

    fmt_ctx = avformat_alloc_context();// 获得 AV format 句柄
    avio_ctx_buffer = (unsigned char *)av_malloc(data_size); // ffmpeg分配内存的方法，给输入分配缓存

    /* fread(file) or memcpy(avio_ctx_buffer, inBuf, n)  省略拷贝文件流的步骤 */

    // 给 av format context 分配 io 操作的句柄，必要传参：输入数据的指针、数据大小、write_flag＝０表明buffer不可写，其他参数忽略，置 NULL
    avio_ctx = avio_alloc_context(avio_ctx_buffer, data_size, 0, NULL, NULL, NULL, NULL);
    fmt_ctx->pb = avio_ctx;

    // 打开输入的数据流，读取 header 的格式内容，注意必须的后续处理 avformat_close_input()
    avformat_open_input(&fmt_ctx, NULL, NULL, NULL)

    // 获取音视频流的信息
    avformat_find_stream_info(fmt_ctx, NULL) 

```

ffmpeg 有一个方法直接打印音视频信息

```c
av_dump_format(fmt_ctx, 0, NULL, 0);
```

print: (源码的输出格式凌乱，笔者整理过)

```
Input #0, mov,mp4,m4a,3gp,3g2,mj2, from '(null)':
Stream #0:0 : Video: h264 (High) (avc1 / 0x31637661), yuv420p, 368x640, 487 kb/s 24 fps,
Stream #0:1 : Audio: aac (LC) (mp4a / 0x6134706D), 44100 Hz, stereo, fltp, 48 kb/s
```

实践编程，获取音视频信息，当然需要细致地调用API，看下面代码

- 查看音视频流的索引、类型、解码器类型

```c
  avformat_find_stream_info(fmt_ctx, NULL);

  for(int i=0; i<fmt_ctx->nb_streams; i++){
      AVStream *stream = fmt_ctx->streams[i];
      AVCodecParameters *codec_par = stream->codecpar;
      av_log(NULL, AV_LOG_INFO, "find audio stream index=%d, type=%s, codec id=%d", 
        i, av_get_media_type_string(codec_par->codec_type), codec_par->codec_id);
  }
```

print:

```
find audio stream index=0, type=video, codec id=27
find audio stream index=1, type=audio, codec id=86018
```

- 看到没，上面代码只获得解码器的id值(枚举类型)，那么解码器的信息呢，加上下面代码，可以看到音视频流的格式，以及获得解码器句柄以便于解码步骤使用。

```c
  AVCodec *decodec = NULL;
  decodec = avcodec_find_decoder(codec_par->codec_id); // 获得解码器
  av_log(NULL, AV_LOG_INFO, "find codec name=%s\t%s", decodec->name, decodec->long_name);
```

print:

```
find audio stream index=0, type=video, codec id=27
find codec name=h264    H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10
find audio stream index=1, type=audio, codec id=86018
find codec name=aac     AAC (Advanced Audio Coding)
```

- 关于视频，可以查看帧率(一秒有多少帧画面)

```c
  // 获得一个分数
  AVRational framerate = av_guess_frame_rate(fmt_ctx, stream, NULL);
  av_log(NULL, AV_LOG_INFO, "video framerate=%d/%d", framerate.num, framerate.den);
```

print:

```
video framerate=24/1
```

至此，我们掌握了如何利用 ffmpeg 的 C 语言 API 来读取音视频文件流的信息

### 第二步，解码

简单说一下音视频文件的解码过程，对大部分音视频格式来说，在原始流的数据中，不同类型的流会按时序先后交错在一起，这是多路复用，这样的数据分布，即有利于播放器打开本地文件，读取某一时段的音视频时，方便进行fseek操作（移动文件描述符的读写指针）；也有利于网络在线观看视频，“空投”从某一刻开始播放视频，从文件某一段下载数据。

直观的看下面的循环读取文件流的代码

```c
    /* begin 解码过程 */
    AVPacket *pkt;
    AVFrame *frame;

    // 分配原始文件流packet的缓存
    pkt = av_packet_alloc();
    // 分配 AV 帧 的内存
    frame = av_frame_alloc();

    // 在循环中不断读取下一个文件流的 packet 包
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
      if(pkt->size){
            /*
            demux 解复用
            原始流的数据中，不同格式的流会交错在一起（多路复用）
            从原始流中读取的每一个 packet 的流可能是不一样的，需要判断 packet 的流索引，按类型处理
            */
            if(pkt->stream_index == video_stream_idx){
              //　此处省略处理视频的逻辑
            }else if(pkt->stream_index == audio_stream_idx){
              //　此处省略处理音频的逻辑
            }
        }
        av_packet_unref(pkt);
        av_frame_unref(frame);
    }
    /* end 解码过程 */

    // flush data
    avcodec_send_packet(video_decodec_ctx, NULL);
    avcodec_send_packet(audio_decodec_ctx, NULL);

```

上面代码是对音视频流进行解复用的主要过程，在循环中分别处理不同类型的流数据，到了这一步，就是使用解码器对循环中获取的 packet 包进行解码。


#### 解码前的准备

ffmepg 中，解码工具需要初始化好两个指针，一个是解码器，一个是解码器上下文，上下文是用来存储此次操作的变量集合，比如 io 的句柄、解码的帧数累加值，视频的帧率等等。让我们重新编写上面读取音视频流的循环，给音视频流分别分配好这两个指针，并且处理好错误返回值。（下面代码的 goto 语句暂且略过，后面再提）

```c
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

```

以上方式是循环读取文件的所有流，这样写有助于新手理解音视频文件包含各种流的知识点，若是业务需求，只要对单独一个流（比如视频流处理），可以用以下方式获取特定的流，在根据流的解码器id，分配解码工具。

```c
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not find stream information\n");
        goto clean1;
    }
    ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not find %s stream\n",
                av_get_media_type_string(type));
        return ret;
    }
    int stream_index = ret;
    AVStream *st = fmt_ctx->streams[stream_index];
```


#### 解码的循环

修改上面解码的循环，以视频流为例，如何从流中读取帧，为便于理解，在关键处注释清楚。

```c
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
```

#### 回收内存

上文的代码中，多次出现 goto 语句，我认为适当的使用 goto 使编程更加方便，比如执行过程结束的清理工作，以下是回收 ffmpeg AV 库产生的各种变量的内存，C/C++语言编程都需要多注意这一点。

```c
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
```


#### 自由发挥

看到了这里，可以说入门 ffmpeg 编程了，什么，你问后面的转码怎么做？笔者就留白了，本文已经介绍了最基本的解码过程了，编码也就是逆向过程，我建议阅读 ffmepg 官方源码的example，以及多了解音视频各种格式的知识。


#### 实际例子

我提供两个小例子在 github 上

- [转码GIF](https://github.com/lightfish-zhang/mpegUtil/blob/master/c/gen_gif.c)
- [生成缩略图](https://github.com/lightfish-zhang/mpegUtil/blob/master/c/gen_thumbnail.c)


请安装好 linux 下 ffmepg 环境，找到例子代码里的 Ｍakefile 文件编译，例如：

```
make -f Makefile_test_dump_info
```

以后我会将这两个小例子修改，实现跨语言调用，如 nodejs addon 或 golang cgo


## Reference

[ffmpeg example](https://github.com/FFmpeg/FFmpeg/tree/master/doc/examples) (本文代码就是从example改过来的)

