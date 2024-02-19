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


static int zmbv_fd = -1;
static int screen_count = 0;
static uint8_t cur_pal[256*3];
static uint8_t cur_screen[320*240];
static int frameno;
static uint8_t *packed = NULL;
static int packed_size = 0;


////////////////////////////////////////////////////////////////////////////////
static void zmbv_open (void) {
  uint32_t scc;
  zmbv_fd = open("stream.zmbv", O_RDONLY);
  if (zmbv_fd < 0) { fprintf(stderr, "FATAL: can't open stream!\n"); exit(1); }
  if (read(zmbv_fd, &scc, 4) != 4) { fprintf(stderr, "FATAL: can't read screen count!\n"); exit(1); }
  if (scc == 0) { fprintf(stderr, "FATAL: no screens!\n"); exit(1); }
  screen_count = scc;
  // index count
  if (read(zmbv_fd, &scc, 4) != 4) { fprintf(stderr, "FATAL: can't read index count!\n"); exit(1); }
  // index table offset
  if (read(zmbv_fd, &scc, 4) != 4) { fprintf(stderr, "FATAL: can't read index table offset!\n"); exit(1); }
  printf("%d screens found\n", screen_count);
  frameno = 0;
}


static void zmbv_close (void) {
  if (zmbv_fd >= 0) close(zmbv_fd);
  zmbv_fd = -1;
  if (packed != NULL) free(packed);
  packed = NULL;
  packed_size = 0;
}


////////////////////////////////////////////////////////////////////////////////
static int next_screen (zmbv_codec_t zc) {
  if (frameno < screen_count) {
    uint32_t size;
    if (read(zmbv_fd, &size, 4) != 4) {
      printf("can't read packed frame size for frame #%d\n", frameno);
      return 0;
    }
    if (packed_size < size) {
      packed = realloc(packed, size);
      packed_size = size;
    }
    if (read(zmbv_fd, packed, size) != size) {
      printf("can't read packed frame data for frame #%d\n", frameno);
      return 0;
    }
    /* HACK! zmbv_decode_frame() will check the necessary things!
    if (size < 2 || packed[0] > 2) {
      printf("invalid packed frame data for frame #%d\n", frameno);
      return 0;
    }
    */
    //printf("%d\n", size);
    if (zmbv_decode_frame(zc, packed, size) < 0) {
      printf("can't decode packed frame #%d\n", frameno);
      return 0;
    }
    memcpy(cur_pal, zmbv_get_palette(zc), 768);
    for (int y = 0; y < 240; ++y) memcpy(cur_screen+y*320, zmbv_get_decoded_line(zc, y), 320);
    ++frameno;
    return 1;
  }
  return 0;
}


////////////////////////////////////////////////////////////////////////////////
static int do_decode_screens (int (*writer)(void *udata), void *udata) {
  int res = -1;
  zmbv_codec_t zc;
  zc = zmbv_codec_new(ZMBV_INIT_FLAG_NONE, -1);
  if (zc == NULL) { printf("FATAL: can't create codec!\n"); return -1; }
  if (zmbv_decode_setup(zc, 320, 240) < 0) { printf("FATAL: can't init decoder!\n"); goto quit; }
  frameno = 0;
  while (next_screen(zc)) {
    if (writer != NULL && writer(udata) < 0) {
      printf("\rFATAL: can't write uncompressed frame for screen #%d\n", frameno);
      break;
    }
  }
  if (frameno == screen_count) {
    res = 0;
  } else {
    printf("\rFATAL: invalid number of frames read; got %d, expected %d\n", frameno, screen_count);
  }
quit:
  zmbv_codec_free(zc);
  return res;
}


////////////////////////////////////////////////////////////////////////////////
static int writer_bin (void *udata) {
  if (write((long)udata, cur_pal, 768) != 768) return -1;
  if (write((long)udata, cur_screen, 320*240) != 320*240) return -1;
  return 0;
}


static void decode_screens (void) {
  uint32_t scc;
  int fd = open("stream.vpf.unp", O_WRONLY|O_CREAT|O_TRUNC, 0644);
  if (fd < 0) { printf("FATAL: can't create output file!\n"); return; }
  scc = screen_count;
  write(fd, &scc, 4);
  do_decode_screens(writer_bin, (void *)fd);
  close(fd);
}


////////////////////////////////////////////////////////////////////////////////
int main (void) {
  zmbv_open();
  decode_screens();
  zmbv_close();
  return 0;
}
