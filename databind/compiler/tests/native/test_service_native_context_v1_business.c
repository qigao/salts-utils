#include "native_context_v1.service_native.h"

#include <stdint.h>

typedef struct native_counter_receiver {
    uint64_t total;
    unsigned owned_calls;
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

/*
 * The exact same canonical NativeSourceIR/context-v1 FunctionAbi supports
 * owning records in ordinary Native execution. Orbit's current u64
 * CallPort does NOT yet admit this shape; no pseudo-managed Orbit adapter.
 *
 * The application clones into caller-owned response storage BEFORE
 * publishing business state. This neither aliases the request nor moves
 * its ownership behind the caller's back.
 */
int databind_9_RoomState_7_Counter_4_Echo(
    void *receiver, const OwnedPacket *request, OwnedPacket *response) {
    native_counter_receiver *self = (native_counter_receiver *)receiver;
    if (self == NULL || request == NULL || response == NULL ||
        request == response)
        return 17;
    if (request->label.size > 64u || request->payload.size > 64u ||
        (request->label.size != 0u && request->label.data == NULL) ||
        (request->payload.size != 0u && request->payload.data == NULL) ||
        self->total > UINT64_MAX - request->payload.size)
        return 23;
    if (OwnedPacket_clone(response, request) != 0)
        return 24;
    self->total += (uint64_t)request->payload.size;
    ++self->owned_calls;
    return 0;
}
