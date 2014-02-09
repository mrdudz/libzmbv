/*
 *  Copyright (C) 2002-2013  The DOSBox Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include "zmbv.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/******************************************************************************/
#define DBZV_VERSION_HIGH  (0)
#define DBZV_VERSION_LOW   (1)

#define COMPRESSION_NONE  (0)
#define COMPRESSION_ZLIB  (1)

#define MAX_VECTOR  (16)

enum {
  Mask_KeyFrame = 0x01,
  Mask_DeltaPalette = 0x02
};


/******************************************************************************/
zmbv_format_t VideoCodec::BPPFormat (int bpp) {
  switch (bpp) {
    case 8: return ZMBV_FORMAT_8BPP;
    case 15: return ZMBV_FORMAT_15BPP;
    case 16: return ZMBV_FORMAT_16BPP;
    case 32: return ZMBV_FORMAT_32BPP;
  }
  return ZMBV_FORMAT_NONE;
}


int VideoCodec::NeededSize (int awidth, int aheight, zmbv_format_t aformat) {
  int f;
  switch (aformat) {
    case ZMBV_FORMAT_8BPP: f = 1; break;
    case ZMBV_FORMAT_15BPP: case ZMBV_FORMAT_16BPP: f = 2; break;
    case ZMBV_FORMAT_32BPP: f = 4;break;
    default: return -1;
  }
  f = f*awidth*aheight+2*(1+(awidth/8))*(1+(aheight/8))+1024;
  return f+f/1000;
}


/******************************************************************************/
VideoCodec::VideoCodec () {
  CreateVectorTable();
  blocks = 0;
  buf1 = 0;
  buf2 = 0;
  work = 0;
  memset(&zstream, 0, sizeof(zstream));
}


VideoCodec::~VideoCodec () {
  FreeBuffers();
}


void VideoCodec::FreeBuffers (void) {
  if (blocks) delete[] blocks;
  blocks = NULL;
  if (buf1) delete[] buf1;
  buf1 = NULL;
  if (buf2) delete[] buf2;
  buf2 = NULL;
  if (work) delete[] work;
  work = NULL;
}


/******************************************************************************/
bool VideoCodec::SetupBuffers (zmbv_format_t aformat, int blockwidth, int blockheight) {
  FreeBuffers();
  palsize = 0;
  switch (aformat) {
    case ZMBV_FORMAT_8BPP: pixelsize = 1; palsize = 256; break;
    case ZMBV_FORMAT_15BPP: case ZMBV_FORMAT_16BPP: pixelsize = 2; break;
    case ZMBV_FORMAT_32BPP: pixelsize = 4; break;
    default: return false;
  };
  bufsize = (height+2*MAX_VECTOR)*pitch*pixelsize+2048;

  buf1 = new uint8_t[bufsize];
  buf2 = new uint8_t[bufsize];
  work = new uint8_t[bufsize];

  int xblocks = (width/blockwidth);
  int xleft = width%blockwidth;
  if (xleft) ++xblocks;
  int yblocks = (height/blockheight);
  int yleft = height%blockheight;
  if (yleft) ++yblocks;

  blockcount = yblocks*xblocks;
  blocks = new FrameBlock[blockcount];

  if (!buf1 || !buf2 || !work || !blocks) {
    FreeBuffers();
    return false;
  }

  int i = 0;
  for (int y = 0; y < yblocks; ++y) {
    for (int x = 0; x < xblocks; ++x) {
      blocks[i].start = ((y*blockheight)+MAX_VECTOR)*pitch+(x*blockwidth)+MAX_VECTOR;
      blocks[i].dx = (xleft && x == xblocks-1 ? xleft : blockwidth);
      blocks[i].dy = (yleft && y == yblocks-1 ? yleft : blockheight);
      ++i;
    }
  }

  memset(buf1, 0, bufsize);
  memset(buf2, 0, bufsize);
  memset(work, 0, bufsize);
  oldframe = buf1;
  newframe = buf2;
  format = aformat;
  return true;
}


void VideoCodec::CreateVectorTable (void) {
  VectorCount = 1;
  VectorTable[0].x = VectorTable[0].y = 0;
  for (int s = 1; s <= 10; ++s) {
    for (int y = 0-s; y <= 0+s; ++y) {
      for (int x = 0-s; x <= 0+s; ++x) {
        if (abs(x) == s || abs(y) == s) {
          VectorTable[VectorCount].x = x;
          VectorTable[VectorCount].y = y;
          ++VectorCount;
        }
      }
    }
  }
}


