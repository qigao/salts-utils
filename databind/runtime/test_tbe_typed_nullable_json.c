#include "tbe_typed.h"
#include "tinytest.h"

#include <cmeta/data.h>
#include <cmeta/data.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct NullableJsonRecord {
  uint32_t required_value;
  uint32_t optional_value;
  uint32_t nullable_value;
  uint32_t tri_value;
  uint32_t defaulted_value;
  uint8_t presence;
  uint8_t nulls;
} NullableJsonRecord;

static const TbeTypedField NULLABLE_JSON_FIELDS[] = {
    {.name = "required_value",
     .kind = TBE_TYPED_U32,
     .wire_kind = TBE_TYPED_U32,
     .offset = offsetof(NullableJsonRecord, required_value)},
    {.name = "optional_value",
     .kind = TBE_TYPED_U32,
     .wire_kind = TBE_TYPED_U32,
     .offset = offsetof(NullableJsonRecord, optional_value),
     .optional_bit = 0u,
     .flags = TBE_TYPED_FIELD_OPTIONAL},
    {.name = "nullable_value",
     .kind = TBE_TYPED_U32,
     .wire_kind = TBE_TYPED_U32,
     .offset = offsetof(NullableJsonRecord, nullable_value),
     .flags = TBE_TYPED_FIELD_NULLABLE,
     .nullable_bit = 0u},
    {.name = "tri_value",
     .kind = TBE_TYPED_U32,
     .wire_kind = TBE_TYPED_U32,
     .offset = offsetof(NullableJsonRecord, tri_value),
     .optional_bit = 1u,
     .flags = TBE_TYPED_FIELD_OPTIONAL | TBE_TYPED_FIELD_NULLABLE,
     .nullable_bit = 1u},
    {.name = "defaulted_value",
     .kind = TBE_TYPED_U32,
     .wire_kind = TBE_TYPED_U32,
     .offset = offsetof(NullableJsonRecord, defaulted_value),
     .optional_bit = 2u,
     .flags = TBE_TYPED_FIELD_OPTIONAL | TBE_TYPED_FIELD_NULLABLE,
     .nullable_bit = 2u}};

static const TbeTypedType NULLABLE_JSON_TYPE = {
    .name = "Record",
    .size = sizeof(NullableJsonRecord),
    .fields = NULLABLE_JSON_FIELDS,
    .field_count = sizeof(NULLABLE_JSON_FIELDS) / sizeof(NULLABLE_JSON_FIELDS[0]),
    .fixed_block_size = 0u,
    .presence_offset = offsetof(NullableJsonRecord, presence),
    .presence_size = 1u,
    .wire_big_endian = 0,
    .null_offset = offsetof(NullableJsonRecord, nulls),
    .null_size = 1u};

static const cmeta_type_identity NULLABLE_JSON_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.NullableJson.Record");
static const cmeta_type_desc NULLABLE_JSON_CTYPE = {
    "NullableJsonRecord", sizeof(NullableJsonRecord), _Alignof(NullableJsonRecord),
    CMETA_T_OBJECT, NULL, NULL, &NULLABLE_JSON_ID};

static const cmeta_field_desc NULLABLE_JSON_LAYOUT_FIELDS[] = {
    {"required_value", "uint32_t", offsetof(NullableJsonRecord, required_value),
     sizeof(uint32_t), _Alignof(uint32_t), &cmeta_type_uint32, NULL},
    {"optional_value", "uint32_t", offsetof(NullableJsonRecord, optional_value),
     sizeof(uint32_t), _Alignof(uint32_t), &cmeta_type_uint32, NULL},
    {"nullable_value", "uint32_t", offsetof(NullableJsonRecord, nullable_value),
     sizeof(uint32_t), _Alignof(uint32_t), &cmeta_type_uint32, NULL},
    {"tri_value", "uint32_t", offsetof(NullableJsonRecord, tri_value),
     sizeof(uint32_t), _Alignof(uint32_t), &cmeta_type_uint32, NULL},
    {"defaulted_value", "uint32_t", offsetof(NullableJsonRecord, defaulted_value),
     sizeof(uint32_t), _Alignof(uint32_t), &cmeta_type_uint32, NULL}};
static const cmeta_struct_desc NULLABLE_JSON_LAYOUT = {
    "NullableJsonRecord", sizeof(NullableJsonRecord), _Alignof(NullableJsonRecord),
    NULLABLE_JSON_LAYOUT_FIELDS,
    sizeof(NULLABLE_JSON_LAYOUT_FIELDS) / sizeof(NULLABLE_JSON_LAYOUT_FIELDS[0])};
