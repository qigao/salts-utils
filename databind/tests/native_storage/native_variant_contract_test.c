#include "data_bind_native.h"
#include "reader_probe.h"

#include <salts_cmeta_data.h>
#include <tinytest.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
  VARIANT_WORKSPACE_BYTES = 4096,
  VARIANT_MAX_DEPTH = 8,
  VARIANT_MAX_ITEMS = 32,
  VARIANT_MAX_OWNED_BYTES = 64,
  VARIANT_MAX_TOKENS = 16
};

typedef union VariantWorkspace {
  max_align_t alignment;
  unsigned char bytes[VARIANT_WORKSPACE_BYTES];
} VariantWorkspace;

typedef struct NativeVariant {
  int tag;
  union {
    int number;
    tstr text;
  } payload;
} NativeVariant;

typedef struct TokenSink {
  cserde_token tokens[VARIANT_MAX_TOKENS];
  size_t count;
} TokenSink;

static const cmeta_type_identity VARIANT_TYPE_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.databind.native.Variant");
static const cmeta_type_desc VARIANT_TYPE = {
    .name = "NativeVariant",
    .size = sizeof(NativeVariant),
    .align = _Alignof(NativeVariant),
    .kind = CMETA_T_OBJECT,
    .identity = &VARIANT_TYPE_ID};

static bool variant_is_zero(const void *object) {
  const NativeVariant *value = (const NativeVariant *)object;
  return value != NULL && value->tag == 0;
}

static cmeta_status variant_active_tag(const void *object, int64_t *out) {
  const NativeVariant *value = (const NativeVariant *)object;
  if (value == NULL || out == NULL || value->tag == 0)
    return CMETA_INVALID_ARGUMENT;
  *out = value->tag;
  return CMETA_OK;
}

static cmeta_status variant_select(void *object, int64_t tag) {
  NativeVariant *value = (NativeVariant *)object;
  cmeta_status status;
  if (value == NULL || value->tag != 0)
    return CMETA_INVALID_ARGUMENT;
  memset(&value->payload, 0, sizeof(value->payload));
  if (tag == 2) {
    status = cmeta_data_value_init_zero(
        SALTS_TSTR_CMETA_DATA_REF, &value->payload.text);
    if (status != CMETA_OK) return status;
  }
  value->tag = (int)tag;
  return CMETA_OK;
}

static void variant_restore_zero(void *object) {
  NativeVariant *value = (NativeVariant *)object;
  if (value == NULL) return;
  if (value->tag == 2)
    (void)cmeta_data_value_restore_zero(
        SALTS_TSTR_CMETA_DATA_REF, &value->payload.text);
  memset(value, 0, sizeof(*value));
}

static const cmeta_data_variant_case VARIANT_CASES[] = {
    {1, "test.databind.native.Variant.number", "number",
     offsetof(NativeVariant, payload), &cmeta_data_int},
    {2, "test.databind.native.Variant.text", "text",
     offsetof(NativeVariant, payload), SALTS_TSTR_CMETA_DATA_REF}};

static const cmeta_data_variant_shape VARIANT_SHAPE = {
    .tag_offset = offsetof(NativeVariant, tag),
    .tag = &cmeta_data_int,
    .cases = VARIANT_CASES,
    .case_count = sizeof(VARIANT_CASES) / sizeof(VARIANT_CASES[0])};

static const cmeta_data_variant_ops VARIANT_OPS = {
    .struct_size = sizeof(cmeta_data_variant_ops),
    .abi_version = CMETA_DATA_VARIANT_OPS_ABI_VERSION,
    .storage_type = &VARIANT_TYPE,
    .is_zero = variant_is_zero,
    .active_tag = variant_active_tag,
    .select = variant_select,
    .restore_zero = variant_restore_zero};

static const cmeta_data_desc VARIANT_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.databind.native.Variant.data",
    .display_name = "NativeVariant",
    .kind = CMETA_DATA_VARIANT,
    .storage_type = &VARIANT_TYPE,
    .shape = &VARIANT_SHAPE,
    .variant_ops = &VARIANT_OPS};

