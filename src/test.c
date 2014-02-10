#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "libzmbv/zmbv.h"
#include "libzmbv/zmbv_avi.h"


////////////////////////////////////////////////////////////////////////////////
typedef struct {
  off_t scrofs;
  off_t palofs;
} scrinfo_t;


static int screen_fd = -1;
static scrinfo_t *screens = NULL;
static int screen_count = 0;
static int screen_alloted = 0;
static uint8_t cur_pal[256*3];
static uint8_t cur_screen[320*240];
static int frameno;


static zmvb_init_flags_t iflg = ZMBV_INIT_FLAG_NONE;
static int complevel = -1;


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
  frameno = 0;
}


static void finish_screens (void) {
  if (screen_fd >= 0) close(screen_fd);
  screen_fd = -1;
  if (screens != NULL) free(screens);
  screens = NULL;
  screen_count = screen_alloted = 0;
}


////////////////////////////////////////////////////////////////////////////////
static void load_screen (int idx) {
  if (idx >= 0 && idx < screen_count) {
    lseek(screen_fd, screens[idx].scrofs, SEEK_SET);
    read(screen_fd, cur_screen, 320*240);
    lseek(screen_fd, screens[idx].palofs, SEEK_SET);
    read(screen_fd, cur_pal, 768);
  }
}


static int next_screen (void) {
  if (frameno < screen_count) {
    load_screen(frameno++);
    return 1;
  }
  return 0;
}


////////////////////////////////////////////////////////////////////////////////
static int do_encode_screens (int (*writer) (void *udata, const void *buf, uint32_t size), void *udata) {
  int res = -1;
  zmbv_format_t fmt;
  int buf_size;
  int32_t written;
  void *buf;
  zmbv_codec_t zc;
  zc = zmbv_codec_new(iflg, complevel);
  if (zc == NULL) { printf("FATAL: can't create codec!\n"); return -1; }
  fmt = zmbv_bpp_to_format(8);
  buf_size = zmbv_work_buffer_size(320, 240, fmt);
  //printf("buf_size: %d\n", buf_size);
  buf = malloc(buf_size);
  if (buf == NULL) goto quit;
  if (zmbv_encode_setup(zc, 320, 240) < 0) { printf("FATAL: can't init encoder!\n"); goto quit; }
  frameno = 0;
  while (next_screen()) {
    int flags = (frameno%300 ? ZMBV_PREP_FLAG_NONE : ZMBV_PREP_FLAG_KEYFRAME);
    if (zmbv_encode_prepare_frame(zc, flags, fmt, cur_pal, buf, buf_size) < 0) {
      printf("\rFATAL: can't prepare frame for screen #%d\n", frameno);
      break;
    }
    for (int y = 0; y < 240; ++y) {
      if (zmbv_encode_line(zc, cur_screen+y*320) < 0) {
        printf("\rFATAL: can't encode line #%d for screen #%d\n", y, frameno);
        break;
      }
    }
    written = zmvb_encode_finish_frame(zc);
    if (written < 0) {
      printf("\rFATAL: can't finish frame for screen #%d\n", frameno);
      break;
    }
    if (writer != NULL && writer(udata, buf, written) < 0) {
      printf("\rFATAL: can't write compressed frame for screen #%d\n", frameno);
      break;
    }
  }
  res = 0;
quit:
  if (buf != NULL) free(buf);
  zmbv_codec_free(zc);
  return res;
}


////////////////////////////////////////////////////////////////////////////////
static int writer_avi (void *udata, const void *buf, uint32_t size) {
  return zmbv_avi_write_chunk_video(udata, buf, size);
}


static void encode_screens_to_avi (void) {
  zmbv_avi_t zavi;
  zavi = zmbv_avi_start("stream.avi", 320, 240, 18);
  if (zavi == NULL) { printf("FATAL: can't create output file!\n"); return; }
  do_encode_screens(writer_avi, zavi);
  zmbv_avi_stop(zavi);
}


////////////////////////////////////////////////////////////////////////////////
static int writer_bin (void *udata, const void *buf, uint32_t size) {
  return (write((long)udata, buf, size) == size ? 0 : -1);
}


static void encode_screens_to_bin (void) {
  int fd = open("stream.zmbv", O_WRONLY|O_CREAT|O_TRUNC, 0644);
  if (fd < 0) { printf("FATAL: can't create output file!\n"); return; }
  do_encode_screens(writer_bin, (void *)fd);
  close(fd);
}


////////////////////////////////////////////////////////////////////////////////
int main (int argc, char *argv[]) {
  if (argc > 1) {
    //iflg |= ZMBV_INIT_FLAG_NOZLIB;
    complevel = atoi(argv[1]);
    printf("using compression level %d\n", complevel);
  }
  scan_screens();
  printf("encoding to zmbv...\n");
  encode_screens_to_bin();
  printf("encoding to avi...\n");
  encode_screens_to_avi();
  finish_screens();
  return 0;
}
