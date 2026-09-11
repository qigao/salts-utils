#include "tbe_typed.h"
#include "tinytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct OptionalBinaryChild {
  uint16_t value;
} OptionalBinaryChild;

TBE_TYPED_VEC_DEFINE(optional_binary_child_vec_t, OptionalBinaryChild)

typedef struct OptionalBinaryRecord {
  uint16_t scalar;
  tstr text;
  tbe_bytes_t bytes;
  optional_binary_child_vec_t children;
  uint8_t presence[1];
} OptionalBinaryRecord;

static const TbeTypedField OPTIONAL_BINARY_CHILD_FIELDS[] = {{
    .name = "value",
    .kind = TBE_TYPED_U16,
    .wire_kind = TBE_TYPED_U16,
    .offset = offsetof(OptionalBinaryChild, value),
    .wire_offset = 0u,
    .wire_size = sizeof(uint16_t),
    .flags = TBE_TYPED_FIELD_WIRE_OFFSET,
}};

static const TbeTypedType OPTIONAL_BINARY_CHILD_LE = {
    .name = "OptionalBinaryChild",
    .size = sizeof(OptionalBinaryChild),
    .fields = OPTIONAL_BINARY_CHILD_FIELDS,
    .field_count = 1u,
    .fixed_block_size = sizeof(uint16_t),
};

static const TbeTypedType OPTIONAL_BINARY_CHILD_BE = {
    .name = "OptionalBinaryChild",
    .size = sizeof(OptionalBinaryChild),
    .fields = OPTIONAL_BINARY_CHILD_FIELDS,
    .field_count = 1u,
    .fixed_block_size = sizeof(uint16_t),
    .wire_big_endian = 1,
};

#define OPTIONAL_BINARY_FIELDS(CHILD_TYPE)                                                \
  {                                                                                       \
      {.name = "scalar",                                                                 \
       .kind = TBE_TYPED_U16,                                                             \
       .wire_kind = TBE_TYPED_U16,                                                        \
       .offset = offsetof(OptionalBinaryRecord, scalar),                                  \
       .wire_offset = 1u,                                                                 \
       .wire_size = sizeof(uint16_t),                                                      \
       .optional_bit = 0u,                                                                \
       .flags = TBE_TYPED_FIELD_OPTIONAL | TBE_TYPED_FIELD_WIRE_OFFSET},                  \
      {.name = "text",                                                                   \
       .kind = TBE_TYPED_STRING,                                                          \
       .wire_kind = TBE_TYPED_STRING,                                                     \
       .offset = offsetof(OptionalBinaryRecord, text),                                    \
       .optional_bit = 1u,                                                                \
       .flags = TBE_TYPED_FIELD_OPTIONAL | TBE_TYPED_FIELD_VAR_DATA},                     \
      {.name = "bytes",                                                                  \
       .kind = TBE_TYPED_BYTES,                                                           \
       .wire_kind = TBE_TYPED_BYTES,                                                      \
       .offset = offsetof(OptionalBinaryRecord, bytes),                                   \
       .optional_bit = 2u,                                                                \
       .flags = TBE_TYPED_FIELD_OPTIONAL | TBE_TYPED_FIELD_VAR_DATA},                     \
      {.name = "children",                                                               \
       .kind = TBE_TYPED_LIST,                                                            \
       .wire_kind = TBE_TYPED_LIST,                                                       \
       .offset = offsetof(OptionalBinaryRecord, children),                                \
       .element_kind = TBE_TYPED_OBJECT,                                                  \
       .element_wire_kind = TBE_TYPED_OBJECT,                                             \
       .element_size = sizeof(OptionalBinaryChild),                                       \
       .object_type = (CHILD_TYPE),                                                       \
       .optional_bit = 3u,                                                                \
       .flags = TBE_TYPED_FIELD_OPTIONAL | TBE_TYPED_FIELD_GROUP},                        \
  }

static const TbeTypedField OPTIONAL_BINARY_FIELDS_LE[] =
    OPTIONAL_BINARY_FIELDS(&OPTIONAL_BINARY_CHILD_LE);
static const TbeTypedField OPTIONAL_BINARY_FIELDS_BE[] =
    OPTIONAL_BINARY_FIELDS(&OPTIONAL_BINARY_CHILD_BE);

