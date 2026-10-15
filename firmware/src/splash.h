#pragma once
#include <stdint.h>
#include <lvgl.h>

// Initialize splash module. Creates the canvas widget inside `parent` and
// allocates the 480x480 pixel buffer (PSRAM).
void splash_init(lv_obj_t *parent);

// Advance animation frame if hold time elapsed. Call from main loop.
void splash_tick(void);

// Cycle to the next animation in the catalog.
void splash_next(void);

// Show/hide the splash container.
void splash_show(void);
void splash_hide(void);

// Pick the next animation matching the current usage-rate group.
// Called automatically by splash_show(); also exposed so other modules can
// trigger a re-pick when the rate group changes mid-display.
void splash_pick_for_current_rate(void);

// True when splash is currently rendering (used to gate re-picks).
bool splash_is_active(void);


// Mini animated creature for embedding elsewhere (e.g. the idle screen).
// Renders the named official animation (e.g. "cloud") at ~px×px
// inside `parent`; returns the canvas object (position it with lv_obj_align) or
// NULL if the animation isn't found / allocation fails. Drive it with
// splash_mini_tick(). This is the idle screen's creature; other screens use
// the instance API below.
lv_obj_t* splash_mini_create(lv_obj_t *parent, const char *anim_name, int px);
void splash_mini_tick(void);

// Mini creature instances that can switch animation (Pace / Focus screens).
// The buffer is sized once for the largest of `names`, at `cell` px per art
// cell; the canvas resizes to each animation, so position it with
// lv_obj_align (re-applied on resize). Free before deleting its parent.
struct SplashMini;
SplashMini* splash_mini_new(lv_obj_t *parent, const char *const *names, int count, int cell);
void        splash_mini_set(SplashMini *m, const char *anim_name);   // no-op if already playing
void        splash_mini_set_bg(SplashMini *m, uint16_t rgb565);      // art background (default black)
void        splash_mini_step(SplashMini *m);                         // call every loop
lv_obj_t*   splash_mini_obj(SplashMini *m);
void        splash_mini_free(SplashMini *m);

// Pace zone from the daemon's projection (0 comfortable, 1 tight, 2 the limit
// lands before the reset). In zone 2 the splash races (racing car / jumping)
// and the corner mascot fidgets as if usage were heavy.
void splash_set_pace(int zone);
// The 5-hour window just refilled: the next splash pick is "jumping happy".
void splash_celebrate(void);

// Corner mascot (usage screen, PSRAM boards): the still Clawd idles in the
// logo slot, does occasional acts, and takes walk-off/lurk/walk-back trips.
// feet_y = px of the art's ground line; cell = px per art cell in the corner.
lv_obj_t* splash_mascot_create(lv_obj_t *parent, int slot_x, int feet_y, int cell);
void splash_mascot_tick(void);
void splash_mascot_set_visible(bool v);
// Attention: the corner Clawd walks off, a full-size one walks in and waves
// and points until attention(false), then they swap back. No-op without the
// PSRAM mascot.
void splash_mascot_attention(bool on);
