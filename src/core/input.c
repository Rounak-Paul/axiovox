#include "input.h"
#include <string.h>
void ax_input_begin_frame(AxInput *input)
{
    if (!input) return;
    memset(input->pressed, 0, sizeof(input->pressed));
    memset(input->released, 0, sizeof(input->released));
    input->scroll_x = input->scroll_y = 0;
}
void ax_input_key(AxInput *input, AxKey key, bool pressed, bool repeat, int modifiers)
{
    if (!input || key <= AX_KEY_UNKNOWN || key >= AX_KEY_COUNT) return;
    if (pressed && !input->down[key] && !repeat) input->pressed[key] = true;
    if (!pressed && input->down[key]) input->released[key] = true;
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
        for (int i = 1; i < AX_KEY_COUNT; ++i) if (input->down[i]) ax_input_key(input, (AxKey)i, false, false, 0);
        memset(input->mouse_down, 0, sizeof(input->mouse_down));
    }
    AxEvent event = {.type = AX_EVENT_FOCUS, .data.focus = {focused}};
    ax_event_emit(input->events, &event);
}
bool ax_input_down(const AxInput *input, AxKey key)
{
    return input && !input->captured && key > AX_KEY_UNKNOWN && key < AX_KEY_COUNT && input->down[key];
}

bool ax_input_pressed(const AxInput *input, AxKey key)
{
    return input && !input->captured && key > AX_KEY_UNKNOWN && key < AX_KEY_COUNT && input->pressed[key];
}
bool ax_input_released(const AxInput *input, AxKey key)
{
    return input && !input->captured && key > AX_KEY_UNKNOWN && key < AX_KEY_COUNT && input->released[key];
}
bool ax_input_mouse_down(const AxInput *input, unsigned button)
{
    return input && !input->captured && button < 8 && input->mouse_down[button];
}
