#include "data_bind_native.h"

#include <cmeta/enum.h>
#include <cmeta/struct.h>
#include <salts_cmeta_data.h>
#include <tinytest.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
  WRITER_WORKSPACE_BYTES = 4096,
  WRITER_MAX_DEPTH = 8,
  WRITER_MAX_ITEMS = 64,
  WRITER_MAX_OWNED_BYTES = 64,
  WRITER_MAX_TOKENS = 32
};

typedef struct WriterWorkspace {
  unsigned char storage[WRITER_WORKSPACE_BYTES];
} WriterWorkspace;

typedef struct TokenSink {
  cserde_token tokens[WRITER_MAX_TOKENS];
  size_t count;
  size_t fail_at;
  size_t finish_calls;
} TokenSink;

typedef struct NativeWriterTokenSource {
  const cserde_token *tokens;
  size_t count;
  size_t index;
} NativeWriterTokenSource;

static cserde_status token_sink_write(void *context,
                                      const cserde_token *token) {
  TokenSink *sink = (TokenSink *)context;
  if (sink == NULL || token == NULL) return CSERDE_INVALID_ARGUMENT;
  if (sink->count == sink->fail_at) return CSERDE_SINK_ERROR;
  if (sink->count == WRITER_MAX_TOKENS) return CSERDE_LIMIT_EXCEEDED;
  sink->tokens[sink->count++] = *token;
  return CSERDE_OK;
}

static cserde_status token_sink_finish(void *context) {
  TokenSink *sink = (TokenSink *)context;
  if (sink == NULL) return CSERDE_INVALID_ARGUMENT;
  ++sink->finish_calls;
  return CSERDE_OK;
}

static const cserde_writer_ops TOKEN_SINK_OPS = {
    sizeof(cserde_writer_ops),
    CSERDE_WRITER_OPS_ABI_VERSION,
    token_sink_write,
    token_sink_finish};

static cserde_status token_source_next(void *context, cserde_token *out) {
  NativeWriterTokenSource *source = (NativeWriterTokenSource *)context;
  if (source == NULL || out == NULL) return CSERDE_INVALID_ARGUMENT;
  if (source->index == source->count) return CSERDE_DONE;
  *out = source->tokens[source->index++];
  return CSERDE_OK;
}

static const cserde_reader_ops TOKEN_SOURCE_OPS = {
    sizeof(cserde_reader_ops),
    CSERDE_READER_OPS_ABI_VERSION,
    token_source_next};

static WriterWorkspace workspace;
static DataBindNativeOptions options;
static DataBindNativeDiagnostic diagnostic;

