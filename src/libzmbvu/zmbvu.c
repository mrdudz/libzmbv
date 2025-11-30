/*
 * Copyright (C) 2002-2013  The DOSBox Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * C translation by Ketmar // Invisible Vector
 */
#include "zmbvu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ZMBVU_USE_MINIZ
# include <zlib.h>
# define mz_deflateInit   deflateInit
# define mz_inflateInit   inflateInit
# define mz_deflateEnd    deflateEnd
# define mz_inflateEnd    inflateEnd
# define mz_deflateReset  deflateReset
# define mz_inflateReset  inflateReset
# define mz_deflate       deflate
# define mz_inflate       inflate
# define mz_stream        z_stream
# define MZ_OK            Z_OK
# define MZ_SYNC_FLUSH    Z_SYNC_FLUSH
#else
# ifdef MINIZ_NO_MALLOC
#  undef MINIZ_NO_MALLOC
# endif
# include "miniz.c"
# define mz_inflateReset(_strm)  ({ int res = mz_inflateEnd(_strm); if (res == MZ_OK) res = mz_inflateInit(_strm); res; })
#endif

#ifdef __clang__
#define ATTR_PACKED __attribute__((packed))
#elif defined(__GNUC__)
#define ATTR_PACKED __attribute__((packed,gcc_struct))
#elif defined(_MSC_VER)
#define ATTR_PACKED
#else
#warn "make sure to define ATTR_PACKED for your compiler"
#define ATTR_PACKED
#endif

/******************************************************************************/
#define DBZV_VERSION_HIGH  (0)
#define DBZV_VERSION_LOW   (1)

#define COMPRESSION_NONE  (0)
#define COMPRESSION_ZLIB  (1)

#define MAX_VECTOR  (16)

enum {
  FRAME_MASK_KEYFRAME = 0x01,
  FRAME_MASK_DELTA_PALETTE = 0x02
};


/******************************************************************************/
zmbvu_format_t zmbvu_bpp_to_format (int bpp) {
  switch (bpp) {
    case 8: return ZMBVU_FORMAT_8BPP;
    case 15: return ZMBVU_FORMAT_15BPP;
    case 16: return ZMBVU_FORMAT_16BPP;
    case 32: return ZMBVU_FORMAT_32BPP;
  }
  return ZMBVU_FORMAT_NONE;
}


/******************************************************************************/
typedef struct {
  int start;
  int dx, dy;
} zmbvu_frame_block_t;


typedef struct {
  int x, y;
  int slot;
} zmbvu_unpacker_vector_t;

#ifdef _MSC_VER
#pragma pack(push, 1)
#endif

typedef struct ATTR_PACKED {
  uint8_t high_version;
  uint8_t low_version;
  uint8_t compression;
  uint8_t format;
  uint8_t blockwidth;
  uint8_t blockheight;
} zmbvu_keyframe_header_t;

#ifdef _MSC_VER
#pragma pack(pop)
#endif

typedef enum {
  ZMBVU_MODE_UNKNOWN,
  ZMBVU_MODE_DECODER
} zmbvu_unpacker_mode_t;


struct zmbvu_unpacker_s {
  zmbvu_unpacker_mode_t mode;
  int unpack_compression;

  zmbvu_unpacker_vector_t vector_table[512];
  int vector_count;

  uint8_t *oldframe, *newframe;
  uint8_t *buf1, *buf2, *work;
  int bufsize;

  int blockcount;
  zmbvu_frame_block_t *blocks;

  int workUsed, workPos;

  int palsize;
  uint8_t palette[256*3];

  int height, width, pitch;

  zmbvu_format_t format;
  int pixelsize;

  mz_stream zstream;
  int zstream_inited; // <0: deflate; >0: inflate; 0: not inited
};


/******************************************************************************/
/* generate functions from templates */
/* decoder templates */
#define ZMBVU_UNXOR_BLOCK_TPL(_pxtype,_pxsize) \
static inline void zmbvu_unxor_block_##_pxsize (zmbvu_unpacker_t zc, int vx, int vy, zmbvu_frame_block_t *block) { \
  _pxtype *pold = ((_pxtype *)zc->oldframe)+block->start+(vy*zc->pitch)+vx; \
  _pxtype *pnew = ((_pxtype *)zc->newframe)+block->start; \
  for (int y = 0; y < block->dy; ++y) { \
    for (int x = 0; x < block->dx; ++x) { \
      pnew[x] = pold[x]^*((_pxtype *)&zc->work[zc->workPos]); \
      zc->workPos += sizeof(_pxtype); \
    } \
    pold += zc->pitch; \
    pnew += zc->pitch; \
  } \
}


