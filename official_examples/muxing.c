/*
 * Copyright (c) 2003 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * libavformat API example.
 *
 * Output a media file in any supported libavformat format. The default
 * codecs are used.
 * @example muxing.c
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>            /* 包含了有关音频通道布局的信息 */
#include <libavutil/opt.h>                       /* 用于处理 FFmpeg 中的选项，这些选项用于配置不同编码器和过滤器的参数 */
#include <libavutil/mathematics.h>               /* 包含了一些数学函数和常量，用于进行时间码、时间戳等计算 */
#include <libavutil/timestamp.h>                 /* 提供了一些处理时间戳的函数，这在音视频处理中非常重要，用于确定帧的时间顺序和持续时间等信息 */
#include <libavcodec/avcodec.h>                  /* 包含了音视频编解码器的定义和函数，允许你进行音视频编码和解码操作 */
#include <libavformat/avformat.h>                /* 包含了多种媒体格式的定义和函数，用于音视频文件的读取和写入 */
#include <libswscale/swscale.h>                  /* 提供了图像缩放和转换的功能，用于处理视频帧的大小和格式 */
#include <libswresample/swresample.h>            /* 用于音频重采样的功能，允许你改变音频的采样率和通道数 */

#define STREAM_DURATION   10.0                   /* 视频流的持续时间（单位：秒） */
#define STREAM_FRAME_RATE 25                     /* 视频流的帧率（每秒帧数）*/
#define STREAM_PIX_FMT    AV_PIX_FMT_YUV420P     /* 默认视频像素格式 */
#define SCALE_FLAGS SWS_BICUBIC                  /* 视频像素格式转换的标志，*/


/**
 * @brief 这是一个封装输出 AVStream 的结构体，用于存储编码器相关的信息，以及当前帧的时间戳
 * 可管理输出流的各种信息和相关的数据结构，以便在音视频处理过程中能够有效的进行数据封装、编码和格式转换等操作
 */
typedef struct OutputStream {
    // 指向音视频输出流的指针，表示输出流的相关信息，比如编码参数，时间基准等
    AVStream *st;
    // 指向音视频编码器上下文的指针，表示与输出流相关联的编码器的参数和状态
    AVCodecContext *enc;

    // 表示下一个将要生成的帧的显示时间戳
    int64_t next_pts;
    // 记录已经处理的样本数
    int samples_count;

    // 指向音视频帧的指针，用于存储待编码的音视频数据
    AVFrame *frame;
    // 用于临时存储音视频帧的指针，通常在编码之前或者处理过程中进行临时存储
    AVFrame *tmp_frame;

    // 指向音视频数据包的指针，用于存储编码后的音视频数据
    AVPacket *tmp_pkt;

    // 用于控制时间戳的参数，通常用于计算下一个帧的时间戳
    float t, tincr, tincr2;

    // 指向 SwsContext 结构体的指针，表示用于"视频帧转换"的上下文
    struct SwsContext *sws_ctx;
    // 指向 SwsContext 结构体的指针，表示用于"音频帧重采样"的上下文
    struct SwrContext *swr_ctx;
} OutputStream;





/**
 * @brief 这个函数用于输出 AVPacket 的信息，包括 pts（显示时间戳）、dts（解码时间戳）、时长
 * @param fmt_ctx 指向音视频格式上下文的常量指针，包含了音视频文件的相关信息，如编解码器、流信息
 * @param pkt 指向音视频包的常量指针，包含了音视频数据以及时间戳等信息。
 */