/******************************************************************************/
template<class P> INLINE int VideoCodec::PossibleBlock (int vx, int vy, FrameBlock *block) {
  int ret = 0;
  P *pold = ((P *)oldframe)+block->start+(vy*pitch)+vx;
  P *pnew = ((P *)newframe)+block->start;
  for (int y = 0; y < block->dy; y += 4) {
    for (int x = 0; x < block->dx; x += 4) {
      int test = 0-((pold[x]-pnew[x])&0x00ffffff);
      ret -= (test>>31);
    }
    pold += pitch*4;
    pnew += pitch*4;
  }
  return ret;
}


template<class P> INLINE int VideoCodec::CompareBlock (int vx, int vy, FrameBlock *block) {
  int ret = 0;
  P *pold = ((P *)oldframe)+block->start+(vy*pitch)+vx;
  P *pnew = ((P *)newframe)+block->start;
  for (int y = 0; y < block->dy; ++y) {
    for (int x = 0; x < block->dx; ++x) {
      int test = 0-((pold[x]-pnew[x])&0x00ffffff);
      ret -= (test>>31);
    }
    pold += pitch;
    pnew += pitch;
  }
  return ret;
}


template<class P> INLINE void VideoCodec::AddXorBlock (int vx, int vy, FrameBlock * block) {
  P *pold= ((P *)oldframe)+block->start+(vy*pitch)+vx;
  P *pnew= ((P *)newframe)+block->start;
  for (int y = 0; y < block->dy; ++y) {
    for (int x = 0; x < block->dx; ++x) {
      *((P *)&work[workUsed]) = pnew[x]^pold[x];
      workUsed += sizeof(P);
    }
    pold += pitch;
    pnew += pitch;
  }
}


template<class P> void VideoCodec::AddXorFrame (void) {
  int written = 0;
  int lastvector = 0;
  int8_t *vectors = (int8_t *)&work[workUsed];
  /* align the following xor data on 4 byte boundary*/
  workUsed = (workUsed+blockcount*2+3)&~3;
  int totalx = 0;
  int totaly = 0;
  for (int b = 0;b < blockcount; ++b) {
    FrameBlock *block = &blocks[b];
    int bestvx = 0;
    int bestvy = 0;
    int bestchange = CompareBlock<P>(0, 0, block);
    int possibles = 64;
    for (int v = 0; v < VectorCount && possibles; ++v) {
      if (bestchange < 4) break;
      int vx = VectorTable[v].x;
      int vy = VectorTable[v].y;
      if (PossibleBlock<P>(vx, vy, block) < 4) {
        --possibles;
        //if (!possibles) Msg("Ran out of possibles, at %d of %d best %d\n", v, VectorCount, bestchange);
        int testchange = CompareBlock<P>(vx, vy, block);
        if (testchange < bestchange) {
          bestchange = testchange;
          bestvx = vx;
          bestvy = vy;
        }
      }
    }
    vectors[b*2+0] = (bestvx << 1);
    vectors[b*2+1] = (bestvy << 1);
    if (bestchange) {
      vectors[b*2+0] |= 1;
      AddXorBlock<P>(bestvx, bestvy, block);
    }
  }
}


/******************************************************************************/
bool VideoCodec::SetupCompress (int awidth, int aheight) {
  width = awidth;
  height = aheight;
  pitch = awidth+2*MAX_VECTOR;
  format = ZMBV_FORMAT_NONE;
  if (deflateInit(&zstream, 4) != Z_OK) return false;
  return true;
}


bool VideoCodec::SetupDecompress (int awidth, int aheight) {
  width = awidth;
  height = aheight;
  pitch = awidth+2*MAX_VECTOR;
  format = ZMBV_FORMAT_NONE;
  if (inflateInit (&zstream) != Z_OK) return false;
  return true;
}


