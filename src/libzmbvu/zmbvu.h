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
#ifndef ZMBVU_H
#define ZMBVU_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* uncomment this to use system zlib instead of miniz */
/*#define ZMBVU_USE_ZLIB*/


typedef enum {
  ZMBVU_FORMAT_NONE  = 0x00,
  /*ZMBVU_FORMAT_1BPP  = 0x01,*/
  /*ZMBVU_FORMAT_2BPP  = 0x02,*/
  /*ZMBVU_FORMAT_4BPP  = 0x03,*/
  ZMBVU_FORMAT_8BPP  = 0x04,
  ZMBVU_FORMAT_15BPP = 0x05,
  ZMBVU_FORMAT_16BPP = 0x06,
  /*ZMBVU_FORMAT_24BPP = 0x07,*/
  ZMBVU_FORMAT_32BPP = 0x08
} zmbvu_format_t;


/* opaque codec data */
typedef struct zmbvu_unpacker_s *zmbvu_unpacker_t;


/* utilities */
/* returns ZMBVU_FORMAT_NONE for unknown bpp */
extern zmbvu_format_t zmbvu_bpp_to_format (int bpp);


extern zmbvu_unpacker_t zmbvu_unpacker_new (void);
extern void zmbvu_unpacker_free (zmbvu_unpacker_t zc);


/* return <0 on error; 0 on ok */
extern int zmbvu_decode_setup (zmbvu_unpacker_t zc, int width, int height);
/* return <0 on error; 0 on ok */
extern int zmbvu_decode_frame (zmbvu_unpacker_t zc, const void *framedata, int size);
/* return !0 if palette was be changed on this frame */
extern int zmbvu_decode_is_palette_changed (zmbvu_unpacker_t zc, const void *framedata, int size);

/* this can be called after zmbvu_decode_frame() */
extern const uint8_t *zmbvu_get_palette (zmbvu_unpacker_t zc);
/* this can be called after zmbvu_decode_frame() */
extern const void *zmbvu_get_decoded_line (zmbvu_unpacker_t zc, int idx) ;
/* this can be called after zmbvu_decode_frame() */
extern zmbvu_format_t zmbvu_get_decoded_format (zmbvu_unpacker_t zc);

/* <0: error; 0: never */
extern int zmbvu_get_width (zmbvu_unpacker_t zc);
extern int zmbvu_get_height (zmbvu_unpacker_t zc);


#ifdef __cplusplus
}
#endif
#endif
