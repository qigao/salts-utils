#include "data_bind_format_provider.h"

#include <cserde/reader.h>

#include <stdlib.h>
#include <string.h>

typedef struct test_reader_context {
  cserde_reader reader;
  const char *data;
  size_t len;
  int emitted;
} test_reader_context;

static cserde_status test_next(void *opaque, cserde_token *out) {
  test_reader_context *context = (test_reader_context *)opaque;
  if (context->emitted) return CSERDE_DONE;
  context->emitted = 1;
  memset(out, 0, sizeof(*out));
  out->kind = CSERDE_STRING;
  out->value.slice.data = (const unsigned char *)context->data;
  out->value.slice.size = context->len;
  out->value.slice.lifetime = CSERDE_VIEW_STABLE;
  return CSERDE_OK;
}

static const cserde_reader_ops TEST_READER_OPS = {
    sizeof(cserde_reader_ops), CSERDE_READER_OPS_ABI_VERSION, test_next};

static int OPEN_CALLS;
static int CLOSE_CALLS;

static DataBindStatus test_open(
    const char *data,
    size_t len,
    size_t max_depth,
    cserde_reader **out_reader,
    void **out_owner,
    DataBindError *error) {
  test_reader_context *context;
  (void)max_depth;
  (void)error;
  ++OPEN_CALLS;
  context = (test_reader_context *)calloc(1u, sizeof(*context));
  if (context == NULL) return DATA_BIND_ERR_OOM;
  context->data = data;
  context->len = len;
  if (cserde_reader_init(&context->reader, &TEST_READER_OPS, context) != CSERDE_OK) {
    free(context);
    return DATA_BIND_ERR_RUNTIME;
  }
  *out_reader = &context->reader;
  *out_owner = context;
  return DATA_BIND_OK;
}

static void test_close(cserde_reader *reader, void *owner) {
  (void)reader;
  ++CLOSE_CALLS;
  free(owner);
}

static const DataBindFormatProvider TEST_PROVIDER =
    DATA_BIND_FORMAT_PROVIDER_INIT(
        DATA_BIND_FORMAT_JSON, test_open, test_close);

int main(void) {
  DataBindFormatReader lease = DATA_BIND_FORMAT_READER_INIT;
  DataBindError error = DATA_BIND_ERROR_INIT;
  cserde_token token = {0};
  DataBindFormatProvider invalid = TEST_PROVIDER;
  const char payload[] = "ok";

  invalid.abi_version = DATA_BIND_FORMAT_PROVIDER_ABI_VERSION + 1u;
  if (data_bind_format_reader_open(
          &invalid, payload, sizeof(payload) - 1u, 4u, &lease, &error) !=
      DATA_BIND_ERR_INVALID_ARG)
    return 1;
  if (OPEN_CALLS != 0 || lease.reader != NULL) return 2;

  if (data_bind_format_reader_open(
          &TEST_PROVIDER, payload, sizeof(payload) - 1u, 4u, &lease, &error) !=
      DATA_BIND_OK)
    return 3;
  if (OPEN_CALLS != 1 || lease.reader == NULL ||
      lease.provider != &TEST_PROVIDER)
    return 4;
  if (cserde_reader_next(lease.reader, &token) != CSERDE_OK ||
      token.kind != CSERDE_STRING ||
      token.value.slice.size != sizeof(payload) - 1u ||
      memcmp(token.value.slice.data, payload, sizeof(payload) - 1u) != 0)
    return 5;
  if (cserde_reader_next(lease.reader, &token) != CSERDE_DONE) return 6;

  if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return 7;
  if (CLOSE_CALLS != 1 || lease.reader != NULL || lease.provider != NULL)
    return 8;
  if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return 9;
  if (CLOSE_CALLS != 1) return 10;
  return 0;
}