/******************************************************************************/
bool VideoCodec::PrepareCompressFrame (int flags,  zmbv_format_t aformat, const uint8_t *pal, void *writeBuf, int writeSize) {
  int i;
  uint8_t *firstByte;

  if (aformat != format) {
    if (!SetupBuffers(aformat, 16, 16)) return false;
    flags |= 1; // force a keyframe
  }
  /* replace oldframe with new frame */
  uint8_t *copyFrame = newframe;
  newframe = oldframe;
  oldframe = copyFrame;

  compress.linesDone = 0;
  compress.writeSize = writeSize;
  compress.writeDone = 1;
  compress.writeBuf = (uint8_t *)writeBuf;
  /* set a pointer to the first byte which will contain info about this frame */
  firstByte = compress.writeBuf;
  *firstByte = 0;
  //Reset the work buffer
  workUsed = 0;
  workPos = 0;
  if (flags&1) {
    /* make a keyframe */
    *firstByte |= Mask_KeyFrame;
    KeyframeHeader *header = (KeyframeHeader *)(compress.writeBuf+compress.writeDone);
    header->high_version = DBZV_VERSION_HIGH;
    header->low_version = DBZV_VERSION_LOW;
    header->compression = COMPRESSION_ZLIB;
    header->format = format;
    header->blockwidth = 16;
    header->blockheight = 16;
    compress.writeDone += sizeof(KeyframeHeader);
    /* copy the new frame directly over */
    if (palsize) {
      if (pal) {
        memcpy(&palette, pal, sizeof(palette));
      } else {
        memset(&palette, 0, sizeof(palette));
      }
      /* keyframes get the full palette */
      for (i = 0; i < palsize; ++i) {
        work[workUsed++] = palette[i*4+0];
        work[workUsed++] = palette[i*4+1];
        work[workUsed++] = palette[i*4+2];
      }
    }
    /* Restart deflate */
    deflateReset(&zstream);
  } else {
    if (palsize && pal && memcmp(pal, palette, palsize*4)) {
      *firstByte |= Mask_DeltaPalette;
      for (i = 0; i < palsize; ++i) {
        work[workUsed++] = palette[i*4+0]^pal[i*4+0];
        work[workUsed++] = palette[i*4+1]^pal[i*4+1];
        work[workUsed++] = palette[i*4+2]^pal[i*4+2];
      }
      memcpy(&palette, pal, palsize*4);
    }
  }
  return true;
}


/******************************************************************************/
void VideoCodec::CompressLines (int lineCount, const void *lineData[]) {
  int linePitch = pitch*pixelsize;
  int lineWidth = width*pixelsize;
  int i = 0;
  uint8_t *destStart = newframe+pixelsize*(MAX_VECTOR+(compress.linesDone+MAX_VECTOR)*pitch);
  while (i < lineCount && compress.linesDone < height) {
    memcpy(destStart, lineData[i], lineWidth);
    destStart += linePitch;
    ++i;
    ++compress.linesDone;
  }
}


/******************************************************************************/
int VideoCodec::FinishCompressFrame (void) {
  uint8_t firstByte = *compress.writeBuf;
  if (firstByte&Mask_KeyFrame) {
    int i;
    /* add the full frame data */
    uint8_t *readFrame = newframe+pixelsize*(MAX_VECTOR+MAX_VECTOR*pitch);
    for (i = 0; i < height; ++i) {
      memcpy(&work[workUsed], readFrame, width*pixelsize);
      readFrame += pitch*pixelsize;
      workUsed += width*pixelsize;
    }
  } else {
    /* add the delta frame data */
    switch (format) {
      case ZMBV_FORMAT_8BPP: AddXorFrame<int8_t>(); break;
      case ZMBV_FORMAT_15BPP: case ZMBV_FORMAT_16BPP: AddXorFrame<int16_t>(); break;
      case ZMBV_FORMAT_32BPP: AddXorFrame<int32_t>(); break;
      default: return -1; /* the thing that should not happen */
    }
  }

  /* create the actual frame with compression */
  zstream.next_in = (Bytef *)work;
  zstream.avail_in = workUsed;
  zstream.total_in = 0;

  zstream.next_out = (Bytef *)(compress.writeBuf+compress.writeDone);
  zstream.avail_out = compress.writeSize-compress.writeDone;
  zstream.total_out = 0;
  int res = deflate(&zstream, Z_SYNC_FLUSH);
  return compress.writeDone+zstream.total_out;
}


/******************************************************************************/
template<class P> INLINE void VideoCodec::UnXorBlock (int vx, int vy, FrameBlock *block) {
  P *pold = ((P *)oldframe)+block->start+(vy*pitch)+vx;
  P *pnew = ((P *)newframe)+block->start;
  for (int y = 0; y < block->dy; ++y) {
    for (int x = 0; x < block->dx; ++x) {
      pnew[x] = pold[x]^*((P*)&work[workPos]);
      workPos += sizeof(P);
    }
    pold += pitch;
    pnew += pitch;
  }
}


template<class P> INLINE void VideoCodec::CopyBlock (int vx, int vy, FrameBlock *block) {
  P *pold = ((P *)oldframe)+block->start+(vy*pitch)+vx;
  P *pnew = ((P *)newframe)+block->start;
  for (int y = 0; y < block->dy; ++y) {
    for (int x = 0; x < block->dx; ++x) {
      pnew[x] = pold[x];
    }
    pold+=pitch;
    pnew+=pitch;
  }
}