static void reset_native(void) {
  memset(&workspace, 0, sizeof(workspace));
  options = (DataBindNativeOptions)DATA_BIND_NATIVE_OPTIONS_INIT;
  diagnostic = (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  options.workspace = workspace.storage;
  options.workspace_bytes = sizeof(workspace.storage);
  options.max_depth = WRITER_MAX_DEPTH;
  options.max_items = WRITER_MAX_ITEMS;
  options.max_owned_bytes = WRITER_MAX_OWNED_BYTES;
}

static void open_writer(TokenSink *sink, cserde_writer *writer) {
  memset(sink, 0, sizeof(*sink));
  sink->fail_at = SIZE_MAX;
  memset(writer, 0, sizeof(*writer));
  check_equal(cserde_writer_init(writer, &TOKEN_SINK_OPS, sink), CSERDE_OK);
}

static void open_reader(const TokenSink *sink, NativeWriterTokenSource *source,
                        cserde_reader *reader) {
  source->tokens = sink->tokens;
  source->count = sink->count;
  source->index = 0u;
  memset(reader, 0, sizeof(*reader));
  check_equal(cserde_reader_init(reader, &TOKEN_SOURCE_OPS, source), CSERDE_OK);
}

static DataBindStatus encode_value(const cmeta_data_desc *data,
                                   const void *source, size_t source_bytes,
                                   cserde_writer *writer) {
  return data_bind_native_encode(&options, data, source, source_bytes, writer,
                                 &diagnostic);
}

static DataBindStatus decode_value(const cmeta_data_desc *data,
                                   cserde_reader *reader,
                                   void *destination, size_t destination_bytes) {
  return data_bind_native_decode(&options, data, reader, destination,
                                 destination_bytes, &diagnostic);
}

typedef struct NativeEnumBox {
  uint64_t bits;
  bool engaged;
} NativeEnumBox;

static bool enum_is_zero(const void *object) {
  const NativeEnumBox *value = (const NativeEnumBox *)object;
  return value != NULL && !value->engaged && value->bits == 0u;
}

static cmeta_status enum_read(const void *object, uint64_t *out) {
  const NativeEnumBox *value = (const NativeEnumBox *)object;
  if (value == NULL || out == NULL) return CMETA_INVALID_ARGUMENT;
  if (!value->engaged) return CMETA_CALLBACK_ERROR;
  *out = value->bits;
  return CMETA_OK;
}

static cmeta_status enum_assign(void *object, uint64_t bits) {
  NativeEnumBox *value = (NativeEnumBox *)object;
  if (value == NULL) return CMETA_INVALID_ARGUMENT;
  value->bits = bits;
  value->engaged = true;
  return CMETA_OK;
}

static void enum_restore(void *object) {
  NativeEnumBox *value = (NativeEnumBox *)object;
  if (value == NULL) return;
  value->bits = 0u;
  value->engaged = false;
}

static cmeta_status enum_init_zero(void *object) {
  if (object == NULL) return CMETA_INVALID_ARGUMENT;
  enum_restore(object);
  return CMETA_OK;
}

static void enum_move(void *destination, void *source) {
  NativeEnumBox *to = (NativeEnumBox *)destination;
  NativeEnumBox *from = (NativeEnumBox *)source;
  if (to == NULL || from == NULL || to == from) return;
  *to = *from;
  enum_restore(from);
}

static const cmeta_type_identity ENUM_IDENTITY =
    CMETA_TYPE_ID_ATOM_INIT("test.native-writer.Enum");
static const cmeta_type_desc ENUM_TYPE = {
    "NativeEnumBox", sizeof(NativeEnumBox), _Alignof(NativeEnumBox),
    CMETA_T_OBJECT, NULL, NULL, &ENUM_IDENTITY};
static const cmeta_data_construct_ops ENUM_CONSTRUCT_OPS = {
    sizeof(cmeta_data_construct_ops), CMETA_DATA_CONSTRUCT_OPS_ABI_VERSION,
    &ENUM_TYPE, enum_init_zero, enum_restore, enum_move};
static const cmeta_enum_bits_item ENUM_ITEMS[] = {
    {0u, "ZERO", "zero"},
    {1u, "READY", "ready"}};
static const cmeta_enum_domain ENUM_DOMAIN = {
    sizeof(cmeta_enum_domain), CMETA_ENUM_DOMAIN_ABI_VERSION,
    CMETA_ENUM_UNSIGNED, 8u, CMETA_ENUM_ORDINARY,
    ENUM_ITEMS, 2u, 0u};
static const cmeta_data_enum_bits_ops ENUM_OPS = {
    sizeof(cmeta_data_enum_bits_ops), CMETA_DATA_ENUM_BITS_OPS_ABI_VERSION,
    &ENUM_TYPE, &ENUM_DOMAIN, enum_is_zero, enum_read, enum_assign, enum_restore};
static const cmeta_data_desc ENUM_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.native-writer.Enum.data",
    .display_name = "NativeEnum",
    .kind = CMETA_DATA_ENUM,
    .storage_type = &ENUM_TYPE,
    .enum_bits_ops = &ENUM_OPS,
    .construct_ops = &ENUM_CONSTRUCT_OPS};

Struct(WriterRow, (int, id), (tstr, name));
static const cmeta_type_identity ROW_IDENTITY =
    CMETA_TYPE_ID_ATOM_INIT("test.native-writer.WriterRow");
