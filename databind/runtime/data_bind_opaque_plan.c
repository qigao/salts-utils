#include "data_bind_opaque_plan.h"

#include <stdio.h>
#include <string.h>

static size_t opaque_out_size(size_t requested, size_t full) {
  return requested != 0u && requested < full ? requested : full;
}

static DataBindStatus opaque_fail(
    DataBindError *error, DataBindStatus status, const char *message) {
  size_t size;
  if (error == NULL) return status;
  size = opaque_out_size(error->size, sizeof(*error));
  memset(error, 0, size);
  if (size >= sizeof(size_t)) error->size = size;
  if (size >= offsetof(DataBindError, code) + sizeof(error->code))
    error->code = status;
  if (size >= offsetof(DataBindError, line) + sizeof(error->line))
    error->line = -1;
  if (size >= offsetof(DataBindError, column) + sizeof(error->column))
    error->column = -1;
  if (size >= offsetof(DataBindError, message) + sizeof(error->message))
    snprintf(error->message, sizeof(error->message), "%s",
             message != NULL ? message : "Opaque bytes plan failed");
  return status;
}

static void opaque_clear_error(DataBindError *error) {
  if (error == NULL) return;
  (void)opaque_fail(error, DATA_BIND_OK, "");
}

static uint32_t opaque_state_flag(DataBindOpaqueState state) {
  switch (state) {
  case DATA_BIND_OPAQUE_VALUE:
    return DATA_BIND_OPAQUE_STATE_VALUE;
  case DATA_BIND_OPAQUE_ABSENT:
    return DATA_BIND_OPAQUE_STATE_ABSENT;
  case DATA_BIND_OPAQUE_NULL:
    return DATA_BIND_OPAQUE_STATE_NULL;
  default:
    return 0u;
  }
}

DataBindStatus data_bind_opaque_plan_validate(
    const DataBindOpaquePlan *plan, DataBindError *error) {
  const uint32_t admitted =
      DATA_BIND_OPAQUE_STATE_VALUE |
      DATA_BIND_OPAQUE_STATE_ABSENT |
      DATA_BIND_OPAQUE_STATE_NULL;

  if (plan == NULL ||
      plan->size < sizeof(*plan) ||
      plan->abi_version != DATA_BIND_OPAQUE_PLAN_ABI_VERSION ||
      plan->logical_type == NULL ||
      strcmp(plan->logical_type, "bytes") != 0 ||
      plan->max_bytes == 0u ||
      (plan->value_states & DATA_BIND_OPAQUE_STATE_VALUE) == 0u ||
      (plan->value_states & ~admitted) != 0u)
    return opaque_fail(
        error, DATA_BIND_ERR_SCHEMA,
        "Opaque plan must describe bounded canonical bytes");

  opaque_clear_error(error);
  return DATA_BIND_OK;
}

static DataBindStatus opaque_admit(
    const DataBindOpaquePlan *plan,
    DataBindOpaqueState state,
    const void *data,
    size_t bytes,
    DataBindError *error) {
  const uint32_t flag = opaque_state_flag(state);
  DataBindStatus status = data_bind_opaque_plan_validate(plan, error);
  if (status != DATA_BIND_OK) return status;

  if (flag == 0u || (plan->value_states & flag) == 0u)
    return opaque_fail(
        error, DATA_BIND_ERR_TYPE_MISMATCH,
        "Opaque logical state is not admitted by the compiled plan");

  if (state != DATA_BIND_OPAQUE_VALUE) {
    if (data != NULL || bytes != 0u)
      return opaque_fail(
          error, DATA_BIND_ERR_INVALID_ARG,
          "Opaque ABSENT/NULL state cannot carry payload bytes");
    return DATA_BIND_OK;
  }

  if (bytes > plan->max_bytes)
    return opaque_fail(
        error, DATA_BIND_ERR_LIMIT,
        "Opaque payload exceeds the compiled byte bound");
  if (bytes != 0u && data == NULL)
    return opaque_fail(
        error, DATA_BIND_ERR_INVALID_ARG,
        "Opaque VALUE payload bytes are missing");

  return DATA_BIND_OK;
}

static void opaque_publish(
    DataBindOpaqueSpan *out,
    DataBindOpaqueState state,
    const void *data,
    size_t bytes,
    DataBindOpaqueOwnership ownership) {
  DataBindOpaqueSpan full = {
      sizeof(DataBindOpaqueSpan),
      DATA_BIND_OPAQUE_SPAN_ABI_VERSION,
      state,
      (const unsigned char *)data,
      bytes,
      ownership};
  size_t size = opaque_out_size(out->size, sizeof(*out));
  memset(out, 0, size);
  memcpy(out, &full, size);
  out->size = size;
}

DataBindStatus data_bind_opaque_plan_borrow(
    const DataBindOpaquePlan *plan,
    DataBindOpaqueState state,
    const void *data,
    size_t bytes,
    DataBindOpaqueSpan *out,
    DataBindError *error) {
  DataBindStatus status;
  if (out == NULL || out->size < sizeof(size_t))
    return opaque_fail(
        error, DATA_BIND_ERR_INVALID_ARG,
        "Opaque borrowed span output is invalid");

  status = opaque_admit(plan, state, data, bytes, error);
  if (status != DATA_BIND_OK) return status;

  opaque_publish(
      out, state,
      state == DATA_BIND_OPAQUE_VALUE ? data : NULL,
      state == DATA_BIND_OPAQUE_VALUE ? bytes : 0u,
      DATA_BIND_OPAQUE_BORROWED);
  opaque_clear_error(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_opaque_plan_copy(
    const DataBindOpaquePlan *plan,
    DataBindOpaqueState state,
    const void *data,
    size_t bytes,
    void *destination,
    size_t destination_bytes,
    DataBindOpaqueSpan *out,
    DataBindError *error) {
  DataBindStatus status;
  if (out == NULL || out->size < sizeof(size_t))
    return opaque_fail(
        error, DATA_BIND_ERR_INVALID_ARG,
        "Opaque copied span output is invalid");

  status = opaque_admit(plan, state, data, bytes, error);
  if (status != DATA_BIND_OK) return status;

  if (state == DATA_BIND_OPAQUE_VALUE) {
    if (bytes > destination_bytes ||
        (bytes != 0u && destination == NULL))
      return opaque_fail(
          error, DATA_BIND_ERR_LIMIT,
          "Opaque destination cannot hold the bounded payload");
    if (bytes != 0u) memcpy(destination, data, bytes);
  }

  opaque_publish(
      out, state,
      state == DATA_BIND_OPAQUE_VALUE && bytes != 0u ? destination : NULL,
      state == DATA_BIND_OPAQUE_VALUE ? bytes : 0u,
      DATA_BIND_OPAQUE_CALLER_OWNED);
  opaque_clear_error(error);
  return DATA_BIND_OK;
}
