#include "data_bind_csv_provider.h"

#include <csv_parser.h>
#include <dsv_filter.h>

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
  size_t *rows;
  size_t row_position;
  size_t row_count;
  size_t column;
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

static DataBindQueryStatus csv_query_status(qvm_status_t status) {
  switch (status) {
  case QVM_STATUS_OK:
    return DATA_BIND_QUERY_OK;
  case QVM_STATUS_INVALID_ARGUMENT:
    return DATA_BIND_QUERY_INVALID_ARGUMENT;
  case QVM_STATUS_INVALID_PROGRAM:
    return DATA_BIND_QUERY_INVALID_PROGRAM;
  case QVM_STATUS_UNSUPPORTED:
    return DATA_BIND_QUERY_UNSUPPORTED;
  case QVM_STATUS_BACKEND_ERROR:
    return DATA_BIND_QUERY_BACKEND_ERROR;
  case QVM_STATUS_NO_MEMORY:
    return DATA_BIND_QUERY_NO_MEMORY;
  case QVM_STATUS_RESOURCE_LIMIT:
    return DATA_BIND_QUERY_RESOURCE_LIMIT;
  case QVM_STATUS_BUFFER_TOO_SMALL:
    return DATA_BIND_QUERY_BUFFER_TOO_SMALL;
  default:
    return DATA_BIND_QUERY_BACKEND_ERROR;
  }
}

static qvm_limits_t csv_query_limits(const DataBindQueryLimits *limits) {
  if (limits == NULL) return qvm_default_limits();
  return (qvm_limits_t){
      limits->max_instructions, limits->max_operands,
      limits->max_regexes, limits->max_steps};
}

