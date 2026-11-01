#include "log.h"
#include <stdatomic.h>
#include <stdio.h>
static atomic_int threshold = AX_LOG_INFO;
static atomic_flag lock = ATOMIC_FLAG_INIT;
void ax_log_set_level(AxLogLevel level)
{
    if (level >= AX_LOG_DEBUG && level <= AX_LOG_ERROR) atomic_store(&threshold, level);
}
void ax_log(AxLogLevel level, const char *format, ...)
{
    static const char *names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    if (!format || level < AX_LOG_DEBUG || level > AX_LOG_ERROR || (int)level < atomic_load(&threshold)) return;
    while (atomic_flag_test_and_set(&lock)) { }
    fprintf(stderr, "[%s] ", names[level]);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    atomic_flag_clear(&lock);
}
