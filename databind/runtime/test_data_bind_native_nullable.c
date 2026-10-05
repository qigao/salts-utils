#include "data_bind_message_plan.h"
#include "data_bind_format_provider.h"
#include "data_bind_projection_plan.h"
#include "tinytest.h"

#include <salts_cmeta_data.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct NullableJsonRecord {
  uint32_t required_value;
  uint32_t optional_value;
  uint32_t nullable_value;
  uint32_t tri_value;
  uint32_t defaulted_value;
  tstr owned;
  uint8_t presence;
  uint8_t nulls;
} NullableJsonRecord;

static const cmeta_type_identity NULLABLE_JSON_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.NullableJson.Record");
static const cmeta_type_desc NULLABLE_JSON_CTYPE = {
    "NullableJsonRecord", sizeof(NullableJsonRecord), _Alignof(NullableJsonRecord),
    CMETA_T_OBJECT, NULL, NULL, &NULLABLE_JSON_ID};

static cmeta_field_desc NULLABLE_JSON_LAYOUT_FIELDS[] = {
    {"required_value", "uint32_t", offsetof(NullableJsonRecord, required_value),
     sizeof(uint32_t), _Alignof(uint32_t), &cmeta_type_uint32, NULL},
    {"optional_value", "uint32_t", offsetof(NullableJsonRecord, optional_value),
     sizeof(uint32_t), _Alignof(uint32_t), &cmeta_type_uint32, NULL},
    {"nullable_value", "uint32_t", offsetof(NullableJsonRecord, nullable_value),
     sizeof(uint32_t), _Alignof(uint32_t), &cmeta_type_uint32, NULL},
    {"tri_value", "uint32_t", offsetof(NullableJsonRecord, tri_value),
     sizeof(uint32_t), _Alignof(uint32_t), &cmeta_type_uint32, NULL},
    {"defaulted_value", "uint32_t", offsetof(NullableJsonRecord, defaulted_value),
     sizeof(uint32_t), _Alignof(uint32_t), &cmeta_type_uint32, NULL},
    {"owned", "tstr", offsetof(NullableJsonRecord, owned),
     sizeof(tstr), _Alignof(tstr), NULL, NULL}};
static const cmeta_struct_desc NULLABLE_JSON_LAYOUT = {
    "NullableJsonRecord", sizeof(NullableJsonRecord), _Alignof(NullableJsonRecord),
    NULLABLE_JSON_LAYOUT_FIELDS,
    sizeof(NULLABLE_JSON_LAYOUT_FIELDS) / sizeof(NULLABLE_JSON_LAYOUT_FIELDS[0])};
static cmeta_data_field_desc NULLABLE_JSON_DATA_FIELDS[] = {
    {"test.NullableJson.Record.required_value", "required_value",
     offsetof(NullableJsonRecord, required_value), &cmeta_data_uint32},
    {"test.NullableJson.Record.optional_value", "optional_value",
     offsetof(NullableJsonRecord, optional_value), &cmeta_data_uint32},
    {"test.NullableJson.Record.nullable_value", "nullable_value",
     offsetof(NullableJsonRecord, nullable_value), &cmeta_data_uint32},
    {"test.NullableJson.Record.tri_value", "tri_value",
     offsetof(NullableJsonRecord, tri_value), &cmeta_data_uint32},
    {"test.NullableJson.Record.defaulted_value", "defaulted_value",
     offsetof(NullableJsonRecord, defaulted_value), &cmeta_data_uint32},
    {"test.NullableJson.Record.owned", "owned",
     offsetof(NullableJsonRecord, owned), NULL}};
static const cmeta_data_struct_shape NULLABLE_JSON_SHAPE = {
    &NULLABLE_JSON_LAYOUT,
    NULLABLE_JSON_DATA_FIELDS,
    sizeof(NULLABLE_JSON_DATA_FIELDS) / sizeof(NULLABLE_JSON_DATA_FIELDS[0])};
static const cmeta_data_desc NULLABLE_JSON_DATA = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.NullableJson.Record.data", "NullableJsonRecord", CMETA_DATA_STRUCT,
    &NULLABLE_JSON_CTYPE, &NULLABLE_JSON_SHAPE, NULL, NULL, NULL};