static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{
    // 获取音视频流的时间基准，用于将时间戳转换成可读的时间格式
    //
    // 时间基准（Time Base）是在多媒体处理中用于表示时间的概念。它是一个分数，通常以分子和分母的形式表示，其中分子表示时间的单位，分母表示每秒钟
    // 的计数。时间基准的主要作用是将时间从以纳秒或其他单位时间表示的时间戳转换为可读的时间格式，例如小时、分钟、秒、毫秒等。
    //
    // 在多媒体处理中，不同的媒体流（如音频和视频）通常都有自己的时间基准，因为它们可能以不同的速率进行采样或播放。这意味着对于同一时刻，音频流和
    // 视频流的时间戳可能是不同的。
    //
    // 时间基准的常见应用包括：
    // 1. 时间戳转换：将原始时间戳（通常是以时间基准的分数形式表示）转换为可读的时间格式，以便显示给用户或进行时间相关的操作。
    // 2. 同步：将不同媒体流的时间戳进行同步，以确保音频和视频等媒体在播放时能够正确的同步。
    // 3. 媒体编辑和处理： 在编辑和处理多媒体数据时，时间基准用于精确控制和调整媒体的时间。
    // 4. 时间码生成： 在视频制作中，时间基准常用于生成时间码，以便在后期制作中进行精确的编辑和同步。
    //
    // 总之，时间基准是多媒体处理中的重要概念，用于管理和表示时间，以确保多媒体数据在不同环境和应用中能够正确的处理和表示。不同的多媒体框架和标准
    // 可能使用不同的时间基准，因此在处理多媒体数据时需要注意时间基准的匹配和转换。
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    // pts: 音视频包的显示时间戳
    // pts_time: 以可读的格式显示音视频包的显示时间戳
    // dts: 音视频包的解码时间戳
    // dts_time: 以可读的时间格式显示音视频包的解码时间戳
    // duration: 音视频包的持续时间
    // duration_time: 以可读的时间格式显示音视频包的持续时间
    // stream_index: 音视频包所属的流的索引
    printf("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}





/**
 * @brief 这段代码是一个用于编码并写入帧数据到媒体文件的函数，它通常在音视频
 * 处理中用于将帧数据经过编码后写入媒体文件。
 * @param fmt_ctx 指向音视频格式上下文的常量指针，包含了音视频文件的相关信息，如编解码器、流信息
 * @param c 指向音视频编码器上下文的指针，表示与输出流相关联的编码器的参数和状态
 * @param st 指向输出流的指针，表示输出流的相关信息，包括流的编解码参数和时间基准等
 * @param frame 指向输入帧的指针，表示待编码的原始帧数据
 * @param pkt 指向输出数据包的指针，用于存储编码后的数据包
 * @return int 
 */
static int write_frame(AVFormatContext *fmt_ctx,
                       AVCodecContext *c,
                       AVStream *st,
                       AVFrame *frame,
                       AVPacket *pkt)
{
    int ret;

    // 将输入帧 frame 发送到编码器 c 进行编码。avcodec_send_frame 函数会将帧数据传递给编码器，但不会立即产生输出数据
    ret = avcodec_send_frame(c, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame to the encoder: %s\n",
                av_err2str(ret));
        exit(1);
    }

    while (ret >= 0)
    {
        // 在一个循环中，这行代码尝试从编码器 c 获取编码后的数据包，并将器存储在 pkt 中。如果返回 AVERROR(EAGAIN），表示
        // 编码器需要更多的输入数据；如果返回 AVERROR_EOF，表示编码器已经完成编码；如果返回负数，表示编码出现错误。
        ret = avcodec_receive_packet(c, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            break;
        }
        else if (ret < 0)
        {
            fprintf(stderr, "Error encoding a frame: %s\n", av_err2str(ret));
            exit(1);
        }

        // 这一行代码将输出数据包的时间戳从编码器时间基准（c->time_base）重新映射到输出流的时间基准（st->time_base）, 这是为了
        // 确保输出的时间戳与输出流的时间戳基准相匹配
        av_packet_rescale_ts(pkt, c->time_base, st->time_base);
        // 设置输出数据包的流索引，以指示数据包属于哪个输出流
        pkt->stream_index = st->index;

        log_packet(fmt_ctx, pkt);
        // 这一行代码将编辑后的的数据包写入到媒体文件当中。fmt_ctx 是表示媒体文件格式的上下文，pkt 包含了编码后的数据。函数会将
        // 数据包写入媒体文件，并自动处理时间戳和媒体文件的格式
        ret = av_interleaved_write_frame(fmt_ctx, pkt);
        /* pkt is now blank (av_interleaved_write_frame() takes ownership of
         * its contents and resets pkt), so that no unreferencing is necessary.
         * This would be different if one used av_write_frame(). */
        if (ret < 0) {
            fprintf(stderr, "Error while writing output packet: %s\n", av_err2str(ret));
            exit(1);
        }
    }

    return ret == AVERROR_EOF ? 1 : 0;
}





/**
 * @brief 用于向输出文件中添加一个输出流，并初始化相关的编码器参数
 * 这个函数的主要目的是为输出流配置编码器参数，并将其添加到输出媒体文件的格式上下文中，以便后续可以使用这些配置来编码和写入音视频
 * 数据。函数根据流的类型（音频或者视频）和选择的编码器类型来设置参数，以确保输出流的数据按照所需的方式编码和写入到媒体文件中。
 * @param ost 指向自定义的 OutputStream 结构体的指针用于表示输出流的相关信息，如编码器上下文，帧数据等。
 * @param oc 指向输出媒体文件的格式上下文的指针，用于表示输出文件的信息，如文件格式、流信息等。
 * @param codec 指向编码器指针的指针，用于存储找到的编码器
 * @param codec_id 一个枚举值，表示要使用的编码器的标识符
 */
static void add_stream(OutputStream *ost,
                       AVFormatContext *oc,
                       const AVCodec **codec,
                       enum AVCodecID codec_id)
{
    AVCodecContext *c;
    int i;

    // 根据给定的编码器标识符查找对应的编码器，并将其复制给 *codec 指针
    *codec = avcodec_find_encoder(codec_id);
    if (!(*codec))
    {
        fprintf(stderr, "Could not find encoder for '%s'\n",
                avcodec_get_name(codec_id));
        exit(1);
    }

    // 分配一个用于临时存储数据包的内存，并将其复制给 ost->tmp_pkt，这个数据包用于暂时存储编码后的数据
    ost->tmp_pkt = av_packet_alloc();
    if (!ost->tmp_pkt) {
        fprintf(stderr, "Could not allocate AVPacket\n");
        exit(1);
    }

    // 创建一个新的输出流并将其赋值给 ost->st, 这个新的输出流会添加到输出文件的格式上下文 oc 中。如果创建失败，会打印错误消息
    // 并终止程序
    ost->st = avformat_new_stream(oc, NULL);
    if (!ost->st) {
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }

    // 设置新创建的输出流的标识符，通常用于标识不同的流
    ost->st->id = oc->nb_streams - 1;
    // 为找到的编码器分配一个编码器上下文，并将其赋值给 ost->enc。编码器上下文包含了编码器的配置参数。
    c = avcodec_alloc_context3(*codec);
    if (!c) {
        fprintf(stderr, "Could not alloc an encoding context\n");
        exit(1);
    }
    ost->enc = c;

    // 根据编码器类型（音频或视频），来设置编码器参数，包括采样率、位率、分辨率、时间基准等
    // 1. 对于音频流，设置了音频编码参数，如采样格式、采样率、位率、声道、布局等
    // 2. 对于视频流，设置了视频编码参数，如位率、分辨率、帧率、像素格式等。
    // 这些参数会根据具体的编码器类型和需求设置
    switch ((*codec)->type) {
        case AVMEDIA_TYPE_AUDIO:
        {
            c->sample_fmt  = (*codec)->sample_fmts ? (*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
            c->bit_rate    = 64000;
            c->sample_rate = 44100;
            if ((*codec)->supported_samplerates)
            {
                c->sample_rate = (*codec)->supported_samplerates[0];
                for (i = 0; (*codec)->supported_samplerates[i]; i++)
                {
                    if ((*codec)->supported_samplerates[i] == 44100)
                    {
                        c->sample_rate = 44100;
                    }

                }
            }
            c->channels       = av_get_channel_layout_nb_channels(c->channel_layout);
            c->channel_layout = AV_CH_LAYOUT_STEREO;
            if ((*codec)->channel_layouts)
            {
                c->channel_layout = (*codec)->channel_layouts[0];
                for (i = 0; (*codec)->channel_layouts[i]; i++)
                {
                    if ((*codec)->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
                    {
                        c->channel_layout = AV_CH_LAYOUT_STEREO;
                    }
                }
            }
            c->channels        = av_get_channel_layout_nb_channels(c->channel_layout);
            ost->st->time_base = (AVRational){ 1, c->sample_rate };
            break;
        }
        case AVMEDIA_TYPE_VIDEO:
        {
            c->codec_id = codec_id;

            c->bit_rate = 400000;
            /* Resolution must be a multiple of two. */
            c->width    = 352;
            c->height   = 288;
            /* timebase: This is the fundamental unit of time (in seconds) in terms
             * of which frame timestamps are represented. For fixed-fps content,
             * timebase should be 1/framerate and timestamp increments should be
             * identical to 1. */
            ost->st->time_base = (AVRational){ 1, STREAM_FRAME_RATE };
            c->time_base       = ost->st->time_base;

            c->gop_size      = 12; /* emit one intra frame every twelve frames at most */
            c->pix_fmt       = STREAM_PIX_FMT;
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
        }
        default:
            break;
    }

    // 检查输出文件格式的标志位，如果需要在全局添加流头信息，则将编码器上下文的标志 AV_CODEC_FLAG_GLOBAL_HEADER
    // 设置为相应的值，这是一种格式要求，在某些情况下，流头信息需要被单独添加到输出流文件。
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
    {
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
}






/**
 * @brief 用于分配并配置一个音频帧。音频帧通常用于存储音频数据，以便后续进行编码、解码或处理。
 * 这个函数的主要作用是创建一个音频帧并为其设置相关属性，以便在处理音频数据时可以有效的使用它。
 * 通常，它用于音频解码或编码过程中，以便准备好的音频数据可以被编码器或解码器使用。
 * @param sample_fmt 一个枚举值，表示音频样本的格式，例如 "AV_SAMPLE_FMT_FLTP" 表示浮点数样本。
 * @param channel_layout 一个表示音频通道布局的整数值，例如 "AV_CH_LAYOUT_STEREO" 表示立体声通道布局。
 * @param sample_rate 音频采样率，表示每秒采集的音频样本数量。
 * @param nb_samples 音频中包含的音频样本数量
 * */
static AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt,
                                  uint64_t channel_layout,
                                  int sample_rate,
                                  int nb_samples)
{
    // 分配一个新的音频帧，音频帧用于存储音频数据
    AVFrame *frame = av_frame_alloc();
    int ret;

    if (!frame)
    {
        fprintf(stderr, "Error allocating an audio frame\n");
        exit(1);
    }

    // 设置音频样本的格式
    frame->format = sample_fmt;
    // 设置音频通道布局
    frame->channel_layout = channel_layout;
    // 设置音频采样率
    frame->sample_rate = sample_rate;
    // 设置音频帧中包含的音频样本数量
    frame->nb_samples = nb_samples;

    if (nb_samples)
    {
        // 分配音频缓冲区，用于存储音频样本数据
        ret = av_frame_get_buffer(frame, 0);
        if (ret < 0)
        {
            fprintf(stderr, "Error allocating an audio buffer\n");
            exit(1);
        }
    }

    return frame;
}






/**
 * @brief 用于初始化音频编码器的相关设置和参数包括打开编码器、设
 * 置参数、创建重采样器上下文等，并将音频流编码并写入容器中
 * @param oc 表示媒体格式的上下文，通常用于表示媒体文件的格式信息和写入媒体文件
 * @param codec 表示要使用的音频编码器，指定了音频数据的编码方式
 * @param ost 表示输出流，包括音频编码器的相关设置和参数
 * @param opt_arg 用于配置音频编码器的选项参数的字典
 */
static void open_audio(AVFormatContext *oc,
                       const AVCodec *codec,
                       OutputStream *ost,
                       AVDictionary *opt_arg)
{
    // 声明一个指向音频编码上下文结构体的指针
    AVCodecContext *c;
    // 用于存储样本数量
    int nb_samples;
    // 存储函数的返回值或者错误代码
    int ret;
    // 用于存储一些选项
    AVDictionary *opt = NULL;

    // 音频编码上下文
    c = ost->enc;

    // 拷贝传入的字典
    av_dict_copy(&opt, opt_arg, 0);
    // 打开音频编码器，并将选项 opt 应用于它
    ret = avcodec_open2(c, codec, &opt);
    // 释放字典
    av_dict_free(&opt);
    if (ret < 0)
    {
        fprintf(stderr, "Could not open audio codec: %s\n", av_err2str(ret));
        exit(1);
    }

    // 信号生成器时间
    ost->t     = 0;
    // 信号生成器，频率增加量
    ost->tincr = 2 * M_PI * 110.0 / c->sample_rate;
    // 信号生成器，频率增加量的增加量
    ost->tincr2 = 2 * M_PI * 110.0 / c->sample_rate / c->sample_rate;

    // 根据音频编码器的能力，设置 nb_samples，如果支持可变帧大小，则将 nb_samples 设置为 10000
    // 否则设置为 c->frame_size
    if (c->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
    {
        nb_samples = 10000;
    }
    else
    {
        nb_samples = c->frame_size;
    }


    // 分配音频帧和临时音频帧的内存空间
    ost->frame     = alloc_audio_frame(c->sample_fmt, c->channel_layout,
                                       c->sample_rate, nb_samples);
    ost->tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_S16, c->channel_layout,
                                       c->sample_rate, nb_samples);

    // 将音频编码器的参数复制到输出流的编解码器参数中
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0)
    {
        fprintf(stderr, "Could not copy the stream parameters\n");
        exit(1);
    }

    // 创建音频重采样器上下文
    ost->swr_ctx = swr_alloc();
    if (!ost->swr_ctx)
    {
        fprintf(stderr, "Could not allocate resampler context\n");
        exit(1);
    }

    // 设置音频重采样器上下文的参数，包括输入通道数、输入采样率、输入采样格式、输出通道数、输出采样率和输出采样格式
    av_opt_set_int       (ost->swr_ctx, "in_channel_count",   c->channels,       0);
    av_opt_set_int       (ost->swr_ctx, "in_sample_rate",     c->sample_rate,    0);
    av_opt_set_sample_fmt(ost->swr_ctx, "in_sample_fmt",      AV_SAMPLE_FMT_S16, 0);
    av_opt_set_int       (ost->swr_ctx, "out_channel_count",  c->channels,       0);
    av_opt_set_int       (ost->swr_ctx, "out_sample_rate",    c->sample_rate,    0);
    av_opt_set_sample_fmt(ost->swr_ctx, "out_sample_fmt",     c->sample_fmt,     0);

    /* initialize the resampling context */
    if ((ret = swr_init(ost->swr_ctx)) < 0)
    {
        fprintf(stderr, "Failed to initialize the resampling context\n");
        exit(1);
    }
}






/**
 * 生成音频帧并且填充音频数据
 * */
static AVFrame *get_audio_frame(OutputStream *ost)
{
    // 初始化为输出流中的临时音频帧
    AVFrame *frame = ost->tmp_frame;
    int j, i, v;
    // 初始化为音频帧中第一个数据平面的指针，用于填充音频帧的样本数据
    int16_t *q = (int16_t*)frame->data[0];

    // 比较时间戳，检查是否超过了预定的流时长，如果超过了就返回 null，不再生成更多的音频帧
    if (av_compare_ts(ost->next_pts,
                      ost->enc->time_base,
                      STREAM_DURATION,
                      (AVRational){ 1, 1 }) > 0)
    {
        return NULL;
    }


    // 进入一个循环，用于生成音频样本数据
    // 遍历音频帧中的每一个样本
    for (j = 0; j < frame->nb_samples; j++)
    {
        // 计算一个样本的音频数据。这里使用正弦函数来生成音频信号的振幅，乘以10000以将其缩放到合适的范围 ost->t 表示时间
        // 根据正弦函数的变化来生成音频波形
        v = (int)(sin(ost->t) * 10000);

        // 遍历每个音频通道
        for (i = 0; i < ost->enc->channels; i++)
        {
            // 将计算得到的音频数据存储到音频帧中，并移动指针 q 到下一个样本位置
            *q++ = v;
        }

        // 控制正弦波的时间和频率
        ost->t     += ost->tincr;
        ost->tincr += ost->tincr2;
    }

    // 设置音频帧的时间戳为下一个时间戳
    frame->pts = ost->next_pts;
    // 增加下一个时间戳，以确保下一个音频帧具有递增的时间戳
    ost->next_pts  += frame->nb_samples;

    // 返回填充好数据的音频帧
    return frame;
}






/**
 * 将生成的音频帧进行编码，并将编码后的音频数据写入到输出媒体文件中，同时更新时间戳和样本计数
 * */
static int write_audio_frame(AVFormatContext *oc, OutputStream *ost)
{
    // 表示音频编码器的上下文
    AVCodecContext *c;
    // 存储音频帧
    AVFrame *frame;
    // 存储函数返回值或者错误代码
    int ret;
    // 用于表示目标样本数
    int dst_nb_samples;
    // 将输出流结构体中的音频编码器上下文赋值给c
    c = ost->enc;
    // 获取音频帧
    frame = get_audio_frame(ost);

    if (frame)
    {
        // 将原音频帧的样本数与音频编码器的采样率进行比例缩放，以获得目标音频帧的样本数。这一步
        // 通常涉及到重采样，以确保音频数据与编码器的期望格式相匹配
        dst_nb_samples = av_rescale_rnd(swr_get_delay(ost->swr_ctx, c->sample_rate) + frame->nb_samples,
                                        c->sample_rate,
                                        c->sample_rate,
                                        AV_ROUND_UP);
        // 确保 dst_nb_samples 与源音频帧的样本数一致
        av_assert0(dst_nb_samples == frame->nb_samples);

        // 确保 ost->frame 可以被写入，因为编码器可能会在内部保留对输入帧的引用
        ret = av_frame_make_writable(ost->frame);
        if (ret < 0)
            exit(1);

        // 将音频数据从源格式转换为目标格式
        // ost->swr_ctx 是一个音频重采样器上下文，用于执行格式转换
        // ost->frame->data 存储着目标音频数据的位置
        // dst_nb_samples 表示目标样本数
        // frame->data 存储着源音频数据的位置
        // frame->nb_samples 表示源音频帧的样本数
        ret = swr_convert(ost->swr_ctx,
                          ost->frame->data,
                          dst_nb_samples,
                          (const uint8_t **)frame->data,
                          frame->nb_samples);
        if (ret < 0)
        {
            fprintf(stderr, "Error while converting\n");
            exit(1);
        }
        // 将frame更新为转换后的音频帧
        frame = ost->frame;
        // 根据样本计数和编码器的时间基准计算音频帧的时间戳
        frame->pts = av_rescale_q(ost->samples_count, (AVRational){1, c->sample_rate}, c->time_base);
        // 增加样本计数以跟踪以处理的样本数量
        ost->samples_count += dst_nb_samples;
    }

    // 将编码后的音频数帧写入到输出媒体文件中，其中包括媒体容器、音频编码器上下文、输出流、音频帧和临时数据包
    return write_frame(oc, c, ost->st, frame, ost->tmp_pkt);
}






/**
 * 为图像帧分配内存，并设置图像帧的基本属性，以便后续在图像处理或编码中使用。
 * 这是一个常见的多媒体处理函数，用于准备图像数据以供进一步处理
 * */
static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
    // 表示图像帧
    AVFrame *picture;
    // 存储函数返回值或者错误代码
    int ret;

    // 分配一个空的AVFrame结构体，并将地址赋值给 picture，这个函数
    // 分配了一个用于存储图像数据的框架，但是还没有分配数据缓冲区
    picture = av_frame_alloc();
    if (!picture)
    {
        return NULL;
    }

    // 设置图像帧的像素格式，通常是枚举值 pix_fmt, 表示图像的颜色编码格式，如 yuv420，rgb 等
    picture->format = pix_fmt;
    // 设置图像帧的宽度
    picture->width  = width;
    // 设置图像帧的高度
    picture->height = height;

    // 分配图像帧的数据缓冲区，这个函数会根据图像帧的属性（像素格式、宽度、高度等）自动分配合适大小的内存
    // 用于存储图像数据
    ret = av_frame_get_buffer(picture, 0);
    if (ret < 0)
    {
        fprintf(stderr, "Could not allocate frame data.\n");
        exit(1);
    }

    return picture;
}




/**
 * @brief 初始化视频编码器，包括打开编码器、分配图像帧内存、处理不同的像素格式以及复制流
 * 参数到复用器中。这是一个用于准备视频编码的关键函数，在多媒体处理应用程序中常用于处理和
 * 编码视频流
 * @param oc 表示媒体格式的上下文，通常用于表示媒体文件的格式信息和写入媒体文件
 * @param codec 表示要使用的视频编码器，指定了视频数据的编码方式，确定了视频数据的压缩格式
 * @param ost 表示输出流，包括视频编码器的相关设置和参数，例如视频编码器上下文、临时帧
 * @param opt_arg 表示用于配置视频编码器的选项参数的字典
 */
static void open_video(AVFormatContext *oc,
                       const AVCodec *codec,
                       OutputStream *ost,
                       AVDictionary *opt_arg)
{
    // 存储函数返回值或者错误代码
    int ret;
    // 将输出流中的编码器上下文赋值给c
    AVCodecContext *c = ost->enc;
    // 存储一些选项参数
    AVDictionary *opt = NULL;
    // 将传入的 opt_arg 字典拷贝到opt字典中，以便后续用于配置视频编码器的选项
    av_dict_copy(&opt, opt_arg, 0);

    // 打开视频编码器，并将 opt 应用与它
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0)
    {
        fprintf(stderr, "Could not open video codec: %s\n", av_err2str(ret));
        exit(1);
    }

    // 分配并且初始化一个重复使用的视频帧
    // 使用 alloc_picture 函数分配图像帧内存，传入了像素格式 c->pic_fmt,宽度 c->width 和高度 c->height
    ost->frame = alloc_picture(c->pix_fmt, c->width, c->height);
    if (!ost->frame)
    {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }

    // 根据像素格式 c->pic_fmt 的不同，可能需要分配一个临时的 yuv420p 格式图像帧， ost->tmp_frame
    // 如果视频的输出像素格式不是 yuv420p，就需要分配一个 yuv420p 格式的临时图像帧，后续会将它转换为所需要的输出格式
    ost->tmp_frame = NULL;
    if (c->pix_fmt != AV_PIX_FMT_YUV420P)
    {
        ost->tmp_frame = alloc_picture(AV_PIX_FMT_YUV420P, c->width, c->height);
        if (!ost->tmp_frame)
        {
            fprintf(stderr, "Could not allocate temporary picture\n");
            exit(1);
        }
    }

    // 将视频流的参数复制到复用器中，
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0)
    {
        fprintf(stderr, "Could not copy the stream parameters\n");
        exit(1);
    }
}




