#include "cell_sprite.h"
#include "clawd_anim.h"

static lv_color_t rgb565(uint16_t c) {
    return lv_color_make((uint8_t)(((c >> 11) & 0x1F) * 255 / 31),
                         (uint8_t)(((c >> 5) & 0x3F) * 255 / 63),
                         (uint8_t)((c & 0x1F) * 255 / 31));
}

static void draw_cb(lv_event_t* e) {
    const CellSprite* s = (const CellSprite*)lv_event_get_user_data(e);
    if (!s->cells || !s->palette || s->w <= 0 || s->h <= 0) return;
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_area_t o;
    lv_obj_get_coords(s->obj, &o);

    // Only the cells inside the area being redrawn.
    const lv_area_t& clip = layer->_clip_area;
    const int c = s->cell;
    int gx0 = (clip.x1 - o.x1) / c, gx1 = (clip.x2 - o.x1) / c;
    int gy0 = (clip.y1 - o.y1) / c, gy1 = (clip.y2 - o.y1) / c;
    if (clip.x1 < o.x1) gx0 = 0;
    if (clip.y1 < o.y1) gy0 = 0;
    if (gx1 >= s->w) gx1 = s->w - 1;
    if (gy1 >= s->h) gy1 = s->h - 1;
    if (gx0 > gx1 || gy0 > gy1) return;

    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_opa = LV_OPA_COVER;
    d.radius = 0;
    d.border_width = 0;
    for (int gy = gy0; gy <= gy1; gy++) {
        const uint8_t* row = &s->cells[gy * s->w];
        int gx = gx0;
        while (gx <= gx1) {
            const uint8_t code = row[s->mirror ? s->w - 1 - gx : gx];
            int end = gx + 1;               // one run of the same colour
            while (end <= gx1 && row[s->mirror ? s->w - 1 - end : end] == code) end++;
            if (code && code < CLAWD_PALETTE_SIZE) {
                d.bg_color = rgb565(s->palette[code]);
                const lv_area_t a = { o.x1 + gx * c, o.y1 + gy * c, o.x1 + end * c - 1, o.y1 + (gy + 1) * c - 1 };
                lv_draw_rect(layer, &d, &a);
            }
            gx = end;
        }
    }
}

void cell_sprite_init(CellSprite* s, lv_obj_t* parent, int cell) {
    s->cells = nullptr;
    s->palette = nullptr;
    s->w = s->h = 0;
    s->cell = cell > 0 ? cell : 1;
    s->mirror = false;
    s->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(s->obj);
    lv_obj_set_size(s->obj, 0, 0);
    lv_obj_clear_flag(s->obj, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_add_flag(s->obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s->obj, draw_cb, LV_EVENT_DRAW_MAIN, s);
}

void cell_sprite_set(CellSprite* s, const uint8_t* cells, int w, int h, const uint16_t* palette, bool mirror) {
    s->cells = cells;
    s->palette = palette;
    s->mirror = mirror;
    if (w != s->w || h != s->h) {
        s->w = (int16_t)w;
        s->h = (int16_t)h;
        lv_obj_set_size(s->obj, w * s->cell, h * s->cell);
    }
}

void cell_sprite_set_cell(CellSprite* s, int cell) {
    s->cell = (int16_t)(cell > 0 ? cell : 1);
    lv_obj_set_size(s->obj, s->w * s->cell, s->h * s->cell);
}
