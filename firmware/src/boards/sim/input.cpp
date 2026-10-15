#include "../../hal/input_hal.h"
#include "sim_platform.h"
#include <SDL.h>

void input_hal_init(void) {}

bool input_hal_is_held(InputButton btn) {
    const Uint8* keys = SDL_GetKeyboardState(NULL);
    switch (btn) {
    case INPUT_BTN_PRIMARY:   return keys[SDL_SCANCODE_B] != 0 || sim_script_held('b');
    case INPUT_BTN_SECONDARY: return keys[SDL_SCANCODE_N] != 0 || sim_script_held('n');
    }
    return false;
}