/**
 * @brief 生成一个 yuv 格式的图像数据帧，其中包含 y、cb、cr 分量的数据。
 * 生成的图像数据可以用于视频编码或处理，以产生特定的图像效果
 * @param pict 图像帧
 * @param frame_index 当前帧的索引或者帧号，用于生成图像数据的模式
 * @param width 图像的宽度
 * @param height 图像的高度
 */
static void fill_yuv_image(AVFrame *pict,
                           int frame_index,
                           int width,
                           int height)
{
    int x, y, i;

    // 将帧号赋值给变量i，用于生成图像数据的模式
    i = frame_index;

    // 遍历图像的每一行
    for (y = 0; y < height; y++)
    {
        // 遍历图像的每一列
        for (x = 0; x < width; x++)
        {
            // 当前像素位置 (x, y) 处， 将计算得到的Y分量值（亮度值）存储到图像帧的Y分量数据平面中。
            // 这个计算使用了 (x + y + i * 3)，其中 i 是帧号，用于生成不同的模式。
            pict->data[0][y * pict->linesize[0] + x] = x + y + i * 3;
        }
    }

    // 外部循环，遍历 Cb 和 Cr 分量的每一行，注意是图像高度的一半
    for (y = 0; y < height / 2; y++) {
        // 内部循环，遍历Cb和Cr分量的每一列，注意是图像宽度的一半。
        for (x = 0; x < width / 2; x++) {
            // 在当前位置 (x, y) 处，将计算得到的Cb分量值（蓝色色度值）存储到图像帧的Cb分量数据平面中
            pict->data[1][y * pict->linesize[1] + x] = 128 + y + i * 2;
            // 在当前位置 (x, y) 处，将计算得到的Cr分量值（红色色度值）存储到图像帧的Cr分量数据平面中。
            pict->data[2][y * pict->linesize[2] + x] = 64 + x + i * 5;
        }
    }
}





