#include "binary_exec.socket.h"
#include "binary_exec.flowmq.h"

#include <data_bind_binary_reader.h>
#include <data_bind_socket_execution_plan.h>
#include <tinytest.h>

#include <cmeta/data.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static DataBind *binary_exec_codec(void) {
  static const char schema[] =
      "schema BinaryExec [version(1)];"
      "message Event {"
      " @Min(1) @Max(10) uint32 sequence;"
      " optional nullable uint32 sample default 7;"
      "}"
      "channel Events: Event;";
  DataBind *codec = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;
  check_equal(
      data_bind_create_from_text(
          schema, sizeof(schema) - 1u, &codec, &error),
      DATA_BIND_OK);
  return codec;
}

static const DataBindBinaryReaderFieldPlan *binary_field(
    const DataBindBinaryReaderPlan *plan, const char *name) {
  size_t i;
  if (plan == NULL || name == NULL) return NULL;
  for (i = 0u; i < plan->field_count; ++i)
    if (plan->fields[i].field_name != NULL &&
        strcmp(plan->fields[i].field_name, name) == 0)
      return &plan->fields[i];
  return NULL;
}

static const cmeta_data_field_desc *native_field(
    const DataBindNativeTypeBinding *binding, const char *name) {
  const cmeta_data_struct_shape *shape;
  size_t i;
  if (binding == NULL || binding->data == NULL ||
      binding->data->kind != CMETA_DATA_STRUCT ||
      binding->data->shape == NULL || name == NULL)
    return NULL;
  shape = (const cmeta_data_struct_shape *)binding->data->shape;
  for (i = 0u; i < shape->field_count; ++i)
    if (shape->fields[i].name != NULL &&
        strcmp(shape->fields[i].name, name) == 0)
      return &shape->fields[i];
  return NULL;
}

static void write_u32(
    unsigned char *destination, int big_endian, uint32_t value) {
  if (big_endian) {
    destination[0] = (unsigned char)(value >> 24u);
    destination[1] = (unsigned char)(value >> 16u);
    destination[2] = (unsigned char)(value >> 8u);
    destination[3] = (unsigned char)value;
  } else {
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8u);
    destination[2] = (unsigned char)(value >> 16u);
    destination[3] = (unsigned char)(value >> 24u);
  }
}

static void set_state(
    unsigned char *wire,
    size_t state_offset,
    unsigned bit,
    int enabled) {
  unsigned char *byte = wire + state_offset + bit / 8u;
  unsigned char mask = (unsigned char)(1u << (bit % 8u));
  if (enabled)
    *byte |= mask;
  else
    *byte &= (unsigned char)~mask;
}

static void make_wire(
    const DataBindBinaryReaderPlan *plan,
    unsigned char *wire,
    size_t capacity,
    uint32_t sequence,
    int sample_present,
    int sample_null,
    uint32_t sample) {
  const DataBindBinaryReaderFieldPlan *sequence_field =
      binary_field(plan, "sequence");
  const DataBindBinaryReaderFieldPlan *sample_field =
      binary_field(plan, "sample");

  check_not_null(sequence_field);
  check_not_null(sample_field);
  check(plan->fixed_block_size <= capacity);
  if (sequence_field == NULL || sample_field == NULL ||
      plan->fixed_block_size > capacity)
    return;

  memset(wire, 0, capacity);
  write_u32(
      wire + sequence_field->wire_offset,
      plan->wire_big_endian, sequence);
  write_u32(
      wire + sample_field->wire_offset,
      plan->wire_big_endian, sample);

  set_state(
      wire, plan->presence_offset,
      sample_field->optional_bit, sample_present);
  set_state(
      wire, plan->null_offset,
      sample_field->nullable_bit, sample_null);
}

static uint32_t read_native_u32(
    const unsigned char *storage,
    const cmeta_data_field_desc *field) {
  uint32_t value = 0u;
  if (storage != NULL && field != NULL)
    memcpy(&value, storage + field->offset, sizeof(value));
  return value;
}

static int state_is_set(
    const unsigned char *storage,
    const DataBindNativeStateBinding *state) {
  if (storage == NULL || state == NULL) return 0;
  return (storage[state->byte_offset] &
          (unsigned char)(1u << state->bit)) != 0u;
}

