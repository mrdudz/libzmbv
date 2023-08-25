#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include <sys/stat.h>
#include <sys/types.h>

/* NOTE: options should be supplied at build time of the library (zmbv*.c) files */
/*#define ZMBV_USE_MINIZ*/
/*#define ZMBV_INCLUDE_DECODER*/
#include "libzmbv/zmbv.h"
#include "libzmbv/zmbv_avi.h"

////////////////////////////////////////////////////////////////////////////////

#define PALETTE_NUM_COLORS      256
#define PALETTE_COLORS_BPP      (8 * 3)

#define PALETTE_SIZE    (PALETTE_NUM_COLORS * (PALETTE_COLORS_BPP / 8))

#define VIDEO_WIDTH     320
#define VIDEO_HEIGHT    240

#define VIDEO_SIZE      (VIDEO_WIDTH * VIDEO_HEIGHT)

#define VIDEO_BPP           8

#define VIDEO_FPS           50
#define VIDEO_NUM_FRAMES    (VIDEO_FPS * 30)    // 30 seconds

#define AUDIO_FREQ          48000
#define AUDIO_CHANNELS      2

#define AUDIO_SIZE          (AUDIO_FREQ / VIDEO_FPS)
#define AUDIO_BUFFER_SIZE   (AUDIO_SIZE * AUDIO_CHANNELS)

////////////////////////////////////////////////////////////////////////////////
// each KEYFRAME_INTERVAL frame will be key one
#define KEYFRAME_INTERVAL  (300)

////////////////////////////////////////////////////////////////////////////////

static int max_frames_count = VIDEO_NUM_FRAMES;
static int frameno = 0;

static zmvb_init_flags_t iflg = ZMBV_INIT_FLAG_NONE;

static int complevel = -1;  /* compression level, -1 means default */

static int verbose = 0;

static uint8_t cur_pal[PALETTE_SIZE];
static uint8_t cur_screen[VIDEO_SIZE];

static int16_t cur_audio[AUDIO_BUFFER_SIZE];

////////////////////////////////////////////////////////////////////////////////
static void generate_video_frame (int idx) {
    int x, y;
    if (verbose > 1) { printf("generate_video_frame: %d\n", idx); }
#if 1
    for (y = 0; y < PALETTE_NUM_COLORS; ++y) {
        for (x = 0; x < (PALETTE_COLORS_BPP / 8); ++x) {
            cur_pal[(y * (PALETTE_COLORS_BPP / 8)) + x] = (y*(PALETTE_COLORS_BPP / 8)  + ((x*3) ^ idx)) & 0xff;
        }
    }
#endif
#if 1
    for (y = 0; y < VIDEO_HEIGHT; ++y) {
        for (x = 0; x < VIDEO_WIDTH; ++x) {
            cur_screen[(y * VIDEO_WIDTH) + x] = (((x * 3) + ((idx * 7) ^ ((y * 1) + idx)))) & 0xff;
        }
    }
#endif
#if 1
    // progress bar at the top
    for (y = 0; y < 10; ++y) {
        for (x = 0; x < (idx % VIDEO_WIDTH); ++x) {
            cur_screen[(y * VIDEO_WIDTH) + x] = 0xff;
        }
    }
#endif
}

static double smpcount;
static double get_audio_sample(void)
{
    smpcount+=0.0050;
    return sin(smpcount);
}

static void generate_audio_frame (int idx) {
    int x;
    idx = idx; /* get rid of warning */
    for (x = 0; x < AUDIO_BUFFER_SIZE; x+=2) {
        double smp = get_audio_sample() * 4000.0f;
        cur_audio[x] = (int16_t)smp;
        cur_audio[x+1] = (int16_t)smp;
    }
}

////////////////////////////////////////////////////////////////////////////////
static int do_encode_screens (zmbv_avi_t zavi) {
    int res = -1;
    zmbv_format_t fmt;
    int buf_size;
    int32_t written;
    void *buf;
    int flags;
    zmbv_codec_t zcodec;

    zcodec = zmbv_codec_new(iflg, complevel);
    if (zcodec == NULL) {
        printf("FATAL: can't create codec!\n");
        return -1;
    }
    fmt = zmbv_bpp_to_format(VIDEO_BPP);
    buf_size = zmbv_work_buffer_size(VIDEO_WIDTH, VIDEO_HEIGHT, fmt);
    buf = malloc(buf_size);
    if (buf == NULL) {
        printf("FATAL: can't init buffer!\n");
        goto quit;
    }
    if (zmbv_encode_setup(zcodec, VIDEO_WIDTH, VIDEO_HEIGHT) < 0) {
        printf("FATAL: can't init encoder!\n");
        goto quit;
    }

    frameno = 0;

    while (frameno < max_frames_count) {

        flags = ((frameno % KEYFRAME_INTERVAL == 0) ? ZMBV_PREP_FLAG_KEYFRAME : ZMBV_PREP_FLAG_NONE);
#if 1
        generate_audio_frame(frameno);
        generate_video_frame(frameno);
#endif
        frameno++;

        if (verbose > 1) {
            printf("do_encode_screens: frame %d\n", frameno);
        }
#if 1
        // encode video frame
        if (zmbv_encode_prepare_frame(zcodec, flags, fmt, cur_pal, buf, buf_size) < 0) {
            printf("FATAL: can't prepare frame for screen #%d\n", frameno);
            break;
        }
        for (int y = 0; y < VIDEO_HEIGHT; ++y) {
            if (zmbv_encode_line(zcodec, cur_screen + (y * VIDEO_WIDTH)) < 0) {
                printf("FATAL: can't encode line #%d for screen #%d\n", y, frameno);
                break;
            }
        }
        written = zmvb_encode_finish_frame(zcodec);
        if (written < 0) {
            printf("FATAL: can't finish frame for screen #%d\n", frameno);
            break;
        }
#else
        written = 10;
#endif
#if 1
        // write avi chunk
        if (zmbv_avi_write_chunk_video(zavi, buf, written) < 0) {
            printf("FATAL: can't write video frame for screen #%d\n", frameno);
            break;
        }
#endif
#if 1
        // write avi chunks
        if (zmbv_avi_write_chunk_audio(zavi, cur_audio, AUDIO_SIZE) < 0) {
            printf("FATAL: can't write audio frame for screen #%d\n", frameno);
            break;
        }
#endif
    }
    res = 0;
quit:
    if (buf != NULL) { free(buf); }
    zmbv_codec_free(zcodec);
    return res;
}

static void encode_screens_to_avi (char *outname) {
    zmbv_avi_t zavi;

    zavi = zmbv_avi_start(outname, VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_FPS, AUDIO_FREQ);
    if (zavi == NULL) {
        printf("FATAL: can't create output file!\n");
        return;
    }
    do_encode_screens(zavi);
    zmbv_avi_stop(zavi);
}

////////////////////////////////////////////////////////////////////////////////
int main (int argc, char *argv[]) {
    argc = argc; /* get rid of warning */
    argv = argv; /* get rid of warning */

    complevel = 4;

#ifdef ZMBV_USE_MINIZ
    iflg |= ZMBV_INIT_FLAG_NOZLIB;
#endif
    printf("using compression level %d\n", complevel);

    max_frames_count = VIDEO_NUM_FRAMES;
    printf("generating %d frames\n", max_frames_count);

    printf("encoding to avi...\n");
    encode_screens_to_avi("outstream.avi");

    return 0;
}
