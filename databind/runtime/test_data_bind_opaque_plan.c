#include "data_bind_opaque_plan.h"
#include "tinytest.h"

#include <string.h>

spec("DataBind opaque bytes pass-through plan") {
  it("publishes bounded borrowed and caller-owned VALUE spans") {
    static const unsigned char source[] = {0x00u, 0x7fu, 0xffu};
    unsigned char destination[3] = {0};
    DataBindOpaquePlan plan = DATA_BIND_OPAQUE_PLAN_INIT;
    DataBindOpaqueSpan span = DATA_BIND_OPAQUE_SPAN_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;

    plan.max_bytes = sizeof(source);
    check_equal(data_bind_opaque_plan_validate(&plan, &error), DATA_BIND_OK);

    check_equal(
        data_bind_opaque_plan_borrow(
            &plan, DATA_BIND_OPAQUE_VALUE,
            source, sizeof(source), &span, &error),
        DATA_BIND_OK);
    check_equal(span.state, DATA_BIND_OPAQUE_VALUE);
    check_equal(span.ownership, DATA_BIND_OPAQUE_BORROWED);
    check_equal(span.data, source);
    check_equal(span.bytes, sizeof(source));

    span = (DataBindOpaqueSpan)DATA_BIND_OPAQUE_SPAN_INIT;
    check_equal(
        data_bind_opaque_plan_copy(
            &plan, DATA_BIND_OPAQUE_VALUE,
            source, sizeof(source),
            destination, sizeof(destination), &span, &error),
        DATA_BIND_OK);
    check_equal(span.ownership, DATA_BIND_OPAQUE_CALLER_OWNED);
    check_equal(span.data, destination);
    check_equal(span.bytes, sizeof(source));
    check_equal(memcmp(destination, source, sizeof(source)), 0);
  }

  it("fails closed on bounds and does not publish partial copies") {
    static const unsigned char source[] = {1u, 2u, 3u};
    unsigned char destination[3] = {0xa5u, 0xa5u, 0xa5u};
    DataBindOpaquePlan plan = DATA_BIND_OPAQUE_PLAN_INIT;
    DataBindOpaqueSpan span = DATA_BIND_OPAQUE_SPAN_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;

    plan.max_bytes = 2u;
    check_equal(
        data_bind_opaque_plan_borrow(
            &plan, DATA_BIND_OPAQUE_VALUE,
            source, sizeof(source), &span, &error),
        DATA_BIND_ERR_LIMIT);

    plan.max_bytes = sizeof(source);
    check_equal(
        data_bind_opaque_plan_copy(
            &plan, DATA_BIND_OPAQUE_VALUE,
            source, sizeof(source),
            destination, sizeof(destination) - 1u, &span, &error),
        DATA_BIND_ERR_LIMIT);
    check_equal(destination[0], (unsigned char)0xa5u);
    check_equal(destination[1], (unsigned char)0xa5u);
    check_equal(destination[2], (unsigned char)0xa5u);
  }

  it("rejects ABSENT and NULL unless explicitly admitted") {
    DataBindOpaquePlan plan = DATA_BIND_OPAQUE_PLAN_INIT;
    DataBindOpaqueSpan span = DATA_BIND_OPAQUE_SPAN_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;

    plan.max_bytes = 8u;
    check_equal(
        data_bind_opaque_plan_borrow(
            &plan, DATA_BIND_OPAQUE_ABSENT,
            NULL, 0u, &span, &error),
        DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(
        data_bind_opaque_plan_borrow(
            &plan, DATA_BIND_OPAQUE_NULL,
            NULL, 0u, &span, &error),
        DATA_BIND_ERR_TYPE_MISMATCH);

    plan.value_states |=
        DATA_BIND_OPAQUE_STATE_ABSENT | DATA_BIND_OPAQUE_STATE_NULL;
    check_equal(
        data_bind_opaque_plan_borrow(
            &plan, DATA_BIND_OPAQUE_ABSENT,
            NULL, 0u, &span, &error),
        DATA_BIND_OK);
    check_equal(span.state, DATA_BIND_OPAQUE_ABSENT);
    check_null(span.data);
    check_equal(span.bytes, (size_t)0u);

    span = (DataBindOpaqueSpan)DATA_BIND_OPAQUE_SPAN_INIT;
    check_equal(
        data_bind_opaque_plan_borrow(
            &plan, DATA_BIND_OPAQUE_NULL,
            NULL, 0u, &span, &error),
        DATA_BIND_OK);
    check_equal(span.state, DATA_BIND_OPAQUE_NULL);
  }

  it("never admits a structured/native logical type as opaque bytes") {
    DataBindOpaquePlan plan = DATA_BIND_OPAQUE_PLAN_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;

    plan.logical_type = "TelemetryEvent";
    plan.max_bytes = 64u;
    check_equal(
        data_bind_opaque_plan_validate(&plan, &error),
        DATA_BIND_ERR_SCHEMA);
  }
}