static const cmeta_data_field_desc NULLABLE_JSON_DATA_FIELDS[] = {
    {"test.NullableJson.Record.required_value", "required_value",
     offsetof(NullableJsonRecord, required_value), &cmeta_data_uint32},
    {"test.NullableJson.Record.optional_value", "optional_value",
     offsetof(NullableJsonRecord, optional_value), &cmeta_data_uint32},
    {"test.NullableJson.Record.nullable_value", "nullable_value",
     offsetof(NullableJsonRecord, nullable_value), &cmeta_data_uint32},
    {"test.NullableJson.Record.tri_value", "tri_value",
     offsetof(NullableJsonRecord, tri_value), &cmeta_data_uint32},
    {"test.NullableJson.Record.defaulted_value", "defaulted_value",
     offsetof(NullableJsonRecord, defaulted_value), &cmeta_data_uint32}};
static const cmeta_data_struct_shape NULLABLE_JSON_SHAPE = {
    &NULLABLE_JSON_LAYOUT,
    NULLABLE_JSON_DATA_FIELDS,
    sizeof(NULLABLE_JSON_DATA_FIELDS) / sizeof(NULLABLE_JSON_DATA_FIELDS[0])};
static const cmeta_data_desc NULLABLE_JSON_DATA = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.NullableJson.Record.data", "NullableJsonRecord", CMETA_DATA_STRUCT,
    &NULLABLE_JSON_CTYPE, &NULLABLE_JSON_SHAPE, NULL, NULL, NULL};

static const TbeTypedDescriptor NULLABLE_JSON_DESCRIPTOR =
    TBE_TYPED_DESCRIPTOR_INIT(&NULLABLE_JSON_TYPE, &NULLABLE_JSON_DATA);

static DataBind *nullable_json_codec(void) {
  static const char schema[] =
      "schema NullableJson [version(1)];"
      "message Record {"
      " uint32 required_value;"
      " optional uint32 optional_value;"
      " nullable uint32 nullable_value;"
      " optional nullable uint32 tri_value;"
      " optional nullable uint32 defaulted_value default 7;"
      "}";
  DataBind *codec = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;
  return data_bind_create_from_text(schema, sizeof(schema) - 1u, &codec, &error) ==
                 DATA_BIND_OK
             ? codec
             : NULL;
}

static void check_null_state(const NullableJsonRecord *record) {
  check_equal(record->required_value, 1u);
  check_equal(record->optional_value, 0u);
  check_equal(record->nullable_value, 0u);
  check_equal(record->tri_value, 0u);
  check_equal(record->defaulted_value, 7u);
  check_equal(record->presence, (uint8_t)((1u << 1) | (1u << 2)));
  check_equal(record->nulls, (uint8_t)((1u << 0) | (1u << 1)));
}

static void check_json_shape(const char *json) {
  check_not_null(json);
  if (!json) return;
  check_contains(json, "\"required_value\":1");
  check(strstr(json, "\"optional_value\"") == NULL);
  check_contains(json, "\"nullable_value\":null");
  check_contains(json, "\"tri_value\":null");
  check_contains(json, "\"defaulted_value\":7");
}

