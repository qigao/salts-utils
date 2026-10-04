#include "data_bind_yaml_provider.h"

#include <cyaml.h>
#include <cyaml_json_adapter.h>
#include <json_cserde_reader.h>
#include <json_parser.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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


typedef struct data_bind_yaml_writer_frame {
  cserde_token_kind begin_kind;
  cyaml_node_t *node;
  int expect_key;
} data_bind_yaml_writer_frame;

typedef struct data_bind_yaml_writer_owner {
  cserde_writer writer;
  DataBindWriteFn write;
  void *write_user;
  cyaml_doc_t *document;
  data_bind_yaml_writer_frame *frames;
  size_t max_depth;
  size_t depth;
  char *pending_key;
  int root_written;
} data_bind_yaml_writer_owner;

static cserde_status yaml_writer_attach(
    data_bind_yaml_writer_owner *owner,
    cyaml_node_t *node) {
  data_bind_yaml_writer_frame *frame;
  if (owner == NULL || node == NULL) return CSERDE_INVALID_ARGUMENT;

  if (owner->depth == 0u) {
    if (owner->root_written) return CSERDE_UNSUPPORTED;
    cyaml_set_root(owner->document, node);
    owner->root_written = 1;
    return CSERDE_OK;
  }

  frame = &owner->frames[owner->depth - 1u];
  if (frame->begin_kind == CSERDE_ARRAY_BEGIN)
    return cyaml_seq_push(frame->node, node)
               ? CSERDE_OK
               : CSERDE_CALLBACK_ERROR;

  if (frame->begin_kind != CSERDE_MAP_BEGIN ||
      frame->expect_key || owner->pending_key == NULL)
    return CSERDE_UNSUPPORTED;

  if (cyaml_has(owner->document, frame->node, owner->pending_key))
    return CSERDE_UNSUPPORTED;
  if (!cyaml_map_set(
          owner->document, frame->node, owner->pending_key, node))
    return CSERDE_CALLBACK_ERROR;
  free(owner->pending_key);
  owner->pending_key = NULL;
  frame->expect_key = 1;
  return CSERDE_OK;
}

static cyaml_node_t *yaml_writer_scalar(
    data_bind_yaml_writer_owner *owner,
    const cserde_token *token) {
  if (owner == NULL || token == NULL) return NULL;
  switch (token->kind) {
  case CSERDE_NULL:
    return cyaml_new_null(owner->document);
  case CSERDE_BOOL:
    return cyaml_new_bool(owner->document, token->value.boolean);
  case CSERDE_SINT:
    return cyaml_new_int(owner->document, token->value.sint);
  case CSERDE_UINT:
    return cyaml_new_uint(owner->document, token->value.uint);
  case CSERDE_FLOAT:
    return isfinite(token->value.floating)
               ? cyaml_new_float(owner->document, token->value.floating)
               : NULL;
  case CSERDE_STRING:
    if (token->value.slice.size != 0u &&
        token->value.slice.data == NULL)
      return NULL;
    return cyaml_new_str(
        owner->document,
        token->value.slice.size != 0u
            ? (const char *)token->value.slice.data
            : "",
        token->value.slice.size);
  default:
    return NULL;
  }
}

static cserde_status yaml_writer_map_key(
    data_bind_yaml_writer_owner *owner,
    data_bind_yaml_writer_frame *frame,
    const cserde_token *token) {
  char *key;
  if (owner == NULL || frame == NULL || token == NULL ||
      token->kind != CSERDE_STRING || !frame->expect_key ||
      owner->pending_key != NULL ||
      (token->value.slice.size != 0u &&
       token->value.slice.data == NULL))
    return CSERDE_UNSUPPORTED;
  if (token->value.slice.size == SIZE_MAX)
    return CSERDE_LIMIT_EXCEEDED;
  if (token->value.slice.size != 0u &&
      memchr(token->value.slice.data, '\0',
             token->value.slice.size) != NULL)
    return CSERDE_UNSUPPORTED;
  key = (char *)malloc(token->value.slice.size + 1u);
  if (key == NULL) return CSERDE_CALLBACK_ERROR;
  if (token->value.slice.size != 0u)
    memcpy(key, token->value.slice.data, token->value.slice.size);
  key[token->value.slice.size] = '\0';
  owner->pending_key = key;
  frame->expect_key = 0;
  return CSERDE_OK;
}

