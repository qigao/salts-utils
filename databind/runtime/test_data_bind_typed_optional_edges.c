#include "data_bind_typed.h"
#include "tinytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
  EDGE_SCALAR = 1,
  EDGE_GROUP = 2,
  EDGE_TEXT = 4,
  EDGE_BYTES = 8,
  EDGE_MASK_COUNT = 16,
  EDGE_FIXED_SIZE = 6,
  EDGE_ABSENT_SIZE = 18,
  EDGE_MAX_SIZE = 24,
  EDGE_SENTINEL = 0xa5,
  EDGE_FIELD_COUNT = 5
};

typedef struct OptionalEdgeChild { uint16_t value; } OptionalEdgeChild;
DATA_BIND_TYPED_VEC_DEFINE(optional_edge_children_t, OptionalEdgeChild)

typedef struct OptionalEdgeRecord {
  uint16_t required;
  uint16_t scalar;
  optional_edge_children_t children;
  tstr text;
  tbe_bytes_t bytes;
  uint8_t presence[2];
} OptionalEdgeRecord;

_Static_assert(sizeof(tbe_bytes_t) == sizeof(vec_t), "bytes storage must match vec_t");
_Static_assert(sizeof(optional_edge_children_t) == sizeof(vec_t),
               "group storage must match vec_t");

static const DataBindTypedField EDGE_CHILD_FIELDS[] = {{
    .name = "value", .kind = DATA_BIND_TYPED_U16, .wire_kind = DATA_BIND_TYPED_U16,
    .offset = offsetof(OptionalEdgeChild, value), .wire_offset = 0u,
    .wire_size = sizeof(uint16_t), .flags = DATA_BIND_TYPED_FIELD_WIRE_OFFSET
}};

static const DataBindTypedField EDGE_FIELDS[] = {
    {.name = "required", .kind = DATA_BIND_TYPED_U16, .wire_kind = DATA_BIND_TYPED_U16,
     .offset = offsetof(OptionalEdgeRecord, required), .wire_offset = 2u,
     .wire_size = sizeof(uint16_t), .flags = DATA_BIND_TYPED_FIELD_WIRE_OFFSET},
    {.name = "scalar", .kind = DATA_BIND_TYPED_U16, .wire_kind = DATA_BIND_TYPED_U16,
     .offset = offsetof(OptionalEdgeRecord, scalar), .wire_offset = 4u,
     .wire_size = sizeof(uint16_t), .optional_bit = 0u,
     .flags = DATA_BIND_TYPED_FIELD_OPTIONAL | DATA_BIND_TYPED_FIELD_WIRE_OFFSET},
    {.name = "children", .kind = DATA_BIND_TYPED_LIST, .wire_kind = DATA_BIND_TYPED_LIST,
     .offset = offsetof(OptionalEdgeRecord, children), .element_kind = DATA_BIND_TYPED_OBJECT,
     .element_wire_kind = DATA_BIND_TYPED_OBJECT, .element_size = sizeof(OptionalEdgeChild),
     .optional_bit = 7u, .flags = DATA_BIND_TYPED_FIELD_OPTIONAL | DATA_BIND_TYPED_FIELD_GROUP},
    {.name = "text", .kind = DATA_BIND_TYPED_STRING, .wire_kind = DATA_BIND_TYPED_STRING,
     .offset = offsetof(OptionalEdgeRecord, text), .optional_bit = 8u,
     .flags = DATA_BIND_TYPED_FIELD_OPTIONAL | DATA_BIND_TYPED_FIELD_VAR_DATA},
    {.name = "bytes", .kind = DATA_BIND_TYPED_BYTES, .wire_kind = DATA_BIND_TYPED_BYTES,
     .offset = offsetof(OptionalEdgeRecord, bytes), .optional_bit = 15u,
     .flags = DATA_BIND_TYPED_FIELD_OPTIONAL | DATA_BIND_TYPED_FIELD_VAR_DATA}
};

typedef struct OptionalEdgeDescriptor {
  DataBindTypedType child;
  DataBindTypedField fields[EDGE_FIELD_COUNT];
  DataBindTypedType type;
} OptionalEdgeDescriptor;

static void optional_edge_descriptor(OptionalEdgeDescriptor *descriptor, int big_endian) {
  const DataBindTypedType child = {
      .name = "OptionalEdgeChild", .size = sizeof(OptionalEdgeChild),
      .fields = EDGE_CHILD_FIELDS, .field_count = 1u,
      .fixed_block_size = sizeof(uint16_t), .wire_big_endian = big_endian
  };
  const DataBindTypedType type = {
      .name = "OptionalEdgeRecord", .size = sizeof(OptionalEdgeRecord),
      .field_count = EDGE_FIELD_COUNT, .fixed_block_size = EDGE_FIXED_SIZE,
      .presence_offset = offsetof(OptionalEdgeRecord, presence), .presence_size = 2u,
      .wire_big_endian = big_endian
  };
  descriptor->child = child;
  memcpy(descriptor->fields, EDGE_FIELDS, sizeof(EDGE_FIELDS));
  descriptor->fields[2].object_type = &descriptor->child;
  descriptor->type = type;
  descriptor->type.fields = descriptor->fields;
}

