#ifdef __DUMP_INFO_PROGRAM__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>

#include "dump_info.h"
#include "log.h"

#define INBUF_SIZE (10<<20)

void Ffmpeglog(int l, char* t) {
    if(l <= AV_LOG_INFO)
        fprintf(stderr, "%s\n", t);
}

int main(int argc, char **argv)
{
    char *filename, *outfilename;
    FILE *f;
    uint8_t* data;
    size_t   data_size;

    set_log_callback();

    if (argc <= 1) {
        fprintf(stderr, "Usage: %s <input file>\n", argv[0]);
        exit(0);
    }
    filename    = argv[1];
    f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Could not open %s\n", filename);
        exit(1);
    }

    data = malloc(INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE);
    /* set end of buffer to 0 (this ensures that no overreading happens for damaged MPEG streams) */
    memset(data + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);


    /* read raw data from the input file */
    data_size = fread(data, 1, INBUF_SIZE, f); // read 1 byte every time, ret = how many time, INBUF_SIZE = max time
    if (!data_size)
        exit(1);

    dump_info(data, data_size);

    free(data);
    fclose(f);

}
#endif

