#include "tbe_typed.h"
#include "tinytest.h"

#include <cmeta/data.h>
#include <salts_cmeta_fixed_width.h>
#include <tstr.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct NullableBinaryRecord {
  uint16_t required_value;
  uint16_t nullable_value;
  uint16_t tri_value;
  tstr note;
  uint8_t presence;
  uint8_t nulls;
} NullableBinaryRecord;

static const TbeTypedField NULLABLE_BINARY_FIELDS[] = {
    {.name = "required_value",
     .kind = TBE_TYPED_U16,
     .wire_kind = TBE_TYPED_U16,
     .offset = offsetof(NullableBinaryRecord, required_value),
     .wire_offset = 2u,
     .wire_size = 2u,
     .flags = TBE_TYPED_FIELD_WIRE_OFFSET},
    {.name = "nullable_value",
     .kind = TBE_TYPED_U16,
     .wire_kind = TBE_TYPED_U16,
     .offset = offsetof(NullableBinaryRecord, nullable_value),
     .wire_offset = 4u,
     .wire_size = 2u,
     .flags = TBE_TYPED_FIELD_WIRE_OFFSET | TBE_TYPED_FIELD_NULLABLE,
     .nullable_bit = 0u},
    {.name = "tri_value",
     .kind = TBE_TYPED_U16,
     .wire_kind = TBE_TYPED_U16,
     .offset = offsetof(NullableBinaryRecord, tri_value),
     .wire_offset = 6u,
     .wire_size = 2u,
     .optional_bit = 0u,
     .flags = TBE_TYPED_FIELD_WIRE_OFFSET | TBE_TYPED_FIELD_OPTIONAL |
              TBE_TYPED_FIELD_NULLABLE,
     .nullable_bit = 1u},
    {.name = "note",
     .kind = TBE_TYPED_STRING,
     .wire_kind = TBE_TYPED_STRING,
     .offset = offsetof(NullableBinaryRecord, note),
     .optional_bit = 1u,
     .flags = TBE_TYPED_FIELD_OPTIONAL | TBE_TYPED_FIELD_NULLABLE |
              TBE_TYPED_FIELD_VAR_DATA,
     .nullable_bit = 2u}};

static const TbeTypedType NULLABLE_BINARY_TYPE = {
    .name = "NullableBinary",
    .size = sizeof(NullableBinaryRecord),
    .fields = NULLABLE_BINARY_FIELDS,
    .field_count = sizeof(NULLABLE_BINARY_FIELDS) / sizeof(NULLABLE_BINARY_FIELDS[0]),
    .fixed_block_size = 8u,
    .presence_offset = offsetof(NullableBinaryRecord, presence),
    .presence_size = 1u,
    .wire_big_endian = 0,
    .null_offset = offsetof(NullableBinaryRecord, nulls),
    .null_size = 1u};

typedef struct CanonicalBinaryRecord {
  uint16_t required_value;
  uint16_t tri_value;
  uint8_t presence;
  uint8_t nulls;
} CanonicalBinaryRecord;

static const cmeta_type_identity CANONICAL_BINARY_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.NullableBinary.Canonical");
static const cmeta_type_desc CANONICAL_BINARY_CTYPE = {
    "CanonicalBinaryRecord", sizeof(CanonicalBinaryRecord),
    _Alignof(CanonicalBinaryRecord), CMETA_T_OBJECT,
    NULL, NULL, &CANONICAL_BINARY_ID};
static const cmeta_field_desc CANONICAL_BINARY_LAYOUT_FIELDS[] = {
    {"required_value", "uint16_t", offsetof(CanonicalBinaryRecord, required_value),
     sizeof(uint16_t), _Alignof(uint16_t), &salts_uint16_cmeta_type, NULL},
    {"tri_value", "uint16_t", offsetof(CanonicalBinaryRecord, tri_value),
     sizeof(uint16_t), _Alignof(uint16_t), &salts_uint16_cmeta_type, NULL}};
static const cmeta_struct_desc CANONICAL_BINARY_LAYOUT = {
    "CanonicalBinaryRecord", sizeof(CanonicalBinaryRecord),
    _Alignof(CanonicalBinaryRecord), CANONICAL_BINARY_LAYOUT_FIELDS, 2u};
static const cmeta_data_field_desc CANONICAL_BINARY_DATA_FIELDS[] = {
    {"test.NullableBinary.Canonical.required_value", "required_value",
     offsetof(CanonicalBinaryRecord, required_value), &salts_uint16_cmeta_data},
    {"test.NullableBinary.Canonical.tri_value", "tri_value",
     offsetof(CanonicalBinaryRecord, tri_value), &salts_uint16_cmeta_data}};
static const cmeta_data_struct_shape CANONICAL_BINARY_SHAPE = {
    &CANONICAL_BINARY_LAYOUT, CANONICAL_BINARY_DATA_FIELDS, 2u};