static const cmeta_type_desc ROW_TYPE = {
    "WriterRow", sizeof(WriterRow), _Alignof(WriterRow),
    CMETA_T_OBJECT, NULL, NULL, &ROW_IDENTITY};
static cmeta_field_desc ROW_LAYOUT_FIELDS[2];
static cmeta_data_field_desc ROW_FIELDS[2];
static const cmeta_struct_desc ROW_LAYOUT = {
    "WriterRow", sizeof(WriterRow), _Alignof(WriterRow),
    ROW_LAYOUT_FIELDS, 2u};
static const cmeta_data_struct_shape ROW_SHAPE = {
    &ROW_LAYOUT, ROW_FIELDS, 2u};
static const cmeta_data_desc ROW_DATA = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.native-writer.WriterRow.data",
    .display_name = "WriterRow",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &ROW_TYPE,
    .shape = &ROW_SHAPE};

static void prepare_row_descriptor(void) {
  ROW_LAYOUT_FIELDS[0] = StructMeta(WriterRow)->fields[0];
  ROW_LAYOUT_FIELDS[1] = StructMeta(WriterRow)->fields[1];
  ROW_LAYOUT_FIELDS[1].type = salts_tstr_cmeta_data.storage_type;
  ROW_FIELDS[0] = (cmeta_data_field_desc){
      "test.native-writer.WriterRow.id", "id",
      offsetof(WriterRow, id), &cmeta_data_int};
  ROW_FIELDS[1] = (cmeta_data_field_desc){
      "test.native-writer.WriterRow.name", "name",
      offsetof(WriterRow, name), &salts_tstr_cmeta_data};
  check_true(cmeta_data_desc_valid(&ROW_DATA));
}

