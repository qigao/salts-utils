#include "tbe_typed.h"
#include "data_bind_native.h"
#include "tinytest.h"

#include <cmeta/data.h>
#include <salts_cmeta_data.h>
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

enum { NULLABLE_FIELD_COUNT = 4, NULLABLE_NOTE_INDEX = 3,
       NULLABLE_WORKSPACE_BYTES = 4096, NULLABLE_MAX_DEPTH = 8,
       NULLABLE_MAX_ITEMS = 16, NULLABLE_MAX_OWNED_BYTES = 64 };

static const cmeta_type_identity NULLABLE_NATIVE_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.NullableBinary.Record");
static const cmeta_type_desc NULLABLE_NATIVE_TYPE = {
    "NullableBinaryRecord", sizeof(NullableBinaryRecord),
    _Alignof(NullableBinaryRecord), CMETA_T_OBJECT, NULL, NULL, &NULLABLE_NATIVE_ID};
static cmeta_field_desc nullable_layout_fields[] = {
    {"required_value", "uint16_t", offsetof(NullableBinaryRecord, required_value),
     sizeof(uint16_t), _Alignof(uint16_t), &cmeta_type_uint16, NULL},
    {"nullable_value", "uint16_t", offsetof(NullableBinaryRecord, nullable_value),
     sizeof(uint16_t), _Alignof(uint16_t), &cmeta_type_uint16, NULL},
    {"tri_value", "uint16_t", offsetof(NullableBinaryRecord, tri_value),
     sizeof(uint16_t), _Alignof(uint16_t), &cmeta_type_uint16, NULL},
    {"note", "tstr", offsetof(NullableBinaryRecord, note),
     sizeof(tstr), _Alignof(tstr), NULL, NULL}};
static const cmeta_struct_desc NULLABLE_NATIVE_LAYOUT = {
    "NullableBinaryRecord", sizeof(NullableBinaryRecord),
    _Alignof(NullableBinaryRecord), nullable_layout_fields, NULLABLE_FIELD_COUNT};
static cmeta_data_field_desc nullable_native_fields[] = {
    {"test.NullableBinary.required_value", "required_value",
     offsetof(NullableBinaryRecord, required_value), &cmeta_data_uint16},
    {"test.NullableBinary.nullable_value", "nullable_value",
     offsetof(NullableBinaryRecord, nullable_value), &cmeta_data_uint16},
    {"test.NullableBinary.tri_value", "tri_value",
     offsetof(NullableBinaryRecord, tri_value), &cmeta_data_uint16},
    {"test.NullableBinary.note", "note", offsetof(NullableBinaryRecord, note), NULL}};
static const cmeta_data_struct_shape NULLABLE_NATIVE_SHAPE = {
    &NULLABLE_NATIVE_LAYOUT, nullable_native_fields, NULLABLE_FIELD_COUNT};
static const cmeta_data_desc NULLABLE_NATIVE_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.NullableBinary.Record.data", .display_name = "NullableBinaryRecord",
    .kind = CMETA_DATA_STRUCT, .storage_type = &NULLABLE_NATIVE_TYPE,
    .shape = &NULLABLE_NATIVE_SHAPE};

static _Alignas(64) unsigned char nullable_workspace[NULLABLE_WORKSPACE_BYTES];
static DataBindNativeOptions nullable_options;
static DataBindNativeDiagnostic nullable_diagnostic;
static NullableBinaryRecord owned_source;
static NullableBinaryRecord owned_destination;
static uint8_t *owned_wire;

/* Single-threaded native lifecycle owns text and the complete host envelope.
 * Binary owns only wire state. Initialization requires unused storage, and
 * clear runs after parsing has stopped borrowing input or host pointers. */
static DataBindStatus nullable_init(const cmeta_data_desc *data, void *object) {
  return data_bind_native_init(&nullable_options, data, object,
                               data->storage_type->size, &nullable_diagnostic);
}

static DataBindStatus nullable_clear(const cmeta_data_desc *data, void *object) {
  return data_bind_native_clear(&nullable_options, data, object,
                                data->storage_type->size, &nullable_diagnostic);
}

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
     sizeof(uint16_t), _Alignof(uint16_t), &cmeta_type_uint16, NULL},
    {"tri_value", "uint16_t", offsetof(CanonicalBinaryRecord, tri_value),
     sizeof(uint16_t), _Alignof(uint16_t), &cmeta_type_uint16, NULL}};
static const cmeta_struct_desc CANONICAL_BINARY_LAYOUT = {
    "CanonicalBinaryRecord", sizeof(CanonicalBinaryRecord),
    _Alignof(CanonicalBinaryRecord), CANONICAL_BINARY_LAYOUT_FIELDS, 2u};
