#include "data_bind_yaml_provider.h"

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

  return 0;
}
