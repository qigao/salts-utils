#include "data_bind_json_provider.h"

#include <json_cserde_reader.h>
#include <json_parser.h>

#include <stddef.h>
#include <stdio.h>

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

static DataBindStatus json_provider_open(
    const char *data,
    size_t len,
    size_t max_depth,
    cserde_reader **out_reader,
    void **out_owner,
    DataBindError *error) {
  json_value_t *root;
  cserde_reader *reader;
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

  reader = json_cserde_reader_create(root, max_depth);
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

static void json_provider_close(cserde_reader *reader, void *owner) {
  json_cserde_reader_destroy(reader);
  json_free((json_value_t *)owner);
}

static const DataBindFormatProvider JSON_PROVIDER =
    DATA_BIND_FORMAT_PROVIDER_INIT(
        DATA_BIND_FORMAT_JSON,
        json_provider_open,
        json_provider_close);

const DataBindFormatProvider *data_bind_json_format_provider(void) {
  return &JSON_PROVIDER;
}
