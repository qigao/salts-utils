#include "data_bind_binary_reader.h"
#include "data_bind_binary_writer.h"
#include "data_bind_binary_wire.h"
#include "tinytest.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const DataBindBinaryFieldPlan BINARY_FIELDS[] = {
    {sizeof(DataBindBinaryFieldPlan), "flag",
     CSERDE_BOOL, 8u, 2u, 1u, 0u, 0u, 0u},
    {sizeof(DataBindBinaryFieldPlan), "delta",
     CSERDE_SINT, 16u, 3u, 2u, 0u, 0u, 0u},
    {sizeof(DataBindBinaryFieldPlan), "count",
     CSERDE_UINT, 32u, 5u, 4u, 0u, 0u, 0u},
    {sizeof(DataBindBinaryFieldPlan), "ratio",
     CSERDE_FLOAT, 32u, 9u, 4u, 0u, 0u, 0u},
    {sizeof(DataBindBinaryFieldPlan), "maybe",
     CSERDE_UINT, 16u, 13u, 2u, 0u, 0u,
     DATA_BIND_BINARY_FIELD_OPTIONAL |
         DATA_BIND_BINARY_FIELD_NULLABLE},
};

static DataBindBinaryLayoutPlan binary_plan(int big_endian) {
  DataBindBinaryLayoutPlan plan = DATA_BIND_BINARY_LAYOUT_PLAN_INIT;
  plan.type_name = "BinaryScalars";
  plan.wire_big_endian = big_endian;
  plan.fixed_block_size = 15u;
  plan.presence_offset = 0u;
  plan.presence_size = 1u;
  plan.null_offset = 1u;
  plan.null_size = 1u;
  plan.fields = BINARY_FIELDS;
  plan.field_count = sizeof(BINARY_FIELDS) / sizeof(BINARY_FIELDS[0]);
  return plan;
}

static void write_payload(
    unsigned char *wire,
    int big_endian,
    int present,
    int is_null) {
  memset(wire, 0, 15u);
  if (present) wire[0] |= 1u;
  if (is_null) wire[1] |= 1u;
  data_bind_binary_wire_write_u8(wire + 2u, big_endian, 2u);
  data_bind_binary_wire_write_i16(wire + 3u, big_endian, (int16_t)-1234);
  data_bind_binary_wire_write_u32(wire + 5u, big_endian, UINT32_C(0x11223344));
  data_bind_binary_wire_write_f32(wire + 9u, big_endian, 1.5f);
  data_bind_binary_wire_write_u16(wire + 13u, big_endian, UINT16_C(513));
}

static int next_token(cserde_reader *reader, cserde_token *out) {
  return cserde_reader_next(reader, out) == CSERDE_OK;
}

static int accept_zero_wire(const void *data, size_t len, void *user) {
  static const unsigned char expected[sizeof(uint32_t)] = {0};
  if (data == NULL || user == NULL || len != sizeof(expected) ||
      memcmp(data, expected, sizeof(expected)) != 0)
    return -1;
  ++*(unsigned *)user;
  return 0;
}

static int token_key_is(const cserde_token *token, const char *text) {
  size_t length = strlen(text);
  return token != NULL && token->kind == CSERDE_STRING &&
         token->value.slice.size == length &&
         (length == 0u ||
          memcmp(token->value.slice.data, text, length) == 0);
}

static void expect_required_prefix(
    cserde_reader *reader,
    int expect_maybe,
    int maybe_null) {
  cserde_token token = {0};

  check_true(next_token(reader, &token));
  check_equal(token.kind, CSERDE_MAP_BEGIN);

  check_true(next_token(reader, &token));
  check_true(token_key_is(&token, "flag"));
  check_true(next_token(reader, &token));
  check_equal(token.kind, CSERDE_BOOL);
  check_true(token.value.boolean);

  check_true(next_token(reader, &token));
  check_true(token_key_is(&token, "delta"));
  check_true(next_token(reader, &token));
  check_equal(token.kind, CSERDE_SINT);
  check_equal(token.value.sint, (int64_t)-1234);

  check_true(next_token(reader, &token));
  check_true(token_key_is(&token, "count"));
  check_true(next_token(reader, &token));
  check_equal(token.kind, CSERDE_UINT);
  check_equal(token.value.uint, UINT64_C(0x11223344));

  check_true(next_token(reader, &token));
  check_true(token_key_is(&token, "ratio"));
  check_true(next_token(reader, &token));
  check_equal(token.kind, CSERDE_FLOAT);
  check(fabs(token.value.floating - 1.5) < 0.000001);

  if (expect_maybe) {
    check_true(next_token(reader, &token));
    check_true(token_key_is(&token, "maybe"));
    check_true(next_token(reader, &token));
    if (maybe_null) {
      check_equal(token.kind, CSERDE_NULL);
    } else {
      check_equal(token.kind, CSERDE_UINT);
      check_equal(token.value.uint, UINT64_C(513));
    }
  }

  check_true(next_token(reader, &token));
  check_equal(token.kind, CSERDE_MAP_END);
  check_equal(cserde_reader_next(reader, &token), CSERDE_DONE);
}

