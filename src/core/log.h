#pragma once
#include <stdarg.h>
typedef enum { AX_LOG_DEBUG, AX_LOG_INFO, AX_LOG_WARN, AX_LOG_ERROR } AxLogLevel;
/** Sets the minimum severity printed; safe on any thread. */
void ax_log_set_level(AxLogLevel level);
/** Prints one serialized message to stderr; format follows printf. */
void ax_log(AxLogLevel level, const char *format, ...);
