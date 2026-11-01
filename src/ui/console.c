#include "console.h"
#include <stdio.h>
#include <string.h>
/** Returns the start of the preceding UTF-8 codepoint. */
static size_t previous(const char *text, size_t position)
{
    if (!position) return 0;
    --position;
    while (position && ((unsigned char)text[position] & 0xc0) == 0x80) --position;
    return position;
}
/** Returns the end of the next UTF-8 codepoint. */
static size_t next(const char *text, size_t position, size_t length)
{
    if (position == length) return length;
    ++position;
    while (position < length && ((unsigned char)text[position] & 0xc0) == 0x80) ++position;
    return position;
}
void ax_console_print(AxConsole *console, const char *text)
{
    if (!console || !text) return;
    if (console->output_count == AX_CONSOLE_LINES) {
        memmove(console->output, console->output + 1, (AX_CONSOLE_LINES - 1) * sizeof(console->output[0]));
        --console->output_count;
    }
    snprintf(console->output[console->output_count++], AX_COMMAND_TEXT, "%s", text);
}
void ax_console_open(AxConsole *console, bool open)
{
    console->open = open;
    console->input->captured = open;
    if (open) {
        strcpy(console->line, "/");
        console->cursor = console->length = 1;
        console->history_position = console->history_count;
        console->draft[0] = 0;
    }
}
/** Replaces the editable line with a bounded history/draft entry. */
static void replace_line(AxConsole *console, const char *text)
{
    snprintf(console->line, sizeof(console->line), "%s", text);
    console->cursor = console->length = strlen(console->line);
}
/** Executes the current command and preserves history before closing input. */
static void submit(AxConsole *console)
{
    char line[AX_COMMAND_TEXT];
    strcpy(line, console->line);
    if (console->length > 1) {
        if (console->history_count == AX_CONSOLE_HISTORY) {
            memmove(console->history, console->history + 1, (AX_CONSOLE_HISTORY - 1) * sizeof(console->history[0]));
            --console->history_count;
        }
        strcpy(console->history[console->history_count++], line);
        ax_console_print(console, line);
        if (!ax_command_execute(console->commands, line)) ax_console_print(console, "Command failed: unknown name or invalid arguments. Use /core.help");
    }
    ax_console_open(console, false);
}
/** Completes a unique dotted command name and lists ambiguous candidates. */
static void complete(AxConsole *console)
{
    if (!console->length || console->cursor != console->length || strchr(console->line, ' ')) return;
    size_t offset = console->line[0] == '/' ? 1 : 0;
    const char *prefix = console->line + offset;
    const char *match = NULL;
    size_t count = 0;
    for (size_t i = 0; i < console->commands->count; ++i) {
        const char *name = console->commands->entries[i].name;
        if (!strncmp(name, prefix, console->length - offset)) { match = name; ++count; }
    }
    if (count == 1) {
        snprintf(console->line, sizeof(console->line), "/%s ", match);
        console->cursor = console->length = strlen(console->line);
    } else if (count > 1) {
        for (size_t i = 0; i < console->commands->count; ++i) {
            const char *name = console->commands->entries[i].name;
            if (!strncmp(name, prefix, console->length - offset)) ax_console_print(console, name);
        }
    }
}
/** Handles editing/navigation without consuming OS text events twice. */
static void key_event(const AxEvent *event, void *user)
{
    AxConsole *console = user;
    if (!console->open || !event->data.key.pressed) return;
    switch ((AxKey)event->data.key.key) {
    case AX_KEY_ESCAPE: ax_console_open(console, false); break;
    case AX_KEY_ENTER: if (!event->data.key.repeat) submit(console); break;
    case AX_KEY_TAB: complete(console); break;
    case AX_KEY_LEFT: console->cursor = previous(console->line, console->cursor); break;
    case AX_KEY_RIGHT: console->cursor = next(console->line, console->cursor, console->length); break;
    case AX_KEY_HOME: console->cursor = 0; break;
    case AX_KEY_END: console->cursor = console->length; break;
    case AX_KEY_BACKSPACE:
        if (console->cursor) {
            size_t start = previous(console->line, console->cursor);
            memmove(console->line + start, console->line + console->cursor, console->length - console->cursor + 1);
            console->length -= console->cursor - start;
            console->cursor = start;
        }
        break;
    case AX_KEY_DELETE:
        if (console->cursor < console->length) {
            size_t end = next(console->line, console->cursor, console->length);
            memmove(console->line + console->cursor, console->line + end, console->length - end + 1);
            console->length -= end - console->cursor;
        }
        break;
    case AX_KEY_UP:
        if (console->history_position) {
            if (console->history_position == console->history_count) strcpy(console->draft, console->line);
            replace_line(console, console->history[--console->history_position]);
        }
        break;
    case AX_KEY_DOWN:
        if (console->history_position < console->history_count) {
            ++console->history_position;
            replace_line(console, console->history_position == console->history_count ? console->draft : console->history[console->history_position]);
        }
        break;
    default: break;
    }
}
/** Opens on the layout-produced slash and inserts validated Unicode as UTF-8. */
static void text_event(const AxEvent *event, void *user)
{
    AxConsole *console = user;
    uint32_t c = event->data.text.codepoint;
    if (!console->open) { if (c == '/') ax_console_open(console, true); return; }
    char bytes[4];
    size_t count;
    if (c < 0x80) { bytes[0] = (char)c; count = 1; }
    else if (c < 0x800) { bytes[0] = (char)(0xc0 | (c >> 6)); bytes[1] = (char)(0x80 | (c & 63)); count = 2; }
    else if (c < 0x10000) { bytes[0] = (char)(0xe0 | (c >> 12)); bytes[1] = (char)(0x80 | ((c >> 6) & 63)); bytes[2] = (char)(0x80 | (c & 63)); count = 3; }
    else { bytes[0] = (char)(0xf0 | (c >> 18)); bytes[1] = (char)(0x80 | ((c >> 12) & 63)); bytes[2] = (char)(0x80 | ((c >> 6) & 63)); bytes[3] = (char)(0x80 | (c & 63)); count = 4; }
    if (console->length + count >= sizeof(console->line)) return;
    memmove(console->line + console->cursor + count, console->line + console->cursor, console->length - console->cursor + 1);
    memcpy(console->line + console->cursor, bytes, count);
    console->length += count;
    console->cursor += count;
}
bool ax_console_init(AxConsole *console, AxCommands *commands, AxInput *input, AxEvents *events)
{
    if (!console || !commands || !input || !events) return false;
    *console = (AxConsole){.commands = commands, .input = input, .events = events};
    console->key_subscription = ax_event_subscribe(events, AX_EVENT_KEY, key_event, console);
    console->text_subscription = ax_event_subscribe(events, AX_EVENT_TEXT, text_event, console);
    if (!console->key_subscription || !console->text_subscription) { ax_console_destroy(console); return false; }
    ax_console_print(console, "Press / for commands. /core.help lists commands.");
    return true;
}
void ax_console_destroy(AxConsole *console)
{
    if (!console) return;
    ax_event_unsubscribe(console->events, console->key_subscription);
    ax_event_unsubscribe(console->events, console->text_subscription);
    if (console->input) console->input->captured = false;
    *console = (AxConsole){0};
}
