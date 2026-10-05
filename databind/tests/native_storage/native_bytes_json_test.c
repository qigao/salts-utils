#include "data_bind_message_plan.h"
#include "data_bind_format_provider.h"
#include "data_bind_projection_plan.h"
#include "native_test_alignment.h"
#include "reader_probe.h"

#include <cstl/byte_buffer.h>
#include <tinytest.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
  BYTES_FIELD_COUNT = 2,
  BYTES_WORKSPACE_SIZE = 4096,
  BYTES_OUTPUT_SIZE = 128,
  BYTES_MAX_DEPTH = 4,
  BYTES_MAX_ITEMS = 16,
  BYTES_MAX_OWNED = 32
};

typedef struct BytesRecord {
  stl_byte_buffer value;
  uint32_t count;
} BytesRecord;

static const cmeta_type_identity BYTES_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.native-bytes.BytesRecord");
static const cmeta_type_desc BYTES_TYPE = {
    .name = "BytesRecord", .size = sizeof(BytesRecord), .align = _Alignof(BytesRecord),
    .kind = CMETA_T_OBJECT, .identity = &BYTES_ID};
static cmeta_field_desc bytes_layout_fields[] = {
    {"value", "stl_byte_buffer", offsetof(BytesRecord, value), sizeof(stl_byte_buffer),
     _Alignof(stl_byte_buffer), NULL, NULL},
    {"count", "uint32_t", offsetof(BytesRecord, count), sizeof(uint32_t),
     _Alignof(uint32_t), &cmeta_type_uint32, NULL}};
static const cmeta_struct_desc BYTES_LAYOUT = {
    "BytesRecord", sizeof(BytesRecord), _Alignof(BytesRecord),
    bytes_layout_fields, BYTES_FIELD_COUNT};
static cmeta_data_field_desc bytes_fields[] = {
    {"test.native-bytes.value", "value", offsetof(BytesRecord, value), NULL},
    {"test.native-bytes.count", "count", offsetof(BytesRecord, count), &cmeta_data_uint32}};
static const cmeta_data_struct_shape BYTES_SHAPE = {
    &BYTES_LAYOUT, bytes_fields, BYTES_FIELD_COUNT};
static const cmeta_data_desc BYTES_DATA = {
    .struct_size = sizeof(cmeta_data_desc), .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.native-bytes.BytesRecord.data", .display_name = "BytesRecord",
    .kind = CMETA_DATA_STRUCT, .storage_type = &BYTES_TYPE, .shape = &BYTES_SHAPE};
static const DataBindNativeTypeBinding BYTES_BINDING =
    DATA_BIND_NATIVE_TYPE_BINDING_INIT("BytesRecord", &BYTES_DATA);
static const char BYTES_SCHEMA[] =
    "message BytesRecord { uint32 count; [name(payload), alias(oldPayload)] bytes value; }";
static const unsigned char SEED_BYTES[] = "seed";
enum { BYTES_SEED_LENGTH = sizeof(SEED_BYTES) - 1u };
static const char SEED_JSON[] = "{\"count\":7,\"payload\":\"seed\"}";

typedef union BytesWorkspace {
  DataBindNativeTestAlignment alignment;
  unsigned char bytes[BYTES_WORKSPACE_SIZE];
} BytesWorkspace;
typedef struct BytesOutput {
  char bytes[BYTES_OUTPUT_SIZE];
  size_t size;
  size_t capacity;
} BytesOutput;

static DataBind *codec;
static DataBindMessagePlan *message_plan;
static DataBindFormatPlan *format_plan;
static DataBindNativeOptions options;
static DataBindMessagePlanDiagnostic diagnostic;
static DataBindError error;
static BytesWorkspace workspace;
static BytesRecord published;

/* Single-threaded replacement owns staging and published buffers separately.
 * Reader slices expire at close. Publication occurs only after successful close;
 * every failure destroys staging while retaining the prior owner. */