spec("generated Binary provider transport composition") {
  it("shares one provider across Socket and FlowMQ and executes Socket payloads") {
    const DataBindBinaryReaderPlan *reader_plan =
        databind_binary_exec_binary_Event_databind_binary_reader_plan();
    const DataBindFormatProvider *provider =
        databind_binary_exec_binary_Event_databind_binary_provider();
    DataBindNativeTypeBinding binding = {0};
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    DataBindSocketExecutionPlan *execution = NULL;
    DataBindMessagePlanDiagnostic diagnostic =
        DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    const cmeta_data_field_desc *sequence_field;
    const cmeta_data_field_desc *sample_field;
    const DataBindNativeStateBinding *sample_presence = NULL;
    const DataBindNativeStateBinding *sample_null = NULL;
    unsigned char workspace[2048] = {0};
    DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
    union {
      uint64_t align_u64;
      double align_double;
      void *align_pointer;
      unsigned char bytes[256];
    } storage;
    unsigned char wire[64] = {0};
    size_t i;

    check_not_null(reader_plan);
    check_not_null(provider);
    check_equal(provider->format, DATA_BIND_FORMAT_BINARY);
    check_equal(databind_binary_exec_socket_plan.message_type, "Event");
    check_equal(databind_binary_exec_flowmq_channel_plan.message_type, "Event");

    check_equal(
        databind_binary_exec_socket_plan.native_binding(&binding, &error),
        DATA_BIND_OK);
    check_equal(binding.idl_type_name, "Event");
    check_true(binding.data != NULL && binding.data->storage_type != NULL);
    check(binding.data->storage_type->size <= sizeof(storage.bytes));
    check(binding.data->storage_type->align <= _Alignof(storage));
    if (binding.data == NULL || binding.data->storage_type == NULL ||
        binding.data->storage_type->size > sizeof(storage.bytes))
      return;

    sequence_field = native_field(&binding, "sequence");
    sample_field = native_field(&binding, "sample");
    check_not_null(sequence_field);
    check_not_null(sample_field);

    for (i = 0u; i < binding.presence_count; ++i)
      if (binding.presence[i].field_name != NULL &&
          strcmp(binding.presence[i].field_name, "sample") == 0)
        sample_presence = &binding.presence[i];
    for (i = 0u; i < binding.null_count; ++i)
      if (binding.nulls[i].field_name != NULL &&
          strcmp(binding.nulls[i].field_name, "sample") == 0)
        sample_null = &binding.nulls[i];
    check_not_null(sample_presence);
    check_not_null(sample_null);

    codec = binary_exec_codec();
    check_not_null(codec);
    if (codec == NULL) return;
    check_equal(
        data_bind_socket_execution_plan_compile(
            codec, &databind_binary_exec_socket_plan,
            &execution, &error),
        DATA_BIND_OK);
    check_not_null(execution);
    data_bind_free(codec);
    codec = NULL;

    options.workspace = workspace;
    options.workspace_bytes = sizeof(workspace);
    options.max_depth = 16u;
    options.max_items = 64u;
    options.max_owned_bytes = 1024u;

    make_wire(reader_plan, wire, sizeof(wire), 3u, 0, 0, 0u);
    memset(storage.bytes, 0, sizeof(storage.bytes));
    check_equal(
        data_bind_socket_execution_plan_decode_payload(
            execution, provider,
            wire, reader_plan->fixed_block_size, 16u,
            &options, storage.bytes, binding.data->storage_type->size,
            &diagnostic, &error),
        DATA_BIND_OK);
    check_equal(read_native_u32(storage.bytes, sequence_field), (uint32_t)3u);
    check_equal(read_native_u32(storage.bytes, sample_field), (uint32_t)7u);
    check_true(state_is_set(storage.bytes, sample_presence));
    check_false(state_is_set(storage.bytes, sample_null));

    diagnostic =
        (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    make_wire(reader_plan, wire, sizeof(wire), 0u, 0, 0, 0u);
    memset(storage.bytes, 0xA5, sizeof(storage.bytes));
    check_equal(
        data_bind_socket_execution_plan_decode_payload(
            execution, provider,
            wire, reader_plan->fixed_block_size, 16u,
            &options, storage.bytes, binding.data->storage_type->size,
            &diagnostic, &error),
        DATA_BIND_ERR_VALIDATION);
    check_equal(read_native_u32(storage.bytes, sequence_field), (uint32_t)0u);
    check_equal(read_native_u32(storage.bytes, sample_field), (uint32_t)0u);
    check_false(state_is_set(storage.bytes, sample_presence));
    check_false(state_is_set(storage.bytes, sample_null));

    diagnostic =
        (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    make_wire(reader_plan, wire, sizeof(wire), 4u, 1, 1, 99u);
    memset(storage.bytes, 0, sizeof(storage.bytes));
    check_equal(
        data_bind_socket_execution_plan_decode_payload(
            execution, provider,
            wire, reader_plan->fixed_block_size, 16u,
            &options, storage.bytes, binding.data->storage_type->size,
            &diagnostic, &error),
        DATA_BIND_OK);
    check_equal(read_native_u32(storage.bytes, sequence_field), (uint32_t)4u);
    check_equal(read_native_u32(storage.bytes, sample_field), (uint32_t)0u);
    check_true(state_is_set(storage.bytes, sample_presence));
    check_true(state_is_set(storage.bytes, sample_null));

    data_bind_socket_execution_plan_free(execution);
  }
}
