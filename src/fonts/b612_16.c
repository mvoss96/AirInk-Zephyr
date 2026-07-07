/*******************************************************************************
 * Size: 16 px
 * Bpp: 1
 * Opts: --font b612.ttf --size 16 --bpp 1 --format lvgl --range 0x20-0x7F --range 0xB0 --lv-include lvgl.h -o b612_16.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef B612_16
#define B612_16 1
#endif

#if B612_16

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x0,

    /* U+0021 "!" */
    0x6d, 0xb6, 0xdb, 0x43, 0x20,

    /* U+0022 "\"" */
    0xff, 0xf0,

    /* U+0023 "#" */
    0x18, 0x41, 0x8c, 0x18, 0xcf, 0xff, 0x10, 0xc3,
    0xc, 0x30, 0x8f, 0xff, 0x31, 0x83, 0x18, 0x31,
    0x80,

    /* U+0024 "$" */
    0x10, 0x21, 0xf6, 0xad, 0x1a, 0x3c, 0x3c, 0x1c,
    0x2c, 0x58, 0xb1, 0x5f, 0x4, 0x8,

    /* U+0025 "%" */
    0x70, 0x26, 0x42, 0x32, 0x31, 0x91, 0xc, 0x90,
    0x38, 0x80, 0x9, 0xc0, 0x59, 0x4, 0xc8, 0x66,
    0x42, 0x32, 0x20, 0xe0,

    /* U+0026 "&" */
    0x1e, 0x8, 0x82, 0x0, 0x80, 0x30, 0x1e, 0xe,
    0x8f, 0x33, 0xc6, 0xf0, 0xe6, 0x30, 0xfe,

    /* U+0027 "'" */
    0xf8,

    /* U+0028 "(" */
    0x7c, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xc7,

    /* U+0029 ")" */
    0xe3, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3e,

    /* U+002A "*" */
    0x20, 0x8f, 0x9c, 0x50, 0x0,

    /* U+002B "+" */
    0x10, 0x20, 0x47, 0xf1, 0x2, 0x4, 0x8,

    /* U+002C "," */
    0xf6,

    /* U+002D "-" */
    0xf0,

    /* U+002E "." */
    0xf0,

    /* U+002F "/" */
    0x6, 0x8, 0x30, 0x41, 0x83, 0x4, 0x18, 0x20,
    0xc1, 0x86, 0xc, 0x10, 0x0,

    /* U+0030 "0" */
    0x3c, 0x66, 0x42, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0xc3, 0x42, 0x66, 0x3c,

    /* U+0031 "1" */
    0x18, 0x71, 0xe6, 0xc1, 0x83, 0x6, 0xc, 0x18,
    0x30, 0x63, 0xf0,

    /* U+0032 "2" */
    0x39, 0x98, 0x18, 0x30, 0x61, 0x87, 0x1c, 0x30,
    0xc3, 0x7, 0xf0,

    /* U+0033 "3" */
    0xfc, 0x18, 0x61, 0x87, 0xf, 0x81, 0x83, 0x6,
    0x18, 0x67, 0x0,

    /* U+0034 "4" */
    0x8, 0x4, 0x6, 0x2, 0x3, 0x1, 0x1, 0x98,
    0x8c, 0xc6, 0x7f, 0xc1, 0x80, 0xc0, 0x60,

    /* U+0035 "5" */
    0xfd, 0x83, 0x6, 0xf, 0xd1, 0xc1, 0x83, 0x6,
    0x18, 0xe7, 0x0,

    /* U+0036 "6" */
    0xe, 0xc, 0xc, 0xc, 0x4, 0x2, 0xf3, 0x8e,
    0x83, 0x41, 0xb0, 0xc8, 0xc3, 0xc0,

    /* U+0037 "7" */
    0xff, 0x2, 0x6, 0x4, 0xc, 0x18, 0x18, 0x18,
    0x30, 0x30, 0x30, 0x30,

    /* U+0038 "8" */
    0x7c, 0xc6, 0xc6, 0xc6, 0x7c, 0x3c, 0x6e, 0xc3,
    0xc3, 0xc3, 0x66, 0x3c,

    /* U+0039 "9" */
    0x3c, 0x66, 0xc3, 0xc3, 0xc3, 0x67, 0x3b, 0x3,
    0x6, 0x6, 0xc, 0x30,

    /* U+003A ":" */
    0xd8, 0xd, 0x80,

    /* U+003B ";" */
    0xf0, 0xf6,

    /* U+003C "<" */
    0x0, 0xc, 0x63, 0x8c, 0x1c, 0xe, 0x6, 0x2,

    /* U+003D "=" */
    0xfc, 0xf, 0xc0,

    /* U+003E ">" */
    0x1, 0x80, 0xc0, 0xe0, 0x61, 0xce, 0x30, 0x80,

    /* U+003F "?" */
    0x7d, 0x8c, 0x18, 0x30, 0xe3, 0x8c, 0x18, 0x0,
    0x0, 0xc1, 0x80,

    /* U+0040 "@" */
    0xf, 0x81, 0x87, 0x18, 0xc, 0xdf, 0x6c, 0xd,
    0xe0, 0x6f, 0x3f, 0x7b, 0x1b, 0xd8, 0xde, 0xc6,
    0xf6, 0x76, 0xdd, 0xe3, 0x0, 0x1c, 0x0, 0x3f,
    0x0,

    /* U+0041 "A" */
    0xc, 0x3, 0x80, 0xe0, 0x68, 0x12, 0x4, 0xc3,
    0x10, 0xfc, 0x21, 0x98, 0x24, 0x9, 0x3,

    /* U+0042 "B" */
    0xfc, 0xce, 0xc6, 0xc6, 0xcc, 0xf8, 0xc6, 0xc3,
    0xc3, 0xc3, 0xc6, 0xfc,

    /* U+0043 "C" */
    0x1f, 0x18, 0x18, 0x18, 0xc, 0x6, 0x3, 0x1,
    0x80, 0xc0, 0x30, 0xc, 0x3, 0xe0,

    /* U+0044 "D" */
    0xf8, 0xce, 0xc6, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0xc3, 0xc6, 0xcc, 0xf8,

    /* U+0045 "E" */
    0xff, 0x83, 0x6, 0xc, 0x1f, 0xb0, 0x60, 0xc1,
    0x83, 0x7, 0xf0,

    /* U+0046 "F" */
    0xff, 0x83, 0x6, 0xc, 0x1f, 0xb0, 0x60, 0xc1,
    0x83, 0x6, 0x0,

    /* U+0047 "G" */
    0x1f, 0x38, 0x18, 0x18, 0xc, 0x6, 0x3f, 0x7,
    0x83, 0xc1, 0xb0, 0xcc, 0xe3, 0xe0,

    /* U+0048 "H" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xff, 0xc3, 0xc3,
    0xc3, 0xc3, 0xc3, 0xc3,

    /* U+0049 "I" */
    0xff, 0xff, 0xff,

    /* U+004A "J" */
    0x3c, 0x30, 0xc3, 0xc, 0x30, 0xc3, 0xc, 0x31,
    0xfe,

    /* U+004B "K" */
    0xc3, 0xc6, 0xcc, 0xd8, 0xd0, 0xe0, 0xf0, 0xd8,
    0xcc, 0xc4, 0xc6, 0xc3,

    /* U+004C "L" */
    0xc1, 0x83, 0x6, 0xc, 0x18, 0x30, 0x60, 0xc1,
    0x83, 0x7, 0xf0,

    /* U+004D "M" */
    0xc0, 0x7c, 0xf, 0x83, 0xf8, 0x7f, 0x17, 0xa2,
    0xf6, 0xde, 0x53, 0xca, 0x78, 0x8f, 0x11, 0xe0,
    0x30,

    /* U+004E "N" */
    0xc1, 0xf0, 0xfc, 0x7e, 0x3d, 0x9e, 0xcf, 0x37,
    0x8b, 0xc7, 0xe1, 0xf0, 0xf8, 0x30,

    /* U+004F "O" */
    0x1f, 0x6, 0x31, 0x83, 0x60, 0x3c, 0x7, 0x80,
    0xf0, 0x1e, 0x3, 0xc0, 0x6c, 0x18, 0xc6, 0xf,
    0x80,

    /* U+0050 "P" */
    0xf9, 0x9b, 0x1e, 0x3c, 0x79, 0xbe, 0x60, 0xc1,
    0x83, 0x6, 0x0,

    /* U+0051 "Q" */
    0x1f, 0x6, 0x31, 0x83, 0x60, 0x3c, 0x7, 0x80,
    0xf0, 0x1e, 0x3, 0xc0, 0x6c, 0xd8, 0xce, 0xf,
    0xc0, 0xe,

    /* U+0052 "R" */
    0xf8, 0xcc, 0xc6, 0xc6, 0xc6, 0xcc, 0xf8, 0xd8,
    0xcc, 0xc4, 0xc6, 0xc3,

    /* U+0053 "S" */
    0x3c, 0x83, 0x6, 0xf, 0xf, 0x7, 0x7, 0x6,
    0xe, 0x37, 0xc0,

    /* U+0054 "T" */
    0xff, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x18, 0x18, 0x18, 0x18,

    /* U+0055 "U" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0xc3, 0xc3, 0x66, 0x3c,

    /* U+0056 "V" */
    0x81, 0xe0, 0xb0, 0x48, 0x66, 0x33, 0x10, 0x98,
    0x6c, 0x34, 0xa, 0x7, 0x3, 0x0,

    /* U+0057 "W" */
    0x82, 0xa, 0xc, 0x2c, 0x71, 0xb1, 0x46, 0x45,
    0x11, 0x16, 0x46, 0xdb, 0x1b, 0x2c, 0x28, 0xa0,
    0xa3, 0x83, 0x8e, 0xe, 0x18,

    /* U+0058 "X" */
    0x60, 0x8c, 0x63, 0x10, 0x6c, 0xa, 0x3, 0x0,
    0xe0, 0x78, 0x13, 0xc, 0x46, 0x19, 0x3,

    /* U+0059 "Y" */
    0xc1, 0xc3, 0x62, 0x66, 0x34, 0x3c, 0x18, 0x18,
    0x18, 0x18, 0x18, 0x18,

    /* U+005A "Z" */
    0x7f, 0x81, 0x80, 0x80, 0xc0, 0xc0, 0x60, 0x60,
    0x30, 0x30, 0x18, 0x18, 0xf, 0xf0,

    /* U+005B "[" */
    0xfe, 0x31, 0x8c, 0x63, 0x18, 0xc6, 0x31, 0x8c,
    0x63, 0xe0,

    /* U+005C "\\" */
    0x81, 0x81, 0x3, 0x6, 0x6, 0xc, 0x8, 0x18,
    0x10, 0x30, 0x60, 0x40, 0xc0,

    /* U+005D "]" */
    0xf8, 0xc6, 0x31, 0x8c, 0x63, 0x18, 0xc6, 0x31,
    0x8f, 0xe0,

    /* U+005E "^" */
    0xc, 0x7, 0x83, 0x31, 0x86, 0xc0, 0xc0,

    /* U+005F "_" */
    0xff, 0x80,

    /* U+0060 "`" */
    0x48, 0x80,

    /* U+0061 "a" */
    0x7c, 0xc, 0x1b, 0xfc, 0x78, 0xf1, 0xe7, 0x76,

    /* U+0062 "b" */
    0xc1, 0x83, 0x6, 0xf, 0xd9, 0xb1, 0xe3, 0xc7,
    0x8f, 0x1e, 0x6f, 0x80,

    /* U+0063 "c" */
    0x3c, 0xc3, 0x6, 0xc, 0x18, 0x30, 0x30, 0x3e,

    /* U+0064 "d" */
    0x6, 0xc, 0x18, 0x33, 0xec, 0xf1, 0xe3, 0xc7,
    0x8f, 0x1b, 0x77, 0x60,

    /* U+0065 "e" */
    0x3c, 0xcb, 0x1e, 0x3f, 0xf8, 0x30, 0x32, 0x3c,

    /* U+0066 "f" */
    0x3d, 0x86, 0x18, 0xf9, 0x86, 0x18, 0x61, 0x86,
    0x18, 0x60,

    /* U+0067 "g" */
    0x3e, 0xcf, 0x1e, 0x3c, 0x78, 0xf1, 0xb3, 0x3e,
    0xc, 0x18, 0x67, 0x80,

    /* U+0068 "h" */
    0xc1, 0x83, 0x6, 0xd, 0xdc, 0xf1, 0xe3, 0xc7,
    0x8f, 0x1e, 0x3c, 0x60,

    /* U+0069 "i" */
    0xc3, 0xff, 0xff,

    /* U+006A "j" */
    0x30, 0x3, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3e,

    /* U+006B "k" */
    0xc0, 0xc0, 0xc0, 0xc0, 0xcc, 0xc8, 0xd8, 0xe0,
    0xf0, 0xd8, 0xcc, 0xc4, 0xc6,

    /* U+006C "l" */
    0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0x70,

    /* U+006D "m" */
    0xfd, 0xdc, 0xcf, 0x19, 0xe3, 0x3c, 0x67, 0x8c,
    0xf1, 0x9e, 0x33, 0xc6, 0x60,

    /* U+006E "n" */
    0xfd, 0xcf, 0x1e, 0x3c, 0x78, 0xf1, 0xe3, 0xc6,

    /* U+006F "o" */
    0x3c, 0x66, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0x66,
    0x3c,

    /* U+0070 "p" */
    0xfd, 0xdb, 0x1e, 0x3c, 0x78, 0xf1, 0xe6, 0xf9,
    0x83, 0x6, 0xc, 0x0,

    /* U+0071 "q" */
    0x3e, 0xcf, 0x1e, 0x3c, 0x78, 0xf1, 0xb7, 0x7e,
    0xc, 0x18, 0x30, 0x60,

    /* U+0072 "r" */
    0xdf, 0x31, 0x8c, 0x63, 0x18, 0xc0,

    /* U+0073 "s" */
    0x7b, 0xc, 0x3c, 0x38, 0x70, 0xc3, 0xf8,

    /* U+0074 "t" */
    0x1, 0x86, 0x3f, 0x61, 0x86, 0x18, 0x61, 0x86,
    0xe,

    /* U+0075 "u" */
    0xc7, 0x8f, 0x1e, 0x3c, 0x78, 0xf1, 0xe7, 0x7e,

    /* U+0076 "v" */
    0x83, 0x8f, 0x12, 0x24, 0xcd, 0xa, 0x1c, 0x30,

    /* U+0077 "w" */
    0x8c, 0x63, 0x1c, 0xcd, 0x7a, 0x56, 0x94, 0xa7,
    0x38, 0xcc, 0x23, 0x0,

    /* U+0078 "x" */
    0x63, 0x11, 0xd, 0x82, 0x81, 0x80, 0xe0, 0xd8,
    0xc6, 0x43, 0x0,

    /* U+0079 "y" */
    0xc1, 0x41, 0x63, 0x62, 0x26, 0x36, 0x34, 0x14,
    0x18, 0x8, 0x18, 0x10, 0x60,

    /* U+007A "z" */
    0x7e, 0x8, 0x30, 0xc1, 0x86, 0x8, 0x30, 0x7e,

    /* U+007B "{" */
    0x1e, 0x60, 0xc1, 0x83, 0x6, 0xc, 0x70, 0x30,
    0x60, 0xc1, 0x83, 0x6, 0x7, 0x80,

    /* U+007C "|" */
    0xff, 0xff, 0xff, 0xfc,

    /* U+007D "}" */
    0xf0, 0x30, 0x60, 0xc1, 0x83, 0x6, 0x7, 0x18,
    0x30, 0x60, 0xc1, 0x83, 0x3c, 0x0,

    /* U+007E "~" */
    0x71, 0x9f, 0xe,

    /* U+00B0 "°" */
    0x69, 0x96
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 90, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 90, .box_w = 3, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 6, .adv_w = 102, .box_w = 4, .box_h = 3, .ofs_x = 1, .ofs_y = 9},
    {.bitmap_index = 8, .adv_w = 218, .box_w = 12, .box_h = 11, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 25, .adv_w = 141, .box_w = 7, .box_h = 16, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 39, .adv_w = 230, .box_w = 13, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 59, .adv_w = 179, .box_w = 10, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 74, .adv_w = 51, .box_w = 2, .box_h = 3, .ofs_x = 1, .ofs_y = 9},
    {.bitmap_index = 75, .adv_w = 115, .box_w = 4, .box_h = 16, .ofs_x = 2, .ofs_y = -3},
    {.bitmap_index = 83, .adv_w = 115, .box_w = 4, .box_h = 16, .ofs_x = 2, .ofs_y = -3},
    {.bitmap_index = 91, .adv_w = 141, .box_w = 6, .box_h = 6, .ofs_x = 1, .ofs_y = 5},
    {.bitmap_index = 96, .adv_w = 154, .box_w = 7, .box_h = 8, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 103, .adv_w = 64, .box_w = 2, .box_h = 4, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 104, .adv_w = 115, .box_w = 4, .box_h = 1, .ofs_x = 2, .ofs_y = 4},
    {.bitmap_index = 105, .adv_w = 77, .box_w = 2, .box_h = 2, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 106, .adv_w = 141, .box_w = 7, .box_h = 14, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 119, .adv_w = 166, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 131, .adv_w = 166, .box_w = 7, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 142, .adv_w = 166, .box_w = 7, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 153, .adv_w = 166, .box_w = 7, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 164, .adv_w = 166, .box_w = 9, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 179, .adv_w = 166, .box_w = 7, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 190, .adv_w = 166, .box_w = 9, .box_h = 12, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 204, .adv_w = 166, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 216, .adv_w = 166, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 228, .adv_w = 166, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 240, .adv_w = 77, .box_w = 3, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 243, .adv_w = 64, .box_w = 2, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 245, .adv_w = 166, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 1},
    {.bitmap_index = 253, .adv_w = 128, .box_w = 6, .box_h = 3, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 256, .adv_w = 166, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 1},
    {.bitmap_index = 264, .adv_w = 141, .box_w = 7, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 275, .adv_w = 256, .box_w = 13, .box_h = 15, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 300, .adv_w = 166, .box_w = 10, .box_h = 12, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 315, .adv_w = 166, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 327, .adv_w = 154, .box_w = 9, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 341, .adv_w = 166, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 353, .adv_w = 154, .box_w = 7, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 364, .adv_w = 141, .box_w = 7, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 375, .adv_w = 166, .box_w = 9, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 389, .adv_w = 179, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 401, .adv_w = 77, .box_w = 2, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 404, .adv_w = 115, .box_w = 6, .box_h = 12, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 413, .adv_w = 154, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 425, .adv_w = 141, .box_w = 7, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 436, .adv_w = 230, .box_w = 11, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 453, .adv_w = 192, .box_w = 9, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 467, .adv_w = 205, .box_w = 11, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 484, .adv_w = 154, .box_w = 7, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 495, .adv_w = 205, .box_w = 11, .box_h = 13, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 513, .adv_w = 166, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 525, .adv_w = 141, .box_w = 7, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 536, .adv_w = 154, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 548, .adv_w = 179, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 560, .adv_w = 166, .box_w = 9, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 574, .adv_w = 243, .box_w = 14, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 595, .adv_w = 166, .box_w = 10, .box_h = 12, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 610, .adv_w = 154, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 622, .adv_w = 154, .box_w = 9, .box_h = 12, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 636, .adv_w = 115, .box_w = 5, .box_h = 15, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 646, .adv_w = 141, .box_w = 7, .box_h = 14, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 659, .adv_w = 128, .box_w = 5, .box_h = 15, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 669, .adv_w = 192, .box_w = 10, .box_h = 5, .ofs_x = 1, .ofs_y = 7},
    {.bitmap_index = 676, .adv_w = 141, .box_w = 9, .box_h = 1, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 678, .adv_w = 141, .box_w = 3, .box_h = 3, .ofs_x = 2, .ofs_y = 10},
    {.bitmap_index = 680, .adv_w = 134, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 688, .adv_w = 154, .box_w = 7, .box_h = 13, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 700, .adv_w = 128, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 708, .adv_w = 154, .box_w = 7, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 720, .adv_w = 141, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 728, .adv_w = 102, .box_w = 6, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 738, .adv_w = 154, .box_w = 7, .box_h = 13, .ofs_x = 1, .ofs_y = -4},
    {.bitmap_index = 750, .adv_w = 154, .box_w = 7, .box_h = 13, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 762, .adv_w = 77, .box_w = 2, .box_h = 12, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 765, .adv_w = 77, .box_w = 4, .box_h = 16, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 773, .adv_w = 141, .box_w = 8, .box_h = 13, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 786, .adv_w = 90, .box_w = 4, .box_h = 13, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 793, .adv_w = 230, .box_w = 11, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 806, .adv_w = 154, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 814, .adv_w = 154, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 823, .adv_w = 154, .box_w = 7, .box_h = 13, .ofs_x = 2, .ofs_y = -4},
    {.bitmap_index = 835, .adv_w = 154, .box_w = 7, .box_h = 13, .ofs_x = 1, .ofs_y = -4},
    {.bitmap_index = 847, .adv_w = 102, .box_w = 5, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 853, .adv_w = 115, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 860, .adv_w = 102, .box_w = 6, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 869, .adv_w = 154, .box_w = 7, .box_h = 9, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 877, .adv_w = 141, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 885, .adv_w = 192, .box_w = 10, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 897, .adv_w = 141, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 908, .adv_w = 141, .box_w = 8, .box_h = 13, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 921, .adv_w = 128, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 929, .adv_w = 166, .box_w = 7, .box_h = 15, .ofs_x = 2, .ofs_y = -3},
    {.bitmap_index = 943, .adv_w = 51, .box_w = 2, .box_h = 15, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 947, .adv_w = 154, .box_w = 7, .box_h = 15, .ofs_x = 2, .ofs_y = -3},
    {.bitmap_index = 961, .adv_w = 192, .box_w = 8, .box_h = 3, .ofs_x = 2, .ofs_y = 4},
    {.bitmap_index = 964, .adv_w = 77, .box_w = 4, .box_h = 4, .ofs_x = 1, .ofs_y = 8}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 95, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 176, .range_length = 1, .glyph_id_start = 96,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};