spec("typed nullable JSON") {
  it("preserves ABSENT NULL VALUE and defaults through the ordinary typed path") {
    static const char input[] =
        "{\"required_value\":1,\"nullable_value\":null,\"tri_value\":null}";
    DataBind *codec = nullable_json_codec();
    NullableJsonRecord record;
    DataBindError error = DATA_BIND_ERROR_INIT;
    char *json = NULL;
    size_t json_len = 0u;

    check_not_null(codec);
    if (!codec) return;

    check_equal(tbe_typed_init(&NULLABLE_JSON_TYPE, &record, &error), DATA_BIND_OK);
    check_equal(
        tbe_typed_parse_ex(codec, "Record", &NULLABLE_JSON_TYPE,
                           DATA_BIND_FORMAT_JSON, input, sizeof(input) - 1u,
                           0u, &record, &error),
        DATA_BIND_OK);
    check_null_state(&record);

    check_equal(
        tbe_typed_serialize_ex(codec, "Record", &NULLABLE_JSON_TYPE, &record,
                               DATA_BIND_FORMAT_JSON, &json, &json_len, &error),
        DATA_BIND_OK);
    check(json_len != 0u);
    check_json_shape(json);

    tbe_typed_serialized_free(json);
    tbe_typed_clear(&NULLABLE_JSON_TYPE, &record);
    data_bind_free(codec);
  }

  it("preserves the same states through the canonical CMeta descriptor path") {
    static const char input[] =
        "{\"required_value\":1,\"nullable_value\":null,\"tri_value\":null}";
    DataBind *codec = nullable_json_codec();
    NullableJsonRecord record;
    DataBindError error = DATA_BIND_ERROR_INIT;
    char *json = NULL;
    size_t json_len = 0u;

    check_not_null(codec);
    if (!codec) return;

    check_equal(
        tbe_typed_descriptor_init(&NULLABLE_JSON_DESCRIPTOR, &record, &error),
        DATA_BIND_OK);
    check_equal(
        tbe_typed_descriptor_parse(
            codec, "Record", &NULLABLE_JSON_DESCRIPTOR, DATA_BIND_FORMAT_JSON,
            input, sizeof(input) - 1u, 0u, &record, &error),
        DATA_BIND_OK);
    check_null_state(&record);

    check_equal(
        tbe_typed_descriptor_serialize(
            codec, "Record", &NULLABLE_JSON_DESCRIPTOR, &record,
            DATA_BIND_FORMAT_JSON, &json, &json_len, &error),
        DATA_BIND_OK);
    check(json_len != 0u);
    check_json_shape(json);

    tbe_typed_serialized_free(json);
    check_equal(
        tbe_typed_descriptor_clear(&NULLABLE_JSON_DESCRIPTOR, &record, &error),
        DATA_BIND_OK);
    data_bind_free(codec);
  }

  it("does not replace explicit null with a default") {
    static const char input[] =
        "{\"required_value\":1,\"nullable_value\":2,"
        "\"defaulted_value\":null}";
    DataBind *codec = nullable_json_codec();
    NullableJsonRecord record;
    DataBindError error = DATA_BIND_ERROR_INIT;
    char *json = NULL;
    size_t json_len = 0u;

    check_not_null(codec);
    if (!codec) return;

    check_equal(tbe_typed_init(&NULLABLE_JSON_TYPE, &record, &error), DATA_BIND_OK);
    check_equal(
        tbe_typed_parse_ex(codec, "Record", &NULLABLE_JSON_TYPE,
                           DATA_BIND_FORMAT_JSON, input, sizeof(input) - 1u,
                           0u, &record, &error),
        DATA_BIND_OK);
    check_equal(record.defaulted_value, 0u);
    check((record.presence & (uint8_t)(1u << 2)) != 0u);
    check((record.nulls & (uint8_t)(1u << 2)) != 0u);

    check_equal(
        tbe_typed_serialize_ex(codec, "Record", &NULLABLE_JSON_TYPE, &record,
                               DATA_BIND_FORMAT_JSON, &json, &json_len, &error),
        DATA_BIND_OK);
    check_contains(json, "\"defaulted_value\":null");

    tbe_typed_serialized_free(json);
    tbe_typed_clear(&NULLABLE_JSON_TYPE, &record);
    data_bind_free(codec);
  }

  it("rejects explicit null for non-null fields without publishing partial state") {
    static const char input[] =
        "{\"required_value\":null,\"nullable_value\":2}";
    DataBind *codec = nullable_json_codec();
    NullableJsonRecord record;
    NullableJsonRecord before;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_not_null(codec);
    if (!codec) return;

    memset(&record, 0x5a, sizeof(record));
    before = record;
    check_not_equal(
        tbe_typed_parse_ex(codec, "Record", &NULLABLE_JSON_TYPE,
                           DATA_BIND_FORMAT_JSON, input, sizeof(input) - 1u,
                           0u, &record, &error),
        DATA_BIND_OK);
    check_equal(memcmp(&record, &before, sizeof(record)), 0);

    data_bind_free(codec);
  }

  it("rejects impossible ABSENT plus NULL native state") {
    DataBind *codec = nullable_json_codec();
    NullableJsonRecord record = {0};
    DataBindError error = DATA_BIND_ERROR_INIT;
    char *json = NULL;
    size_t json_len = 0u;

    check_not_null(codec);
    if (!codec) return;

    record.required_value = 1u;
    record.nullable_value = 2u;
    record.nulls = (uint8_t)(1u << 1); /* tri_value null while absent */

    check_equal(
        tbe_typed_serialize_ex(codec, "Record", &NULLABLE_JSON_TYPE, &record,
                               DATA_BIND_FORMAT_JSON, &json, &json_len, &error),
        DATA_BIND_ERR_SCHEMA);
    check_null(json);
    check_equal(json_len, 0u);
    check_contains(error.message, "absent");

    data_bind_free(codec);
  }

  it("preserves the same tri-state contract through YAML on both typed paths") {
    static const char input[] =
        "required_value: 1\n"
        "nullable_value: null\n"
        "tri_value: ~\n";
    DataBind *codec = nullable_json_codec();
    NullableJsonRecord ordinary;
    NullableJsonRecord canonical;
    NullableJsonRecord roundtrip;
    DataBindError error = DATA_BIND_ERROR_INIT;
    char *yaml = NULL;
    size_t yaml_len = 0u;

    check_not_null(codec);
    if (!codec) return;

    check_equal(tbe_typed_init(&NULLABLE_JSON_TYPE, &ordinary, &error),
                DATA_BIND_OK);
    check_equal(
        tbe_typed_parse_ex(codec, "Record", &NULLABLE_JSON_TYPE,
                           DATA_BIND_FORMAT_YAML, input, sizeof(input) - 1u,
                           0u, &ordinary, &error),
        DATA_BIND_OK);
    check_null_state(&ordinary);

    check_equal(
        tbe_typed_serialize_ex(codec, "Record", &NULLABLE_JSON_TYPE, &ordinary,
                               DATA_BIND_FORMAT_YAML, &yaml, &yaml_len, &error),
        DATA_BIND_OK);
    check_not_null(yaml);
    check(yaml_len != 0u);

    check_equal(tbe_typed_init(&NULLABLE_JSON_TYPE, &roundtrip, &error),
                DATA_BIND_OK);
    check_equal(
        tbe_typed_parse_ex(codec, "Record", &NULLABLE_JSON_TYPE,
                           DATA_BIND_FORMAT_YAML, yaml, yaml_len, 0u,
                           &roundtrip, &error),
        DATA_BIND_OK);
    check_null_state(&roundtrip);
    tbe_typed_clear(&NULLABLE_JSON_TYPE, &roundtrip);
    tbe_typed_serialized_free(yaml);
    yaml = NULL;
    yaml_len = 0u;

    check_equal(
        tbe_typed_descriptor_init(&NULLABLE_JSON_DESCRIPTOR, &canonical, &error),
        DATA_BIND_OK);
    check_equal(
        tbe_typed_descriptor_parse(
            codec, "Record", &NULLABLE_JSON_DESCRIPTOR, DATA_BIND_FORMAT_YAML,
            input, sizeof(input) - 1u, 0u, &canonical, &error),
        DATA_BIND_OK);
    check_null_state(&canonical);

    check_equal(
        tbe_typed_descriptor_serialize(
            codec, "Record", &NULLABLE_JSON_DESCRIPTOR, &canonical,
            DATA_BIND_FORMAT_YAML, &yaml, &yaml_len, &error),
        DATA_BIND_OK);
    check_not_null(yaml);
    check(yaml_len != 0u);

    check_equal(tbe_typed_init(&NULLABLE_JSON_TYPE, &roundtrip, &error),
                DATA_BIND_OK);
    check_equal(
        tbe_typed_parse_ex(codec, "Record", &NULLABLE_JSON_TYPE,
                           DATA_BIND_FORMAT_YAML, yaml, yaml_len, 0u,
                           &roundtrip, &error),
        DATA_BIND_OK);
    check_null_state(&roundtrip);

    tbe_typed_clear(&NULLABLE_JSON_TYPE, &roundtrip);
    tbe_typed_serialized_free(yaml);
    check_equal(
        tbe_typed_descriptor_clear(&NULLABLE_JSON_DESCRIPTOR, &canonical, &error),
        DATA_BIND_OK);
    tbe_typed_clear(&NULLABLE_JSON_TYPE, &ordinary);
    data_bind_free(codec);
  }

  it("keeps CSV and XML nullable formats explicitly unsupported") {
    static const char text[] = "{}";
    DataBind *codec = nullable_json_codec();
    NullableJsonRecord record = {0};
    DataBindError error = DATA_BIND_ERROR_INIT;
    char *out = NULL;
    size_t out_len = 0u;

    check_not_null(codec);
    if (!codec) return;

    check_equal(
        tbe_typed_parse_ex(codec, "Record", &NULLABLE_JSON_TYPE,
                           DATA_BIND_FORMAT_CSV, text, sizeof(text) - 1u,
                           0u, &record, &error),
        DATA_BIND_ERR_SCHEMA);
    check_contains(error.message, "nullable");

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        tbe_typed_serialize_ex(codec, "Record", &NULLABLE_JSON_TYPE, &record,
                               DATA_BIND_FORMAT_XML, &out, &out_len, &error),
        DATA_BIND_ERR_SCHEMA);
    check_null(out);
    check_equal(out_len, 0u);

    data_bind_free(codec);
  }
}
