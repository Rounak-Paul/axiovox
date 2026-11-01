#include "command.h"
#include <string.h>
#include <ctype.h>
/** Validates nonempty dot-separated identifier segments. */
static bool valid_name(const char *name)
{
    bool start = true;
    if (!name || !*name) return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p) {
        if (*p == '.') { if (start) return false; start = true; }
        else {
            if (!(isalpha(*p) || *p == '_' || (!start && isdigit(*p)))) return false;
            start = false;
        }
    }
    return !start;
}
bool ax_command_register(AxCommands *commands, const char *name, AxCommandFn fn, void *user)
{
    if (!commands || !valid_name(name) || !fn || commands->count == AX_COMMAND_LIMIT) return false;
    for (size_t i = 0; i < commands->count; ++i) if (!strcmp(name, commands->entries[i].name)) return false;
    commands->entries[commands->count++] = (AxCommand){name, fn, user};
    return true;
}
bool ax_command_invoke(AxCommands *commands, int argc, const char *const *argv)
{
    if (!commands || !argv || argc < 1 || argc > AX_COMMAND_ARGUMENTS) return false;
    for (int i = 0; i < argc; ++i) if (!argv[i]) return false;
    for (size_t i = 0; i < commands->count; ++i) if (!strcmp(argv[0], commands->entries[i].name)) return commands->entries[i].fn(argc, argv, commands->entries[i].user);
    return false;
}
bool ax_command_execute(AxCommands *commands, const char *line)
{
    if (!commands || !line || strlen(line) >= AX_COMMAND_TEXT) return false;
    char buffer[AX_COMMAND_TEXT];
    const char *argv[AX_COMMAND_ARGUMENTS];
    int argc = 0;
    char *out = buffer;
    const char *p = line;
    while (isspace((unsigned char)*p)) ++p;
    if (*p == '/') ++p;
    while (*p) {
        while (isspace((unsigned char)*p)) ++p;
        if (!*p) break;
        if (argc == AX_COMMAND_ARGUMENTS) return false;
        argv[argc++] = out;
        char quote = 0;
        while (*p && (quote || !isspace((unsigned char)*p))) {
            char c = *p++;
            if (c == '\\') { if (!*p) return false; *out++ = *p++; }
            else if (c == '"' || c == '\'') {
                if (!quote) quote = c;
                else if (quote == c) quote = 0;
                else *out++ = c;
            } else *out++ = c;
        }
        if (quote) return false;
        *out++ = 0;
    }
    return ax_command_invoke(commands, argc, argv);
}
