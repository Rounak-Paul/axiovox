#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "files.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <wchar.h>
/** Converts a bounded UTF-8 path to an allocated Windows path. */
static wchar_t *wide_path(const char *path)
{
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (!count) return NULL;
    wchar_t *result = malloc((size_t)count * sizeof(*result));
    if (result && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, result, count)) { free(result); return NULL; }
    return result;
}
#else
#include <unistd.h>
#endif
FILE *ax_file_open(const char *path, const char *mode)
{
    if (!path || !mode) return NULL;
#ifdef _WIN32
    wchar_t *p = wide_path(path), *m = wide_path(mode);
    FILE *file = p && m ? _wfopen(p, m) : NULL;
    free(p); free(m);
    return file;
#else
    return fopen(path, mode);
#endif
}
bool ax_file_exists(const char *path)
{
    if (!path || !*path) return false;
#ifdef _WIN32
    wchar_t *p = wide_path(path);
    bool exists = p && GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
    free(p); return exists;
#else
    struct stat info;
    return stat(path, &info) == 0;
#endif
}
bool ax_file_write_atomic(const char *path, AxFileWriteFn write, const void *user)
{
    if (!path || !*path || !write || strlen(path) > AX_PATH_CAPACITY - 16) return false;
    char temporary[AX_PATH_CAPACITY];
    snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX", path);
#ifdef _WIN32
    wchar_t *temp = wide_path(temporary), *destination = wide_path(path);
    if (!temp || !destination) { free(temp); free(destination); return false; }
    int descriptor = -1;
    if (_wmktemp_s(temp, wcslen(temp) + 1) || _wsopen_s(&descriptor, temp, _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _SH_DENYRW, _S_IREAD | _S_IWRITE)) {
        free(temp); free(destination); return false;
    }
    FILE *file = _fdopen(descriptor, "wb");
    if (!file) { _close(descriptor); _wremove(temp); free(temp); free(destination); return false; }
#else
    int descriptor = mkstemp(temporary);
    if (descriptor < 0) return false;
    FILE *file = fdopen(descriptor, "wb");
    if (!file) { close(descriptor); remove(temporary); return false; }
#endif
    bool ok = write(file, user);
    if (fflush(file)) ok = false;
#ifdef _WIN32
    if (_commit(descriptor)) ok = false;
#else
    if (fsync(descriptor)) ok = false;
#endif
    if (fclose(file)) ok = false;
#ifdef _WIN32
    if (ok) ok = MoveFileExW(temp, destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) _wremove(temp);
    free(temp); free(destination);
#else
    if (ok) ok = rename(temporary, path) == 0;
    if (!ok) remove(temporary);
#endif
    return ok;
}
/** Creates one directory, accepting an already existing directory but not a file. */
static bool make_directory(const char *path)
{
#ifdef _WIN32
    wchar_t *p = wide_path(path);
    if (!p) return false;
    bool ok = CreateDirectoryW(p, NULL) != 0;
    if (!ok && GetLastError() == ERROR_ALREADY_EXISTS) {
        DWORD attributes = GetFileAttributesW(p);
        ok = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    free(p); return ok;
#else
    if (mkdir(path, 0700) == 0) return true;
    struct stat info;
    return errno == EEXIST && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
#endif
}
bool ax_settings_path(char *path, size_t capacity)
{
    if (!path || !capacity) return false;
    path[0] = 0;
#ifdef _WIN32
    const wchar_t *local = _wgetenv(L"LOCALAPPDATA");
    char base[AX_PATH_CAPACITY];
    if (!local || !WideCharToMultiByte(CP_UTF8, 0, local, -1, base, sizeof(base), NULL, NULL)) return false;
    int length = snprintf(path, capacity, "%s/Axiovox", base);
#elif defined(__APPLE__)
    const char *home = getenv("HOME");
    if (!home || home[0] != '/') return false;
    int length = snprintf(path, capacity, "%s/Library/Application Support/Axiovox", home);
#else
    const char *base = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    int length;
    if (base && base[0] == '/') length = snprintf(path, capacity, "%s/axiovox", base);
    else if (home && home[0] == '/') length = snprintf(path, capacity, "%s/.config/axiovox", home);
    else return false;
#endif
    if (length < 0 || (size_t)length + sizeof("/settings.cfg") > capacity) { path[0] = 0; return false; }
    for (char *p = path + 1; *p; ++p) if (*p == '/' || *p == '\\') {
#ifdef _WIN32
        if (p == path + 2 && path[1] == ':') continue;
#endif
        char separator = *p; *p = 0;
        bool ok = make_directory(path);
        *p = separator;
        if (!ok) { path[0] = 0; return false; }
    }
    if (!make_directory(path)) { path[0] = 0; return false; }
    strcat(path, "/settings.cfg");
    return true;
}
