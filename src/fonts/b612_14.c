/*******************************************************************************
 * Size: 14 px
 * Bpp: 1
 * Opts: --font b612.ttf --size 14 --bpp 1 --format lvgl --range 0x20-0x7F --range 0xB0 --lv-include lvgl.h -o b612_14.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef B612_14
#define B612_14 1
#endif

#if B612_14

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x0,

    /* U+0021 "!" */
    0x55, 0x55, 0x3c,

    /* U+0022 "\"" */
    0xb6, 0x80,

    /* U+0023 "#" */
    0x18, 0x82, 0x10, 0x46, 0x7f, 0xf1, 0x18, 0x62,
    0xc, 0x47, 0xfe, 0x31, 0x6, 0x20, 0x8c, 0x0,

    /* U+0024 "$" */
    0x11, 0xed, 0x24, 0x91, 0xc3, 0x87, 0x14, 0x59,
    0x7e, 0x10, 0x40,

    /* U+0025 "%" */
    0x70, 0x88, 0x98, 0x89, 0x8, 0xb0, 0x72, 0x0,
    0x60, 0x4, 0xe0, 0xd1, 0x9, 0x11, 0x91, 0x10,
    0xe0,

    /* U+0026 "&" */
    0x3c, 0x64, 0x60, 0x60, 0x30, 0x50, 0x99, 0x8d,
    0x87, 0x46, 0x3f,

    /* U+0027 "'" */
    0xe0,

    /* U+0028 "(" */
    0xd2, 0x49, 0x24, 0x92, 0x49, 0x80,

    /* U+0029 ")" */
    0x44, 0x92, 0x49, 0x24, 0x92, 0xc0,

    /* U+002A "*" */
    0x21, 0x3e, 0xe5, 0x0,

    /* U+002B "+" */
    0x10, 0x41, 0x3f, 0x10, 0x41, 0x0,

    /* U+002C "," */
    0xf6,

    /* U+002D "-" */
    0xf0,

    /* U+002E "." */
    0xf0,

    /* U+002F "/" */
    0xc, 0x21, 0x84, 0x10, 0xc2, 0x18, 0x41, 0xc,
    0x20,

    /* U+0030 "0" */
    0x38, 0x8a, 0x14, 0x18, 0x30, 0x60, 0xc1, 0x82,
    0x88, 0xe0,

    /* U+0031 "1" */
    0x10, 0xc7, 0x24, 0x10, 0x41, 0x4, 0x10, 0x47,
    0xc0,

    /* U+0032 "2" */
    0x7b, 0x30, 0x41, 0xc, 0x61, 0xc, 0x63, 0xf,
    0xc0,

    /* U+0033 "3" */
    0x7c, 0x21, 0x8c, 0x78, 0x30, 0x41, 0xc, 0x6e,
    0x0,

    /* U+0034 "4" */
    0x10, 0x43, 0x8, 0x61, 0x24, 0xa2, 0xfc, 0x20,
    0x80,

    /* U+0035 "5" */
    0xfe, 0x8, 0x20, 0xf8, 0x30, 0x41, 0xc, 0xee,
    0x0,

    /* U+0036 "6" */
    0xc, 0x60, 0x82, 0x7, 0xcc, 0xd0, 0xa1, 0x42,
    0xc8, 0xe0,

    /* U+0037 "7" */
    0xfc, 0x30, 0x86, 0x30, 0x82, 0x10, 0x41, 0x4,
    0x0,

    /* U+0038 "8" */
    0x38, 0x89, 0x13, 0x63, 0x8f, 0xb1, 0xc1, 0x83,
    0x8d, 0xe0,

    /* U+0039 "9" */
    0x38, 0x8a, 0xc, 0x18, 0x28, 0xce, 0x82, 0x4,
    0x31, 0xc0,

    /* U+003A ":" */
    0xf0, 0xf0,

    /* U+003B ";" */
    0xf0, 0xf6,

    /* U+003C "<" */
    0x2, 0x1c, 0xe7, 0xc, 0xc, 0x6, 0x3,

    /* U+003D "=" */
    0xfc, 0xf, 0xc0,

    /* U+003E ">" */
    0x1, 0x80, 0xc0, 0x60, 0x63, 0x9c, 0x60,

    /* U+003F "?" */
    0x7b, 0x10, 0x41, 0x18, 0x43, 0xc, 0x0, 0x82,
    0x0,

    /* U+0040 "@" */
    0x1f, 0x4, 0x11, 0x1, 0x2f, 0x28, 0x13, 0x2,
    0x63, 0xcc, 0x89, 0x91, 0x32, 0x6d, 0x37, 0x20,
    0x2, 0x10, 0x3e, 0x0,

    /* U+0041 "A" */
    0x8, 0xe, 0x5, 0x2, 0x81, 0x61, 0xb0, 0x88,
    0x7c, 0x63, 0x20, 0x90, 0x40,

    /* U+0042 "B" */
    0xfa, 0x38, 0x61, 0x8b, 0xc8, 0xe1, 0x86, 0x3f,
    0x80,

    /* U+0043 "C" */
    0x3c, 0x81, 0x4, 0x8, 0x10, 0x20, 0x40, 0x40,
    0xc0, 0xf0,

    /* U+0044 "D" */
    0xf9, 0x1a, 0x14, 0x18, 0x30, 0x60, 0xc1, 0x85,
    0x1b, 0xc0,

    /* U+0045 "E" */
    0xfe, 0x8, 0x20, 0x83, 0xe8, 0x20, 0x82, 0xf,
    0xc0,

    /* U+0046 "F" */
    0xfa, 0x8, 0x20, 0x83, 0xe8, 0x20, 0x82, 0x8,
    0x0,

    /* U+0047 "G" */
    0x3e, 0x60, 0x40, 0x80, 0x80, 0x8f, 0x81, 0x81,
    0x41, 0x61, 0x1e,

    /* U+0048 "H" */
    0x1, 0x6, 0xc, 0x18, 0x3f, 0xe0, 0xc1, 0x83,
    0x6, 0x8,

    /* U+0049 "I" */
    0xff, 0xe0,

    /* U+004A "J" */
    0x31, 0x11, 0x11, 0x11, 0x11, 0xe0,

    /* U+004B "K" */
    0x8d, 0x12, 0x45, 0x8e, 0x1c, 0x28, 0x58, 0x99,
    0x12, 0x10,

    /* U+004C "L" */
    0x82, 0x8, 0x20, 0x82, 0x8, 0x20, 0x82, 0xf,
    0x80,

    /* U+004D "M" */
    0x80, 0xf0, 0x3c, 0x1f, 0x85, 0xa3, 0x6c, 0x99,
    0x26, 0x71, 0x8c, 0x62, 0x18, 0x4,

    /* U+004E "N" */
    0x83, 0x87, 0xd, 0x1b, 0x32, 0x66, 0xc5, 0x87,
    0xe, 0x8,

    /* U+004F "O" */
    0x3e, 0x31, 0x90, 0x50, 0x18, 0xc, 0x6, 0x3,
    0x1, 0x41, 0x31, 0x8f, 0x80,

    /* U+0050 "P" */
    0xf2, 0x38, 0x61, 0x86, 0x2f, 0x20, 0x82, 0x8,
    0x0,

    /* U+0051 "Q" */
    0x3c, 0x31, 0x90, 0x50, 0x18, 0xc, 0x6, 0x3,
    0x1, 0x4d, 0xb3, 0x8f, 0xc0, 0x30,

    /* U+0052 "R" */
    0xf1, 0x1a, 0x14, 0x28, 0x51, 0x3c, 0x48, 0x99,
    0x1a, 0x18,

    /* U+0053 "S" */
    0x7a, 0x8, 0x20, 0x60, 0xe0, 0xc1, 0x6, 0x3f,
    0x0,

    /* U+0054 "T" */
    0xfe, 0x20, 0x40, 0x81, 0x2, 0x4, 0x8, 0x10,
    0x20, 0x40,

    /* U+0055 "U" */
    0x83, 0x6, 0xc, 0x18, 0x30, 0x60, 0xc1, 0x82,
    0x88, 0xe0,

    /* U+0056 "V" */
    0x41, 0xa0, 0x98, 0x44, 0x62, 0x21, 0x90, 0x58,
    0x28, 0x14, 0xe, 0x3, 0x0,

    /* U+0057 "W" */
    0x42, 0x1a, 0x18, 0x91, 0xc4, 0xca, 0x22, 0x53,
    0x12, 0xd0, 0x96, 0x87, 0x94, 0x38, 0xa0, 0xc6,
    0x6, 0x30,

    /* U+0058 "X" */
    0x61, 0x11, 0x8c, 0x82, 0x81, 0xc0, 0x40, 0x50,
    0x6c, 0x22, 0x31, 0x90, 0x40,

    /* U+0059 "Y" */
    0xc6, 0x89, 0x91, 0x42, 0x82, 0x4, 0x8, 0x10,
    0x20, 0x40,

    /* U+005A "Z" */
    0x7e, 0x2, 0x4, 0xc, 0x8, 0x18, 0x10, 0x30,
    0x20, 0x40, 0xff,

    /* U+005B "[" */
    0xf8, 0x88, 0x88, 0x88, 0x88, 0x88, 0x8f,

    /* U+005C "\\" */
    0x83, 0x4, 0x10, 0x60, 0x83, 0x4, 0x10, 0x60,
    0x83,

    /* U+005D "]" */
    0xf1, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1f,

    /* U+005E "^" */
    0x18, 0x1e, 0x19, 0x98, 0x20,

    /* U+005F "_" */
    0xff,

    /* U+0060 "`" */
    0xc8,

    /* U+0061 "a" */
    0xf8, 0x10, 0x5f, 0xc6, 0x18, 0xdd,

    /* U+0062 "b" */
    0x82, 0x8, 0x20, 0xfb, 0x38, 0x61, 0x86, 0x18,
    0xbc,

    /* U+0063 "c" */
    0x3a, 0x61, 0x8, 0x41, 0x7,

    /* U+0064 "d" */
    0x4, 0x10, 0x41, 0x3d, 0x18, 0x61, 0x86, 0x14,
    0xdd,

    /* U+0065 "e" */
    0x39, 0x18, 0x7f, 0x82, 0x4, 0x4e,

    /* U+0066 "f" */
    0x3a, 0x10, 0x8f, 0x21, 0x8, 0x42, 0x10, 0x80,

    /* U+0067 "g" */
    0x3d, 0x18, 0x61, 0x86, 0x14, 0xcf, 0x4, 0x10,
    0xdc,

    /* U+0068 "h" */
    0x84, 0x21, 0xf, 0x46, 0x31, 0x8c, 0x63, 0x10,

    /* U+0069 "i" */
    0x9f, 0xe0,

    /* U+006A "j" */
    0x20, 0x12, 0x49, 0x24, 0x92, 0x70,

    /* U+006B "k" */
    0x82, 0x8, 0x20, 0x9a, 0xce, 0x30, 0xe2, 0xc9,
    0xa3,

    /* U+006C "l" */
    0x92, 0x49, 0x24, 0x92, 0x30,

    /* U+006D "m" */
    0xf3, 0xa3, 0x18, 0x86, 0x21, 0x88, 0x62, 0x18,
    0x86, 0x21,

    /* U+006E "n" */
    0xf4, 0x63, 0x18, 0xc6, 0x31,

    /* U+006F "o" */
    0x38, 0x8a, 0xc, 0x18, 0x30, 0x51, 0x1c,

    /* U+0070 "p" */
    0xfb, 0x28, 0x61, 0x86, 0x18, 0xbc, 0x82, 0x8,
    0x20,

    /* U+0071 "q" */
    0x3d, 0x18, 0x61, 0x86, 0x14, 0xdf, 0x4, 0x10,
    0x41,

    /* U+0072 "r" */
    0xbc, 0x88, 0x88, 0x88,

    /* U+0073 "s" */
    0x7c, 0x20, 0xe1, 0x84, 0x3e,

    /* U+0074 "t" */
    0x44, 0xf4, 0x44, 0x44, 0x43,

    /* U+0075 "u" */
    0x8c, 0x63, 0x18, 0xc6, 0x2f,

    /* U+0076 "v" */
    0x42, 0x8d, 0x91, 0x22, 0xc7, 0x6, 0xc,

    /* U+0077 "w" */
    0x88, 0xcc, 0xa6, 0x5b, 0xa5, 0x53, 0x31, 0x98,
    0x44,

    /* U+0078 "x" */
    0x42, 0x64, 0x3c, 0x18, 0x18, 0x2c, 0x66, 0x42,

    /* U+0079 "y" */
    0xc2, 0x8d, 0x91, 0x22, 0xc7, 0x6, 0xc, 0x10,
    0x63, 0x80,

    /* U+007A "z" */
    0x7c, 0x21, 0x84, 0x30, 0x84, 0x3f,

    /* U+007B "{" */
    0x19, 0x8, 0x42, 0x13, 0x4, 0x21, 0x8, 0x42,
    0xc,

    /* U+007C "|" */
    0xff, 0xfc,

    /* U+007D "}" */
    0xc0, 0x82, 0x8, 0x20, 0x81, 0xc8, 0x20, 0x82,
    0x8, 0x23, 0x0,

    /* U+007E "~" */
    0x71, 0xcf, 0x6,

    /* U+00B0 "°" */
    0x69, 0x96
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 78, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 78, .box_w = 2, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 4, .adv_w = 90, .box_w = 3, .box_h = 3, .ofs_x = 1, .ofs_y = 8},
    {.bitmap_index = 6, .adv_w = 190, .box_w = 11, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 22, .adv_w = 123, .box_w = 6, .box_h = 14, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 33, .adv_w = 202, .box_w = 12, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 50, .adv_w = 157, .box_w = 8, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 61, .adv_w = 45, .box_w = 1, .box_h = 3, .ofs_x = 1, .ofs_y = 8},
    {.bitmap_index = 62, .adv_w = 101, .box_w = 3, .box_h = 14, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 68, .adv_w = 101, .box_w = 3, .box_h = 14, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 74, .adv_w = 123, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 5},
    {.bitmap_index = 78, .adv_w = 134, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 84, .adv_w = 56, .box_w = 2, .box_h = 4, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 85, .adv_w = 101, .box_w = 4, .box_h = 1, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 86, .adv_w = 67, .box_w = 2, .box_h = 2, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 87, .adv_w = 123, .box_w = 6, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 96, .adv_w = 146, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 106, .adv_w = 146, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 115, .adv_w = 146, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 124, .adv_w = 146, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 133, .adv_w = 146, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 142, .adv_w = 146, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 151, .adv_w = 146, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 161, .adv_w = 146, .box_w = 6, .box_h = 11, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 170, .adv_w = 146, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 180, .adv_w = 146, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 190, .adv_w = 67, .box_w = 2, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 192, .adv_w = 56, .box_w = 2, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 194, .adv_w = 146, .box_w = 7, .box_h = 8, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 201, .adv_w = 112, .box_w = 6, .box_h = 3, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 204, .adv_w = 146, .box_w = 7, .box_h = 8, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 211, .adv_w = 123, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 220, .adv_w = 224, .box_w = 11, .box_h = 14, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 240, .adv_w = 146, .box_w = 9, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 253, .adv_w = 146, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 262, .adv_w = 134, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 272, .adv_w = 146, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 282, .adv_w = 134, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 291, .adv_w = 123, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 300, .adv_w = 146, .box_w = 8, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 311, .adv_w = 157, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 321, .adv_w = 67, .box_w = 1, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 323, .adv_w = 101, .box_w = 4, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 329, .adv_w = 134, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 339, .adv_w = 123, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 348, .adv_w = 202, .box_w = 10, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 362, .adv_w = 168, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 372, .adv_w = 179, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 385, .adv_w = 134, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 394, .adv_w = 179, .box_w = 9, .box_h = 12, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 408, .adv_w = 146, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 418, .adv_w = 123, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 427, .adv_w = 134, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 437, .adv_w = 157, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 447, .adv_w = 146, .box_w = 9, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 460, .adv_w = 213, .box_w = 13, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 478, .adv_w = 146, .box_w = 9, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 491, .adv_w = 134, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 501, .adv_w = 134, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 512, .adv_w = 101, .box_w = 4, .box_h = 14, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 519, .adv_w = 123, .box_w = 6, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 528, .adv_w = 112, .box_w = 4, .box_h = 14, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 535, .adv_w = 168, .box_w = 9, .box_h = 4, .ofs_x = 1, .ofs_y = 7},
    {.bitmap_index = 540, .adv_w = 123, .box_w = 8, .box_h = 1, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 541, .adv_w = 123, .box_w = 3, .box_h = 2, .ofs_x = 2, .ofs_y = 9},
    {.bitmap_index = 542, .adv_w = 118, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 548, .adv_w = 134, .box_w = 6, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 557, .adv_w = 112, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 562, .adv_w = 134, .box_w = 6, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 571, .adv_w = 123, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 577, .adv_w = 90, .box_w = 5, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 585, .adv_w = 134, .box_w = 6, .box_h = 12, .ofs_x = 1, .ofs_y = -4},
    {.bitmap_index = 594, .adv_w = 134, .box_w = 5, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 602, .adv_w = 67, .box_w = 1, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 604, .adv_w = 67, .box_w = 3, .box_h = 15, .ofs_x = -1, .ofs_y = -4},
    {.bitmap_index = 610, .adv_w = 123, .box_w = 6, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 619, .adv_w = 78, .box_w = 3, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 624, .adv_w = 202, .box_w = 10, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 634, .adv_w = 134, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 639, .adv_w = 134, .box_w = 7, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 646, .adv_w = 134, .box_w = 6, .box_h = 12, .ofs_x = 1, .ofs_y = -4},
    {.bitmap_index = 655, .adv_w = 134, .box_w = 6, .box_h = 12, .ofs_x = 1, .ofs_y = -4},
    {.bitmap_index = 664, .adv_w = 90, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 668, .adv_w = 101, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 673, .adv_w = 90, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 678, .adv_w = 134, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 683, .adv_w = 123, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 690, .adv_w = 168, .box_w = 9, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 699, .adv_w = 123, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 707, .adv_w = 123, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 717, .adv_w = 112, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 723, .adv_w = 145, .box_w = 5, .box_h = 14, .ofs_x = 2, .ofs_y = -3},
    {.bitmap_index = 732, .adv_w = 45, .box_w = 1, .box_h = 14, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 734, .adv_w = 134, .box_w = 6, .box_h = 14, .ofs_x = 2, .ofs_y = -3},
    {.bitmap_index = 745, .adv_w = 168, .box_w = 8, .box_h = 3, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 748, .adv_w = 67, .box_w = 4, .box_h = 4, .ofs_x = 1, .ofs_y = 7}
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
    -22, -11, -11, -11, -11, -11, -11, -11,
    -11, -11, -11, -11, -11, -11, -11, 11,
    11, -11, -11, -11, -11, -11, -11, -11,
    -11, -11, -11, -11, -11, -11, -11, -11,
    -11, -11, -11, -11, -11, -11, -11, -11,
    -11, -11, 11, 11, 11, -11, -11, -11,
    -17, -11, -11, -11, -11, -11, -11, -11,
    -11, -11, -11, -11, -11, -11, -11, -11,
    -11, -11, -11, -11, -11, -11, -11, -11,
    -11, -11, -11, -11, -11, -11, -11, -11,
    -11, -11, -11, -11, -11, -11, -11, -11,
    -11, -11, -11, -11, -11, -11, -11, -11,
    -11, -11, -11, -11, -11, -11, -11, -11,
    -11, -11, -11, -11, -11, -11, -11, -11,
    -11, -11, -11, -11, -11, -11, -11, -11,
    -11, -11, -11, -11, -11, -11, -11, -11,
    -11, -11, -11, -11, -11, -11, -11, -11,
    -11, -11, -11, -11, -11, -11, -11, -11,
    -11, -11, -11, -11, -11, -11, -11, -11,
    -11, -11, -11, -11, -11, -11, -11, -11,
    -11, -11, -11, -11, -11, -11, -11, -11,
    -11, -11, -11, -11, -11, -11, -11, -11,
    -11, -11, 11, -11, -11, -11, -11, -11,
    -11, -11, -11, -11, -11, -11, -11, -11,
    -11, -11, -11, -11, -11, -11, -11, -11,
    -11, -11, 11, -11, -11, -11, -22, -11,
    -11, -11, -11, -11, -11, -11, 11, 11,
    11, 11, 11, -11, -11, -11, -11, -11,
    -11, -11, -11, -11, -11, -11, -11, -11
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
const lv_font_t b612_14 = {
#else
lv_font_t b612_14 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 16,          /*The maximum line height required by the font*/
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



#endif /*#if B612_14*/

