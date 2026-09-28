#include "openapi_projection.h"

#include "salts_fs.h"

#include <salts_uuid.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct openapi_operation {
  const IdlOperation *operation;
  const char *service_name;
  const char *operation_name;
  const char *request_type;
  const char *response_type;
  const databind_compiler_http_operation_config *config;
  char *route;
  const char *method;
  int success_status;
} openapi_operation;

static int openapi_json_string(FILE *file, const char *text) {
  const unsigned char *p = (const unsigned char *)text;
  if (file == NULL || text == NULL || fputc('"', file) == EOF) return -1;
  for (; *p != '\0'; ++p) {
    switch (*p) {
    case '"':
      if (fputs("\\\"", file) == EOF) return -1;
      break;
    case '\\':
      if (fputs("\\\\", file) == EOF) return -1;
      break;
    case '\b':
      if (fputs("\\b", file) == EOF) return -1;
      break;
    case '\f':
      if (fputs("\\f", file) == EOF) return -1;
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
      if (*p < 0x20u) {
        if (fprintf(file, "\\u%04x", (unsigned)*p) < 0) return -1;
      } else if (fputc((int)*p, file) == EOF) {
        return -1;
      }
      break;
    }
  }
  return fputc('"', file) == EOF ? -1 : 0;
}