/**
 * @brief 生成视频帧并填充视频数据。
 * 该函数根据一定的时间间隔生成视频帧，然后将其准备好以供编码和写入媒体文件
 * @param ost 表示输出流，包含了与输出视频流相关的设置和参数，例如视频编码器的上下文、帧信息等。
 * @return AVFrame* 
 */
static AVFrame *get_video_frame(OutputStream *ost)
{
    // 指向视频编码器上下文的指针，用于表示视频编码器的参数和配置
    AVCodecContext *c = ost->enc;

    // 检查是否需要生成更多的视频帧，使用 av_compare_ts 函数比较 ost->next_pts 和 c->time_base
    // 以确定是否超过了预定的流时长 STREAM_DURATION, 如果超过了流时长，就返回 null，不再返回更多的视频帧
    if (av_compare_ts(ost->next_pts,
                      c->time_base,
                      STREAM_DURATION,
                      (AVRational){ 1, 1 }) > 0)
    {
        return NULL;
    }

    // 检查是否可以使帧可写，使用 av_frame_make_writable 函数确保 ost->frame 可以被写入
    // 因为编码器可能会在内部保留对输入帧的引用
    // 如果无法使帧可写，就退出程序
    if (av_frame_make_writable(ost->frame) < 0)
    {
        exit(1);
    }

    if (c->pix_fmt != AV_PIX_FMT_YUV420P)
    {
        // 如果视频帧的像素格式不是 AV_PIC_FMT_YUV420P，说明输出视频需要的像素格式与生成的图像格式不匹配
        // 创建一个图像格式转换上下文 ost->sws_ctx, 用于将生成的 YUV420P 格式图像转换为编码期望的格式 c->pix_fmt
        // 如果无法创建转换上下文，就在标准错误流中打印错误消息并退出程序
        if (!ost->sws_ctx)
        {
            ost->sws_ctx = sws_getContext(c->width,
                                          c->height,
                                          AV_PIX_FMT_YUV420P,
                                          c->width,
                                          c->height,
                                          c->pix_fmt,
                                          SCALE_FLAGS,
                                          NULL,
                                          NULL,
                                          NULL);
            if (!ost->sws_ctx)
            {
                fprintf(stderr, "Could not initialize the conversion context\n");
                exit(1);
            }
        }

        // 调用 fill_yuv_image 函数，根据 ost->next_pts、c->width、c->height
        // 生成 yuv 格式的图像数据。这个函数负责填充 y、db 和 cr 分量的数据
        fill_yuv_image(ost->tmp_frame, ost->next_pts, c->width, c->height);

        // 使用 sws_scale 函数将生成的 yuv 数据从 yuv420p 格式转换为编码器期望的像素格式 c->pic_fmt
        // ost->sws_ctx 是图像格式转换上下文
        // ost->tmp_frame 存储着待转换的图像数据
        // ost->frame 存储着转换后的图像数据
        sws_scale(ost->sws_ctx,
                  (const uint8_t * const *) ost->tmp_frame->data,
                  ost->tmp_frame->linesize,
                  0,
                  c->height,
                  ost->frame->data,
                  ost->frame->linesize);
    }
    else
    {
        // 如果像素格式是yuv420p，则直接调用 fill_yuv_image 函数填充 ost->frame
        fill_yuv_image(ost->frame, ost->next_pts, c->width, c->height);
    }

    // 设置生成的帧的时间戳，并递增 ost->next_pts，以确保每帧具有递增的时间戳
    ost->frame->pts = ost->next_pts++;

    // 返回填充好的视频帧
    return ost->frame;
}





