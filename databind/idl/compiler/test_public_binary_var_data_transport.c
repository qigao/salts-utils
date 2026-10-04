#include "binary_var.socket.h"
#include "binary_var.flowmq.h"

#include <data_bind_binary_reader.h>
#include <data_bind_format_provider.h>
#include <tinytest.h>

#include <stdint.h>
#include <string.h>

static const DataBindBinaryFieldPlan *var_data_field(
    const DataBindBinaryLayoutPlan *plan, const char *name) {
  size_t i;
  if (plan == NULL || name == NULL) return NULL;
  for (i = 0u; i < plan->field_count; ++i)
    if (plan->fields[i].field_name != NULL &&
        strcmp(plan->fields[i].field_name, name) == 0)
      return &plan->fields[i];
  return NULL;
}

static void var_data_write_u32(
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

static void var_data_set_state(
    unsigned char *wire, size_t offset, unsigned bit, int enabled) {
  unsigned char *byte = wire + offset + bit / 8u;
  const unsigned char mask =
      (unsigned char)(1u << (bit % 8u));
  if (enabled)
    *byte |= mask;
  else
    *byte &= (unsigned char)~mask;
}

static size_t var_data_make_wire(
    const DataBindBinaryLayoutPlan *plan,
    unsigned char *wire, size_t capacity) {
  static const unsigned char source[] = {'c', 'a', 'm'};
  static const unsigned char payload[] = {0x00u, 0x7fu, 0xffu};
  const DataBindBinaryFieldPlan *sequence =
      var_data_field(plan, "sequence");
  const DataBindBinaryFieldPlan *payload_field =
      var_data_field(plan, "payload");
  size_t cursor;

  if (plan == NULL || wire == NULL ||
      sequence == NULL || payload_field == NULL ||
      plan->fixed_block_size > capacity)
    return 0u;

  memset(wire, 0, capacity);
  var_data_write_u32(
      wire + sequence->wire_offset,
      plan->wire_big_endian, UINT32_C(7));
  var_data_set_state(
      wire, plan->presence_offset,
      payload_field->optional_bit, 1);
  var_data_set_state(
      wire, plan->null_offset,
      payload_field->nullable_bit, 0);

  cursor = plan->fixed_block_size;
  if (capacity - cursor < sizeof(uint32_t) + sizeof(source))
    return 0u;
  var_data_write_u32(
      wire + cursor, plan->wire_big_endian,
      (uint32_t)sizeof(source));
  cursor += sizeof(uint32_t);
  memcpy(wire + cursor, source, sizeof(source));
  cursor += sizeof(source);

  if (capacity - cursor < sizeof(uint32_t) + sizeof(payload))
    return 0u;
  var_data_write_u32(
      wire + cursor, plan->wire_big_endian,
      (uint32_t)sizeof(payload));
  cursor += sizeof(uint32_t);
  memcpy(wire + cursor, payload, sizeof(payload));
  cursor += sizeof(payload);
  return cursor;
}

static int var_data_key_is(
    const cserde_token *token, const char *text) {
  const size_t length = strlen(text);
  return token != NULL && token->kind == CSERDE_STRING &&
         token->value.slice.size == length &&
         (length == 0u ||
          memcmp(token->value.slice.data, text, length) == 0);
}

spec("generated Binary VAR_DATA transport composition") {
  it("shares one generated VAR_DATA provider across Socket and FlowMQ") {
    static const unsigned char expected_payload[] =
        {0x00u, 0x7fu, 0xffu};
    const DataBindBinaryLayoutPlan *plan =
        databind_binary_var_binary_TelemetryEvent_databind_binary_layout_plan();
    const DataBindFormatProvider *provider =
        databind_binary_var_binary_TelemetryEvent_databind_binary_provider();
    DataBindFormatReader lease = DATA_BIND_FORMAT_READER_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    unsigned char wire[64] = {0};
    size_t wire_size;
    cserde_token token = {0};

    check_not_null(plan);
    check_not_null(provider);
    if (plan == NULL || provider == NULL) return;

    check_equal(provider->format, DATA_BIND_FORMAT_BINARY);
    check_equal(databind_binary_var_socket_plan.message_type, "TelemetryEvent");
    check_equal(
        databind_binary_var_flowmq_channel_plan.message_type,
        "TelemetryEvent");

    check_not_null(var_data_field(plan, "source"));
    check_not_null(var_data_field(plan, "payload"));
    check_equal(
        var_data_field(plan, "source")->representation,
        (size_t)DATA_BIND_BINARY_REP_VAR_DATA);
    check_equal(
        var_data_field(plan, "payload")->representation,
        (size_t)DATA_BIND_BINARY_REP_VAR_DATA);

    wire_size = var_data_make_wire(plan, wire, sizeof(wire));
    check_greater(wire_size, plan->fixed_block_size);
    if (wire_size <= plan->fixed_block_size) return;

    check_equal(
        data_bind_format_reader_open(
            provider, (const char *)wire, wire_size, 8u,
            &lease, &error),
        DATA_BIND_OK);
    check_not_null(lease.reader);
    if (lease.reader == NULL) return;

    check_equal(cserde_reader_next(lease.reader, &token), CSERDE_OK);
    check_equal(token.kind, CSERDE_MAP_BEGIN);

    check_equal(cserde_reader_next(lease.reader, &token), CSERDE_OK);
    check_true(var_data_key_is(&token, "sequence"));
    check_equal(cserde_reader_next(lease.reader, &token), CSERDE_OK);
    check_equal(token.kind, CSERDE_UINT);
    check_equal(token.value.uint, UINT64_C(7));

    check_equal(cserde_reader_next(lease.reader, &token), CSERDE_OK);
    check_true(var_data_key_is(&token, "source"));
    check_equal(cserde_reader_next(lease.reader, &token), CSERDE_OK);
    check_equal(token.kind, CSERDE_STRING);
    check_equal(token.value.slice.size, (size_t)3u);
    check(memcmp(token.value.slice.data, "cam", 3u) == 0);

    check_equal(cserde_reader_next(lease.reader, &token), CSERDE_OK);
    check_true(var_data_key_is(&token, "payload"));
    check_equal(cserde_reader_next(lease.reader, &token), CSERDE_OK);
    check_equal(token.kind, CSERDE_BYTES);
    check_equal(token.value.slice.size, sizeof(expected_payload));
    check(memcmp(
        token.value.slice.data,
        expected_payload, sizeof(expected_payload)) == 0);

    check_equal(cserde_reader_next(lease.reader, &token), CSERDE_OK);
    check_equal(token.kind, CSERDE_MAP_END);
    check_equal(cserde_reader_next(lease.reader, &token), CSERDE_DONE);
    check_equal(data_bind_format_reader_close(&lease), DATA_BIND_OK);
  }
}
