#include "data_bind_message_plan.h"
#include "data_bind_format_provider.h"
#include "data_bind_projection_plan.h"
#include "native_test_alignment.h"

#include <cmeta/struct.h>
#include <cmeta_cmeta_data.h>
#include <tinytest.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
  MESSAGE_FIELD_COUNT = 2,
  MESSAGE_TEXT_INDEX = 1,
  MESSAGE_WORKSPACE_BYTES = 4096,
  MESSAGE_OUTPUT_BYTES = 128,
  MESSAGE_MAX_DEPTH = 4,
  MESSAGE_MAX_ITEMS = 16,
  MESSAGE_MAX_OWNED_BYTES = 64
};

Struct(MessageRecord, (uint32_t, id), (tstr, text));
static const cmeta_type_identity RECORD_IDENTITY =
    CMETA_TYPE_ID_ATOM_INIT("test.native-message.Record");
static const cmeta_type_desc RECORD_TYPE = {
    .name = "MessageRecord", .size = sizeof(MessageRecord),
    .align = _Alignof(MessageRecord), .kind = CMETA_T_OBJECT,
    .identity = &RECORD_IDENTITY};
static cmeta_field_desc record_layout_fields[MESSAGE_FIELD_COUNT];
static const cmeta_struct_desc RECORD_LAYOUT = {
    "MessageRecord", sizeof(MessageRecord), _Alignof(MessageRecord),
    record_layout_fields, MESSAGE_FIELD_COUNT};
static cmeta_data_field_desc record_fields[] = {
    {"test.native-message.Record.id", "id", offsetof(MessageRecord, id),
     &cmeta_data_uint32},
    {"test.native-message.Record.text", "text", offsetof(MessageRecord, text), NULL}};
static const cmeta_data_struct_shape RECORD_SHAPE = {
    &RECORD_LAYOUT, record_fields, MESSAGE_FIELD_COUNT};
static const cmeta_data_desc RECORD_DATA = {
    .struct_size = sizeof(cmeta_data_desc), .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.native-message.Record.data", .display_name = "MessageRecord",
    .kind = CMETA_DATA_STRUCT, .storage_type = &RECORD_TYPE, .shape = &RECORD_SHAPE};
static const DataBindNativeTypeBinding RECORD_BINDING =
    DATA_BIND_NATIVE_TYPE_BINDING_INIT("Record", &RECORD_DATA);
static const char RECORD_SCHEMA[] =
    "message Record { uint32 id; [name(displayText), alias(oldText)] string text; }";
static const char RECORD_JSON[] = "{\"id\":7,\"displayText\":\"mapped\"}";

typedef union MessageWorkspace {
  DataBindNativeTestAlignment alignment;
  unsigned char bytes[MESSAGE_WORKSPACE_BYTES];
} MessageWorkspace;

typedef struct MessageOutput {
  char bytes[MESSAGE_OUTPUT_BYTES];
  size_t size;
  size_t capacity;
} MessageOutput;

static DataBind *codec;
static DataBindMessagePlan *message_plan;
static DataBindFormatPlan *format_plan;
static DataBindNativeOptions options;
static DataBindMessagePlanDiagnostic diagnostic;
static DataBindError error;
static MessageWorkspace workspace;
static MessageRecord published;

/* One thread owns leases, staging and published storage. Tokens are borrowed
 * until reader close; only CMeta-owned text is published. Every attempt closes
 * its lease and destroys staging, with publication after a successful close. */
static DataBindStatus replace_json(const char *input, size_t size) {
  DataBindFormatReader lease = DATA_BIND_FORMAT_READER_INIT;
  DataBindFormatCanonicalReader canonical = DATA_BIND_FORMAT_CANONICAL_READER_INIT;
  MessageRecord staging;
  DataBindStatus status, close_status;
  if (cmeta_data_value_init_zero(&RECORD_DATA, &staging) != CMETA_OK)
    return DATA_BIND_ERR_SCHEMA;
  status = data_bind_format_reader_open(
      data_bind_builtin_format_provider(DATA_BIND_FORMAT_JSON), input, size,
      MESSAGE_MAX_DEPTH, &lease, &error);
  if (status != DATA_BIND_OK) goto cleanup;
  status = data_bind_format_canonical_reader_init(format_plan, lease.reader, &canonical, &error);
  if (status != DATA_BIND_OK) goto cleanup;
  status = data_bind_message_plan_decode_native(
      message_plan, &options, data_bind_format_canonical_reader_reader(&canonical),
      &staging, sizeof(staging), &diagnostic);
cleanup:
  close_status = data_bind_format_reader_close(&lease);
  if (status == DATA_BIND_OK) status = close_status;
  if (status == DATA_BIND_OK) {
    cmeta_data_value_destroy(&RECORD_DATA, &published);
    cmeta_data_trait_move_construct(&RECORD_DATA, &published, &staging);
  }
  cmeta_data_value_destroy(&RECORD_DATA, &staging);
  return status;
}

