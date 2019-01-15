
#include <libavcodec/avcodec.h>
#ifdef __cplusplus
extern "C" {
#endif

void* muxing_begin(const char* formatname, const char* filename, const int dst_framerate, const int dst_width, const int dst_hight);
int muxing_write_video(void* ctx, AVFrame* frame) ;
int muxing_write_audio(void* ctx, AVFrame* frame) ;
int muxing_end(void* ctx, void* outbuff, int outbufflen, int* outsz);

#ifdef __cplusplus
}
#endif

