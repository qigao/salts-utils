#include "native_context_v1.service_native.h"

#include <stdint.h>

typedef struct native_counter_receiver {
    uint64_t total;
} native_counter_receiver;

/*
 * One canonical generated Native business symbol and one versioned receiver
 * ABI. No global/TLS state, reflection fallback or test-only invoke engine.
 */
int databind_9_RoomState_7_Counter_5_Apply(
    void *receiver, const Delta *request, Total *response) {
    native_counter_receiver *self = (native_counter_receiver *)receiver;
    if (self == NULL || request == NULL || response == NULL)
        return 17;
    self->total += request->delta;
    response->total = self->total;
    return 0;
}
