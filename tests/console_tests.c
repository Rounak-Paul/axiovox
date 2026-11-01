#include "ui/console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); return EXIT_FAILURE; } } while (0)
typedef struct { int calls; int argc; char value[AX_COMMAND_TEXT]; } State;
/** Captures dispatched argv for transport-independent invocation checks. */
static bool command(int argc, const char *const *argv, void *user)
{
    State *state = user;
    ++state->calls;
    state->argc = argc;
    if (argc > 1) snprintf(state->value, sizeof(state->value), "%s", argv[1]);
    return true;
}
/** Types ASCII through the same text path as the window adapter. */
static void type(AxInput *input, const char *text)
{
    while (*text) ax_input_text(input, (unsigned char)*text++);
}
/** Sends one key press/release to the console editor. */
static void key(AxInput *input, AxKey key)
{
    ax_input_key(input, key, true, false, 0);
    ax_input_key(input, key, false, false, 0);
}
/** Verifies parsing, direct invocation, input capture, UTF-8 editing, history, and completion. */
int main(void)
{
    AxEvents events = {0};
    AxInput input = {.events = &events};
    AxCommands commands = {0};
    State state = {0};
    AxConsole console;
    CHECK(ax_command_register(&commands, "game.player.name", command, &state));
    CHECK(!ax_command_register(&commands, "game..name", command, &state));
    CHECK(!ax_command_register(&commands, ".name", command, &state));
    CHECK(!ax_command_register(&commands, "game.name.", command, &state));
    CHECK(ax_command_execute(&commands, "/game.player.name \"hello world\""));
    CHECK(state.argc == 2 && !strcmp(state.value, "hello world"));
    CHECK(ax_command_execute(&commands, "game.player.name 'hello \\world'"));
    CHECK(!strcmp(state.value, "hello world"));
    CHECK(ax_command_execute(&commands, "game.player.name \"\""));
    CHECK(state.argc == 2 && !strcmp(state.value, ""));
    CHECK(!ax_command_execute(&commands, "game.player.name \"unclosed"));
    CHECK(!ax_command_execute(&commands, "game.player.name dangling\\"));
    CHECK(!ax_command_execute(&commands, "game.player.name a b c d e f g h i j k l m n o p"));
    const char *argv[] = {"game.player.name", "from script adapter"};
    CHECK(ax_command_invoke(&commands, 2, argv));
    CHECK(!strcmp(state.value, "from script adapter"));
    CHECK(ax_console_init(&console, &commands, &input, &events));
    ax_input_key(&input, AX_KEY_W, true, false, 0);
    CHECK(ax_input_down(&input, AX_KEY_W));
    CHECK(ax_input_pressed(&input, AX_KEY_W));
    ax_input_begin_frame(&input);
    CHECK(!ax_input_pressed(&input, AX_KEY_W));
    ax_input_key(&input, AX_KEY_W, true, true, 0);
    CHECK(!ax_input_pressed(&input, AX_KEY_W));
    ax_input_key(&input, AX_KEY_SLASH, true, false, AX_MOD_CONTROL);
    CHECK(!console.open);
    ax_input_key(&input, AX_KEY_SLASH, false, false, 0);
    ax_input_key(&input, AX_KEY_SLASH, true, false, 0);
    CHECK(console.open && input.captured && !strcmp(console.line, "/"));
    type(&input, "/");
    CHECK(!strcmp(console.line, "/"));
    ax_input_key(&input, AX_KEY_SLASH, false, false, 0);
    key(&input, AX_KEY_ESCAPE);
    key(&input, AX_KEY_SLASH);
    CHECK(console.open && !strcmp(console.line, "/"));
    key(&input, AX_KEY_ESCAPE);
    type(&input, "/");
    CHECK(console.open && input.captured && !strcmp(console.line, "/"));
    CHECK(!ax_input_down(&input, AX_KEY_W));
    type(&input, "game.player.name test");
    key(&input, AX_KEY_ENTER);
    CHECK(!console.open && !input.captured && !strcmp(state.value, "test"));
    type(&input, "/");
    ax_input_key(&input, AX_KEY_UP, true, false, AX_MOD_ALT);
    ax_input_key(&input, AX_KEY_UP, false, false, 0);
    CHECK(!strcmp(console.line, "/game.player.name test"));
    key(&input, AX_KEY_DOWN);
    CHECK(!strcmp(console.line, "/"));
    type(&input, "game.pl");
    key(&input, AX_KEY_TAB);
    CHECK(!strcmp(console.line, "/game.player.name "));
    ax_input_text(&input, 0x1f600);
    CHECK(console.length == strlen("/game.player.name ") + 4);
    key(&input, AX_KEY_LEFT);
    key(&input, AX_KEY_DELETE);
    CHECK(!strcmp(console.line, "/game.player.name "));
    ax_input_text(&input, 0xe9);
    key(&input, AX_KEY_BACKSPACE);
    CHECK(!strcmp(console.line, "/game.player.name "));
    size_t length = console.length;
    ax_input_text(&input, 0xd800);
    ax_input_text(&input, 0x110000);
    CHECK(console.length == length);
    for (size_t i = 0; i < AX_COMMAND_TEXT + 20; ++i) ax_input_text(&input, 'x');
    CHECK(console.length == AX_COMMAND_TEXT - 1);
    key(&input, AX_KEY_ESCAPE);
    ax_input_focus(&input, false);
    CHECK(!ax_input_down(&input, AX_KEY_W));
    CHECK(ax_input_released(&input, AX_KEY_W));
    CHECK(ax_command_register(&commands, "core.help", command, &state));
    CHECK(ax_command_register(&commands, "renderer.nodes", command, &state));
    key(&input, AX_KEY_SLASH);
    CHECK(console.suggestion_count == 3 && console.selected_suggestion == 0);
    CHECK(!strcmp(console.suggestions[0]->name, "core.help"));
    key(&input, AX_KEY_DOWN);
    CHECK(console.selected_suggestion == 1 && !strcmp(console.line, "/"));
    key(&input, AX_KEY_DOWN);
    CHECK(console.selected_suggestion == 2);
    key(&input, AX_KEY_DOWN);
    CHECK(console.selected_suggestion == 0);
    key(&input, AX_KEY_UP);
    CHECK(console.selected_suggestion == 2);
    int calls = state.calls;
    key(&input, AX_KEY_ENTER);
    CHECK(console.open && !strcmp(console.line, "/renderer.nodes ") && console.suggestion_count == 0);
    CHECK(state.calls == calls);
    key(&input, AX_KEY_ENTER);
    CHECK(!console.open && state.calls == calls + 1);
    key(&input, AX_KEY_SLASH);
    type(&input, "game.pl");
    CHECK(console.suggestion_count == 1 && !strcmp(console.suggestions[0]->name, "game.player.name"));
    key(&input, AX_KEY_TAB);
    CHECK(!strcmp(console.line, "/game.player.name ") && console.suggestion_count == 0);
    type(&input, "retained values");
    while (console.cursor > 8) key(&input, AX_KEY_LEFT);
    CHECK(console.suggestion_count == 1);
    key(&input, AX_KEY_TAB);
    CHECK(!strcmp(console.line, "/game.player.name retained values"));
    key(&input, AX_KEY_ESCAPE);
    key(&input, AX_KEY_SLASH);
    type(&input, "nonexistent");
    CHECK(console.suggestion_count == 0);
    for (int i = 0; i < 11; ++i) key(&input, AX_KEY_BACKSPACE);
    CHECK(!strcmp(console.line, "/") && console.suggestion_count == 3);
    ax_input_key(&input, AX_KEY_TAB, true, false, AX_MOD_SHIFT);
    ax_input_key(&input, AX_KEY_TAB, false, false, 0);
    CHECK(console.selected_suggestion == 2);
    key(&input, AX_KEY_ESCAPE);
    key(&input, AX_KEY_SLASH);
    type(&input, "core.help");
    calls = state.calls;
    key(&input, AX_KEY_ENTER);
    CHECK(!console.open && state.calls == calls + 1);
        size_t old_count = console.output_count;
    ax_console_destroy(&console);
    type(&input, "/");
    CHECK(!console.open && !input.captured && old_count > 0);
    return EXIT_SUCCESS;
}
