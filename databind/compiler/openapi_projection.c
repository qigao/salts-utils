#include "openapi_projection.h"

#include "salts_fs.h"

#include <salts_uuid.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct openapi_operation {
  const Node *operation;
  const char *service_name;
  const char *operation_name;
  const char *request_type;
  const char *response_type;
  const databind_compiler_http_operation_config *config;
  char *route;
  const char *method;
  int success_status;
} openapi_operation;

static const Node *openapi_child(const Node *parent, const char *name) {
  size_t i;
  if (parent == NULL || parent->type != NODE_MAP || name == NULL) return NULL;
  for (i = 0u; i < parent->data.map.count; ++i) {
    const Node *child = parent->data.map.items[i];
    if (child != NULL && child->name != NULL &&
        strcmp(child->name, name) == 0)
      return child;
  }
  return NULL;
}

static const Node *openapi_list(const Node *parent, const char *name) {
  const Node *child = openapi_child(parent, name);
  return child != NULL && child->type == NODE_LIST ? child : NULL;
}

static const char *openapi_string(const Node *parent, const char *name) {
  const Node *child = openapi_child(parent, name);
  return child != NULL && child->type == NODE_STRING
             ? child->data.string_val
             : NULL;
}

static int openapi_flag(const Node *parent, const char *name) {
  return openapi_child(parent, name) != NULL;
}

static char *openapi_strdup(const char *text) {
  size_t length;
  char *copy;
  if (text == NULL) return NULL;
  length = strlen(text);
  if (length == SIZE_MAX) return NULL;
  copy = (char *)malloc(length + 1u);
  if (copy != NULL) memcpy(copy, text, length + 1u);
  return copy;
}

static const Node *openapi_find_named(
    const Node *root, const char *list_name, const char *name) {
  const Node *list = openapi_list(root, list_name);
  size_t i;
  if (list == NULL || name == NULL) return NULL;
  for (i = 0u; i < list->data.list.count; ++i) {
    const Node *item = list->data.list.items[i];
    const char *candidate = openapi_string(item, "name");
    if (candidate != NULL && strcmp(candidate, name) == 0)
      return item;
  }
  return NULL;
}

static const Node *openapi_record(const Node *root, const char *name) {
  const Node *result;
  result = openapi_find_named(root, "messages", name);
  if (result != NULL) return result;
  result = openapi_find_named(root, "composites", name);
  if (result != NULL) return result;
  return openapi_find_named(root, "groups", name);
}

static const Node *openapi_enum(const Node *root, const char *name) {
  return openapi_find_named(root, "enums", name);
}

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

static const Node *openapi_field(
    const Node *root, const char *type_name, const char *field_name) {
  const Node *record = openapi_record(root, type_name);
  const Node *fields = openapi_list(record, "fields");
  size_t i;
  if (fields == NULL || field_name == NULL) return NULL;
  for (i = 0u; i < fields->data.list.count; ++i) {
    const Node *field = fields->data.list.items[i];
    const char *name = openapi_string(field, "name");
    if (name != NULL && strcmp(name, field_name) == 0) return field;
  }
  return NULL;
}