static const cmeta_data_field_desc CANONICAL_BINARY_DATA_FIELDS[] = {
    {"test.NullableBinary.Canonical.required_value", "required_value",
     offsetof(CanonicalBinaryRecord, required_value), &cmeta_data_uint16},
    {"test.NullableBinary.Canonical.tri_value", "tri_value",
     offsetof(CanonicalBinaryRecord, tri_value), &cmeta_data_uint16}};
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
  before_each() {
    nullable_options = (DataBindNativeOptions)DATA_BIND_NATIVE_OPTIONS_INIT;
    nullable_diagnostic = (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    nullable_options.workspace = nullable_workspace;
    nullable_options.workspace_bytes = sizeof(nullable_workspace);
    nullable_options.max_depth = NULLABLE_MAX_DEPTH;
    nullable_options.max_items = NULLABLE_MAX_ITEMS;
    nullable_options.max_owned_bytes = NULLABLE_MAX_OWNED_BYTES;
    nullable_layout_fields[NULLABLE_NOTE_INDEX].type = salts_tstr_cmeta_data.storage_type;
    nullable_native_fields[NULLABLE_NOTE_INDEX].value = &salts_tstr_cmeta_data;
  }
  after_each() {
    /* Provider cleanup also runs after a fatal assertion with a live owner. */
    (void)cmeta_data_value_restore_zero(&NULLABLE_NATIVE_DATA, &owned_source);
    (void)cmeta_data_value_restore_zero(&NULLABLE_NATIVE_DATA, &owned_destination);
    tbe_typed_serialized_free(owned_wire);
    owned_wire = NULL;
  }

  it("preserves and replaces canonical text ownership through historical Binary decode") {
    static const unsigned char payload[] = "owned";
    static const unsigned char previous[] = "keep";
    DataBindError error = DATA_BIND_ERROR_INIT;
    size_t wire_len = 0u;
    unsigned char before[sizeof(owned_destination)];
    tstr previous_owner;

    check_equal(nullable_init(&NULLABLE_NATIVE_DATA, &owned_source), DATA_BIND_OK);
    check_equal(nullable_init(&NULLABLE_NATIVE_DATA, &owned_destination), DATA_BIND_OK);
    check_equal(cmeta_data_buffer_assign(&salts_tstr_cmeta_data, &owned_source.note,
                payload, sizeof(payload) - 1u, NULLABLE_MAX_OWNED_BYTES), CMETA_OK);
    check_equal(cmeta_data_buffer_assign(&salts_tstr_cmeta_data, &owned_destination.note,
                previous, sizeof(previous) - 1u, NULLABLE_MAX_OWNED_BYTES), CMETA_OK);
    owned_source.required_value = 7u;
    owned_source.tri_value = 9u;
    owned_source.presence = (uint8_t)((1u << 0) | (1u << 1));
    previous_owner = owned_destination.note;
    memcpy(before, &owned_destination, sizeof(before));
    check_equal(tbe_typed_serialize_binary(&NULLABLE_BINARY_TYPE, &owned_source,
                &owned_wire, &wire_len, &error), DATA_BIND_OK);
    check_true(wire_len > 1u);
    check_equal(tbe_typed_parse_binary(&NULLABLE_BINARY_TYPE, owned_wire,
                wire_len - 1u, &owned_destination, &error), DATA_BIND_ERR_PARSE);
    check_equal(&owned_destination, before, sizeof(before));
    check_true(owned_destination.note == previous_owner);
    check_equal(memcmp(owned_destination.note, previous, sizeof(previous) - 1u), 0);

    check_equal(tbe_typed_parse_binary(&NULLABLE_BINARY_TYPE, owned_wire,
                wire_len, &owned_destination, &error), DATA_BIND_OK);
    check_true(owned_destination.note != owned_source.note);
    check_equal(nullable_clear(&NULLABLE_NATIVE_DATA, &owned_source), DATA_BIND_OK);
    tbe_typed_serialized_free(owned_wire);
    owned_wire = NULL;
    check_equal(owned_destination.required_value, 7u);
    check_equal(owned_destination.tri_value, 9u);
    check_equal(tstr_len(owned_destination.note), sizeof(payload) - 1u);
    check_equal(memcmp(owned_destination.note, payload, sizeof(payload) - 1u), 0);
    check_equal(nullable_clear(&NULLABLE_NATIVE_DATA, &owned_destination), DATA_BIND_OK);
    check_equal(nullable_clear(&NULLABLE_NATIVE_DATA, &owned_destination), DATA_BIND_OK);
    check_null(owned_destination.note);
    check_equal(owned_destination.presence, 0u);
    check_equal(owned_destination.nulls, 0u);
  }
  it("round trips dual state bitmaps and NULL variable data") {
    NullableBinaryRecord value;
    NullableBinaryRecord decoded;
    DataBindError error = DATA_BIND_ERROR_INIT;
    uint8_t *wire = NULL;
    size_t wire_len = 0u;

    check_equal(nullable_init(&NULLABLE_NATIVE_DATA, &value),
                DATA_BIND_OK);
    check_equal(nullable_init(&NULLABLE_NATIVE_DATA, &decoded),
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
    check_equal(nullable_clear(&NULLABLE_NATIVE_DATA, &decoded), DATA_BIND_OK);
    check_equal(nullable_clear(&NULLABLE_NATIVE_DATA, &value), DATA_BIND_OK);
  }

  it("rejects impossible ABSENT plus NULL and nonempty NULL tail payload") {
    NullableBinaryRecord value;
    NullableBinaryRecord decoded;
    DataBindError error = DATA_BIND_ERROR_INIT;
    uint8_t *wire = NULL;
    size_t wire_len = 0u;
    uint8_t malformed[13] = {0};

    check_equal(nullable_init(&NULLABLE_NATIVE_DATA, &value),
                DATA_BIND_OK);
    value.required_value = 1u;
    value.nulls = (uint8_t)(1u << 1); /* tri_value NULL while absent */

    check_equal(
        tbe_typed_serialize_binary(&NULLABLE_BINARY_TYPE, &value,
                                   &wire, &wire_len, &error),
        DATA_BIND_ERR_SCHEMA);
    check_null(wire);
    check_equal(wire_len, 0u);

    check_equal(nullable_init(&NULLABLE_NATIVE_DATA, &decoded),
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

    check_equal(nullable_clear(&NULLABLE_NATIVE_DATA, &decoded), DATA_BIND_OK);
    check_equal(nullable_clear(&NULLABLE_NATIVE_DATA, &value), DATA_BIND_OK);
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
        nullable_init(&CANONICAL_BINARY_DATA, &value),
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
        nullable_init(&CANONICAL_BINARY_DATA, &decoded),
        DATA_BIND_OK);
    check_equal(
        tbe_typed_descriptor_parse_binary(
            codec, "Canonical", &CANONICAL_BINARY_DESCRIPTOR,
            wire, wire_len, &decoded, &error),
        DATA_BIND_OK);
    check_equal(decoded.required_value, 7u);
    check_equal(decoded.tri_value, 0u);
    check_equal(decoded.presence, 1u);
    check_equal(decoded.nulls, 1u);

    tbe_typed_serialized_free(wire);
    check_equal(
        nullable_clear(&CANONICAL_BINARY_DATA, &decoded),
        DATA_BIND_OK);
    check_equal(
        nullable_clear(&CANONICAL_BINARY_DATA, &value),
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

    uint8_t guarded[sizeof(CanonicalBinaryRecord)];
    uint8_t before[sizeof(guarded)];
    memset(guarded, 0xa5, sizeof(guarded));
    memcpy(before, guarded, sizeof(before));
    check_equal(tbe_typed_descriptor_serialize_binary_into(
                    &CANONICAL_BINARY_DESCRIPTOR, &value, guarded,
                    sizeof(guarded), &wire_len, &error), DATA_BIND_ERR_SCHEMA);
    check_equal(wire_len, CANONICAL_BINARY_OVERLAY.fixed_block_size);
    check_equal(guarded, before, sizeof(before));
  }

  it("keeps fixed native storage unchanged on Binary size and state rejection") {
    static const char schema[] = "message Canonical { uint16 required_value; optional nullable uint16 tri_value; }";
    enum { WIRE_BYTES = 6, TRAILING_BYTES = WIRE_BYTES + 1 };
    static const uint8_t invalid_state[WIRE_BYTES] = {0u, 1u, 7u, 0u, 0u, 0u};
    static const uint8_t valid_with_trailing[TRAILING_BYTES] = {1u, 0u, 7u, 0u, 9u, 0u, 0u};
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    CanonicalBinaryRecord value = {0};
    uint8_t before[sizeof(value)];
    check_equal(data_bind_create_from_text(schema, sizeof(schema) - 1u, &codec, &error), DATA_BIND_OK);
    check_equal(nullable_init(&CANONICAL_BINARY_DATA, &value), DATA_BIND_OK);
    memcpy(before, &value, sizeof(before));
    check_equal(tbe_typed_descriptor_parse_binary(codec, "Canonical", &CANONICAL_BINARY_DESCRIPTOR,
                                                  valid_with_trailing, WIRE_BYTES - 1u, &value, &error),
                DATA_BIND_ERR_PARSE);
    check_equal(&value, before, sizeof(before));
    check_equal(tbe_typed_descriptor_parse_binary(codec, "Canonical", &CANONICAL_BINARY_DESCRIPTOR,
                                                  valid_with_trailing, sizeof(valid_with_trailing), &value, &error),
                DATA_BIND_ERR_PARSE);
    check_equal(&value, before, sizeof(before));
    check_equal(tbe_typed_descriptor_parse_binary(codec, "Canonical", &CANONICAL_BINARY_DESCRIPTOR,
                                                  invalid_state, sizeof(invalid_state), &value, &error),
                DATA_BIND_ERR_SCHEMA);
    check_equal(&value, before, sizeof(before));
    check_equal(nullable_clear(&CANONICAL_BINARY_DATA, &value), DATA_BIND_OK);
    data_bind_free(codec);
  }
}
