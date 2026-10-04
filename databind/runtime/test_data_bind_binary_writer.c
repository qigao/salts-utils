#include "data_bind_binary_writer.h"
#include "data_bind_binary_wire.h"
#include "tinytest.h"

#include <stdint.h>
#include <string.h>

typedef struct BinarySink {
  unsigned char *data;
  size_t capacity;
  size_t size;
  unsigned calls;
} BinarySink;

static int binary_sink_write(const void *data, size_t len, void *opaque) {
  BinarySink *sink = (BinarySink *)opaque;
  if (sink == NULL || data == NULL || len > sink->capacity) return -1;
  memcpy(sink->data, data, len);
  sink->size = len;
  ++sink->calls;
  return 0;
}

static const DataBindBinaryFieldPlan SCALAR_FIELDS[] = {
    {sizeof(DataBindBinaryFieldPlan), "flag",
     CSERDE_BOOL, 8u, 2u, 1u, 0u, 0u, 0u,
     DATA_BIND_BINARY_REP_FIXED, 0u},
    {sizeof(DataBindBinaryFieldPlan), "delta",
     CSERDE_SINT, 16u, 3u, 2u, 0u, 0u, 0u,
     DATA_BIND_BINARY_REP_FIXED, 0u},
    {sizeof(DataBindBinaryFieldPlan), "count",
     CSERDE_UINT, 32u, 5u, 4u, 0u, 0u, 0u,
     DATA_BIND_BINARY_REP_FIXED, 0u},
    {sizeof(DataBindBinaryFieldPlan), "ratio",
     CSERDE_FLOAT, 32u, 9u, 4u, 0u, 0u, 0u,
     DATA_BIND_BINARY_REP_FIXED, 0u},
    {sizeof(DataBindBinaryFieldPlan), "maybe",
     CSERDE_UINT, 16u, 13u, 2u, 0u, 0u,
     DATA_BIND_BINARY_FIELD_OPTIONAL |
         DATA_BIND_BINARY_FIELD_NULLABLE,
     DATA_BIND_BINARY_REP_FIXED, 0u},
};

static DataBindBinaryLayoutPlan scalar_plan(int big_endian) {
  DataBindBinaryLayoutPlan plan = DATA_BIND_BINARY_LAYOUT_PLAN_INIT;
  plan.type_name = "BinaryScalars";
  plan.wire_big_endian = big_endian;
  plan.fixed_block_size = 15u;
  plan.presence_offset = 0u;
  plan.presence_size = 1u;
  plan.null_offset = 1u;
  plan.null_size = 1u;
  plan.fields = SCALAR_FIELDS;
  plan.field_count = sizeof(SCALAR_FIELDS) / sizeof(SCALAR_FIELDS[0]);
  return plan;
}