static int openapi_operation_has_error(
    const Node *operation, const char *error_type) {
  const Node *errors = openapi_list(operation, "errors");
  size_t i;
  if (errors == NULL || error_type == NULL) return 0;
  for (i = 0u; i < errors->data.list.count; ++i) {
    const Node *item = errors->data.list.items[i];
    if (item != NULL && item->type == NODE_STRING &&
        item->data.string_val != NULL &&
        strcmp(item->data.string_val, error_type) == 0)
      return 1;
  }
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
    const Node *root,
    const databind_compiler_http_projection_config *config,
    const openapi_operation *operations,
    size_t operation_count) {
  size_t i, j;

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
    const Node *schema_field;
    const char *wire;
    if (field->service_name == NULL || field->operation_name == NULL ||
        field->schema_field == NULL)
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
    schema_field = openapi_field(root, type_name, field->schema_field);
    if (schema_field == NULL) return 0;

    if (field->location == DATABIND_COMPILER_HTTP_PATH) {
      wire = field->wire_name != NULL ? field->wire_name : field->schema_field;
      if (openapi_flag(schema_field, "is_optional") ||
          openapi_flag(schema_field, "is_nullable") ||
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
    const Node *root,
    const databind_compiler_http_projection_config *config,
    openapi_operation **out_operations,
    size_t *out_count) {
  const Node *services = openapi_list(root, "services");
  openapi_operation *result = NULL;
  size_t count = 0u, index = 0u;
  size_t i, j, k;

  if (out_operations == NULL || out_count == NULL || services == NULL)
    return -1;
  *out_operations = NULL;
  *out_count = 0u;

  for (i = 0u; i < services->data.list.count; ++i) {
    const Node *operations = openapi_list(services->data.list.items[i], "operations");
    if (operations != NULL) count += operations->data.list.count;
  }

  if (count != 0u) {
    result = (openapi_operation *)calloc(count, sizeof(*result));
    if (result == NULL) return -1;
  }

  for (i = 0u; i < services->data.list.count; ++i) {
    const Node *service = services->data.list.items[i];
    const Node *operations = openapi_list(service, "operations");
    const char *service_name = openapi_string(service, "name");
    if (service_name == NULL || operations == NULL) goto fail;

    for (j = 0u; j < operations->data.list.count; ++j, ++index) {
      const Node *operation = operations->data.list.items[j];
      const char *operation_name = openapi_string(operation, "name");
      const databind_compiler_http_operation_config *op_config;
      const char *configured_route;
      if (operation_name == NULL) goto fail;

      op_config = openapi_operation_config(
          config, service_name, operation_name);
      result[index].operation = operation;
      result[index].service_name = service_name;
      result[index].operation_name = operation_name;
      result[index].request_type = openapi_string(operation, "request_type");
      result[index].response_type = openapi_string(operation, "response_type");
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

  if (!openapi_http_config_valid(root, config, result, count))
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

static int openapi_field_is_collection(const Node *field) {
  return openapi_flag(field, "is_collection") ||
         openapi_flag(field, "is_list") ||
         openapi_flag(field, "is_set") ||
         openapi_flag(field, "is_map") ||
         openapi_flag(field, "is_group_field");
}

static int openapi_constraint_kind_present(
    const Node *field, const char *kind) {
  const Node *constraints = openapi_list(field, "constraints");
  size_t i;
  for (i = 0u; constraints != NULL && i < constraints->data.list.count; ++i) {
    const char *candidate =
        openapi_string(constraints->data.list.items[i], "kind");
    if (candidate != NULL && strcmp(candidate, kind) == 0) return 1;
  }
  return 0;
}

static int openapi_emit_default(
    FILE *file, const Node *field, const char *type,
    int *first) {
  const char *value;
  const char *json_type;
  const char *format;
  int is_unsigned;

  if (!openapi_flag(field, "has_default")) return 0;
  value = openapi_string(field, "default_value");
  if (value == NULL || openapi_emit_key(file, first, "default") != 0)
    return -1;

  if (strcmp(value, "null") == 0 && openapi_flag(field, "is_nullable"))
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
    FILE *file, const Node *field,
    const char *type, int is_collection, int is_map, int *first) {
  const Node *constraints = openapi_list(field, "constraints");
  size_t i;
  const char *json_type = NULL;
  const char *format = NULL;
  int is_unsigned = 0;

  (void)openapi_primitive(type, &json_type, &format, &is_unsigned);

  if (is_unsigned && !openapi_constraint_kind_present(field, "min")) {
    if (openapi_emit_key(file, first, "minimum") != 0 ||
        fputs("0", file) == EOF)
      return -1;
  }

  for (i = 0u; constraints != NULL && i < constraints->data.list.count; ++i) {
    const Node *constraint = constraints->data.list.items[i];
    const char *kind = openapi_string(constraint, "kind");
    if (kind == NULL) return -1;

    if (strcmp(kind, "min") == 0 || strcmp(kind, "max") == 0) {
      const char *value = openapi_string(constraint, "value");
      const char *key = strcmp(kind, "min") == 0 ? "minimum" : "maximum";
      if (json_type == NULL ||
          (strcmp(json_type, "integer") != 0 &&
           strcmp(json_type, "number") != 0) ||
          value == NULL ||
          openapi_emit_key(file, first, key) != 0 ||
          fputs(value, file) == EOF)
        return -1;
    } else if (strcmp(kind, "pattern") == 0) {
      const char *pattern = openapi_string(constraint, "pattern");
      if (json_type == NULL || strcmp(json_type, "string") != 0 ||
          strcmp(type, "bytes") == 0 || pattern == NULL ||
          openapi_emit_key(file, first, "pattern") != 0 ||
          openapi_json_string(file, pattern) != 0)
        return -1;
    } else if (strcmp(kind, "size") == 0) {
      const char *minimum = openapi_string(constraint, "min");
      const char *maximum = openapi_string(constraint, "max");
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
        /*
         * Decoded-byte @Size cannot be represented losslessly by a base64
         * string minLength/maxLength. Fail closed.
         */
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
    FILE *file, const Node *root, const char *type, int nullable);

static int openapi_emit_field_schema(
    FILE *file, const Node *root, const Node *field) {
  const char *type = openapi_string(field, "type");
  const char *json_type = NULL;
  const char *format = NULL;
  const Node *record;
  const Node *enumeration;
  int is_unsigned = 0;
  int nullable = openapi_flag(field, "is_nullable");
  int is_map = openapi_flag(field, "is_map");
  int is_set = openapi_flag(field, "is_set");
  int is_collection = openapi_field_is_collection(field);
  int first = 1;

  if (file == NULL || root == NULL || field == NULL || type == NULL)
    return -1;
  if (fputc('{', file) == EOF) return -1;

  if (is_map) {
    const char *key_type = openapi_string(field, "key_type");
    const char *value_type = openapi_string(field, "value_type");
    if (key_type == NULL || strcmp(key_type, "string") != 0 ||
        value_type == NULL ||
        openapi_emit_key(file, &first, "type") != 0 ||
        openapi_emit_type_value(file, "object", nullable) != 0 ||
        openapi_emit_key(file, &first, "additionalProperties") != 0 ||
        openapi_emit_type_schema(file, root, value_type, 0) != 0)
      return -1;
  } else if (is_collection) {
    const char *inner = openapi_string(field, "inner_type");
    if (inner == NULL ||
        openapi_emit_key(file, &first, "type") != 0 ||
        openapi_emit_type_value(file, "array", nullable) != 0 ||
        (is_set &&
         (openapi_emit_key(file, &first, "uniqueItems") != 0 ||
          fputs("true", file) == EOF)) ||
        openapi_emit_key(file, &first, "items") != 0 ||
        openapi_emit_type_schema(file, root, inner, 0) != 0)
      return -1;
  } else if (openapi_primitive(type, &json_type, &format, &is_unsigned)) {
    if (openapi_emit_key(file, &first, "type") != 0 ||
        openapi_emit_type_value(file, json_type, nullable) != 0)
      return -1;
    if (format != NULL) {
      if (openapi_emit_key(file, &first, "format") != 0 ||
          openapi_json_string(file, format) != 0)
        return -1;
      if (strcmp(type, "bytes") == 0) {
        if (openapi_emit_key(file, &first, "contentEncoding") != 0 ||
            openapi_json_string(file, "base64") != 0)
          return -1;
      }
    }
  } else if ((record = openapi_record(root, type)) != NULL) {
    (void)record;
    if (nullable) {
      if (openapi_emit_key(file, &first, "anyOf") != 0 ||
          fputs("[", file) == EOF ||
          openapi_emit_ref(file, type) != 0 ||
          fputs(",{\"type\":\"null\"}]", file) == EOF)
        return -1;
    } else {
      char ref[512];
      int written = snprintf(ref, sizeof(ref), "#/components/schemas/%s", type);
      if (written <= 0 || (size_t)written >= sizeof(ref) ||
          openapi_emit_key(file, &first, "$ref") != 0 ||
          openapi_json_string(file, ref) != 0)
        return -1;
    }
  } else if ((enumeration = openapi_enum(root, type)) != NULL) {
    const Node *items = openapi_list(enumeration, "items");
    size_t i;
    const char *underlying = openapi_string(enumeration, "underlying_type");
    const char *enum_json_type = NULL;
    const char *enum_format = NULL;
    if (!openapi_primitive(
            underlying, &enum_json_type, &enum_format, &is_unsigned) ||
        enum_json_type == NULL || strcmp(enum_json_type, "integer") != 0 ||
        items == NULL ||
        openapi_emit_key(file, &first, "type") != 0 ||
        openapi_emit_type_value(file, "integer", nullable) != 0)
      return -1;
    if (enum_format != NULL &&
        (openapi_emit_key(file, &first, "format") != 0 ||
         openapi_json_string(file, enum_format) != 0))
      return -1;
    if (is_unsigned &&
        openapi_emit_key(file, &first, "minimum") == 0) {
      if (fputs("0", file) == EOF) return -1;
    }
    if (openapi_emit_key(file, &first, "enum") != 0 ||
        fputc('[', file) == EOF)
      return -1;
    for (i = 0u; i < items->data.list.count; ++i) {
      const char *value = openapi_string(items->data.list.items[i], "value");
      if (value == NULL || (i != 0u && fputc(',', file) == EOF) ||
          fputs(value, file) == EOF)
        return -1;
    }
    if (fputc(']', file) == EOF) return -1;
  } else {
    return -1;
  }

  if (openapi_emit_constraints(
          file, field, type, is_collection && !is_map, is_map, &first) != 0 ||
      openapi_emit_default(file, field, type, &first) != 0 ||
      fputc('}', file) == EOF)
    return -1;
  return 0;
}

static int openapi_emit_type_schema(
    FILE *file, const Node *root, const char *type, int nullable) {
  const char *json_type = NULL;
  const char *format = NULL;
  const Node *record;
  const Node *enumeration;
  int is_unsigned = 0;
  int first = 1;

  if (file == NULL || root == NULL || type == NULL) return -1;
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
  } else if ((record = openapi_record(root, type)) != NULL) {
    (void)record;
    if (nullable) {
      if (openapi_emit_key(file, &first, "anyOf") != 0 ||
          fputs("[", file) == EOF ||
          openapi_emit_ref(file, type) != 0 ||
          fputs(",{\"type\":\"null\"}]", file) == EOF)
        return -1;
    } else {
      char ref[512];
      int written = snprintf(ref, sizeof(ref), "#/components/schemas/%s", type);
      if (written <= 0 || (size_t)written >= sizeof(ref) ||
          openapi_emit_key(file, &first, "$ref") != 0 ||
          openapi_json_string(file, ref) != 0)
        return -1;
    }
  } else if ((enumeration = openapi_enum(root, type)) != NULL) {
    const Node *items = openapi_list(enumeration, "items");
    size_t i;
    const char *underlying = openapi_string(enumeration, "underlying_type");
    if (!openapi_primitive(underlying, &json_type, &format, &is_unsigned) ||
        json_type == NULL || strcmp(json_type, "integer") != 0 ||
        items == NULL ||
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
    for (i = 0u; i < items->data.list.count; ++i) {
      const char *value = openapi_string(items->data.list.items[i], "value");
      if (value == NULL || (i != 0u && fputc(',', file) == EOF) ||
          fputs(value, file) == EOF)
        return -1;
    }
    if (fputc(']', file) == EOF) return -1;
  } else {
    return -1;
  }

  return fputc('}', file) == EOF ? -1 : 0;
}

static int openapi_emit_record_schema(
    FILE *file, const Node *root, const Node *record) {
  const Node *fields = openapi_list(record, "fields");
  size_t i;
  int first_required = 1;

  if (file == NULL || root == NULL || record == NULL || fields == NULL)
    return -1;
  if (fputs("{\"type\":\"object\",\"properties\":{", file) == EOF)
    return -1;

  for (i = 0u; i < fields->data.list.count; ++i) {
    const Node *field = fields->data.list.items[i];
    const char *name = openapi_string(field, "name");
    if (name == NULL ||
        (i != 0u && fputc(',', file) == EOF) ||
        openapi_json_string(file, name) != 0 ||
        fputc(':', file) == EOF ||
        openapi_emit_field_schema(file, root, field) != 0)
      return -1;
  }

  if (fputc('}', file) == EOF) return -1;

  for (i = 0u; i < fields->data.list.count; ++i) {
    const Node *field = fields->data.list.items[i];
    if (!openapi_flag(field, "is_optional")) {
      if (first_required) {
        if (fputs(",\"required\":[", file) == EOF) return -1;
        first_required = 0;
      } else if (fputc(',', file) == EOF) {
        return -1;
      }
      if (openapi_json_string(file, openapi_string(field, "name")) != 0)
        return -1;
    }
  }
  if (!first_required && fputc(']', file) == EOF) return -1;

  return fputc('}', file) == EOF ? -1 : 0;
}

static int openapi_emit_components(FILE *file, const Node *root) {
  const char *lists[] = {"composites", "groups", "messages"};
  size_t l, i;
  int first = 1;

  if (fputs("\"components\":{\"schemas\":{", file) == EOF) return -1;

  for (l = 0u; l < sizeof(lists) / sizeof(lists[0]); ++l) {
    const Node *records = openapi_list(root, lists[l]);
    for (i = 0u; records != NULL && i < records->data.list.count; ++i) {
      const Node *record = records->data.list.items[i];
      const char *name = openapi_string(record, "name");
      if (name == NULL ||
          openapi_emit_comma(file, &first) != 0 ||
          openapi_json_string(file, name) != 0 ||
          fputc(':', file) == EOF ||
          openapi_emit_record_schema(file, root, record) != 0)
        return -1;
    }
  }

  {
    const Node *enums = openapi_list(root, "enums");
    for (i = 0u; enums != NULL && i < enums->data.list.count; ++i) {
      const Node *enumeration = enums->data.list.items[i];
      const char *name = openapi_string(enumeration, "name");
      const char *underlying = openapi_string(enumeration, "underlying_type");
      const Node *items = openapi_list(enumeration, "items");
      const char *json_type = NULL;
      const char *format = NULL;
      int is_unsigned = 0;
      size_t j;
      int property_first = 1;
      if (name == NULL || underlying == NULL || items == NULL ||
          !openapi_primitive(
              underlying, &json_type, &format, &is_unsigned) ||
          json_type == NULL || strcmp(json_type, "integer") != 0 ||
          openapi_emit_comma(file, &first) != 0 ||
          openapi_json_string(file, name) != 0 ||
          fputs(":{", file) == EOF ||
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
      for (j = 0u; j < items->data.list.count; ++j) {
        const char *value = openapi_string(items->data.list.items[j], "value");
        if (value == NULL || (j != 0u && fputc(',', file) == EOF) ||
            fputs(value, file) == EOF)
          return -1;
      }
      if (fputs("]}", file) == EOF) return -1;
    }
  }

  return fputs("}}", file) == EOF ? -1 : 0;
}

static int openapi_field_is_body(
    const databind_compiler_http_projection_config *config,
    const openapi_operation *operation,
    databind_compiler_projection_direction direction,
    const Node *field) {
  const char *name = openapi_string(field, "name");
  const databind_compiler_http_field_config *mapping =
      openapi_field_config(
          config, operation->service_name, operation->operation_name,
          direction, name);
  if (mapping == NULL) return 1;
  if (direction == DATABIND_COMPILER_PROJECTION_INGRESS)
    return mapping->location == DATABIND_COMPILER_HTTP_BODY;
  return mapping->location == DATABIND_COMPILER_HTTP_RESPONSE_BODY;
}

static int openapi_body_field_count(
    const Node *root,
    const databind_compiler_http_projection_config *config,
    const openapi_operation *operation,
    databind_compiler_projection_direction direction,
    int *out_required) {
  const char *type_name =
      direction == DATABIND_COMPILER_PROJECTION_INGRESS
          ? operation->request_type : operation->response_type;
  const Node *record;
  const Node *fields;
  size_t i;
  int count = 0;
  int required = 0;

  if (out_required != NULL) *out_required = 0;
  if (type_name == NULL || strcmp(type_name, "void") == 0) return 0;
  record = openapi_record(root, type_name);
  fields = openapi_list(record, "fields");
  if (fields == NULL) return -1;
  for (i = 0u; i < fields->data.list.count; ++i) {
    if (openapi_field_is_body(config, operation, direction,
                              fields->data.list.items[i])) {
      ++count;
      if (!openapi_flag(fields->data.list.items[i], "is_optional"))
        required = 1;
    }
  }
  if (out_required != NULL) *out_required = required;
  return count;
}

static int openapi_emit_body_schema(
    FILE *file, const Node *root,
    const databind_compiler_http_projection_config *config,
    const openapi_operation *operation,
    databind_compiler_projection_direction direction) {
  const char *type_name =
      direction == DATABIND_COMPILER_PROJECTION_INGRESS
          ? operation->request_type : operation->response_type;
  const Node *record = openapi_record(root, type_name);
  const Node *fields = openapi_list(record, "fields");
  size_t i;
  int first_property = 1;
  int first_required = 1;
  int body_count = openapi_body_field_count(
      root, config, operation, direction, NULL);

  if (body_count < 0 || record == NULL || fields == NULL) return -1;
  if ((size_t)body_count == fields->data.list.count)
    return openapi_emit_ref(file, type_name);

  if (fputs("{\"type\":\"object\",\"properties\":{", file) == EOF)
    return -1;
  for (i = 0u; i < fields->data.list.count; ++i) {
    const Node *field = fields->data.list.items[i];
    const char *name = openapi_string(field, "name");
    if (!openapi_field_is_body(config, operation, direction, field))
      continue;
    if (name == NULL ||
        openapi_emit_comma(file, &first_property) != 0 ||
        openapi_json_string(file, name) != 0 ||
        fputc(':', file) == EOF ||
        openapi_emit_field_schema(file, root, field) != 0)
      return -1;
  }
  if (fputc('}', file) == EOF) return -1;

  for (i = 0u; i < fields->data.list.count; ++i) {
    const Node *field = fields->data.list.items[i];
    if (!openapi_field_is_body(config, operation, direction, field) ||
        openapi_flag(field, "is_optional"))
      continue;
    if (first_required) {
      if (fputs(",\"required\":[", file) == EOF) return -1;
      first_required = 0;
    } else if (fputc(',', file) == EOF) {
      return -1;
    }
    if (openapi_json_string(file, openapi_string(field, "name")) != 0)
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
    FILE *file, const Node *root,
    const databind_compiler_http_projection_config *config,
    const openapi_operation *operation,
    int *operation_first) {
  const Node *record = openapi_record(root, operation->request_type);
  const Node *fields = openapi_list(record, "fields");
  size_t i;
  size_t count = 0u;

  if (operation->request_type == NULL ||
      strcmp(operation->request_type, "void") == 0)
    return 0;
  if (fields == NULL || operation_first == NULL) return -1;

  for (i = 0u; i < fields->data.list.count; ++i) {
    const Node *field = fields->data.list.items[i];
    const char *name = openapi_string(field, "name");
    const databind_compiler_http_field_config *mapping =
        openapi_field_config(
            config, operation->service_name, operation->operation_name,
            DATABIND_COMPILER_PROJECTION_INGRESS, name);
    if (mapping != NULL &&
        mapping->location != DATABIND_COMPILER_HTTP_BODY)
      ++count;
  }
  if (count == 0u) return 0;

  if (openapi_emit_key(file, operation_first, "parameters") != 0 ||
      fputc('[', file) == EOF)
    return -1;

  count = 0u;
  for (i = 0u; i < fields->data.list.count; ++i) {
    const Node *field = fields->data.list.items[i];
    const char *name = openapi_string(field, "name");
    const databind_compiler_http_field_config *mapping =
        openapi_field_config(
            config, operation->service_name, operation->operation_name,
            DATABIND_COMPILER_PROJECTION_INGRESS, name);
    const char *location;
    const char *wire;
    int required;

    if (mapping == NULL ||
        mapping->location == DATABIND_COMPILER_HTTP_BODY)
      continue;

    location = openapi_parameter_location(mapping->location);
    if (location == NULL) return -1;
    wire = mapping->wire_name != NULL ? mapping->wire_name : name;
    required = mapping->location == DATABIND_COMPILER_HTTP_PATH ||
               !openapi_flag(field, "is_optional");

    if (count++ != 0u && fputc(',', file) == EOF) return -1;
    if (fputs("{\"name\":", file) == EOF ||
        openapi_json_string(file, wire) != 0 ||
        fputs(",\"in\":", file) == EOF ||
        openapi_json_string(file, location) != 0 ||
        fprintf(file, ",\"required\":%s,\"schema\":",
                required ? "true" : "false") < 0 ||
        openapi_emit_field_schema(file, root, field) != 0 ||
        fputc('}', file) == EOF)
      return -1;
  }

  return fputc(']', file) == EOF ? -1 : 0;
}

static int openapi_emit_response_headers(
    FILE *file, const Node *root,
    const databind_compiler_http_projection_config *config,
    const openapi_operation *operation,
    int *response_first) {
  const Node *record;
  const Node *fields;
  size_t i;
  size_t count = 0u;

  if (operation->response_type == NULL ||
      strcmp(operation->response_type, "void") == 0)
    return 0;
  if (response_first == NULL) return -1;
  record = openapi_record(root, operation->response_type);
  fields = openapi_list(record, "fields");
  if (fields == NULL) return -1;

  for (i = 0u; i < fields->data.list.count; ++i) {
    const Node *field = fields->data.list.items[i];
    const char *name = openapi_string(field, "name");
    const databind_compiler_http_field_config *mapping =
        openapi_field_config(
            config, operation->service_name, operation->operation_name,
            DATABIND_COMPILER_PROJECTION_EGRESS, name);
    if (mapping != NULL &&
        mapping->location == DATABIND_COMPILER_HTTP_RESPONSE_HEADER)
      ++count;
  }
  if (count == 0u) return 0;

  if (openapi_emit_key(file, response_first, "headers") != 0 ||
      fputc('{', file) == EOF)
    return -1;

  count = 0u;
  for (i = 0u; i < fields->data.list.count; ++i) {
    const Node *field = fields->data.list.items[i];
    const char *name = openapi_string(field, "name");
    const databind_compiler_http_field_config *mapping =
        openapi_field_config(
            config, operation->service_name, operation->operation_name,
            DATABIND_COMPILER_PROJECTION_EGRESS, name);
    const char *wire;
    if (mapping == NULL ||
        mapping->location != DATABIND_COMPILER_HTTP_RESPONSE_HEADER)
      continue;
    wire = mapping->wire_name != NULL ? mapping->wire_name : name;

    if (count++ != 0u && fputc(',', file) == EOF) return -1;
    if (openapi_json_string(file, wire) != 0 ||
        fputs(":{\"schema\":", file) == EOF ||
        openapi_emit_field_schema(file, root, field) != 0 ||
        fputc('}', file) == EOF)
      return -1;
  }

  return fputc('}', file) == EOF ? -1 : 0;
}

static int openapi_emit_error_schema_for_status(
    FILE *file, const Node *root,
    const databind_compiler_http_projection_config *config,
    const openapi_operation *operation, int status) {
  const Node *errors = openapi_list(operation->operation, "errors");
  size_t i;
  size_t count = 0u;

  for (i = 0u; errors != NULL && i < errors->data.list.count; ++i) {
    const Node *item = errors->data.list.items[i];
    const char *error_type =
        item != NULL && item->type == NODE_STRING
            ? item->data.string_val : NULL;
    if (error_type != NULL &&
        openapi_error_status(
            config, operation->service_name,
            operation->operation_name, error_type) == status)
      ++count;
  }

  if (count == 0u) return -1;
  if (count == 1u) {
    for (i = 0u; i < errors->data.list.count; ++i) {
      const char *error_type = errors->data.list.items[i]->data.string_val;
      if (openapi_error_status(
              config, operation->service_name,
              operation->operation_name, error_type) == status)
        return openapi_emit_ref(file, error_type);
    }
    return -1;
  }

  if (fputs("{\"oneOf\":[", file) == EOF) return -1;
  count = 0u;
  for (i = 0u; i < errors->data.list.count; ++i) {
    const char *error_type = errors->data.list.items[i]->data.string_val;
    if (openapi_error_status(
            config, operation->service_name,
            operation->operation_name, error_type) != status)
      continue;
    if (count++ != 0u && fputc(',', file) == EOF) return -1;
    if (openapi_emit_ref(file, error_type) != 0) return -1;
  }
  return fputs("]}", file) == EOF ? -1 : 0;
}

static int openapi_emit_operation(
    FILE *file, const Node *root,
    const databind_compiler_http_projection_config *config,
    const openapi_operation *operation) {
  int first = 1;
  int request_body_required = 0;
  int request_body_count;
  int response_body_count;
  const Node *errors;
  size_t i, j;
  char operation_id[512];
  int operation_id_length;

  if (fputc('{', file) == EOF) return -1;

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
          file, root, config, operation, &first) != 0)
    return -1;

  request_body_count = openapi_body_field_count(
      root, config, operation,
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
            file, root, config, operation,
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
            file, root, config, operation, &success_first) != 0)
      return -1;

    response_body_count = openapi_body_field_count(
        root, config, operation,
        DATABIND_COMPILER_PROJECTION_EGRESS, NULL);
    if (response_body_count < 0) return -1;
    if (response_body_count > 0) {
      if (openapi_emit_key(file, &success_first, "content") != 0 ||
          fputs("{\"application/json\":{\"schema\":", file) == EOF ||
          openapi_emit_body_schema(
              file, root, config, operation,
              DATABIND_COMPILER_PROJECTION_EGRESS) != 0 ||
          fputs("}}", file) == EOF)
        return -1;
    }
    if (fputc('}', file) == EOF) return -1;

    errors = openapi_list(operation->operation, "errors");
    for (i = 0u; errors != NULL && i < errors->data.list.count; ++i) {
      const Node *item = errors->data.list.items[i];
      const char *error_type =
          item != NULL && item->type == NODE_STRING
              ? item->data.string_val : NULL;
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
        const Node *previous_item = errors->data.list.items[j];
        const char *previous =
            previous_item != NULL && previous_item->type == NODE_STRING
                ? previous_item->data.string_val : NULL;
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
              file, root, config, operation, error_status) != 0 ||
          fputs("}}}", file) == EOF)
        return -1;
    }
  }

  return fputs("}}", file) == EOF ? -1 : 0;
}

static int openapi_emit_paths(
    FILE *file, const Node *root,
    const databind_compiler_http_projection_config *config,
    const openapi_operation *operations, size_t operation_count) {
  size_t i, j;
  int first_path = 1;

  if (fputs("\"paths\":{", file) == EOF) return -1;
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
          openapi_emit_operation(file, root, config, &operations[j]) != 0)
        return -1;
    }
    if (fputc('}', file) == EOF) return -1;
  }
  return fputc('}', file) == EOF ? -1 : 0;
}

