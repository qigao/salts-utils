#include "data_bind_native.h"

#include <cstl/typed.h>
#include <salts_cmeta_data.h>
#include <tstr.h>
#include <tinytest.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
  CONTAINER_WORKSPACE_BYTES = 8192,
  CONTAINER_MAX_DEPTH = 12,
  CONTAINER_MAX_ITEMS = 128,
  CONTAINER_MAX_OWNED_BYTES = 256,
  CONTAINER_MAX_TOKENS = 64
};

typedef struct ContainerWorkspace {
  _Alignas(64) unsigned char bytes[CONTAINER_WORKSPACE_BYTES];
} ContainerWorkspace;

typedef struct TokenSink {
  cserde_token tokens[CONTAINER_MAX_TOKENS];
  size_t count;
} TokenSink;

typedef struct NativeContainerTokenSource {
  const cserde_token *tokens;
  size_t count;
  size_t index;
} NativeContainerTokenSource;

typed(Vec, NativeIntVec, int);
typed(Set, NativeIntSet, int);
typed(Map, NativeIntLongMap, int, long);
typed(Vec, NativeTextVec, tstr,
      &salts_tstr_cmeta_type, &salts_tstr_cmeta_data);

static ContainerWorkspace workspace;
static DataBindNativeOptions options;
static DataBindNativeDiagnostic diagnostic;
static size_t element_callback_calls;

static cserde_status sink_write(void *context, const cserde_token *token) {
  TokenSink *sink = (TokenSink *)context;
  if (sink == NULL || token == NULL) return CSERDE_INVALID_ARGUMENT;
  if (sink->count == CONTAINER_MAX_TOKENS) return CSERDE_LIMIT_EXCEEDED;
  sink->tokens[sink->count++] = *token;
  return CSERDE_OK;
}

static cserde_status sink_finish(void *context) {
  return context != NULL ? CSERDE_OK : CSERDE_INVALID_ARGUMENT;
}

static const cserde_writer_ops SINK_OPS = {
    sizeof(cserde_writer_ops), CSERDE_WRITER_OPS_ABI_VERSION,
    sink_write, sink_finish};

static cserde_status source_next(void *context, cserde_token *out) {
  NativeContainerTokenSource *source = (NativeContainerTokenSource *)context;
  if (source == NULL || out == NULL) return CSERDE_INVALID_ARGUMENT;
  if (source->index == source->count) return CSERDE_DONE;
  *out = source->tokens[source->index++];
  return CSERDE_OK;
}

static const cserde_reader_ops SOURCE_OPS = {
    sizeof(cserde_reader_ops), CSERDE_READER_OPS_ABI_VERSION,
    source_next};