static const DataBindBinaryFieldPlan VAR_FIELDS[] = {
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

static DataBindBinaryLayoutPlan var_plan(int big_endian) {
  DataBindBinaryLayoutPlan plan = DATA_BIND_BINARY_LAYOUT_PLAN_INIT;
  plan.type_name = "BinaryVar";
  plan.wire_big_endian = big_endian;
  plan.fixed_block_size = 6u;
  plan.presence_offset = 0u;
  plan.presence_size = 1u;
  plan.null_offset = 1u;
  plan.null_size = 1u;
  plan.fields = VAR_FIELDS;
  plan.field_count = sizeof(VAR_FIELDS) / sizeof(VAR_FIELDS[0]);
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

static cserde_token map_begin(void) {
  cserde_token token = {0};
  token.kind = CSERDE_MAP_BEGIN;
  return token;
}

static cserde_token map_end(void) {
  cserde_token token = {0};
  token.kind = CSERDE_MAP_END;
  return token;
}

static cserde_token key(const char *name) {
  cserde_token token = {0};
  token.kind = CSERDE_STRING;
  token.value.slice.data = (const unsigned char *)name;
  token.value.slice.size = strlen(name);
  token.value.slice.lifetime = CSERDE_VIEW_STABLE;
  return token;
}

static cserde_token boolean(int value) {
  cserde_token token = {0};
  token.kind = CSERDE_BOOL;
  token.value.boolean = value != 0;
  return token;
}

static cserde_token sint(int64_t value) {
  cserde_token token = {0};
  token.kind = CSERDE_SINT;
  token.value.sint = value;
  return token;
}

static cserde_token uint_value(uint64_t value) {
  cserde_token token = {0};
  token.kind = CSERDE_UINT;
  token.value.uint = value;
  return token;
}

static cserde_token floating(double value) {
  cserde_token token = {0};
  token.kind = CSERDE_FLOAT;
  token.value.floating = value;
  return token;
}

static cserde_token null_value(void) {
  cserde_token token = {0};
  token.kind = CSERDE_NULL;
  return token;
}

static cserde_token slice_value(
    cserde_token_kind kind, const void *data, size_t size) {
  cserde_token token = {0};
  token.kind = kind;
  token.value.slice.data = (const unsigned char *)data;
  token.value.slice.size = size;
  token.value.slice.lifetime = CSERDE_VIEW_STABLE;
  return token;
}

static int write_token(cserde_writer *writer, cserde_token token) {
  return cserde_writer_write(writer, &token) == CSERDE_OK;
}

static void write_required_scalars(cserde_writer *writer) {
  check_true(write_token(writer, key("flag")));
  check_true(write_token(writer, boolean(1)));
  check_true(write_token(writer, key("delta")));
  check_true(write_token(writer, sint(-1234)));
  check_true(write_token(writer, key("count")));
  check_true(write_token(writer, uint_value(UINT32_C(0x11223344))));
  check_true(write_token(writer, key("ratio")));
  check_true(write_token(writer, floating(1.5)));
}

spec("DataBind flat Binary canonical writer") {
  it("writes VAR_DATA without a fixed block in both wire orders") {
    int big_endian;
    for (big_endian = 0; big_endian <= 1; ++big_endian) {
      DataBindBinaryLayoutPlan plan = tail_only_plan(big_endian);
      unsigned char wire[16] = {0};
      BinarySink sink = {wire, sizeof(wire), 0u, 0u};
      cserde_writer *writer = NULL;
      void *owner = NULL;
      DataBindError error = DATA_BIND_ERROR_INIT;
      check_equal(data_bind_binary_writer_open(
                      &plan, binary_sink_write, &sink, 8u,
                      &writer, &owner, &error),
                  DATA_BIND_OK);
      check_true(write_token(writer, map_begin()));
      check_true(write_token(writer, key("text")));
      check_true(write_token(writer, slice_value(CSERDE_STRING, "cat", 3u)));
      check_true(write_token(writer, map_end()));
      check_equal(cserde_writer_finish(writer), CSERDE_OK);
      check_equal(sink.calls, 1u);
      check_equal(sink.size, (size_t)7u);
      check_equal(data_bind_binary_wire_read_u32(wire, big_endian), (uint32_t)3u);
      check(memcmp(wire + 4u, "cat", 3u) == 0);
      check_equal(data_bind_binary_writer_close(writer, owner, &error), DATA_BIND_OK);
    }
  }

  it("writes fixed scalars in little and big endian order") {
    int big_endian;
    for (big_endian = 0; big_endian <= 1; ++big_endian) {
      DataBindBinaryLayoutPlan plan = scalar_plan(big_endian);
      unsigned char wire[32] = {0};
      BinarySink sink = {wire, sizeof(wire), 0u, 0u};
      cserde_writer *writer = NULL;
      void *owner = NULL;
      DataBindError error = DATA_BIND_ERROR_INIT;

      check_equal(
          data_bind_binary_writer_open(
              &plan, binary_sink_write, &sink, 8u,
              &writer, &owner, &error),
          DATA_BIND_OK);
      check_not_null(writer);
      check_true(write_token(writer, map_begin()));
      write_required_scalars(writer);
      check_true(write_token(writer, key("maybe")));
      check_true(write_token(writer, uint_value(513u)));
      check_true(write_token(writer, map_end()));
      check_equal(cserde_writer_finish(writer), CSERDE_OK);
      check_equal(sink.calls, 1u);
      check_equal(sink.size, (size_t)15u);
      check_equal(wire[0] & 1u, 1u);
      check_equal(wire[1] & 1u, 0u);
      check_equal(
          data_bind_binary_wire_read_i16(wire + 3u, big_endian),
          (int16_t)-1234);
      check_equal(
          data_bind_binary_wire_read_u32(wire + 5u, big_endian),
          UINT32_C(0x11223344));
      check_equal(
          data_bind_binary_wire_read_u16(wire + 13u, big_endian),
          UINT16_C(513));
      check_equal(
          data_bind_binary_writer_close(writer, owner, &error),
          DATA_BIND_OK);
      check_equal(sink.calls, 1u);
    }
  }

  it("preserves ABSENT and NULL as distinct fixed-field states") {
    DataBindBinaryLayoutPlan plan = scalar_plan(0);
    unsigned char wire[32] = {0};
    BinarySink sink = {wire, sizeof(wire), 0u, 0u};
    cserde_writer *writer = NULL;
    void *owner = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(
        data_bind_binary_writer_open(
            &plan, binary_sink_write, &sink, 8u,
            &writer, &owner, &error),
        DATA_BIND_OK);
    check_true(write_token(writer, map_begin()));
    write_required_scalars(writer);
    check_true(write_token(writer, map_end()));
    check_equal(cserde_writer_finish(writer), CSERDE_OK);
    check_equal(wire[0] & 1u, 0u);
    check_equal(wire[1] & 1u, 0u);
    check_equal(
        data_bind_binary_writer_close(writer, owner, &error),
        DATA_BIND_OK);

    memset(wire, 0, sizeof(wire));
    sink.size = 0u;
    sink.calls = 0u;
    writer = NULL;
    owner = NULL;
    check_equal(
        data_bind_binary_writer_open(
            &plan, binary_sink_write, &sink, 8u,
            &writer, &owner, &error),
        DATA_BIND_OK);
    check_true(write_token(writer, map_begin()));
    write_required_scalars(writer);
    check_true(write_token(writer, key("maybe")));
    check_true(write_token(writer, null_value()));
    check_true(write_token(writer, map_end()));
    check_equal(cserde_writer_finish(writer), CSERDE_OK);
    check_equal(wire[0] & 1u, 1u);
    check_equal(wire[1] & 1u, 1u);
    check_equal(
        data_bind_binary_writer_close(writer, owner, &error),
        DATA_BIND_OK);
  }

  it("writes positional VAR_DATA tails for ABSENT NULL and VALUE") {
    static const char source[] = "cam";
    static const unsigned char payload[] = {0x00u, 0x7fu, 0xffu};
    DataBindBinaryLayoutPlan plan = var_plan(0);
    unsigned char wire[64] = {0};
    BinarySink sink = {wire, sizeof(wire), 0u, 0u};
    cserde_writer *writer = NULL;
    void *owner = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    size_t payload_prefix;

    check_equal(
        data_bind_binary_writer_open(
            &plan, binary_sink_write, &sink, 8u,
            &writer, &owner, &error),
        DATA_BIND_OK);
    check_true(write_token(writer, map_begin()));
    check_true(write_token(writer, key("sequence")));
    check_true(write_token(writer, uint_value(7u)));
    check_true(write_token(writer, key("source")));
    check_true(write_token(
        writer, slice_value(
                    CSERDE_STRING, source, sizeof(source) - 1u)));
    check_true(write_token(writer, map_end()));
    check_equal(cserde_writer_finish(writer), CSERDE_OK);
    payload_prefix = 6u + sizeof(uint32_t) + sizeof(source) - 1u;
    check_equal(
        data_bind_binary_wire_read_u32(wire + 6u, 0),
        (uint32_t)(sizeof(source) - 1u));
    check_equal(
        data_bind_binary_wire_read_u32(wire + payload_prefix, 0),
        UINT32_C(0));
    check_equal(wire[0] & 1u, 0u);
    check_equal(wire[1] & 1u, 0u);
    check_equal(
        data_bind_binary_writer_close(writer, owner, &error),
        DATA_BIND_OK);

    memset(wire, 0, sizeof(wire));
    sink.size = 0u;
    sink.calls = 0u;
    writer = NULL;
    owner = NULL;
    check_equal(
        data_bind_binary_writer_open(
            &plan, binary_sink_write, &sink, 8u,
            &writer, &owner, &error),
        DATA_BIND_OK);
    check_true(write_token(writer, map_begin()));
    check_true(write_token(writer, key("sequence")));
    check_true(write_token(writer, uint_value(7u)));
    check_true(write_token(writer, key("source")));
    check_true(write_token(
        writer, slice_value(
                    CSERDE_STRING, source, sizeof(source) - 1u)));
    check_true(write_token(writer, key("payload")));
    check_true(write_token(writer, null_value()));
    check_true(write_token(writer, map_end()));
    check_equal(cserde_writer_finish(writer), CSERDE_OK);
    check_equal(wire[0] & 1u, 1u);
    check_equal(wire[1] & 1u, 1u);
    check_equal(
        data_bind_binary_wire_read_u32(wire + payload_prefix, 0),
        UINT32_C(0));
    check_equal(
        data_bind_binary_writer_close(writer, owner, &error),
        DATA_BIND_OK);

    memset(wire, 0, sizeof(wire));
    sink.size = 0u;
    sink.calls = 0u;
    writer = NULL;
    owner = NULL;
    check_equal(
        data_bind_binary_writer_open(
            &plan, binary_sink_write, &sink, 8u,
            &writer, &owner, &error),
        DATA_BIND_OK);
    check_true(write_token(writer, map_begin()));
    check_true(write_token(writer, key("sequence")));
    check_true(write_token(writer, uint_value(7u)));
    check_true(write_token(writer, key("source")));
    check_true(write_token(
        writer, slice_value(
                    CSERDE_STRING, source, sizeof(source) - 1u)));
    check_true(write_token(writer, key("payload")));
    check_true(write_token(
        writer, slice_value(CSERDE_BYTES, payload, sizeof(payload))));
    check_true(write_token(writer, map_end()));
    check_equal(cserde_writer_finish(writer), CSERDE_OK);
    check_equal(wire[0] & 1u, 1u);
    check_equal(wire[1] & 1u, 0u);
    check_equal(
        data_bind_binary_wire_read_u32(wire + payload_prefix, 0),
        (uint32_t)sizeof(payload));
    check(
        memcmp(
            wire + payload_prefix + sizeof(uint32_t),
            payload, sizeof(payload)) == 0);
    check_equal(
        data_bind_binary_writer_close(writer, owner, &error),
        DATA_BIND_OK);
  }

  it("rejects out-of-order required fields and wrong scalar kinds") {
    DataBindBinaryLayoutPlan plan = scalar_plan(0);
    unsigned char wire[32] = {0};
    BinarySink sink = {wire, sizeof(wire), 0u, 0u};
    cserde_writer *writer = NULL;
    void *owner = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(
        data_bind_binary_writer_open(
            &plan, binary_sink_write, &sink, 8u,
            &writer, &owner, &error),
        DATA_BIND_OK);
    check_true(write_token(writer, map_begin()));
    {
      cserde_token bad = key("count");
      check_equal(
          cserde_writer_write(writer, &bad),
          CSERDE_INVALID_STATE);
    }
    (void)data_bind_binary_writer_close(writer, owner, NULL);

    writer = NULL;
    owner = NULL;
    check_equal(
        data_bind_binary_writer_open(
            &plan, binary_sink_write, &sink, 8u,
            &writer, &owner, &error),
        DATA_BIND_OK);
    check_true(write_token(writer, map_begin()));
    check_true(write_token(writer, key("flag")));
    {
      cserde_token bad = uint_value(1u);
      check_equal(
          cserde_writer_write(writer, &bad),
          CSERDE_UNSUPPORTED);
    }
    (void)data_bind_binary_writer_close(writer, owner, NULL);
  }

  it("commits to the byte sink once and leaves bounded sinks unchanged on failure") {
    DataBindBinaryLayoutPlan plan = scalar_plan(0);
    unsigned char wire[8];
    unsigned char before[sizeof(wire)];
    BinarySink sink = {wire, sizeof(wire), 0u, 0u};
    cserde_writer *writer = NULL;
    void *owner = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    memset(wire, 0xa5, sizeof(wire));
    memcpy(before, wire, sizeof(wire));
    check_equal(
        data_bind_binary_writer_open(
            &plan, binary_sink_write, &sink, 8u,
            &writer, &owner, &error),
        DATA_BIND_OK);
    check_true(write_token(writer, map_begin()));
    write_required_scalars(writer);
    check_true(write_token(writer, map_end()));
    check_equal(cserde_writer_finish(writer), CSERDE_SINK_ERROR);
    check_equal(sink.calls, 0u);
    check_equal(sink.size, (size_t)0u);
    check(memcmp(wire, before, sizeof(wire)) == 0);
    check_equal(
        data_bind_binary_writer_close(writer, owner, &error),
        DATA_BIND_ERR_IO);
  }
}