static void csv_query_diagnostic(
    DataBindQueryDiagnostic *out,
    const qvm_diagnostic_t *native) {
  size_t size;
  if (out == NULL || out->size < sizeof(*out) || native == NULL) return;
  size = out->size;
  *out = (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  out->size = size;
  out->status = csv_query_status(native->status);
  out->instruction = native->instruction;
  out->opcode = native->opcode;
  out->operand = native->operand;
  snprintf(out->message, sizeof(out->message), "%s",
           native->message != NULL ? native->message : "");
}

static DataBindStatus csv_query_failure(
    const DataBindQueryDiagnostic *diagnostic) {
  if (diagnostic == NULL) return DATA_BIND_ERR_PARSE;
  switch (diagnostic->status) {
  case DATA_BIND_QUERY_RESOURCE_LIMIT:
    return DATA_BIND_ERR_LIMIT;
  case DATA_BIND_QUERY_NO_MEMORY:
    return DATA_BIND_ERR_OOM;
  case DATA_BIND_QUERY_INVALID_ARGUMENT:
    return DATA_BIND_ERR_INVALID_ARG;
  default:
    return DATA_BIND_ERR_PARSE;
  }
}

static void csv_emit_string(cserde_token *out, vstr view) {
  memset(out, 0, sizeof(*out));
  out->kind = CSERDE_STRING;
  out->value.slice.data = (const unsigned char *)view.data;
  out->value.slice.size = view.len;
  out->value.slice.lifetime = CSERDE_VIEW_STABLE;
}

static size_t csv_current_row(const data_bind_csv_reader *context) {
  return context->rows != NULL
      ? context->rows[context->row_position]
      : context->row_position;
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
      csv_emit_string(
          out, csv_get_v(context->document, csv_current_row(context),
                         context->column));
      ++context->column;
      context->stage = context->column < context->column_count
                           ? DATA_BIND_CSV_FIELD_KEY
                           : DATA_BIND_CSV_ROW_END;
      return CSERDE_OK;

    case DATA_BIND_CSV_ROW_END:
      out->kind = CSERDE_MAP_END;
      ++context->row_position;
      context->stage = context->row_position < context->row_count
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

static DataBindStatus csv_reader_create(
    csv_doc_t *document,
    size_t *rows,
    size_t row_count,
    cserde_reader **out_reader,
    void **out_owner,
    DataBindError *error) {
  data_bind_csv_reader *context;

  context = (data_bind_csv_reader *)calloc(1u, sizeof(*context));
  if (context == NULL) {
    free(rows);
    csv_free(document);
    return csv_provider_error(error, DATA_BIND_ERR_OOM,
                              "Unable to allocate CSV provider state");
  }

  context->document = document;
  context->rows = rows;
  context->row_count = row_count;
  context->column_count = csv_column_count(document);
  context->stage = DATA_BIND_CSV_ARRAY_BEGIN;

  if (cserde_reader_init(&context->reader, &CSV_READER_OPS, context) !=
      CSERDE_OK) {
    free(context->rows);
    csv_free(context->document);
    free(context);
    return csv_provider_error(error, DATA_BIND_ERR_RUNTIME,
                              "Unable to initialize CSV CSerde reader");
  }

  *out_reader = &context->reader;
  *out_owner = context;
  return DATA_BIND_OK;
}

static csv_doc_t *csv_parse_binding_document(
    const char *data,
    size_t len,
    DataBindError *error) {
  csv_options_t options = CSV_OPTIONS_DEFAULT;
  csv_doc_t *document;
  const char *diagnostic;

  options.has_header = true;
  document = csv_parse_opts(data, len, &options);
  if (document == NULL) {
    diagnostic = csv_get_error();
    csv_provider_error(
        error, DATA_BIND_ERR_PARSE,
        diagnostic != NULL && diagnostic[0] != '\0'
            ? diagnostic
            : "CSV parse failed");
  }
  return document;
}

static DataBindStatus csv_provider_open(
    const char *data,
    size_t len,
    size_t max_depth,
    cserde_reader **out_reader,
    void **out_owner,
    DataBindError *error) {
  csv_doc_t *document;

  if (out_reader == NULL || out_owner == NULL)
    return csv_provider_error(error, DATA_BIND_ERR_INVALID_ARG,
                              "Invalid CSV provider output");
  *out_reader = NULL;
  *out_owner = NULL;

  /* The canonical representation is ARRAY<MAP<STRING, STRING>>. */
  if (max_depth < 2u)
    return csv_provider_error(error, DATA_BIND_ERR_LIMIT,
                              "CSV provider requires max_depth >= 2");

  document = csv_parse_binding_document(data, len, error);
  if (document == NULL) return DATA_BIND_ERR_PARSE;

  return csv_reader_create(
      document, NULL, csv_row_count(document),
      out_reader, out_owner, error);
}

static DataBindStatus csv_provider_open_selected(
    const char *data,
    size_t len,
    size_t max_depth,
    DataBindStreamSelection selection,
    const char *path,
    const DataBindQueryLimits *query_limits,
    DataBindQueryDiagnostic *query_diagnostic,
    cserde_reader **out_reader,
    void **out_owner,
    DataBindError *error) {
  csv_doc_t *document = NULL;
  csv_doc_t *filter_document = NULL;
  dsv_filter_t *filter = NULL;
  csv_options_t filter_options = CSV_OPTIONS_DEFAULT;
  qvm_limits_t native_limits = csv_query_limits(query_limits);
  qvm_diagnostic_t native_diagnostic = {0};
  size_t *rows = NULL;
  size_t selected_count = 0u;
  size_t raw_row;
  size_t data_rows;

  if (out_reader == NULL || out_owner == NULL)
    return csv_provider_error(error, DATA_BIND_ERR_INVALID_ARG,
                              "Invalid selected CSV provider output");
  *out_reader = NULL;
  *out_owner = NULL;

  if (max_depth < 2u)
    return csv_provider_error(error, DATA_BIND_ERR_LIMIT,
                              "CSV provider requires max_depth >= 2");

  if (selection == DATA_BIND_STREAM_SELECT_ROOT ||
      selection == DATA_BIND_STREAM_SELECT_ALL)
    return csv_provider_open(
        data, len, max_depth, out_reader, out_owner, error);

  if (query_diagnostic != NULL) {
    size_t size = query_diagnostic->size;
    *query_diagnostic =
        (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
    query_diagnostic->size = size;
  }

  document = csv_parse_binding_document(data, len, error);
  if (document == NULL) return DATA_BIND_ERR_PARSE;
  data_rows = csv_row_count(document);

  filter_options.has_header = false;
  filter_document = csv_parse_opts(data, len, &filter_options);
  if (filter_document == NULL) {
    csv_free(document);
    return csv_provider_error(error, DATA_BIND_ERR_PARSE,
                              "CSV filter parse failed");
  }

  filter = dsv_filter_create(filter_document, 0u);
  if (filter == NULL ||
      !dsv_filter_compile_ex(
          filter, path, &native_limits, &native_diagnostic)) {
    DataBindStatus status;
    const char *filter_error = dsv_filter_error(filter);
    csv_query_diagnostic(query_diagnostic, &native_diagnostic);
    status = csv_query_failure(query_diagnostic);
    if (filter != NULL) dsv_filter_destroy(filter);
    csv_free(filter_document);
    csv_free(document);
    return csv_provider_error(
        error, status,
        query_diagnostic != NULL && query_diagnostic->message[0] != '\0'
            ? query_diagnostic->message
            : (filter_error != NULL && filter_error[0] != '\0'
                   ? filter_error
                   : "CSV filter compile failed"));
  }
  csv_query_diagnostic(query_diagnostic, &native_diagnostic);

  if (data_rows != 0u) {
    if (data_rows > SIZE_MAX / sizeof(*rows)) {
      dsv_filter_destroy(filter);
      csv_free(filter_document);
      csv_free(document);
      return csv_provider_error(error, DATA_BIND_ERR_LIMIT,
                                "CSV selected-row count is too large");
    }
    rows = (size_t *)malloc(data_rows * sizeof(*rows));
    if (rows == NULL) {
      dsv_filter_destroy(filter);
      csv_free(filter_document);
      csv_free(document);
      return csv_provider_error(error, DATA_BIND_ERR_OOM,
                                "Unable to allocate CSV selection state");
    }
  }

  for (raw_row = 1u;
       raw_row < csv_row_count(filter_document) &&
       selected_count < data_rows;
       ++raw_row) {
    int matched = dsv_filter_check_row(filter, raw_row);
    if (matched < 0) {
      const qvm_diagnostic_t *native =
          dsv_filter_qvm_diagnostic(filter);
      DataBindStatus status;
      csv_query_diagnostic(query_diagnostic, native);
      status = csv_query_failure(query_diagnostic);
      free(rows);
      dsv_filter_destroy(filter);
      csv_free(filter_document);
      csv_free(document);
      return csv_provider_error(
          error, status, "CSV filter evaluation failed");
    }
    if (matched) {
      rows[selected_count++] = raw_row - 1u;
      if (selection == DATA_BIND_STREAM_SELECT_PATH_FIRST) break;
    }
  }

  dsv_filter_destroy(filter);
  csv_free(filter_document);

  return csv_reader_create(
      document, rows, selected_count,
      out_reader, out_owner, error);
}

static void csv_provider_close(cserde_reader *reader, void *opaque) {
  data_bind_csv_reader *context = (data_bind_csv_reader *)opaque;
  (void)reader;
  if (context != NULL) {
    free(context->rows);
    csv_free(context->document);
    free(context);
  }
}

static const DataBindFormatProvider CSV_PROVIDER =
    DATA_BIND_FORMAT_PROVIDER_WITH_SELECTION_INIT(
        DATA_BIND_FORMAT_CSV,
        csv_provider_open,
        csv_provider_close,
        csv_provider_open_selected);

const DataBindFormatProvider *data_bind_csv_format_provider(void) {
  return &CSV_PROVIDER;
}
