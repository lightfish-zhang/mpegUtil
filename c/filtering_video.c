#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>

typedef struct filtering_context {
    AVFilterContext *buffersink_ctx;
    AVFilterContext *buffersrc_ctx;
    AVFilterGraph *filter_graph;
    AVFilterInOut *outputs;
    AVFilterInOut *inputs;
}filtering_context_t;

void* init_filters(const char *filters_descr, AVCodecContext* dec_ctx, int enc_pix_fmt)
{
    filtering_context_t* fctx = (filtering_context_t*)av_mallocz(sizeof(filtering_context_t));;
    char args[512];
    int ret = 0;
    const AVFilter *buffersrc  = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");
    fctx->outputs = avfilter_inout_alloc();
    fctx->inputs  = avfilter_inout_alloc();

    fctx->filter_graph = avfilter_graph_alloc();
    if (!fctx->outputs || !fctx->inputs || !fctx->filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* buffer video source: the decoded frames from the decoder will be inserted here. */
    snprintf(args, sizeof(args),
            "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
            dec_ctx->time_base.num, dec_ctx->time_base.den,
            dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);

    ret = avfilter_graph_create_filter(&fctx->buffersrc_ctx, buffersrc, "in",
                                       args, NULL, fctx->filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
        goto end;
    }

    /* buffer video sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&fctx->buffersink_ctx, buffersink, "out",
                                       NULL, NULL, fctx->filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
        goto end;
    }

    ret = av_opt_set_bin(fctx->buffersink_ctx, "pix_fmts",
            (uint8_t*)&enc_pix_fmt, sizeof(enc_pix_fmt),
            AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
        goto end;
    }

    /*
     * Set the endpoints for the filter graph. The filter_graph will
     * be linked to the graph described by filters_descr.
     */

    /*
     * The buffer source output must be connected to the input pad of
     * the first filter described by filters_descr; since the first
     * filter input label is not specified, it is set to "in" by
     * default.
     */
    fctx->outputs->name       = av_strdup("in");
    fctx->outputs->filter_ctx = fctx->buffersrc_ctx;
    fctx->outputs->pad_idx    = 0;
    fctx->outputs->next       = NULL;

    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    fctx->inputs->name       = av_strdup("out");
    fctx->inputs->filter_ctx = fctx->buffersink_ctx;
    fctx->inputs->pad_idx    = 0;
    fctx->inputs->next       = NULL;

    if ((ret = avfilter_graph_parse_ptr(fctx->filter_graph, filters_descr,
                                    &fctx->inputs, &fctx->outputs, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot parse filters_descr:%s\n", filters_descr);
        goto end;
    }

    if ((ret = avfilter_graph_config(fctx->filter_graph, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot config graph\n");
        goto end;
    }

end:
    return fctx;
}

void free_filters(void* ctx)
{
    filtering_context_t* filtering_ctx;
    if (!ctx)
        return ;
    filtering_ctx = (filtering_context_t*)ctx;

    avfilter_inout_free(&filtering_ctx->inputs);
    avfilter_inout_free(&filtering_ctx->outputs);
    avfilter_graph_free(&filtering_ctx->filter_graph);
    av_free(filtering_ctx);
}

int filtering(void* ctx, AVFrame* frame, AVFrame* filt_frame)
{
    int ret;

    if (!ctx || !frame || !filt_frame) {
        return -1;
    }
    filtering_context_t* filtering_ctx = (filtering_context_t*)ctx;

    /* push the decoded frame into the filtergraph */
    if (av_buffersrc_add_frame_flags(filtering_ctx->buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
        return -2;
    }

    /* pull filtered frames from the filtergraph */
    while (1) {
        ret = av_buffersink_get_frame(filtering_ctx->buffersink_ctx, filt_frame);
        if (ret >= 0)
            return 0;
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return 1;
        if (ret < 0)
            return -3;
    }
}