template<class P> void VideoCodec::UnXorFrame (void) {
  int8_t *vectors = (int8_t *)&work[workPos];
  workPos = (workPos+blockcount*2+3)&~3;
  for (int b = 0; b < blockcount; ++b) {
    FrameBlock *block = &blocks[b];
    int delta = vectors[b*2+0]&1;
    int vx = vectors[b*2+0]>>1;
    int vy = vectors[b*2+1]>>1;
    if (delta) UnXorBlock<P>(vx, vy, block); else CopyBlock<P>(vx, vy, block);
  }
}


/******************************************************************************/
bool VideoCodec::DecompressFrame (void *framedata, int size) {
  uint8_t *data = (uint8_t *)framedata;
  uint8_t tag;

  tag = *data++;
  if (--size<=0) return false;
  if (tag&Mask_KeyFrame) {
    KeyframeHeader *header = (KeyframeHeader *)data;
    size -= sizeof(KeyframeHeader);
    data += sizeof(KeyframeHeader);
    if (size <= 0) return false;
    if (header->low_version != DBZV_VERSION_LOW || header->high_version != DBZV_VERSION_HIGH) return false;
    if (format != (zmbv_format_t)header->format && !SetupBuffers((zmbv_format_t)header->format, header->blockwidth, header->blockheight)) return false;
    inflateReset(&zstream);
  }
  zstream.next_in = (Bytef *)data;
  zstream.avail_in = size;
  zstream.total_in = 0;

  zstream.next_out = (Bytef *)work;
  zstream.avail_out = bufsize;
  zstream.total_out = 0;
  int res = inflate(&zstream, Z_FINISH);
  workUsed= zstream.total_out;
  workPos = 0;
  if (tag&Mask_KeyFrame) {
    if (palsize) {
      for (i = 0; i < palsize; ++i) {
        palette[i*4+0] = work[workPos++];
        palette[i*4+1] = work[workPos++];
        palette[i*4+2] = work[workPos++];
      }
    }
    newframe = buf1;
    oldframe = buf2;
    uint8_t *writeframe = newframe+pixelsize*(MAX_VECTOR+MAX_VECTOR*pitch);
    for (int i = 0; i < height; ++i) {
      memcpy(writeframe, &work[workPos], width*pixelsize);
      writeframe += pitch*pixelsize;
      workPos += width*pixelsize;
    }
  } else {
    data = oldframe;
    oldframe = newframe;
    newframe = data;
    if (tag&Mask_DeltaPalette) {
      for (int i = 0; i < palsize; ++i) {
        palette[i*4+0] ^= work[workPos++];
        palette[i*4+1] ^= work[workPos++];
        palette[i*4+2] ^= work[workPos++];
      }
    }
    switch (format) {
      case ZMBV_FORMAT_8BPP: UnXorFrame<int8_t>(); break;
      case ZMBV_FORMAT_15BPP: case ZMBV_FORMAT_16BPP: UnXorFrame<int16_t>(); break;
      case ZMBV_FORMAT_32BPP: UnXorFrame<int32_t>(); break;
      default: return false; /* the thing that should not happen */
    }
  }
  return true;
}


/******************************************************************************/
void VideoCodec::Output_UpsideDown_24(void *output) {
  uint8_t *r;
  uint8_t *w = (uint8_t *)output;
  int pad = width&3;
  for (int i = height-1; i >= 0; --i) {
    r = newframe+pixelsize*(MAX_VECTOR+(i+MAX_VECTOR)*pitch);
    switch (format) {
      case ZMBV_FORMAT_8BPP:
        for (int j = 0; j < width; ++j) {
          int c = r[j];
          *w++ = palette[c*4+2];
          *w++ = palette[c*4+1];
          *w++ = palette[c*4+0];
        }
        break;
      case ZMBV_FORMAT_15BPP:
        for (int j = 0; j < width; ++j) {
          uint16_t c = *(uint16_t *)&r[j*2];
          *w++ = (uint8_t)(((c&0x001f)*0x21)>>2);
          *w++ = (uint8_t)(((c&0x03e0)*0x21)>>7);
          *w++ = (uint8_t)(((c&0x7c00)*0x21)>>12);
        }
        break;
      case ZMBV_FORMAT_16BPP:
        for (int j = 0; j < width; ++j) {
          uint16_t c = *(uint16_t *)&r[j*2];
          *w++ = (uint8_t)(((c&0x001f)*0x21)>>2);
          *w++ = (uint8_t)(((c&0x07e0)*0x41)>>9);
          *w++ = (uint8_t)(((c&0xf800)*0x21)>>13);
        }
        break;
      case ZMBV_FORMAT_32BPP:
        for (int j = 0; j < width; ++j) {
          *w++ = r[j*4+0];
          *w++ = r[j*4+1];
          *w++ = r[j*4+2];
        }
        break;
      default: ; /* the thing that should not happen */
    }
    // maintain 32-bit alignment for scanlines
    w += pad;
  }
}
