#include "data_bind_csv_provider.h"

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

static int read_row(cserde_reader *reader, const char *id,
                    const char *name) {
  cserde_token token = {0};
  return next_kind(reader, CSERDE_MAP_BEGIN, &token) &&
      next_kind(reader, CSERDE_STRING, &token) &&
      slice_equal(&token, "id") &&
      next_kind(reader, CSERDE_STRING, &token) &&
      slice_equal(&token, id) &&
      next_kind(reader, CSERDE_STRING, &token) &&
      slice_equal(&token, "name") &&
      next_kind(reader, CSERDE_STRING, &token) &&
      slice_equal(&token, name) &&
      next_kind(reader, CSERDE_MAP_END, &token);
}

int main(void) {
  static const char csv[] = "id,name\n7,alice\n8,bob\n9,bob\n";
  DataBindFormatReader lease = DATA_BIND_FORMAT_READER_INIT;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindQueryLimits limits = DATA_BIND_QUERY_LIMITS_INIT;
  DataBindQueryDiagnostic diagnostic = DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  cserde_token token = {0};

  if (data_bind_format_reader_open(
          data_bind_csv_format_provider(),
          csv, sizeof(csv) - 1u, 1u, &lease, &error) != DATA_BIND_ERR_LIMIT)
    return 1;
  if (lease.reader != NULL || error.code != DATA_BIND_ERR_LIMIT) return 2;

  error = (DataBindError)DATA_BIND_ERROR_INIT;
  lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
  if (data_bind_format_reader_open(
          data_bind_csv_format_provider(),
          csv, sizeof(csv) - 1u, 2u, &lease, &error) != DATA_BIND_OK)
    return 3;
  if (!next_kind(lease.reader, CSERDE_ARRAY_BEGIN, &token)) return 4;
  if (!read_row(lease.reader, "7", "alice")) return 5;
  if (!read_row(lease.reader, "8", "bob")) return 6;
  if (!read_row(lease.reader, "9", "bob")) return 7;
  if (!next_kind(lease.reader, CSERDE_ARRAY_END, &token)) return 8;
  if (cserde_reader_next(lease.reader, &token) != CSERDE_DONE) return 9;
  if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return 10;

  lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
  error = (DataBindError)DATA_BIND_ERROR_INIT;
  diagnostic = (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  if (data_bind_format_reader_open_selected(
          data_bind_csv_format_provider(),
          csv, sizeof(csv) - 1u, 2u,
          DATA_BIND_STREAM_SELECT_PATH_FIRST, "name == \"bob\"",
          &limits, &diagnostic, &lease, &error) != DATA_BIND_OK)
    return 11;
  if (!next_kind(lease.reader, CSERDE_ARRAY_BEGIN, &token)) return 12;
  if (!read_row(lease.reader, "8", "bob")) return 13;
  if (!next_kind(lease.reader, CSERDE_ARRAY_END, &token)) return 14;
  if (cserde_reader_next(lease.reader, &token) != CSERDE_DONE) return 15;
  if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return 16;

  lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
  error = (DataBindError)DATA_BIND_ERROR_INIT;
  diagnostic = (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  if (data_bind_format_reader_open_selected(
          data_bind_csv_format_provider(),
          csv, sizeof(csv) - 1u, 2u,
          DATA_BIND_STREAM_SELECT_PATH_ALL, "name == \"bob\"",
          &limits, &diagnostic, &lease, &error) != DATA_BIND_OK)
    return 17;
  if (!next_kind(lease.reader, CSERDE_ARRAY_BEGIN, &token)) return 18;
  if (!read_row(lease.reader, "8", "bob")) return 19;
  if (!read_row(lease.reader, "9", "bob")) return 20;
  if (!next_kind(lease.reader, CSERDE_ARRAY_END, &token)) return 21;
  if (cserde_reader_next(lease.reader, &token) != CSERDE_DONE) return 22;
  if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return 23;

  lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
  error = (DataBindError)DATA_BIND_ERROR_INIT;
  diagnostic = (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  if (data_bind_format_reader_open_selected(
          data_bind_csv_format_provider(),
          csv, sizeof(csv) - 1u, 2u,
          DATA_BIND_STREAM_SELECT_PATH_ALL, "(",
          &limits, &diagnostic, &lease, &error) == DATA_BIND_OK)
    return 24;
  if (lease.reader != NULL) return 25;

  return 0;
}