#define ZMBVU_COPY_BLOCK_TPL(_pxtype,_pxsize) \
static inline void zmbvu_copy_block_##_pxsize (zmbvu_unpacker_t zc, int vx, int vy, zmbvu_frame_block_t *block) { \
  _pxtype *pold = ((_pxtype *)zc->oldframe)+block->start+(vy*zc->pitch)+vx; \
  _pxtype *pnew = ((_pxtype *)zc->newframe)+block->start; \
  for (int y = 0; y < block->dy; ++y) { \
    for (int x = 0; x < block->dx; ++x) { \
      pnew[x] = pold[x]; \
    } \
    pold += zc->pitch; \
    pnew += zc->pitch; \
  } \
}


#define ZMBVU_UNXOR_FRAME_TPL(_pxtype,_pxsize) \
static inline void zmbvu_unxor_frame_##_pxsize (zmbvu_unpacker_t zc) { \
  int8_t *vectors = (int8_t *)&zc->work[zc->workPos]; \
  zc->workPos = (zc->workPos+zc->blockcount*2+3)&~3; \
  for (int b = 0; b < zc->blockcount; ++b) { \
    zmbvu_frame_block_t *block = &zc->blocks[b]; \
    int delta = vectors[b*2+0]&1; \
    int vx = vectors[b*2+0]>>1; \
    int vy = vectors[b*2+1]>>1; \
    if (delta) zmbvu_unxor_block_##_pxsize(zc, vx, vy, block); else zmbvu_copy_block_##_pxsize(zc, vx, vy, block); \
  } \
}

/* generate functions */
ZMBVU_UNXOR_BLOCK_TPL(uint8_t,  8)
ZMBVU_UNXOR_BLOCK_TPL(uint16_t,16)
ZMBVU_UNXOR_BLOCK_TPL(uint32_t,32)

ZMBVU_COPY_BLOCK_TPL(uint8_t,  8)
ZMBVU_COPY_BLOCK_TPL(uint16_t,16)
ZMBVU_COPY_BLOCK_TPL(uint32_t,32)

ZMBVU_UNXOR_FRAME_TPL(uint8_t,  8)
ZMBVU_UNXOR_FRAME_TPL(uint16_t,16)
ZMBVU_UNXOR_FRAME_TPL(uint32_t,32)


/******************************************************************************/
static void zmbvu_create_vector_table (zmbvu_unpacker_t zc) {
  if (zc != NULL) {
    zc->vector_table[0].x = zc->vector_table[0].y = 0;
    zc->vector_count = 1;
    for (int s = 1; s <= 10; ++s) {
      for (int y = 0-s; y <= 0+s; ++y) {
        for (int x = 0-s; x <= 0+s; ++x) {
          if (abs(x) == s || abs(y) == s) {
            zc->vector_table[zc->vector_count].x = x;
            zc->vector_table[zc->vector_count].y = y;
            ++zc->vector_count;
          }
        }
      }
    }
  }
}


zmbvu_unpacker_t zmbvu_unpacker_new (void) {
  zmbvu_unpacker_t zc = malloc(sizeof(*zc));
  if (zc != NULL) {
    /*
    zc->blocks = NULL;
    zc->buf1 = NULL;
    zc->buf2 = NULL;
    zc->work = NULL;
    memset(&zc->zstream, 0, sizeof(zc->zstream));
    zc->zstream_inited = 0;
    */
    memset(zc, 0, sizeof(*zc));
    zmbvu_create_vector_table(zc);
    zc->mode = ZMBVU_MODE_UNKNOWN;
  }
  return zc;
}


static void zmbvu_free_buffers (zmbvu_unpacker_t zc) {
  if (zc != NULL) {
    if (zc->blocks != NULL) free(zc->blocks);
    if (zc->buf1 != NULL) free(zc->buf1);
    if (zc->buf2 != NULL) free(zc->buf2);
    if (zc->work != NULL) free(zc->work);
    zc->blocks = NULL;
    zc->buf1 = NULL;
    zc->buf2 = NULL;
    zc->work = NULL;
  }
}


static void zmbvu_zlib_deinit (zmbvu_unpacker_t zc) {
  if (zc != NULL) {
    if (zc->zstream_inited) mz_inflateEnd(&zc->zstream);
    zc->zstream_inited = 0;
  }
}


