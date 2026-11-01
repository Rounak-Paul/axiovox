#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum { AX_EVENT_WINDOW_CLOSE, AX_EVENT_WINDOW_RESIZE, AX_EVENT_DEVICE_SELECTED, AX_EVENT_CONFIG_CHANGED, AX_EVENT_KEY, AX_EVENT_TEXT, AX_EVENT_FOCUS } AxEventType;
typedef struct {
    AxEventType type;
    union {
        struct { int width, height; } resize;
        struct { int key, modifiers; bool pressed, repeat; } key;
        struct { uint32_t codepoint; } text;
        struct { bool focused; } focus;
        struct { uint32_t index; const char *name; } device;
        struct { const char *key, *value; bool restart_required; } config;
    } data;
} AxEvent;
typedef void (*AxEventFn)(const AxEvent *event, void *user);
typedef struct { uint64_t id; AxEventType type; AxEventFn fn; void *user; } AxListener;
typedef struct { AxListener listeners[64]; uint64_t next_id; unsigned dispatch_depth; } AxEvents;
/** Registers a listener on the owner thread; returns zero when full or invalid. */
uint64_t ax_event_subscribe(AxEvents *events, AxEventType type, AxEventFn fn, void *user);
/** Removes a subscription on the owner thread; safe during dispatch. */
void ax_event_unsubscribe(AxEvents *events, uint64_t id);
/** Synchronously dispatches borrowed event data on the owner thread. New listeners wait until the next emission. */
void ax_event_emit(AxEvents *events, const AxEvent *event);
