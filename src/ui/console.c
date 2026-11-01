#include "console.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
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
/** Rebuilds sorted command matches when editing the command token; arguments remain untouched. */
static void refresh_suggestions(AxConsole *console)
{
    console->suggestion_count = console->selected_suggestion = 0;
    if (!console->open) return;
    size_t offset = console->line[0] == '/' ? 1 : 0;
    size_t end = offset;
    while (end < console->length && !isspace((unsigned char)console->line[end])) ++end;
    if (console->cursor < offset || console->cursor > end) return;
    for (size_t i = 0; i < console->commands->count; ++i) {
        const AxCommand *entry = &console->commands->entries[i];
        if (strncmp(entry->name, console->line + offset, end - offset)) continue;
        size_t position = console->suggestion_count++;
        while (position && strcmp(console->suggestions[position - 1]->name, entry->name) > 0) {
            console->suggestions[position] = console->suggestions[position - 1];
            --position;
        }
        console->suggestions[position] = entry;
    }
}
/** Moves the suggestion highlight with wraparound, without changing the editable command. */
static void move_suggestion(AxConsole *console, bool backwards)
{
    if (!console->suggestion_count) return;
    if (backwards) console->selected_suggestion = console->selected_suggestion ? console->selected_suggestion - 1 : console->suggestion_count - 1;
    else console->selected_suggestion = (console->selected_suggestion + 1) % console->suggestion_count;
}
void ax_console_print(AxConsole *console, const char *text)
{
    if (!console || !text) return;
    if (console->output_count == AX_CONSOLE_LINES) {
        memmove(console->output, console->output + 1, (AX_CONSOLE_LINES - 1) * sizeof(console->output[0]));
        --console->output_count;
    }
    snprintf(console->output[console->output_count++], AX_COMMAND_TEXT, "%s", text);
    ++console->output_revision;
}
void ax_console_open(AxConsole *console, bool open)
{
    console->open = open;
    console->opening_key_text = false;
    console->input->captured = open;
    if (open) {
        strcpy(console->line, "/");
        console->cursor = console->length = 1;
        console->history_position = console->history_count;
        console->draft[0] = 0;
    }
    refresh_suggestions(console);
}
/** Replaces the editable line with a bounded history/draft entry. */
static void replace_line(AxConsole *console, const char *text)
{
    snprintf(console->line, sizeof(console->line), "%s", text);
    console->cursor = console->length = strlen(console->line);
    refresh_suggestions(console);
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
/** Accepts the highlighted command token, preserving existing values and adding an argument separator. */
static bool complete(AxConsole *console)
{
    if (!console->suggestion_count) return false;
    size_t offset = console->line[0] == '/' ? 1 : 0;
    size_t end = offset;
    while (end < console->length && !isspace((unsigned char)console->line[end])) ++end;
    const char *name = console->suggestions[console->selected_suggestion]->name;
    size_t name_length = strlen(name), tail_length = console->length - end;
    if (name_length + (tail_length ? tail_length : 1) + 1 >= sizeof(console->line)) return false;
    char completed[AX_COMMAND_TEXT];
    completed[0] = '/';
    memcpy(completed + 1, name, name_length);
    if (tail_length) memcpy(completed + 1 + name_length, console->line + end, tail_length + 1);
    else { completed[1 + name_length] = ' '; completed[2 + name_length] = 0; }
    replace_line(console, completed);
    console->cursor = 2 + name_length;
    refresh_suggestions(console);
    return true;
}
/** Executes an exact command, or first accepts a partial/arrow-selected suggestion. */
static void enter(AxConsole *console)
{
    if (console->suggestion_count) {
        size_t offset = console->line[0] == '/' ? 1 : 0;
        const char *name = console->suggestions[console->selected_suggestion]->name;
        if (strcmp(console->line + offset, name)) { complete(console); return; }
    }
    submit(console);
}
/** Handles editing/navigation without consuming OS text events twice. */
static void key_event(const AxEvent *event, void *user)
{
    AxConsole *console = user;
    if (!event->data.key.pressed) {
        if (event->data.key.key == AX_KEY_SLASH) console->opening_key_text = false;
        return;
    }
    if (!event->data.key.repeat) console->opening_key_text = false;
    if (!console->open) {
        if (event->data.key.key == AX_KEY_SLASH && !event->data.key.repeat && !event->data.key.modifiers) {
            ax_console_open(console, true);
            console->opening_key_text = true;
        }
        return;
    }
    switch ((AxKey)event->data.key.key) {
    case AX_KEY_ESCAPE: console->input->consumed[AX_KEY_ESCAPE] = true; ax_console_open(console, false); break;
    case AX_KEY_ENTER: if (!event->data.key.repeat) enter(console); break;
    case AX_KEY_TAB:
        if (event->data.key.modifiers & AX_MOD_SHIFT) move_suggestion(console, true);
        else complete(console);
        break;
    case AX_KEY_LEFT: console->cursor = previous(console->line, console->cursor); refresh_suggestions(console); break;
    case AX_KEY_RIGHT: console->cursor = next(console->line, console->cursor, console->length); refresh_suggestions(console); break;
    case AX_KEY_HOME: console->cursor = 0; refresh_suggestions(console); break;
    case AX_KEY_END: console->cursor = console->length; refresh_suggestions(console); break;
    case AX_KEY_BACKSPACE:
        if (console->cursor) {
            size_t start = previous(console->line, console->cursor);
            memmove(console->line + start, console->line + console->cursor, console->length - console->cursor + 1);
            console->length -= console->cursor - start;
            console->cursor = start;
            refresh_suggestions(console);
        }
        break;
    case AX_KEY_DELETE:
        if (console->cursor < console->length) {
            size_t end = next(console->line, console->cursor, console->length);
            memmove(console->line + console->cursor, console->line + end, console->length - end + 1);
            console->length -= end - console->cursor;
            refresh_suggestions(console);
        }
        break;
    case AX_KEY_UP:
        if (console->suggestion_count && !(event->data.key.modifiers & AX_MOD_ALT)) { move_suggestion(console, true); break; }
        if (console->history_position) {
            if (console->history_position == console->history_count) strcpy(console->draft, console->line);
            replace_line(console, console->history[--console->history_position]);
        }
        break;
    case AX_KEY_DOWN:
        if (console->suggestion_count && !(event->data.key.modifiers & AX_MOD_ALT)) { move_suggestion(console, false); break; }
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
    if (console->opening_key_text) {
        console->opening_key_text = false;
        if (c == '/') return;
    }
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
    refresh_suggestions(console);
}
bool ax_console_init(AxConsole *console, AxCommands *commands, AxInput *input, AxEvents *events)
{
    if (!console || !commands || !input || !events) return false;
    *console = (AxConsole){.commands = commands, .input = input, .events = events};
    console->key_subscription = ax_event_subscribe(events, AX_EVENT_KEY, key_event, console);
    console->text_subscription = ax_event_subscribe(events, AX_EVENT_TEXT, text_event, console);
    if (!console->key_subscription || !console->text_subscription) { ax_console_destroy(console); return false; }
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