static void reset_native(void) {
  memset(&workspace, 0, sizeof(workspace));
  options = (DataBindNativeOptions)DATA_BIND_NATIVE_OPTIONS_INIT;
  diagnostic = (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  options.workspace = workspace.bytes;
  options.workspace_bytes = sizeof(workspace.bytes);
  options.max_depth = CONTAINER_MAX_DEPTH;
  options.max_items = CONTAINER_MAX_ITEMS;
  options.max_owned_bytes = CONTAINER_MAX_OWNED_BYTES;
  element_callback_calls = 0u;
}

static void open_writer(TokenSink *sink, cserde_writer *writer) {
  memset(sink, 0, sizeof(*sink));
  memset(writer, 0, sizeof(*writer));
  check_equal(cserde_writer_init(writer, &SINK_OPS, sink), CSERDE_OK);
}

static void open_reader(const TokenSink *sink, NativeContainerTokenSource *source,
                        cserde_reader *reader) {
  memset(source, 0, sizeof(*source));
  source->tokens = sink->tokens;
  source->count = sink->count;
  memset(reader, 0, sizeof(*reader));
  check_equal(cserde_reader_init(reader, &SOURCE_OPS, source), CSERDE_OK);
}

static DataBindStatus native_init(const cmeta_data_desc *data, void *value) {
  return data_bind_native_init(
      &options, data, value, data->storage_type->size, &diagnostic);
}

static DataBindStatus native_clear(const cmeta_data_desc *data, void *value) {
  return data_bind_native_clear(
      &options, data, value, data->storage_type->size, &diagnostic);
}

static DataBindStatus roundtrip(
    const cmeta_data_desc *data, const void *source, void *destination,
    TokenSink *sink) {
  cserde_writer writer;
  cserde_reader reader;
  NativeContainerTokenSource token_source;
  DataBindStatus status;

  open_writer(sink, &writer);
  status = data_bind_native_encode(
      &options, data, source, data->storage_type->size,
      &writer, &diagnostic);
  if (status != DATA_BIND_OK) return status;

  open_reader(sink, &token_source, &reader);
  return data_bind_native_decode(
      &options, data, &reader, destination, data->storage_type->size,
      &diagnostic);
}

static const cmeta_data_desc *counting_element(const void *object) {
  (void)object;
  ++element_callback_calls;
  return &cmeta_data_int;
}

spec("DataBind canonical CSTL native containers") {
  before_each() {
    reset_native();
  }

  it("measures collection graphs only from static member metadata") {
    cmeta_data_collection_ops ops = NativeIntVec_collection_ops;
    cmeta_data_desc data = NativeIntVec_collection_data;
    DataBindNativeRequirements requirements =
        (DataBindNativeRequirements)DATA_BIND_NATIVE_REQUIREMENTS_INIT;

    ops.element = counting_element;
    data.collection_ops = &ops;
    check_equal(
        data_bind_native_measure(
            &options, &data, &requirements, &diagnostic),
        DATA_BIND_OK);
    check_equal(element_callback_calls, (size_t)0u);
    check_equal(requirements.descriptor_depth, (size_t)2u);
    check_equal(requirements.descriptor_nodes, (size_t)2u);
    check_equal(requirements.container_depth, (size_t)1u);
    check_equal(requirements.field_tracking_bytes, (size_t)0u);
    check_true(requirements.decode_bytes > requirements.staging_bytes);
    check_true(requirements.decode_bytes <= sizeof(workspace.bytes));
  }

  it("round-trips typed Vec<int> through array tokens") {
    NativeIntVec source = {0};
    NativeIntVec destination = {0};
    TokenSink sink;
    const int *value;

    check_equal(native_init(&NativeIntVec_collection_data, &source),
                DATA_BIND_OK);
    check_equal(native_init(&NativeIntVec_collection_data, &destination),
                DATA_BIND_OK);
    check_equal(NativeIntVec_push(&source, 3), STL_OK);
    check_equal(NativeIntVec_push(&source, 5), STL_OK);

    check_equal(roundtrip(
                    &NativeIntVec_collection_data, &source, &destination,
                    &sink),
                DATA_BIND_OK);
    check_equal(sink.count, (size_t)4u);
    check_true(sink.tokens[0].kind == CSERDE_ARRAY_BEGIN);
    check_true(sink.tokens[1].kind == CSERDE_SINT);
    check_true(sink.tokens[2].kind == CSERDE_SINT);
    check_true(sink.tokens[3].kind == CSERDE_ARRAY_END);
    check_equal(NativeIntVec_size(&source), (size_t)2u);
    check_equal(NativeIntVec_size(&destination), (size_t)2u);
    value = NativeIntVec_at_const(&destination, 0u);
    check_not_null(value);
    check_equal(*value, 3);
    value = NativeIntVec_at_const(&destination, 1u);
    check_not_null(value);
    check_equal(*value, 5);

    check_equal(native_clear(&NativeIntVec_collection_data, &destination),
                DATA_BIND_OK);
    check_equal(native_clear(&NativeIntVec_collection_data, &source),
                DATA_BIND_OK);
    check_equal(NativeIntVec_size(&destination), (size_t)0u);
    check_equal(NativeIntVec_size(&source), (size_t)0u);
  }

  it("round-trips typed Set<int> through canonical collection semantics") {
    NativeIntSet source = {0};
    NativeIntSet destination = {0};
    TokenSink sink;

    check_equal(native_init(&NativeIntSet_collection_data, &source),
                DATA_BIND_OK);
    check_equal(native_init(&NativeIntSet_collection_data, &destination),
                DATA_BIND_OK);
    check_equal(NativeIntSet_add(&source, 5), STL_OK);
    check_equal(NativeIntSet_add(&source, 3), STL_OK);

    check_equal(roundtrip(
                    &NativeIntSet_collection_data, &source, &destination,
                    &sink),
                DATA_BIND_OK);
    check_equal(NativeIntSet_size(&source), (size_t)2u);
    check_equal(NativeIntSet_size(&destination), (size_t)2u);
    check_true(NativeIntSet_contains(&destination, 3));
    check_true(NativeIntSet_contains(&destination, 5));

    check_equal(native_clear(&NativeIntSet_collection_data, &destination),
                DATA_BIND_OK);
    check_equal(native_clear(&NativeIntSet_collection_data, &source),
                DATA_BIND_OK);
  }

  it("round-trips typed Map<int,long> without raw map storage knowledge") {
    NativeIntLongMap source = {0};
    NativeIntLongMap destination = {0};
    TokenSink sink;
    const long *value;

    check_equal(native_init(&NativeIntLongMap_map_data, &source),
                DATA_BIND_OK);
    check_equal(native_init(&NativeIntLongMap_map_data, &destination),
                DATA_BIND_OK);
    check_equal(NativeIntLongMap_put(&source, 2, 20L), STL_OK);
    check_equal(NativeIntLongMap_put(&source, 1, 10L), STL_OK);

    check_equal(roundtrip(
                    &NativeIntLongMap_map_data, &source, &destination,
                    &sink),
                DATA_BIND_OK);
    check_true(sink.count >= (size_t)6u);
    check_true(sink.tokens[0].kind == CSERDE_MAP_BEGIN);
    check_true(sink.tokens[sink.count - 1u].kind == CSERDE_MAP_END);
    check_equal(NativeIntLongMap_size(&destination), (size_t)2u);
    value = NativeIntLongMap_get_const(&destination, 1);
    check_not_null(value);
    check_equal(*value, 10L);
    value = NativeIntLongMap_get_const(&destination, 2);
    check_not_null(value);
    check_equal(*value, 20L);

    check_equal(native_clear(&NativeIntLongMap_map_data, &destination),
                DATA_BIND_OK);
    check_equal(native_clear(&NativeIntLongMap_map_data, &source),
                DATA_BIND_OK);
  }

  it("uses Salts 1.7.2 managed traits for explicit Vec<tstr>") {
    NativeTextVec source = {0};
    NativeTextVec destination = {0};
    TokenSink sink;
    tstr first = NULL;
    tstr second = NULL;
    const tstr *value;

    check_equal(native_init(&NativeTextVec_collection_data, &source),
                DATA_BIND_OK);
    check_equal(native_init(&NativeTextVec_collection_data, &destination),
                DATA_BIND_OK);
    first = tstr_dup("alpha");
    second = tstr_dup("beta");
    check_not_null(first);
    check_not_null(second);
    check_equal(NativeTextVec_push(&source, first), STL_OK);
    check_equal(NativeTextVec_push(&source, second), STL_OK);
    tstr_free(first);
    tstr_free(second);
    first = NULL;
    second = NULL;

    check_equal(roundtrip(
                    &NativeTextVec_collection_data, &source, &destination,
                    &sink),
                DATA_BIND_OK);
    check_equal(NativeTextVec_size(&destination), (size_t)2u);
    value = NativeTextVec_at_const(&destination, 0u);
    check_not_null(value);
    check_equal(tstr_len(*value), (size_t)5u);
    check_equal(memcmp(*value, "alpha", 5u), 0);
    value = NativeTextVec_at_const(&destination, 1u);
    check_not_null(value);
    check_equal(tstr_len(*value), (size_t)4u);
    check_equal(memcmp(*value, "beta", 4u), 0);

    check_equal(native_clear(&NativeTextVec_collection_data, &destination),
                DATA_BIND_OK);
    check_equal(native_clear(&NativeTextVec_collection_data, &source),
                DATA_BIND_OK);
  }

  it("aborts a partially decoded Vec and leaves destination empty") {
    NativeIntVec destination = {0};
    cserde_token tokens[4] = {0};
    NativeContainerTokenSource source = {0};
    cserde_reader reader = {0};

    tokens[0].kind = CSERDE_ARRAY_BEGIN;
    tokens[1].kind = CSERDE_SINT;
    tokens[1].value.sint = 7;
    tokens[2].kind = CSERDE_STRING;
    tokens[2].value.slice.data = (const unsigned char *)"bad";
    tokens[2].value.slice.size = 3u;
    tokens[2].value.slice.lifetime = CSERDE_VIEW_STABLE;
    tokens[3].kind = CSERDE_ARRAY_END;

    check_equal(native_init(&NativeIntVec_collection_data, &destination),
                DATA_BIND_OK);
    source.tokens = tokens;
    source.count = 4u;
    check_equal(cserde_reader_init(&reader, &SOURCE_OPS, &source), CSERDE_OK);
    check_equal(
        data_bind_native_decode(
            &options, &NativeIntVec_collection_data, &reader,
            &destination, sizeof(destination), &diagnostic),
        DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(NativeIntVec_size(&destination), (size_t)0u);
    check_equal(native_clear(&NativeIntVec_collection_data, &destination),
                DATA_BIND_OK);
  }
}
