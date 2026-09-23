#include "data_bind_json_provider.h"

#include <cserde/reader.h>

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

int main(void) {
  static const char json[] = "{\"id\":7}";
  static const char nested[] =
      "{\"items\":[{\"id\":7},{\"id\":8}]}";
  DataBindFormatReader lease = DATA_BIND_FORMAT_READER_INIT;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindQueryLimits limits = DATA_BIND_QUERY_LIMITS_INIT;
  DataBindQueryDiagnostic diagnostic = DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  cserde_token token = {0};

  if (data_bind_format_reader_open(
          data_bind_json_format_provider(),
          json, sizeof(json) - 1u, 8u, &lease, &error) != DATA_BIND_OK)
    return 1;
  if (!read_id_object(lease.reader, 7u)) return 2;
  if (cserde_reader_next(lease.reader, &token) != CSERDE_DONE) return 3;
  if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return 4;

  error = (DataBindError)DATA_BIND_ERROR_INIT;
  if (data_bind_format_reader_open(
          data_bind_json_format_provider(),
          "{", 1u, 8u, &lease, &error) != DATA_BIND_ERR_PARSE)
    return 5;
  if (lease.reader != NULL || error.code != DATA_BIND_ERR_PARSE) return 6;

  lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
  error = (DataBindError)DATA_BIND_ERROR_INIT;
  diagnostic = (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  if (data_bind_format_reader_open_selected(
          data_bind_json_format_provider(),
          nested, sizeof(nested) - 1u, 8u,
          DATA_BIND_STREAM_SELECT_PATH_FIRST, "$.items[1]",
          &limits, &diagnostic, &lease, &error) != DATA_BIND_OK)
    return 7;
  if (!read_id_object(lease.reader, 8u)) return 8;
  if (cserde_reader_next(lease.reader, &token) != CSERDE_DONE) return 9;
  if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return 10;

  lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
  error = (DataBindError)DATA_BIND_ERROR_INIT;
  diagnostic = (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  if (data_bind_format_reader_open_selected(
          data_bind_json_format_provider(),
          nested, sizeof(nested) - 1u, 8u,
          DATA_BIND_STREAM_SELECT_PATH_ALL, "$.items[*]",
          &limits, &diagnostic, &lease, &error) != DATA_BIND_OK)
    return 11;
  if (!next_kind(lease.reader, CSERDE_ARRAY_BEGIN, &token)) return 12;
  if (!read_id_object(lease.reader, 7u)) return 13;
  if (!read_id_object(lease.reader, 8u)) return 14;
  if (!next_kind(lease.reader, CSERDE_ARRAY_END, &token)) return 15;
  if (cserde_reader_next(lease.reader, &token) != CSERDE_DONE) return 16;
  if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return 17;

  lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
  error = (DataBindError)DATA_BIND_ERROR_INIT;
  diagnostic = (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  if (data_bind_format_reader_open_selected(
          data_bind_json_format_provider(),
          nested, sizeof(nested) - 1u, 8u,
          DATA_BIND_STREAM_SELECT_PATH_FIRST, "$[",
          &limits, &diagnostic, &lease, &error) == DATA_BIND_OK)
    return 18;
  if (lease.reader != NULL || diagnostic.status == DATA_BIND_QUERY_OK)
    return 19;

  return 0;
}
