#include "input.h"
#include <math.h>
#include <string.h>
void ax_input_begin_frame(AxInput *input)
{
    if (!input) return;
    memset(input->pressed, 0, sizeof(input->pressed));
    memset(input->released, 0, sizeof(input->released));
    memset(input->repeated, 0, sizeof(input->repeated));
    memset(input->consumed, 0, sizeof(input->consumed));
    memset(input->mouse_pressed, 0, sizeof(input->mouse_pressed));
    memset(input->mouse_released, 0, sizeof(input->mouse_released));
    input->focus_lost = false;
    input->scroll_x = input->scroll_y = 0;
    input->mouse_dx = input->mouse_dy = 0;
}
void ax_input_key(AxInput *input, AxKey key, bool pressed, bool repeat, int modifiers)
{
    if (!input || key <= AX_KEY_UNKNOWN || key >= AX_KEY_COUNT) return;
    if (pressed && !input->down[key] && !repeat) input->pressed[key] = true;
    if (!pressed && input->down[key]) input->released[key] = true;
    if (pressed && repeat) input->repeated[key] = true;
    input->down[key] = pressed;
    AxEvent event = {.type = AX_EVENT_KEY, .data.key = {key, modifiers, pressed, repeat}};
    ax_event_emit(input->events, &event);
}
void ax_input_text(AxInput *input, uint32_t codepoint)
{
    if (!input || codepoint < 32 || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return;
    AxEvent event = {.type = AX_EVENT_TEXT, .data.text = {codepoint}};
    ax_event_emit(input->events, &event);
}
void ax_input_focus(AxInput *input, bool focused)
{
    if (!input) return;
    if (!focused) {
        input->focus_lost = true;
        for (int i = 1; i < AX_KEY_COUNT; ++i) if (input->down[i]) ax_input_key(input, (AxKey)i, false, false, 0);
        for (unsigned i = 0; i < 8; ++i) if (input->mouse_down[i]) ax_input_mouse_button(input, i, false);
        memset(input->pressed, 0, sizeof(input->pressed));
        memset(input->repeated, 0, sizeof(input->repeated));
        memset(input->mouse_pressed, 0, sizeof(input->mouse_pressed));
        input->scroll_x = input->scroll_y = 0;
    }
    input->pointer_valid = false;
    input->mouse_dx = input->mouse_dy = 0;
    AxEvent event = {.type = AX_EVENT_FOCUS, .data.focus = {focused}};
    ax_event_emit(input->events, &event);
}
bool ax_input_down(const AxInput *input, AxKey key)
{
    return input && !input->captured && key > AX_KEY_UNKNOWN && key < AX_KEY_COUNT && !input->consumed[key] && input->down[key];
}

bool ax_input_pressed(const AxInput *input, AxKey key)
{
    return input && !input->captured && key > AX_KEY_UNKNOWN && key < AX_KEY_COUNT && !input->consumed[key] && input->pressed[key];
}
bool ax_input_released(const AxInput *input, AxKey key)
{
    return input && !input->captured && key > AX_KEY_UNKNOWN && key < AX_KEY_COUNT && !input->consumed[key] && input->released[key];
}
bool ax_input_mouse_down(const AxInput *input, unsigned button)
{
    return input && !input->captured && button < 8 && input->mouse_down[button];
}

void ax_input_mouse_button(AxInput *input, unsigned button, bool pressed)
{
    if (!input || button >= 8) return;
    if (pressed && !input->mouse_down[button]) input->mouse_pressed[button] = true;
    if (!pressed && input->mouse_down[button]) input->mouse_released[button] = true;
    input->mouse_down[button] = pressed;
}

void ax_input_mouse_move(AxInput *input, double x, double y)
{
    if (!input || !isfinite(x) || !isfinite(y)) return;
    if (input->pointer_valid) {
        input->mouse_dx += x - input->mouse_x;
        input->mouse_dy += y - input->mouse_y;
    }
    input->mouse_x = x;
    input->mouse_y = y;
    input->pointer_valid = true;
}

void ax_input_set_pointer_locked(AxInput *input, bool locked)
{
    if (!input) return;
    input->pointer_locked = locked;
    input->pointer_valid = false;
    input->mouse_dx = input->mouse_dy = 0;
}

bool ax_input_mouse_delta(const AxInput *input, double *dx, double *dy)
{
    bool available = input && !input->captured && input->pointer_locked && isfinite(input->mouse_dx) && isfinite(input->mouse_dy);
    if (dx) *dx = available ? input->mouse_dx : 0;
    if (dy) *dy = available ? input->mouse_dy : 0;
    return available;
}