static cserde_status yaml_cserde_write(
    void *opaque,
    const cserde_token *token) {
  data_bind_yaml_writer_owner *owner =
      (data_bind_yaml_writer_owner *)opaque;
  data_bind_yaml_writer_frame *frame;
  cyaml_node_t *node;
  cserde_status status;

  if (owner == NULL || token == NULL) return CSERDE_INVALID_ARGUMENT;

  if (owner->depth != 0u) {
    frame = &owner->frames[owner->depth - 1u];
    if (frame->begin_kind == CSERDE_MAP_BEGIN &&
        frame->expect_key && token->kind != CSERDE_MAP_END)
      return yaml_writer_map_key(owner, frame, token);
  }

  if (token->kind == CSERDE_ARRAY_END ||
      token->kind == CSERDE_MAP_END) {
    if (owner->depth == 0u) return CSERDE_UNSUPPORTED;
    frame = &owner->frames[owner->depth - 1u];
    if ((token->kind == CSERDE_ARRAY_END &&
         frame->begin_kind != CSERDE_ARRAY_BEGIN) ||
        (token->kind == CSERDE_MAP_END &&
         (frame->begin_kind != CSERDE_MAP_BEGIN ||
          !frame->expect_key || owner->pending_key != NULL)))
      return CSERDE_UNSUPPORTED;
    --owner->depth;
    return CSERDE_OK;
  }

  if (token->kind == CSERDE_ARRAY_BEGIN ||
      token->kind == CSERDE_MAP_BEGIN) {
    if (owner->depth >= owner->max_depth)
      return CSERDE_LIMIT_EXCEEDED;
    node = token->kind == CSERDE_ARRAY_BEGIN
               ? cyaml_new_seq(owner->document)
               : cyaml_new_map(owner->document);
    if (node == NULL) return CSERDE_CALLBACK_ERROR;
    status = yaml_writer_attach(owner, node);
    if (status != CSERDE_OK) return status;
    frame = &owner->frames[owner->depth++];
    frame->begin_kind = token->kind;
    frame->node = node;
    frame->expect_key = token->kind == CSERDE_MAP_BEGIN;
    return CSERDE_OK;
  }

  if (token->kind == CSERDE_BYTES)
    return CSERDE_UNSUPPORTED;

  node = yaml_writer_scalar(owner, token);
  if (node == NULL) return CSERDE_UNSUPPORTED;
  return yaml_writer_attach(owner, node);
}

static cserde_status yaml_cserde_finish(void *opaque) {
  data_bind_yaml_writer_owner *owner =
      (data_bind_yaml_writer_owner *)opaque;
  char *output;
  size_t output_len = 0u;
  int write_status;

  if (owner == NULL || owner->write == NULL ||
      owner->document == NULL || !owner->root_written ||
      owner->depth != 0u || owner->pending_key != NULL)
    return CSERDE_UNSUPPORTED;

  output = cyaml_emit(owner->document, NULL, &output_len);
  if (output == NULL) return CSERDE_CALLBACK_ERROR;
  write_status = owner->write(output, output_len, owner->write_user);
  free(output);
  return write_status == 0 ? CSERDE_OK : CSERDE_SINK_ERROR;
}

static const cserde_writer_ops YAML_WRITER_OPS = {
    sizeof(cserde_writer_ops),
    CSERDE_WRITER_OPS_ABI_VERSION,
    yaml_cserde_write,
    yaml_cserde_finish};