static const DataBindBinaryFieldPlan VAR_DATA_FIELDS[] = {
    {sizeof(DataBindBinaryFieldPlan), "sequence",
     CSERDE_UINT, 32u, 2u, 4u, 0u, 0u, 0u,
     DATA_BIND_BINARY_REP_FIXED, 0u},
    {sizeof(DataBindBinaryFieldPlan), "source",
     CSERDE_STRING, 0u, 0u, 0u, 0u, 0u, 0u,
     DATA_BIND_BINARY_REP_VAR_DATA, 4u},
    {sizeof(DataBindBinaryFieldPlan), "payload",
     CSERDE_BYTES, 0u, 0u, 0u, 0u, 0u,
     DATA_BIND_BINARY_FIELD_OPTIONAL |
         DATA_BIND_BINARY_FIELD_NULLABLE,
     DATA_BIND_BINARY_REP_VAR_DATA, 4u},
};

static DataBindBinaryLayoutPlan var_data_plan(int big_endian) {
  DataBindBinaryLayoutPlan plan = DATA_BIND_BINARY_LAYOUT_PLAN_INIT;
  plan.type_name = "BinaryVarData";
  plan.wire_big_endian = big_endian;
  plan.fixed_block_size = 6u;
  plan.presence_offset = 0u;
  plan.presence_size = 1u;
  plan.null_offset = 1u;
  plan.null_size = 1u;
  plan.fields = VAR_DATA_FIELDS;
  plan.field_count =
      sizeof(VAR_DATA_FIELDS) / sizeof(VAR_DATA_FIELDS[0]);
  return plan;
}

static const DataBindBinaryFieldPlan TAIL_ONLY_FIELDS[] = {
    {sizeof(DataBindBinaryFieldPlan), "text", CSERDE_STRING,
     0u, 0u, 0u, 0u, 0u, 0u, DATA_BIND_BINARY_REP_VAR_DATA, 4u},
};

static DataBindBinaryLayoutPlan tail_only_plan(int big_endian) {
  DataBindBinaryLayoutPlan plan = DATA_BIND_BINARY_LAYOUT_PLAN_INIT;
  plan.type_name = "TailOnly";
  plan.wire_big_endian = big_endian;
  plan.fields = TAIL_ONLY_FIELDS;
  plan.field_count = sizeof(TAIL_ONLY_FIELDS) / sizeof(TAIL_ONLY_FIELDS[0]);
  return plan;
}

static size_t write_var_data_payload(
    unsigned char *wire, size_t capacity, int big_endian,
    int payload_present, int payload_null,
    const void *payload_data, size_t payload_size) {
  static const char source[] = "cam";
  size_t cursor = 6u;
  if (wire == NULL || capacity < 14u) return 0u;
  memset(wire, 0, capacity);
  if (payload_present) wire[0] |= 1u;
  if (payload_null) wire[1] |= 1u;
  data_bind_binary_wire_write_u32(wire + 2u, big_endian, UINT32_C(7));

  if (!data_bind_binary_wire_write_var_data(
          wire + cursor, capacity - cursor, big_endian,
          source, sizeof(source) - 1u))
    return 0u;
  cursor += sizeof(uint32_t) + sizeof(source) - 1u;

  if (!data_bind_binary_wire_write_var_data(
          wire + cursor, capacity - cursor, big_endian,
          payload_present && !payload_null ? payload_data : NULL,
          payload_present && !payload_null ? payload_size : 0u))
    return 0u;
  cursor += sizeof(uint32_t) +
            (payload_present && !payload_null ? payload_size : 0u);
  return cursor;
}

static void expect_var_data_message(
    cserde_reader *reader, int expect_payload, int payload_null,
    const unsigned char *payload_data, size_t payload_size) {
  cserde_token token = {0};

  check_true(next_token(reader, &token));
  check_equal(token.kind, CSERDE_MAP_BEGIN);

  check_true(next_token(reader, &token));
  check_true(token_key_is(&token, "sequence"));
  check_true(next_token(reader, &token));
  check_equal(token.kind, CSERDE_UINT);
  check_equal(token.value.uint, UINT64_C(7));

  check_true(next_token(reader, &token));
  check_true(token_key_is(&token, "source"));
  check_true(next_token(reader, &token));
  check_equal(token.kind, CSERDE_STRING);
  check_equal(token.value.slice.size, (size_t)3u);
  check(memcmp(token.value.slice.data, "cam", 3u) == 0);
  check_equal(token.value.slice.lifetime, CSERDE_VIEW_STABLE);

  if (expect_payload) {
    check_true(next_token(reader, &token));
    check_true(token_key_is(&token, "payload"));
    check_true(next_token(reader, &token));
    if (payload_null) {
      check_equal(token.kind, CSERDE_NULL);
    } else {
      check_equal(token.kind, CSERDE_BYTES);
      check_equal(token.value.slice.size, payload_size);
      if (payload_size != 0u)
        check(memcmp(token.value.slice.data, payload_data, payload_size) == 0);
      check_equal(token.value.slice.lifetime, CSERDE_VIEW_STABLE);
    }
  }

  check_true(next_token(reader, &token));
  check_equal(token.kind, CSERDE_MAP_END);
  check_equal(cserde_reader_next(reader, &token), CSERDE_DONE);
}

