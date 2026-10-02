#include "data_bind_format_provider.h"

#include <cserde/reader.h>
#include <cserde/writer.h>

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

typedef struct test_writer_context {
  cserde_writer writer;
  size_t token_count;
} test_writer_context;

static int WRITER_OPEN_CALLS;
static int WRITER_CLOSE_CALLS;

static cserde_status test_writer_write_token(
    void *opaque, const cserde_token *token) {
  test_writer_context *context = (test_writer_context *)opaque;
  if (context == NULL || token == NULL) return CSERDE_UNSUPPORTED;
  ++context->token_count;
  return CSERDE_OK;
}

static cserde_status test_writer_finish(void *opaque) {
  return opaque != NULL ? CSERDE_OK : CSERDE_SINK_ERROR;
}

static const cserde_writer_ops TEST_WRITER_OPS = {
    sizeof(cserde_writer_ops), CSERDE_WRITER_OPS_ABI_VERSION,
    test_writer_write_token, test_writer_finish};

static DataBindStatus test_writer_open(
    DataBindWriteFn write, void *write_user, size_t max_depth,
    cserde_writer **out_writer, void **out_owner, DataBindError *error) {
  test_writer_context *context;
  (void)write_user;
  (void)max_depth;
  (void)error;
  if (write == NULL || out_writer == NULL || out_owner == NULL)
    return DATA_BIND_ERR_INVALID_ARG;
  ++WRITER_OPEN_CALLS;
  context = (test_writer_context *)calloc(1u, sizeof(*context));
  if (context == NULL) return DATA_BIND_ERR_OOM;
  if (cserde_writer_init(&context->writer, &TEST_WRITER_OPS, context) !=
      CSERDE_OK) {
    free(context);
    return DATA_BIND_ERR_RUNTIME;
  }
  *out_writer = &context->writer;
  *out_owner = context;
  return DATA_BIND_OK;
}

static DataBindStatus test_writer_close(
    cserde_writer *writer, void *owner, DataBindError *error) {
  cserde_status status;
  (void)error;
  ++WRITER_CLOSE_CALLS;
  status = cserde_writer_finish(writer);
  free(owner);
  return status == CSERDE_OK ? DATA_BIND_OK : DATA_BIND_ERR_RUNTIME;
}

static int test_byte_sink(const void *data, size_t len, void *user) {
  (void)data;
  (void)len;
  (void)user;
  return 0;
}

static const DataBindFormatProvider TEST_WRITER_PROVIDER =
    DATA_BIND_FORMAT_PROVIDER_WITH_SELECTION_AND_WRITER_INIT(
        DATA_BIND_FORMAT_JSON, test_open, test_close, NULL,
        test_writer_open, test_writer_close);

int main(void) {
  DataBindFormatReader lease = DATA_BIND_FORMAT_READER_INIT;
  DataBindError error = DATA_BIND_ERROR_INIT;
  cserde_token token = {0};
  DataBindFormatProvider invalid = TEST_PROVIDER;
  const char payload[] = "ok";
  DataBindFormatWriter writer_lease = DATA_BIND_FORMAT_WRITER_INIT;

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

  lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
  error = (DataBindError)DATA_BIND_ERROR_INIT;
  if (data_bind_format_reader_open_selected(
          &TEST_PROVIDER, payload, sizeof(payload) - 1u, 4u,
          DATA_BIND_STREAM_SELECT_ROOT, NULL, NULL, NULL,
          &lease, &error) != DATA_BIND_ERR_INVALID_ARG)
    return 11;
  if (OPEN_CALLS != 1 || lease.reader != NULL) return 12;

  error = (DataBindError)DATA_BIND_ERROR_INIT;
  if (data_bind_format_writer_open(
          &TEST_PROVIDER, test_byte_sink, NULL, 4u,
          &writer_lease, &error) != DATA_BIND_ERR_INVALID_ARG)
    return 13;
  if (WRITER_OPEN_CALLS != 0 || writer_lease.writer != NULL) return 14;

  error = (DataBindError)DATA_BIND_ERROR_INIT;
  if (data_bind_format_writer_open(
          &TEST_WRITER_PROVIDER, test_byte_sink, NULL, 4u,
          &writer_lease, &error) != DATA_BIND_OK)
    return 15;
  if (WRITER_OPEN_CALLS != 1 || writer_lease.writer == NULL ||
      writer_lease.provider != &TEST_WRITER_PROVIDER)
    return 16;
  token = (cserde_token){.kind = CSERDE_UINT, .value.uint = 7u};
  if (cserde_writer_write(writer_lease.writer, &token) != CSERDE_OK)
    return 17;
  if (data_bind_format_writer_close(&writer_lease, &error) != DATA_BIND_OK)
    return 18;
  if (WRITER_CLOSE_CALLS != 1 || writer_lease.writer != NULL ||
      writer_lease.provider != NULL)
    return 19;
  if (data_bind_format_writer_close(&writer_lease, &error) != DATA_BIND_OK)
    return 20;
  if (WRITER_CLOSE_CALLS != 1) return 21;
  return 0;
}