static DataBindStatus yaml_provider_writer_status(
    cserde_status status,
    DataBindError *error) {
  switch (status) {
  case CSERDE_OK:
    return yaml_provider_error(error, DATA_BIND_OK, "");
  case CSERDE_LIMIT_EXCEEDED:
    return yaml_provider_error(
        error, DATA_BIND_ERR_LIMIT,
        "YAML writer exceeded its configured depth");
  case CSERDE_UNSUPPORTED:
    return yaml_provider_error(
        error, DATA_BIND_ERR_TYPE_MISMATCH,
        "Canonical CSerde token stream is not representable as YAML");
  case CSERDE_SINK_ERROR:
    return yaml_provider_error(
        error, DATA_BIND_ERR_IO,
        "YAML writer byte sink rejected output");
  case CSERDE_CALLBACK_ERROR:
    return yaml_provider_error(
        error, DATA_BIND_ERR_OOM,
        "YAML writer could not build or emit the document");
  default:
    return yaml_provider_error(
        error, DATA_BIND_ERR_RUNTIME,
        "YAML CSerde writer failed");
  }
}

static DataBindStatus yaml_provider_writer_open(
    DataBindWriteFn write,
    void *write_user,
    size_t max_depth,
    cserde_writer **out_writer,
    void **out_owner,
    DataBindError *error) {
  data_bind_yaml_writer_owner *owner;

  if (write == NULL || out_writer == NULL || out_owner == NULL)
    return yaml_provider_error(
        error, DATA_BIND_ERR_INVALID_ARG,
        "Invalid YAML writer request");
  *out_writer = NULL;
  *out_owner = NULL;

  if (max_depth > SIZE_MAX / sizeof(*owner->frames))
    return yaml_provider_error(
        error, DATA_BIND_ERR_LIMIT,
        "YAML writer depth is too large");

  owner = (data_bind_yaml_writer_owner *)calloc(1u, sizeof(*owner));
  if (owner == NULL)
    return yaml_provider_error(
        error, DATA_BIND_ERR_OOM,
        "Unable to allocate YAML writer lease");
  owner->write = write;
  owner->write_user = write_user;
  owner->max_depth = max_depth;
  owner->document = cyaml_doc_new();
  if (owner->document == NULL) {
    free(owner);
    return yaml_provider_error(
        error, DATA_BIND_ERR_OOM,
        "Unable to allocate YAML writer document");
  }
  if (max_depth != 0u) {
    owner->frames = (data_bind_yaml_writer_frame *)calloc(
        max_depth, sizeof(*owner->frames));
    if (owner->frames == NULL) {
      cyaml_free(owner->document);
      free(owner);
      return yaml_provider_error(
          error, DATA_BIND_ERR_OOM,
          "Unable to allocate YAML writer depth stack");
    }
  }
  if (cserde_writer_init(
          &owner->writer, &YAML_WRITER_OPS, owner) != CSERDE_OK) {
    free(owner->frames);
    cyaml_free(owner->document);
    free(owner);
    return yaml_provider_error(
        error, DATA_BIND_ERR_RUNTIME,
        "Unable to initialize YAML CSerde writer");
  }

  *out_writer = &owner->writer;
  *out_owner = owner;
  return yaml_provider_error(error, DATA_BIND_OK, "");
}

static DataBindStatus yaml_provider_writer_close(
    cserde_writer *writer,
    void *opaque,
    DataBindError *error) {
  data_bind_yaml_writer_owner *owner =
      (data_bind_yaml_writer_owner *)opaque;
  cserde_status status =
      writer != NULL ? cserde_writer_finish(writer)
                     : CSERDE_INVALID_ARGUMENT;
  if (owner != NULL) {
    free(owner->pending_key);
    free(owner->frames);
    cyaml_free(owner->document);
    free(owner);
  }
  return yaml_provider_writer_status(status, error);
}

static const DataBindFormatProvider YAML_PROVIDER =
    DATA_BIND_FORMAT_PROVIDER_WITH_SELECTION_AND_WRITER_INIT(
        DATA_BIND_FORMAT_YAML,
        yaml_provider_open,
        yaml_provider_close,
        yaml_provider_open_selected,
        yaml_provider_writer_open,
        yaml_provider_writer_close);

const DataBindFormatProvider *data_bind_yaml_format_provider(void) {
  return &YAML_PROVIDER;
}
