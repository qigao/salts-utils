#include "method_plan_projection.h"
#include "binary_layout_ir.h"
#include "opaque_plan_codegen.h"

#include "cmeta_fs.h"
#include "cmeta_uuid.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int projection_type_has_field(
    const IdlContract *contract,
    const char *type_name, const char *field_name) {
  const IdlDataDecl *type;
  size_t i;
  if (contract == NULL || type_name == NULL || field_name == NULL) return 0;
  type = idl_contract_find_data(contract, type_name);
  if (type == NULL || type->fields == NULL) return 0;
  for (i = 0u; i < type->field_count; ++i)
    if (type->fields[i].name != NULL &&
        strcmp(type->fields[i].name, field_name) == 0)
      return 1;
  return 0;
}

static int projection_operation_has_error(
    const IdlOperation *operation, const char *error_type) {
  size_t i;
  if (operation == NULL || error_type == NULL ||
      operation->error_types == NULL)
    return 0;
  for (i = 0u; i < operation->error_count; ++i)
    if (operation->error_types[i] != NULL &&
        strcmp(operation->error_types[i], error_type) == 0)
      return 1;
  return 0;
}

static int projection_c_identifier_valid(const char *text) {
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

static int projection_c_string(FILE *file, const char *text) {
  const unsigned char *p = (const unsigned char *)text;
  if (file == NULL || text == NULL || fputc('"', file) == EOF) return -1;
  for (; *p != '\0'; ++p) {
    switch (*p) {
    case '\\':
      if (fputs("\\\\", file) == EOF) return -1;
      break;
    case '"':
      if (fputs("\\\"", file) == EOF) return -1;
      break;
    case '\n':
      if (fputs("\\n", file) == EOF) return -1;
      break;
    case '\r':
      if (fputs("\\r", file) == EOF) return -1;
      break;
    case '\t':
      if (fputs("\\t", file) == EOF) return -1;
      break;
    default:
      if (*p < 0x20u || *p >= 0x7fu) {
        if (fprintf(file, "\\x%02X", (unsigned)*p) < 0) return -1;
      } else if (fputc((int)*p, file) == EOF) {
        return -1;
      }
      break;
    }
  }
  return fputc('"', file) == EOF ? -1 : 0;
}

static int projection_open_atomic(
    const char *output, char **out_temp, FILE **out_file) {
  cmeta_uuid_t uuid;
  char uuid_text[SALTS_UUID_STRING_SIZE];
  size_t length;
  char *temp;
  FILE *file;
  if (output == NULL || output[0] == '\0' || out_temp == NULL ||
      out_file == NULL)
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

static int projection_commit_atomic(
    const char *output, char *temp, FILE *file, int success) {
  int result = -1;
  if (file != NULL && fclose(file) != 0) success = 0;
  if (success && cmeta_fs_rename(temp, output) == SALTS_OK) result = 0;
  if (result != 0 && temp != NULL) (void)cmeta_fs_unlink(temp);
  free(temp);
  return result;
}

static const char *http_location_name(
    databind_compiler_http_field_location location) {
  switch (location) {
  case DATABIND_COMPILER_HTTP_PATH:
    return "DATA_BIND_HTTP_PATH";
  case DATABIND_COMPILER_HTTP_QUERY:
    return "DATA_BIND_HTTP_QUERY";
  case DATABIND_COMPILER_HTTP_HEADER:
    return "DATA_BIND_HTTP_HEADER";
  case DATABIND_COMPILER_HTTP_COOKIE:
    return "DATA_BIND_HTTP_COOKIE";
  case DATABIND_COMPILER_HTTP_BODY:
    return "DATA_BIND_HTTP_BODY";
  case DATABIND_COMPILER_HTTP_RESPONSE_HEADER:
    return "DATA_BIND_HTTP_RESPONSE_HEADER";
  case DATABIND_COMPILER_HTTP_RESPONSE_BODY:
    return "DATA_BIND_HTTP_RESPONSE_BODY";
  default:
    return NULL;
  }
}

static const char *direction_name(
    databind_compiler_projection_direction direction) {
  switch (direction) {
  case DATABIND_COMPILER_PROJECTION_INGRESS:
    return "DATA_BIND_BINDING_INGRESS";
  case DATABIND_COMPILER_PROJECTION_EGRESS:
    return "DATA_BIND_BINDING_EGRESS";
  default:
    return NULL;
  }
}

static const char *runtime_format_name(DataBindFormat format) {
  switch (format) {
  case DATA_BIND_FORMAT_JSON:
    return "DATA_BIND_FORMAT_JSON";
  case DATA_BIND_FORMAT_BINARY:
    return "DATA_BIND_FORMAT_BINARY";
  case DATA_BIND_FORMAT_YAML:
    return "DATA_BIND_FORMAT_YAML";
  case DATA_BIND_FORMAT_CSV:
    return "DATA_BIND_FORMAT_CSV";
  case DATA_BIND_FORMAT_XML:
    return "DATA_BIND_FORMAT_XML";
  }
  return NULL;
}

static int compiler_format_valid(DataBindFormat format) {
  return runtime_format_name(format) != NULL;
}

static const char *runtime_payload_kind_name(DataBindPayloadKind kind) {
  switch (kind) {
  case DATA_BIND_PAYLOAD_FORMAT:
    return "DATA_BIND_PAYLOAD_FORMAT";
  case DATA_BIND_PAYLOAD_OPAQUE:
    return "DATA_BIND_PAYLOAD_OPAQUE";
  default:
    return NULL;
  }
}

static const char *runtime_payload_format_name(
    DataBindPayloadKind kind, DataBindFormat format) {
  if (kind == DATA_BIND_PAYLOAD_OPAQUE)
    return format == DATA_BIND_FORMAT_NONE ? "DATA_BIND_FORMAT_NONE" : NULL;
  if (kind != DATA_BIND_PAYLOAD_FORMAT) return NULL;
  return runtime_format_name(format);
}

static int compiler_payload_profile_valid(
    DataBindPayloadKind kind, DataBindFormat format, size_t opaque_max_bytes) {
  if (kind == DATA_BIND_PAYLOAD_FORMAT)
    return compiler_format_valid(format);
  if (kind == DATA_BIND_PAYLOAD_OPAQUE)
    return format == DATA_BIND_FORMAT_NONE &&
           opaque_max_bytes != 0u && opaque_max_bytes <= UINT32_MAX;
  return 0;
}

static int projection_format_type_representable(
    const IdlContract *contract,
    const databind_binary_format_plan *format_plan,
    const char *type_name,
    DataBindFormat format) {
  databind_binary_type_layout layout = {0};
  databind_binary_layout_diagnostic diagnostic = {0};
  databind_binary_layout_status status;

  if (format != DATA_BIND_FORMAT_BINARY ||
      type_name == NULL || strcmp(type_name, "void") == 0)
    return 1;

  status = databind_binary_layout_build(
      contract, format_plan, type_name, &layout, &diagnostic);
  databind_binary_layout_destroy(&layout);
  return status == DATABIND_BINARY_LAYOUT_OK;
}

static int projection_operation_formats_representable(
    const IdlContract *contract,
    const databind_binary_format_plan *format_plan,
    const IdlOperation *operation,
    DataBindFormat ingress_format,
    DataBindFormat egress_format) {
  if (operation == NULL) return 0;
  return projection_format_type_representable(
             contract, format_plan, operation->request_type, ingress_format) &&
         projection_format_type_representable(
             contract, format_plan, operation->response_type, egress_format);
}

static int projection_payload_type_representable(
    const IdlContract *contract,
    const databind_binary_format_plan *format_plan,
    const char *type_name,
    DataBindPayloadKind kind,
    DataBindFormat format) {
  if (kind == DATA_BIND_PAYLOAD_OPAQUE)
    return databind_compiler_opaque_plan_admit(type_name) == 0;
  if (kind != DATA_BIND_PAYLOAD_FORMAT) return 0;
  return projection_format_type_representable(
      contract, format_plan, type_name, format);
}

static int projection_operation_profiles_representable(
    const IdlContract *contract,
    const databind_binary_format_plan *format_plan,
    const IdlOperation *operation,
    DataBindPayloadKind ingress_kind,
    DataBindFormat ingress_format,
    DataBindPayloadKind egress_kind,
    DataBindFormat egress_format) {
  if (operation == NULL) return 0;
  return projection_payload_type_representable(
             contract, format_plan, operation->request_type,
             ingress_kind, ingress_format) &&
         projection_payload_type_representable(
             contract, format_plan, operation->response_type,
             egress_kind, egress_format);
}

static int http_method_valid(const char *method) {
  static const char *const methods[] = {
      "GET", "HEAD", "POST", "PUT", "DELETE",
      "CONNECT", "OPTIONS", "TRACE", "PATCH"};
  size_t i;
  if (method == NULL || method[0] == '\0') return 0;
  for (i = 0u; i < sizeof(methods) / sizeof(methods[0]); ++i)
    if (strcmp(method, methods[i]) == 0) return 1;
  return 0;
}

static int config_operation_matches(
    const char *service, const char *operation,
    const char *candidate_service, const char *candidate_operation) {
  return service != NULL && operation != NULL &&
         candidate_service != NULL && candidate_operation != NULL &&
         strcmp(service, candidate_service) == 0 &&
         strcmp(operation, candidate_operation) == 0;
}

static const databind_compiler_http_operation_config *http_operation_config(
    const databind_compiler_http_projection_config *config,
    const char *service, const char *operation, size_t *out_index) {
  size_t i;
  if (out_index != NULL) *out_index = SIZE_MAX;
  for (i = 0u; config != NULL && i < config->operation_count; ++i) {
    if (config_operation_matches(
            service, operation, config->operations[i].service_name,
            config->operations[i].operation_name)) {
      if (out_index != NULL) *out_index = i;
      return &config->operations[i];
    }
  }
  return NULL;
}

static const databind_compiler_rpc_operation_config *rpc_operation_config(
    const databind_compiler_rpc_projection_config *config,
    const char *service, const char *operation, size_t *out_index) {
  size_t i;
  if (out_index != NULL) *out_index = SIZE_MAX;
  for (i = 0u; config != NULL && i < config->operation_count; ++i) {
    if (config_operation_matches(
            service, operation, config->operations[i].service_name,
            config->operations[i].operation_name)) {
      if (out_index != NULL) *out_index = i;
      return &config->operations[i];
    }
  }
  return NULL;
}

static int http_route_placeholder_count(
    const char *route, const char *name) {
  const char *p = route;
  size_t count = 0u;
  size_t length;
  if (route == NULL || name == NULL || name[0] == '\0') return 0;
  length = strlen(name);
  while ((p = strchr(p, '{')) != NULL) {
    const char *end = strchr(p + 1, '}');
    if (end == NULL) return 0;
    if ((size_t)(end - p - 1) == length &&
        memcmp(p + 1, name, length) == 0)
      ++count;
    p = end + 1;
  }
  return (int)count;
}

static int http_field_location_valid(
    databind_compiler_projection_direction direction,
    databind_compiler_http_field_location location) {
  if (direction == DATABIND_COMPILER_PROJECTION_INGRESS)
    return location >= DATABIND_COMPILER_HTTP_PATH &&
           location <= DATABIND_COMPILER_HTTP_BODY;
  if (direction == DATABIND_COMPILER_PROJECTION_EGRESS)
    return location == DATABIND_COMPILER_HTTP_RESPONSE_HEADER ||
           location == DATABIND_COMPILER_HTTP_RESPONSE_BODY;
  return 0;
}

static int http_config_shape_valid(
    const databind_compiler_http_projection_config *config) {
  size_t i, j;
  const char *prefix =
      config != NULL && config->symbol_prefix != NULL
          ? config->symbol_prefix : "databind_generated";
  if (!projection_c_identifier_valid(prefix)) return 0;
  if (config == NULL) return 1;
  if ((config->operation_count != 0u && config->operations == NULL) ||
      (config->field_count != 0u && config->fields == NULL) ||
      (config->error_count != 0u && config->errors == NULL))
    return 0;
  for (i = 0u; i < config->operation_count; ++i) {
    const databind_compiler_http_operation_config *left = &config->operations[i];
    if (left->service_name == NULL || left->operation_name == NULL ||
        (left->method != NULL && !http_method_valid(left->method)) ||
        !compiler_payload_profile_valid(
            left->ingress_payload_kind,
            left->ingress_format,
            left->opaque_max_bytes) ||
        !compiler_payload_profile_valid(
            left->egress_payload_kind,
            left->egress_format,
            left->opaque_max_bytes) ||
        (left->route != NULL &&
         (left->route[0] != '/' || strchr(left->route, '?') != NULL ||
          strchr(left->route, '#') != NULL)) ||
        (left->success_status != 0 &&
         (left->success_status < 100 || left->success_status > 599)))
      return 0;
    for (j = 0u; j < i; ++j)
      if (config_operation_matches(
              left->service_name, left->operation_name,
              config->operations[j].service_name,
              config->operations[j].operation_name))
        return 0;
  }
  for (i = 0u; i < config->field_count; ++i) {
    const databind_compiler_http_field_config *left = &config->fields[i];
    {
      const databind_compiler_http_operation_config *operation =
          http_operation_config(
              config, left->service_name, left->operation_name, NULL);
      if (left->service_name == NULL || left->operation_name == NULL ||
          left->schema_field == NULL ||
          !http_field_location_valid(left->direction, left->location) ||
          (left->wire_name != NULL && left->wire_name[0] == '\0') ||
          (operation != NULL &&
           left->direction == DATABIND_COMPILER_PROJECTION_INGRESS &&
           operation->ingress_payload_kind == DATA_BIND_PAYLOAD_OPAQUE) ||
          (operation != NULL &&
           left->direction == DATABIND_COMPILER_PROJECTION_EGRESS &&
           operation->egress_payload_kind == DATA_BIND_PAYLOAD_OPAQUE))
        return 0;
    }
    for (j = 0u; j < i; ++j)
      if (left->direction == config->fields[j].direction &&
          config_operation_matches(
              left->service_name, left->operation_name,
              config->fields[j].service_name,
              config->fields[j].operation_name) &&
          strcmp(left->schema_field, config->fields[j].schema_field) == 0)
        return 0;
  }
  for (i = 0u; i < config->error_count; ++i) {
    const databind_compiler_http_error_config *left = &config->errors[i];
    {
      const databind_compiler_http_operation_config *operation =
          http_operation_config(
              config, left->service_name, left->operation_name, NULL);
      if (left->service_name == NULL || left->operation_name == NULL ||
          left->error_type == NULL || left->status < 100 || left->status > 599 ||
          (operation != NULL &&
           (operation->ingress_payload_kind == DATA_BIND_PAYLOAD_OPAQUE ||
            operation->egress_payload_kind == DATA_BIND_PAYLOAD_OPAQUE)))
        return 0;
    }
    for (j = 0u; j < i; ++j)
      if (config_operation_matches(
              left->service_name, left->operation_name,
              config->errors[j].service_name,
              config->errors[j].operation_name) &&
          strcmp(left->error_type, config->errors[j].error_type) == 0)
        return 0;
  }
  return 1;
}

static int rpc_config_shape_valid(
    const databind_compiler_rpc_projection_config *config) {
  size_t i, j;
  const char *prefix =
      config != NULL && config->symbol_prefix != NULL
          ? config->symbol_prefix : "databind_generated";
  if (!projection_c_identifier_valid(prefix)) return 0;
  if (config == NULL) return 1;
  if ((config->operation_count != 0u && config->operations == NULL) ||
      (config->field_count != 0u && config->fields == NULL) ||
      (config->error_count != 0u && config->errors == NULL))
    return 0;
  for (i = 0u; i < config->operation_count; ++i) {
    const databind_compiler_rpc_operation_config *left = &config->operations[i];
    if (left->service_name == NULL || left->operation_name == NULL ||
        (left->wire_method != NULL && left->wire_method[0] == '\0') ||
        !compiler_format_valid(left->ingress_format) ||
        !compiler_format_valid(left->egress_format))
      return 0;
    for (j = 0u; j < i; ++j)
      if (config_operation_matches(
              left->service_name, left->operation_name,
              config->operations[j].service_name,
              config->operations[j].operation_name))
        return 0;
  }
  for (i = 0u; i < config->field_count; ++i) {
    const databind_compiler_rpc_field_config *left = &config->fields[i];
    if (left->service_name == NULL || left->operation_name == NULL ||
        left->schema_field == NULL ||
        (left->direction != DATABIND_COMPILER_PROJECTION_INGRESS &&
         left->direction != DATABIND_COMPILER_PROJECTION_EGRESS) ||
        (left->wire_name != NULL && left->wire_name[0] == '\0'))
      return 0;
    for (j = 0u; j < i; ++j)
      if (left->direction == config->fields[j].direction &&
          config_operation_matches(
              left->service_name, left->operation_name,
              config->fields[j].service_name,
              config->fields[j].operation_name) &&
          strcmp(left->schema_field, config->fields[j].schema_field) == 0)
        return 0;
  }
  for (i = 0u; i < config->error_count; ++i) {
    const databind_compiler_rpc_error_config *left = &config->errors[i];
    if (left->service_name == NULL || left->operation_name == NULL ||
        left->error_type == NULL || left->code == 0)
      return 0;
    for (j = 0u; j < i; ++j)
      if (config_operation_matches(
              left->service_name, left->operation_name,
              config->errors[j].service_name,
              config->errors[j].operation_name) &&
          strcmp(left->error_type, config->errors[j].error_type) == 0)
        return 0;
  }
  return 1;
}

static int http_operation_config_valid(
    const IdlContract *contract,
    const databind_binary_format_plan *format_plan, const IdlOperation *operation,
    const databind_compiler_http_projection_config *config,
    const char *service_name, const char *operation_name) {
  const databind_compiler_http_operation_config *op_config =
      http_operation_config(config, service_name, operation_name, NULL);
  const char *request_type =
      operation != NULL ? operation->request_type : NULL;
  const char *response_type =
      operation != NULL ? operation->response_type : NULL;
  const DataBindFormat ingress_format =
      op_config != NULL ? op_config->ingress_format : DATA_BIND_FORMAT_JSON;
  const DataBindFormat egress_format =
      op_config != NULL ? op_config->egress_format : DATA_BIND_FORMAT_JSON;
  const DataBindPayloadKind ingress_kind =
      op_config != NULL ? op_config->ingress_payload_kind
                        : DATA_BIND_PAYLOAD_FORMAT;
  const DataBindPayloadKind egress_kind =
      op_config != NULL ? op_config->egress_payload_kind
                        : DATA_BIND_PAYLOAD_FORMAT;
  size_t i;

  if (!projection_operation_profiles_representable(
          contract, format_plan, operation,
          ingress_kind, ingress_format,
          egress_kind, egress_format))
    return 0;
  for (i = 0u; config != NULL && i < config->field_count; ++i) {
    const databind_compiler_http_field_config *field = &config->fields[i];
    const char *type_name;
    const char *wire;
    if (!config_operation_matches(
            service_name, operation_name,
            field->service_name, field->operation_name))
      continue;
    type_name = field->direction == DATABIND_COMPILER_PROJECTION_INGRESS
                    ? request_type : response_type;
    if (type_name == NULL || strcmp(type_name, "void") == 0 ||
        !projection_type_has_field(contract, type_name, field->schema_field))
      return 0;
    if (field->location == DATABIND_COMPILER_HTTP_PATH) {
      if (op_config == NULL || op_config->route == NULL) return 0;
      wire = field->wire_name != NULL ? field->wire_name : field->schema_field;
      if (http_route_placeholder_count(op_config->route, wire) != 1)
        return 0;
    }
  }
  for (i = 0u; config != NULL && i < config->error_count; ++i) {
    const databind_compiler_http_error_config *error = &config->errors[i];
    if (config_operation_matches(
            service_name, operation_name,
            error->service_name, error->operation_name) &&
        !projection_operation_has_error(operation, error->error_type))
      return 0;
  }
  if (op_config != NULL && op_config->route != NULL) {
    const char *p = op_config->route;
    while ((p = strchr(p, '{')) != NULL) {
      const char *end = strchr(p + 1, '}');
      size_t matches = 0u;
      size_t length;
      if (end == NULL || end == p + 1) return 0;
      length = (size_t)(end - p - 1);
      for (i = 0u; config != NULL && i < config->field_count; ++i) {
        const databind_compiler_http_field_config *field = &config->fields[i];
        const char *wire;
        if (!config_operation_matches(
                service_name, operation_name,
                field->service_name, field->operation_name) ||
            field->direction != DATABIND_COMPILER_PROJECTION_INGRESS ||
            field->location != DATABIND_COMPILER_HTTP_PATH)
          continue;
        wire = field->wire_name != NULL ? field->wire_name : field->schema_field;
        if (strlen(wire) == length && memcmp(wire, p + 1, length) == 0)
          ++matches;
      }
      if (matches != 1u) return 0;
      p = end + 1;
    }
  }
  return 1;
}

static int rpc_operation_config_valid(
    const IdlContract *contract,
    const databind_binary_format_plan *format_plan, const IdlOperation *operation,
    const databind_compiler_rpc_projection_config *config,
    const char *service_name, const char *operation_name) {
  const databind_compiler_rpc_operation_config *op_config =
      rpc_operation_config(config, service_name, operation_name, NULL);
  const char *request_type =
      operation != NULL ? operation->request_type : NULL;
  const char *response_type =
      operation != NULL ? operation->response_type : NULL;
  const DataBindFormat ingress_format =
      op_config != NULL ? op_config->ingress_format : DATA_BIND_FORMAT_JSON;
  const DataBindFormat egress_format =
      op_config != NULL ? op_config->egress_format : DATA_BIND_FORMAT_JSON;
  size_t i;

  if (!projection_operation_formats_representable(
          contract, format_plan, operation, ingress_format, egress_format))
    return 0;
  for (i = 0u; config != NULL && i < config->field_count; ++i) {
    const databind_compiler_rpc_field_config *field = &config->fields[i];
    const char *type_name;
    if (!config_operation_matches(
            service_name, operation_name,
            field->service_name, field->operation_name))
      continue;
    type_name = field->direction == DATABIND_COMPILER_PROJECTION_INGRESS
                    ? request_type : response_type;
    if (type_name == NULL || strcmp(type_name, "void") == 0 ||
        !projection_type_has_field(contract, type_name, field->schema_field))
      return 0;
  }
  for (i = 0u; config != NULL && i < config->error_count; ++i) {
    const databind_compiler_rpc_error_config *error = &config->errors[i];
    if (config_operation_matches(
            service_name, operation_name,
            error->service_name, error->operation_name) &&
        !projection_operation_has_error(operation, error->error_type))
      return 0;
  }
  return 1;
}

static size_t projection_operation_count(
    const IdlContract *contract) {
  size_t count = 0u;
  size_t i;
  if (contract == NULL) return 0u;
  for (i = 0u; i < contract->service_count; ++i)
    count += contract->services[i].operation_count;
  return count;
}

static int http_emit_fields(
    FILE *file, const databind_compiler_http_projection_config *config,
    const char *prefix, const char *service, const char *operation,
    size_t operation_index, size_t *out_count) {
  size_t i, count = 0u;
  for (i = 0u; config != NULL && i < config->field_count; ++i)
    if (config_operation_matches(
            service, operation, config->fields[i].service_name,
            config->fields[i].operation_name))
      ++count;
  *out_count = count;
  if (count == 0u) return 0;

  if (fprintf(file,
              "static const DataBindHttpFieldProjection %s_http_fields_%zu[] = {\n",
              prefix, operation_index) < 0)
    return -1;
  for (i = 0u; i < config->field_count; ++i) {
    const databind_compiler_http_field_config *field = &config->fields[i];
    const char *direction;
    const char *location;
    if (!config_operation_matches(
            service, operation, field->service_name, field->operation_name))
      continue;
    direction = direction_name(field->direction);
    location = http_location_name(field->location);
    if (direction == NULL || location == NULL ||
        fprintf(file, "  { sizeof(DataBindHttpFieldProjection), %s, ", direction) < 0 ||
        projection_c_string(file, field->schema_field) != 0 ||
        fprintf(file, ", %s, ", location) < 0)
      return -1;
    if (field->wire_name != NULL) {
      if (projection_c_string(file, field->wire_name) != 0) return -1;
    } else if (fputs("NULL", file) == EOF) {
      return -1;
    }
    if (field->ordinal == SIZE_MAX) {
      if (fputs(", SIZE_MAX },\n", file) == EOF) return -1;
    } else if (fprintf(file, ", %zuu },\n", field->ordinal) < 0) {
      return -1;
    }
  }
  return fputs("};\n\n", file) == EOF ? -1 : 0;
}

static int http_emit_errors(
    FILE *file, const databind_compiler_http_projection_config *config,
    const char *prefix, const char *service, const char *operation,
    size_t operation_index, size_t *out_count) {
  size_t i, count = 0u;
  for (i = 0u; config != NULL && i < config->error_count; ++i)
    if (config_operation_matches(
            service, operation, config->errors[i].service_name,
            config->errors[i].operation_name))
      ++count;
  *out_count = count;
  if (count == 0u) return 0;

  if (fprintf(file,
              "static const DataBindHttpErrorMapping %s_http_errors_%zu[] = {\n",
              prefix, operation_index) < 0)
    return -1;
  for (i = 0u; i < config->error_count; ++i) {
    const databind_compiler_http_error_config *error = &config->errors[i];
    if (!config_operation_matches(
            service, operation, error->service_name, error->operation_name))
      continue;
    if (fputs("  { sizeof(DataBindHttpErrorMapping), ", file) == EOF ||
        projection_c_string(file, error->error_type) != 0 ||
        fprintf(file, ", %d },\n", error->status) < 0)
      return -1;
  }
  return fputs("};\n\n", file) == EOF ? -1 : 0;
}

static int rpc_emit_fields(
    FILE *file, const databind_compiler_rpc_projection_config *config,
    const char *prefix, const char *service, const char *operation,
    size_t operation_index, size_t *out_count) {
  size_t i, count = 0u;
  for (i = 0u; config != NULL && i < config->field_count; ++i)
    if (config_operation_matches(
            service, operation, config->fields[i].service_name,
            config->fields[i].operation_name))
      ++count;
  *out_count = count;
  if (count == 0u) return 0;

  if (fprintf(file,
              "static const DataBindRpcFieldProjection %s_rpc_fields_%zu[] = {\n",
              prefix, operation_index) < 0)
    return -1;
  for (i = 0u; i < config->field_count; ++i) {
    const databind_compiler_rpc_field_config *field = &config->fields[i];
    const char *direction;
    if (!config_operation_matches(
            service, operation, field->service_name, field->operation_name))
      continue;
    direction = direction_name(field->direction);
    if (direction == NULL ||
        fprintf(file, "  { sizeof(DataBindRpcFieldProjection), %s, ", direction) < 0 ||
        projection_c_string(file, field->schema_field) != 0 ||
        fputs(", ", file) == EOF)
      return -1;
    if (field->wire_name != NULL) {
      if (projection_c_string(file, field->wire_name) != 0) return -1;
    } else if (fputs("NULL", file) == EOF) {
      return -1;
    }
    if (field->ordinal == SIZE_MAX) {
      if (fputs(", SIZE_MAX },\n", file) == EOF) return -1;
    } else if (fprintf(file, ", %zuu },\n", field->ordinal) < 0) {
      return -1;
    }
  }
  return fputs("};\n\n", file) == EOF ? -1 : 0;
}

static int rpc_emit_errors(
    FILE *file, const databind_compiler_rpc_projection_config *config,
    const char *prefix, const char *service, const char *operation,
    size_t operation_index, size_t *out_count) {
  size_t i, count = 0u;
  for (i = 0u; config != NULL && i < config->error_count; ++i)
    if (config_operation_matches(
            service, operation, config->errors[i].service_name,
            config->errors[i].operation_name))
      ++count;
  *out_count = count;
  if (count == 0u) return 0;

  if (fprintf(file,
              "static const DataBindRpcErrorMapping %s_rpc_errors_%zu[] = {\n",
              prefix, operation_index) < 0)
    return -1;
  for (i = 0u; i < config->error_count; ++i) {
    const databind_compiler_rpc_error_config *error = &config->errors[i];
    if (!config_operation_matches(
            service, operation, error->service_name, error->operation_name))
      continue;
    if (fputs("  { sizeof(DataBindRpcErrorMapping), ", file) == EOF ||
        projection_c_string(file, error->error_type) != 0 ||
        fprintf(file, ", %d },\n", error->code) < 0)
      return -1;
  }
  return fputs("};\n\n", file) == EOF ? -1 : 0;
}

static int http_generate(
    const databind_compiler_projection_input *input,
    const databind_compiler_projection_request *request,
    void *context) {
  const IdlContract *contract = input != NULL ? input->contract : NULL;
  const databind_compiler_http_projection_config *config =
      request != NULL
          ? (const databind_compiler_http_projection_config *)request->config
          : NULL;
  const char *prefix =
      config != NULL && config->symbol_prefix != NULL
          ? config->symbol_prefix : "databind_generated";
  unsigned char *used_operations = NULL;
  unsigned char *used_fields = NULL;
  unsigned char *used_errors = NULL;
  char *temp = NULL;
  FILE *file = NULL;
  size_t total_operations;
  size_t operation_index = 0u;
  size_t opaque_max_bytes = 0u;
  int has_opaque = 0;
  size_t i, j, k;
  int ok = 0;
  (void)context;

  if (contract == NULL || input->binary_format == NULL ||
      request == NULL || request->output == NULL ||
      !http_config_shape_valid(config))
    return -1;
  total_operations = projection_operation_count(contract);

  for (i = 0u; config != NULL && i < config->operation_count; ++i) {
    const databind_compiler_http_operation_config *operation =
        &config->operations[i];
    if (operation->ingress_payload_kind != DATA_BIND_PAYLOAD_OPAQUE &&
        operation->egress_payload_kind != DATA_BIND_PAYLOAD_OPAQUE)
      continue;
    if (!has_opaque) {
      opaque_max_bytes = operation->opaque_max_bytes;
      has_opaque = 1;
    } else if (opaque_max_bytes != operation->opaque_max_bytes) {
      return -1;
    }
  }

  if (config != NULL && config->operation_count != 0u)
    used_operations = (unsigned char *)calloc(config->operation_count, 1u);
  if (config != NULL && config->field_count != 0u)
    used_fields = (unsigned char *)calloc(config->field_count, 1u);
  if (config != NULL && config->error_count != 0u)
    used_errors = (unsigned char *)calloc(config->error_count, 1u);
  if ((config != NULL && config->operation_count != 0u && used_operations == NULL) ||
      (config != NULL && config->field_count != 0u && used_fields == NULL) ||
      (config != NULL && config->error_count != 0u && used_errors == NULL))
    goto cleanup;

  if (projection_open_atomic(request->output, &temp, &file) != 0) goto cleanup;

  if (fprintf(file,
              "#ifndef DATABIND_GENERATED_%s_HTTP_PROJECTION_H\n"
              "#define DATABIND_GENERATED_%s_HTTP_PROJECTION_H\n\n"
              "#include <data_bind_method_plan.h>\n\n",
              prefix, prefix) < 0)
    goto cleanup;

  if (has_opaque &&
      databind_compiler_opaque_plan_emit(
          file, prefix, opaque_max_bytes) != 0)
    goto cleanup;

  for (i = 0u; i < contract->service_count; ++i) {
    const IdlService *service = &contract->services[i];
    const char *service_name = service->name;
    if (service_name == NULL) goto cleanup;
    for (j = 0u; j < service->operation_count; ++j, ++operation_index) {
      const IdlOperation *operation = &service->operations[j];
      const char *operation_name = operation->name;
      size_t fields_count = 0u, errors_count = 0u;
      if (operation_name == NULL ||
          !http_operation_config_valid(
              contract, input->binary_format, operation, config,
              service_name, operation_name))
        goto cleanup;
      if (http_emit_fields(
              file, config, prefix, service_name, operation_name,
              operation_index, &fields_count) != 0 ||
          http_emit_errors(
              file, config, prefix, service_name, operation_name,
              operation_index, &errors_count) != 0)
        goto cleanup;

      if (config != NULL) {
        size_t op_index = SIZE_MAX;
        (void)http_operation_config(
            config, service_name, operation_name, &op_index);
        if (op_index != SIZE_MAX) used_operations[op_index] = 1u;
        for (k = 0u; k < config->field_count; ++k)
          if (config_operation_matches(
                  service_name, operation_name,
                  config->fields[k].service_name,
                  config->fields[k].operation_name))
            used_fields[k] = 1u;
        for (k = 0u; k < config->error_count; ++k)
          if (config_operation_matches(
                  service_name, operation_name,
                  config->errors[k].service_name,
                  config->errors[k].operation_name))
            used_errors[k] = 1u;
      }
    }
  }

  if (fprintf(
          file,
          "static const DataBindHttpProjectionArtifactEntry %s_http_entries[%zu] = {\n",
          prefix, total_operations != 0u ? total_operations : 1u) < 0)
    goto cleanup;

  operation_index = 0u;
  for (i = 0u; i < contract->service_count; ++i) {
    const IdlService *service = &contract->services[i];
    const char *service_name = service->name;
    for (j = 0u; j < service->operation_count; ++j, ++operation_index) {
      const IdlOperation *operation = &service->operations[j];
      const char *operation_name = operation->name;
      const databind_compiler_http_operation_config *op_config =
          http_operation_config(config, service_name, operation_name, NULL);
      size_t field_count = 0u, error_count = 0u;
      for (k = 0u; config != NULL && k < config->field_count; ++k)
        if (config_operation_matches(
                service_name, operation_name,
                config->fields[k].service_name,
                config->fields[k].operation_name))
          ++field_count;
      for (k = 0u; config != NULL && k < config->error_count; ++k)
        if (config_operation_matches(
                service_name, operation_name,
                config->errors[k].service_name,
                config->errors[k].operation_name))
          ++error_count;

      if (fputs("  { sizeof(DataBindHttpProjectionArtifactEntry), ", file) == EOF ||
          projection_c_string(file, service_name) != 0 ||
          fputs(", ", file) == EOF ||
          projection_c_string(file, operation_name) != 0 ||
          fputs(", { sizeof(DataBindHttpProjectionConfig), "
                "DATA_BIND_METHOD_PLAN_ABI_VERSION, ", file) == EOF)
        goto cleanup;

      if (op_config != NULL && op_config->method != NULL) {
        if (projection_c_string(file, op_config->method) != 0) goto cleanup;
      } else if (fputs("NULL", file) == EOF) goto cleanup;
      if (fputs(", ", file) == EOF) goto cleanup;
      if (op_config != NULL && op_config->route != NULL) {
        if (projection_c_string(file, op_config->route) != 0) goto cleanup;
      } else if (fputs("NULL", file) == EOF) goto cleanup;

      if (fprintf(
              file, ", %d, UINT64_C(%llu), ",
              op_config != NULL && op_config->success_status != 0
                  ? op_config->success_status : 200,
              (unsigned long long)(
                  op_config != NULL ? op_config->context_flags : 0u)) < 0)
        goto cleanup;

      if (field_count != 0u) {
        if (fprintf(file, "%s_http_fields_%zu, %zuu, ",
                    prefix, operation_index, field_count) < 0)
          goto cleanup;
      } else if (fputs("NULL, 0u, ", file) == EOF) goto cleanup;

      if (error_count != 0u) {
        if (fprintf(file, "%s_http_errors_%zu, %zuu",
                    prefix, operation_index, error_count) < 0)
          goto cleanup;
      } else if (fputs("NULL, 0u", file) == EOF) goto cleanup;
      {
        const DataBindPayloadKind ingress_kind =
            op_config != NULL ? op_config->ingress_payload_kind
                              : DATA_BIND_PAYLOAD_FORMAT;
        const DataBindPayloadKind egress_kind =
            op_config != NULL ? op_config->egress_payload_kind
                              : DATA_BIND_PAYLOAD_FORMAT;
        const DataBindFormat ingress_format =
            op_config != NULL ? op_config->ingress_format
                              : DATA_BIND_FORMAT_JSON;
        const DataBindFormat egress_format =
            op_config != NULL ? op_config->egress_format
                              : DATA_BIND_FORMAT_JSON;
        const char *ingress_format_name =
            runtime_payload_format_name(ingress_kind, ingress_format);
        const char *egress_format_name =
            runtime_payload_format_name(egress_kind, egress_format);
        const char *ingress_kind_name =
            runtime_payload_kind_name(ingress_kind);
        const char *egress_kind_name =
            runtime_payload_kind_name(egress_kind);

        if (ingress_format_name == NULL || egress_format_name == NULL ||
            ingress_kind_name == NULL || egress_kind_name == NULL ||
            fprintf(
                file, ", %s, %s, %s, %s, ",
                ingress_format_name, egress_format_name,
                ingress_kind_name, egress_kind_name) < 0)
          goto cleanup;

        if (ingress_kind == DATA_BIND_PAYLOAD_OPAQUE) {
          if (fprintf(file, "&%s_opaque_plan, ", prefix) < 0)
            goto cleanup;
        } else if (fputs("NULL, ", file) == EOF) {
          goto cleanup;
        }

        if (egress_kind == DATA_BIND_PAYLOAD_OPAQUE) {
          if (fprintf(file, "&%s_opaque_plan", prefix) < 0)
            goto cleanup;
        } else if (fputs("NULL", file) == EOF) {
          goto cleanup;
        }
      }
      if (fputs(" } },\n", file) == EOF) goto cleanup;
    }
  }
  if (fputs("};\n\n", file) == EOF ||
      fprintf(
          file,
          "static const DataBindHttpProjectionArtifact %s_http_projection = {\n"
          "  sizeof(DataBindHttpProjectionArtifact), DATA_BIND_METHOD_PLAN_ABI_VERSION,\n"
          "  %s_http_entries, %zuu\n};\n\n"
          "#endif /* DATABIND_GENERATED_%s_HTTP_PROJECTION_H */\n",
          prefix, prefix, total_operations, prefix) < 0)
    goto cleanup;

  for (i = 0u; config != NULL && i < config->operation_count; ++i)
    if (!used_operations[i]) goto cleanup;
  for (i = 0u; config != NULL && i < config->field_count; ++i)
    if (!used_fields[i]) goto cleanup;
  for (i = 0u; config != NULL && i < config->error_count; ++i)
    if (!used_errors[i]) goto cleanup;

  ok = 1;

cleanup:
  free(used_operations);
  free(used_fields);
  free(used_errors);
  return projection_commit_atomic(request != NULL ? request->output : NULL,
                                  temp, file, ok);
}

static int rpc_generate(
    const databind_compiler_projection_input *input,
    const databind_compiler_projection_request *request,
    void *context) {
  const IdlContract *contract = input != NULL ? input->contract : NULL;
  const databind_compiler_rpc_projection_config *config =
      request != NULL
          ? (const databind_compiler_rpc_projection_config *)request->config
          : NULL;
  const char *prefix =
      config != NULL && config->symbol_prefix != NULL
          ? config->symbol_prefix : "databind_generated";
  unsigned char *used_operations = NULL;
  unsigned char *used_fields = NULL;
  unsigned char *used_errors = NULL;
  char *temp = NULL;
  FILE *file = NULL;
  size_t total_operations;
  size_t operation_index = 0u;
  size_t i, j, k;
  int ok = 0;
  (void)context;

  if (contract == NULL || input->binary_format == NULL ||
      request == NULL || request->output == NULL ||
      !rpc_config_shape_valid(config))
    return -1;
  total_operations = projection_operation_count(contract);

  if (config != NULL && config->operation_count != 0u)
    used_operations = (unsigned char *)calloc(config->operation_count, 1u);
  if (config != NULL && config->field_count != 0u)
    used_fields = (unsigned char *)calloc(config->field_count, 1u);
  if (config != NULL && config->error_count != 0u)
    used_errors = (unsigned char *)calloc(config->error_count, 1u);
  if ((config != NULL && config->operation_count != 0u && used_operations == NULL) ||
      (config != NULL && config->field_count != 0u && used_fields == NULL) ||
      (config != NULL && config->error_count != 0u && used_errors == NULL))
    goto cleanup;

  if (projection_open_atomic(request->output, &temp, &file) != 0) goto cleanup;
  if (fprintf(file,
              "#ifndef DATABIND_GENERATED_%s_RPC_PROJECTION_H\n"
              "#define DATABIND_GENERATED_%s_RPC_PROJECTION_H\n\n"
              "#include <data_bind_method_plan.h>\n\n",
              prefix, prefix) < 0)
    goto cleanup;

  for (i = 0u; i < contract->service_count; ++i) {
    const IdlService *service = &contract->services[i];
    const char *service_name = service->name;
    if (service_name == NULL) goto cleanup;
    for (j = 0u; j < service->operation_count; ++j, ++operation_index) {
      const IdlOperation *operation = &service->operations[j];
      const char *operation_name = operation->name;
      size_t fields_count = 0u, errors_count = 0u;
      if (operation_name == NULL ||
          !rpc_operation_config_valid(
              contract, input->binary_format, operation, config,
              service_name, operation_name))
        goto cleanup;
      if (rpc_emit_fields(
              file, config, prefix, service_name, operation_name,
              operation_index, &fields_count) != 0 ||
          rpc_emit_errors(
              file, config, prefix, service_name, operation_name,
              operation_index, &errors_count) != 0)
        goto cleanup;

      if (config != NULL) {
        size_t op_index = SIZE_MAX;
        (void)rpc_operation_config(
            config, service_name, operation_name, &op_index);
        if (op_index != SIZE_MAX) used_operations[op_index] = 1u;
        for (k = 0u; k < config->field_count; ++k)
          if (config_operation_matches(
                  service_name, operation_name,
                  config->fields[k].service_name,
                  config->fields[k].operation_name))
            used_fields[k] = 1u;
        for (k = 0u; k < config->error_count; ++k)
          if (config_operation_matches(
                  service_name, operation_name,
                  config->errors[k].service_name,
                  config->errors[k].operation_name))
            used_errors[k] = 1u;
      }
    }
  }

  if (fprintf(
          file,
          "static const DataBindRpcProjectionArtifactEntry %s_rpc_entries[%zu] = {\n",
          prefix, total_operations != 0u ? total_operations : 1u) < 0)
    goto cleanup;

  operation_index = 0u;
  for (i = 0u; i < contract->service_count; ++i) {
    const IdlService *service = &contract->services[i];
    const char *service_name = service->name;
    for (j = 0u; j < service->operation_count; ++j, ++operation_index) {
      const IdlOperation *operation = &service->operations[j];
      const char *operation_name = operation->name;
      const databind_compiler_rpc_operation_config *op_config =
          rpc_operation_config(config, service_name, operation_name, NULL);
      size_t field_count = 0u, error_count = 0u;
      for (k = 0u; config != NULL && k < config->field_count; ++k)
        if (config_operation_matches(
                service_name, operation_name,
                config->fields[k].service_name,
                config->fields[k].operation_name))
          ++field_count;
      for (k = 0u; config != NULL && k < config->error_count; ++k)
        if (config_operation_matches(
                service_name, operation_name,
                config->errors[k].service_name,
                config->errors[k].operation_name))
          ++error_count;

      if (fputs("  { sizeof(DataBindRpcProjectionArtifactEntry), ", file) == EOF ||
          projection_c_string(file, service_name) != 0 ||
          fputs(", ", file) == EOF ||
          projection_c_string(file, operation_name) != 0 ||
          fputs(", { sizeof(DataBindRpcProjectionConfig), "
                "DATA_BIND_METHOD_PLAN_ABI_VERSION, ", file) == EOF)
        goto cleanup;
      if (op_config != NULL && op_config->wire_method != NULL) {
        if (projection_c_string(file, op_config->wire_method) != 0) goto cleanup;
      } else if (fputs("NULL", file) == EOF) goto cleanup;
      if (fputs(", ", file) == EOF) goto cleanup;

      if (field_count != 0u) {
        if (fprintf(file, "%s_rpc_fields_%zu, %zuu, ",
                    prefix, operation_index, field_count) < 0)
          goto cleanup;
      } else if (fputs("NULL, 0u, ", file) == EOF) goto cleanup;

      if (error_count != 0u) {
        if (fprintf(file, "%s_rpc_errors_%zu, %zuu",
                    prefix, operation_index, error_count) < 0)
          goto cleanup;
      } else if (fputs("NULL, 0u", file) == EOF) goto cleanup;
      if (fprintf(
              file, ", %s, %s",
              runtime_format_name(
                  op_config != NULL ? op_config->ingress_format
                                    : DATA_BIND_FORMAT_JSON),
              runtime_format_name(
                  op_config != NULL ? op_config->egress_format
                                    : DATA_BIND_FORMAT_JSON)) < 0)
        goto cleanup;
      if (fputs(" } },\n", file) == EOF) goto cleanup;
    }
  }

  if (fputs("};\n\n", file) == EOF ||
      fprintf(
          file,
          "static const DataBindRpcProjectionArtifact %s_rpc_projection = {\n"
          "  sizeof(DataBindRpcProjectionArtifact), DATA_BIND_METHOD_PLAN_ABI_VERSION,\n"
          "  %s_rpc_entries, %zuu\n};\n\n"
          "#endif /* DATABIND_GENERATED_%s_RPC_PROJECTION_H */\n",
          prefix, prefix, total_operations, prefix) < 0)
    goto cleanup;

  for (i = 0u; config != NULL && i < config->operation_count; ++i)
    if (!used_operations[i]) goto cleanup;
  for (i = 0u; config != NULL && i < config->field_count; ++i)
    if (!used_fields[i]) goto cleanup;
  for (i = 0u; config != NULL && i < config->error_count; ++i)
    if (!used_errors[i]) goto cleanup;

  ok = 1;

cleanup:
  free(used_operations);
  free(used_fields);
  free(used_errors);
  return projection_commit_atomic(request != NULL ? request->output : NULL,
                                  temp, file, ok);
}

databind_compiler_projection_backend
databind_compiler_http_method_plan_backend(void) {
  databind_compiler_projection_backend backend = {
      {DATABIND_COMPILER_PROJECTION_AXIS_TRANSPORT,
       DATABIND_COMPILER_TRANSPORT_HTTP},
      "http", http_generate, NULL,
      DATABIND_COMPILER_OUTPUT_STAGED_SINGLE};
  return backend;
}

databind_compiler_projection_backend
databind_compiler_rpc_method_plan_backend(void) {
  databind_compiler_projection_backend backend = {
      {DATABIND_COMPILER_PROJECTION_AXIS_TRANSPORT,
       DATABIND_COMPILER_TRANSPORT_RPC},
      "rpc", rpc_generate, NULL,
      DATABIND_COMPILER_OUTPUT_STAGED_SINGLE};
  return backend;
}