static int openapi_open_atomic(
    const char *output, char **out_temp, FILE **out_file) {
  salts_uuid_t uuid;
  char uuid_text[SALTS_UUID_STRING_SIZE];
  size_t length;
  char *temp;
  FILE *file;

  if (output == NULL || output[0] == '\0' || out_temp == NULL ||
      out_file == NULL)
    return -1;
  *out_temp = NULL;
  *out_file = NULL;

  if (salts_uuid_v4_generate(&uuid) != SALTS_OK ||
      salts_uuid_format(&uuid, uuid_text, sizeof(uuid_text)) != SALTS_OK)
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

static int openapi_commit_atomic(
    const char *output, char *temp, FILE *file, int success) {
  int result = -1;
  if (file != NULL && fclose(file) != 0) success = 0;
  if (success && salts_fs_rename(temp, output) == SALTS_OK) result = 0;
  if (result != 0 && temp != NULL) (void)salts_fs_unlink(temp);
  free(temp);
  return result;
}

static int openapi_method_valid(const char *method) {
  static const char *const methods[] = {
      "GET", "HEAD", "POST", "PUT", "DELETE",
      "CONNECT", "OPTIONS", "TRACE", "PATCH"};
  size_t i;
  if (method == NULL || method[0] == '\0') return 0;
  for (i = 0u; i < sizeof(methods) / sizeof(methods[0]); ++i)
    if (strcmp(method, methods[i]) == 0) return 1;
  return 0;
}

static int openapi_operation_matches(
    const char *service, const char *operation,
    const char *candidate_service, const char *candidate_operation) {
  return service != NULL && operation != NULL &&
         candidate_service != NULL && candidate_operation != NULL &&
         strcmp(service, candidate_service) == 0 &&
         strcmp(operation, candidate_operation) == 0;
}

static const databind_compiler_http_operation_config *openapi_operation_config(
    const databind_compiler_http_projection_config *config,
    const char *service, const char *operation) {
  size_t i;
  for (i = 0u; config != NULL && i < config->operation_count; ++i)
    if (openapi_operation_matches(
            service, operation,
            config->operations[i].service_name,
            config->operations[i].operation_name))
      return &config->operations[i];
  return NULL;
}

static const databind_compiler_http_field_config *openapi_field_config(
    const databind_compiler_http_projection_config *config,
    const char *service, const char *operation,
    databind_compiler_projection_direction direction,
    const char *field_name) {
  size_t i;
  for (i = 0u; config != NULL && i < config->field_count; ++i) {
    const databind_compiler_http_field_config *field = &config->fields[i];
    if (field->direction == direction &&
        field->schema_field != NULL &&
        field_name != NULL &&
        strcmp(field->schema_field, field_name) == 0 &&
        openapi_operation_matches(
            service, operation, field->service_name, field->operation_name))
      return field;
  }
  return NULL;
}

static int openapi_error_status(
    const databind_compiler_http_projection_config *config,
    const char *service, const char *operation,
    const char *error_type) {
  size_t i;
  for (i = 0u; config != NULL && i < config->error_count; ++i) {
    const databind_compiler_http_error_config *error = &config->errors[i];
    if (error->error_type != NULL && error_type != NULL &&
        strcmp(error->error_type, error_type) == 0 &&
        openapi_operation_matches(
            service, operation, error->service_name, error->operation_name))
      return error->status;
  }
  return 500;
}


static const IdlField *openapi_field(
    const IdlContract *contract,
    const char *type_name, const char *field_name) {
  const IdlDataDecl *record;
  size_t i;
  if (contract == NULL || type_name == NULL || field_name == NULL)
    return NULL;
  record = idl_contract_find_data(contract, type_name);
  if (record == NULL || record->fields == NULL) return NULL;
  for (i = 0u; i < record->field_count; ++i)
    if (record->fields[i].name != NULL &&
        strcmp(record->fields[i].name, field_name) == 0)
      return &record->fields[i];
  return NULL;
}

static int openapi_operation_has_error(
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

static int openapi_route_placeholder_count(
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


static int openapi_http_config_valid(
    const IdlContract *contract,
    const databind_compiler_http_projection_config *config,
    const openapi_operation *operations,
    size_t operation_count) {
  size_t i, j;

  if (contract == NULL) return 0;
  if (config == NULL) return 1;
  if ((config->operation_count != 0u && config->operations == NULL) ||
      (config->field_count != 0u && config->fields == NULL) ||
      (config->error_count != 0u && config->errors == NULL))
    return 0;

  for (i = 0u; i < config->operation_count; ++i) {
    const databind_compiler_http_operation_config *item = &config->operations[i];
    int found = 0;
    if (item->service_name == NULL || item->operation_name == NULL ||
        (item->method != NULL && !openapi_method_valid(item->method)) ||
        item->ingress_format != DATA_BIND_FORMAT_JSON ||
        item->egress_format != DATA_BIND_FORMAT_JSON ||
        (item->route != NULL &&
         (item->route[0] != '/' || strchr(item->route, '?') != NULL ||
          strchr(item->route, '#') != NULL)) ||
        (item->success_status != 0 &&
         (item->success_status < 100 || item->success_status > 599)))
      return 0;
    for (j = 0u; j < operation_count; ++j)
      if (openapi_operation_matches(
              operations[j].service_name, operations[j].operation_name,
              item->service_name, item->operation_name)) {
        found = 1;
        break;
      }
    if (!found) return 0;
  }

  for (i = 0u; i < config->field_count; ++i) {
    const databind_compiler_http_field_config *field = &config->fields[i];
    const openapi_operation *operation = NULL;
    const char *type_name;
    const IdlField *schema_field;
    const char *wire;
    if (field->service_name == NULL || field->operation_name == NULL ||
        field->schema_field == NULL ||
        field->wire_name == NULL || field->wire_name[0] == '\0')
      return 0;
    for (j = 0u; j < operation_count; ++j)
      if (openapi_operation_matches(
              operations[j].service_name, operations[j].operation_name,
              field->service_name, field->operation_name)) {
        operation = &operations[j];
        break;
      }
    if (operation == NULL) return 0;
    if (field->direction == DATABIND_COMPILER_PROJECTION_INGRESS) {
      if (field->location < DATABIND_COMPILER_HTTP_PATH ||
          field->location > DATABIND_COMPILER_HTTP_BODY)
        return 0;
      type_name = operation->request_type;
    } else if (field->direction == DATABIND_COMPILER_PROJECTION_EGRESS) {
      if (field->location != DATABIND_COMPILER_HTTP_RESPONSE_HEADER &&
          field->location != DATABIND_COMPILER_HTTP_RESPONSE_BODY)
        return 0;
      type_name = operation->response_type;
    } else {
      return 0;
    }
    if (type_name == NULL || strcmp(type_name, "void") == 0)
      return 0;
    schema_field = openapi_field(
        contract, type_name, field->schema_field);
    if (schema_field == NULL) return 0;

    if (field->location == DATABIND_COMPILER_HTTP_PATH) {
      wire = field->wire_name != NULL ? field->wire_name : field->schema_field;
      if (schema_field->optional || schema_field->nullable ||
          openapi_route_placeholder_count(operation->route, wire) != 1)
        return 0;
    }
  }

  for (i = 0u; i < config->error_count; ++i) {
    const databind_compiler_http_error_config *error = &config->errors[i];
    const openapi_operation *operation = NULL;
    if (error->service_name == NULL || error->operation_name == NULL ||
        error->error_type == NULL || error->status < 100 || error->status > 599)
      return 0;
    for (j = 0u; j < operation_count; ++j)
      if (openapi_operation_matches(
              operations[j].service_name, operations[j].operation_name,
              error->service_name, error->operation_name)) {
        operation = &operations[j];
        break;
      }
    if (operation == NULL ||
        !openapi_operation_has_error(operation->operation, error->error_type))
      return 0;
  }

  return 1;
}

static char *openapi_default_route(
    const char *service, const char *operation) {
  size_t a, b;
  char *out;
  if (service == NULL || operation == NULL) return NULL;
  a = strlen(service);
  b = strlen(operation);
  if (a > SIZE_MAX - b - 3u) return NULL;
  out = (char *)malloc(a + b + 3u);
  if (out == NULL) return NULL;
  out[0] = '/';
  memcpy(out + 1u, service, a);
  out[a + 1u] = '/';
  memcpy(out + a + 2u, operation, b + 1u);
  return out;
}

static void openapi_operations_free(
    openapi_operation *operations, size_t count) {
  size_t i;
  if (operations == NULL) return;
  for (i = 0u; i < count; ++i) free(operations[i].route);
  free(operations);
}

static int openapi_operations_build(
    const IdlContract *contract,
    const databind_compiler_http_projection_config *config,
    openapi_operation **out_operations,
    size_t *out_count) {
  openapi_operation *result = NULL;
  size_t count = 0u, index = 0u;
  size_t i, j, k;

  if (contract == NULL || out_operations == NULL || out_count == NULL)
    return -1;
  *out_operations = NULL;
  *out_count = 0u;

  for (i = 0u; i < contract->service_count; ++i)
    count += contract->services[i].operation_count;

  if (count != 0u) {
    result = (openapi_operation *)calloc(count, sizeof(*result));
    if (result == NULL) return -1;
  }

  for (i = 0u; i < contract->service_count; ++i) {
    const IdlService *service = &contract->services[i];
    const char *service_name = service->name;
    if (service_name == NULL) goto fail;

    for (j = 0u; j < service->operation_count; ++j, ++index) {
      const IdlOperation *operation = &service->operations[j];
      const char *operation_name = operation->name;
      const databind_compiler_http_operation_config *op_config;
      const char *configured_route;
      if (operation_name == NULL) goto fail;

      op_config = openapi_operation_config(
          config, service_name, operation_name);
      result[index].operation = operation;
      result[index].service_name = service_name;
      result[index].operation_name = operation_name;
      result[index].request_type = operation->request_type;
      result[index].response_type = operation->response_type;
      result[index].config = op_config;
      result[index].method =
          op_config != NULL && op_config->method != NULL
              ? op_config->method : "POST";
      result[index].success_status =
          op_config != NULL && op_config->success_status != 0
              ? op_config->success_status : 200;

      if (!openapi_method_valid(result[index].method) ||
          result[index].request_type == NULL ||
          result[index].response_type == NULL)
        goto fail;

      if (op_config != NULL &&
          (op_config->ingress_format != DATA_BIND_FORMAT_JSON ||
           op_config->egress_format != DATA_BIND_FORMAT_JSON))
        goto fail;

      configured_route =
          op_config != NULL ? op_config->route : NULL;
      result[index].route =
          configured_route != NULL
              ? openapi_strdup(configured_route)
              : openapi_default_route(service_name, operation_name);
      if (result[index].route == NULL ||
          result[index].route[0] != '/' ||
          strchr(result[index].route, '?') != NULL ||
          strchr(result[index].route, '#') != NULL)
        goto fail;
    }
  }

  if (!openapi_http_config_valid(contract, config, result, count))
    goto fail;

  for (i = 0u; i < count; ++i)
    for (j = 0u; j < i; ++j)
      if (strcmp(result[i].route, result[j].route) == 0 &&
          strcmp(result[i].method, result[j].method) == 0)
        goto fail;

  for (i = 0u; config != NULL && i < config->field_count; ++i) {
    const databind_compiler_http_field_config *field = &config->fields[i];
    if (field->location != DATABIND_COMPILER_HTTP_PATH) continue;
    for (j = 0u; j < count; ++j) {
      if (!openapi_operation_matches(
              result[j].service_name, result[j].operation_name,
              field->service_name, field->operation_name))
        continue;
      for (k = 0u; k < config->field_count; ++k) {
        const databind_compiler_http_field_config *other = &config->fields[k];
        const char *left;
        const char *right;
        if (k == i ||
            other->location != DATABIND_COMPILER_HTTP_PATH ||
            other->direction != DATABIND_COMPILER_PROJECTION_INGRESS ||
            !openapi_operation_matches(
                field->service_name, field->operation_name,
                other->service_name, other->operation_name))
          continue;
        left = field->wire_name != NULL ? field->wire_name : field->schema_field;
        right = other->wire_name != NULL ? other->wire_name : other->schema_field;
        if (strcmp(left, right) == 0) goto fail;
      }
    }
  }

  *out_operations = result;
  *out_count = count;
  return 0;

fail:
  openapi_operations_free(result, count);
  return -1;
}

static int openapi_primitive(
    const char *type,
    const char **json_type,
    const char **format,
    int *is_unsigned) {
  if (json_type == NULL || format == NULL || is_unsigned == NULL)
    return 0;
  *json_type = NULL;
  *format = NULL;
  *is_unsigned = 0;
  if (type == NULL) return 0;

  if (strcmp(type, "bool") == 0) {
    *json_type = "boolean";
    return 1;
  }

#define OPENAPI_INT32(T) \
  if (strcmp(type, T) == 0) { *json_type = "integer"; *format = "int32"; return 1; }
#define OPENAPI_UINT32(T) \
  if (strcmp(type, T) == 0) { *json_type = "integer"; *format = "int32"; *is_unsigned = 1; return 1; }
#define OPENAPI_INT64(T) \
  if (strcmp(type, T) == 0) { *json_type = "integer"; *format = "int64"; return 1; }
#define OPENAPI_UINT64(T) \
  if (strcmp(type, T) == 0) { *json_type = "integer"; *format = "int64"; *is_unsigned = 1; return 1; }

  OPENAPI_INT32("int8")
  OPENAPI_INT32("i8")
  OPENAPI_INT32("int8_t")
  OPENAPI_UINT32("uint8")
  OPENAPI_UINT32("u8")
  OPENAPI_UINT32("uint8_t")
  OPENAPI_INT32("int16")
  OPENAPI_INT32("i16")
  OPENAPI_INT32("int16_t")
  OPENAPI_UINT32("uint16")
  OPENAPI_UINT32("u16")
  OPENAPI_UINT32("uint16_t")
  OPENAPI_INT32("int32")
  OPENAPI_INT32("i32")
  OPENAPI_INT32("int32_t")
  OPENAPI_UINT32("uint32")
  OPENAPI_UINT32("u32")
  OPENAPI_UINT32("uint32_t")
  OPENAPI_INT64("int64")
  OPENAPI_INT64("i64")
  OPENAPI_INT64("int64_t")
  OPENAPI_UINT64("uint64")
  OPENAPI_UINT64("u64")
  OPENAPI_UINT64("uint64_t")
#undef OPENAPI_INT32
#undef OPENAPI_UINT32
#undef OPENAPI_INT64
#undef OPENAPI_UINT64

  if (strcmp(type, "float") == 0 ||
      strcmp(type, "float32") == 0 ||
      strcmp(type, "f32") == 0) {
    *json_type = "number";
    *format = "float";
    return 1;
  }
  if (strcmp(type, "double") == 0 ||
      strcmp(type, "float64") == 0 ||
      strcmp(type, "f64") == 0) {
    *json_type = "number";
    *format = "double";
    return 1;
  }
  if (strcmp(type, "string") == 0) {
    *json_type = "string";
    return 1;
  }
  if (strcmp(type, "bytes") == 0) {
    *json_type = "string";
    *format = "byte";
    return 1;
  }
  if (strcmp(type, "uuid") == 0) {
    *json_type = "string";
    *format = "uuid";
    return 1;
  }
  if (strcmp(type, "datetime") == 0) {
    *json_type = "string";
    *format = "date-time";
    return 1;
  }
  if (strcmp(type, "date") == 0) {
    *json_type = "string";
    *format = "date";
    return 1;
  }
  if (strcmp(type, "time") == 0) {
    *json_type = "string";
    *format = "time";
    return 1;
  }
  if (strcmp(type, "duration") == 0) {
    *json_type = "string";
    *format = "duration";
    return 1;
  }
  return 0;
}

static int openapi_emit_comma(FILE *file, int *first) {
  if (file == NULL || first == NULL) return -1;
  if (!*first && fputc(',', file) == EOF) return -1;
  *first = 0;
  return 0;
}

static int openapi_emit_key(FILE *file, int *first, const char *key) {
  return openapi_emit_comma(file, first) == 0 &&
                 openapi_json_string(file, key) == 0 &&
                 fputc(':', file) != EOF
             ? 0 : -1;
}

static int openapi_emit_ref(FILE *file, const char *name) {
  char ref[512];
  int written;
  written = snprintf(ref, sizeof(ref), "#/components/schemas/%s", name);
  if (written <= 0 || (size_t)written >= sizeof(ref)) return -1;
  return fputs("{\"$ref\":", file) == EOF ||
                 openapi_json_string(file, ref) != 0 ||
                 fputc('}', file) == EOF
             ? -1 : 0;
}

static int openapi_emit_type_value(
    FILE *file, const char *json_type, int nullable) {
  if (nullable) {
    if (fputs("[", file) == EOF ||
        openapi_json_string(file, json_type) != 0 ||
        fputc(',', file) == EOF ||
        openapi_json_string(file, "null") != 0 ||
        fputc(']', file) == EOF)
      return -1;
    return 0;
  }
  return openapi_json_string(file, json_type);
}

static int openapi_constraint_kind_present(
    const IdlField *field, const char *kind) {
  size_t i;
  if (field == NULL || kind == NULL || field->constraints == NULL)
    return 0;
  for (i = 0u; i < field->constraint_count; ++i)
    if (field->constraints[i].kind != NULL &&
        strcmp(field->constraints[i].kind, kind) == 0)
      return 1;
  return 0;
}


static int openapi_emit_default(
    FILE *file, const IdlField *field, const char *type,
    int *first) {
  const char *value;
  const char *json_type;
  const char *format;
  int is_unsigned;

  if (field == NULL || field->default_value == NULL) return 0;
  value = field->default_value;
  if (openapi_emit_key(file, first, "default") != 0) return -1;

  if (strcmp(value, "null") == 0 && field->nullable)
    return fputs("null", file) == EOF ? -1 : 0;

  if (!openapi_primitive(type, &json_type, &format, &is_unsigned))
    return -1;

  if (strcmp(json_type, "string") == 0)
    return openapi_json_string(file, value);
  if (strcmp(json_type, "boolean") == 0) {
    if (strcmp(value, "true") != 0 && strcmp(value, "false") != 0)
      return -1;
    return fputs(value, file) == EOF ? -1 : 0;
  }
  return fputs(value, file) == EOF ? -1 : 0;
}

static int openapi_emit_constraints(
    FILE *file, const IdlField *field,
    const char *type, int is_collection, int is_map, int *first) {
  size_t i;
  const char *json_type = NULL;
  const char *format = NULL;
  int is_unsigned = 0;

  if (field == NULL) return -1;
  (void)openapi_primitive(type, &json_type, &format, &is_unsigned);

  if (is_unsigned && !openapi_constraint_kind_present(field, "min")) {
    if (openapi_emit_key(file, first, "minimum") != 0 ||
        fputs("0", file) == EOF)
      return -1;
  }

  for (i = 0u; i < field->constraint_count; ++i) {
    const IdlConstraint *constraint = &field->constraints[i];
    const char *kind = constraint->kind;
    if (kind == NULL) return -1;

    if (strcmp(kind, "min") == 0 || strcmp(kind, "max") == 0) {
      const char *value = constraint->value;
      const char *key = strcmp(kind, "min") == 0 ? "minimum" : "maximum";
      if (json_type == NULL ||
          (strcmp(json_type, "integer") != 0 &&
           strcmp(json_type, "number") != 0) ||
          value == NULL ||
          openapi_emit_key(file, first, key) != 0 ||
          fputs(value, file) == EOF)
        return -1;
    } else if (strcmp(kind, "pattern") == 0) {
      const char *pattern = constraint->pattern;
      if (json_type == NULL || strcmp(json_type, "string") != 0 ||
          strcmp(type, "bytes") == 0 || pattern == NULL ||
          openapi_emit_key(file, first, "pattern") != 0 ||
          openapi_json_string(file, pattern) != 0)
        return -1;
    } else if (strcmp(kind, "size") == 0) {
      const char *minimum = constraint->minimum;
      const char *maximum = constraint->maximum;
      const char *min_key = NULL;
      const char *max_key = NULL;

      if (is_map) {
        min_key = "minProperties";
        max_key = "maxProperties";
      } else if (is_collection) {
        min_key = "minItems";
        max_key = "maxItems";
      } else if (json_type != NULL && strcmp(json_type, "string") == 0 &&
                 strcmp(type, "bytes") != 0) {
        min_key = "minLength";
        max_key = "maxLength";
      } else {
        return -1;
      }

      if (minimum != NULL &&
          (openapi_emit_key(file, first, min_key) != 0 ||
           fputs(minimum, file) == EOF))
        return -1;
      if (maximum != NULL &&
          (openapi_emit_key(file, first, max_key) != 0 ||
           fputs(maximum, file) == EOF))
        return -1;
    } else {
      return -1;
    }
  }
  return 0;
}


static int openapi_emit_type_schema(
    FILE *file, const IdlContract *contract,
    const char *type, int nullable) {
  const char *json_type = NULL;
  const char *format = NULL;
  const IdlDataDecl *data;
  int is_unsigned = 0;
  int first = 1;

  if (file == NULL || contract == NULL || type == NULL) return -1;
  if (fputc('{', file) == EOF) return -1;

  if (openapi_primitive(type, &json_type, &format, &is_unsigned)) {
    if (openapi_emit_key(file, &first, "type") != 0 ||
        openapi_emit_type_value(file, json_type, nullable) != 0)
      return -1;
    if (format != NULL &&
        (openapi_emit_key(file, &first, "format") != 0 ||
         openapi_json_string(file, format) != 0))
      return -1;
    if (strcmp(type, "bytes") == 0 &&
        (openapi_emit_key(file, &first, "contentEncoding") != 0 ||
         openapi_json_string(file, "base64") != 0))
      return -1;
    if (is_unsigned &&
        (openapi_emit_key(file, &first, "minimum") != 0 ||
         fputs("0", file) == EOF))
      return -1;
  } else {
    data = idl_contract_find_data(contract, type);
    if (data == NULL) return -1;
    if (data->kind == IDL_DATA_ENUM) {
      size_t i;
      if (!openapi_primitive(
              data->underlying_type != NULL ? data->underlying_type : "int32",
              &json_type, &format, &is_unsigned) ||
          json_type == NULL || strcmp(json_type, "integer") != 0 ||
          openapi_emit_key(file, &first, "type") != 0 ||
          openapi_emit_type_value(file, "integer", nullable) != 0)
        return -1;
      if (format != NULL &&
          (openapi_emit_key(file, &first, "format") != 0 ||
           openapi_json_string(file, format) != 0))
        return -1;
      if (is_unsigned &&
          (openapi_emit_key(file, &first, "minimum") != 0 ||
           fputs("0", file) == EOF))
        return -1;
      if (openapi_emit_key(file, &first, "enum") != 0 ||
          fputc('[', file) == EOF)
        return -1;
      for (i = 0u; i < data->enum_item_count; ++i) {
        const char *value = data->enum_items[i].value;
        if (value == NULL || (i != 0u && fputc(',', file) == EOF) ||
            fputs(value, file) == EOF)
          return -1;
      }
      if (fputc(']', file) == EOF) return -1;
    } else if (data->kind == IDL_DATA_MESSAGE ||
               data->kind == IDL_DATA_COMPOSITE ||
               data->kind == IDL_DATA_GROUP) {
      if (nullable) {
        if (openapi_emit_key(file, &first, "anyOf") != 0 ||
            fputs("[", file) == EOF ||
            openapi_emit_ref(file, type) != 0 ||
            fputs(",{\"type\":\"null\"}]", file) == EOF)
          return -1;
      } else {
        char ref[512];
        int written =
            snprintf(ref, sizeof(ref), "#/components/schemas/%s", type);
        if (written <= 0 || (size_t)written >= sizeof(ref) ||
            openapi_emit_key(file, &first, "$ref") != 0 ||
            openapi_json_string(file, ref) != 0)
          return -1;
      }
    } else {
      return -1;
    }
  }
  return fputc('}', file) == EOF ? -1 : 0;
}

static int openapi_emit_field_schema(
    FILE *file, const IdlContract *contract,
    const IdlField *field) {
  const char *type;
  const char *json_type = NULL;
  const char *format = NULL;
  const IdlDataDecl *data;
  int is_unsigned = 0;
  int nullable;
  int is_map;
  int is_set;
  int is_collection;
  int first = 1;

  if (file == NULL || contract == NULL || field == NULL ||
      field->type_name == NULL)
    return -1;

  type = field->type_name;
  nullable = field->nullable;
  is_map = field->collection_kind == IDL_COLLECTION_MAP;
  is_set = field->collection_kind == IDL_COLLECTION_SET;
  is_collection = field->collection_kind != IDL_COLLECTION_NONE;

  if (fputc('{', file) == EOF) return -1;

  if (is_map) {
    if (field->key_type == NULL || strcmp(field->key_type, "string") != 0 ||
        field->value_type == NULL ||
        openapi_emit_key(file, &first, "type") != 0 ||
        openapi_emit_type_value(file, "object", nullable) != 0 ||
        openapi_emit_key(file, &first, "additionalProperties") != 0 ||
        openapi_emit_type_schema(file, contract, field->value_type, 0) != 0)
      return -1;
  } else if (is_collection) {
    if (field->inner_type == NULL ||
        openapi_emit_key(file, &first, "type") != 0 ||
        openapi_emit_type_value(file, "array", nullable) != 0 ||
        (is_set &&
         (openapi_emit_key(file, &first, "uniqueItems") != 0 ||
          fputs("true", file) == EOF)) ||
        openapi_emit_key(file, &first, "items") != 0 ||
        openapi_emit_type_schema(file, contract, field->inner_type, 0) != 0)
      return -1;
  } else if (openapi_primitive(type, &json_type, &format, &is_unsigned)) {
    if (openapi_emit_key(file, &first, "type") != 0 ||
        openapi_emit_type_value(file, json_type, nullable) != 0)
      return -1;
    if (format != NULL) {
      if (openapi_emit_key(file, &first, "format") != 0 ||
          openapi_json_string(file, format) != 0)
        return -1;
      if (strcmp(type, "bytes") == 0 &&
          (openapi_emit_key(file, &first, "contentEncoding") != 0 ||
           openapi_json_string(file, "base64") != 0))
        return -1;
    }
  } else {
    data = idl_contract_find_data(contract, type);
    if (data == NULL) return -1;
    if (data->kind == IDL_DATA_ENUM) {
      size_t i;
      const char *underlying =
          data->underlying_type != NULL ? data->underlying_type : "int32";
      if (!openapi_primitive(
              underlying, &json_type, &format, &is_unsigned) ||
          json_type == NULL || strcmp(json_type, "integer") != 0 ||
          openapi_emit_key(file, &first, "type") != 0 ||
          openapi_emit_type_value(file, "integer", nullable) != 0)
        return -1;
      if (format != NULL &&
          (openapi_emit_key(file, &first, "format") != 0 ||
           openapi_json_string(file, format) != 0))
        return -1;
      if (is_unsigned &&
          (openapi_emit_key(file, &first, "minimum") != 0 ||
           fputs("0", file) == EOF))
        return -1;
      if (openapi_emit_key(file, &first, "enum") != 0 ||
          fputc('[', file) == EOF)
        return -1;
      for (i = 0u; i < data->enum_item_count; ++i) {
        const char *value = data->enum_items[i].value;
        if (value == NULL || (i != 0u && fputc(',', file) == EOF) ||
            fputs(value, file) == EOF)
          return -1;
      }
      if (fputc(']', file) == EOF) return -1;
    } else if (data->kind == IDL_DATA_MESSAGE ||
               data->kind == IDL_DATA_COMPOSITE ||
               data->kind == IDL_DATA_GROUP) {
      if (nullable) {
        if (openapi_emit_key(file, &first, "anyOf") != 0 ||
            fputs("[", file) == EOF ||
            openapi_emit_ref(file, type) != 0 ||
            fputs(",{\"type\":\"null\"}]", file) == EOF)
          return -1;
      } else {
        char ref[512];
        int written =
            snprintf(ref, sizeof(ref), "#/components/schemas/%s", type);
        if (written <= 0 || (size_t)written >= sizeof(ref) ||
            openapi_emit_key(file, &first, "$ref") != 0 ||
            openapi_json_string(file, ref) != 0)
          return -1;
      }
    } else {
      return -1;
    }
  }

  if (openapi_emit_constraints(
          file, field, type, is_collection && !is_map, is_map, &first) != 0 ||
      openapi_emit_default(file, field, type, &first) != 0 ||
      fputc('}', file) == EOF)
    return -1;
  return 0;
}


static int openapi_emit_record_schema(
    FILE *file, const IdlContract *contract,
    const IdlDataDecl *record) {
  size_t i;
  int first_required = 1;

  if (file == NULL || contract == NULL || record == NULL ||
      (record->field_count != 0u && record->fields == NULL))
    return -1;
  if (fputs("{\"type\":\"object\",\"properties\":{", file) == EOF)
    return -1;

  for (i = 0u; i < record->field_count; ++i) {
    const IdlField *field = &record->fields[i];
    if (field->name == NULL ||
        (i != 0u && fputc(',', file) == EOF) ||
        openapi_json_string(file, field->name) != 0 ||
        fputc(':', file) == EOF ||
        openapi_emit_field_schema(file, contract, field) != 0)
      return -1;
  }

  if (fputc('}', file) == EOF) return -1;

  for (i = 0u; i < record->field_count; ++i) {
    const IdlField *field = &record->fields[i];
    if (!field->optional) {
      if (first_required) {
        if (fputs(",\"required\":[", file) == EOF) return -1;
        first_required = 0;
      } else if (fputc(',', file) == EOF) {
        return -1;
      }
      if (field->name == NULL ||
          openapi_json_string(file, field->name) != 0)
        return -1;
    }
  }
  if (!first_required && fputc(']', file) == EOF) return -1;

  return fputc('}', file) == EOF ? -1 : 0;
}


static int openapi_field_is_body(
    const databind_compiler_http_projection_config *config,
    const openapi_operation *operation,
    databind_compiler_projection_direction direction,
    const IdlField *field) {
  const databind_compiler_http_field_config *mapping;
  if (field == NULL) return 0;
  mapping = openapi_field_config(
      config, operation->service_name, operation->operation_name,
      direction, field->name);
  if (mapping == NULL) return 1;
  if (direction == DATABIND_COMPILER_PROJECTION_INGRESS)
    return mapping->location == DATABIND_COMPILER_HTTP_BODY;
  return mapping->location == DATABIND_COMPILER_HTTP_RESPONSE_BODY;
}


static int openapi_body_field_count(
    const IdlContract *contract,
    const databind_compiler_http_projection_config *config,
    const openapi_operation *operation,
    databind_compiler_projection_direction direction,
    int *out_required) {
  const char *type_name =
      direction == DATABIND_COMPILER_PROJECTION_INGRESS
          ? operation->request_type : operation->response_type;
  const IdlDataDecl *record;
  size_t i;
  int count = 0;
  int required = 0;

  if (out_required != NULL) *out_required = 0;
  if (contract == NULL || type_name == NULL ||
      strcmp(type_name, "void") == 0)
    return 0;
  record = idl_contract_find_data(contract, type_name);
  if (record == NULL || (record->field_count != 0u && record->fields == NULL))
    return -1;
  for (i = 0u; i < record->field_count; ++i) {
    const IdlField *field = &record->fields[i];
    if (openapi_field_is_body(config, operation, direction, field)) {
      ++count;
      if (!field->optional) required = 1;
    }
  }
  if (out_required != NULL) *out_required = required;
  return count;
}


static int openapi_emit_body_schema(
    FILE *file, const IdlContract *contract,
    const databind_compiler_http_projection_config *config,
    const openapi_operation *operation,
    databind_compiler_projection_direction direction) {
  const char *type_name =
      direction == DATABIND_COMPILER_PROJECTION_INGRESS
          ? operation->request_type : operation->response_type;
  const IdlDataDecl *record =
      idl_contract_find_data(contract, type_name);
  size_t i;
  int first_property = 1;
  int first_required = 1;
  int body_count = openapi_body_field_count(
      contract, config, operation, direction, NULL);

  if (body_count < 0 || record == NULL ||
      (record->field_count != 0u && record->fields == NULL))
    return -1;
  if ((size_t)body_count == record->field_count)
    return openapi_emit_ref(file, type_name);

  if (fputs("{\"type\":\"object\",\"properties\":{", file) == EOF)
    return -1;
  for (i = 0u; i < record->field_count; ++i) {
    const IdlField *field = &record->fields[i];
    if (!openapi_field_is_body(config, operation, direction, field))
      continue;
    if (field->name == NULL ||
        openapi_emit_comma(file, &first_property) != 0 ||
        openapi_json_string(file, field->name) != 0 ||
        fputc(':', file) == EOF ||
        openapi_emit_field_schema(file, contract, field) != 0)
      return -1;
  }
  if (fputc('}', file) == EOF) return -1;

  for (i = 0u; i < record->field_count; ++i) {
    const IdlField *field = &record->fields[i];
    if (!openapi_field_is_body(config, operation, direction, field) ||
        field->optional)
      continue;
    if (first_required) {
      if (fputs(",\"required\":[", file) == EOF) return -1;
      first_required = 0;
    } else if (fputc(',', file) == EOF) {
      return -1;
    }
    if (field->name == NULL ||
        openapi_json_string(file, field->name) != 0)
      return -1;
  }
  if (!first_required && fputc(']', file) == EOF) return -1;
  return fputc('}', file) == EOF ? -1 : 0;
}

static const char *openapi_parameter_location(
    databind_compiler_http_field_location location) {
  switch (location) {
  case DATABIND_COMPILER_HTTP_PATH:
    return "path";
  case DATABIND_COMPILER_HTTP_QUERY:
    return "query";
  case DATABIND_COMPILER_HTTP_HEADER:
    return "header";
  case DATABIND_COMPILER_HTTP_COOKIE:
    return "cookie";
  default:
    return NULL;
  }
}


static int openapi_emit_parameters(
    FILE *file, const IdlContract *contract,
    const databind_compiler_http_projection_config *config,
    const openapi_operation *operation,
    int *operation_first) {
  const IdlDataDecl *record;
  size_t i;
  size_t count = 0u;

  if (operation->request_type == NULL ||
      strcmp(operation->request_type, "void") == 0)
    return 0;
  record = idl_contract_find_data(contract, operation->request_type);
  if (record == NULL || operation_first == NULL ||
      (record->field_count != 0u && record->fields == NULL))
    return -1;

  for (i = 0u; i < record->field_count; ++i) {
    const IdlField *field = &record->fields[i];
    const databind_compiler_http_field_config *mapping =
        openapi_field_config(
            config, operation->service_name, operation->operation_name,
            DATABIND_COMPILER_PROJECTION_INGRESS, field->name);
    if (mapping != NULL &&
        mapping->location != DATABIND_COMPILER_HTTP_BODY)
      ++count;
  }
  if (count == 0u) return 0;

  if (openapi_emit_key(file, operation_first, "parameters") != 0 ||
      fputc('[', file) == EOF)
    return -1;

  count = 0u;
  for (i = 0u; i < record->field_count; ++i) {
    const IdlField *field = &record->fields[i];
    const databind_compiler_http_field_config *mapping =
        openapi_field_config(
            config, operation->service_name, operation->operation_name,
            DATABIND_COMPILER_PROJECTION_INGRESS, field->name);
    const char *location;
    const char *wire;
    int required;

    if (mapping == NULL ||
        mapping->location == DATABIND_COMPILER_HTTP_BODY)
      continue;

    location = openapi_parameter_location(mapping->location);
    if (location == NULL || field->name == NULL) return -1;
    wire = mapping->wire_name != NULL ? mapping->wire_name : field->name;
    required = mapping->location == DATABIND_COMPILER_HTTP_PATH ||
               !field->optional;

    if (count++ != 0u && fputc(',', file) == EOF) return -1;
    if (fputs("{\"name\":", file) == EOF ||
        openapi_json_string(file, wire) != 0 ||
        fputs(",\"in\":", file) == EOF ||
        openapi_json_string(file, location) != 0 ||
        fprintf(file, ",\"required\":%s,\"schema\":",
                required ? "true" : "false") < 0 ||
        openapi_emit_field_schema(file, contract, field) != 0 ||
        fputc('}', file) == EOF)
      return -1;
  }

  return fputc(']', file) == EOF ? -1 : 0;
}


static int openapi_emit_response_headers(
    FILE *file, const IdlContract *contract,
    const databind_compiler_http_projection_config *config,
    const openapi_operation *operation,
    int *response_first) {
  const IdlDataDecl *record;
  size_t i;
  size_t count = 0u;

  if (operation->response_type == NULL ||
      strcmp(operation->response_type, "void") == 0)
    return 0;
  if (response_first == NULL) return -1;
  record = idl_contract_find_data(contract, operation->response_type);
  if (record == NULL ||
      (record->field_count != 0u && record->fields == NULL))
    return -1;

  for (i = 0u; i < record->field_count; ++i) {
    const IdlField *field = &record->fields[i];
    const databind_compiler_http_field_config *mapping =
        openapi_field_config(
            config, operation->service_name, operation->operation_name,
            DATABIND_COMPILER_PROJECTION_EGRESS, field->name);
    if (mapping != NULL &&
        mapping->location == DATABIND_COMPILER_HTTP_RESPONSE_HEADER)
      ++count;
  }
  if (count == 0u) return 0;

  if (openapi_emit_key(file, response_first, "headers") != 0 ||
      fputc('{', file) == EOF)
    return -1;

  count = 0u;
  for (i = 0u; i < record->field_count; ++i) {
    const IdlField *field = &record->fields[i];
    const databind_compiler_http_field_config *mapping =
        openapi_field_config(
            config, operation->service_name, operation->operation_name,
            DATABIND_COMPILER_PROJECTION_EGRESS, field->name);
    const char *wire;
    if (mapping == NULL ||
        mapping->location != DATABIND_COMPILER_HTTP_RESPONSE_HEADER)
      continue;
    if (field->name == NULL) return -1;
    wire = mapping->wire_name != NULL ? mapping->wire_name : field->name;

    if (count++ != 0u && fputc(',', file) == EOF) return -1;
    if (openapi_json_string(file, wire) != 0 ||
        fputs(":{\"schema\":", file) == EOF ||
        openapi_emit_field_schema(file, contract, field) != 0 ||
        fputc('}', file) == EOF)
      return -1;
  }

  return fputc('}', file) == EOF ? -1 : 0;
}


static int openapi_emit_error_schema_for_status(
    FILE *file, const IdlContract *contract,
    const databind_compiler_http_projection_config *config,
    const openapi_operation *operation, int status) {
  size_t i;
  size_t count = 0u;
  const IdlOperation *idl_operation =
      operation != NULL ? operation->operation : NULL;

  (void)contract;
  if (idl_operation == NULL) return -1;

  for (i = 0u; i < idl_operation->error_count; ++i) {
    const char *error_type = idl_operation->error_types[i];
    if (error_type != NULL &&
        openapi_error_status(
            config, operation->service_name,
            operation->operation_name, error_type) == status)
      ++count;
  }

  if (count == 0u) return -1;
  if (count == 1u) {
    for (i = 0u; i < idl_operation->error_count; ++i) {
      const char *error_type = idl_operation->error_types[i];
      if (error_type != NULL &&
          openapi_error_status(
              config, operation->service_name,
              operation->operation_name, error_type) == status)
        return openapi_emit_ref(file, error_type);
    }
    return -1;
  }

  if (fputs("{\"oneOf\":[", file) == EOF) return -1;
  count = 0u;
  for (i = 0u; i < idl_operation->error_count; ++i) {
    const char *error_type = idl_operation->error_types[i];
    if (error_type == NULL ||
        openapi_error_status(
            config, operation->service_name,
            operation->operation_name, error_type) != status)
      continue;
    if (count++ != 0u && fputc(',', file) == EOF) return -1;
    if (openapi_emit_ref(file, error_type) != 0) return -1;
  }
  return fputs("]}", file) == EOF ? -1 : 0;
}


static int openapi_emit_operation(
    FILE *file, const IdlContract *contract,
    const databind_compiler_http_projection_config *config,
    const openapi_operation *operation) {
  int first = 1;
  int request_body_required = 0;
  int request_body_count;
  int response_body_count;
  size_t i, j;
  char operation_id[512];
  int operation_id_length;
  const IdlOperation *idl_operation =
      operation != NULL ? operation->operation : NULL;

  if (contract == NULL || idl_operation == NULL ||
      fputc('{', file) == EOF)
    return -1;

  operation_id_length = snprintf(
      operation_id, sizeof(operation_id), "%s.%s",
      operation->service_name, operation->operation_name);
  if (operation_id_length <= 0 ||
      (size_t)operation_id_length >= sizeof(operation_id) ||
      openapi_emit_key(file, &first, "operationId") != 0 ||
      openapi_json_string(file, operation_id) != 0 ||
      openapi_emit_key(file, &first, "tags") != 0 ||
      fputc('[', file) == EOF ||
      openapi_json_string(file, operation->service_name) != 0 ||
      fputc(']', file) == EOF)
    return -1;

  if (openapi_emit_parameters(
          file, contract, config, operation, &first) != 0)
    return -1;

  request_body_count = openapi_body_field_count(
      contract, config, operation,
      DATABIND_COMPILER_PROJECTION_INGRESS,
      &request_body_required);
  if (request_body_count < 0) return -1;
  if (request_body_count > 0) {
    if (openapi_emit_key(file, &first, "requestBody") != 0 ||
        fprintf(
            file,
            "{\"required\":%s,\"content\":{\"application/json\":{\"schema\":",
            request_body_required ? "true" : "false") < 0 ||
        openapi_emit_body_schema(
            file, contract, config, operation,
            DATABIND_COMPILER_PROJECTION_INGRESS) != 0 ||
        fputs("}}}", file) == EOF)
      return -1;
  }

  if (openapi_emit_key(file, &first, "responses") != 0 ||
      fputc('{', file) == EOF)
    return -1;

  {
    char status[4];
    int response_map_first = 1;
    int success_first = 1;
    int written =
        snprintf(status, sizeof(status), "%d", operation->success_status);

    if (written != 3 ||
        openapi_emit_key(file, &response_map_first, status) != 0 ||
        fputc('{', file) == EOF ||
        openapi_emit_key(file, &success_first, "description") != 0 ||
        openapi_json_string(file, "Success") != 0 ||
        openapi_emit_response_headers(
            file, contract, config, operation, &success_first) != 0)
      return -1;

    response_body_count = openapi_body_field_count(
        contract, config, operation,
        DATABIND_COMPILER_PROJECTION_EGRESS, NULL);
    if (response_body_count < 0) return -1;
    if (response_body_count > 0) {
      if (openapi_emit_key(file, &success_first, "content") != 0 ||
          fputs("{\"application/json\":{\"schema\":", file) == EOF ||
          openapi_emit_body_schema(
              file, contract, config, operation,
              DATABIND_COMPILER_PROJECTION_EGRESS) != 0 ||
          fputs("}}", file) == EOF)
        return -1;
    }
    if (fputc('}', file) == EOF) return -1;

    for (i = 0u; i < idl_operation->error_count; ++i) {
      const char *error_type = idl_operation->error_types[i];
      int error_status;
      int seen = 0;
      char error_status_text[4];
      int error_first = 1;

      if (error_type == NULL) return -1;
      error_status = openapi_error_status(
          config, operation->service_name,
          operation->operation_name, error_type);
      if (error_status == operation->success_status) return -1;

      for (j = 0u; j < i; ++j) {
        const char *previous = idl_operation->error_types[j];
        if (previous != NULL &&
            openapi_error_status(
                config, operation->service_name,
                operation->operation_name, previous) == error_status) {
          seen = 1;
          break;
        }
      }
      if (seen) continue;

      if (snprintf(error_status_text, sizeof(error_status_text),
                   "%d", error_status) != 3 ||
          openapi_emit_key(
              file, &response_map_first, error_status_text) != 0 ||
          fputc('{', file) == EOF ||
          openapi_emit_key(file, &error_first, "description") != 0 ||
          openapi_json_string(file, "Typed Service error") != 0 ||
          openapi_emit_key(file, &error_first, "content") != 0 ||
          fputs("{\"application/json\":{\"schema\":", file) == EOF ||
          openapi_emit_error_schema_for_status(
              file, contract, config, operation, error_status) != 0 ||
          fputs("}}}", file) == EOF)
        return -1;
    }
  }

  return fputs("}}", file) == EOF ? -1 : 0;
}


static int openapi_emit_paths(
    FILE *file, const IdlContract *contract,
    const databind_compiler_http_projection_config *config,
    const openapi_operation *operations, size_t operation_count) {
  size_t i, j;
  int first_path = 1;

  if (contract == NULL ||
      fputs("\"paths\":{", file) == EOF)
    return -1;
  for (i = 0u; i < operation_count; ++i) {
    int earlier = 0;
    int first_method = 1;
    for (j = 0u; j < i; ++j)
      if (strcmp(operations[i].route, operations[j].route) == 0) {
        earlier = 1;
        break;
      }
    if (earlier) continue;

    if (openapi_emit_comma(file, &first_path) != 0 ||
        openapi_json_string(file, operations[i].route) != 0 ||
        fputc(':', file) == EOF ||
        fputc('{', file) == EOF)
      return -1;

    for (j = 0u; j < operation_count; ++j) {
      char method[16];
      size_t k;
      if (strcmp(operations[i].route, operations[j].route) != 0) continue;
      if (strlen(operations[j].method) >= sizeof(method)) return -1;
      for (k = 0u; operations[j].method[k] != '\0'; ++k)
        method[k] = (char)tolower((unsigned char)operations[j].method[k]);
      method[k] = '\0';
      if (openapi_emit_comma(file, &first_method) != 0 ||
          openapi_json_string(file, method) != 0 ||
          fputc(':', file) == EOF ||
          openapi_emit_operation(
              file, contract, config, &operations[j]) != 0)
        return -1;
    }
    if (fputc('}', file) == EOF) return -1;
  }
  return fputc('}', file) == EOF ? -1 : 0;
}

static int openapi_emit_components(
    FILE *file, const IdlContract *contract) {
  size_t i;
  int first = 1;

  if (file == NULL || contract == NULL ||
      fputs("\"components\":{\"schemas\":{", file) == EOF)
    return -1;

  for (i = 0u; i < contract->data_count; ++i) {
    const IdlDataDecl *data = &contract->data[i];
    const char *name = data->name;
    const char *json_type = NULL;
    const char *format = NULL;
    int is_unsigned = 0;

    if (data->kind == IDL_DATA_UNION) continue;
    if (name == NULL ||
        openapi_emit_comma(file, &first) != 0 ||
        openapi_json_string(file, name) != 0 ||
        fputc(':', file) == EOF)
      return -1;

    if (data->kind == IDL_DATA_ENUM) {
      size_t j;
      int property_first = 1;
      const char *underlying =
          data->underlying_type != NULL ? data->underlying_type : "int32";
      if (!openapi_primitive(
              underlying, &json_type, &format, &is_unsigned) ||
          json_type == NULL || strcmp(json_type, "integer") != 0 ||
          fputc('{', file) == EOF ||
          openapi_emit_key(file, &property_first, "type") != 0 ||
          openapi_json_string(file, "integer") != 0)
        return -1;
      if (format != NULL &&
          (openapi_emit_key(file, &property_first, "format") != 0 ||
           openapi_json_string(file, format) != 0))
        return -1;
      if (is_unsigned &&
          (openapi_emit_key(file, &property_first, "minimum") != 0 ||
           fputs("0", file) == EOF))
        return -1;
      if (openapi_emit_key(file, &property_first, "enum") != 0 ||
          fputc('[', file) == EOF)
        return -1;
      for (j = 0u; j < data->enum_item_count; ++j) {
        const char *value = data->enum_items[j].value;
        if (value == NULL || (j != 0u && fputc(',', file) == EOF) ||
            fputs(value, file) == EOF)
          return -1;
      }
      if (fputs("]}", file) == EOF) return -1;
    } else if (data->kind == IDL_DATA_MESSAGE ||
               data->kind == IDL_DATA_COMPOSITE ||
               data->kind == IDL_DATA_GROUP) {
      if (openapi_emit_record_schema(file, contract, data) != 0)
        return -1;
    } else {
      return -1;
    }
  }

  return fputs("}}", file) == EOF ? -1 : 0;
}

;
