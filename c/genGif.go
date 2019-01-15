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
