#include "tinytest.h"
#include "native_context_v1.service_native.h"

#include <stdint.h>

typedef struct native_counter_receiver {
    uint64_t total;
} native_counter_receiver;

spec("Canonical Native Service context v1") {
  it("binds two independent receivers through one immutable FunctionAbi") {
    native_counter_receiver a = {UINT64_C(5)};
    native_counter_receiver b = {UINT64_C(100)};
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
}
