/*******************************************************************************
 * Size: 12 px
 * Bpp: 4
 * Opts: --font assets/StyreneB-Regular.otf -r 0xA3,0xA5,0xB0,0xB7,0x20AC,0x2212 --size 12 --format lvgl --bpp 4 --no-compress -o firmware/src/font_sym_12.c --lv-include lvgl.h
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef FONT_SYM_12
#define FONT_SYM_12 1
#endif

#if FONT_SYM_12

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+00A3 "£" */
    0x0, 0x2c, 0xfd, 0x40, 0x0, 0xba, 0x27, 0xe0,
    0x0, 0xd4, 0x0, 0xd1, 0x0, 0xd4, 0x0, 0x0,
    0x4e, 0xff, 0xee, 0x0, 0x1, 0xe5, 0x11, 0x0,
    0x0, 0xe5, 0x0, 0x0, 0x4f, 0xff, 0xff, 0xfe,

    /* U+00A5 "¥" */
    0xc, 0x90, 0x2, 0xe4, 0x0, 0xc8, 0x1d, 0x50,
    0x0, 0x1c, 0xe5, 0x0, 0xb, 0xef, 0xfe, 0xe4,
    0x0, 0x4, 0xd0, 0x0, 0xb, 0xee, 0xfe, 0xe4,
    0x0, 0x4, 0xd0, 0x0, 0x0, 0x4, 0xd0, 0x0,

    /* U+00B0 "°" */
    0x1b, 0xd8, 0xb, 0x71, 0xc5, 0xe1, 0x7, 0x8a,
    0x71, 0xc5, 0x1b, 0xd8, 0x0,

    /* U+00B7 "·" */
    0xa4, 0xe7,

    /* U+20AC "€" */
    0x0, 0x3c, 0xfe, 0xa0, 0x0, 0xe8, 0x12, 0xc7,
    0x6e, 0xfe, 0xe4, 0x0, 0x0, 0xf1, 0x0, 0x0,
    0x6e, 0xfe, 0xe4, 0x0, 0x0, 0xf4, 0x0, 0x98,
    0x0, 0xab, 0x13, 0xf4, 0x0, 0x1b, 0xfe, 0x70,

    /* U+2212 "−" */
    0x0, 0x0, 0x0, 0xe, 0xee, 0xee, 0xe5
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 136, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 32, .adv_w = 135, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 64, .adv_w = 106, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 77, .adv_w = 56, .box_w = 2, .box_h = 2, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 79, .adv_w = 137, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 111, .adv_w = 134, .box_w = 7, .box_h = 2, .ofs_x = 1, .ofs_y = 3}
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
const lv_font_t font_sym_12 = {
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .release_glyph = NULL,
    .line_height = 9,
    .base_line = 0,
    .subpx = LV_FONT_SUBPX_NONE,
    .kerning = 0,
    .static_bitmap = 0,
    .underline_position = 0,
    .underline_thickness = 1,
    .dsc = &font_dsc,
    .fallback = NULL,
    .user_data = NULL,
};



#endif /*#if FONT_SYM_12*/