spec("DataBind canonical Binary reader") {
  it("borrows exact fixed bytes without endian conversion and rejects malformed metadata") {
    enum { BYTE_EXTENT = 6 };
    static const unsigned char wire[BYTE_EXTENT] = {0u, 1u, 0x80u, 0xffu, 2u, 3u};
    DataBindBinaryFieldPlan field = DATA_BIND_BINARY_FIELD_PLAN_INIT;
    DataBindBinaryLayoutPlan plan = DATA_BIND_BINARY_LAYOUT_PLAN_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    cserde_reader *reader = NULL;
    void *owner = NULL;
    cserde_token token = {0};
    field.field_name = "digest";
    field.token_kind = CSERDE_BYTES;
    field.wire_extent = sizeof(wire);
    plan.type_name = "FixedBytes";
    plan.fixed_block_size = sizeof(wire);
    plan.fields = &field;
    plan.field_count = 1u;
    for (int big_endian = 0; big_endian <= 1; ++big_endian) {
      plan.wire_big_endian = big_endian;
      check_equal(data_bind_binary_reader_open(&plan, wire, sizeof(wire), 0u,
                  &reader, &owner, &error), DATA_BIND_OK);
      check_equal(cserde_reader_next(reader, &token), CSERDE_OK);
      check_equal(token.kind, CSERDE_MAP_BEGIN);
      check_equal(cserde_reader_next(reader, &token), CSERDE_OK);
      check_true(token_key_is(&token, "digest"));
      check_equal(cserde_reader_next(reader, &token), CSERDE_OK);
      check_equal(token.kind, CSERDE_BYTES);
      check_true(token.value.slice.data == wire);
      check_equal(token.value.slice.size, sizeof(wire));
      check_equal(token.value.slice.lifetime, CSERDE_VIEW_STABLE);
      check_equal(token.value.slice.data, wire, sizeof(wire));
      check_equal(cserde_reader_next(reader, &token), CSERDE_OK);
      check_equal(token.kind, CSERDE_MAP_END);
      data_bind_binary_reader_close(reader, owner);
      reader = NULL;
      owner = NULL;
    }
    check_equal(data_bind_binary_reader_open(&plan, wire, sizeof(wire) - 1u, 0u,
                &reader, &owner, &error), DATA_BIND_ERR_PARSE);
    check_null(reader);
    check_null(owner);
    field.scalar_bits = 8u;
    check_equal(data_bind_binary_layout_plan_validate(&plan, &error), DATA_BIND_ERR_SCHEMA);
    field.scalar_bits = 0u;
    field.tail_prefix_bytes = sizeof(uint32_t);
    check_equal(data_bind_binary_layout_plan_validate(&plan, &error), DATA_BIND_ERR_SCHEMA);
    field.tail_prefix_bytes = 0u;
    field.wire_extent = 0u;
    check_equal(data_bind_binary_layout_plan_validate(&plan, &error), DATA_BIND_ERR_SCHEMA);
  }

  it("preflights GROUP strides and active entry states in both wire orders") {
    enum { GROUP_ENTRY_STRIDE = 17u, GROUP_ENTRY_COUNT = 2u,
           GROUP_WIRE_BYTES = 2u + DATA_BIND_BINARY_GROUP_HEADER_SIZE +
                              GROUP_ENTRY_STRIDE * GROUP_ENTRY_COUNT + sizeof(uint32_t) + 3u };
    const DataBindBinaryFieldPlan fields[] = {
        {sizeof(DataBindBinaryFieldPlan), "entries", CSERDE_ARRAY_BEGIN,
         0u, 0u, 0u, 0u, 0u, DATA_BIND_BINARY_FIELD_OPTIONAL | DATA_BIND_BINARY_FIELD_NULLABLE,
         DATA_BIND_BINARY_REP_GROUP, DATA_BIND_BINARY_GROUP_HEADER_SIZE},
        {sizeof(DataBindBinaryFieldPlan), "text", CSERDE_STRING,
         0u, 0u, 0u, 0u, 0u, 0u, DATA_BIND_BINARY_REP_VAR_DATA, sizeof(uint32_t)}};
    for (int big_endian = 0; big_endian <= 1; ++big_endian) {
      DataBindBinaryLayoutPlan entry = binary_plan(big_endian);
      const DataBindBinaryLayoutPlan *children[] = {&entry, NULL};
      DataBindBinaryLayoutPlan plan = DATA_BIND_BINARY_LAYOUT_PLAN_INIT;
      unsigned char wire[GROUP_WIRE_BYTES + 1u] = {0};
      cserde_reader *reader = NULL;
      void *owner = NULL;
      DataBindError error = DATA_BIND_ERROR_INIT;
      cserde_token token = {0};
      size_t maps = 0u, arrays = 0u, integers = 0u, texts = 0u;
      cserde_status status;
      plan.type_name = "GroupAndTail";
      plan.wire_big_endian = big_endian;
      plan.fixed_block_size = 2u;
      plan.presence_size = 1u;
      plan.null_offset = 1u;
      plan.null_size = 1u;
      plan.fields = fields;
      plan.field_count = sizeof(fields) / sizeof(fields[0]);
      plan.child_plans = children;
      wire[0] = 1u;
      data_bind_binary_wire_write_u16(wire + plan.fixed_block_size, big_endian, GROUP_ENTRY_STRIDE);
      data_bind_binary_wire_write_u16(wire + plan.fixed_block_size + sizeof(uint16_t), big_endian, GROUP_ENTRY_COUNT);
      for (size_t row = 0u; row < GROUP_ENTRY_COUNT; ++row)
        write_payload(wire + plan.fixed_block_size + DATA_BIND_BINARY_GROUP_HEADER_SIZE + row * GROUP_ENTRY_STRIDE,
                      big_endian, 1, 0);
      check_true(data_bind_binary_wire_write_var_data(
          wire + GROUP_WIRE_BYTES - sizeof(uint32_t) - 3u, sizeof(uint32_t) + 3u,
          big_endian, "cat", 3u));
      check_equal(data_bind_binary_reader_open(&plan, wire, GROUP_WIRE_BYTES, 3u,
                  &reader, &owner, &error), DATA_BIND_OK);
      while ((status = cserde_reader_next(reader, &token)) == CSERDE_OK) {
        if (token.kind == CSERDE_MAP_BEGIN) ++maps;
        if (token.kind == CSERDE_ARRAY_BEGIN) ++arrays;
        if (token.kind == CSERDE_SINT) {
          ++integers;
          check_equal(token.value.sint, (int64_t)-1234);
        }
        if (token_key_is(&token, "cat")) ++texts;
      }
      check_equal(status, CSERDE_DONE);
      check_equal(maps, (size_t)3u);
      check_equal(arrays, (size_t)1u);
      check_equal(integers, (size_t)2u);
      check_equal(texts, (size_t)1u);
      data_bind_binary_reader_close(reader, owner);
      reader = NULL;
      owner = NULL;
      check_equal(data_bind_binary_reader_open(&plan, wire, GROUP_WIRE_BYTES, 2u,
                  &reader, &owner, &error), DATA_BIND_ERR_LIMIT);
      check_null(reader);
      check_null(owner);
      wire[plan.fixed_block_size + DATA_BIND_BINARY_GROUP_HEADER_SIZE] = 0u;
      wire[plan.fixed_block_size + DATA_BIND_BINARY_GROUP_HEADER_SIZE + 1u] = 1u;
      check_equal(data_bind_binary_reader_open(&plan, wire, GROUP_WIRE_BYTES, 3u,
                  &reader, &owner, &error), DATA_BIND_ERR_PARSE);
      check_null(reader);
      wire[0] = 0u;
      check_equal(data_bind_binary_reader_open(&plan, wire, GROUP_WIRE_BYTES, 3u,
                  &reader, &owner, &error), DATA_BIND_OK);
      arrays = 0u;
      texts = 0u;
      while (cserde_reader_next(reader, &token) == CSERDE_OK) {
        if (token.kind == CSERDE_ARRAY_BEGIN) ++arrays;
        if (token_key_is(&token, "cat")) ++texts;
      }
      check_equal(arrays, (size_t)0u);
      check_equal(texts, (size_t)1u);
      data_bind_binary_reader_close(reader, owner);
      reader = NULL;
      owner = NULL;
      wire[0] = 1u;
      wire[1] = 1u;
      check_equal(data_bind_binary_reader_open(&plan, wire, GROUP_WIRE_BYTES, 3u,
                  &reader, &owner, &error), DATA_BIND_ERR_PARSE);
      wire[0] = 0u;
      wire[1] = 0u;
      data_bind_binary_wire_write_u16(wire + plan.fixed_block_size, big_endian, (uint16_t)(entry.fixed_block_size - 1u));
      check_equal(data_bind_binary_reader_open(&plan, wire, GROUP_WIRE_BYTES, 3u,
                  &reader, &owner, &error), DATA_BIND_ERR_PARSE);
      data_bind_binary_wire_write_u16(wire + plan.fixed_block_size, big_endian, GROUP_ENTRY_STRIDE);
      data_bind_binary_wire_write_u16(wire + plan.fixed_block_size + sizeof(uint16_t), big_endian, UINT16_MAX);
      check_equal(data_bind_binary_reader_open(&plan, wire, GROUP_WIRE_BYTES, 3u,
                  &reader, &owner, &error), DATA_BIND_ERR_PARSE);
      data_bind_binary_wire_write_u16(wire + plan.fixed_block_size + sizeof(uint16_t), big_endian, GROUP_ENTRY_COUNT);
      check_equal(data_bind_binary_reader_open(&plan, wire, GROUP_WIRE_BYTES + 1u, 3u,
                  &reader, &owner, &error), DATA_BIND_ERR_PARSE);
      for (size_t bytes = plan.fixed_block_size; bytes < GROUP_WIRE_BYTES; ++bytes) {
        check_equal(data_bind_binary_reader_open(&plan, wire, bytes, 3u,
                    &reader, &owner, &error), DATA_BIND_ERR_PARSE);
        check_null(reader);
        check_null(owner);
      }
      children[0] = NULL;
      check_equal(data_bind_binary_layout_plan_validate(&plan, &error), DATA_BIND_ERR_SCHEMA);
      children[0] = &plan;
      check_equal(data_bind_binary_layout_plan_validate(&plan, &error), DATA_BIND_ERR_SCHEMA);
      entry = var_data_plan(big_endian);
      children[0] = &entry;
      check_equal(data_bind_binary_layout_plan_validate(&plan, &error), DATA_BIND_ERR_SCHEMA);
    }
  }

  it("validates exact fixed child extents, cycles, tails and depth before opening") {
    enum { RECORD_CHAIN_LENGTH = DATA_BIND_BINARY_LAYOUT_MAX_DEPTH + 1u };
    DataBindBinaryLayoutPlan plans[RECORD_CHAIN_LENGTH];
    DataBindBinaryFieldPlan fields[RECORD_CHAIN_LENGTH];
    const DataBindBinaryLayoutPlan *children[RECORD_CHAIN_LENGTH];
    unsigned char wire[sizeof(uint32_t)] = {0};
    cserde_reader *reader = NULL;
    void *owner = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    for (size_t i = 0u; i < RECORD_CHAIN_LENGTH; ++i) {
      plans[i] = (DataBindBinaryLayoutPlan)DATA_BIND_BINARY_LAYOUT_PLAN_INIT;
      fields[i] = (DataBindBinaryFieldPlan)DATA_BIND_BINARY_FIELD_PLAN_INIT;
      plans[i].type_name = "FixedRecord";
      plans[i].fixed_block_size = sizeof(wire);
      plans[i].fields = &fields[i];
      plans[i].field_count = 1u;
      plans[i].child_plans = &children[i];
      fields[i].field_name = "value";
      fields[i].wire_extent = sizeof(wire);
      if (i + 1u < RECORD_CHAIN_LENGTH) {
        fields[i].token_kind = CSERDE_MAP_BEGIN;
        children[i] = &plans[i + 1u];
      } else {
        fields[i].token_kind = CSERDE_UINT;
        fields[i].scalar_bits = sizeof(uint32_t) * 8u;
        children[i] = NULL;
      }
    }
    check_equal(data_bind_binary_layout_plan_validate(&plans[1], &error), DATA_BIND_OK);
    check_equal(data_bind_binary_reader_open(&plans[1], wire, sizeof(wire), 0u,
                &reader, &owner, &error), DATA_BIND_OK);
    {
      cserde_token token = {0};
      cserde_writer *writer = NULL;
      void *writer_owner = NULL;
      unsigned publications = 0u;
      size_t maps = 0u;
      cserde_status status;
      check_equal(data_bind_binary_writer_open(&plans[1], accept_zero_wire, &publications, 0u,
                  &writer, &writer_owner, &error), DATA_BIND_OK);
      while ((status = cserde_reader_next(reader, &token)) == CSERDE_OK) {
        if (token.kind == CSERDE_MAP_BEGIN) ++maps;
        check_equal(cserde_writer_write(writer, &token), CSERDE_OK);
      }
      check_equal(status, CSERDE_DONE);
      check_equal(maps, (size_t)DATA_BIND_BINARY_LAYOUT_MAX_DEPTH);
      check_equal(publications, 0u);
      check_equal(data_bind_binary_writer_close(writer, writer_owner, &error), DATA_BIND_OK);
      check_equal(publications, 1u);
    }
    data_bind_binary_reader_close(reader, owner);
    reader = NULL;
    owner = NULL;
    check_equal(data_bind_binary_reader_open(&plans[0], wire, sizeof(wire), 0u,
                &reader, &owner, &error), DATA_BIND_ERR_LIMIT);
    check_null(reader);
    check_null(owner);
    check_equal(data_bind_binary_reader_open(&plans[1], wire, sizeof(wire), 1u,
                &reader, &owner, &error), DATA_BIND_ERR_LIMIT);
    children[0] = &plans[0];
    check_equal(data_bind_binary_layout_plan_validate(&plans[0], &error), DATA_BIND_ERR_SCHEMA);
    children[0] = &plans[RECORD_CHAIN_LENGTH - 1u];
    fields[0].wire_extent = sizeof(wire) - 1u;
    check_equal(data_bind_binary_layout_plan_validate(&plans[0], &error), DATA_BIND_ERR_SCHEMA);
    fields[0].wire_extent = sizeof(wire);
    plans[RECORD_CHAIN_LENGTH - 1u].wire_big_endian = 1;
    check_equal(data_bind_binary_layout_plan_validate(&plans[0], &error), DATA_BIND_ERR_SCHEMA);
    plans[RECORD_CHAIN_LENGTH - 1u].wire_big_endian = 0;
    children[0] = NULL;
    check_equal(data_bind_binary_layout_plan_validate(&plans[0], &error), DATA_BIND_ERR_SCHEMA);
    children[0] = &plans[RECORD_CHAIN_LENGTH - 1u];
    plans[0].size = DATA_BIND_BINARY_LAYOUT_PLAN_V1_SIZE;
    check_equal(data_bind_binary_layout_plan_validate(&plans[0], &error), DATA_BIND_ERR_SCHEMA);
    plans[0].size = sizeof(plans[0]);
    {
      DataBindBinaryLayoutPlan tail = tail_only_plan(0);
      children[0] = &tail;
      check_equal(data_bind_binary_layout_plan_validate(&plans[0], &error), DATA_BIND_ERR_SCHEMA);
    }
  }
  it("reads VAR_DATA without a fixed block in both wire orders") {
    int big_endian;
    for (big_endian = 0; big_endian <= 1; ++big_endian) {
      DataBindBinaryLayoutPlan plan = tail_only_plan(big_endian);
      unsigned char wire[7];
      cserde_reader *reader = NULL;
      void *owner = NULL;
      cserde_token token = {0};
      DataBindError error = DATA_BIND_ERROR_INIT;
      check_true(data_bind_binary_wire_write_var_data(
          wire, sizeof(wire), big_endian, "cat", 3u));
      check_equal(data_bind_binary_reader_open(
                      &plan, wire, sizeof(wire), 8u, &reader, &owner, &error),
                  DATA_BIND_OK);
      check_true(next_token(reader, &token));
      check_equal(token.kind, CSERDE_MAP_BEGIN);
      check_true(next_token(reader, &token));
      check_true(token_key_is(&token, "text"));
      check_true(next_token(reader, &token));
      check_equal(token.kind, CSERDE_STRING);
      check_equal(token.value.slice.size, (size_t)3u);
      check(memcmp(token.value.slice.data, "cat", 3u) == 0);
      check_true(next_token(reader, &token));
      check_equal(token.kind, CSERDE_MAP_END);
      check_equal(cserde_reader_next(reader, &token), CSERDE_DONE);
      data_bind_binary_reader_close(reader, owner);
      reader = NULL;
      owner = NULL;
      check_equal(data_bind_binary_reader_open(
                      &plan, wire, 3u, 8u, &reader, &owner, &error),
                  DATA_BIND_ERR_PARSE);
      check_null(reader);
      check_null(owner);
    }
  }

  it("emits canonical little-endian fixed scalar tokens") {
    DataBindBinaryLayoutPlan plan = binary_plan(0);
    unsigned char wire[15];
    cserde_reader *reader = NULL;
    void *owner = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    write_payload(wire, 0, 1, 0);
    check_equal(
        data_bind_binary_layout_plan_validate(&plan, &error),
        DATA_BIND_OK);
    check_equal(
        data_bind_binary_reader_open(
            &plan, wire, sizeof(wire), 8u,
            &reader, &owner, &error),
        DATA_BIND_OK);
    check_not_null(reader);
    check_not_null(owner);
    expect_required_prefix(reader, 1, 0);
    data_bind_binary_reader_close(reader, owner);
  }

  it("preserves the same scalar semantics in big-endian wire order") {
    DataBindBinaryLayoutPlan plan = binary_plan(1);
    unsigned char wire[15];
    cserde_reader *reader = NULL;
    void *owner = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    write_payload(wire, 1, 1, 0);
    check_equal(
        data_bind_binary_reader_open(
            &plan, wire, sizeof(wire), 8u,
            &reader, &owner, &error),
        DATA_BIND_OK);
    expect_required_prefix(reader, 1, 0);
    data_bind_binary_reader_close(reader, owner);
  }

  it("omits ABSENT optional fields and distinguishes explicit NULL") {
    DataBindBinaryLayoutPlan plan = binary_plan(0);
    unsigned char wire[15];
    cserde_reader *reader = NULL;
    void *owner = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    write_payload(wire, 0, 0, 0);
    check_equal(
        data_bind_binary_reader_open(
            &plan, wire, sizeof(wire), 8u,
            &reader, &owner, &error),
        DATA_BIND_OK);
    expect_required_prefix(reader, 0, 0);
    data_bind_binary_reader_close(reader, owner);

    reader = NULL;
    owner = NULL;
    write_payload(wire, 0, 1, 1);
    check_equal(
        data_bind_binary_reader_open(
            &plan, wire, sizeof(wire), 8u,
            &reader, &owner, &error),
        DATA_BIND_OK);
    expect_required_prefix(reader, 1, 1);
    data_bind_binary_reader_close(reader, owner);
  }

  it("rejects malformed state truncation and trailing bytes before publication") {
    DataBindBinaryLayoutPlan plan = binary_plan(0);
    unsigned char wire[16];
    cserde_reader *reader = NULL;
    void *owner = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    write_payload(wire, 0, 0, 1);
    check_equal(
        data_bind_binary_reader_open(
            &plan, wire, 15u, 8u,
            &reader, &owner, &error),
        DATA_BIND_ERR_PARSE);
    check_null(reader);
    check_null(owner);
    check_equal(error.path, "maybe");

    write_payload(wire, 0, 1, 0);
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        data_bind_binary_reader_open(
            &plan, wire, 14u, 8u,
            &reader, &owner, &error),
        DATA_BIND_ERR_PARSE);
    check_null(reader);
    check_contains(error.message, "shorter");

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        data_bind_binary_reader_open(
            &plan, wire, sizeof(wire), 8u,
            &reader, &owner, &error),
        DATA_BIND_ERR_PARSE);
    check_null(reader);
    check_contains(error.message, "trailing");
  }

  it("emits canonical VAR_DATA string and bytes in both endian orders") {
    static const unsigned char payload[] = {0x00u, 0x7fu, 0xffu};
    int big_endian;

    for (big_endian = 0; big_endian <= 1; ++big_endian) {
      DataBindBinaryLayoutPlan plan = var_data_plan(big_endian);
      unsigned char wire[64];
      size_t wire_size = write_var_data_payload(
          wire, sizeof(wire), big_endian, 1, 0,
          payload, sizeof(payload));
      cserde_reader *reader = NULL;
      void *owner = NULL;
      DataBindError error = DATA_BIND_ERROR_INIT;

      check_greater(wire_size, plan.fixed_block_size);
      check_equal(
          data_bind_binary_layout_plan_validate(&plan, &error),
          DATA_BIND_OK);
      check_equal(
          data_bind_binary_reader_open(
              &plan, wire, wire_size, 8u,
              &reader, &owner, &error),
          DATA_BIND_OK);
      check_not_null(reader);
      check_not_null(owner);
      if (reader != NULL)
        expect_var_data_message(
            reader, 1, 0, payload, sizeof(payload));
      data_bind_binary_reader_close(reader, owner);
    }
  }

  it("preserves ABSENT NULL and empty VALUE as distinct VAR_DATA states") {
    DataBindBinaryLayoutPlan plan = var_data_plan(0);
    unsigned char wire[64];
    cserde_reader *reader = NULL;
    void *owner = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    size_t wire_size;

    wire_size = write_var_data_payload(
        wire, sizeof(wire), 0, 0, 0, NULL, 0u);
    check_equal(
        data_bind_binary_reader_open(
            &plan, wire, wire_size, 8u,
            &reader, &owner, &error),
        DATA_BIND_OK);
    if (reader != NULL)
      expect_var_data_message(reader, 0, 0, NULL, 0u);
    data_bind_binary_reader_close(reader, owner);

    reader = NULL;
    owner = NULL;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    wire_size = write_var_data_payload(
        wire, sizeof(wire), 0, 1, 1, NULL, 0u);
    check_equal(
        data_bind_binary_reader_open(
            &plan, wire, wire_size, 8u,
            &reader, &owner, &error),
        DATA_BIND_OK);
    if (reader != NULL)
      expect_var_data_message(reader, 1, 1, NULL, 0u);
    data_bind_binary_reader_close(reader, owner);

    reader = NULL;
    owner = NULL;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    wire_size = write_var_data_payload(
        wire, sizeof(wire), 0, 1, 0, NULL, 0u);
    check_equal(
        data_bind_binary_reader_open(
            &plan, wire, wire_size, 8u,
            &reader, &owner, &error),
        DATA_BIND_OK);
    if (reader != NULL)
      expect_var_data_message(reader, 1, 0, NULL, 0u);
    data_bind_binary_reader_close(reader, owner);
  }

  it("rejects malformed VAR_DATA tails before publishing a reader") {
    DataBindBinaryLayoutPlan plan = var_data_plan(0);
    unsigned char wire[64];
    static const unsigned char payload[] = {1u, 2u};
    cserde_reader *reader = NULL;
    void *owner = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    size_t wire_size;
    size_t payload_prefix;

    wire_size = write_var_data_payload(
        wire, sizeof(wire), 0, 1, 0,
        payload, sizeof(payload));
    check_greater(wire_size, (size_t)0u);
    payload_prefix = 6u + sizeof(uint32_t) + 3u;

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        data_bind_binary_reader_open(
            &plan, wire, payload_prefix + 2u, 8u,
            &reader, &owner, &error),
        DATA_BIND_ERR_PARSE);
    check_null(reader);
    check_null(owner);
    check_equal(error.path, "payload");
    check_contains(error.message, "prefix");

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    data_bind_binary_wire_write_u32(
        wire + payload_prefix, 0, UINT32_C(8));
    check_equal(
        data_bind_binary_reader_open(
            &plan, wire, payload_prefix + sizeof(uint32_t) + 2u,
            8u, &reader, &owner, &error),
        DATA_BIND_ERR_PARSE);
    check_null(reader);
    check_equal(error.path, "payload");
    check_contains(error.message, "payload");

    /*
     * ABSENT is state, not tail omission. The existing Binary wire contract
     * still consumes the positional VAR_DATA entry, even when hidden bytes are
     * not published through CSerde.
     */
    wire_size = write_var_data_payload(
        wire, sizeof(wire), 0, 0, 0, NULL, 0u);
    data_bind_binary_wire_write_u32(
        wire + payload_prefix, 0, UINT32_C(1));
    wire[payload_prefix + sizeof(uint32_t)] = 0xa5u;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        data_bind_binary_reader_open(
            &plan, wire,
            payload_prefix + sizeof(uint32_t) + 1u,
            8u, &reader, &owner, &error),
        DATA_BIND_OK);
    if (reader != NULL)
      expect_var_data_message(reader, 0, 0, NULL, 0u);
    data_bind_binary_reader_close(reader, owner);
    reader = NULL;
    owner = NULL;

    wire_size = write_var_data_payload(
        wire, sizeof(wire), 0, 1, 1, NULL, 0u);
    data_bind_binary_wire_write_u32(
        wire + payload_prefix, 0, UINT32_C(1));
    wire[payload_prefix + sizeof(uint32_t)] = 0xa5u;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        data_bind_binary_reader_open(
            &plan, wire,
            payload_prefix + sizeof(uint32_t) + 1u,
            8u, &reader, &owner, &error),
        DATA_BIND_ERR_PARSE);
    check_equal(error.path, "payload");
    check_contains(error.message, "NULL");

    wire_size = write_var_data_payload(
        wire, sizeof(wire), 0, 1, 0,
        payload, sizeof(payload));
    wire[wire_size] = 0x5au;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        data_bind_binary_reader_open(
            &plan, wire, wire_size + 1u, 8u,
            &reader, &owner, &error),
        DATA_BIND_ERR_PARSE);
    check_contains(error.message, "trailing");
  }

  it("continues to admit released v1 fixed-scalar field records") {
    DataBindBinaryLayoutPlan plan = binary_plan(0);
    DataBindBinaryFieldPlan fields[5];
    unsigned char wire[15];
    cserde_reader *reader = NULL;
    void *owner = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    size_t i;
    DataBindBinaryLayoutPlan *released;

    memcpy(fields, BINARY_FIELDS, sizeof(fields));
    for (i = 0u; i < sizeof(fields) / sizeof(fields[0]); ++i)
      fields[i].size = DATA_BIND_BINARY_FIELD_PLAN_V1_SIZE;
    plan.fields = fields;
    plan.size = DATA_BIND_BINARY_LAYOUT_PLAN_V1_SIZE;
    released = (DataBindBinaryLayoutPlan *)malloc(plan.size);
    check_not_null(released);
    if (released == NULL) return;
    memcpy(released, &plan, plan.size);

    write_payload(wire, 0, 1, 0);
    check_equal(
        data_bind_binary_layout_plan_validate(released, &error),
        DATA_BIND_OK);
    check_equal(
        data_bind_binary_reader_open(
            released, wire, sizeof(wire), 8u,
            &reader, &owner, &error),
        DATA_BIND_OK);
    if (reader != NULL) expect_required_prefix(reader, 1, 0);
    data_bind_binary_reader_close(reader, owner);
    free(released);
  }

  it("fails closed on unsupported or overlapping runtime plans") {
    DataBindBinaryLayoutPlan plan = binary_plan(0);
    DataBindBinaryFieldPlan fields[5];
    DataBindError error = DATA_BIND_ERROR_INIT;

    memcpy(fields, BINARY_FIELDS, sizeof(fields));
    plan.fields = fields;

    fields[0].token_kind = CSERDE_BYTES;
    check_equal(
        data_bind_binary_layout_plan_validate(&plan, &error),
        DATA_BIND_ERR_SCHEMA);

    fields[0] = BINARY_FIELDS[0];
    fields[1].wire_offset = fields[0].wire_offset;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        data_bind_binary_layout_plan_validate(&plan, &error),
        DATA_BIND_ERR_SCHEMA);
    check_equal(error.path, "delta");
  }
}