static int optional_edge_init(const DataBindTypedType *type, OptionalEdgeRecord *record) {
  const OptionalEdgeChild child = {UINT16_C(0x9abc)};
  DataBindError error = DATA_BIND_ERROR_INIT;
  if (data_bind_typed_init(type, record, &error) != DATA_BIND_OK) return 0;
  record->required = UINT16_C(0x2468);
  record->scalar = UINT16_C(0x1357);
  record->text = tstr_dup("xy");
  if (record->text == NULL || tbe_bytes_t_resize(&record->bytes, 2u) != STL_OK ||
      optional_edge_children_t_push(&record->children, child) != STL_OK) {
    data_bind_typed_clear(type, record);
    return 0;
  }
  memcpy(tbe_bytes_t_data(&record->bytes), "\xab\xcd", 2u);
  return 1;
}

static void optional_edge_presence(OptionalEdgeRecord *record, unsigned mask) {
  record->presence[0] = (uint8_t)(((mask & EDGE_SCALAR) ? 1u : 0u) |
                                  ((mask & EDGE_GROUP) ? 0x80u : 0u));
  record->presence[1] = (uint8_t)(((mask & EDGE_TEXT) ? 1u : 0u) |
                                  ((mask & EDGE_BYTES) ? 0x80u : 0u));
}

/* Independent wire oracle: fixed -> group -> var-data, with no runtime helpers. */
static size_t optional_edge_expected(unsigned mask, int big_endian, uint8_t *wire) {
  size_t cursor = EDGE_FIXED_SIZE;
  memset(wire, 0, EDGE_MAX_SIZE);
  wire[0] = (uint8_t)(((mask & EDGE_SCALAR) ? 1u : 0u) |
                       ((mask & EDGE_GROUP) ? 0x80u : 0u));
  wire[1] = (uint8_t)(((mask & EDGE_TEXT) ? 1u : 0u) |
                       ((mask & EDGE_BYTES) ? 0x80u : 0u));
  wire[2] = big_endian ? 0x24u : 0x68u;
  wire[3] = big_endian ? 0x68u : 0x24u;
  if (mask & EDGE_SCALAR) {
    wire[4] = big_endian ? 0x13u : 0x57u;
    wire[5] = big_endian ? 0x57u : 0x13u;
  }
  wire[cursor + (big_endian ? 1u : 0u)] = 2u;
  wire[cursor + (big_endian ? 3u : 2u)] = (mask & EDGE_GROUP) ? 1u : 0u;
  cursor += 4u;
  if (mask & EDGE_GROUP) {
    wire[cursor++] = big_endian ? 0x9au : 0xbcu;
    wire[cursor++] = big_endian ? 0xbcu : 0x9au;
  }
  wire[cursor + (big_endian ? 3u : 0u)] = (mask & EDGE_TEXT) ? 2u : 0u;
  cursor += 4u;
  if (mask & EDGE_TEXT) { wire[cursor++] = 'x'; wire[cursor++] = 'y'; }
  wire[cursor + (big_endian ? 3u : 0u)] = (mask & EDGE_BYTES) ? 2u : 0u;
  cursor += 4u;
  if (mask & EDGE_BYTES) { wire[cursor++] = 0xabu; wire[cursor++] = 0xcdu; }
  return cursor;
}

