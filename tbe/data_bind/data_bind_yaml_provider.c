#include "data_bind_yaml_provider.h"

#include <cyaml.h>
#include <cyaml_json_adapter.h>
#include <json_cserde_reader.h>
#include <json_parser.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct data_bind_yaml_owner {
  cyaml_doc_t *document;
  json_value_t *json;
} data_bind_yaml_owner;

static DataBindStatus yaml_provider_error(
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

static DataBindQueryStatus yaml_query_status(qvm_status_t status) {
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

static qvm_limits_t yaml_query_limits(
    const DataBindQueryLimits *limits) {
  if (limits == NULL) return qvm_default_limits();
  return (qvm_limits_t){
      limits->max_instructions,
      limits->max_operands,
      limits->max_regexes,
      limits->max_steps};
}

static void yaml_query_diagnostic(
    DataBindQueryDiagnostic *out,
    const qvm_diagnostic_t *native) {
  size_t size;
  if (out == NULL || out->size < sizeof(*out) || native == NULL) return;
  size = out->size;
  *out = (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  out->size = size;
  out->status = yaml_query_status(native->status);
  out->instruction = native->instruction;
  out->opcode = native->opcode;
  out->operand = native->operand;
  snprintf(out->message, sizeof(out->message), "%s",
           native->message != NULL ? native->message : "");
}

static DataBindStatus yaml_query_failure(
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

static cyaml_doc_t *yaml_parse_document(
    const char *data,
    size_t len,
    size_t max_depth,
    DataBindError *error) {
  cyaml_opts_t options = CYAML_OPTS_DEFAULT;
  cyaml_error_t diagnostic = {0};
  cyaml_doc_t *document;

  if (max_depth > UINT32_MAX)
    options.max_depth = UINT32_MAX;
  else if (max_depth == 0u)
    options.max_depth = 1u;
  else
    options.max_depth = (uint32_t)max_depth;

  document = cyaml_parse(data, len, &options, &diagnostic);
  if (document == NULL) {
    DataBindStatus status =
        diagnostic.code == CYAML_ERR_NOMEM ? DATA_BIND_ERR_OOM
                                           : DATA_BIND_ERR_PARSE;
    yaml_provider_error(
        error, status,
        diagnostic.msg[0] != '\0' ? diagnostic.msg : "YAML parse failed");
  }
  return document;
}

static DataBindStatus yaml_reader_from_json(
    cyaml_doc_t *document,
    json_value_t *json,
    size_t max_depth,
    cserde_reader **out_reader,
    void **out_owner,
    DataBindError *error) {
  data_bind_yaml_owner *owner;
  cserde_reader *reader;

  if (json == NULL) {
    cyaml_free(document);
    return yaml_provider_error(
        error, DATA_BIND_ERR_PARSE,
        "YAML value cannot be represented by the canonical CSerde token model");
  }

  owner = (data_bind_yaml_owner *)calloc(1u, sizeof(*owner));
  if (owner == NULL) {
    json_free(json);
    cyaml_free(document);
    return yaml_provider_error(error, DATA_BIND_ERR_OOM,
                               "Unable to allocate YAML provider state");
  }
  owner->document = document;
  owner->json = json;

  reader = json_cserde_reader_create(owner->json, max_depth);
  if (reader == NULL) {
    json_free(owner->json);
    cyaml_free(owner->document);
    free(owner);
    return yaml_provider_error(
        error, DATA_BIND_ERR_OOM,
        "Unable to create bounded YAML CSerde reader");
  }

  *out_reader = reader;
  *out_owner = owner;
  return DATA_BIND_OK;
}

static DataBindStatus yaml_provider_open(
    const char *data,
    size_t len,
    size_t max_depth,
    cserde_reader **out_reader,
    void **out_owner,
    DataBindError *error) {
  cyaml_doc_t *document;

  if (out_reader == NULL || out_owner == NULL)
    return yaml_provider_error(error, DATA_BIND_ERR_INVALID_ARG,
                               "Invalid YAML provider output");
  *out_reader = NULL;
  *out_owner = NULL;

  document = yaml_parse_document(data, len, max_depth, error);
  if (document == NULL)
    return error != NULL && error->code != DATA_BIND_OK
               ? error->code
               : DATA_BIND_ERR_PARSE;

  return yaml_reader_from_json(
      document, json_value_from_cyaml(document), max_depth,
      out_reader, out_owner, error);
}

static DataBindStatus yaml_provider_open_selected(
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
  cyaml_doc_t *document;
  cyaml_path_result_t matches = {0};
  qvm_limits_t native_limits = yaml_query_limits(query_limits);
  qvm_diagnostic_t native_diagnostic = {0};
  json_value_t *selected = NULL;

  if (out_reader == NULL || out_owner == NULL)
    return yaml_provider_error(error, DATA_BIND_ERR_INVALID_ARG,
                               "Invalid selected YAML provider output");
  *out_reader = NULL;
  *out_owner = NULL;

  if (query_diagnostic != NULL) {
    size_t size = query_diagnostic->size;
    *query_diagnostic =
        (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
    query_diagnostic->size = size;
  }

  document = yaml_parse_document(data, len, max_depth, error);
  if (document == NULL)
    return error != NULL && error->code != DATA_BIND_OK
               ? error->code
               : DATA_BIND_ERR_PARSE;

  if (selection == DATA_BIND_STREAM_SELECT_ROOT)
    return yaml_reader_from_json(
        document, json_value_from_cyaml(document), max_depth,
        out_reader, out_owner, error);

  if (selection == DATA_BIND_STREAM_SELECT_ALL) {
    selected = json_value_from_cyaml(document);
    if (selected != NULL && json_type(selected) != JSON_ARRAY) {
      json_value_t *array = json_create_array();
      if (array == NULL || !json_array_add_checked(array, selected)) {
        json_free(array);
        json_free(selected);
        selected = NULL;
      } else {
        selected = array;
      }
    }
    return yaml_reader_from_json(
        document, selected, max_depth, out_reader, out_owner, error);
  }

  matches = cyaml_path_query_ex(
      document, NULL, path, &native_limits, &native_diagnostic);
  yaml_query_diagnostic(query_diagnostic, &native_diagnostic);

  if (matches.error != NULL ||
      (query_diagnostic != NULL &&
       query_diagnostic->status != DATA_BIND_QUERY_OK)) {
    DataBindStatus status = yaml_query_failure(query_diagnostic);
    const char *message =
        query_diagnostic != NULL && query_diagnostic->message[0] != '\0'
            ? query_diagnostic->message
            : (matches.error != NULL ? matches.error : "YPath query failed");
    cyaml_path_result_free(&matches);
    cyaml_free(document);
    return yaml_provider_error(error, status, message);
  }

  if (selection == DATA_BIND_STREAM_SELECT_PATH_FIRST) {
    if (matches.count == 0u) {
      cyaml_path_result_free(&matches);
      cyaml_free(document);
      return yaml_provider_error(
          error, DATA_BIND_ERR_TYPE_MISMATCH,
          "YPath selected no value");
    }
    selected = json_value_from_cyaml_node(
        document, cyaml_path_get(&matches, 0u));
  } else {
    uint32_t index;
    selected = json_create_array();
    for (index = 0u; selected != NULL && index < matches.count; ++index) {
      json_value_t *item = json_value_from_cyaml_node(
          document, cyaml_path_get(&matches, index));
      if (item == NULL || !json_array_add_checked(selected, item)) {
        json_free(item);
        json_free(selected);
        selected = NULL;
        break;
      }
    }
  }

  cyaml_path_result_free(&matches);
  return yaml_reader_from_json(
      document, selected, max_depth, out_reader, out_owner, error);
}

static void yaml_provider_close(cserde_reader *reader, void *opaque) {
  data_bind_yaml_owner *owner = (data_bind_yaml_owner *)opaque;
  json_cserde_reader_destroy(reader);
  if (owner != NULL) {
    json_free(owner->json);
    cyaml_free(owner->document);
    free(owner);
  }
}

static const DataBindFormatProvider YAML_PROVIDER =
    DATA_BIND_FORMAT_PROVIDER_WITH_SELECTION_INIT(
        DATA_BIND_FORMAT_YAML,
        yaml_provider_open,
        yaml_provider_close,
        yaml_provider_open_selected);

const DataBindFormatProvider *data_bind_yaml_format_provider(void) {
  return &YAML_PROVIDER;
}
