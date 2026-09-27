#include "data_bind_socket_execution_plan.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct DataBindSocketExecutionPlan {
  DataBindSocketPlan socket;
  char *channel_name;
  char *message_type;
  DataBindNativeTypeBinding native;
  DataBindFormatPlan *format;
  DataBindMessagePlan *message;
};

static size_t socket_exec_required_plan_size(void) {
  return offsetof(DataBindSocketPlan, native_binding) +
         sizeof(((DataBindSocketPlan *)0)->native_binding);
}

static void socket_exec_error_clear(DataBindError *error) {
  size_t size;
  if (error == NULL) return;
  size = error->size != 0u && error->size < sizeof(*error)
             ? error->size
             : sizeof(*error);
  memset(error, 0, size);
  if (size >= sizeof(size_t)) error->size = size;
  if (size >= offsetof(DataBindError, code) + sizeof(error->code))
    error->code = DATA_BIND_OK;
  if (size >= offsetof(DataBindError, line) + sizeof(error->line))
    error->line = -1;
  if (size >= offsetof(DataBindError, column) + sizeof(error->column))
    error->column = -1;
}

static DataBindStatus socket_exec_fail(
    DataBindError *error,
    DataBindStatus status,
    const char *path,
    const char *message) {
  size_t size;
  if (error == NULL) return status;
  size = error->size != 0u && error->size < sizeof(*error)
             ? error->size
             : sizeof(*error);
  socket_exec_error_clear(error);
  if (size >= offsetof(DataBindError, code) + sizeof(error->code))
    error->code = status;
  if (size >= offsetof(DataBindError, path) + sizeof(error->path))
    snprintf(error->path, sizeof(error->path), "%s",
             path != NULL ? path : "");
  if (size >= offsetof(DataBindError, message) + sizeof(error->message))
    snprintf(error->message, sizeof(error->message), "%s",
             message != NULL ? message : "Socket execution plan failed");
  return status;
}

static char *socket_exec_strdup(const char *text) {
  size_t length;
  char *copy;
  if (text == NULL) return NULL;
  length = strlen(text);
  if (length == SIZE_MAX) return NULL;
  copy = (char *)malloc(length + 1u);
  if (copy != NULL) memcpy(copy, text, length + 1u);
  return copy;
}

void data_bind_socket_execution_plan_free(
    DataBindSocketExecutionPlan *plan) {
  if (plan == NULL) return;
  data_bind_message_plan_free(plan->message);
  data_bind_format_plan_free(plan->format);
  free(plan->message_type);
  free(plan->channel_name);
  free(plan);
}

DataBindStatus data_bind_socket_execution_plan_compile(
    DataBind *codec,
    const DataBindSocketPlan *socket_plan,
    DataBindSocketExecutionPlan **out_plan,
    DataBindError *error) {
  DataBindSocketExecutionPlan *plan = NULL;
  DataBindError resolver_error = DATA_BIND_ERROR_INIT;
  DataBindMessagePlanDiagnostic message_diagnostic =
      DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
  DataBindStatus status;

  socket_exec_error_clear(error);
  if (out_plan != NULL) *out_plan = NULL;

  if (codec == NULL || socket_plan == NULL || out_plan == NULL)
    return socket_exec_fail(
        error, DATA_BIND_ERR_INVALID_ARG, NULL,
        "Invalid Socket execution plan compile arguments");

  if (socket_plan->size < socket_exec_required_plan_size() ||
      socket_plan->abi_version != DATA_BIND_SOCKET_PLAN_ABI_VERSION ||
      socket_plan->channel_name == NULL ||
      socket_plan->channel_name[0] == '\0' ||
      socket_plan->message_type == NULL ||
      socket_plan->message_type[0] == '\0' ||
      socket_plan->native_binding == NULL)
    return socket_exec_fail(
        error, DATA_BIND_ERR_SCHEMA,
        socket_plan->message_type,
        "SocketPlan does not expose the required native-binding contract");

  plan = (DataBindSocketExecutionPlan *)calloc(1u, sizeof(*plan));
  if (plan == NULL)
    return socket_exec_fail(
        error, DATA_BIND_ERR_OOM, socket_plan->message_type,
        "Could not allocate SocketExecutionPlan");

  plan->channel_name = socket_exec_strdup(socket_plan->channel_name);
  plan->message_type = socket_exec_strdup(socket_plan->message_type);
  if (plan->channel_name == NULL || plan->message_type == NULL) {
    status = socket_exec_fail(
        error, DATA_BIND_ERR_OOM, socket_plan->message_type,
        "Could not copy SocketPlan identity");
    goto fail;
  }

  plan->socket = *socket_plan;
  plan->socket.size = sizeof(plan->socket);
  plan->socket.channel_name = plan->channel_name;
  plan->socket.message_type = plan->message_type;

  status = socket_plan->native_binding(&plan->native, &resolver_error);
  if (status != DATA_BIND_OK) {
    status = socket_exec_fail(
        error, status,
        resolver_error.path[0] != '\0'
            ? resolver_error.path
            : socket_plan->message_type,
        resolver_error.message[0] != '\0'
            ? resolver_error.message
            : "Socket native-binding resolver failed");
    goto fail;
  }

  if (plan->native.size <
          offsetof(DataBindNativeTypeBinding, null_count) +
              sizeof(plan->native.null_count) ||
      plan->native.abi_version != DATA_BIND_NATIVE_BINDING_ABI_VERSION ||
      plan->native.idl_type_name == NULL ||
      strcmp(plan->native.idl_type_name, plan->message_type) != 0) {
    status = socket_exec_fail(
        error, DATA_BIND_ERR_TYPE_MISMATCH, plan->message_type,
        "Socket native binding does not match the Channel message type");
    goto fail;
  }

  status = data_bind_message_plan_compile(
      codec, plan->message_type, &plan->native,
      &plan->message, &message_diagnostic);
  if (status != DATA_BIND_OK) {
    status = socket_exec_fail(
        error, status,
        message_diagnostic.schema_field[0] != '\0'
            ? message_diagnostic.schema_field
            : plan->message_type,
        message_diagnostic.message[0] != '\0'
            ? message_diagnostic.message
            : "Could not compile Socket MessagePlan");
    goto fail;
  }

  status = data_bind_format_plan_compile(
      codec, plan->message_type, plan->socket.format,
      &plan->format, error);
  if (status != DATA_BIND_OK) goto fail;

  *out_plan = plan;
  socket_exec_error_clear(error);
  return DATA_BIND_OK;

fail:
  data_bind_socket_execution_plan_free(plan);
  return status;
}

