#include "data_bind_xml_provider.h"

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
  static const char xml[] =
      "<root id=\"7\" name=\"attribute\">"
      "<name>alice</name><tag>x</tag><tag>y</tag>"
      "</root>";
  DataBindFormatReader lease = DATA_BIND_FORMAT_READER_INIT;
  DataBindError error = DATA_BIND_ERROR_INIT;
  cserde_token token = {0};

  if (data_bind_format_reader_open(
          data_bind_xml_format_provider(),
          xml, sizeof(xml) - 1u, 0u, &lease, &error) != DATA_BIND_OK)
    return 1;
  if (cserde_reader_next(lease.reader, &token) != CSERDE_LIMIT_EXCEEDED)
    return 2;
  if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return 3;

  lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
  error = (DataBindError)DATA_BIND_ERROR_INIT;
  if (data_bind_format_reader_open(
          data_bind_xml_format_provider(),
          xml, sizeof(xml) - 1u, 8u, &lease, &error) != DATA_BIND_OK)
    return 4;

  if (!next_kind(lease.reader, CSERDE_MAP_BEGIN, &token)) return 5;

  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "name")) return 6;
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "alice")) return 7;

  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "tag")) return 8;
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "x")) return 9;

  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "tag")) return 10;
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "y")) return 11;

  /* The child element named "name" wins over the same-named attribute. */
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "id")) return 12;
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "7")) return 13;

  if (!next_kind(lease.reader, CSERDE_MAP_END, &token)) return 14;
  if (cserde_reader_next(lease.reader, &token) != CSERDE_DONE) return 15;
  if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return 16;
  return 0;
}