spec("DataBind native writer contract") {
  (void)ttest_config__;

  before_each() {
    reset_native();
    prepare_row_descriptor();
  }

  it("round-trips a signed scalar through CSerde tokens") {
    int32_t source = -123;
    int32_t destination = 0;
    TokenSink sink;
    NativeWriterTokenSource token_source;
    cserde_writer writer;
    cserde_reader reader;

    open_writer(&sink, &writer);
    check_equal(encode_value(&cmeta_data_int32, &source, sizeof(source),
                             &writer),
                DATA_BIND_OK);
    check_equal(source, -123);
    check_equal(sink.count, 1u);
    check_true(sink.tokens[0].kind == CSERDE_SINT);
    check_equal(sink.tokens[0].value.sint, INT64_C(-123));
    check_equal(sink.finish_calls, 0u);
    check_true(writer.state == CSERDE_WRITER_READY);

    open_reader(&sink, &token_source, &reader);
    check_equal(decode_value(&cmeta_data_int32, &reader, &destination,
                             sizeof(destination)),
                DATA_BIND_OK);
    check_equal(destination, -123);
  }

  it("round-trips canonical enum bits without inventing text ownership") {
    NativeEnumBox source = {1u, true};
    NativeEnumBox destination = {0u, false};
    TokenSink sink;
    NativeWriterTokenSource token_source;
    cserde_writer writer;
    cserde_reader reader;

    open_writer(&sink, &writer);
    check_equal(encode_value(&ENUM_DATA, &source, sizeof(source), &writer),
                DATA_BIND_OK);
    check_equal(source.bits, UINT64_C(1));
    check_true(source.engaged);
    check_equal(sink.count, 1u);
    check_true(sink.tokens[0].kind == CSERDE_UINT);
    check_equal(sink.tokens[0].value.uint, UINT64_C(1));

    open_reader(&sink, &token_source, &reader);
    check_equal(decode_value(&ENUM_DATA, &reader, &destination,
                             sizeof(destination)),
                DATA_BIND_OK);
    check_equal(destination.bits, UINT64_C(1));
    check_true(destination.engaged);
    enum_restore(&destination);
  }

  it("round-trips owned string and bytes through canonical buffer read") {
    static const unsigned char bytes[] = {'A', 0u, 'B'};
    cmeta_data_desc bytes_data = salts_tstr_cmeta_data;
    tstr text = NULL;
    tstr decoded_text = NULL;
    tstr raw = NULL;
    tstr decoded_raw = NULL;
    TokenSink sink;
    NativeWriterTokenSource token_source;
    cserde_writer writer;
    cserde_reader reader;

    bytes_data.kind = CMETA_DATA_BYTES;
    bytes_data.stable_id = "test.native-writer.bytes";
    bytes_data.display_name = "bytes";

    check_equal(cmeta_data_buffer_init_zero(&salts_tstr_cmeta_data, &text),
                CMETA_OK);
    check_equal(cmeta_data_buffer_init_zero(&salts_tstr_cmeta_data,
                                             &decoded_text),
                CMETA_OK);
    check_equal(cmeta_data_buffer_init_zero(&bytes_data, &raw), CMETA_OK);
    check_equal(cmeta_data_buffer_init_zero(&bytes_data, &decoded_raw),
                CMETA_OK);

    check_equal(cmeta_data_buffer_assign(
                    &salts_tstr_cmeta_data, &text,
                    (const unsigned char *)"Alice", 5u, 5u),
                CMETA_OK);
    check_equal(cmeta_data_buffer_assign(
                    &bytes_data, &raw, bytes, sizeof(bytes), sizeof(bytes)),
                CMETA_OK);

    open_writer(&sink, &writer);
    check_equal(encode_value(&salts_tstr_cmeta_data, &text, sizeof(text),
                             &writer),
                DATA_BIND_OK);
    check_true(sink.tokens[0].kind == CSERDE_STRING);
    check_equal(sink.tokens[0].value.slice.size, 5u);
    open_reader(&sink, &token_source, &reader);
    check_equal(decode_value(&salts_tstr_cmeta_data, &reader, &decoded_text,
                             sizeof(decoded_text)),
                DATA_BIND_OK);
    check_equal(tstr_len(decoded_text), 5u);
    check_equal(memcmp(decoded_text, "Alice", 5u), 0);

    open_writer(&sink, &writer);
    check_equal(encode_value(&bytes_data, &raw, sizeof(raw), &writer),
                DATA_BIND_OK);
    check_true(sink.tokens[0].kind == CSERDE_BYTES);
    check_equal(sink.tokens[0].value.slice.size, sizeof(bytes));
    open_reader(&sink, &token_source, &reader);
    check_equal(decode_value(&bytes_data, &reader, &decoded_raw,
                             sizeof(decoded_raw)),
                DATA_BIND_OK);
    check_equal(tstr_len(decoded_raw), sizeof(bytes));
    check_equal(memcmp(decoded_raw, bytes, sizeof(bytes)), 0);

    (void)cmeta_data_buffer_restore_zero(&salts_tstr_cmeta_data, &text);
    (void)cmeta_data_buffer_restore_zero(&salts_tstr_cmeta_data,
                                         &decoded_text);
    (void)cmeta_data_buffer_restore_zero(&bytes_data, &raw);
    (void)cmeta_data_buffer_restore_zero(&bytes_data, &decoded_raw);
  }

  it("round-trips a Struct using canonical field names") {
    WriterRow source = {0};
    WriterRow destination = {0};
    TokenSink sink;
    NativeWriterTokenSource token_source;
    cserde_writer writer;
    cserde_reader reader;

    source.id = 7;
    check_equal(cmeta_data_buffer_init_zero(&salts_tstr_cmeta_data,
                                             &source.name),
                CMETA_OK);
    check_equal(cmeta_data_buffer_init_zero(&salts_tstr_cmeta_data,
                                             &destination.name),
                CMETA_OK);
    check_equal(cmeta_data_buffer_assign(
                    &salts_tstr_cmeta_data, &source.name,
                    (const unsigned char *)"Bob", 3u, 3u),
                CMETA_OK);

    open_writer(&sink, &writer);
    check_equal(encode_value(&ROW_DATA, &source, sizeof(source), &writer),
                DATA_BIND_OK);
    check_equal(source.id, 7);
    check_equal(tstr_len(source.name), 3u);
    check_equal(sink.count, 6u);
    check_true(sink.tokens[0].kind == CSERDE_MAP_BEGIN);
    check_true(sink.tokens[1].kind == CSERDE_STRING);
    check_true(sink.tokens[2].kind == CSERDE_SINT);
    check_true(sink.tokens[3].kind == CSERDE_STRING);
    check_true(sink.tokens[4].kind == CSERDE_STRING);
    check_true(sink.tokens[5].kind == CSERDE_MAP_END);

    open_reader(&sink, &token_source, &reader);
    check_equal(decode_value(&ROW_DATA, &reader, &destination,
                             sizeof(destination)),
                DATA_BIND_OK);
    check_equal(destination.id, 7);
    check_equal(tstr_len(destination.name), 3u);
    check_equal(memcmp(destination.name, "Bob", 3u), 0);

    (void)cmeta_data_buffer_restore_zero(&salts_tstr_cmeta_data, &source.name);
    (void)cmeta_data_buffer_restore_zero(&salts_tstr_cmeta_data,
                                         &destination.name);
  }

  it("propagates writer failure without mutating native source") {
    WriterRow source = {0};
    TokenSink sink;
    cserde_writer writer;

    source.id = 9;
    check_equal(cmeta_data_buffer_init_zero(&salts_tstr_cmeta_data,
                                             &source.name),
                CMETA_OK);
    check_equal(cmeta_data_buffer_assign(
                    &salts_tstr_cmeta_data, &source.name,
                    (const unsigned char *)"Safe", 4u, 4u),
                CMETA_OK);

    open_writer(&sink, &writer);
    sink.fail_at = 2u;
    check_equal(encode_value(&ROW_DATA, &source, sizeof(source), &writer),
                DATA_BIND_ERR_IO);
    check_equal(source.id, 9);
    check_equal(tstr_len(source.name), 4u);
    check_equal(memcmp(source.name, "Safe", 4u), 0);
    check_equal(diagnostic.source_status, CSERDE_SINK_ERROR);
    check_true(writer.state == CSERDE_WRITER_FAILED);
    check_equal(sink.finish_calls, 0u);

    (void)cmeta_data_buffer_restore_zero(&salts_tstr_cmeta_data, &source.name);
  }

  it("validates the full graph before the first writer callback") {
    cmeta_data_desc invalid = ROW_DATA;
    WriterRow source = {0};
    TokenSink sink;
    cserde_writer writer;

    invalid.storage_type = NULL;
    open_writer(&sink, &writer);
    check_equal(encode_value(&invalid, &source, sizeof(source), &writer),
                DATA_BIND_ERR_SCHEMA);
    check_equal(sink.count, 0u);
  }

  it("enforces aggregate owned payload budget without mutating source") {
    tstr text = NULL;
    TokenSink sink;
    cserde_writer writer;

    check_equal(cmeta_data_buffer_init_zero(&salts_tstr_cmeta_data, &text),
                CMETA_OK);
    check_equal(cmeta_data_buffer_assign(
                    &salts_tstr_cmeta_data, &text,
                    (const unsigned char *)"four", 4u, 4u),
                CMETA_OK);

    options.max_owned_bytes = 3u;
    open_writer(&sink, &writer);
    check_equal(encode_value(&salts_tstr_cmeta_data, &text, sizeof(text),
                             &writer),
                DATA_BIND_ERR_LIMIT);
    check_equal(sink.count, 0u);
    check_equal(tstr_len(text), 4u);
    check_equal(memcmp(text, "four", 4u), 0);

    (void)cmeta_data_buffer_restore_zero(&salts_tstr_cmeta_data, &text);
  }
}