enum {
  NULLABLE_WORKSPACE_BYTES = 4096,
  NULLABLE_OUTPUT_BYTES = 1024,
  NULLABLE_MAX_DEPTH = 4,
  NULLABLE_MAX_ITEMS = 16,
  NULLABLE_MAX_OWNED_BYTES = 128,
  NULLABLE_OWNED_INDEX = 5,
  NULLABLE_REUSE_COUNT = 3
};

static const DataBindNativeStateBinding NULLABLE_PRESENCE[] = {
    {sizeof(DataBindNativeStateBinding), "optional_value", offsetof(NullableJsonRecord, presence), 0u},
    {sizeof(DataBindNativeStateBinding), "tri_value", offsetof(NullableJsonRecord, presence), 1u},
    {sizeof(DataBindNativeStateBinding), "defaulted_value", offsetof(NullableJsonRecord, presence), 2u},
    {sizeof(DataBindNativeStateBinding), "owned", offsetof(NullableJsonRecord, presence), 3u}};
static const DataBindNativeStateBinding NULLABLE_NULLS[] = {
    {sizeof(DataBindNativeStateBinding), "nullable_value", offsetof(NullableJsonRecord, nulls), 0u},
    {sizeof(DataBindNativeStateBinding), "tri_value", offsetof(NullableJsonRecord, nulls), 1u},
    {sizeof(DataBindNativeStateBinding), "defaulted_value", offsetof(NullableJsonRecord, nulls), 2u},
    {sizeof(DataBindNativeStateBinding), "owned", offsetof(NullableJsonRecord, nulls), 3u}};
static const DataBindNativeTypeBinding NULLABLE_NATIVE = {
    sizeof(DataBindNativeTypeBinding), DATA_BIND_NATIVE_BINDING_ABI_VERSION,
    "Record", &NULLABLE_JSON_DATA,
    NULLABLE_PRESENCE, sizeof(NULLABLE_PRESENCE) / sizeof(NULLABLE_PRESENCE[0]),
    NULLABLE_NULLS, sizeof(NULLABLE_NULLS) / sizeof(NULLABLE_NULLS[0])};

typedef union NullableWorkspace {
  uint64_t integer_alignment;
  void *pointer_alignment;
  unsigned char bytes[NULLABLE_WORKSPACE_BYTES];
} NullableWorkspace;

typedef struct NullableOutput {
  char bytes[NULLABLE_OUTPUT_BYTES];
  size_t size;
} NullableOutput;

static DataBind *codec;
static DataBindMessagePlan *plan;
static DataBindNativeOptions options;
static DataBindMessagePlanDiagnostic diagnostic;
static DataBindError error;
static NullableWorkspace workspace;

static int nullable_write(const void *data, size_t size, void *context) {
  NullableOutput *out = (NullableOutput *)context;
  if (size >= sizeof(out->bytes) - out->size) return -1;
  memcpy(out->bytes + out->size, data, size);
  out->size += size;
  out->bytes[out->size] = '\0';
  return 0;
}

/* These helpers only own format leases. MessagePlan receives fresh staging;
 * successful publication and native resource lifetime remain with each test. */
static DataBindStatus nullable_decode(DataBindFormat format, const char *input,
                                      NullableJsonRecord *staging) {
  DataBindFormatReader lease = DATA_BIND_FORMAT_READER_INIT;
  DataBindStatus status = data_bind_format_reader_open(
      data_bind_builtin_format_provider(format), input, strlen(input),
      NULLABLE_MAX_DEPTH, &lease, &error);
  DataBindStatus close_status;
  if (status != DATA_BIND_OK) return status;
  status = data_bind_message_plan_decode_native_format(
      plan, &options, format, lease.reader, staging, sizeof(*staging), &diagnostic);
  close_status = data_bind_format_reader_close(&lease);
  return status == DATA_BIND_OK ? close_status : status;
}

static DataBindStatus nullable_encode(DataBindFormat format,
                                      const NullableJsonRecord *record,
                                      NullableOutput *out) {
  DataBindFormatWriter lease = DATA_BIND_FORMAT_WRITER_INIT;
  DataBindStatus status = data_bind_format_writer_open(
      data_bind_builtin_format_provider(format), nullable_write, out,
      NULLABLE_MAX_DEPTH, &lease, &error);
  DataBindStatus close_status;
  if (status != DATA_BIND_OK) return status;
  status = data_bind_message_plan_encode_native(
      plan, &options, record, sizeof(*record), lease.writer, &diagnostic);
  close_status = data_bind_format_writer_close(&lease, &error);
  return status == DATA_BIND_OK ? close_status : status;
}