static DataBindStatus publish_staging(BytesRecord *staging, DataBindStatus status) {
  if (status == DATA_BIND_OK) {
    cmeta_data_value_destroy(&BYTES_DATA, &published);
    cmeta_data_trait_move_construct(&BYTES_DATA, &published, staging);
  } else {
    check_equal(stl_byte_buffer_size(&staging->value), 0u);
    check_equal(staging->count, 0u);
  }
  cmeta_data_value_destroy(&BYTES_DATA, staging);
  return status;
}

static DataBindStatus replace_json(const char *input, size_t size) {
  DataBindFormatReader lease = DATA_BIND_FORMAT_READER_INIT;
  DataBindFormatCanonicalReader canonical = DATA_BIND_FORMAT_CANONICAL_READER_INIT;
  BytesRecord staging;
  DataBindStatus status, close_status;
  if (cmeta_data_value_init_zero(&BYTES_DATA, &staging) != CMETA_OK)
    return DATA_BIND_ERR_SCHEMA;
  status = data_bind_format_reader_open(
      data_bind_builtin_format_provider(DATA_BIND_FORMAT_JSON), input, size,
      BYTES_MAX_DEPTH, &lease, &error);
  if (status != DATA_BIND_OK) goto cleanup;
  status = data_bind_format_canonical_reader_init(format_plan, lease.reader, &canonical, &error);
  if (status != DATA_BIND_OK) goto cleanup;
  status = data_bind_message_plan_decode_native_format(
      message_plan, &options, DATA_BIND_FORMAT_JSON,
      data_bind_format_canonical_reader_reader(&canonical),
      &staging, sizeof(staging), &diagnostic);
cleanup:
  close_status = data_bind_format_reader_close(&lease);
  if (status == DATA_BIND_OK) status = close_status;
  return publish_staging(&staging, status);
}

static DataBindStatus replace_token(cserde_token_kind kind,
                                    const unsigned char *data, size_t size,
                                    DataBindFormat format) {
  static const unsigned char value_key[] = "value", count_key[] = "count";
  const NativeReaderProbeStep steps[] = {
      native_reader_probe_token(CSERDE_MAP_BEGIN),
      native_reader_probe_slice(CSERDE_STRING, value_key, sizeof(value_key) - 1u,
                               CSERDE_VIEW_STABLE),
      native_reader_probe_slice(kind, data, size, CSERDE_VIEW_TRANSIENT),
      native_reader_probe_slice(CSERDE_STRING, count_key, sizeof(count_key) - 1u,
                               CSERDE_VIEW_STABLE),
      native_reader_probe_sint(7), native_reader_probe_token(CSERDE_MAP_END)};
  NativeReaderProbe probe;
  cserde_reader reader = {0};
  BytesRecord staging;
  DataBindStatus status;
  if (cmeta_data_value_init_zero(&BYTES_DATA, &staging) != CMETA_OK)
    return DATA_BIND_ERR_SCHEMA;
  check_equal(native_reader_probe_open(&probe, steps, sizeof(steps) / sizeof(steps[0]),
                                      &reader), CSERDE_OK);
  status = format == DATA_BIND_FORMAT_NONE
      ? data_bind_message_plan_decode_native(message_plan, &options, &reader,
                                             &staging, sizeof(staging), &diagnostic)
      : data_bind_message_plan_decode_native_format(message_plan, &options, format,
                                                    &reader, &staging, sizeof(staging),
                                                    &diagnostic);
  return publish_staging(&staging, status);
}

static int write_json(const void *data, size_t size, void *context) {
  BytesOutput *out = context;
  if (size > out->capacity - out->size) return -1;
  if (size != 0u) memcpy(out->bytes + out->size, data, size);
  out->size += size;
  out->bytes[out->size] = '\0';
  return 0;
}