static int openapi_generate(
    const Node *root,
    const databind_compiler_projection_request *request,
    void *context) {
  const databind_compiler_openapi_projection_config *config =
      request != NULL
          ? (const databind_compiler_openapi_projection_config *)request->config
          : NULL;
  const databind_compiler_http_projection_config *http =
      config != NULL ? config->http : NULL;
  const Node *schema = openapi_child(root, "schema");
  const char *schema_name = openapi_string(schema, "schema_name");
  const char *schema_version = openapi_string(schema, "schema_version");
  openapi_operation *operations = NULL;
  size_t operation_count = 0u;
  char *temp = NULL;
  FILE *file = NULL;
  int ok = 0;
  (void)context;

  if (root == NULL || request == NULL || request->output == NULL ||
      request->id.axis != DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT ||
      request->id.kind != DATABIND_COMPILER_ARTIFACT_OPENAPI ||
      schema_name == NULL)
    return -1;

  if (openapi_operations_build(
          root, http, &operations, &operation_count) != 0)
    return -1;

  if (openapi_open_atomic(request->output, &temp, &file) != 0)
    goto cleanup;

  if (fputs("{\"openapi\":\"3.1.0\",\"info\":{\"title\":", file) == EOF ||
      openapi_json_string(file, schema_name) != 0 ||
      fputs(",\"version\":", file) == EOF ||
      openapi_json_string(
          file, schema_version != NULL ? schema_version : "1") != 0 ||
      fputs("},", file) == EOF ||
      openapi_emit_paths(file, root, http, operations, operation_count) != 0 ||
      fputc(',', file) == EOF ||
      openapi_emit_components(file, root) != 0 ||
      fputs("}\n", file) == EOF)
    goto cleanup;

  ok = 1;

cleanup:
  openapi_operations_free(operations, operation_count);
  return openapi_commit_atomic(request->output, temp, file, ok);
}

const databind_compiler_projection_backend
    DATABIND_COMPILER_OPENAPI_BACKEND = {
        {DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT,
         DATABIND_COMPILER_ARTIFACT_OPENAPI},
        "openapi",
        openapi_generate,
        NULL};