/*-----------------
 *    KERNING
 *----------------*/


/*Pair left and right glyphs for kerning*/
static const uint8_t kern_pair_glyph_ids[] =
{
    16, 16,
    35, 48,
    35, 53,
    35, 55,
    35, 56,
    35, 67,
    35, 73,
    35, 74,
    35, 75,
    35, 76,
    35, 78,
    35, 81,
    35, 86,
    35, 90,
    36, 48,
    37, 36,
    37, 85,
    38, 48,
    38, 55,
    39, 34,
    39, 66,
    39, 67,
    39, 68,
    39, 69,
    39, 70,
    39, 72,
    39, 73,
    39, 74,
    39, 75,
    39, 76,
    39, 78,
    39, 79,
    39, 80,
    39, 81,
    39, 82,
    39, 83,
    39, 84,
    39, 85,
    39, 86,
    39, 87,
    39, 88,
    39, 90,
    40, 59,
    40, 80,
    40, 82,
    45, 48,
    45, 50,
    45, 53,
    45, 55,
    45, 56,
    45, 58,
    45, 87,
    48, 37,
    48, 53,
    48, 55,
    48, 56,
    48, 57,
    48, 58,
    48, 67,
    48, 73,
    48, 74,
    48, 75,
    49, 34,
    49, 45,
    49, 55,
    49, 57,
    49, 58,
    49, 66,
    49, 68,
    49, 69,
    49, 70,
    49, 72,
    49, 73,
    49, 74,
    49, 75,
    49, 78,
    49, 79,
    49, 80,
    49, 81,
    49, 83,
    49, 86,
    49, 89,
    50, 55,
    50, 56,
    51, 48,
    51, 53,
    51, 55,
    51, 56,
    51, 69,
    52, 55,
    53, 34,
    53, 48,
    53, 66,
    53, 68,
    53, 69,
    53, 70,
    53, 72,
    53, 76,
    53, 78,
    53, 79,
    53, 80,
    53, 81,
    53, 82,
    53, 83,
    53, 84,
    53, 86,
    53, 87,
    53, 88,
    53, 89,
    53, 90,
    54, 34,
    55, 48,
    55, 66,
    55, 69,
    55, 70,
    55, 72,
    55, 74,
    55, 75,
    55, 76,
    55, 78,
    55, 79,
    55, 80,
    55, 81,
    55, 82,
    55, 83,
    55, 86,
    55, 87,
    55, 88,
    55, 89,
    55, 90,
    55, 91,
    56, 34,
    56, 48,
    56, 50,
    56, 69,
    56, 70,
    56, 72,
    56, 73,
    56, 74,
    56, 75,
    56, 76,
    56, 78,
    56, 79,
    56, 81,
    56, 82,
    56, 83,
    56, 86,
    56, 87,
    56, 88,
    56, 89,
    56, 90,
    56, 91,
    57, 48,
    58, 34,
    58, 48,
    58, 66,
    58, 67,
    58, 68,
    58, 69,
    58, 70,
    58, 72,
    58, 74,
    58, 75,
    58, 76,
    58, 78,
    58, 79,
    58, 81,
    58, 82,
    58, 83,
    58, 86,
    58, 87,
    58, 88,
    58, 89,
    58, 90,
    58, 91,
    59, 48,
    66, 53,
    66, 58,
    66, 84,
    68, 53,
    68, 74,
    69, 69,
    69, 79,
    70, 53,
    71, 67,
    71, 68,
    71, 69,
    71, 70,
    71, 71,
    71, 72,
    71, 77,
    71, 78,
    71, 79,
    71, 80,
    71, 81,
    71, 82,
    71, 86,
    71, 88,
    72, 67,
    73, 53,
    73, 58,
    77, 77,
    77, 84,
    79, 75,
    79, 78,
    79, 79,
    80, 53,
    80, 55,
    80, 58,
    81, 53,
    81, 77,
    82, 53,
    82, 58,
    82, 77,
    83, 52,
    83, 71,
    83, 84,
    83, 85,
    83, 90,
    84, 53,
    85, 53,
    85, 85,
    86, 53,
    86, 58,
    86, 75,
    87, 53,
    87, 69,
    88, 69,
    90, 53,
    91, 48,
    91, 53,
    91, 58
};

