#include "data_bind_format_provider.h"

#include <stdio.h>
#include <string.h>

static DataBindStatus provider_error(
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

static void provider_error_clear(DataBindError *error) {
  if (error == NULL || error->size < sizeof(error->size)) return;
  if (error->size >= offsetof(DataBindError, code) + sizeof(error->code))
    error->code = DATA_BIND_OK;
  if (error->size >= offsetof(DataBindError, line) + sizeof(error->line))
    error->line = -1;
  if (error->size >= offsetof(DataBindError, column) + sizeof(error->column))
    error->column = -1;
  if (error->size >= offsetof(DataBindError, path) + sizeof(error->path))
    error->path[0] = '\0';
  if (error->size >= offsetof(DataBindError, message) + sizeof(error->message))
    error->message[0] = '\0';
}

static int provider_valid(const DataBindFormatProvider *provider) {
  return provider != NULL &&
      provider->size >= offsetof(DataBindFormatProvider, close_reader) +
                            sizeof(provider->close_reader) &&
      provider->abi_version == DATA_BIND_FORMAT_PROVIDER_ABI_VERSION &&
      provider->format >= DATA_BIND_FORMAT_BINARY &&
      provider->format <= DATA_BIND_FORMAT_XML &&
      provider->open_reader != NULL &&
      provider->close_reader != NULL;
}

static int provider_has_selected_reader(
    const DataBindFormatProvider *provider) {
  return provider_valid(provider) &&
      provider->size >= offsetof(DataBindFormatProvider, open_selected_reader) +
                            sizeof(provider->open_selected_reader) &&
      provider->open_selected_reader != NULL;
}

static DataBindStatus provider_publish_reader(
    const DataBindFormatProvider *provider,
    cserde_reader *native_reader,
    void *owner,
    DataBindFormatReader *out_reader,
    DataBindError *error) {
  if (native_reader == NULL ||
      native_reader->ops == NULL ||
      native_reader->state != CSERDE_READER_READY) {
    provider->close_reader(native_reader, owner);
    return provider_error(error, DATA_BIND_ERR_RUNTIME,
                          "Format provider returned an invalid CSerde reader");
  }

  out_reader->provider = provider;
  out_reader->reader = native_reader;
  out_reader->owner = owner;
  provider_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_format_reader_open(
    const DataBindFormatProvider *provider,
    const char *data,
    size_t len,
    size_t max_depth,
    DataBindFormatReader *out_reader,
    DataBindError *error) {
  cserde_reader *native_reader = NULL;
  void *owner = NULL;
  DataBindStatus status;
  size_t out_size;

  if (out_reader == NULL || out_reader->size < sizeof(*out_reader))
    return provider_error(error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid format reader output");
  out_size = out_reader->size;
  memset(out_reader, 0, sizeof(*out_reader));
  out_reader->size = out_size;

  if (!provider_valid(provider))
    return provider_error(error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid DataBind format provider");
  if (data == NULL && len != 0u)
    return provider_error(error, DATA_BIND_ERR_INVALID_ARG,
                          "Format input is NULL with non-zero length");

  status = provider->open_reader(
      data, len, max_depth, &native_reader, &owner, error);
  if (status != DATA_BIND_OK) return status;

  return provider_publish_reader(
      provider, native_reader, owner, out_reader, error);
}

DataBindStatus data_bind_format_reader_open_selected(
    const DataBindFormatProvider *provider,
    const char *data,
    size_t len,
    size_t max_depth,
    DataBindStreamSelection selection,
    const char *path,
    const DataBindQueryLimits *query_limits,
    DataBindQueryDiagnostic *query_diagnostic,
    DataBindFormatReader *out_reader,
    DataBindError *error) {
  cserde_reader *native_reader = NULL;
  void *owner = NULL;
  DataBindStatus status;
  size_t out_size;

  if (out_reader == NULL || out_reader->size < sizeof(*out_reader))
    return provider_error(error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid selected format reader output");
  out_size = out_reader->size;
  memset(out_reader, 0, sizeof(*out_reader));
  out_reader->size = out_size;

  if (!provider_has_selected_reader(provider))
    return provider_error(error, DATA_BIND_ERR_INVALID_ARG,
                          "Format provider does not support explicit selection");
  if (data == NULL && len != 0u)
    return provider_error(error, DATA_BIND_ERR_INVALID_ARG,
                          "Format input is NULL with non-zero length");
  if (selection < DATA_BIND_STREAM_SELECT_ROOT ||
      selection > DATA_BIND_STREAM_SELECT_PATH_ALL)
    return provider_error(error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid format selection");
  if ((selection == DATA_BIND_STREAM_SELECT_PATH_FIRST ||
       selection == DATA_BIND_STREAM_SELECT_PATH_ALL) &&
      (path == NULL || path[0] == '\0'))
    return provider_error(error, DATA_BIND_ERR_INVALID_ARG,
                          "Path selection requires a non-empty path");
  if ((selection == DATA_BIND_STREAM_SELECT_ROOT ||
       selection == DATA_BIND_STREAM_SELECT_ALL) &&
      path != NULL && path[0] != '\0')
    return provider_error(error, DATA_BIND_ERR_INVALID_ARG,
                          "Root/all selection does not accept a path");
  if (query_limits != NULL &&
      query_limits->size < sizeof(*query_limits))
    return provider_error(error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid DataBind query limits");
  if (query_diagnostic != NULL &&
      query_diagnostic->size < sizeof(*query_diagnostic))
    return provider_error(error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid DataBind query diagnostic");

  status = provider->open_selected_reader(
      data, len, max_depth, selection, path, query_limits, query_diagnostic,
      &native_reader, &owner, error);
  if (status != DATA_BIND_OK) return status;

  return provider_publish_reader(
      provider, native_reader, owner, out_reader, error);
}

DataBindStatus data_bind_format_reader_close(DataBindFormatReader *reader) {
  size_t size;
  if (reader == NULL || reader->size < sizeof(*reader))
    return DATA_BIND_ERR_INVALID_ARG;
  size = reader->size;
  if (reader->provider != NULL)
    reader->provider->close_reader(reader->reader, reader->owner);
  memset(reader, 0, sizeof(*reader));
  reader->size = size;
  return DATA_BIND_OK;
}
