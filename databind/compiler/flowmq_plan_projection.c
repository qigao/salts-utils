#include "flowmq_plan_projection.h"

#include "binary_layout_ir.h"
#include "binary_reader_codegen.h"
#include "message_native.h"
#include "opaque_plan_codegen.h"

#include "cmeta_fs.h"
#include "cmeta_uuid.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int flowmq_identifier_valid(const char *text) {
  size_t i;
  if (text == NULL || text[0] == '\0' ||
      !((text[0] >= 'A' && text[0] <= 'Z') ||
        (text[0] >= 'a' && text[0] <= 'z') || text[0] == '_'))
    return 0;
  for (i = 1u; text[i] != '\0'; ++i)
    if (!((text[i] >= 'A' && text[i] <= 'Z') ||
          (text[i] >= 'a' && text[i] <= 'z') ||
          (text[i] >= '0' && text[i] <= '9') || text[i] == '_'))
      return 0;
  return 1;
}

static int flowmq_include_basename_valid(const char *text) {
  size_t i;
  if (text == NULL || text[0] == '\0' ||
      strcmp(text, ".") == 0 || strcmp(text, "..") == 0)
    return 0;
  for (i = 0u; text[i] != '\0'; ++i) {
    unsigned char ch = (unsigned char)text[i];
    if (!((ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z') ||
          (ch >= '0' && ch <= '9') ||
          ch == '_' || ch == '-' || ch == '.'))
      return 0;
  }
  return 1;
}

static int flowmq_c_string(FILE *file, const char *text) {
  const unsigned char *p = (const unsigned char *)text;
  if (file == NULL || text == NULL || fputc('"', file) == EOF) return -1;
  for (; *p != '\0'; ++p) {
    if (*p == '\\' || *p == '"') {
      if (fputc('\\', file) == EOF || fputc((int)*p, file) == EOF)
        return -1;
    } else if (*p < 0x20u || *p >= 0x7fu) {
      if (fprintf(file, "\\x%02X", (unsigned)*p) < 0) return -1;
    } else if (fputc((int)*p, file) == EOF) {
      return -1;
    }
  }
  return fputc('"', file) == EOF ? -1 : 0;
}

static const char *flowmq_format_name(DataBindFormat format) {
  switch (format) {
  case DATA_BIND_FORMAT_JSON:
    return "DATA_BIND_FORMAT_JSON";
  case DATA_BIND_FORMAT_BINARY:
    return "DATA_BIND_FORMAT_BINARY";
  default:
    return NULL;
  }
}

static const char *flowmq_pattern_name(
    DataBindFlowMQChannelPattern pattern) {
  switch (pattern) {
  case DATA_BIND_FLOWMQ_CHANNEL_PUB_SUB:
    return "DATA_BIND_FLOWMQ_CHANNEL_PUB_SUB";
  case DATA_BIND_FLOWMQ_CHANNEL_PUSH_PULL:
    return "DATA_BIND_FLOWMQ_CHANNEL_PUSH_PULL";
  default:
    return NULL;
  }
}

static const char *flowmq_service_pattern_name(
    DataBindFlowMQServicePattern pattern) {
  switch (pattern) {
  case DATA_BIND_FLOWMQ_SERVICE_REQ_REP:
    return "DATA_BIND_FLOWMQ_SERVICE_REQ_REP";
  case DATA_BIND_FLOWMQ_SERVICE_ROUTER_DEALER:
    return "DATA_BIND_FLOWMQ_SERVICE_ROUTER_DEALER";
  default:
    return NULL;
  }
}

static int flowmq_service_profile_valid(
    DataBindPayloadKind kind,
    DataBindFormat format,
    size_t opaque_max_bytes) {
  if (kind == DATA_BIND_PAYLOAD_FORMAT)
    return flowmq_format_name(format) != NULL;
  if (kind == DATA_BIND_PAYLOAD_OPAQUE)
    return format == DATA_BIND_FORMAT_NONE &&
           opaque_max_bytes != 0u &&
           opaque_max_bytes <= UINT32_MAX;
  return 0;
}

static const char *flowmq_payload_kind_name(DataBindPayloadKind kind) {
  switch (kind) {
  case DATA_BIND_PAYLOAD_FORMAT:
    return "DATA_BIND_PAYLOAD_FORMAT";
  case DATA_BIND_PAYLOAD_OPAQUE:
    return "DATA_BIND_PAYLOAD_OPAQUE";
  default:
    return NULL;
  }
}

static const char *flowmq_service_format_name(
    DataBindPayloadKind kind, DataBindFormat format) {
  if (kind == DATA_BIND_PAYLOAD_OPAQUE)
    return format == DATA_BIND_FORMAT_NONE ? "DATA_BIND_FORMAT_NONE" : NULL;
  if (kind != DATA_BIND_PAYLOAD_FORMAT) return NULL;
  return flowmq_format_name(format);
}

static int flowmq_config_valid(
    const databind_compiler_flowmq_projection_config *config) {
  int service_mode;
  if (config == NULL ||
      !flowmq_identifier_valid(config->symbol_prefix) ||
      config->max_payload_bytes == 0u ||
      config->max_payload_bytes > UINT32_MAX)
    return 0;

  service_mode =
      config->service_name != NULL || config->operation_name != NULL;
  if (service_mode) {
    const int has_formatted =
        config->ingress_payload_kind == DATA_BIND_PAYLOAD_FORMAT ||
        config->egress_payload_kind == DATA_BIND_PAYLOAD_FORMAT;
    return config->service_name != NULL &&
           config->service_name[0] != '\0' &&
           config->operation_name != NULL &&
           config->operation_name[0] != '\0' &&
           config->channel_name == NULL &&
           flowmq_service_pattern_name(config->service_pattern) != NULL &&
           flowmq_service_profile_valid(
               config->ingress_payload_kind,
               config->ingress_format,
               config->opaque_max_bytes) &&
           flowmq_service_profile_valid(
               config->egress_payload_kind,
               config->egress_format,
               config->opaque_max_bytes) &&
           (has_formatted
                ? flowmq_include_basename_valid(config->native_header_include)
                : config->native_header_include == NULL);
  }

  if (config->channel_name == NULL || config->channel_name[0] == '\0' ||
      flowmq_pattern_name(config->pattern) == NULL)
    return 0;

  if (config->payload_kind == DATA_BIND_PAYLOAD_FORMAT) {
    return flowmq_include_basename_valid(config->native_header_include) &&
           flowmq_format_name(config->format) != NULL;
  }

  if (config->payload_kind == DATA_BIND_PAYLOAD_OPAQUE)
    return config->format == DATA_BIND_FORMAT_NONE &&
           config->native_header_include == NULL &&
           config->opaque_max_bytes != 0u &&
           config->opaque_max_bytes <= UINT32_MAX;

  return 0;
}

static int flowmq_payload_representable(
    const IdlContract *contract,
    const databind_binary_format_plan *format_plan,
    const char *message_type,
    const databind_compiler_flowmq_projection_config *config) {
  databind_binary_type_layout layout = {0};
  databind_binary_layout_diagnostic diagnostic = {0};
  databind_binary_layout_status status;

  if (config == NULL || message_type == NULL || message_type[0] == '\0')
    return 0;

  if (config->payload_kind == DATA_BIND_PAYLOAD_OPAQUE)
    return databind_compiler_opaque_plan_admit(message_type) == 0;

  if (config->payload_kind != DATA_BIND_PAYLOAD_FORMAT)
    return 0;

  if (config->format == DATA_BIND_FORMAT_JSON)
    return 1;
  if (config->format != DATA_BIND_FORMAT_BINARY)
    return 0;

  status = databind_binary_layout_build(
      contract, format_plan, message_type, &layout, &diagnostic);
  databind_binary_layout_destroy(&layout);
  return status == DATABIND_BINARY_LAYOUT_OK;
}

static int flowmq_open_atomic(
    const char *output, char **out_temp, FILE **out_file) {
  cmeta_uuid_t uuid;
  char uuid_text[SALTS_UUID_STRING_SIZE];
  size_t length;
  char *temp;
  FILE *file;

  if (output == NULL || output[0] == '\0' ||
      out_temp == NULL || out_file == NULL)
    return -1;
  *out_temp = NULL;
  *out_file = NULL;
  if (cmeta_uuid_v4_generate(&uuid) != SALTS_OK ||
      cmeta_uuid_format(&uuid, uuid_text, sizeof(uuid_text)) != SALTS_OK)
    return -1;
  length = strlen(output);
  if (length > SIZE_MAX - strlen(uuid_text) - 7u) return -1;
  temp = (char *)malloc(length + strlen(uuid_text) + 7u);
  if (temp == NULL) return -1;
  snprintf(temp, length + strlen(uuid_text) + 7u, "%s.%s.tmp",
           output, uuid_text);
  file = fopen(temp, "wb");
  if (file == NULL) {
    free(temp);
    return -1;
  }
  *out_temp = temp;
  *out_file = file;
  return 0;
}

static int flowmq_commit_atomic(
    const char *output, char *temp, FILE *file, int success) {
  int result = -1;
  if (file != NULL && fclose(file) != 0) success = 0;
  if (success && cmeta_fs_rename(temp, output) == SALTS_OK) result = 0;
  if (result != 0 && temp != NULL) (void)cmeta_fs_unlink(temp);
  free(temp);
  return result;
}


static const IdlOperation *flowmq_service_operation_find(
    const IdlService *service, const char *operation_name) {
  size_t i;
  if (service == NULL || operation_name == NULL) return NULL;
  for (i = 0u; i < service->operation_count; ++i) {
    const IdlOperation *operation = &service->operations[i];
    if (operation->name != NULL &&
        strcmp(operation->name, operation_name) == 0)
      return operation;
  }
  return NULL;
}

static int flowmq_service_payload_representable(
    const IdlContract *contract,
    const databind_binary_format_plan *format_plan,
    const char *type_name,
    DataBindPayloadKind kind,
    DataBindFormat format) {
  databind_binary_type_layout layout = {0};
  databind_binary_layout_diagnostic diagnostic = {0};
  databind_binary_layout_status status;

  if (contract == NULL || format_plan == NULL ||
      type_name == NULL || type_name[0] == '\0')
    return 0;

  if (kind == DATA_BIND_PAYLOAD_OPAQUE)
    return databind_compiler_opaque_plan_admit(type_name) == 0;
  if (kind != DATA_BIND_PAYLOAD_FORMAT)
    return 0;

  if (format == DATA_BIND_FORMAT_JSON)
    return 1;
  if (format != DATA_BIND_FORMAT_BINARY)
    return 0;

  status = databind_binary_layout_build(
      contract, format_plan, type_name, &layout, &diagnostic);
  databind_binary_layout_destroy(&layout);
  return status == DATABIND_BINARY_LAYOUT_OK;
}

static int flowmq_service_qualified_names(
    const IdlContract *contract,
    const IdlService *service,
    const IdlOperation *operation,
    char *service_out, size_t service_out_size,
    char *operation_out, size_t operation_out_size) {
  int service_written;
  int operation_written;
  if (contract == NULL || contract->name == NULL ||
      service == NULL || service->name == NULL ||
      operation == NULL || operation->name == NULL ||
      service_out == NULL || operation_out == NULL)
    return 0;
  service_written = snprintf(
      service_out, service_out_size, "%s.%s",
      contract->name, service->name);
  operation_written = snprintf(
      operation_out, operation_out_size, "%s.%s.%s",
      contract->name, service->name, operation->name);
  return service_written > 0 &&
         (size_t)service_written < service_out_size &&
         operation_written > 0 &&
         (size_t)operation_written < operation_out_size;
}

static int flowmq_service_generate(
    const databind_compiler_projection_input *input,
    const databind_compiler_projection_request *request,
    const databind_compiler_flowmq_projection_config *config) {
  const IdlService *service;
  const IdlOperation *operation;
  databind_compiler_message_native_binding request_binding = {0};
  databind_compiler_message_native_binding response_binding = {0};
  char request_symbol[384];
  char response_symbol[384];
  char qualified_service[512];
  char qualified_operation[768];
  char *temp = NULL;
  FILE *file = NULL;
  int ok = 0;

  if (input == NULL || input->contract == NULL || input->binary_format == NULL ||
      request == NULL || request->output == NULL || config == NULL)
    return -1;

  service = idl_contract_find_service(input->contract, config->service_name);
  if (service == NULL) return -1;
  operation =
      flowmq_service_operation_find(service, config->operation_name);
  if (operation == NULL ||
      operation->request_type == NULL ||
      operation->response_type == NULL ||
      strcmp(operation->request_type, "void") == 0 ||
      strcmp(operation->response_type, "void") == 0)
    return -1;

  if (!flowmq_service_payload_representable(
          input->contract, input->binary_format,
          operation->request_type,
          config->ingress_payload_kind,
          config->ingress_format) ||
      !flowmq_service_payload_representable(
          input->contract, input->binary_format,
          operation->response_type,
          config->egress_payload_kind,
          config->egress_format))
    return -1;

  if (config->ingress_payload_kind == DATA_BIND_PAYLOAD_FORMAT &&
      databind_compiler_message_native_build(
          input->contract, operation->request_type, &request_binding) != 0)
    goto cleanup;
  if (config->egress_payload_kind == DATA_BIND_PAYLOAD_FORMAT &&
      databind_compiler_message_native_build(
          input->contract, operation->response_type, &response_binding) != 0)
    goto cleanup;

  if (snprintf(
          request_symbol, sizeof(request_symbol),
          "%s_flowmq_service_request", config->symbol_prefix) <= 0 ||
      strlen(request_symbol) >= sizeof(request_symbol) - 1u ||
      snprintf(
          response_symbol, sizeof(response_symbol),
          "%s_flowmq_service_response", config->symbol_prefix) <= 0 ||
      strlen(response_symbol) >= sizeof(response_symbol) - 1u ||
      !flowmq_service_qualified_names(
          input->contract, service, operation,
          qualified_service, sizeof(qualified_service),
          qualified_operation, sizeof(qualified_operation)))
    goto cleanup;

  if (flowmq_open_atomic(request->output, &temp, &file) != 0)
    goto cleanup;

  if (fprintf(
          file,
          "#ifndef DATABIND_GENERATED_%s_FLOWMQ_PLAN_H\n"
          "#define DATABIND_GENERATED_%s_FLOWMQ_PLAN_H\n\n"
          "#include <data_bind_flowmq_plan.h>\n",
          config->symbol_prefix,
          config->symbol_prefix) < 0)
    goto cleanup;

  if (config->ingress_payload_kind == DATA_BIND_PAYLOAD_FORMAT ||
      config->egress_payload_kind == DATA_BIND_PAYLOAD_FORMAT) {
    if (fprintf(file, "#include \"%s\"\n\n",
                config->native_header_include) < 0)
      goto cleanup;
  } else if (fputc('\n', file) == EOF) {
    goto cleanup;
  }

  if (config->ingress_payload_kind == DATA_BIND_PAYLOAD_FORMAT &&
      databind_compiler_message_native_emit_binding(
          file, &request_binding, request_symbol) != 0)
    goto cleanup;

  if (config->egress_payload_kind == DATA_BIND_PAYLOAD_FORMAT) {
    if (fputc('\n', file) == EOF ||
        databind_compiler_message_native_emit_binding(
            file, &response_binding, response_symbol) != 0)
      goto cleanup;
  }

  if ((config->ingress_payload_kind == DATA_BIND_PAYLOAD_OPAQUE ||
       config->egress_payload_kind == DATA_BIND_PAYLOAD_OPAQUE) &&
      (fputc('\n', file) == EOF ||
       databind_compiler_opaque_plan_emit(
           file, config->symbol_prefix, config->opaque_max_bytes) != 0))
    goto cleanup;

  if (config->ingress_payload_kind == DATA_BIND_PAYLOAD_FORMAT &&
      config->ingress_format == DATA_BIND_FORMAT_BINARY &&
      databind_compiler_binary_reader_admit(
          input->contract, input->binary_format,
          operation->request_type) == 0) {
    if (fputc('\n', file) == EOF ||
        databind_compiler_binary_reader_emit(
            file, input->contract, input->binary_format,
            operation->request_type, config->symbol_prefix) != 0)
      goto cleanup;
  }

  /*
   * Binary egress is admitted by BinaryLayoutIR above, but DataBind does not
   * yet publish a canonical Binary streaming writer provider. Do not emit a
   * response-side reader and accidentally advertise it as an egress adapter.
   */
  if (fprintf(
          file,
          "\nstatic const DataBindFlowMQServicePlan "
          "%s_flowmq_service_plan = {\n"
          "  sizeof(DataBindFlowMQServicePlan), "
          "DATA_BIND_FLOWMQ_SERVICE_PLAN_ABI_VERSION,\n"
          "  ",
          config->symbol_prefix) < 0 ||
      flowmq_c_string(file, qualified_service) != 0 ||
      fputs(",\n  ", file) == EOF ||
      flowmq_c_string(file, qualified_operation) != 0 ||
      fputs(",\n  ", file) == EOF ||
      flowmq_c_string(file, operation->request_type) != 0 ||
      fputs(",\n  ", file) == EOF ||
      flowmq_c_string(file, operation->response_type) != 0 ||
      fprintf(
          file,
          ",\n  %s, %s, %s, %zuu,\n  ",
          flowmq_service_format_name(
              config->ingress_payload_kind, config->ingress_format),
          flowmq_service_format_name(
              config->egress_payload_kind, config->egress_format),
          flowmq_service_pattern_name(config->service_pattern),
          config->max_payload_bytes) < 0)
    goto cleanup;

  if (config->ingress_payload_kind == DATA_BIND_PAYLOAD_FORMAT) {
    if (fprintf(
            file, "%s__databind_message_native_binding,\n  ",
            request_symbol) < 0)
      goto cleanup;
  } else if (fputs("NULL,\n  ", file) == EOF) {
    goto cleanup;
  }

  if (config->egress_payload_kind == DATA_BIND_PAYLOAD_FORMAT) {
    if (fprintf(
            file, "%s__databind_message_native_binding,\n  ",
            response_symbol) < 0)
      goto cleanup;
  } else if (fputs("NULL,\n  ", file) == EOF) {
    goto cleanup;
  }

  if (fprintf(
          file, "%s, %s, ",
          flowmq_payload_kind_name(config->ingress_payload_kind),
          flowmq_payload_kind_name(config->egress_payload_kind)) < 0)
    goto cleanup;

  if (config->ingress_payload_kind == DATA_BIND_PAYLOAD_OPAQUE) {
    if (fprintf(file, "&%s_opaque_plan, ", config->symbol_prefix) < 0)
      goto cleanup;
  } else if (fputs("NULL, ", file) == EOF) {
    goto cleanup;
  }

  if (config->egress_payload_kind == DATA_BIND_PAYLOAD_OPAQUE) {
    if (fprintf(file, "&%s_opaque_plan\n", config->symbol_prefix) < 0)
      goto cleanup;
  } else if (fputs("NULL\n", file) == EOF) {
    goto cleanup;
  }

  if (fprintf(
          file,
          "};\n\n"
          "#endif /* DATABIND_GENERATED_%s_FLOWMQ_PLAN_H */\n",
          config->symbol_prefix) < 0)
    goto cleanup;

  ok = 1;

cleanup:
  databind_compiler_message_native_destroy(&request_binding);
  databind_compiler_message_native_destroy(&response_binding);
  if (file == NULL) {
    free(temp);
    return -1;
  }
  return flowmq_commit_atomic(request->output, temp, file, ok);
}

static int flowmq_generate(
    const databind_compiler_projection_input *input,
    const databind_compiler_projection_request *request,
    void *context) {
  const databind_compiler_flowmq_projection_config *config =
      request != NULL
          ? (const databind_compiler_flowmq_projection_config *)request->config
          : NULL;
  const IdlChannel *channel;
  const char *message_type;
  databind_compiler_message_native_binding native_binding = {0};
  char native_symbol[384];
  char *temp = NULL;
  FILE *file = NULL;
  int ok = 0;
  (void)context;

  if (input == NULL || input->contract == NULL || input->binary_format == NULL ||
      request == NULL || request->output == NULL ||
      request->id.axis != DATABIND_COMPILER_PROJECTION_AXIS_TRANSPORT ||
      request->id.kind != DATABIND_COMPILER_TRANSPORT_FLOWMQ ||
      !flowmq_config_valid(config))
    return -1;

  if (config->service_name != NULL)
    return flowmq_service_generate(input, request, config);

  channel = idl_contract_find_channel(
      input->contract, config->channel_name);
  if (channel == NULL || channel->message_type == NULL ||
      channel->message_type[0] == '\0')
    return -1;
  message_type = channel->message_type;
  if (!flowmq_payload_representable(
          input->contract, input->binary_format, message_type, config))
    return -1;
  if (config->payload_kind == DATA_BIND_PAYLOAD_FORMAT &&
      databind_compiler_message_native_build(
          input->contract, message_type, &native_binding) != 0)
    return -1;
  if (config->payload_kind == DATA_BIND_PAYLOAD_FORMAT &&
      (snprintf(
           native_symbol, sizeof(native_symbol), "%s_flowmq",
           config->symbol_prefix) <= 0 ||
       strlen(native_symbol) >= sizeof(native_symbol) - 1u)) {
    databind_compiler_message_native_destroy(&native_binding);
    return -1;
  }

  if (flowmq_open_atomic(request->output, &temp, &file) != 0) {
    databind_compiler_message_native_destroy(&native_binding);
    return -1;
  }

  if (fprintf(file,
              "#ifndef DATABIND_GENERATED_%s_FLOWMQ_PLAN_H\n"
              "#define DATABIND_GENERATED_%s_FLOWMQ_PLAN_H\n\n"
              "#include <data_bind_flowmq_plan.h>\n",
              config->symbol_prefix,
              config->symbol_prefix) < 0)
    goto cleanup;

  if (config->payload_kind == DATA_BIND_PAYLOAD_FORMAT) {
    if (fprintf(file, "#include \"%s\"\n\n",
                config->native_header_include) < 0 ||
        databind_compiler_message_native_emit_binding(
            file, &native_binding, native_symbol) != 0)
      goto cleanup;
  } else {
    if (fputc('\n', file) == EOF ||
        databind_compiler_opaque_plan_emit(
            file, config->symbol_prefix, config->opaque_max_bytes) != 0)
      goto cleanup;
  }

  if (config->payload_kind == DATA_BIND_PAYLOAD_FORMAT &&
      config->format == DATA_BIND_FORMAT_BINARY &&
      databind_compiler_binary_reader_admit(
          input->contract, input->binary_format, message_type) == 0) {
    if (fputc('\n', file) == EOF ||
        databind_compiler_binary_reader_emit(
            file, input->contract, input->binary_format, message_type,
            config->symbol_prefix) != 0)
      goto cleanup;
  }

  if (fprintf(
          file,
          "\nstatic const DataBindFlowMQChannelPlan %s_flowmq_channel_plan = {\n"
          "  sizeof(DataBindFlowMQChannelPlan), "
          "DATA_BIND_FLOWMQ_CHANNEL_PLAN_ABI_VERSION,\n"
          "  ",
          config->symbol_prefix) < 0 ||
      flowmq_c_string(file, config->channel_name) != 0 ||
      fputs(",\n  ", file) == EOF ||
      flowmq_c_string(file, message_type) != 0)
    goto cleanup;

  if (config->payload_kind == DATA_BIND_PAYLOAD_FORMAT) {
    if (fprintf(
            file,
            ",\n  %s, %s, %zuu,\n"
            "  %s__databind_message_native_binding,\n"
            "  DATA_BIND_PAYLOAD_FORMAT, NULL\n",
            flowmq_format_name(config->format),
            flowmq_pattern_name(config->pattern),
            config->max_payload_bytes,
            native_symbol) < 0)
      goto cleanup;
  } else {
    if (fprintf(
            file,
            ",\n  DATA_BIND_FORMAT_NONE, %s, %zuu,\n"
            "  NULL, DATA_BIND_PAYLOAD_OPAQUE, &%s_opaque_plan\n",
            flowmq_pattern_name(config->pattern),
            config->max_payload_bytes,
            config->symbol_prefix) < 0)
      goto cleanup;
  }

  if (fprintf(
          file,
          "};\n\n"
          "#endif /* DATABIND_GENERATED_%s_FLOWMQ_PLAN_H */\n",
          config->symbol_prefix) < 0)
    goto cleanup;

  ok = 1;

cleanup:
  databind_compiler_message_native_destroy(&native_binding);
  return flowmq_commit_atomic(request->output, temp, file, ok);
}

databind_compiler_projection_backend
databind_compiler_flowmq_plan_backend(void) {
  databind_compiler_projection_backend backend = {
      {DATABIND_COMPILER_PROJECTION_AXIS_TRANSPORT,
       DATABIND_COMPILER_TRANSPORT_FLOWMQ},
      "flowmq",
      flowmq_generate,
      NULL,
      DATABIND_COMPILER_OUTPUT_STAGED_SINGLE};
  return backend;
}