static VariantWorkspace workspace;
static DataBindNativeOptions options;
static DataBindNativeDiagnostic diagnostic;

static void reset_options(void) {
  memset(&workspace, 0, sizeof(workspace));
  options = (DataBindNativeOptions)DATA_BIND_NATIVE_OPTIONS_INIT;
  diagnostic = (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  options.workspace = workspace.bytes;
  options.workspace_bytes = sizeof(workspace.bytes);
  options.max_depth = VARIANT_MAX_DEPTH;
  options.max_items = VARIANT_MAX_ITEMS;
  options.max_owned_bytes = VARIANT_MAX_OWNED_BYTES;
}

static cserde_status token_sink_write(
    void *context, const cserde_token *token) {
  TokenSink *sink = (TokenSink *)context;
  if (sink == NULL || token == NULL) return CSERDE_INVALID_ARGUMENT;
  if (sink->count == VARIANT_MAX_TOKENS) return CSERDE_LIMIT_EXCEEDED;
  sink->tokens[sink->count++] = *token;
  return CSERDE_OK;
}

static cserde_status token_sink_finish(void *context) {
  return context != NULL ? CSERDE_OK : CSERDE_INVALID_ARGUMENT;
}

static const cserde_writer_ops TOKEN_SINK_OPS = {
    sizeof(cserde_writer_ops), CSERDE_WRITER_OPS_ABI_VERSION,
    token_sink_write, token_sink_finish};

static void open_source(
    NativeReaderProbe *probe, cserde_reader *reader,
    const NativeReaderProbeStep *steps, size_t count) {
  memset(probe, 0, sizeof(*probe));
  memset(reader, 0, sizeof(*reader));
  check_equal(native_reader_probe_open(probe, steps, count, reader), CSERDE_OK);
}

static DataBindStatus decode(
    const cmeta_data_desc *data, cserde_reader *reader,
    void *destination, size_t destination_bytes) {
  return data_bind_native_decode(
      &options, data, reader, destination, destination_bytes, &diagnostic);
}

spec("DataBind canonical native Variant binding") {
  before_each() {
    reset_options();
    check_true(cmeta_data_desc_valid(&VARIANT_DATA));
    check_null(VARIANT_DATA.construct_ops);
    check_true(cmeta_data_value_move_supported(&VARIANT_DATA));
  }

  it("decodes the canonical [tag,payload] representation") {
    const NativeReaderProbeStep steps[] = {
        native_reader_probe_token(CSERDE_ARRAY_BEGIN),
        native_reader_probe_sint(1),
        native_reader_probe_sint(42),
        native_reader_probe_token(CSERDE_ARRAY_END)};
    NativeReaderProbe probe;
    cserde_reader reader = {0};
    NativeVariant value = {0};

    open_source(&probe, &reader, steps, 4u);
    check_equal(decode(&VARIANT_DATA, &reader, &value, sizeof(value)),
                DATA_BIND_OK);
    check_equal(value.tag, 1);
    check_equal(value.payload.number, 42);
    check_equal(probe.calls, (size_t)4u);
    check_equal(cmeta_data_variant_restore_zero(&VARIANT_DATA, &value),
                CMETA_OK);
  }

  it("round-trips one active Variant through the native writer") {
    NativeVariant source = {0};
    NativeVariant destination = {0};
    TokenSink sink = {0};
    cserde_writer writer = {0};
    NativeReaderProbeStep steps[4];
    NativeReaderProbe probe;
    cserde_reader reader = {0};
    size_t i;

    check_equal(cmeta_data_variant_select(&VARIANT_DATA, &source, 1), CMETA_OK);
    source.payload.number = 17;
    check_equal(cserde_writer_init(&writer, &TOKEN_SINK_OPS, &sink), CSERDE_OK);
    check_equal(data_bind_native_encode(
                    &options, &VARIANT_DATA, &source, sizeof(source),
                    &writer, &diagnostic),
                DATA_BIND_OK);
    check_equal(sink.count, (size_t)4u);
    check_true(sink.tokens[0].kind == CSERDE_ARRAY_BEGIN);
    check_true(sink.tokens[1].kind == CSERDE_SINT);
    check_equal(sink.tokens[1].value.sint, INT64_C(1));
    check_true(sink.tokens[2].kind == CSERDE_SINT);
    check_equal(sink.tokens[2].value.sint, INT64_C(17));
    check_true(sink.tokens[3].kind == CSERDE_ARRAY_END);

    for (i = 0u; i < 4u; ++i) {
      steps[i].status = CSERDE_OK;
      steps[i].token = sink.tokens[i];
    }
    open_source(&probe, &reader, steps, 4u);
    check_equal(decode(
                    &VARIANT_DATA, &reader, &destination,
                    sizeof(destination)),
                DATA_BIND_OK);
    check_equal(destination.tag, 1);
    check_equal(destination.payload.number, 17);

    check_equal(cmeta_data_variant_restore_zero(&VARIANT_DATA, &destination),
                CMETA_OK);
    check_equal(cmeta_data_variant_restore_zero(&VARIANT_DATA, &source),
                CMETA_OK);
  }

  it("rolls back an owned payload when the Variant is truncated") {
    static const unsigned char hello[] = {'h', 'e', 'l', 'l', 'o'};
    const NativeReaderProbeStep steps[] = {
        native_reader_probe_token(CSERDE_ARRAY_BEGIN),
        native_reader_probe_sint(2),
        native_reader_probe_slice(
            CSERDE_STRING, hello, sizeof(hello), CSERDE_VIEW_TRANSIENT)};
    NativeReaderProbe probe;
    cserde_reader reader = {0};
    NativeVariant value = {0};

    open_source(&probe, &reader, steps, 3u);
    check_equal(decode(&VARIANT_DATA, &reader, &value, sizeof(value)),
                DATA_BIND_ERR_PARSE);
    check_true(variant_is_zero(&value));
    check_null(value.payload.text);
  }

  it("rejects invalid case storage before consuming source input") {
    const NativeReaderProbeStep steps[] = {
        native_reader_probe_token(CSERDE_ARRAY_BEGIN)};
    cmeta_data_variant_case bad_case = VARIANT_CASES[0];
    cmeta_data_variant_shape bad_shape = VARIANT_SHAPE;
    cmeta_data_desc bad_data = VARIANT_DATA;
    NativeReaderProbe probe;
    cserde_reader reader = {0};
    NativeVariant value = {0};

    bad_case.offset = sizeof(NativeVariant);
    bad_shape.cases = &bad_case;
    bad_shape.case_count = 1u;
    bad_data.shape = &bad_shape;

    open_source(&probe, &reader, steps, 1u);
    check_equal(decode(&bad_data, &reader, &value, sizeof(value)),
                DATA_BIND_ERR_SCHEMA);
    check_equal(probe.calls, (size_t)0u);
    check_true(variant_is_zero(&value));
  }

  it("rejects a selected destination before consuming input") {
    const NativeReaderProbeStep steps[] = {
        native_reader_probe_token(CSERDE_ARRAY_BEGIN)};
    NativeReaderProbe probe;
    cserde_reader reader = {0};
    NativeVariant value = {0};

    check_equal(cmeta_data_variant_select(&VARIANT_DATA, &value, 1), CMETA_OK);
    open_source(&probe, &reader, steps, 1u);
    check_equal(decode(&VARIANT_DATA, &reader, &value, sizeof(value)),
                DATA_BIND_ERR_INVALID_ARG);
    check_equal(probe.calls, (size_t)0u);
    check_equal(value.tag, 1);
    check_equal(cmeta_data_variant_restore_zero(&VARIANT_DATA, &value),
                CMETA_OK);
  }

  it("rejects an unselected Variant on encode without publishing tokens") {
    NativeVariant value = {0};
    TokenSink sink = {0};
    cserde_writer writer = {0};

    check_equal(cserde_writer_init(&writer, &TOKEN_SINK_OPS, &sink), CSERDE_OK);
    check_equal(data_bind_native_encode(
                    &options, &VARIANT_DATA, &value, sizeof(value),
                    &writer, &diagnostic),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(sink.count, (size_t)0u);
  }
}
