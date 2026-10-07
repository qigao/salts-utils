#include "opaque_bytes.socket.h"
#include "opaque_bytes.flowmq.h"

#include <data_bind_opaque_plan.h>
#include <tinytest.h>

#include <string.h>

spec("generated opaque bytes transport composition") {
  it("shares one bounded OpaquePlan across Socket and FlowMQ") {
    static const unsigned char payload[] = {0x00u, 0x7fu, 0xffu};
    unsigned char copy[sizeof(payload)] = {0};
    DataBindOpaqueSpan span = DATA_BIND_OPAQUE_SPAN_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    const DataBindOpaquePlan *plan =
        databind_opaque_bytes_databind_opaque_plan();

    check_not_null(plan);
    check_equal(databind_opaque_bytes_socket_plan.message_type, "bytes");
    check_equal(
        databind_opaque_bytes_flowmq_channel_plan.message_type, "bytes");

    check_equal(
        databind_opaque_bytes_socket_plan.payload_kind,
        DATA_BIND_PAYLOAD_OPAQUE);
    check_equal(
        databind_opaque_bytes_flowmq_channel_plan.payload_kind,
        DATA_BIND_PAYLOAD_OPAQUE);
    check_equal(
        databind_opaque_bytes_socket_plan.format,
        DATA_BIND_FORMAT_NONE);
    check_equal(
        databind_opaque_bytes_flowmq_channel_plan.format,
        DATA_BIND_FORMAT_NONE);
    check_null(databind_opaque_bytes_socket_plan.native_binding);
    check_null(databind_opaque_bytes_flowmq_channel_plan.native_binding);

    check_true(databind_opaque_bytes_socket_plan.opaque_plan == plan);
    check_true(databind_opaque_bytes_flowmq_channel_plan.opaque_plan == plan);
    check_equal(plan->logical_type, "bytes");
    check_equal(plan->max_bytes, (size_t)64u);
    check_equal(
        plan->value_states, (uint32_t)DATA_BIND_OPAQUE_STATE_VALUE);
    check_equal(
        databind_opaque_bytes_socket_plan.max_frame_bytes, (size_t)128u);
    check_equal(
        databind_opaque_bytes_flowmq_channel_plan.max_payload_bytes,
        (size_t)96u);

    check_equal(
        data_bind_opaque_plan_borrow(
            plan, DATA_BIND_OPAQUE_VALUE,
            payload, sizeof(payload), &span, &error),
        DATA_BIND_OK);
    check_true(span.data == payload);
    check_equal(span.bytes, sizeof(payload));
    check_equal(span.ownership, DATA_BIND_OPAQUE_BORROWED);

    span = (DataBindOpaqueSpan)DATA_BIND_OPAQUE_SPAN_INIT;
    check_equal(
        data_bind_opaque_plan_copy(
            plan, DATA_BIND_OPAQUE_VALUE,
            payload, sizeof(payload),
            copy, sizeof(copy), &span, &error),
        DATA_BIND_OK);
    check_true(span.data == copy);
    check_equal(span.ownership, DATA_BIND_OPAQUE_CALLER_OWNED);
    check_equal(memcmp(copy, payload, sizeof(payload)), 0);

    span = (DataBindOpaqueSpan)DATA_BIND_OPAQUE_SPAN_INIT;
    check_equal(
        data_bind_opaque_plan_borrow(
            plan, DATA_BIND_OPAQUE_NULL,
            NULL, 0u, &span, &error),
        DATA_BIND_ERR_TYPE_MISMATCH);
  }
}
