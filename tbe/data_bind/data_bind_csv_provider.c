#include "data_bind_csv_provider.h"

#include <csv_parser.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum data_bind_csv_stage {
  DATA_BIND_CSV_ARRAY_BEGIN = 0,
  DATA_BIND_CSV_ROW_BEGIN,
  DATA_BIND_CSV_FIELD_KEY,
  DATA_BIND_CSV_FIELD_VALUE,
  DATA_BIND_CSV_ROW_END,
  DATA_BIND_CSV_ARRAY_END,
  DATA_BIND_CSV_DONE
} data_bind_csv_stage;

typedef struct data_bind_csv_reader {
  cserde_reader reader;
  csv_doc_t *document;
  size_t row;
  size_t column;
  size_t row_count;
  size_t column_count;
  data_bind_csv_stage stage;
} data_bind_csv_reader;

static DataBindStatus csv_provider_error(
    DataBindError *error,
    DataBindStatus status,
    const char *message) {
  if (error != NULL && error->size >= sizeof(error->size)) {
    if (error->size >= offsetof(DataBindError, code) + sizeof(error->code))
      error->code = status;
    if (error->size >= offsetof(DataBindError, line) + sizeof(error->line))
      error->line = -1;
    if (error->size >= offsetof(DataBindError, column) + sizeof(error->column))
      error->column = -1;
    if (error->size >= offsetof(DataBindError, path) + sizeof(error->path))
      error->path[0] = '\0';
    if (error->size >= offsetof(DataBindError, message) + sizeof(error->message))
      snprintf(error->message, sizeof(error->message), "%s",
               message != NULL ? message : "");
  }
  return status;
}

static void csv_emit_string(cserde_token *out, vstr view) {
  memset(out, 0, sizeof(*out));
  out->kind = CSERDE_STRING;
  out->value.slice.data = (const unsigned char *)view.data;
  out->value.slice.size = view.length;
  out->value.slice.lifetime = CSERDE_VIEW_STABLE;
}

static cserde_status csv_provider_next(void *opaque, cserde_token *out) {
  data_bind_csv_reader *context = (data_bind_csv_reader *)opaque;

  if (context == NULL || out == NULL) return CSERDE_INVALID_ARGUMENT;
  memset(out, 0, sizeof(*out));

  for (;;) {
    switch (context->stage) {
    case DATA_BIND_CSV_ARRAY_BEGIN:
      out->kind = CSERDE_ARRAY_BEGIN;
      context->stage = context->row_count == 0u
                           ? DATA_BIND_CSV_ARRAY_END
                           : DATA_BIND_CSV_ROW_BEGIN;
      return CSERDE_OK;

    case DATA_BIND_CSV_ROW_BEGIN:
      out->kind = CSERDE_MAP_BEGIN;
      context->column = 0u;
      context->stage = context->column_count == 0u
                           ? DATA_BIND_CSV_ROW_END
                           : DATA_BIND_CSV_FIELD_KEY;
      return CSERDE_OK;

    case DATA_BIND_CSV_FIELD_KEY:
      csv_emit_string(out, csv_header_get_v(context->document, context->column));
      context->stage = DATA_BIND_CSV_FIELD_VALUE;
      return CSERDE_OK;

    case DATA_BIND_CSV_FIELD_VALUE:
      csv_emit_string(out, csv_get_v(context->document, context->row,
                                     context->column));
      ++context->column;
      context->stage = context->column < context->column_count
                           ? DATA_BIND_CSV_FIELD_KEY
                           : DATA_BIND_CSV_ROW_END;
      return CSERDE_OK;

    case DATA_BIND_CSV_ROW_END:
      out->kind = CSERDE_MAP_END;
      ++context->row;
      context->stage = context->row < context->row_count
                           ? DATA_BIND_CSV_ROW_BEGIN
                           : DATA_BIND_CSV_ARRAY_END;
      return CSERDE_OK;

    case DATA_BIND_CSV_ARRAY_END:
      out->kind = CSERDE_ARRAY_END;
      context->stage = DATA_BIND_CSV_DONE;
      return CSERDE_OK;

    case DATA_BIND_CSV_DONE:
      return CSERDE_DONE;
    }
  }
}

static const cserde_reader_ops CSV_READER_OPS = {
    sizeof(cserde_reader_ops),
    CSERDE_READER_OPS_ABI_VERSION,
    csv_provider_next};

static DataBindStatus csv_provider_open(
    const char *data,
    size_t len,
    size_t max_depth,
    cserde_reader **out_reader,
    void **out_owner,
    DataBindError *error) {
  csv_options_t options = CSV_OPTIONS_DEFAULT;
  data_bind_csv_reader *context;
  const char *diagnostic;

  if (out_reader == NULL || out_owner == NULL)
    return csv_provider_error(error, DATA_BIND_ERR_INVALID_ARG,
                              "Invalid CSV provider output");
  *out_reader = NULL;
  *out_owner = NULL;

  /* The canonical representation is ARRAY<MAP<STRING, STRING>>. */
  if (max_depth < 2u)
    return csv_provider_error(error, DATA_BIND_ERR_LIMIT,
                              "CSV provider requires max_depth >= 2");

  context = (data_bind_csv_reader *)calloc(1u, sizeof(*context));
  if (context == NULL)
    return csv_provider_error(error, DATA_BIND_ERR_OOM,
                              "Unable to allocate CSV provider state");

  options.has_header = true;
  context->document = csv_parse_opts(data, len, &options);
  if (context->document == NULL) {
    diagnostic = csv_get_error();
    csv_provider_error(
        error, DATA_BIND_ERR_PARSE,
        diagnostic != NULL && diagnostic[0] != '\0'
            ? diagnostic
            : "CSV parse failed");
    free(context);
    return DATA_BIND_ERR_PARSE;
  }

  context->row_count = csv_row_count(context->document);
  context->column_count = csv_column_count(context->document);
  context->stage = DATA_BIND_CSV_ARRAY_BEGIN;

  if (cserde_reader_init(&context->reader, &CSV_READER_OPS, context) !=
      CSERDE_OK) {
    csv_free(context->document);
    free(context);
    return csv_provider_error(error, DATA_BIND_ERR_RUNTIME,
                              "Unable to initialize CSV CSerde reader");
  }

  *out_reader = &context->reader;
  *out_owner = context;
  return DATA_BIND_OK;
}

static void csv_provider_close(cserde_reader *reader, void *opaque) {
  data_bind_csv_reader *context = (data_bind_csv_reader *)opaque;
  (void)reader;
  if (context != NULL) {
    csv_free(context->document);
    free(context);
  }
}

static const DataBindFormatProvider CSV_PROVIDER =
    DATA_BIND_FORMAT_PROVIDER_INIT(
        DATA_BIND_FORMAT_CSV,
        csv_provider_open,
        csv_provider_close);

const DataBindFormatProvider *data_bind_csv_format_provider(void) {
  return &CSV_PROVIDER;
}
