#include "data_bind_json_provider.h"

#include <cserde/reader.h>

#include <string.h>

static int next_kind(cserde_reader *reader, cserde_token_kind kind,
                     cserde_token *token) {
  memset(token, 0, sizeof(*token));
  return cserde_reader_next(reader, token) == CSERDE_OK &&
         token->kind == kind;
}

int main(void) {
  static const char json[] = "{\"id\":7}";
  DataBindFormatReader lease = DATA_BIND_FORMAT_READER_INIT;
  DataBindError error = DATA_BIND_ERROR_INIT;
  cserde_token token = {0};

  if (data_bind_format_reader_open(
          data_bind_json_format_provider(),
          json, sizeof(json) - 1u, 8u, &lease, &error) != DATA_BIND_OK)
    return 1;
  if (!next_kind(lease.reader, CSERDE_MAP_BEGIN, &token)) return 2;
  if (!next_kind(lease.reader, CSERDE_STRING, &token)) return 3;
  if (token.value.slice.size != 2u ||
      memcmp(token.value.slice.data, "id", 2u) != 0)
    return 4;
  if (!next_kind(lease.reader, CSERDE_UINT, &token) ||
      token.value.uint != 7u)
    return 5;
  if (!next_kind(lease.reader, CSERDE_MAP_END, &token)) return 6;
  if (cserde_reader_next(lease.reader, &token) != CSERDE_DONE) return 7;
  if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return 8;

  error = (DataBindError)DATA_BIND_ERROR_INIT;
  if (data_bind_format_reader_open(
          data_bind_json_format_provider(),
          "{", 1u, 8u, &lease, &error) != DATA_BIND_ERR_PARSE)
    return 9;
  if (lease.reader != NULL || error.code != DATA_BIND_ERR_PARSE) return 10;
  return 0;
}
