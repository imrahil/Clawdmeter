#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <lvgl.h>

// Clawd's pixel art drawn without a pixel buffer: for boards without PSRAM
// (the C6), where the full-size sprites' RGB565A8 images (~250 KB for the
// big walk-on, ~690 KB for the performer) don't fit.
//
// A cell sprite is an LVGL object whose LV_EVENT_DRAW_MAIN paints the grid
// of palette cells it points at as rectangles, one per run of same-coloured
// cells in a row, and only the rows and columns inside the area LVGL is
// redrawing. Empty cells (code 0) draw nothing, so the meters behind him
// show through exactly as with the image path. It costs a few dozen bytes;
// the caller owns the cell grid and decides what to invalidate.
//
// Same look as the image path: square cells of `cell` px, the palette's
// RGB565 colours, fully opaque.

struct CellSprite {
    lv_obj_t*       obj;
    const uint8_t*  cells;     // w*h palette codes, row-major; NULL draws nothing
    const uint16_t* palette;   // RGB565, CLAWD_PALETTE_SIZE entries
    int16_t         w, h;      // in cells
    int16_t         cell;      // px per cell
    bool            mirror;    // draw each row right to left
};

// A hidden, unclickable object under `parent`.
void cell_sprite_init(CellSprite* s, lv_obj_t* parent, int cell);
// Point it at a frame and size the object to it (w*cell x h*cell). Doesn't
// invalidate: the caller invalidates what changed (or the whole object).
void cell_sprite_set(CellSprite* s, const uint8_t* cells, int w, int h,
                     const uint16_t* palette, bool mirror);
void cell_sprite_set_cell(CellSprite* s, int cell);
