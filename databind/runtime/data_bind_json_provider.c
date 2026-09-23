#include "data_bind_json_provider.h"

#include <json_cserde_reader.h>
#include <json_parser.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static DataBindStatus json_provider_error(
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

static DataBindQueryStatus json_query_status(qvm_status_t status) {
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

static qvm_limits_t json_query_limits(
    const DataBindQueryLimits *limits) {
  if (limits == NULL) return qvm_default_limits();
  return (qvm_limits_t){
      limits->max_instructions,
      limits->max_operands,
      limits->max_regexes,
      limits->max_steps};
}

static void json_query_diagnostic(
    DataBindQueryDiagnostic *out,
    const qvm_diagnostic_t *native) {
  size_t size;
  if (out == NULL || out->size < sizeof(*out) || native == NULL) return;
  size = out->size;
  *out = (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  out->size = size;
  out->status = json_query_status(native->status);
  out->instruction = native->instruction;
  out->opcode = native->opcode;
  out->operand = native->operand;
  snprintf(out->message, sizeof(out->message), "%s",
           native->message != NULL ? native->message : "");
}

static DataBindStatus json_query_failure(
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

static DataBindStatus json_reader_from_root(
    json_value_t *root,
    size_t max_depth,
    cserde_reader **out_reader,
    void **out_owner,
    DataBindError *error) {
  cserde_reader *reader = json_cserde_reader_create(root, max_depth);
  if (reader == NULL) {
    json_free(root);
    return json_provider_error(
        error, DATA_BIND_ERR_OOM,
        "Unable to create bounded JSON CSerde reader");
  }
  *out_reader = reader;
  *out_owner = root;
  return DATA_BIND_OK;
}

static DataBindStatus json_provider_open(
    const char *data,
    size_t len,
    size_t max_depth,
    cserde_reader **out_reader,
    void **out_owner,
    DataBindError *error) {
  json_value_t *root;
  const char *diagnostic;

  if (out_reader == NULL || out_owner == NULL)
    return json_provider_error(error, DATA_BIND_ERR_INVALID_ARG,
                               "Invalid JSON provider output");
  *out_reader = NULL;
  *out_owner = NULL;

  root = json_parse(data, len);
  if (root == NULL) {
    diagnostic = json_get_error();
    return json_provider_error(
        error, DATA_BIND_ERR_PARSE,
        diagnostic != NULL && diagnostic[0] != '\0'
            ? diagnostic
            : "JSON parse failed");
  }

  return json_reader_from_root(
      root, max_depth, out_reader, out_owner, error);
}

static DataBindStatus json_provider_open_selected(
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
  json_value_t *root = NULL;
  json_value_t *selected = NULL;
  json_path_program_t *program = NULL;
  json_path_result_t *matches = NULL;
  qvm_limits_t native_limits = json_query_limits(query_limits);
  qvm_diagnostic_t native_diagnostic = {0};
  const char *parser_error;

  if (out_reader == NULL || out_owner == NULL)
    return json_provider_error(error, DATA_BIND_ERR_INVALID_ARG,
                               "Invalid selected JSON provider output");
  *out_reader = NULL;
  *out_owner = NULL;

  if (query_diagnostic != NULL) {
    size_t size = query_diagnostic->size;
    *query_diagnostic =
        (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
    query_diagnostic->size = size;
  }

  root = json_parse(data, len);
  if (root == NULL) {
    parser_error = json_get_error();
    return json_provider_error(
        error, DATA_BIND_ERR_PARSE,
        parser_error != NULL && parser_error[0] != '\0'
            ? parser_error
            : "JSON parse failed");
  }

  if (selection == DATA_BIND_STREAM_SELECT_ROOT)
    return json_reader_from_root(
        root, max_depth, out_reader, out_owner, error);

  if (selection == DATA_BIND_STREAM_SELECT_ALL) {
    if (json_type(root) == JSON_ARRAY)
      return json_reader_from_root(
          root, max_depth, out_reader, out_owner, error);

    selected = json_create_array();
    if (selected == NULL || !json_array_add_checked(selected, root)) {
      json_free(selected);
      json_free(root);
      return json_provider_error(
          error, DATA_BIND_ERR_OOM,
          "Unable to normalize JSON all-selection");
    }
    root = NULL;
    return json_reader_from_root(
        selected, max_depth, out_reader, out_owner, error);
  }

  program = json_path_compile_ex(path, &native_limits, &native_diagnostic);
  json_query_diagnostic(query_diagnostic, &native_diagnostic);
  if (program == NULL) {
    DataBindStatus status;
    parser_error = json_path_get_error();
    if (query_diagnostic != NULL &&
        query_diagnostic->status == DATA_BIND_QUERY_OK) {
      query_diagnostic->status = DATA_BIND_QUERY_INVALID_PROGRAM;
      query_diagnostic->instruction = DATA_BIND_QUERY_NO_INSTRUCTION;
      query_diagnostic->opcode = DATA_BIND_QUERY_NO_OPCODE;
      query_diagnostic->operand = DATA_BIND_QUERY_NO_OPERAND;
      snprintf(query_diagnostic->message, sizeof(query_diagnostic->message),
               "%s", parser_error != NULL && parser_error[0] != '\0'
                         ? parser_error
                         : "Invalid JSONPath");
    }
    status = json_query_failure(query_diagnostic);
    json_free(root);
    return json_provider_error(
        error, status,
        query_diagnostic != NULL && query_diagnostic->message[0] != '\0'
            ? query_diagnostic->message
            : (parser_error != NULL ? parser_error : "Invalid JSONPath"));
  }

  if (selection == DATA_BIND_STREAM_SELECT_PATH_FIRST) {
    selected = json_path_get_compiled_ex(
        root, program, &native_diagnostic);
    json_query_diagnostic(query_diagnostic, &native_diagnostic);
    json_path_program_free(program);
    if (selected == NULL) {
      DataBindStatus status =
          query_diagnostic != NULL &&
                  query_diagnostic->status != DATA_BIND_QUERY_OK
              ? json_query_failure(query_diagnostic)
              : DATA_BIND_ERR_TYPE_MISMATCH;
      parser_error = json_path_get_error();
      json_free(root);
      return json_provider_error(
          error, status,
          query_diagnostic != NULL && query_diagnostic->message[0] != '\0'
              ? query_diagnostic->message
              : (parser_error != NULL
                     ? parser_error
                     : "JSONPath selected no value"));
    }

    {
      cserde_reader *reader =
          json_cserde_reader_create(selected, max_depth);
      if (reader == NULL) {
        json_free(root);
        return json_provider_error(
            error, DATA_BIND_ERR_OOM,
            "Unable to create selected JSON CSerde reader");
      }
      *out_reader = reader;
      *out_owner = root;
      return DATA_BIND_OK;
    }
  }

  matches = json_path_query_compiled_ex(
      root, program, &native_diagnostic);
  json_query_diagnostic(query_diagnostic, &native_diagnostic);
  json_path_program_free(program);
  if (matches == NULL) {
    DataBindStatus status =
        query_diagnostic != NULL &&
                query_diagnostic->status != DATA_BIND_QUERY_OK
            ? json_query_failure(query_diagnostic)
            : DATA_BIND_ERR_PARSE;
    parser_error = json_path_get_error();
    json_free(root);
    return json_provider_error(
        error, status,
        query_diagnostic != NULL && query_diagnostic->message[0] != '\0'
            ? query_diagnostic->message
            : (parser_error != NULL ? parser_error : "JSONPath query failed"));
  }

  selected = json_create_array();
  if (selected != NULL) {
    size_t index;
    for (index = 0u; index < json_path_result_size(matches); ++index) {
      json_value_t *copy = json_clone(json_path_result_get(matches, index));
      if (copy == NULL || !json_array_add_checked(selected, copy)) {
        json_free(copy);
        json_free(selected);
        selected = NULL;
        break;
      }
    }
  }
  json_path_result_free(matches);
  json_free(root);

  if (selected == NULL)
    return json_provider_error(
        error, DATA_BIND_ERR_OOM,
        "Unable to materialize JSONPath selection");

  return json_reader_from_root(
      selected, max_depth, out_reader, out_owner, error);
}

static void json_provider_close(cserde_reader *reader, void *owner) {
  json_cserde_reader_destroy(reader);
  json_free((json_value_t *)owner);
}

static const DataBindFormatProvider JSON_PROVIDER =
    DATA_BIND_FORMAT_PROVIDER_WITH_SELECTION_INIT(
        DATA_BIND_FORMAT_JSON,
        json_provider_open,
        json_provider_close,
        json_provider_open_selected);

const DataBindFormatProvider *data_bind_json_format_provider(void) {
  return &JSON_PROVIDER;
}
