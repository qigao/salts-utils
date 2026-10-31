#include "tinytest.h"
#include "native_context_v1.service_native.h"

#include <stdint.h>
#include <string.h>

typedef struct native_counter_receiver {
    uint64_t total;
    unsigned owned_calls;
} native_counter_receiver;

spec("Canonical Native Service context v1") {
  it("binds two independent receivers through one immutable FunctionAbi") {
    native_counter_receiver a = {UINT64_C(5), 0u};
    native_counter_receiver b = {UINT64_C(100), 0u};
    Delta request = {UINT64_C(2)};
    Total response_a = {0};
    Total response_b = {0};
    void *params_a[2] = {&request, &response_a};
    void *params_b[2] = {&request, &response_b};
    DataBindNativeExecution exec_a = {0};
    DataBindNativeExecution exec_b = {0};
    int status = -1;
    const DataBindNativeExecution *shared =
        databind_9_RoomState_7_Counter_5_Apply__databind_execution();

    check_not_null(shared);
    check_true(data_bind_native_execution_valid(shared));
    check_true(shared->context == NULL);
    check_true(!databind_9_RoomState_7_Counter_5_Apply__databind_execution_bind(
        NULL, &exec_a));
    check_true(!databind_9_RoomState_7_Counter_5_Apply__databind_execution_bind(
        &a, NULL));
    check_true(databind_9_RoomState_7_Counter_5_Apply__databind_execution_bind(
        &a, &exec_a));
    check_true(databind_9_RoomState_7_Counter_5_Apply__databind_execution_bind(
        &b, &exec_b));

    check_true(exec_a.context == &a);
    check_true(exec_b.context == &b);
    check_true(exec_a.invoke == exec_b.invoke);
    check_true(shared->context == NULL);

    /* An unbound original descriptor is NEVER an implicit default room. */
    check_true(!shared->invoke(shared->context, &status, params_a, 2u));
    check_true(a.total == UINT64_C(5));
    check_true(b.total == UINT64_C(100));

    status = -1;
    check_true(exec_a.invoke(exec_a.context, &status, params_a, 2u));
    check_equal(status, 0);
    check_true(response_a.total == UINT64_C(7));
    check_true(a.total == UINT64_C(7));
    check_true(b.total == UINT64_C(100));

    status = -1;
    check_true(exec_b.invoke(exec_b.context, &status, params_b, 2u));
    check_equal(status, 0);
    check_true(response_b.total == UINT64_C(102));
    check_true(b.total == UINT64_C(102));
    check_true(a.total == UINT64_C(7));

    request.delta = UINT64_C(3);
    check_true(exec_a.invoke(exec_a.context, &status, params_a, 2u));
    check_equal(status, 0);
    check_true(response_a.total == UINT64_C(10));
    check_true(a.total == UINT64_C(10));
    check_true(b.total == UINT64_C(102));

    /* N->N+1/context rebind never mutates the original FunctionAbi. */
    check_true(shared->context == NULL);
  }

  it("deep-copies owned text and bytes for two canonical contextual receivers") {
    native_counter_receiver a = {UINT64_C(5), 0u};
    native_counter_receiver b = {UINT64_C(100), 0u};
    char scratch_label[] = "Room-A";
    unsigned char scratch_bytes[] = {0u, 0xFFu, 0x42u, 0x80u};
    OwnedPacket borrowed = {
        {scratch_label, sizeof(scratch_label) - 1u},
        {scratch_bytes, sizeof(scratch_bytes)}
    };
    OwnedPacket request = {0};
    OwnedPacket response_a = {0};
    OwnedPacket response_b = {0};
    OwnedPacket snapshot = {0};
    OwnedPacket moved = {0};
    OwnedPacket invalid = {0};
    OwnedPacket_native_cmeta_binding record_binding = {0};
    DataBindNativeExecution exec_a = {0};
    DataBindNativeExecution exec_b = {0};
    void *params_a[2] = {&request, &response_a};
    void *params_b[2] = {&request, &response_b};
    const DataBindNativeExecution *shared =
        databind_9_RoomState_7_Counter_4_Echo__databind_execution();
    int status = -1;

    check_not_null(shared);
    check_true(data_bind_native_execution_valid(shared));
    check_true(shared->context == NULL);
    check_equal(OwnedPacket_native_cmeta_value_bind(&record_binding), 0);
    check_true(cmeta_data_value_traits_supported(&record_binding.data));
    check_true(!databind_9_RoomState_7_Counter_4_Echo__databind_execution_bind(
        NULL, &exec_a));
    check_true(databind_9_RoomState_7_Counter_4_Echo__databind_execution_bind(
        &a, &exec_a));
    check_true(databind_9_RoomState_7_Counter_4_Echo__databind_execution_bind(
        &b, &exec_b));
    check_true(exec_a.context == &a && exec_b.context == &b);
    check_true(exec_a.invoke == exec_b.invoke);

    /* Clone from a borrowed stack view, never free the caller's storage. */
    check_equal(OwnedPacket_clone(&request, &borrowed), 0);
    check_true(request.label.data != scratch_label);
    check_true(request.payload.data != scratch_bytes);
    scratch_label[0] = 'X';
    scratch_bytes[1] = 0x11u;
    check_equal(memcmp(request.label.data, "Room-A", 6u), 0);
    check_equal(request.payload.data[1], 0xFFu);

    /* Both instances use one immutable FunctionAbi and one method symbol. */
    check_true(!shared->invoke(shared->context, &status, params_a, 2u));
    check_equal(a.owned_calls, 0u);
    check_equal(b.owned_calls, 0u);
    check_true(exec_a.invoke(exec_a.context, &status, params_a, 2u));
    check_equal(status, 0);
    check_true(exec_b.invoke(exec_b.context, &status, params_b, 2u));
    check_equal(status, 0);
    check_equal(a.total, UINT64_C(9));
    check_equal(b.total, UINT64_C(104));
    check_equal(a.owned_calls, 1u);
    check_equal(b.owned_calls, 1u);
    check_true(request.label.data != response_a.label.data);
    check_true(response_a.label.data != response_b.label.data);
    check_true(request.payload.data != response_a.payload.data);
    check_true(response_a.payload.data != response_b.payload.data);
    check_equal(memcmp(response_a.label.data, "Room-A", 6u), 0);
    check_equal(memcmp(response_b.payload.data, request.payload.data,
                       sizeof(scratch_bytes)), 0);

    check_equal(OwnedPacket_clone(&snapshot, &response_a), 0);
    OwnedPacket_clear(&request);
    check_true(request.label.data == NULL && request.payload.data == NULL);
    check_equal(memcmp(response_a.label.data, "Room-A", 6u), 0);
    check_equal(memcmp(snapshot.payload.data, response_b.payload.data,
                       sizeof(scratch_bytes)), 0);

    /* Invalid managed input fails without clearing an already owned result.
     * An application error leaves the receiver and response unchanged.
     */
    invalid.label.size = 3u;
    check_true(OwnedPacket_clone(&response_a, &invalid) != 0);
    check_equal(memcmp(response_a.label.data, "Room-A", 6u), 0);
    {
      void *bad_params[2] = {&invalid, &snapshot};
      status = -1;
      check_true(exec_a.invoke(exec_a.context, &status, bad_params, 2u));
      check_equal(status, 23);
      check_equal(a.total, UINT64_C(9));
      check_equal(a.owned_calls, 1u);
    }
    check_equal(OwnedPacket_move(&moved, &response_b), 0);
    check_true(response_b.label.data == NULL);
    check_true(response_b.payload.data == NULL);
    check_equal(memcmp(moved.label.data, "Room-A", 6u), 0);
    OwnedPacket_clear(&moved);
    OwnedPacket_clear(&moved);
    OwnedPacket_clear(&snapshot);
    OwnedPacket_clear(&response_b);
    OwnedPacket_clear(&response_a);
    check_true(shared->context == NULL);
  }
}
