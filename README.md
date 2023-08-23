# libzmbv

C port of DOSBox ZMBV codec and AVI writer

originally started by ketmar@ketmar.no-ip.org at https://repo.or.cz/libzmbv.git

Done since then:

- options have been inverted, by default it will link against libz and omit the
  encoder in libzbmv
- zmbv_avi_start() takes an additional paramert (audio frequency)
- added "test-avi" sample that shows how to write out an AVI with sound
- a couple formatting changes to fix some warnings

# ZMBV

Info copied from https://wiki.multimedia.cx/index.php/DosBox_Capture_Codec

DosBox Capture Codec, added to the DosBox project to capture screen data

FourCC: ZMBV

Samples: http://samples.mplayerhq.hu/V-codecs/ZMBV/

This codec employs ZLIB compression and has intraframes and delta frames. Delta frames seem to have blocks either copied from the previous frame or XOR'ed with some block from the previous frame.

The FourCC for this codec is ZMBV which ostensibly stands for Zip Motion Blocks Video. The data is most commonly stored in AVI files.

## Data Format

Byte 0 of a ZMBV data chunk contains the following flags:

    bits 7-2  undefined
    bit 1     palette change
    bit 0     1 = intraframe, 0 = interframe

If the frame is an intra frame as indicated by bit 0 of byte 0, the next 6 bytes in the data chunk are formatted as follows:

    byte 1    major version
    byte 2    minor version
    byte 3    compression type (0 = uncompressed, 1 = zlib-compressed)
    byte 4    video format
    byte 5    block width
    byte 6    block height

Presently, the only valid major/minor version pair is 0/1. A block width or height of 0 is invalid. These are the video modes presently defined:

    0  none
    1  1 bit/pixel, palettized
    2  2 bits/pixel, palettized
    3  4 bits/pixel, palettized
    4  8 bits/pixel, palettized
    5  15 bits/pixel
    6  16 bits/pixel
    7  24 bits/pixel
    8  32 bits/pixel

Presently, only modes 4 (8 bpp), 5 (15 bpp), 6 (16 bpp) and 8 (32 bpp) are supported.

If the compression type is 1, the remainder of the data chunk is compressed using the standard zlib package. Decompress the data before proceeding with the next step. Otherwise, proceed to the next step. Also note that you must reset zlib for intraframes.

If bit 1 of the frame header (palette change) is set then the first 768 bytes of the uncompressed data represent 256 red-green-blue palette triplets. Each component is one byte and ranges from 0..255.

An intraframe consists of 768 bytes of palette data (for palettized modes) and raw frame data.

An interframe is comprised of up to three parts:

if palette change flag was set then first 768 bytes represent XOR'ed palette difference
block info (2 bytes per block, padded to 4 bytes length)
block differences
Block info is composed from a motion vector and a flag: first byte is (dx << 1) | flag, second byte is (dy << 1). Motion vectors can go out of bounds and in that case you need to zero the out-of-bounds part. Also note that currently motion vectors are limited to a range of (-16..16). Flag tells whether the codec simply copies the block from the decoded offset or copies it and XOR's it with data from block differences. All XORing for 15/16 bpp and 32 bpp modes is done with little-endian integers.

Interframe decoding can be done this way:

```
 for each block {
   a = block_info[current_block][0];
   b = block_info[current_block][1];
   dx = a >> 1;
   dy = b >> 1;
   flag = a & 1;
   copy block from offset (dx, dy) from previous frame.
   if (flag) {
    XOR block with data read from stream.
   }
 }
```
