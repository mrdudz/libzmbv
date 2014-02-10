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
// each KEYFRAME_INTERVAL frame will be key one
#define KEYFRAME_INTERVAL  (300)


////////////////////////////////////////////////////////////////////////////////
typedef struct {
  off_t scrofs;
  off_t palofs;
} scrinfo_t;


static int screen_fd = -1;
static int screen_count = 0;
static uint8_t cur_pal[256*3];
static uint8_t cur_screen[320*240];
static int frameno;
static int this_is_keyframe;
static uint32_t *idxarray = NULL;
static int index_ptr = 0;


static zmvb_init_flags_t iflg = ZMBV_INIT_FLAG_NONE;
static int complevel = -1;


////////////////////////////////////////////////////////////////////////////////
static void scan_screens (void) {
  uint32_t scc;
  screen_fd = open("stream.vpf", O_RDONLY);
  if (screen_fd < 0) { fprintf(stderr, "FATAL: can't open stream!\n"); exit(1); }
  if (read(screen_fd, &scc, 4) != 4) { fprintf(stderr, "FATAL: can't read screen count!\n"); exit(1); }
  if (scc == 0) { fprintf(stderr, "FATAL: no screens!\n"); exit(1); }
  screen_count = scc;
  printf("%d screens found\n", screen_count);
  frameno = 0;
}


static void finish_screens (void) {
  if (screen_fd >= 0) close(screen_fd);
  screen_fd = -1;
}


////////////////////////////////////////////////////////////////////////////////
static void load_screen (int idx) {
  if (idx >= 0 && idx < screen_count) {
    lseek(screen_fd, 4+idx*(320*240+768), SEEK_SET);
    read(screen_fd, cur_pal, 768);
    read(screen_fd, cur_screen, 320*240);
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
    this_is_keyframe = ((frameno-1)%KEYFRAME_INTERVAL == 0);
    int flags = (this_is_keyframe ? ZMBV_PREP_FLAG_KEYFRAME : ZMBV_PREP_FLAG_NONE);
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
  if (this_is_keyframe) {
    off_t pos = lseek((long)udata, 0, SEEK_CUR);
    if (pos == (off_t)-1) return -1;
    idxarray[index_ptr++] = pos;
  }
  if (write((long)udata, &size, 4) != 4) return -1;
  return (write((long)udata, buf, size) == size ? 0 : -1);
}


static void encode_screens_to_bin (void) {
  uint32_t scc = screen_count;
  int fd = open("stream.zmbv", O_WRONLY|O_CREAT|O_TRUNC, 0644);
  if (fd < 0) { printf("FATAL: can't create output file!\n"); return; }
  if (idxarray != NULL) free(idxarray);
  // frame count
  write(fd, &scc, 4);
  // idxarray count
  scc = (screen_count/KEYFRAME_INTERVAL);
  if (scc%KEYFRAME_INTERVAL) ++scc;
  idxarray = malloc(scc*sizeof(idxarray[0]));
  index_ptr = 0;
  write(fd, &scc, 4);
  // idxarray offset
  write(fd, &scc, 4);
  do_encode_screens(writer_bin, (void *)fd);
  // write idxarray
  //printf("%d (%d)\n", (index_ptr == scc), index_ptr);
  scc = lseek(fd, 0, SEEK_CUR);
  write(fd, idxarray, index_ptr*sizeof(idxarray[0]));
  // update header
  lseek(fd, 4*2, SEEK_SET);
  write(fd, &scc, 4);
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
