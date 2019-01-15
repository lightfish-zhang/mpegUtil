
#include <libavcodec/avcodec.h>
#ifdef __cplusplus
extern "C" {
#endif
void* init_filters(const char *filters_descr, AVCodecContext* dec_ctx, int enc_pix_fmt);
void free_filters(void* ctx);
int filtering(void* ctx, AVFrame* frame, AVFrame* filt_frame);

#ifdef __cplusplus
}
#endif

