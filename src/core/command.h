#pragma once
#include <stdbool.h>
#include <stddef.h>
#define AX_COMMAND_LIMIT 64
#define AX_COMMAND_ARGUMENTS 16
#define AX_COMMAND_TEXT 1024
typedef bool (*AxCommandFn)(int argc, const char *const *argv, void *user);
typedef struct { const char *name; AxCommandFn fn; void *user; } AxCommand;
typedef struct { AxCommand entries[AX_COMMAND_LIMIT]; size_t count; } AxCommands;
/** Registers a unique dotted identifier. Name and user data must outlive the registry; owner-thread only. */
bool ax_command_register(AxCommands *commands, const char *name, AxCommandFn fn, void *user);
/** Dispatches token arguments directly, without text parsing; intended for UI and future script adapters. */
bool ax_command_invoke(AxCommands *commands, int argc, const char *const *argv);
/** Parses bounded text with quotes/backslash escapes and an optional leading slash, then invokes it. */
bool ax_command_execute(AxCommands *commands, const char *line);