static DataBindStatus encode_json(BytesOutput *out) {
  DataBindFormatWriter lease = DATA_BIND_FORMAT_WRITER_INIT;
  DataBindFormatCanonicalWriter canonical = DATA_BIND_FORMAT_CANONICAL_WRITER_INIT;
  DataBindStatus status, close_status;
  out->size = 0u;
  out->bytes[0] = '\0';
  status = data_bind_format_writer_open(
      data_bind_builtin_format_provider(DATA_BIND_FORMAT_JSON), write_json, out,
      BYTES_MAX_DEPTH, &lease, &error);
  if (status != DATA_BIND_OK) return status;
  status = data_bind_format_canonical_writer_init(format_plan, lease.writer, &canonical, &error);
  if (status != DATA_BIND_OK) goto cleanup;
  status = data_bind_message_plan_encode_native(
      message_plan, &options, &published, sizeof(published),
      data_bind_format_canonical_writer_writer(&canonical), &diagnostic);
  if (status == DATA_BIND_OK && cserde_writer_finish(
          data_bind_format_canonical_writer_writer(&canonical)) != CSERDE_OK)
    status = DATA_BIND_ERR_RUNTIME;
cleanup:
  close_status = data_bind_format_writer_close(&lease, &error);
  return status == DATA_BIND_OK ? close_status : status;
}

static void check_payload(const unsigned char *bytes, size_t size) {
  check_equal(stl_byte_buffer_size(&published.value), size);
  if (size != 0u) {
    const unsigned char *actual = stl_byte_buffer_data_const(&published.value);
    check_not_null(actual);
    if (actual != NULL) check_equal(actual, bytes, size);
  }
}

