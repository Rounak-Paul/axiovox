#pragma once
#include "event.h"
typedef enum { AX_MOD_SHIFT = 1, AX_MOD_CONTROL = 2, AX_MOD_ALT = 4, AX_MOD_SUPER = 8 } AxModifiers;
typedef enum {
    AX_KEY_UNKNOWN, AX_KEY_SLASH, AX_KEY_ESCAPE, AX_KEY_ENTER, AX_KEY_BACKSPACE,
    AX_KEY_DELETE, AX_KEY_LEFT, AX_KEY_RIGHT, AX_KEY_HOME, AX_KEY_END,
    AX_KEY_UP, AX_KEY_DOWN, AX_KEY_TAB, AX_KEY_SPACE,
    AX_KEY_A, AX_KEY_B, AX_KEY_C, AX_KEY_D, AX_KEY_E, AX_KEY_F, AX_KEY_G,
    AX_KEY_H, AX_KEY_I, AX_KEY_J, AX_KEY_K, AX_KEY_L, AX_KEY_M, AX_KEY_N,
    AX_KEY_O, AX_KEY_P, AX_KEY_Q, AX_KEY_R, AX_KEY_S, AX_KEY_T, AX_KEY_U,
    AX_KEY_V, AX_KEY_W, AX_KEY_X, AX_KEY_Y, AX_KEY_Z,
    AX_KEY_0, AX_KEY_1, AX_KEY_2, AX_KEY_3, AX_KEY_4, AX_KEY_5,
    AX_KEY_6, AX_KEY_7, AX_KEY_8, AX_KEY_9,
    AX_KEY_F1, AX_KEY_F2, AX_KEY_F3, AX_KEY_F4, AX_KEY_F5, AX_KEY_F6,
    AX_KEY_F7, AX_KEY_F8, AX_KEY_F9, AX_KEY_F10, AX_KEY_F11, AX_KEY_F12,
    AX_KEY_LEFT_SHIFT, AX_KEY_RIGHT_SHIFT, AX_KEY_LEFT_CONTROL, AX_KEY_RIGHT_CONTROL,
    AX_KEY_LEFT_ALT, AX_KEY_RIGHT_ALT, AX_KEY_LEFT_SUPER, AX_KEY_RIGHT_SUPER,
    AX_KEY_COUNT
} AxKey;
typedef struct {
    AxEvents *events;
    bool down[AX_KEY_COUNT], pressed[AX_KEY_COUNT], released[AX_KEY_COUNT];
    bool consumed[AX_KEY_COUNT];
    bool mouse_down[8], mouse_pressed[8], mouse_released[8], captured;
    double mouse_x, mouse_y, scroll_x, scroll_y;
} AxInput;
/** Resets per-frame transitions before polling OS events. Owner-thread only. */
void ax_input_begin_frame(AxInput *input);
/** Updates key state and emits a key event. Repeat never creates a new press transition. */
void ax_input_key(AxInput *input, AxKey key, bool pressed, bool repeat, int modifiers);
/** Emits valid Unicode text independently of physical key state. */
void ax_input_text(AxInput *input, uint32_t codepoint);
/** Clears held keys/buttons on focus loss and emits focus state. */
void ax_input_focus(AxInput *input, bool focused);
/** Returns gameplay held state, suppressed while a UI captures input. */
bool ax_input_down(const AxInput *input, AxKey key);

/** Returns gameplay press/release transitions, suppressed while UI captures input. */
bool ax_input_pressed(const AxInput *input, AxKey key);
bool ax_input_released(const AxInput *input, AxKey key);
/** Returns a pointer button's gameplay held state, suppressed while UI captures input. */
bool ax_input_mouse_down(const AxInput *input, unsigned button);

/** Updates pointer-button transitions; consumed by UI before gameplay queries. */
void ax_input_mouse_button(AxInput *input, unsigned button, bool pressed);
