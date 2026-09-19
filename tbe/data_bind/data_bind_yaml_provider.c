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

static DataBindStatus yaml_provider_open(
    const char *data,
    size_t len,
    size_t max_depth,
    cserde_reader **out_reader,
    void **out_owner,
    DataBindError *error) {
  cyaml_opts_t options = CYAML_OPTS_DEFAULT;
  cyaml_error_t diagnostic = {0};
  data_bind_yaml_owner *owner;
  cserde_reader *reader;

  if (out_reader == NULL || out_owner == NULL)
    return yaml_provider_error(error, DATA_BIND_ERR_INVALID_ARG,
                               "Invalid YAML provider output");
  *out_reader = NULL;
  *out_owner = NULL;

  if (max_depth > UINT32_MAX)
    options.max_depth = UINT32_MAX;
  else if (max_depth == 0u)
    options.max_depth = 1u;
  else
    options.max_depth = (uint32_t)max_depth;

  owner = (data_bind_yaml_owner *)calloc(1u, sizeof(*owner));
  if (owner == NULL)
    return yaml_provider_error(error, DATA_BIND_ERR_OOM,
                               "Unable to allocate YAML provider state");

  owner->document = cyaml_parse(data, len, &options, &diagnostic);
  if (owner->document == NULL) {
    DataBindStatus status =
        diagnostic.code == CYAML_ERR_NOMEM ? DATA_BIND_ERR_OOM
                                           : DATA_BIND_ERR_PARSE;
    yaml_provider_error(
        error, status,
        diagnostic.msg[0] != '\0' ? diagnostic.msg : "YAML parse failed");
    free(owner);
    return status;
  }

  owner->json = json_value_from_cyaml(owner->document);
  if (owner->json == NULL) {
    cyaml_free(owner->document);
    free(owner);
    return yaml_provider_error(
        error, DATA_BIND_ERR_PARSE,
        "YAML value cannot be represented by the canonical CSerde token model");
  }

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
    DATA_BIND_FORMAT_PROVIDER_INIT(
        DATA_BIND_FORMAT_YAML,
        yaml_provider_open,
        yaml_provider_close);

const DataBindFormatProvider *data_bind_yaml_format_provider(void) {
  return &YAML_PROVIDER;
}
