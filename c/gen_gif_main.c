#ifdef __GEN_GIF_PROGRAM__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>

#include "gen_gif.h"
#include "log.h"

#define INBUF_SIZE (10<<20)
#define OUTBUF_SIZE (10<<20)

void Ffmpeglog(int l, char* t) {
    if (l <= 32) {
        fprintf(stdout, "%d\t%s\n", l, t);
    }
}

int main(int argc, char **argv)
{
    char *filename, *outfilename;
    FILE *f, *outfile;
    uint8_t* data, *outData;
    size_t   data_size;
    int outSize;

    set_log_callback();

    if (argc <= 2) {
        fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
        exit(0);
    }
    filename    = argv[1];
    outfilename = argv[2];
    f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Could not open %s\n", filename);
        exit(1);
    }

    data = malloc(INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE);
    /* set end of buffer to 0 (this ensures that no overreading happens for damaged MPEG streams) */
    memset(data + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    outData = malloc(OUTBUF_SIZE);

    /* read raw data from the input file */
    data_size = fread(data, 1, INBUF_SIZE, f); // read 1 byte every time, ret = how many time, INBUF_SIZE = max time
    if (!data_size)
        exit(1);

    gen_gif(45, data, data_size, outData, OUTBUF_SIZE, &outSize);

    outfile = fopen(outfilename, "wb");
    if (!outfile) {
        fprintf(stderr, "open file fail.%s", outfilename);
        exit(1);
    }
    fwrite(outData, 1, outSize, outfile);
    fclose(outfile);
    free(outData);
    free(data);
    fclose(f);

}
#endif