/**
 * 编码，并将视频写入视频文件
*/
static int write_video_frame(AVFormatContext *oc, OutputStream *ost)
{
    return write_frame(oc, ost->enc, ost->st, get_video_frame(ost), ost->tmp_pkt);
}





/**
 * @brief 关闭输出流，并释放相应的内存资源
 * @param oc 
 * @param ost 
 */
static void close_stream(AVFormatContext *oc, OutputStream *ost)
{
    avcodec_free_context(&ost->enc);
    av_frame_free(&ost->frame);
    av_frame_free(&ost->tmp_frame);
    av_packet_free(&ost->tmp_pkt);
    sws_freeContext(ost->sws_ctx);
    swr_free(&ost->swr_ctx);
}





int main(int argc, char **argv)
{
    // 视频、音频输出流
    OutputStream video_st = { 0 }, audio_st = { 0 };
    // 指向输出格式的指针
    const AVOutputFormat *fmt;
    // 保存通过命令行传入的输出文件的文件名
    const char *filename;
    // 指向输出媒体上下文的指针，包含有关正在创建的媒体文件的信息
    AVFormatContext *oc;
    // 分别存储音频、视频编解码器的指针
    const AVCodec *audio_codec, *video_codec;
    // 用于存储各种调用的返回值
    int ret;
    int have_video = 0, have_audio = 0;
    int encode_video = 0, encode_audio = 0;
    AVDictionary *opt = NULL;
    int i;

    if (argc < 2)
    {
        printf("usage: %s output_file\n"
               "API example program to output a media file with libavformat.\n"
               "This program generates a synthetic audio and video stream, encodes and\n"
               "muxes them into a file named output_file.\n"
               "The output format is automatically guessed according to the file extension.\n"
               "Raw images can also be output by using '%%d' in the filename.\n"
               "\n", argv[0]);
        return 1;
    }

    filename = argv[1];
    for (i = 2; i + 1 < argc; i += 2)
    {
        if (!strcmp(argv[i], "-flags") || !strcmp(argv[i], "-fflags"))
        {
            av_dict_set(&opt, argv[i] + 1, argv[i + 1], 0);
        }
    }

    // 根据输出文件扩展名分配和初始化输出媒体上下文，如果无法推断，则使用 mpeg 格式
    avformat_alloc_output_context2(&oc, NULL, NULL, filename);
    if (!oc)
    {
        printf("Could not deduce output format from file extension: using MPEG.\n");
        avformat_alloc_output_context2(&oc, NULL, "mpeg", filename);
    }

    if (!oc)
    {
        return 1;
    }

    fmt = oc->oformat;

    // 检查输出格式支持的视频和音频编解码器，并将相应的流添加到输出媒体上下文。
    // 如果存在编解码器，则调用 add_stream 来设置流
    if (fmt->video_codec != AV_CODEC_ID_NONE)
    {
        add_stream(&video_st, oc, &video_codec, fmt->video_codec);
        have_video = 1;
        encode_video = 1;
    }
    if (fmt->audio_codec != AV_CODEC_ID_NONE)
    {
        add_stream(&audio_st, oc, &audio_codec, fmt->audio_codec);
        have_audio = 1;
        encode_audio = 1;
    }

    // 如果存在视频和音频流，则打开相应的编解码器，并分配必要的编码缓冲区
    if (have_video)
    {
        open_video(oc, video_codec, &video_st, opt);
    }

    if (have_audio)
    {
        open_audio(oc, audio_codec, &audio_st, opt);
    }

    // 打印输出格式及其流的信息
    av_dump_format(oc, 0, filename, 1);

    /* open the output file, if needed */
    if (!(fmt->flags & AVFMT_NOFILE))
    {
        // 打开输出文件
        ret = avio_open(&oc->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            fprintf(stderr,
                    "Could not open '%s': %s\n",
                    filename,
                    av_err2str(ret));
            return 1;
        }
    }

    // 写入流头信息
    ret = avformat_write_header(oc, &opt);
    if (ret < 0)
    {
        fprintf(stderr,
                "Error occurred when opening output file: %s\n",
                av_err2str(ret));
        return 1;
    }

    // 程序进入循环，直到视频和音频流都被完全编码和写入
    // 在循环中，根据视频和音频的时间戳选择要编码的流
    // 使用 write_video_frame 函数将编码帧写入媒体文件
    while (encode_video || encode_audio)
    {
        /* select the stream to encode */
        if (encode_video &&
            (!encode_audio || av_compare_ts(video_st.next_pts,
                                            video_st.enc->time_base,
                                            audio_st.next_pts,
                                            audio_st.enc->time_base) <= 0))
        {
            encode_video = !write_video_frame(oc, &video_st);
        } else {
            encode_audio = !write_audio_frame(oc, &audio_st);
        }
    }

    /* Write the trailer, if any. The trailer must be written before you
     * close the CodecContexts open when you wrote the header; otherwise
     * av_write_trailer() may try to use memory that was freed on
     * av_codec_close(). 
     * 写入媒体文件尾部
     * */
    av_write_trailer(oc);

    // 关闭编解码器
    if (have_video)
    {
        close_stream(oc, &video_st);
    }
    if (have_audio)
    {
        close_stream(oc, &audio_st);
    }

    if (!(fmt->flags & AVFMT_NOFILE))
    {
        /* Close the output file. */
        avio_closep(&oc->pb);
    }

    /* free the stream */
    avformat_free_context(oc);

    return 0;
}