static void nullable_clear(NullableJsonRecord *record) {
  check_equal(cmeta_data_value_restore_zero(&NULLABLE_JSON_DATA, record), CMETA_OK);
  /* State bits belong to the DataBind binding, outside the CMeta value graph. */
  record->presence = 0u;
  record->nulls = 0u;
}

static DataBind *nullable_json_codec(void) {
  static const char schema[] =
      "schema NullableJson [version(1)];"
      "message Record {"
      " uint32 required_value;"
      " optional uint32 optional_value;"
      " nullable uint32 nullable_value;"
      " optional nullable uint32 tri_value;"
      " optional nullable uint32 defaulted_value default 7;"
      " optional nullable string owned;"
      "}";
  DataBind *result = NULL;
  DataBindError creation_error = DATA_BIND_ERROR_INIT;
  return data_bind_create_from_text(schema, sizeof(schema) - 1u, &result, &creation_error) ==
                 DATA_BIND_OK
             ? result
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

spec("native nullable text state") {
  before_each() {
    NULLABLE_JSON_LAYOUT_FIELDS[NULLABLE_OWNED_INDEX].type = salts_tstr_cmeta_data.storage_type;
    NULLABLE_JSON_DATA_FIELDS[NULLABLE_OWNED_INDEX].value = &salts_tstr_cmeta_data;
    codec = nullable_json_codec();
    plan = NULL;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    diagnostic = (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    options = (DataBindNativeOptions)DATA_BIND_NATIVE_OPTIONS_INIT;
    options.workspace = workspace.bytes;
    options.workspace_bytes = sizeof(workspace.bytes);
    options.max_depth = NULLABLE_MAX_DEPTH;
    options.max_items = NULLABLE_MAX_ITEMS;
    options.max_owned_bytes = NULLABLE_MAX_OWNED_BYTES;
    check_not_null(codec);
    if (codec != NULL)
      check_equal(data_bind_message_plan_compile(codec, "Record", &NULLABLE_NATIVE,
                                                  &plan, &diagnostic), DATA_BIND_OK);
    check_not_null(plan);
  }

  after_each() {
    data_bind_message_plan_free(plan);
    data_bind_free(codec);
  }

  it("preserves ABSENT NULL VALUE and defaults through JSON and YAML round trips") {
    static const struct { DataBindFormat format; const char *input; } cases[] = {
      {DATA_BIND_FORMAT_JSON, "{\"required_value\":1,\"nullable_value\":null,\"tri_value\":null}"},
      {DATA_BIND_FORMAT_YAML, "required_value: 1\nnullable_value: null\ntri_value: ~\n"}
    };
    size_t i;
    if (plan == NULL) return;
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      NullableJsonRecord record = {0}, roundtrip = {0};
      NullableOutput output = {0};
      check_equal(nullable_decode(cases[i].format, cases[i].input, &record), DATA_BIND_OK);
      check_null_state(&record);
      check_equal(nullable_encode(cases[i].format, &record, &output), DATA_BIND_OK);
      check_greater(output.size, 0u);
      if (cases[i].format == DATA_BIND_FORMAT_JSON) check_json_shape(output.bytes);
      check_equal(nullable_decode(cases[i].format, output.bytes, &roundtrip), DATA_BIND_OK);
      check_null_state(&roundtrip);
      nullable_clear(&roundtrip);
      nullable_clear(&record);
    }
  }

  it("does not replace explicit null with a default") {
    NullableJsonRecord record = {0};
    NullableOutput output = {0};
    if (plan == NULL) return;
    check_equal(nullable_decode(DATA_BIND_FORMAT_JSON,
        "{\"required_value\":1,\"nullable_value\":2,\"defaulted_value\":null}", &record), DATA_BIND_OK);
    check_equal(record.defaulted_value, 0u);
    check_not_equal(record.presence & (1u << 2), 0u);
    check_not_equal(record.nulls & (1u << 2), 0u);
    check_equal(nullable_encode(DATA_BIND_FORMAT_JSON, &record, &output), DATA_BIND_OK);
    check_contains(output.bytes, "\"defaulted_value\":null");
    nullable_clear(&record);
  }

  it("clears failed staging and preserves the published owning object") {
    static const char initial[] =
        "{\"required_value\":1,\"nullable_value\":2,\"owned\":\"published\"}";
    NullableJsonRecord published = {0}, staging = {0};
    unsigned char before[sizeof(published)];
    if (plan == NULL) return;
    check_equal(nullable_decode(DATA_BIND_FORMAT_JSON, initial, &published), DATA_BIND_OK);
    memcpy(before, &published, sizeof(before));
    check_equal(nullable_decode(DATA_BIND_FORMAT_JSON,
        "{\"owned\":\"temporary\",\"required_value\":null,\"nullable_value\":2}", &staging),
        DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(diagnostic.schema_field, "required_value");
    check_equal(staging.presence, 0u);
    check_equal(staging.nulls, 0u);
    check_null(staging.owned);
    check_equal(staging.required_value, 0u);
    check_equal(memcmp(&published, before, sizeof(published)), 0);
    check_equal(published.owned, "published");
    nullable_clear(&staging);
    nullable_clear(&published);
  }

  it("rejects ABSENT plus NULL in validation and before streaming output") {
    static const DataBindFormat formats[] = {DATA_BIND_FORMAT_JSON, DATA_BIND_FORMAT_YAML};
    static const struct { const char *name; unsigned bit; } fields[] = {
      {"tri_value", 1u}, {"defaulted_value", 2u}, {"owned", 3u}
    };
    NullableJsonRecord record = {0};
    size_t i, j;
    if (plan == NULL) return;
    record.required_value = 1u;
    record.nullable_value = 2u;
    for (i = 0u; i < sizeof(fields) / sizeof(fields[0]); ++i) {
      unsigned char before[sizeof(record)];
      record.nulls = (uint8_t)(1u << fields[i].bit);
      memcpy(before, &record, sizeof(before));
      check_equal(data_bind_message_plan_validate_native(plan, &record, sizeof(record), &error),
                  DATA_BIND_ERR_SCHEMA);
      check_equal(error.path, fields[i].name);
      for (j = 0u; j < sizeof(formats) / sizeof(formats[0]); ++j) {
        NullableOutput output = {0};
        check_equal(nullable_encode(formats[j], &record, &output), DATA_BIND_ERR_SCHEMA);
        check_equal(diagnostic.schema_field, fields[i].name);
        check_contains(diagnostic.message, "absent");
        check_equal(output.size, 0u);
      }
      check_equal(memcmp(&record, before, sizeof(record)), 0);
    }
  }

  it("releases owned values across repeated clear and reuse including absent storage") {
    NullableJsonRecord record = {0};
    size_t i;
    if (plan == NULL) return;
    for (i = 0u; i < NULLABLE_REUSE_COUNT; ++i) {
      check_equal(nullable_decode(DATA_BIND_FORMAT_JSON,
          "{\"required_value\":1,\"nullable_value\":null,\"owned\":\"retained\"}", &record), DATA_BIND_OK);
      check_equal(record.owned, "retained");
      check_not_equal(record.presence & (1u << 3), 0u);
      record.presence &= (uint8_t)(UINT8_MAX ^ (1u << 3));
      nullable_clear(&record);
      check_null(record.owned);
      nullable_clear(&record);
      check_equal(record.presence, 0u);
      check_equal(record.nulls, 0u);
    }
  }

  it("rejects CSV and XML publication without a nullable representation") {
    static const DataBindFormat formats[] = {DATA_BIND_FORMAT_CSV, DATA_BIND_FORMAT_XML};
    size_t i;
    if (plan == NULL) return;
    for (i = 0u; i < sizeof(formats) / sizeof(formats[0]); ++i) {
      DataBindFormatPlan *format_plan = NULL;
      check_equal(data_bind_format_plan_compile(codec, "Record", formats[i], &format_plan, &error),
                  DATA_BIND_ERR_SCHEMA);
      check_null(format_plan);
      check_contains(error.message, "NULL");
      data_bind_format_plan_free(format_plan);
    }
  }
}