static const TbeTypedType OPTIONAL_BINARY_RECORD_LE = {
    .name = "OptionalBinaryRecord",
    .size = sizeof(OptionalBinaryRecord),
    .fields = OPTIONAL_BINARY_FIELDS_LE,
    .field_count = 4u,
    .fixed_block_size = 3u,
    .presence_offset = offsetof(OptionalBinaryRecord, presence),
    .presence_size = 1u,
};

static const TbeTypedType OPTIONAL_BINARY_RECORD_BE = {
    .name = "OptionalBinaryRecord",
    .size = sizeof(OptionalBinaryRecord),
    .fields = OPTIONAL_BINARY_FIELDS_BE,
    .field_count = 4u,
    .fixed_block_size = 3u,
    .presence_offset = offsetof(OptionalBinaryRecord, presence),
    .presence_size = 1u,
    .wire_big_endian = 1,
};

enum {
  OPTIONAL_BINARY_ABSENT_SIZE = 15,
  OPTIONAL_BINARY_PRESENT_SIZE = 21,
};

static int optional_binary_fill_stale(const TbeTypedType *type, OptionalBinaryRecord *record) {
  static const uint8_t stale_bytes[] = {UINT8_C(0xab), UINT8_C(0xcd)};
  const OptionalBinaryChild child = {UINT16_C(0x5678)};
  DataBindError error = DATA_BIND_ERROR_INIT;

  if (tbe_typed_init(type, record, &error) != DATA_BIND_OK) return 0;
  record->scalar = UINT16_C(0x1234);
  record->text = tstr_dup("xy");
  if (record->text == NULL ||
      tbe_bytes_t_resize(&record->bytes, sizeof(stale_bytes)) != STL_OK ||
      optional_binary_child_vec_t_push(&record->children, child) != STL_OK) {
    tbe_typed_clear(type, record);
    return 0;
  }
  memcpy(tbe_bytes_t_data(&record->bytes), stale_bytes, sizeof(stale_bytes));
  return 1;
}

static void optional_binary_expect_absent(const TbeTypedType *type, const uint8_t *expected,
                                          size_t expected_size) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  OptionalBinaryRecord record;
  const char *text_before;
  const uint8_t *bytes_before;
  const OptionalBinaryChild *children_before;
  uint8_t *encoded = NULL;
  size_t queried_size = 0u;
  size_t encoded_size = 0u;

  if (!optional_binary_fill_stale(type, &record)) {
    check(0);
    return;
  }
  text_before = record.text;
  bytes_before = tbe_bytes_t_data_const(&record.bytes);
  children_before = optional_binary_child_vec_t_data_const(&record.children);

  check_equal(tbe_typed_serialize_binary_into(type, &record, NULL, 0u, &queried_size, &error),
              DATA_BIND_ERR_BUFFER_TOO_SMALL);
  check_equal(queried_size, expected_size);
  check_equal(tbe_typed_serialize_binary(type, &record, &encoded, &encoded_size, &error),
              DATA_BIND_OK);
  check_equal(encoded_size, expected_size);
  if (encoded != NULL && encoded_size >= expected_size)
    check_equal(encoded, expected, expected_size);

  check_equal(record.presence[0], 0u);
  check_equal(record.scalar, UINT16_C(0x1234));
  check(record.text == text_before);
  check_equal(record.text, "xy");
  check(tbe_bytes_t_data_const(&record.bytes) == bytes_before);
  check_equal(tbe_bytes_t_size(&record.bytes), 2u);
  if (tbe_bytes_t_size(&record.bytes) == 2u)
    check_equal(tbe_bytes_t_data_const(&record.bytes), "\xab\xcd", 2u);
  check(optional_binary_child_vec_t_data_const(&record.children) == children_before);
  check_equal(optional_binary_child_vec_t_size(&record.children), 1u);
  if (optional_binary_child_vec_t_size(&record.children) == 1u)
    check_equal(optional_binary_child_vec_t_at_const(&record.children, 0u)->value,
                UINT16_C(0x5678));

  tbe_typed_serialized_free(encoded);
  tbe_typed_clear(type, &record);
}

