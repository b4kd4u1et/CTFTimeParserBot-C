#include "event.h"

#include <stdlib.h>
#include <string.h>

void ctf_event_init(ctf_event_t *ev)
{
    memset(ev, 0, sizeof(*ev));
}

void ctf_event_free(ctf_event_t *ev)
{
    if (!ev) {
        return;
    }
    free(ev->description);
    ev->description = NULL;
}

void ctf_event_array_free(ctf_event_t *events, size_t count)
{
    if (!events) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        ctf_event_free(&events[i]);
    }
    free(events);
}
