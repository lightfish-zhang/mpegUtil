#ifdef __GEN_THUMBNAIL_PROGRAM__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>

#include "gen_thumbnail.h"
#include "log.h"

#define INBUF_SIZE 1024*1024*10

void Ffmpeglog(int l, char* t) {
    if (l <= 32) {
        fprintf(stderr, "%d\t%s\n", l, t);
    }
}

int main(int argc, char **argv)
{
    char    *filename, *outfilename;
    FILE    *infile, *outfile;
    uint8_t *data, *buffer;
    size_t   fsize, outfilenamelen;
    int      data_size, data_len = 256 * 1024; 

    set_log_callback();

    if (argc <= 2) {
        fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
        exit(0);
    }
    filename    = argv[1];
    outfilename = argv[2];

    outfilenamelen = strlen(outfilename);
    if (outfilenamelen < 3) {
        fprintf(stderr, "outfilename err.%s", outfilename);
        exit(1);
    }

    infile = fopen(filename, "rb");
    if (!infile) {
        fprintf(stderr, "open file fail.%s", filename);
        exit(1);
    }
    if (0 != fseek(infile, 0, SEEK_END)) {
        fprintf(stderr, "seek file fail.%s", filename);
        exit(1);
    }
    fsize = ftell(infile);
    if (fsize < 0) {
        fprintf(stderr, "tell file fail.%s", filename);
        exit(1);
    }
    rewind(infile);
    buffer = (uint8_t *)malloc(fsize);
    if (fsize != fread(buffer, 1, fsize, infile) ) {
        fprintf(stderr, "read file fail.%s", filename);
        exit(1);
    }
    data = (uint8_t *)malloc(data_len);
    gen_thumbnail(&outfilename[outfilenamelen - 3], 320, buffer, fsize, data, data_len, &data_size);

    outfile = fopen(outfilename, "wb");
    if (!outfile) {
        fprintf(stderr, "open file fail.%s", outfilename);
        exit(1);
    }
    fwrite(data, 1, data_size, outfile);
    fclose(outfile);
    free(buffer);
    free(data);
    fclose(infile);

}
#endif  //__CGO__