static void optional_edge_check_mask(const DataBindTypedType *type, OptionalEdgeRecord *record,
                                     unsigned mask) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  uint8_t expected[EDGE_MAX_SIZE];
  uint8_t guarded[EDGE_MAX_SIZE + 2u];
  uint8_t untouched[sizeof(guarded)];
  uint8_t host_before[sizeof(*record)];
  uint8_t *allocated = NULL;
  size_t actual_size = 0u;
  size_t allocated_size = 0u;
  size_t capacity;
  size_t expected_size = optional_edge_expected(mask, type->wire_big_endian, expected);

  optional_edge_presence(record, mask);
  memcpy(host_before, record, sizeof(host_before));
  memset(untouched, EDGE_SENTINEL, sizeof(untouched));
  info("big_endian=%d mask=%u", type->wire_big_endian, mask);
  check_equal(data_bind_typed_serialize_binary_into(type, record, NULL, 0u, &actual_size, &error),
              DATA_BIND_ERR_BUFFER_TOO_SMALL);
  check_equal(actual_size, expected_size);

  for (capacity = 0u; capacity < expected_size; ++capacity) {
    memcpy(guarded, untouched, sizeof(guarded));
    check_equal(data_bind_typed_serialize_binary_into(type, record, guarded + 1u, capacity,
                                                &actual_size, &error),
                DATA_BIND_ERR_BUFFER_TOO_SMALL);
    check_equal(actual_size, expected_size);
    check_equal(guarded, untouched, sizeof(guarded));
  }
  memcpy(guarded, untouched, sizeof(guarded));
  check_equal(data_bind_typed_serialize_binary_into(type, record, guarded + 1u, expected_size,
                                              &actual_size, &error), DATA_BIND_OK);
  check_equal(actual_size, expected_size);
  check_equal(guarded + 1u, expected, expected_size);
  check_equal(guarded[0], EDGE_SENTINEL);
  check_equal(guarded[expected_size + 1u], EDGE_SENTINEL);
  check_equal(data_bind_typed_serialize_binary(type, record, &allocated, &allocated_size, &error),
              DATA_BIND_OK);
  check_not_null(allocated);
  check_equal(allocated_size, expected_size);
  if (allocated != NULL && allocated_size == expected_size)
    check_equal(allocated, expected, expected_size);
  check_equal(record, host_before, sizeof(host_before));
  data_bind_typed_serialized_free(allocated);
}

static void optional_edge_check_unread_storage(const DataBindTypedType *type) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  OptionalEdgeRecord record;
  OptionalEdgeRecord owning;
  vec_t stale;
  char non_string_storage[1] = {'x'};
  uint8_t before[sizeof(record)];
  uint8_t expected[EDGE_MAX_SIZE];
  uint8_t output[EDGE_ABSENT_SIZE];
  size_t length = 0u;

  if (!optional_edge_init(type, &record)) { check(0); return; }
  owning = record;
  /* Not valid tstr storage. A read before its address is caught by ASan. */
  record.text = non_string_storage;
  memcpy(&stale, &record.bytes, sizeof(stale));
  stale.data = NULL;
  stale.size = SIZE_MAX;
  memcpy(&record.bytes, &stale, sizeof(stale));
  memcpy(&stale, &record.children, sizeof(stale));
  stale.data = NULL;
  stale.size = SIZE_MAX;
  memcpy(&record.children, &stale, sizeof(stale));
  memcpy(before, &record, sizeof(before));
  memset(output, EDGE_SENTINEL, sizeof(output));

  check_equal(optional_edge_expected(0u, type->wire_big_endian, expected), EDGE_ABSENT_SIZE);
  check_equal(data_bind_typed_serialize_binary_into(type, &record, NULL, 0u, &length, &error),
              DATA_BIND_ERR_BUFFER_TOO_SMALL);
  check_equal(length, EDGE_ABSENT_SIZE);
  check_equal(data_bind_typed_serialize_binary_into(type, &record, output, sizeof(output),
                                              &length, &error), DATA_BIND_OK);
  check_equal(length, EDGE_ABSENT_SIZE);
  check_equal(output, expected, sizeof(output));
  check_equal(&record, before, sizeof(before));

  /* Absence does not weaken validation when these same fields become present. */
  optional_edge_presence(&record, EDGE_GROUP);
  check_equal(data_bind_typed_serialize_binary_into(type, &record, NULL, 0u, &length, &error),
              DATA_BIND_ERR_SCHEMA);
  check_equal(length, 0u);
  optional_edge_presence(&record, EDGE_BYTES);
  check_equal(data_bind_typed_serialize_binary_into(type, &record, NULL, 0u, &length, &error),
              DATA_BIND_ERR_SCHEMA);
  check_equal(length, 0u);

  record = owning;
  data_bind_typed_clear(type, &record);
}

spec("typed optional binary edges") {
  it("keeps every mixed presence mask canonical and short output buffers untouched") {
    int big_endian;
    for (big_endian = 0; big_endian <= 1; ++big_endian) {
      OptionalEdgeDescriptor descriptor;
      OptionalEdgeRecord record;
      unsigned mask;
      optional_edge_descriptor(&descriptor, big_endian);
      if (!optional_edge_init(&descriptor.type, &record)) { check(0); continue; }
      for (mask = 0u; mask < EDGE_MASK_COUNT; ++mask)
        optional_edge_check_mask(&descriptor.type, &record, mask);
      data_bind_typed_clear(&descriptor.type, &record);
    }
  }

  it("does not inspect absent storage but still rejects invalid present containers") {
    int big_endian;
    for (big_endian = 0; big_endian <= 1; ++big_endian) {
      OptionalEdgeDescriptor descriptor;
      optional_edge_descriptor(&descriptor, big_endian);
      optional_edge_check_unread_storage(&descriptor.type);
    }
  }
}
