#include "event.h"
uint64_t ax_event_subscribe(AxEvents *events, AxEventType type, AxEventFn fn, void *user)
{
    if (!events || !fn || events->next_id == UINT64_MAX) return 0;
    for (size_t i = 0; i < 64; ++i) if (!events->listeners[i].id) {
        uint64_t id = ++events->next_id;
        events->listeners[i] = (AxListener){id, type, fn, user};
        return id;
    }
    return 0;
}
void ax_event_unsubscribe(AxEvents *events, uint64_t id)
{
    if (!events || !id) return;
    for (size_t i = 0; i < 64; ++i) if (events->listeners[i].id == id) events->listeners[i] = (AxListener){0};
}
void ax_event_emit(AxEvents *events, const AxEvent *event)
{
    if (!events || !event || events->dispatch_depth >= 32) return;
    uint64_t limit = events->next_id;
    ++events->dispatch_depth;
    for (size_t i = 0; i < 64; ++i) {
        AxListener listener = events->listeners[i];
        if (listener.id && listener.id <= limit && listener.type == event->type) listener.fn(event, listener.user);
    }
    --events->dispatch_depth;
}