static void optional_binary_expect_present(const TbeTypedType *type, const uint8_t *expected,
                                           size_t expected_size) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  OptionalBinaryRecord record;
  uint8_t encoded[OPTIONAL_BINARY_PRESENT_SIZE] = {0};
  size_t queried_size = 0u;
  size_t encoded_size = 0u;

  if (!optional_binary_fill_stale(type, &record)) {
    check(0);
    return;
  }
  record.presence[0] = UINT8_C(0x0f);
  check_equal(tbe_typed_serialize_binary_into(type, &record, NULL, 0u, &queried_size, &error),
              DATA_BIND_ERR_BUFFER_TOO_SMALL);
  check_equal(queried_size, expected_size);
  check_equal(tbe_typed_serialize_binary_into(type, &record, encoded, sizeof(encoded),
                                               &encoded_size, &error),
              DATA_BIND_OK);
  check_equal(encoded_size, expected_size);
  check_equal(encoded, expected, expected_size);

  tbe_typed_clear(type, &record);
}

static void optional_binary_expect_canonical_roundtrip(const TbeTypedType *type,
                                                       const uint8_t *noncanonical,
                                                       size_t noncanonical_size,
                                                       const uint8_t *canonical,
                                                       size_t canonical_size) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  OptionalBinaryRecord decoded;
  uint8_t *encoded = NULL;
  size_t encoded_size = 0u;

  if (tbe_typed_init(type, &decoded, &error) != DATA_BIND_OK) {
    check(0);
    return;
  }
  check_equal(tbe_typed_parse_binary(type, noncanonical, noncanonical_size, &decoded, &error),
              DATA_BIND_OK);
  check_equal(decoded.presence[0], 0u);
  check_equal(decoded.scalar, 0u);
  check_null(decoded.text);
  check_equal(tbe_bytes_t_size(&decoded.bytes), 0u);
  check_equal(optional_binary_child_vec_t_size(&decoded.children), 0u);

  check_equal(tbe_typed_serialize_binary(type, &decoded, &encoded, &encoded_size, &error),
              DATA_BIND_OK);
  check_equal(encoded_size, canonical_size);
  if (encoded != NULL && encoded_size >= canonical_size)
    check_equal(encoded, canonical, canonical_size);

  tbe_typed_serialized_free(encoded);
  tbe_typed_clear(type, &decoded);
}

spec("typed optional binary encoding") {
  static const uint8_t absent_le[] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
                                      0u, 0u, 0u, 2u, 0u, 0u, 0u};
  static const uint8_t absent_be[] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
                                      0u, 0u, 0u, 0u, 2u, 0u, 0u};
  static const uint8_t present_le[] = {
      0x0fu, 0x34u, 0x12u, 2u, 0u, 0u, 0u, 'x', 'y', 2u, 0u,
      0u,    0u,    0xabu, 0xcdu, 2u, 0u, 1u, 0u, 0x78u, 0x56u};
  static const uint8_t present_be[] = {
      0x0fu, 0x12u, 0x34u, 0u, 0u, 0u, 2u, 'x', 'y', 0u, 0u,
      0u,    2u,    0xabu, 0xcdu, 0u, 2u, 0u, 1u, 0x56u, 0x78u};
  static const uint8_t noncanonical_le[] = {
      0u, 0x34u, 0x12u, 2u, 0u, 0u, 0u, 'x', 'y', 2u, 0u,
      0u, 0u, 0xabu, 0xcdu, 2u, 0u, 1u, 0u, 0x78u, 0x56u};
  static const uint8_t noncanonical_be[] = {
      0u, 0x12u, 0x34u, 0u, 0u, 0u, 2u, 'x', 'y', 0u, 0u,
      0u, 2u, 0xabu, 0xcdu, 0u, 2u, 0u, 1u, 0x56u, 0x78u};

  it("omits stale absent storage and retains the host object") {
    optional_binary_expect_absent(&OPTIONAL_BINARY_RECORD_LE, absent_le, sizeof(absent_le));
    optional_binary_expect_absent(&OPTIONAL_BINARY_RECORD_BE, absent_be, sizeof(absent_be));
  }

  it("preserves present optional values in both byte orders") {
    optional_binary_expect_present(&OPTIONAL_BINARY_RECORD_LE, present_le, sizeof(present_le));
    optional_binary_expect_present(&OPTIONAL_BINARY_RECORD_BE, present_be, sizeof(present_be));
  }

  it("canonicalizes absent optional fields after direct binary parsing") {
    optional_binary_expect_canonical_roundtrip(
        &OPTIONAL_BINARY_RECORD_LE, noncanonical_le, sizeof(noncanonical_le), absent_le,
        sizeof(absent_le));
    optional_binary_expect_canonical_roundtrip(
        &OPTIONAL_BINARY_RECORD_BE, noncanonical_be, sizeof(noncanonical_be), absent_be,
        sizeof(absent_be));
  }
}
