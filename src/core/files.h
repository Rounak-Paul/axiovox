#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#define AX_PATH_CAPACITY 4096
typedef bool (*AxFileWriteFn)(FILE *file, const void *user);
/** Opens a UTF-8 path; mode uses the standard fopen syntax. */
FILE *ax_file_open(const char *path, const char *mode);
/** Writes through a unique sibling temporary file, flushes it, and atomically replaces path on success. */
bool ax_file_write_atomic(const char *path, AxFileWriteFn write, const void *user);
/** Returns whether a UTF-8 file exists without creating it. */
bool ax_file_exists(const char *path);
/** Creates the per-user game config directory and returns its settings.cfg path. */
bool ax_settings_path(char *path, size_t capacity);