spec("DataBind canonical root bytes JSON projection") {
  before_all() {
    bytes_layout_fields[0].type = stl_byte_buffer_cmeta_data.storage_type;
    bytes_fields[0].value = &stl_byte_buffer_cmeta_data;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    diagnostic = (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    DataBindStatus create_status = data_bind_create_from_text(
        BYTES_SCHEMA, sizeof(BYTES_SCHEMA) - 1u, &codec, &error);
    check(create_status == DATA_BIND_OK, "%s", error.message);
    DataBindStatus compile_status = data_bind_message_plan_compile(
        codec, "BytesRecord", &BYTES_BINDING, &message_plan, &diagnostic);
    check(compile_status == DATA_BIND_OK, "%s", diagnostic.message);
    check_equal(data_bind_format_plan_compile(codec, "BytesRecord", DATA_BIND_FORMAT_JSON,
                                             &format_plan, &error), DATA_BIND_OK);
  }
  before_each() {
    options = (DataBindNativeOptions)DATA_BIND_NATIVE_OPTIONS_INIT;
    options.workspace = workspace.bytes;
    options.workspace_bytes = sizeof(workspace.bytes);
    options.max_depth = BYTES_MAX_DEPTH;
    options.max_items = BYTES_MAX_ITEMS;
    options.max_owned_bytes = BYTES_MAX_OWNED;
    diagnostic = (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(cmeta_data_value_init_zero(&BYTES_DATA, &published), CMETA_OK);
    check_equal(replace_json(SEED_JSON, sizeof(SEED_JSON) - 1u), DATA_BIND_OK);
  }
  after_each() {
    cmeta_data_value_destroy(&BYTES_DATA, &published);
  }
  after_all() {
    data_bind_format_plan_free(format_plan);
    data_bind_message_plan_free(message_plan);
    data_bind_free(codec);
  }

  it("copies alias input and emits the primary JSON name after reader close") {
    char input[] = "{\"oldPayload\":\"A\xc3\xa9\",\"count\":9}";
    static const unsigned char expected[] = {'A', 0xc3u, 0xa9u};
    BytesOutput out = {.capacity = BYTES_OUTPUT_SIZE - 1u};
    check_equal(replace_json(input, sizeof(input) - 1u), DATA_BIND_OK);
    memset(input, 0, sizeof(input));
    check_payload(expected, sizeof(expected));
    check_equal(published.count, 9u);
    check_equal(encode_json(&out), DATA_BIND_OK);
    check_equal(out.bytes, "{\"count\":9,\"payload\":\"A\xc3\xa9\"}");
  }

  it("round trips embedded NUL without truncating owned bytes") {
    static const char input[] = "{\"payload\":\"A\\u0000B\",\"count\":7}";
    static const unsigned char expected[] = {'A', 0u, 'B'};
    BytesOutput out = {.capacity = BYTES_OUTPUT_SIZE - 1u};
    check_equal(replace_json(input, sizeof(input) - 1u), DATA_BIND_OK);
    check_payload(expected, sizeof(expected));
    check_equal(encode_json(&out), DATA_BIND_OK);
    check_equal(out.bytes, "{\"count\":7,\"payload\":\"A\\u0000B\"}");
    check_equal(replace_json(out.bytes, out.size), DATA_BIND_OK);
    check_payload(expected, sizeof(expected));
  }

  it("releases a populated buffer when empty bytes replace it") {
    static const char input[] = "{\"payload\":\"\",\"count\":7}";
    BytesOutput out = {.capacity = BYTES_OUTPUT_SIZE - 1u};
    check_equal(replace_json(input, sizeof(input) - 1u), DATA_BIND_OK);
    check_payload(NULL, 0u);
    check_equal(encode_json(&out), DATA_BIND_OK);
    check_equal(out.bytes, "{\"count\":7,\"payload\":\"\"}");
  }

  it("retains the old owner after a later field fails") {
    static const char input[] = "{\"payload\":\"staged\",\"count\":\"invalid\"}";
    const unsigned char *owner = stl_byte_buffer_data_const(&published.value);
    check_equal(replace_json(input, sizeof(input) - 1u), DATA_BIND_ERR_TYPE_MISMATCH);
    check_true(stl_byte_buffer_data_const(&published.value) == owner);
    check_payload(SEED_BYTES, BYTES_SEED_LENGTH);
    check_equal(published.count, 7u);
  }

  it("retains the old owner on bytes budget exhaustion and permits retry") {
    const unsigned char *owner = stl_byte_buffer_data_const(&published.value);
    options.max_owned_bytes = BYTES_SEED_LENGTH - 1u;
    check_equal(replace_json(SEED_JSON, sizeof(SEED_JSON) - 1u), DATA_BIND_ERR_LIMIT);
    check_true(stl_byte_buffer_data_const(&published.value) == owner);
    options.max_owned_bytes = BYTES_SEED_LENGTH;
    check_equal(replace_json(SEED_JSON, sizeof(SEED_JSON) - 1u), DATA_BIND_OK);
    check_payload(SEED_BYTES, BYTES_SEED_LENGTH);
  }

  it("rejects invalid UTF-8 bytes on JSON output without consuming their owner") {
    static const unsigned char invalid[] = {0xc3u, 0x28u};
    BytesOutput out = {.capacity = BYTES_OUTPUT_SIZE - 1u};
    check_equal(replace_token(CSERDE_BYTES, invalid, sizeof(invalid), DATA_BIND_FORMAT_NONE),
                DATA_BIND_OK);
    const unsigned char *owner = stl_byte_buffer_data_const(&published.value);
    check_equal(encode_json(&out), DATA_BIND_ERR_SCHEMA);
    check_true(stl_byte_buffer_data_const(&published.value) == owner);
    check_payload(invalid, sizeof(invalid));
    check_equal(replace_json(SEED_JSON, sizeof(SEED_JSON) - 1u), DATA_BIND_OK);
    check_equal(encode_json(&out), DATA_BIND_OK);
    check_equal(out.bytes, SEED_JSON);
  }

  it("rejects invalid UTF-8 text supplied to the explicit JSON bytes projection") {
    static const unsigned char invalid[] = {0xc3u, 0x28u};
    const unsigned char *owner = stl_byte_buffer_data_const(&published.value);
    check_equal(replace_token(CSERDE_STRING, invalid, sizeof(invalid), DATA_BIND_FORMAT_JSON),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_true(stl_byte_buffer_data_const(&published.value) == owner);
    check_payload(SEED_BYTES, BYTES_SEED_LENGTH);
  }

  it("keeps strict canonical Binary and YAML input from coercing STRING to BYTES") {
    static const unsigned char input[] = "text";
    const unsigned char *owner = stl_byte_buffer_data_const(&published.value);
    check_equal(replace_token(CSERDE_STRING, input, sizeof(input) - 1u, DATA_BIND_FORMAT_NONE),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(replace_token(CSERDE_STRING, input, sizeof(input) - 1u, DATA_BIND_FORMAT_BINARY),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(replace_token(CSERDE_STRING, input, sizeof(input) - 1u, DATA_BIND_FORMAT_YAML),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_true(stl_byte_buffer_data_const(&published.value) == owner);
  }

  it("preserves arbitrary bytes in strict canonical native storage") {
    static const unsigned char input[] = {0u, 0xffu, 0x80u};
    check_equal(replace_token(CSERDE_BYTES, input, sizeof(input), DATA_BIND_FORMAT_NONE),
                DATA_BIND_OK);
    check_payload(input, sizeof(input));
  }

  it("retains native ownership after sink exhaustion and permits fresh output") {
    BytesOutput out = {.capacity = 0u};
    const unsigned char *owner = stl_byte_buffer_data_const(&published.value);
    check_not_equal(encode_json(&out), DATA_BIND_OK);
    check_equal(out.size, 0u);
    check_true(stl_byte_buffer_data_const(&published.value) == owner);
    out.capacity = BYTES_OUTPUT_SIZE - 1u;
    check_equal(encode_json(&out), DATA_BIND_OK);
    check_equal(out.bytes, SEED_JSON);
  }

  it("clears provider-owned bytes and reuses the complete native record") {
    DataBindNativeDiagnostic native = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    check_equal(data_bind_native_clear(&options, &BYTES_DATA, &published,
                                      sizeof(published), &native), DATA_BIND_OK);
    check_equal(stl_byte_buffer_size(&published.value), 0u);
    check_equal(published.count, 0u);
    check_equal(replace_json(SEED_JSON, sizeof(SEED_JSON) - 1u), DATA_BIND_OK);
    check_payload(SEED_BYTES, BYTES_SEED_LENGTH);
  }

  it("rejects duplicate primary and alias input without replacing the old owner") {
    static const char input[] =
        "{\"payload\":\"first\",\"oldPayload\":\"second\",\"count\":7}";
    const unsigned char *owner = stl_byte_buffer_data_const(&published.value);
    check_equal(replace_json(input, sizeof(input) - 1u), DATA_BIND_ERR_TYPE_MISMATCH);
    check_true(stl_byte_buffer_data_const(&published.value) == owner);
    check_payload(SEED_BYTES, BYTES_SEED_LENGTH);
  }

  it("preserves the owner after output bytes budget rejection and permits retry") {
    BytesOutput out = {.capacity = BYTES_OUTPUT_SIZE - 1u};
    const unsigned char *owner = stl_byte_buffer_data_const(&published.value);
    options.max_owned_bytes = BYTES_SEED_LENGTH - 1u;
    check_equal(encode_json(&out), DATA_BIND_ERR_LIMIT);
    check_true(stl_byte_buffer_data_const(&published.value) == owner);
    options.max_owned_bytes = BYTES_SEED_LENGTH;
    check_equal(encode_json(&out), DATA_BIND_OK);
    check_equal(out.bytes, SEED_JSON);
  }

  it("keeps the raw JSON provider strict when no FormatPlan projects bytes") {
    DataBindFormatWriter lease = DATA_BIND_FORMAT_WRITER_INIT;
    BytesOutput out = {.capacity = BYTES_OUTPUT_SIZE - 1u};
    const cserde_token token = {
        .kind = CSERDE_BYTES,
        .value.slice = {SEED_BYTES, BYTES_SEED_LENGTH, CSERDE_VIEW_STABLE}};
    check_equal(data_bind_format_writer_open(
                    data_bind_builtin_format_provider(DATA_BIND_FORMAT_JSON), write_json,
                    &out, BYTES_MAX_DEPTH, &lease, &error), DATA_BIND_OK);
    check_equal(cserde_writer_write(lease.writer, &token), CSERDE_UNSUPPORTED);
    check_equal(out.size, 0u);
    check_equal(data_bind_format_writer_close(&lease, &error), DATA_BIND_ERR_TYPE_MISMATCH);
  }
}
