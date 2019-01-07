UNAME := $(shell uname)

CPP=g++ 
CPPFLAGS=-g -I./ -I/usr/local/include -D__GEN_THUMBNAIL_PROGRAM__
LDFLAGS:=$(LDFLAGS) -lavcodec -lavformat -lswscale -lavutil -lavfilter -lm
#CFLAGS := -O3

LIBOBJS:=gen_thumbnail.o muxing.o filtering_video.o log.o
OBJS:=gen_thumbnail_main.o

LIBRARY:=libffmpeg_wrap.a
PROGRAM:=gen_thumbnail

all: $(PROGRAM) 
$(PROGRAM): $(OBJS) $(LIBRARY)
	$(PURIFY) $(CPP) -o $@ $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS)
$(LIBRARY): $(LIBOBJS)
	ar -r -o $@ $^

.PHONY: clean
clean:
	rm -f $(OBJS); rm -f $(PROGRAM); rm -f $(LIBOBJS); rm -f $(LIBRARY);
