#pragma once
#include "core/command.h"
#include "core/input.h"
#define AX_CONSOLE_LINES 32
#define AX_CONSOLE_HISTORY 32
typedef struct {
    AxCommands *commands;
    AxInput *input;
    AxEvents *events;
    uint64_t key_subscription, text_subscription;
    bool open, opening_key_text;
    char line[AX_COMMAND_TEXT], draft[AX_COMMAND_TEXT];
    size_t cursor, length, history_count, history_position;
    char history[AX_CONSOLE_HISTORY][AX_COMMAND_TEXT];
    char output[AX_CONSOLE_LINES][AX_COMMAND_TEXT];
    size_t output_count;
    uint64_t output_revision;
    const AxCommand *suggestions[AX_COMMAND_LIMIT];
    size_t suggestion_count, selected_suggestion;
} AxConsole;
/** Subscribes keyboard/text handlers; all console operations are owner-thread only. */
bool ax_console_init(AxConsole *console, AxCommands *commands, AxInput *input, AxEvents *events);
/** Removes event subscriptions and releases input capture. */
void ax_console_destroy(AxConsole *console);
/** Appends bounded output, dropping the oldest line when full. */
void ax_console_print(AxConsole *console, const char *text);
/** Opens/closes console and updates UI capture. Opening starts a slash command. */
void ax_console_open(AxConsole *console, bool open);
