# 将ffmpeg封装出golang/cgo库

## 前言

继上一篇 [ffmpeg音视频C编程入门](http://lightfish.cn/2018/12/20/ffmpeg-primer/), 使用高性能的C语言进行音视频的处理，比较执行效率比较高，但是业务需求，快捷开发需要使用更方便的语言，比如 golang，本文介绍如何将 [将视频转成GIF](https://github.com/lightfish-zhang/mpegUtil/blob/master/c/gen_gif.c) 的C语言方法封装成 golang 方法以便调用。

## 认识cgo的封装技巧

最简单的 cgo 封装例子看这篇 [cgo快速入门](https://books.studygolang.com/advanced-go-programming-book/ch2-cgo/ch2-01-hello-cgo.html)

我这里讲几个注意事项

- CGO构建程序会自动构建当前目录下的C源文件，即是 go 会将当前目录下 *.c 文件都编译成 *.o目标文件，再链接汇编，这个特点衍生出几个注意事项:
    + go 代码以静态库或动态库方式引用 C 函数的话，需要将对应的C源文件移出 go源文件所在的目录
    + 如果想要将 C 函数编译到 go 程序，就需要将 C源文件与 go 文件放在同一目录下

- 在C/C++混编下， go 中引用 C 函数，需要将 C 函数名置于全局，即 `extern C`

## 开始编程

第一步，处理例子中已经写好的 gen_gif 方法，修改 gen_gif.h 文件

```c
#ifdef __cplusplus
extern "C" {
#endif

int gen_gif(const int gifSeconds, const int rotate, void* data, int data_size, void* outBuf, int outBufLen, int *outSize);

#ifdef __cplusplus
}
#endif
```

在处理 ffmpeg 的 av_log 日志的回调方法

```c
/* log.c */
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

```

第二步，编写 go 文件


```golang
package c

/*
#include <stdlib.h>
#include "gen_gif.h"
#include "log.h"
#cgo LDFLAGS: -lavcodec -lavformat -lswscale -lavutil -lavfilter -lm
*/
import "C"

import (
	"errors"
	"fmt"
	"unsafe"
)

func init() {
	C.set_log_callback()
}

var logger func(s string) = nil

func SetFfmpegLogger(f func(s string)) {
	logger = f
}

//export Ffmpeglog
func Ffmpeglog(l C.int, t *C.char) {
	if l <= 32 {
		if logger == nil {
			fmt.Printf("ffmpeg log:%s\n", C.GoString(t))
		} else {
			logger(fmt.Sprintf("ffmpeg log:%s\n", C.GoString(t)))
		}
	}
}

func GenGif(second, rotate int, input []byte) (err error, output []byte) {
	buf := make([]byte, 1<<20)
	var outsz C.int
	ret := C.gen_gif(C.int(second), C.int(rotate), unsafe.Pointer(&input[0]), C.int(len(input)), unsafe.Pointer(&buf[0]), C.int(len(buf)), &outsz)
	if ret != 0 {
		return errors.New(fmt.Sprintf("error, ret=%v", ret)), nil
	}
	output = make([]byte, outsz)
	copy(output, buf[:outsz])
	return nil, output
}

```

- 使用动态链接的方式，调用 ffmpeg 的 `libav*` 的函数库，设置 `#cgo LDFLAGS` 的动态链接选项
- 实现 `log.c` 中提供使用者实现的函数，使用 go 语言的方法打印日志
- 处理调用 C 函数传参的指针、变量，通过 `import "C"` 提供的方法转化变量

## 测试代码

将上文编写好的代码，提交到代码仓库，或者在本地的 GOPATH 中建好相应的目录，如笔者的 `github.com/lightfish-zhang/mpegUtil/c` 路径。

接下来，可以在业务需要的地方使用 `GenGif()` 了，让我们来测试一下


```golang
package main

import (
	"fmt"
	"io/ioutil"
	"os"

	mpegUtil "github.com/lightfish-zhang/mpegUtil/c"
)

func main() {
	if len(os.Args) < 3 {
		fmt.Printf("Usage: %s <input file> <output file>\n", os.Args[0])
		os.Exit(1)
	}
	inFile, err := os.Open(os.Args[1])
	if err != nil {
		fmt.Printf("open file fail, path=%v, err=%v", os.Args[1], err)
		os.Exit(1)
	}
	outFile, err := os.Create(os.Args[2])
	if err != nil {
		fmt.Printf("create file fail, path=%v, err=%v", os.Args[2], err)
		os.Exit(1)
	}
	input, err := ioutil.ReadAll(inFile)
	if err != nil {
		fmt.Printf("read file fail, err=%v", err)
		os.Exit(1)
	}

	err, output := mpegUtil.GenGif(5, 90, input)
	if err != nil {
		fmt.Printf("generate gif fail, err=%v", err)
		os.Exit(1)
	}
	_, err = outFile.Write(output)
	if err != nil {
		fmt.Printf("write file fail, err=%v", err)
		os.Exit(1)
	}
}

```

我在本地执行，对某一个 mp4 文件进行转码 GIF，如

```
./genGif test.mp4 test.gif
```

运行成功，在我的电脑上可以看到完美的 GIF 图片！

## 动态链接或者静态链接

golang 语言的优势是打包出一个静态的执行文件，可以在相同平台下运行。但是，我们例子是动态链接了 `ffmpeg` 的 `libav*.so`，在编译或者部署时，都需要在机器上安装好 ffmpeg 库。

有没有便利的方法进行编译或部署呢？

### 打包动态链接库

一个使用 `LD_LIBRARY_PATH` 指定动态链接目录的小技巧，不过这个技巧需要 编译机器与部署机器的运行环境差不多，除了 libav*.so 可以没有预先安装在部署机器。为了保持 `libc.so` 的函数可用，最好机器之间的 linux 版本一样。

使用 `ldd genGif` 查看编译出来的执行文件，需要动态链接哪些 `*.so` 文件，将关键的文件拷贝出来，比如 `libav*` 的 so 文件是必须的，其他比如 `libm.so`, `libz.so` 视部署机器有没有而定。

部署时候，需要把以上找到的 *.so 一起拷贝到目标机器上，同时在运行 golang 程序时，设置好全局变量 `LD_LIBRARY_PATH`，指向 `*.so` 文件目录。

### 静态链接编译

这个方法也需要机器的 linux 最好保持一致，ffmpeg 依赖的 libc.so 的函数好像会随着 linux 版本不同而有所差异。

准备好各种依赖库的静态库，参照如下命令

- 编译C例子

```
g++ -o gen_gif -I./ -I./ffmpeg/include -I/usr/local/include main.o gen_gif.a ./ffmpeg/lib/libavdevice.a ./ffmpeg/lib/libavfilter.a ./ffmpeg/lib/libavformat.a ./ffmpeg/lib/libavcodec.a  ./ffmpeg/lib/libavutil.a  ./ffmpeg/lib/libswreample.a  ./ffmpeg/lib/libswcale.a  ./lib/liblzma.a ./lib/libm.a ./lib/libz.a ./lib/libbz2.a -lpthread
```

- 编译 golang 例子，修改 `#cgo` 命令

```golang
/*
#cgo CFLAGS: -I./ffmpeg/include -I/usr/local/include
#cgo LDFLAGS: -L./lib -L./ffmpeg/lib
*/
```


## Reference

[cgo快速入门](https://books.studygolang.com/advanced-go-programming-book/ch2-cgo/ch2-01-hello-cgo.html)