void zmbvu_unpacker_free (zmbvu_unpacker_t zc) {
  if (zc != NULL) {
    zmbvu_zlib_deinit(zc);
    zmbvu_free_buffers(zc);
    free(zc);
  }
}


/******************************************************************************/
const uint8_t *zmbvu_get_palette (zmbvu_unpacker_t zc) {
  return (zc != NULL ? zc->palette : NULL);
}


const void *zmbvu_get_decoded_line (zmbvu_unpacker_t zc, int idx) {
  if (zc != NULL && zc->mode == ZMBVU_MODE_DECODER && idx >= 0 && idx < zc->height) {
    return zc->newframe+zc->pixelsize*(MAX_VECTOR+MAX_VECTOR*zc->pitch)+zc->pitch*zc->pixelsize*idx;
  }
  return NULL;
}


int zmbvu_get_width (zmbvu_unpacker_t zc) {
  return (zc != NULL ? zc->width : -1);
}


int zmbvu_get_height (zmbvu_unpacker_t zc) {
  return (zc != NULL ? zc->height : -1);
}


zmbvu_format_t zmbvu_get_decoded_format (zmbvu_unpacker_t zc) {
  return (zc != NULL ? zc->format : ZMBVU_FORMAT_NONE);
}


int zmbvu_decode_palette_changed (zmbvu_unpacker_t zc, const void *framedata, int size) {
  return (zc != NULL && framedata != NULL && size > 0 ? (((const uint8_t *)framedata)[0]&FRAME_MASK_DELTA_PALETTE) != 0 : 0);
}


/******************************************************************************/
static int zmbvu_setup_buffers (zmbvu_unpacker_t zc, zmbvu_format_t format, int blockwidth, int blockheight) {
  if (zc != NULL) {
    int xblocks, xleft, yblocks, yleft, i;

    zmbvu_free_buffers(zc);
    zc->palsize = 0;
    switch (format) {
      case ZMBVU_FORMAT_8BPP: zc->pixelsize = 1; zc->palsize = 256; break;
      case ZMBVU_FORMAT_15BPP: case ZMBVU_FORMAT_16BPP: zc->pixelsize = 2; break;
      case ZMBVU_FORMAT_32BPP: zc->pixelsize = 4; break;
      default: return -1;
    };
    zc->bufsize = (zc->height+2*MAX_VECTOR)*zc->pitch*zc->pixelsize+2048;

    zc->buf1 = malloc(zc->bufsize);
    zc->buf2 = malloc(zc->bufsize);
    zc->work = malloc(zc->bufsize);

    if (zc->buf1 == NULL || zc->buf2 == NULL || zc->work == NULL) { zmbvu_free_buffers(zc); return -1; }

    xblocks = (zc->width/blockwidth);
    xleft = zc->width%blockwidth;
    if (xleft) ++xblocks;
    yblocks = (zc->height/blockheight);
    yleft = zc->height%blockheight;
    if (yleft) ++yblocks;

    zc->blockcount = yblocks*xblocks;
    zc->blocks = malloc(sizeof(zmbvu_frame_block_t)*zc->blockcount);
    if (zc->blocks == NULL) { zmbvu_free_buffers(zc); return -1; }

    i = 0;
    for (int y = 0; y < yblocks; ++y) {
      for (int x = 0; x < xblocks; ++x) {
        zc->blocks[i].start = ((y*blockheight)+MAX_VECTOR)*zc->pitch+(x*blockwidth)+MAX_VECTOR;
        zc->blocks[i].dx = (xleft && x == xblocks-1 ? xleft : blockwidth);
        zc->blocks[i].dy = (yleft && y == yblocks-1 ? yleft : blockheight);
        ++i;
      }
    }

    memset(zc->buf1, 0, zc->bufsize);
    memset(zc->buf2, 0, zc->bufsize);
    memset(zc->work, 0, zc->bufsize);
    zc->oldframe = zc->buf1;
    zc->newframe = zc->buf2;
    zc->format = format;
    return 0;
  }
  return -1;
}


/******************************************************************************/
int zmbvu_decode_setup (zmbvu_unpacker_t zc, int width, int height) {
  if (zc != NULL && width > 0 && height > 0 && width <= 16384 && height <= 16384) {
    zc->width = width;
    zc->height = height;
    zc->pitch = width+2*MAX_VECTOR;
    zc->format = ZMBVU_FORMAT_NONE;
    zmbvu_zlib_deinit(zc);
    if (mz_inflateInit(&zc->zstream) != MZ_OK) return -1;
    zc->zstream_inited = 1;
    zc->mode = ZMBVU_MODE_DECODER;
    zc->unpack_compression = 0;
    return 0;
  }
  return -1;
}


