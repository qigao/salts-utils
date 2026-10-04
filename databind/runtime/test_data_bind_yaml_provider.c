#include "data_bind_yaml_provider.h"

#include <cserde/reader.h>
#include <cserde/writer.h>

#include <string.h>

static int next_kind(cserde_reader *reader, cserde_token_kind kind,
                     cserde_token *token) {
  memset(token, 0, sizeof(*token));
  return cserde_reader_next(reader, token) == CSERDE_OK &&
         token->kind == kind;
}

static int slice_equal(const cserde_token *token, const char *text) {
  size_t len = strlen(text);
  return token->kind == CSERDE_STRING &&
         token->value.slice.size == len &&
         memcmp(token->value.slice.data, text, len) == 0;
}

static int read_id_object(cserde_reader *reader, uint64_t expected) {
  cserde_token token = {0};
  return next_kind(reader, CSERDE_MAP_BEGIN, &token) &&
      next_kind(reader, CSERDE_STRING, &token) &&
      slice_equal(&token, "id") &&
      next_kind(reader, CSERDE_UINT, &token) &&
      token.value.uint == expected &&
      next_kind(reader, CSERDE_MAP_END, &token);
}

typedef struct YamlOutput {
  char data[1024];
  size_t size;
  int fail;
  size_t calls;
} YamlOutput;

static int yaml_output_write(const void *data, size_t len, void *user) {
  YamlOutput *out = (YamlOutput *)user;
  if (out == NULL || (data == NULL && len != 0u)) return -1;
  ++out->calls;
  if (out->fail || len > sizeof(out->data) - out->size) return -1;
  if (len != 0u) memcpy(out->data + out->size, data, len);
  out->size += len;
  return 0;
}

static cserde_token yaml_key(const char *text) {
  cserde_token token = {0};
  token.kind = CSERDE_STRING;
  token.value.slice.data = (const unsigned char *)text;
  token.value.slice.size = strlen(text);
  token.value.slice.lifetime = CSERDE_VIEW_STABLE;
  return token;
}