static int write_json(const void *data, size_t size, void *context) {
  MessageOutput *out = (MessageOutput *)context;
  if (size > out->capacity - out->size) return -1;
  memcpy(out->bytes + out->size, data, size);
  out->size += size;
  out->bytes[out->size] = '\0';
  return 0;
}

static DataBindStatus encode_json(MessageOutput *out) {
  DataBindFormatWriter lease = DATA_BIND_FORMAT_WRITER_INIT;
  DataBindFormatCanonicalWriter canonical = DATA_BIND_FORMAT_CANONICAL_WRITER_INIT;
  DataBindStatus status, close_status;
  out->size = 0u;
  out->bytes[0] = '\0';
  status = data_bind_format_writer_open(
      data_bind_builtin_format_provider(DATA_BIND_FORMAT_JSON), write_json, out,
      MESSAGE_MAX_DEPTH, &lease, &error);
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

spec("DataBind canonical message JSON contract") {
  before_all() {
    record_fields[MESSAGE_TEXT_INDEX].value = &cmeta_tstr_cmeta_data;
    for (size_t i = 0; i < MESSAGE_FIELD_COUNT; ++i) {
      record_layout_fields[i] = StructMeta(MessageRecord)->fields[i];
      record_layout_fields[i].type = record_fields[i].value->storage_type;
    }
    check_true(cmeta_data_desc_valid(&RECORD_DATA));
    check_true(cmeta_data_value_move_supported(&RECORD_DATA));
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    diagnostic = (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    check_equal(data_bind_create_from_text(RECORD_SCHEMA, sizeof(RECORD_SCHEMA) - 1u,
                                          &codec, &error), DATA_BIND_OK);
    check_equal(data_bind_message_plan_compile(codec, "Record", &RECORD_BINDING,
                                              &message_plan, &diagnostic), DATA_BIND_OK);
    check_equal(data_bind_format_plan_compile(codec, "Record", DATA_BIND_FORMAT_JSON,
                                             &format_plan, &error), DATA_BIND_OK);
  }
  before_each() {
    memset(&workspace, 0, sizeof(workspace));
    options = (DataBindNativeOptions)DATA_BIND_NATIVE_OPTIONS_INIT;
    options.workspace = workspace.bytes;
    options.workspace_bytes = sizeof(workspace.bytes);
    options.max_depth = MESSAGE_MAX_DEPTH;
    options.max_items = MESSAGE_MAX_ITEMS;
    options.max_owned_bytes = MESSAGE_MAX_OWNED_BYTES;
    diagnostic = (DataBindMessagePlanDiagnostic)DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(cmeta_data_value_init_zero(&RECORD_DATA, &published), CMETA_OK);
    check_equal(replace_json(RECORD_JSON, sizeof(RECORD_JSON) - 1u), DATA_BIND_OK);
  }
  after_each() {
    cmeta_data_value_destroy(&RECORD_DATA, &published);
    check_true(cmeta_tstr_cmeta_buffer_ops.is_zero(&published.text));
  }
  after_all() {
    data_bind_format_plan_free(format_plan);
    data_bind_message_plan_free(message_plan);
    data_bind_free(codec);
  }

  it("uses the declared JSON format and emits only the primary external name") {
    MessageOutput out = {.capacity = MESSAGE_OUTPUT_BYTES - 1u};
    check_equal(published.id, 7u);
    check_equal(published.text, "mapped");
    check_equal(encode_json(&out), DATA_BIND_OK);
    check_equal(out.bytes, RECORD_JSON);
    check_equal(out.size, sizeof(RECORD_JSON) - 1u);
  }

  it("copies alias input before reader close and replaces previous owned text") {
    char input[] = "{\"oldText\":\"replacement\",\"id\":9}";
    MessageOutput out = {.capacity = MESSAGE_OUTPUT_BYTES - 1u};
    check_equal(replace_json(input, sizeof(input) - 1u), DATA_BIND_OK);
    memset(input, 'x', sizeof(input) - 1u);
    check_equal(published.id, 9u);
    check_equal(published.text, "replacement");
    check_equal(encode_json(&out), DATA_BIND_OK);
    check_equal(out.bytes, "{\"id\":9,\"displayText\":\"replacement\"}");
  }

  it("keeps published ownership when a later field rejects staged text") {
    static const char rejected[] = "{\"oldText\":\"staged\",\"id\":\"invalid\"}";
    tstr original = published.text;
    check_equal(replace_json(rejected, sizeof(rejected) - 1u), DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(published.id, 7u);
    check_true(published.text == original);
    check_equal(published.text, "mapped");
    check_equal(replace_json(RECORD_JSON, sizeof(RECORD_JSON) - 1u), DATA_BIND_OK);
  }

  it("rejects duplicate primary and alias keys without publishing either") {
    static const char rejected[] =
        "{\"id\":9,\"oldText\":\"staged\",\"displayText\":\"duplicate\"}";
    tstr original = published.text;
    check_not_equal(replace_json(rejected, sizeof(rejected) - 1u), DATA_BIND_OK);
    check_equal(published.id, 7u);
    check_true(published.text == original);
    check_equal(published.text, "mapped");
  }

  it("rolls back an owned-byte budget rejection and can retry") {
    static const char input[] = "{\"id\":9,\"oldText\":\"replacement\"}";
    tstr original = published.text;
    options.max_owned_bytes = 1u;
    check_equal(replace_json(input, sizeof(input) - 1u), DATA_BIND_ERR_LIMIT);
    check_equal(published.id, 7u);
    check_true(published.text == original);
    options.max_owned_bytes = MESSAGE_MAX_OWNED_BYTES;
    check_equal(replace_json(input, sizeof(input) - 1u), DATA_BIND_OK);
    check_equal(published.text, "replacement");
  }

  it("returns cleared storage to semantic zero and reuses it") {
    DataBindNativeDiagnostic native = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    check_equal(data_bind_native_clear(&options, &RECORD_DATA, &published,
                                      sizeof(published), &native), DATA_BIND_OK);
    check_equal(published.id, 0u);
    check_true(cmeta_tstr_cmeta_buffer_ops.is_zero(&published.text));
    check_equal(replace_json(RECORD_JSON, sizeof(RECORD_JSON) - 1u), DATA_BIND_OK);
    check_equal(published.id, 7u);
    check_equal(published.text, "mapped");
  }

  it("preserves source ownership after a full JSON sink and can encode again") {
    MessageOutput out = {.capacity = 0u};
    tstr original = published.text;
    check_not_equal(encode_json(&out), DATA_BIND_OK);
    check_equal(out.size, 0u);
    check_true(published.text == original);
    out.capacity = MESSAGE_OUTPUT_BYTES - 1u;
    check_equal(encode_json(&out), DATA_BIND_OK);
    check_equal(out.bytes, RECORD_JSON);
  }

  it("rejects invalid UTF-8 text in JSON without consuming native ownership") {
    static const unsigned char invalid_utf8[] = {0xc3u, 0x28u};
    MessageOutput out = {.capacity = MESSAGE_OUTPUT_BYTES - 1u};
    check_equal(cmeta_data_buffer_restore_zero(&cmeta_tstr_cmeta_data,
                                              &published.text), CMETA_OK);
    check_equal(cmeta_data_buffer_assign(&cmeta_tstr_cmeta_data, &published.text,
                                        invalid_utf8, sizeof(invalid_utf8),
                                        MESSAGE_MAX_OWNED_BYTES), CMETA_OK);
    tstr original = published.text;
    /* Native maps CSERDE_UNSUPPORTED to a format/schema rejection. */
    check_equal(encode_json(&out), DATA_BIND_ERR_SCHEMA);
    check_true(published.text == original);
    check_equal(tstr_len(published.text), sizeof(invalid_utf8));
    check_equal(memcmp(published.text, invalid_utf8, sizeof(invalid_utf8)), 0);
    check_equal(replace_json(RECORD_JSON, sizeof(RECORD_JSON) - 1u), DATA_BIND_OK);
    check_equal(encode_json(&out), DATA_BIND_OK);
    check_equal(out.bytes, RECORD_JSON);
  }

  it("rejects incompatible and truncated native binding records at compilation") {
    DataBindNativeTypeBinding incompatible = RECORD_BINDING;
    DataBindMessagePlan *rejected = NULL;
    ++incompatible.abi_version;
    check_equal(data_bind_message_plan_compile(codec, "Record", &incompatible,
                                              &rejected, &diagnostic), DATA_BIND_ERR_SCHEMA);
    check_null(rejected);
    data_bind_message_plan_free(rejected);
    incompatible = RECORD_BINDING;
    incompatible.size = offsetof(DataBindNativeTypeBinding, null_count);
    check_equal(data_bind_message_plan_compile(codec, "Record", &incompatible,
                                              &rejected, &diagnostic), DATA_BIND_ERR_SCHEMA);
    check_null(rejected);
    data_bind_message_plan_free(rejected);
  }

  it("rejects incompatible and truncated canonical CMeta descriptors") {
    cmeta_data_desc incompatible_data = RECORD_DATA;
    DataBindNativeTypeBinding incompatible = RECORD_BINDING;
    DataBindMessagePlan *rejected = NULL;
    incompatible.data = &incompatible_data;
    ++incompatible_data.abi_version;
    check_equal(data_bind_message_plan_compile(codec, "Record", &incompatible,
                                              &rejected, &diagnostic), DATA_BIND_ERR_SCHEMA);
    check_null(rejected);
    data_bind_message_plan_free(rejected);
    incompatible_data = RECORD_DATA;
    incompatible_data.struct_size = offsetof(cmeta_data_desc, shape);
    check_equal(data_bind_message_plan_compile(codec, "Record", &incompatible,
                                              &rejected, &diagnostic), DATA_BIND_ERR_SCHEMA);
    check_null(rejected);
    data_bind_message_plan_free(rejected);
  }
}
