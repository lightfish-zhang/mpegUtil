/* 
mpeg 的日志库用法

#include <libavutil/log.h>
// 设置日志级别
av_log_set_level(AV_LOG_DEBUG)
// 打印日志
av_log(NULL, AV_LOG_INFO,"...%s\n",op)

*/

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
    //　int vsnprintf(buffer,bufsize ,fmt, argptr) , va_list 是可变参数的指针，相关方法有: va_start(), type va_atg(va_list, type), va_end()
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