/******************************************************************************/
int zmbvu_decode_frame (zmbvu_unpacker_t zc, const void *framedata, int size) {
  if (zc != NULL && framedata != NULL && size > 1 && zc->mode == ZMBVU_MODE_DECODER) {
    uint8_t tag;
    const uint8_t *data = (const uint8_t *)framedata;
    tag = *data++;
    if (tag > 2) return -1; /* for now we can have only 0, 1 or 2 in tag byte */
    if (--size <= 0) return -1;
    if (tag&FRAME_MASK_KEYFRAME) {
      const zmbvu_keyframe_header_t *header = (const zmbvu_keyframe_header_t *)data;
      size -= sizeof(zmbvu_keyframe_header_t);
      data += sizeof(zmbvu_keyframe_header_t);
      if (size <= 0) return -1;
      if (header->low_version != DBZV_VERSION_LOW || header->high_version != DBZV_VERSION_HIGH) return -1;
      if (header->compression > COMPRESSION_ZLIB) return -1; /* invalid compression mode */
      if (zc->format != (zmbvu_format_t)header->format && zmbvu_setup_buffers(zc, (zmbvu_format_t)header->format, header->blockwidth, header->blockheight) < 0) return -1;
      zc->unpack_compression = header->compression;
      if (zc->unpack_compression == COMPRESSION_ZLIB) {
        if (mz_inflateReset(&zc->zstream) != MZ_OK) return -1;
      }
    }
    if (size > zc->bufsize) return -1; /* frame too big */
    if (zc->unpack_compression == COMPRESSION_ZLIB) {
      zc->zstream.next_in = (void *)data;
      zc->zstream.avail_in = size;
      zc->zstream.total_in = 0;
      zc->zstream.next_out = (void *)zc->work;
      zc->zstream.avail_out = zc->bufsize;
      zc->zstream.total_out = 0;
      if (mz_inflate(&zc->zstream, MZ_SYNC_FLUSH/*MZ_NO_FLUSH*/) != MZ_OK) return -1; /* the thing that should not be */
      zc->workUsed = zc->zstream.total_out;
    } else {
      if (size > 0) memcpy(zc->work, data, size);
      zc->workUsed = size;
    }
    zc->workPos = 0;
    if (tag&FRAME_MASK_KEYFRAME) {
      if (zc->palsize) {
        for (int i = 0; i < zc->palsize; ++i) {
          zc->palette[i*3+0] = zc->work[zc->workPos++];
          zc->palette[i*3+1] = zc->work[zc->workPos++];
          zc->palette[i*3+2] = zc->work[zc->workPos++];
        }
      }
      zc->newframe = zc->buf1;
      zc->oldframe = zc->buf2;
      uint8_t *writeframe = zc->newframe+zc->pixelsize*(MAX_VECTOR+MAX_VECTOR*zc->pitch);
      for (int i = 0; i < zc->height; ++i) {
        memcpy(writeframe, &zc->work[zc->workPos], zc->width*zc->pixelsize);
        writeframe += zc->pitch*zc->pixelsize;
        zc->workPos += zc->width*zc->pixelsize;
      }
    } else {
      uint8_t *tmp = zc->oldframe;
      zc->oldframe = zc->newframe;
      zc->newframe = tmp;
      if (tag&FRAME_MASK_DELTA_PALETTE) {
        for (int i = 0; i < zc->palsize; ++i) {
          zc->palette[i*3+0] ^= zc->work[zc->workPos++];
          zc->palette[i*3+1] ^= zc->work[zc->workPos++];
          zc->palette[i*3+2] ^= zc->work[zc->workPos++];
        }
      }
      switch (zc->format) {
        case ZMBVU_FORMAT_8BPP: zmbvu_unxor_frame_8(zc); break;
        case ZMBVU_FORMAT_15BPP: case ZMBVU_FORMAT_16BPP: zmbvu_unxor_frame_16(zc); break;
        case ZMBVU_FORMAT_32BPP: zmbvu_unxor_frame_32(zc); break;
        default: return -1; /* the thing that should not be */
      }
    }
    return 0;
  }
  return -1;
}