static const cmeta_data_desc CANONICAL_BINARY_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.NullableBinary.Canonical.data",
    .display_name = "CanonicalBinaryRecord",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &CANONICAL_BINARY_CTYPE,
    .shape = &CANONICAL_BINARY_SHAPE};

static const TbeTypedField CANONICAL_BINARY_FIELDS[] = {
    {.name = "required_value",
     .kind = TBE_TYPED_U16,
     .wire_kind = TBE_TYPED_U16,
     .offset = offsetof(CanonicalBinaryRecord, required_value),
     .wire_offset = 2u,
     .wire_size = 2u,
     .flags = TBE_TYPED_FIELD_WIRE_OFFSET},
    {.name = "tri_value",
     .kind = TBE_TYPED_U16,
     .wire_kind = TBE_TYPED_U16,
     .offset = offsetof(CanonicalBinaryRecord, tri_value),
     .wire_offset = 4u,
     .wire_size = 2u,
     .optional_bit = 0u,
     .flags = TBE_TYPED_FIELD_WIRE_OFFSET | TBE_TYPED_FIELD_OPTIONAL |
              TBE_TYPED_FIELD_NULLABLE,
     .nullable_bit = 0u}};
static const TbeTypedType CANONICAL_BINARY_OVERLAY = {
    .name = "Canonical",
    .size = sizeof(CanonicalBinaryRecord),
    .fields = CANONICAL_BINARY_FIELDS,
    .field_count = 2u,
    .fixed_block_size = 6u,
    .presence_offset = offsetof(CanonicalBinaryRecord, presence),
    .presence_size = 1u,
    .wire_big_endian = 0,
    .null_offset = offsetof(CanonicalBinaryRecord, nulls),
    .null_size = 1u};
static const TbeTypedDescriptor CANONICAL_BINARY_DESCRIPTOR =
    TBE_TYPED_DESCRIPTOR_INIT(&CANONICAL_BINARY_OVERLAY, &CANONICAL_BINARY_DATA);

static DataBind *canonical_binary_codec(void) {
  static const char schema[] =
      "schema NullableBinary [version(1)];"
      "message Canonical {"
      " uint16 required_value;"
      " optional nullable uint16 tri_value;"
      "}";
  DataBind *codec = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;
  return data_bind_create_from_text(schema, sizeof(schema) - 1u, &codec, &error) ==
                 DATA_BIND_OK
             ? codec
             : NULL;
}

