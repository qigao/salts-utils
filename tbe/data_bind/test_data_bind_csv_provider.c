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

int main(void) {
  static const char csv[] = "id,name\n7,alice\n8,bob\n";
  DataBindFormatReader lease = DATA_BIND_FORMAT_READER_INIT;
  DataBindError error = DATA_BIND_ERROR_INIT;
  cserde_token token = {0};

  if (data_bind_format_reader_open(
          data_bind_csv_format_provider(),
          csv, sizeof(csv) - 1u, 1u, &lease, &error) != DATA_BIND_ERR_LIMIT)
    return 1;
  if (lease.reader != NULL || error.code != DATA_BIND_ERR_LIMIT) return 2;

  error = (DataBindError)DATA_BIND_ERROR_INIT;
  if (data_bind_format_reader_open(
          data_bind_csv_format_provider(),
          csv, sizeof(csv) - 1u, 2u, &lease, &error) != DATA_BIND_OK)
    return 3;

  if (!next_kind(lease.reader, CSERDE_ARRAY_BEGIN, &token)) return 4;
  if (!next_kind(lease.reader, CSERDE_MAP_BEGIN, &token)) return 5;

  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "id")) return 6;
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "7")) return 7;
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "name")) return 8;
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "alice")) return 9;
  if (!next_kind(lease.reader, CSERDE_MAP_END, &token)) return 10;

  if (!next_kind(lease.reader, CSERDE_MAP_BEGIN, &token)) return 11;
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "id")) return 12;
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "8")) return 13;
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "name")) return 14;
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "bob")) return 15;
  if (!next_kind(lease.reader, CSERDE_MAP_END, &token)) return 16;

  if (!next_kind(lease.reader, CSERDE_ARRAY_END, &token)) return 17;
  if (cserde_reader_next(lease.reader, &token) != CSERDE_DONE) return 18;
  if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return 19;
  return 0;
}