/* Kerning between the respective left and right glyphs
 * 4.4 format which needs to scaled with `kern_scale`*/
static const int8_t kern_pair_values[] =
{
    -26, -13, -13, -13, -13, -13, -13, -13,
    -13, -13, -13, -13, -13, -13, -13, 13,
    13, -13, -13, -13, -13, -13, -13, -13,
    -13, -13, -13, -13, -13, -13, -13, -13,
    -13, -13, -13, -13, -13, -13, -13, -13,
    -13, -13, 13, 13, 13, -13, -13, -13,
    -19, -13, -13, -13, -13, -13, -13, -13,
    -13, -13, -13, -13, -13, -13, -13, -13,
    -13, -13, -13, -13, -13, -13, -13, -13,
    -13, -13, -13, -13, -13, -13, -13, -13,
    -13, -13, -13, -13, -13, -13, -13, -13,
    -13, -13, -13, -13, -13, -13, -13, -13,
    -13, -13, -13, -13, -13, -13, -13, -13,
    -13, -13, -13, -13, -13, -13, -13, -13,
    -13, -13, -13, -13, -13, -13, -13, -13,
    -13, -13, -13, -13, -13, -13, -13, -13,
    -13, -13, -13, -13, -13, -13, -13, -13,
    -13, -13, -13, -13, -13, -13, -13, -13,
    -13, -13, -13, -13, -13, -13, -13, -13,
    -13, -13, -13, -13, -13, -13, -13, -13,
    -13, -13, -13, -13, -13, -13, -13, -13,
    -13, -13, -13, -13, -13, -13, -13, -13,
    -13, -13, 13, -13, -13, -13, -13, -13,
    -13, -13, -13, -13, -13, -13, -13, -13,
    -13, -13, -13, -13, -13, -13, -13, -13,
    -13, -13, 13, -13, -13, -13, -26, -13,
    -13, -13, -13, -13, -13, -13, 13, 13,
    13, 13, 13, -13, -13, -13, -13, -13,
    -13, -13, -13, -13, -13, -13, -13, -13
};

/*Collect the kern pair's data in one place*/
static const lv_font_fmt_txt_kern_pair_t kern_pairs =
{
    .glyph_ids = kern_pair_glyph_ids,
    .values = kern_pair_values,
    .pair_cnt = 232,
    .glyph_ids_size = 0
};

/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = &kern_pairs,
    .kern_scale = 16,
    .cmap_num = 2,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t b612_16 = {
#else
lv_font_t b612_16 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 18,          /*The maximum line height required by the font*/
    .base_line = 4,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -2,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if B612_16*/