const DataBindSocketPlan *data_bind_socket_execution_plan_socket(
    const DataBindSocketExecutionPlan *plan) {
  return plan != NULL ? &plan->socket : NULL;
}

const DataBindNativeTypeBinding *
data_bind_socket_execution_plan_native_binding(
    const DataBindSocketExecutionPlan *plan) {
  return plan != NULL ? &plan->native : NULL;
}

DataBindStatus data_bind_socket_execution_plan_decode_native(
    const DataBindSocketExecutionPlan *plan,
    const DataBindNativeOptions *native_options,
    cserde_reader *reader,
    void *destination,
    size_t destination_bytes,
    DataBindMessagePlanDiagnostic *diagnostic) {
  if (plan == NULL || plan->message == NULL)
    return DATA_BIND_ERR_INVALID_ARG;
  return data_bind_message_plan_decode_native(
      plan->message, native_options, reader,
      destination, destination_bytes, diagnostic);
}


static int socket_exec_provider_format_matches(
    const DataBindFormatProvider *provider,
    DataBindFormat format) {
  return provider != NULL &&
      provider->size >=
          offsetof(DataBindFormatProvider, format) + sizeof(provider->format) &&
      provider->abi_version == DATA_BIND_FORMAT_PROVIDER_ABI_VERSION &&
      provider->format == format;
}

DataBindStatus data_bind_socket_execution_plan_decode_payload(
    const DataBindSocketExecutionPlan *plan,
    const DataBindFormatProvider *provider,
    const void *payload,
    size_t payload_bytes,
    size_t max_depth,
    const DataBindNativeOptions *native_options,
    void *destination,
    size_t destination_bytes,
    DataBindMessagePlanDiagnostic *message_diagnostic,
    DataBindError *format_error) {
  DataBindFormatReader format_reader = DATA_BIND_FORMAT_READER_INIT;
  DataBindFormatCanonicalReader canonical =
      DATA_BIND_FORMAT_CANONICAL_READER_INIT;
  DataBindStatus status;
  DataBindStatus close_status;

  socket_exec_error_clear(format_error);

  if (plan == NULL || plan->message == NULL || plan->format == NULL ||
      native_options == NULL || destination == NULL ||
      (payload == NULL && payload_bytes != 0u))
    return socket_exec_fail(
        format_error, DATA_BIND_ERR_INVALID_ARG,
        plan != NULL ? plan->message_type : NULL,
        "Invalid Socket payload decode arguments");

  if (!socket_exec_provider_format_matches(provider, plan->socket.format))
    return socket_exec_fail(
        format_error, DATA_BIND_ERR_TYPE_MISMATCH, plan->message_type,
        "Socket format provider does not match the compiled SocketPlan format");

  status = data_bind_format_reader_open(
      provider, (const char *)payload, payload_bytes, max_depth,
      &format_reader, format_error);
  if (status != DATA_BIND_OK) return status;

  status = data_bind_format_canonical_reader_init(
      plan->format, format_reader.reader, &canonical, format_error);
  if (status == DATA_BIND_OK) {
    status = data_bind_socket_execution_plan_decode_native(
        plan, native_options,
        data_bind_format_canonical_reader_reader(&canonical),
        destination, destination_bytes, message_diagnostic);
  }

  close_status = data_bind_format_reader_close(&format_reader);
  if (status == DATA_BIND_OK && close_status != DATA_BIND_OK)
    return socket_exec_fail(
        format_error, close_status, plan->message_type,
        "Socket format provider lease could not be closed");

  return status;
}
