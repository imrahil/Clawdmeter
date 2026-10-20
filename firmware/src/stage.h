#pragma once
#include <stdbool.h>
#include <lvgl.h>

// The stage: a black curtain that rises from the bottom bezel over the
// meters (pages, battery and the corner seat too), so Clawd performs full
// size on a black screen, as on v1's splash, rather than in front of the
// meters. The performer is drawn above it; nothing else is.
//
// It's a Field (field.h): a flat rectangle that redraws only the strips it
// gains or loses, so the rise and the drop are cheap even on the C6. Once
// it's fully up the meters are hidden underneath (nothing redraws behind
// the black), and shown again as it starts to drop. Owned and sequenced by
// the director: the screensaver, a tap on the meters (the peek) and earned
// shows run on it.

void stage_init(lv_obj_t* parent);   // after the meters and the corner Clawd, before the performer
void stage_tick(void);

void stage_rise(void);               // up from the bottom bezel
void stage_drop(void);               // back down; hidden when gone
void stage_hide_now(void);           // no animation (a page opens, a mode switch)

bool stage_visible(void);            // any part of it on screen
bool stage_moving(void);             // rising or dropping