int main(void) {
  static const char yaml[] =
      "items:\n"
      "  - id: 7\n"
      "  - id: 8\n";
  DataBindFormatReader lease = DATA_BIND_FORMAT_READER_INIT;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindQueryLimits limits = DATA_BIND_QUERY_LIMITS_INIT;
  DataBindQueryDiagnostic diagnostic = DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  cserde_token token = {0};

  if (data_bind_format_reader_open(
          data_bind_yaml_format_provider(),
          yaml, sizeof(yaml) - 1u, 8u, &lease, &error) != DATA_BIND_OK)
    return 1;
  if (!next_kind(lease.reader, CSERDE_MAP_BEGIN, &token)) return 2;
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "items")) return 3;
  if (!next_kind(lease.reader, CSERDE_ARRAY_BEGIN, &token)) return 4;
  if (!read_id_object(lease.reader, 7u)) return 5;
  if (!read_id_object(lease.reader, 8u)) return 6;
  if (!next_kind(lease.reader, CSERDE_ARRAY_END, &token)) return 7;
  if (!next_kind(lease.reader, CSERDE_MAP_END, &token)) return 8;
  if (cserde_reader_next(lease.reader, &token) != CSERDE_DONE) return 9;
  if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return 10;

  lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
  error = (DataBindError)DATA_BIND_ERROR_INIT;
  diagnostic = (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  if (data_bind_format_reader_open_selected(
          data_bind_yaml_format_provider(),
          yaml, sizeof(yaml) - 1u, 8u,
          DATA_BIND_STREAM_SELECT_PATH_FIRST, "/items[1]",
          &limits, &diagnostic, &lease, &error) != DATA_BIND_OK)
    return 11;
  if (!read_id_object(lease.reader, 8u)) return 12;
  if (cserde_reader_next(lease.reader, &token) != CSERDE_DONE) return 13;
  if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return 14;

  lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
  error = (DataBindError)DATA_BIND_ERROR_INIT;
  diagnostic = (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  if (data_bind_format_reader_open_selected(
          data_bind_yaml_format_provider(),
          yaml, sizeof(yaml) - 1u, 8u,
          DATA_BIND_STREAM_SELECT_PATH_ALL, "/items/*",
          &limits, &diagnostic, &lease, &error) != DATA_BIND_OK)
    return 15;
  if (!next_kind(lease.reader, CSERDE_ARRAY_BEGIN, &token)) return 16;
  if (!read_id_object(lease.reader, 7u)) return 17;
  if (!read_id_object(lease.reader, 8u)) return 18;
  if (!next_kind(lease.reader, CSERDE_ARRAY_END, &token)) return 19;
  if (cserde_reader_next(lease.reader, &token) != CSERDE_DONE) return 20;
  if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return 21;

  lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
  error = (DataBindError)DATA_BIND_ERROR_INIT;
  diagnostic = (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  if (data_bind_format_reader_open_selected(
          data_bind_yaml_format_provider(),
          yaml, sizeof(yaml) - 1u, 8u,
          DATA_BIND_STREAM_SELECT_PATH_FIRST, "[",
          &limits, &diagnostic, &lease, &error) == DATA_BIND_OK)
    return 22;
  if (lease.reader != NULL) return 23;

  {
    DataBindFormatWriter writer = DATA_BIND_FORMAT_WRITER_INIT;
    YamlOutput output = {{0}, 0u, 0, 0u};
    cserde_token key = {0};
    cserde_token value = {0};

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    if (data_bind_format_writer_open(
            data_bind_yaml_format_provider(),
            yaml_output_write, &output, 4u, &writer, &error) != DATA_BIND_OK)
      return 24;

    value = (cserde_token){.kind = CSERDE_MAP_BEGIN};
    if (cserde_writer_write(writer.writer, &value) != CSERDE_OK) return 25;
    key = yaml_key("id");
    if (cserde_writer_write(writer.writer, &key) != CSERDE_OK) return 26;
    value = (cserde_token){.kind = CSERDE_UINT, .value.uint = 7u};
    if (cserde_writer_write(writer.writer, &value) != CSERDE_OK) return 27;
    key = yaml_key("items");
    if (cserde_writer_write(writer.writer, &key) != CSERDE_OK) return 28;
    value = (cserde_token){.kind = CSERDE_ARRAY_BEGIN};
    if (cserde_writer_write(writer.writer, &value) != CSERDE_OK) return 29;
    value = (cserde_token){.kind = CSERDE_SINT, .value.sint = -2};
    if (cserde_writer_write(writer.writer, &value) != CSERDE_OK) return 30;
    value = yaml_key("ok");
    if (cserde_writer_write(writer.writer, &value) != CSERDE_OK) return 31;
    value = (cserde_token){.kind = CSERDE_ARRAY_END};
    if (cserde_writer_write(writer.writer, &value) != CSERDE_OK) return 32;
    key = yaml_key("nil");
    if (cserde_writer_write(writer.writer, &key) != CSERDE_OK) return 33;
    value = (cserde_token){.kind = CSERDE_NULL};
    if (cserde_writer_write(writer.writer, &value) != CSERDE_OK) return 34;
    value = (cserde_token){.kind = CSERDE_MAP_END};
    if (cserde_writer_write(writer.writer, &value) != CSERDE_OK) return 35;
    if (data_bind_format_writer_close(&writer, &error) != DATA_BIND_OK)
      return 36;
    if (output.calls != 1u || output.size == 0u) return 37;

    lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
    if (data_bind_format_reader_open(
            data_bind_yaml_format_provider(),
            output.data, output.size, 4u, &lease, &error) != DATA_BIND_OK)
      return 38;
    if (!next_kind(lease.reader, CSERDE_MAP_BEGIN, &token)) return 39;
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        !slice_equal(&token, "id")) return 40;
    if (!next_kind(lease.reader, CSERDE_UINT, &token) ||
        token.value.uint != 7u) return 41;
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        !slice_equal(&token, "items")) return 42;
    if (!next_kind(lease.reader, CSERDE_ARRAY_BEGIN, &token)) return 43;
    if (!next_kind(lease.reader, CSERDE_SINT, &token) ||
        token.value.sint != -2) return 44;
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        !slice_equal(&token, "ok")) return 45;
    if (!next_kind(lease.reader, CSERDE_ARRAY_END, &token)) return 46;
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        !slice_equal(&token, "nil")) return 47;
    if (!next_kind(lease.reader, CSERDE_NULL, &token)) return 48;
    if (!next_kind(lease.reader, CSERDE_MAP_END, &token)) return 49;
    if (cserde_reader_next(lease.reader, &token) != CSERDE_DONE) return 50;
    if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return 51;
  }

  {
    DataBindFormatWriter writer = DATA_BIND_FORMAT_WRITER_INIT;
    YamlOutput output = {{0}, 0u, 0, 0u};
    cserde_token token_out = {.kind = CSERDE_UINT, .value.uint = UINT64_C(9)};
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    if (data_bind_format_writer_open(
            data_bind_yaml_format_provider(),
            yaml_output_write, &output, 0u, &writer, &error) != DATA_BIND_OK)
      return 52;
    if (cserde_writer_write(writer.writer, &token_out) != CSERDE_OK) return 53;
    if (data_bind_format_writer_close(&writer, &error) != DATA_BIND_OK)
      return 54;
    if (output.calls != 1u || output.size == 0u) return 55;
  }

  {
    DataBindFormatWriter writer = DATA_BIND_FORMAT_WRITER_INIT;
    YamlOutput output = {{0}, 0u, 1, 0u};
    cserde_token token_out = {.kind = CSERDE_UINT, .value.uint = UINT64_C(5)};
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    if (data_bind_format_writer_open(
            data_bind_yaml_format_provider(),
            yaml_output_write, &output, 0u, &writer, &error) != DATA_BIND_OK)
      return 56;
    if (cserde_writer_write(writer.writer, &token_out) != CSERDE_OK) return 57;
    if (data_bind_format_writer_close(&writer, &error) != DATA_BIND_ERR_IO)
      return 58;
  }

  {
    DataBindFormatWriter writer = DATA_BIND_FORMAT_WRITER_INIT;
    YamlOutput output = {{0}, 0u, 0, 0u};
    static const unsigned char raw[] = {0x01u, 0x02u};
    cserde_token token_out = {
        .kind = CSERDE_BYTES,
        .value.slice = {raw, sizeof(raw), CSERDE_VIEW_STABLE}};
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    if (data_bind_format_writer_open(
            data_bind_yaml_format_provider(),
            yaml_output_write, &output, 1u, &writer, &error) != DATA_BIND_OK)
      return 59;
    if (cserde_writer_write(writer.writer, &token_out) != CSERDE_UNSUPPORTED)
      return 60;
  }

  return 0;
}
