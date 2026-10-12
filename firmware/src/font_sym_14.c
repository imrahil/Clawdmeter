/*******************************************************************************
 * Size: 14 px
 * Bpp: 4
 * Opts: --font assets/StyreneB-Regular.otf -r 0xA3,0xA5,0xB0,0xB7,0x20AC,0x2212 --size 14 --format lvgl --bpp 4 --no-compress -o firmware/src/font_sym_14.c --lv-include lvgl.h
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef FONT_SYM_14
#define FONT_SYM_14 1
#endif

#if FONT_SYM_14

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+00A3 "£" */
    0x0, 0x5, 0xdf, 0xc3, 0x0, 0x0, 0x3f, 0x94,
    0xbe, 0x0, 0x0, 0x7f, 0x0, 0x2f, 0x30, 0x0,
    0x8d, 0x0, 0x7, 0x20, 0x0, 0x8d, 0x0, 0x0,
    0x0, 0x2f, 0xff, 0xff, 0xf0, 0x0, 0x3, 0x9d,
    0x33, 0x30, 0x0, 0x0, 0x8d, 0x0, 0x0, 0x0,
    0x3, 0x9d, 0x33, 0x33, 0x30, 0x2f, 0xff, 0xff,
    0xff, 0xf3,

    /* U+00A5 "¥" */
    0xb, 0xd0, 0x0, 0x1d, 0x90, 0x0, 0xdb, 0x0,
    0xbc, 0x0, 0x0, 0x2e, 0x99, 0xd1, 0x0, 0x0,
    0x3, 0xff, 0x20, 0x0, 0x8, 0xee, 0xff, 0xee,
    0x70, 0x1, 0x11, 0xbb, 0x11, 0x10, 0x0, 0x0,
    0xba, 0x0, 0x0, 0x8, 0xee, 0xff, 0xee, 0x70,
    0x1, 0x11, 0xbb, 0x11, 0x10, 0x0, 0x0, 0xba,
    0x0, 0x0,

    /* U+00B0 "°" */
    0x7, 0xdc, 0x50, 0x6d, 0x35, 0xf3, 0xa7, 0x0,
    0xb7, 0x8c, 0x12, 0xe4, 0xa, 0xff, 0x80, 0x0,
    0x0, 0x0,

    /* U+00B7 "·" */
    0xbd, 0xcd,

    /* U+20AC "€" */
    0x0, 0x5, 0xdf, 0xd8, 0x0, 0x4, 0xf8, 0x36,
    0xf8, 0x0, 0x9d, 0x0, 0x8, 0xb5, 0xff, 0xff,
    0xf2, 0x0, 0x1, 0xba, 0x11, 0x0, 0x4, 0xef,
    0xfe, 0xe2, 0x0, 0x1, 0xab, 0x11, 0x5, 0x90,
    0x7, 0xe0, 0x0, 0xab, 0x0, 0x1f, 0xa4, 0x7f,
    0x40, 0x0, 0x3c, 0xfd, 0x60,

    /* U+2212 "−" */
    0xef, 0xff, 0xff, 0xf7, 0x22, 0x22, 0x22, 0x21
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 158, .box_w = 10, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 50, .adv_w = 158, .box_w = 10, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 100, .adv_w = 124, .box_w = 6, .box_h = 6, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 118, .adv_w = 65, .box_w = 2, .box_h = 2, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 120, .adv_w = 159, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 165, .adv_w = 156, .box_w = 8, .box_h = 2, .ofs_x = 1, .ofs_y = 3}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x2, 0xd, 0x14, 0x2009, 0x216f
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 163, .range_length = 8560, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 6, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/


static const lv_font_fmt_txt_dsc_t font_dsc = {
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
const lv_font_t font_sym_14 = {
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .release_glyph = NULL,
    .line_height = 10,
    .base_line = 0,
    .subpx = LV_FONT_SUBPX_NONE,
    .kerning = 0,
    .static_bitmap = 0,
    .underline_position = -1,
    .underline_thickness = 1,
    .dsc = &font_dsc,
    .fallback = NULL,
    .user_data = NULL,
};



#endif /*#if FONT_SYM_14*/