spec("typed nullable TBE binary") {
  it("round trips dual state bitmaps and NULL variable data") {
    NullableBinaryRecord value;
    NullableBinaryRecord decoded;
    DataBindError error = DATA_BIND_ERROR_INIT;
    uint8_t *wire = NULL;
    size_t wire_len = 0u;

    check_equal(tbe_typed_init(&NULLABLE_BINARY_TYPE, &value, &error),
                DATA_BIND_OK);
    check_equal(tbe_typed_init(&NULLABLE_BINARY_TYPE, &decoded, &error),
                DATA_BIND_OK);

    value.required_value = 7u;
    value.nullable_value = 123u;
    value.tri_value = 9u;
    value.presence = (uint8_t)((1u << 0) | (1u << 1));
    value.nulls = (uint8_t)((1u << 0) | (1u << 2));

    check_equal(
        tbe_typed_serialize_binary(&NULLABLE_BINARY_TYPE, &value,
                                   &wire, &wire_len, &error),
        DATA_BIND_OK);
    check_not_null(wire);
    check_equal(wire_len, (size_t)12u);
    if (wire != NULL && wire_len == 12u) {
      check_equal(wire[0], value.presence);
      check_equal(wire[1], value.nulls);
      check_equal(wire[2], 7u);
      check_equal(wire[3], 0u);
      check_equal(wire[4], 0u);
      check_equal(wire[5], 0u);
      check_equal(wire[6], 9u);
      check_equal(wire[7], 0u);
      check_equal(wire[8], 0u);
      check_equal(wire[9], 0u);
      check_equal(wire[10], 0u);
      check_equal(wire[11], 0u);
    }

    check_equal(
        tbe_typed_parse_binary(&NULLABLE_BINARY_TYPE, wire, wire_len,
                               &decoded, &error),
        DATA_BIND_OK);
    check_equal(decoded.required_value, 7u);
    check_equal(decoded.nullable_value, 0u);
    check_equal(decoded.tri_value, 9u);
    check_null(decoded.note);
    check_equal(decoded.presence, value.presence);
    check_equal(decoded.nulls, value.nulls);

    tbe_typed_serialized_free(wire);
    tbe_typed_clear(&NULLABLE_BINARY_TYPE, &decoded);
    tbe_typed_clear(&NULLABLE_BINARY_TYPE, &value);
  }

  it("rejects impossible ABSENT plus NULL and nonempty NULL tail payload") {
    NullableBinaryRecord value;
    NullableBinaryRecord decoded;
    DataBindError error = DATA_BIND_ERROR_INIT;
    uint8_t *wire = NULL;
    size_t wire_len = 0u;
    uint8_t malformed[13] = {0};

    check_equal(tbe_typed_init(&NULLABLE_BINARY_TYPE, &value, &error),
                DATA_BIND_OK);
    value.required_value = 1u;
    value.nulls = (uint8_t)(1u << 1); /* tri_value NULL while absent */

    check_equal(
        tbe_typed_serialize_binary(&NULLABLE_BINARY_TYPE, &value,
                                   &wire, &wire_len, &error),
        DATA_BIND_ERR_SCHEMA);
    check_null(wire);
    check_equal(wire_len, 0u);

    check_equal(tbe_typed_init(&NULLABLE_BINARY_TYPE, &decoded, &error),
                DATA_BIND_OK);
    malformed[0] = (uint8_t)(1u << 1); /* note present */
    malformed[1] = (uint8_t)(1u << 2); /* note NULL */
    malformed[2] = 1u;                 /* required_value */
    malformed[8] = 1u;                 /* tail length = 1 */
    malformed[12] = 'x';
    check_equal(
        tbe_typed_parse_binary(&NULLABLE_BINARY_TYPE,
                               malformed, sizeof(malformed),
                               &decoded, &error),
        DATA_BIND_ERR_PARSE);
    check_contains(error.message, "NULL");

    tbe_typed_clear(&NULLABLE_BINARY_TYPE, &decoded);
    tbe_typed_clear(&NULLABLE_BINARY_TYPE, &value);
  }

  it("round trips the same fixed state through the canonical CMeta descriptor") {
    DataBind *codec = canonical_binary_codec();
    CanonicalBinaryRecord value = {0};
    CanonicalBinaryRecord decoded = {0};
    DataBindError error = DATA_BIND_ERROR_INIT;
    uint8_t *wire = NULL;
    size_t wire_len = 0u;

    check_not_null(codec);
    if (!codec) return;

    check_equal(
        tbe_typed_descriptor_init(&CANONICAL_BINARY_DESCRIPTOR, &value, &error),
        DATA_BIND_OK);
    value.required_value = 7u;
    value.tri_value = 99u;
    value.presence = 1u;
    value.nulls = 1u;

    check_equal(
        tbe_typed_descriptor_serialize_binary(
            &CANONICAL_BINARY_DESCRIPTOR, &value, &wire, &wire_len, &error),
        DATA_BIND_OK);
    check_not_null(wire);
    check_equal(wire_len, (size_t)6u);
    if (wire != NULL && wire_len == 6u) {
      check_equal(wire[0], 1u);
      check_equal(wire[1], 1u);
      check_equal(wire[2], 7u);
      check_equal(wire[3], 0u);
      check_equal(wire[4], 0u);
      check_equal(wire[5], 0u);
    }

    check_equal(
        tbe_typed_descriptor_init(&CANONICAL_BINARY_DESCRIPTOR, &decoded, &error),
        DATA_BIND_OK);
    check_equal(
        tbe_typed_descriptor_parse(
            codec, "Canonical", &CANONICAL_BINARY_DESCRIPTOR,
            DATA_BIND_FORMAT_BINARY, wire, wire_len, 0u, &decoded, &error),
        DATA_BIND_OK);
    check_equal(decoded.required_value, 7u);
    check_equal(decoded.tri_value, 0u);
    check_equal(decoded.presence, 1u);
    check_equal(decoded.nulls, 1u);

    tbe_typed_serialized_free(wire);
    check_equal(
        tbe_typed_descriptor_clear(&CANONICAL_BINARY_DESCRIPTOR, &decoded, &error),
        DATA_BIND_OK);
    check_equal(
        tbe_typed_descriptor_clear(&CANONICAL_BINARY_DESCRIPTOR, &value, &error),
        DATA_BIND_OK);
    data_bind_free(codec);
  }

  it("rejects impossible canonical CMeta state before binary publication") {
    CanonicalBinaryRecord value = {0};
    DataBindError error = DATA_BIND_ERROR_INIT;
    uint8_t *wire = NULL;
    size_t wire_len = 0u;

    value.required_value = 1u;
    value.nulls = 1u;

    check_equal(
        tbe_typed_descriptor_serialize_binary(
            &CANONICAL_BINARY_DESCRIPTOR, &value, &wire, &wire_len, &error),
        DATA_BIND_ERR_SCHEMA);
    check_null(wire);
    check_equal(wire_len, 0u);
    check_contains(error.message, "absent");
  }
}
