#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "libzmbv/zmbv.h"


////////////////////////////////////////////////////////////////////////////////
typedef struct {
  off_t scrofs;
  off_t palofs;
} scrinfo_t;


static int screen_fd = -1;
static scrinfo_t *screens = NULL;
static int screen_count = 0;
static int screen_alloted = 0;
static uint8_t cur_pal[256*4];
static uint8_t cur_screen[320*240];


////////////////////////////////////////////////////////////////////////////////
static void add_screen (off_t scrofs, off_t palofs) {
  if (screen_count+1 > screen_alloted) {
    screen_alloted = ((screen_count+1)|0x7ff)+1;
    screens = (scrinfo_t *)realloc(screens, sizeof(screens[0])*screen_alloted);
    if (screens == NULL) { fprintf(stderr, "OUT OF MEMORY!\n"); exit(1); }
  }
  screens[screen_count].scrofs = scrofs;
  screens[screen_count].palofs = palofs;
  ++screen_count;
}


static void scan_screens (void) {
  uint16_t wdt = 0, hgt = 0;
  uint8_t bpp = 0;
  off_t lpal_ofs = 0;
  screen_fd = open("stream.raw", O_RDONLY);
  if (screen_fd < 0) { fprintf(stderr, "FATAL: can't open stream!\n"); exit(1); }
  for (;;) {
    uint8_t ec;
    if (read(screen_fd, &ec, 1) != 1) break;
    if (ec == 0) {
      // new screen
      if (wdt == 320 && hgt == 240 && bpp == 8) {
        off_t pos = lseek(screen_fd, 0, SEEK_CUR);
        add_screen(pos, lpal_ofs);
      }
      lseek(screen_fd, wdt*hgt, SEEK_CUR);
    } else if (ec == 1) {
      // param change
      read(screen_fd, &wdt, 2);
      read(screen_fd, &hgt, 2);
      read(screen_fd, &bpp, 1);
    } else if (ec == 2) {
      // palette change
      lpal_ofs = lseek(screen_fd, 0, SEEK_CUR);
      lseek(screen_fd, 768, SEEK_CUR);
    } else {
      fprintf(stderr, "UNKNOWN EVENT: 0x%02x\n", ec);
      exit(1);
    }
  }
  printf("%d screens found\n", screen_count);
}


////////////////////////////////////////////////////////////////////////////////
static void load_screen (int idx) {
  if (idx >= 0 && idx < screen_count) {
    uint8_t pal[768];
    lseek(screen_fd, screens[idx].scrofs, SEEK_SET);
    read(screen_fd, cur_screen, 320*240);
    lseek(screen_fd, screens[idx].palofs, SEEK_SET);
    read(screen_fd, pal, 768);
    for (int f = 0; f < 256; ++f) {
      /*
      uint8_t r = 255*pal[f*3+0]/63;
      uint8_t g = 255*pal[f*3+0]/63;
      uint8_t b = 255*pal[f*3+0]/63;
      */
      //cur_pal[f] = rgb2col(pal[f*3+0], pal[f*3+1], pal[f*3+2]);
      cur_pal[f*4+0] = pal[f*3+0];
      cur_pal[f*4+1] = pal[f*3+1];
      cur_pal[f*4+2] = pal[f*3+2];
      cur_pal[f*4+3] = 0;
    }
  }
}


////////////////////////////////////////////////////////////////////////////////
static void encode_screens (void) {
  zmbv_format_t fmt;
  int buf_size;
  int32_t written;
  void *buf;
  VideoCodec *vc;
  FILE *fo;
  vc = new VideoCodec();
  fmt = VideoCodec::BPPFormat(8);
  vc->SetupCompress(320, 240);
  buf_size = vc->NeededSize(320, 240, fmt);
  printf("buf_size: %d\n", buf_size);
  buf = malloc(buf_size);
  fo = fopen("stream.zmbv", "w");
  if (fo != NULL) {
    for (int f = 0; f < screen_count; ++f) {
      int flags = (f%300 ? VideoCodec::FLAGS_NONE : VideoCodec::FLAGS_KEYFRAME);
      if (f%256 == 0) {
        printf("\r [%d/%d]\r", f, screen_count);
      }
      load_screen(f);
      if (!vc->PrepareCompressFrame(flags, fmt, cur_pal, buf, buf_size)) {
        printf("\rFATAL: can't prepare frame for screen #%d\n", f);
        break;
      }
      for (int y = 0; y < 240; ++y) {
        void *lptr = cur_screen+y*320;
        vc->CompressLines(1, &lptr);
      }
      written = vc->FinishCompressFrame();
      if (written < 0) {
        printf("\rFATAL: can't finish frame for screen #%d\n", f);
        break;
      }
      fwrite(&written, 4, 1, fo);
      if (written > 0) fwrite(buf, written, 1, fo);
    }
    printf("\r [%d/%d]\n", screen_count, screen_count);
    fclose(fo);
  } else {
    printf("\rFATAL: can't create output file!\n");
  }
  free(buf);
  delete vc;
}


////////////////////////////////////////////////////////////////////////////////
int main (int argc, char *argv[]) {
  scan_screens();
  encode_screens();
  return 0;
}
