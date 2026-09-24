#include "projection_config.h"

#include <json_parser.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int config_error(char *error, size_t error_size, const char *message) {
  if (error != NULL && error_size != 0u)
    snprintf(error, error_size, "%s", message != NULL ? message : "projection config error");
  return -1;
}

static int config_errorf(
    char *error, size_t error_size, const char *format, const char *value) {
  if (error != NULL && error_size != 0u)
    snprintf(error, error_size, format, value != NULL ? value : "<null>");
  return -1;
}

static int string_equal_n(const char *left, size_t left_len, const char *right) {
  size_t right_len;
  if (left == NULL || right == NULL) return 0;
  right_len = strlen(right);
  return left_len == right_len && memcmp(left, right, left_len) == 0;
}

static int object_keys_valid(
    const json_value_t *object,
    const char *const *allowed,
    size_t allowed_count,
    char *error,
    size_t error_size) {
  size_t i;
  if (object == NULL || json_type(object) != JSON_OBJECT)
    return config_error(error, error_size, "Projection config object expected");

  for (i = 0u; i < json_object_size(object); ++i) {
    const char *key = json_object_key(object, i);
    size_t key_len = json_object_key_len(object, i);
    size_t j;
    int known = 0;

    for (j = 0u; j < i; ++j) {
      const char *previous = json_object_key(object, j);
      size_t previous_len = json_object_key_len(object, j);
      if (key_len == previous_len &&
          key != NULL && previous != NULL &&
          memcmp(key, previous, key_len) == 0)
        return config_errorf(
            error, error_size,
            "Projection config repeats key '%s'", key);
    }

    for (j = 0u; j < allowed_count; ++j) {
      if (string_equal_n(key, key_len, allowed[j])) {
        known = 1;
        break;
      }
    }
    if (!known)
      return config_errorf(
          error, error_size,
          "Unknown projection config key '%s'", key);
  }
  return 0;
}

static const char *required_string(
    const json_value_t *object, const char *key,
    char *error, size_t error_size) {
  json_value_t *value = json_object_get(object, key);
  const char *text;
  size_t length;
  if (value == NULL || json_type(value) != JSON_STRING) {
    config_errorf(error, error_size,
                  "Projection config requires string '%s'", key);
    return NULL;
  }
  text = json_string(value);
  length = json_string_len(value);
  if (text == NULL || length == 0u || strlen(text) != length) {
    config_errorf(error, error_size,
                  "Projection config requires non-empty text '%s'", key);
    return NULL;
  }
  return text;
}

static int optional_string(
    const json_value_t *object, const char *key, const char **out,
    char *error, size_t error_size) {
  json_value_t *value = json_object_get(object, key);
  const char *text;
  size_t length;
  *out = NULL;
  if (value == NULL) return 0;
  if (json_type(value) != JSON_STRING)
    return config_errorf(error, error_size,
                         "Projection config '%s' must be a string", key);
  text = json_string(value);
  length = json_string_len(value);
  if (text == NULL || length == 0u || strlen(text) != length)
    return config_errorf(error, error_size,
                         "Projection config '%s' must be non-empty", key);
  *out = text;
  return 0;
}

static int number_token(
    const json_value_t *value, const char **out, size_t *out_len) {
  if (value == NULL || json_type(value) != JSON_NUMBER) return 0;
  *out = json_number_text(value, out_len);
  return *out != NULL && *out_len != 0u;
}

static int parse_u64_value(const json_value_t *value, uint64_t *out) {
  const char *text;
  size_t length;
  size_t i;
  uint64_t result = 0u;
  if (!number_token(value, &text, &length)) return 0;
  for (i = 0u; i < length; ++i) {
    unsigned digit;
    if (text[i] < '0' || text[i] > '9') return 0;
    digit = (unsigned)(text[i] - '0');
    if (result > (UINT64_MAX - digit) / 10u) return 0;
    result = result * 10u + digit;
  }
  *out = result;
  return 1;
}

static int parse_i64_value(const json_value_t *value, int64_t *out) {
  const char *text;
  size_t length;
  size_t i = 0u;
  int negative = 0;
  uint64_t magnitude = 0u;
  uint64_t limit;
  if (!number_token(value, &text, &length)) return 0;
  if (text[0] == '-') {
    negative = 1;
    i = 1u;
    if (i == length) return 0;
  }
  limit = negative ? (uint64_t)INT64_MAX + UINT64_C(1)
                   : (uint64_t)INT64_MAX;
  for (; i < length; ++i) {
    unsigned digit;
    if (text[i] < '0' || text[i] > '9') return 0;
    digit = (unsigned)(text[i] - '0');
    if (magnitude > (limit - digit) / 10u) return 0;
    magnitude = magnitude * 10u + digit;
  }
  if (negative) {
    if (magnitude == (uint64_t)INT64_MAX + UINT64_C(1))
      *out = INT64_MIN;
    else
      *out = -(int64_t)magnitude;
  } else {
    *out = (int64_t)magnitude;
  }
  return 1;
}

static int optional_int(
    const json_value_t *object, const char *key,
    int minimum, int maximum, int default_value, int *out,
    char *error, size_t error_size) {
  json_value_t *value = json_object_get(object, key);
  int64_t parsed;
  *out = default_value;
  if (value == NULL) return 0;
  if (!parse_i64_value(value, &parsed) ||
      parsed < minimum || parsed > maximum)
    return config_errorf(error, error_size,
                         "Projection config '%s' is outside the admitted integer range",
                         key);
  *out = (int)parsed;
  return 0;
}

static int optional_ordinal(
    const json_value_t *object, size_t *out,
    char *error, size_t error_size) {
  json_value_t *value = json_object_get(object, "ordinal");
  uint64_t parsed;
  *out = SIZE_MAX;
  if (value == NULL) return 0;
  if (!parse_u64_value(value, &parsed) || parsed > (uint64_t)SIZE_MAX)
    return config_error(error, error_size,
                        "Projection config ordinal is outside size_t range");
  *out = (size_t)parsed;
  return 0;
}

static int parse_direction(
    const char *text, databind_compiler_projection_direction *out) {
  if (strcmp(text, "ingress") == 0) {
    *out = DATABIND_COMPILER_PROJECTION_INGRESS;
    return 1;
  }
  if (strcmp(text, "egress") == 0) {
    *out = DATABIND_COMPILER_PROJECTION_EGRESS;
    return 1;
  }
  return 0;
}

static int parse_http_location(
    const char *text, databind_compiler_http_field_location *out) {
  static const struct {
    const char *name;
    databind_compiler_http_field_location location;
  } rows[] = {
      {"path", DATABIND_COMPILER_HTTP_PATH},
      {"query", DATABIND_COMPILER_HTTP_QUERY},
      {"header", DATABIND_COMPILER_HTTP_HEADER},
      {"cookie", DATABIND_COMPILER_HTTP_COOKIE},
      {"body", DATABIND_COMPILER_HTTP_BODY},
      {"response_header", DATABIND_COMPILER_HTTP_RESPONSE_HEADER},
      {"response_body", DATABIND_COMPILER_HTTP_RESPONSE_BODY},
  };
  size_t i;
  for (i = 0u; i < sizeof(rows) / sizeof(rows[0]); ++i) {
    if (strcmp(text, rows[i].name) == 0) {
      *out = rows[i].location;
      return 1;
    }
  }
  return 0;
}

static int parse_http_context(
    const json_value_t *operation, uint64_t *out,
    char *error, size_t error_size) {
  static const struct {
    const char *name;
    uint64_t bit;
  } rows[] = {
      {"deadline", UINT64_C(1) << 0},
      {"cancellation", UINT64_C(1) << 1},
      {"request_id", UINT64_C(1) << 2},
      {"trace", UINT64_C(1) << 3},
      {"principal", UINT64_C(1) << 4},
      {"metadata", UINT64_C(1) << 5},
  };
  json_value_t *array = json_object_get(operation, "context");
  size_t i;
  *out = 0u;
  if (array == NULL) return 0;
  if (json_type(array) != JSON_ARRAY)
    return config_error(error, error_size,
                        "HTTP projection context must be an array");
  for (i = 0u; i < json_array_size(array); ++i) {
    json_value_t *value = json_array_get(array, i);
    const char *name;
    size_t j;
    uint64_t bit = 0u;
    if (value == NULL || json_type(value) != JSON_STRING)
      return config_error(error, error_size,
                          "HTTP projection context entries must be strings");
    name = json_string(value);
    if (name == NULL || name[0] == '\0')
      return config_error(error, error_size,
                          "HTTP projection context name must be non-empty");
    for (j = 0u; j < sizeof(rows) / sizeof(rows[0]); ++j)
      if (strcmp(name, rows[j].name) == 0) {
        bit = rows[j].bit;
        break;
      }
    if (bit == 0u)
      return config_errorf(error, error_size,
                           "Unknown HTTP context capability '%s'", name);
    if ((*out & bit) != 0u)
      return config_errorf(error, error_size,
                           "Duplicate HTTP context capability '%s'", name);
    *out |= bit;
  }
  return 0;
}

static int section_counts(
    const json_value_t *section, size_t *operations,
    size_t *fields, size_t *errors,
    char *error, size_t error_size) {
  static const char *const section_keys[] = {"operations"};
  json_value_t *array;
  size_t i;
  *operations = *fields = *errors = 0u;
  if (section == NULL || json_type(section) != JSON_OBJECT)
    return config_error(error, error_size,
                        "Projection backend section must be an object");
  if (object_keys_valid(
          section, section_keys,
          sizeof(section_keys) / sizeof(section_keys[0]),
          error, error_size) != 0)
    return -1;
  array = json_object_get(section, "operations");
  if (array == NULL || json_type(array) != JSON_ARRAY)
    return config_error(error, error_size,
                        "Projection backend requires an operations array");
  *operations = json_array_size(array);
  for (i = 0u; i < *operations; ++i) {
    json_value_t *operation = json_array_get(array, i);
    json_value_t *operation_fields;
    json_value_t *operation_errors;
    if (operation == NULL || json_type(operation) != JSON_OBJECT)
      return config_error(error, error_size,
                          "Projection operation must be an object");
    operation_fields = json_object_get(operation, "fields");
    operation_errors = json_object_get(operation, "errors");
    if (operation_fields != NULL) {
      if (json_type(operation_fields) != JSON_ARRAY)
        return config_error(error, error_size,
                            "Projection operation fields must be an array");
      if (*fields > SIZE_MAX - json_array_size(operation_fields))
        return config_error(error, error_size,
                            "Projection field count overflow");
      *fields += json_array_size(operation_fields);
    }
    if (operation_errors != NULL) {
      if (json_type(operation_errors) != JSON_ARRAY)
        return config_error(error, error_size,
                            "Projection operation errors must be an array");
      if (*errors > SIZE_MAX - json_array_size(operation_errors))
        return config_error(error, error_size,
                            "Projection error count overflow");
      *errors += json_array_size(operation_errors);
    }
  }
  return 0;
}

static int parse_http(
    const json_value_t *section,
    databind_compiler_projection_config *out,
    char *error, size_t error_size) {
  static const char *const operation_keys[] = {
      "service", "operation", "method", "route", "success_status",
      "context", "fields", "errors"};
  static const char *const field_keys[] = {
      "direction", "field", "location", "name", "ordinal"};
  static const char *const error_keys[] = {"type", "status"};
  json_value_t *operations;
  size_t operation_count, field_count, error_count;
  size_t oi, fi = 0u, ei = 0u;

  if (section_counts(section, &operation_count, &field_count, &error_count,
                     error, error_size) != 0)
    return -1;
  if (operation_count != 0u) {
    out->http_operations = (databind_compiler_http_operation_config *)calloc(
        operation_count, sizeof(*out->http_operations));
    if (out->http_operations == NULL)
      return config_error(error, error_size,
                          "Out of memory allocating HTTP operations");
  }
  if (field_count != 0u) {
    out->http_fields = (databind_compiler_http_field_config *)calloc(
        field_count, sizeof(*out->http_fields));
    if (out->http_fields == NULL)
      return config_error(error, error_size,
                          "Out of memory allocating HTTP fields");
  }
  if (error_count != 0u) {
    out->http_errors = (databind_compiler_http_error_config *)calloc(
        error_count, sizeof(*out->http_errors));
    if (out->http_errors == NULL)
      return config_error(error, error_size,
                          "Out of memory allocating HTTP errors");
  }

  operations = json_object_get(section, "operations");
  for (oi = 0u; oi < operation_count; ++oi) {
    json_value_t *operation = json_array_get(operations, oi);
    json_value_t *fields;
    json_value_t *errors;
    databind_compiler_http_operation_config *op = &out->http_operations[oi];
    size_t i;

    if (object_keys_valid(
            operation, operation_keys,
            sizeof(operation_keys) / sizeof(operation_keys[0]),
            error, error_size) != 0)
      return -1;
    op->service_name = required_string(operation, "service", error, error_size);
    if (op->service_name == NULL) return -1;
    op->operation_name =
        required_string(operation, "operation", error, error_size);
    if (op->operation_name == NULL) return -1;
    if (optional_string(operation, "method", &op->method, error, error_size) != 0 ||
        optional_string(operation, "route", &op->route, error, error_size) != 0 ||
        optional_int(operation, "success_status", 100, 599, 0,
                     &op->success_status, error, error_size) != 0 ||
        parse_http_context(operation, &op->context_flags,
                           error, error_size) != 0)
      return -1;

    fields = json_object_get(operation, "fields");
    for (i = 0u; fields != NULL && i < json_array_size(fields); ++i, ++fi) {
      json_value_t *field = json_array_get(fields, i);
      databind_compiler_http_field_config *dst = &out->http_fields[fi];
      const char *direction;
      const char *location;
      if (field == NULL || json_type(field) != JSON_OBJECT)
        return config_error(error, error_size,
                            "HTTP field mapping must be an object");
      if (object_keys_valid(
              field, field_keys,
              sizeof(field_keys) / sizeof(field_keys[0]),
              error, error_size) != 0)
        return -1;
      direction = required_string(field, "direction", error, error_size);
      location = required_string(field, "location", error, error_size);
      dst->schema_field = required_string(field, "field", error, error_size);
      if (direction == NULL || location == NULL || dst->schema_field == NULL)
        return -1;
      if (!parse_direction(direction, &dst->direction))
        return config_errorf(error, error_size,
                             "Unknown projection direction '%s'", direction);
      if (!parse_http_location(location, &dst->location))
        return config_errorf(error, error_size,
                             "Unknown HTTP field location '%s'", location);
      if (optional_string(field, "name", &dst->wire_name,
                          error, error_size) != 0 ||
          optional_ordinal(field, &dst->ordinal, error, error_size) != 0)
        return -1;
      dst->service_name = op->service_name;
      dst->operation_name = op->operation_name;
    }

    errors = json_object_get(operation, "errors");
    for (i = 0u; errors != NULL && i < json_array_size(errors); ++i, ++ei) {
      json_value_t *entry = json_array_get(errors, i);
      databind_compiler_http_error_config *dst = &out->http_errors[ei];
      if (entry == NULL || json_type(entry) != JSON_OBJECT)
        return config_error(error, error_size,
                            "HTTP error mapping must be an object");
      if (object_keys_valid(
              entry, error_keys,
              sizeof(error_keys) / sizeof(error_keys[0]),
              error, error_size) != 0)
        return -1;
      dst->error_type = required_string(entry, "type", error, error_size);
      if (dst->error_type == NULL ||
          optional_int(entry, "status", 100, 599, -1,
                       &dst->status, error, error_size) != 0)
        return -1;
      if (dst->status == -1)
        return config_error(error, error_size,
                            "HTTP error mapping requires status");
      dst->service_name = op->service_name;
      dst->operation_name = op->operation_name;
    }
  }

  out->http.operations = out->http_operations;
  out->http.operation_count = operation_count;
  out->http.fields = out->http_fields;
  out->http.field_count = field_count;
  out->http.errors = out->http_errors;
  out->http.error_count = error_count;
  out->has_http = 1;
  return 0;
}

static int parse_rpc(
    const json_value_t *section,
    databind_compiler_projection_config *out,
    char *error, size_t error_size) {
  static const char *const operation_keys[] = {
      "service", "operation", "wire_method", "fields", "errors"};
  static const char *const field_keys[] = {
      "direction", "field", "name", "ordinal"};
  static const char *const error_keys[] = {"type", "code"};
  json_value_t *operations;
  size_t operation_count, field_count, error_count;
  size_t oi, fi = 0u, ei = 0u;

  if (section_counts(section, &operation_count, &field_count, &error_count,
                     error, error_size) != 0)
    return -1;
  if (operation_count != 0u) {
    out->rpc_operations = (databind_compiler_rpc_operation_config *)calloc(
        operation_count, sizeof(*out->rpc_operations));
    if (out->rpc_operations == NULL)
      return config_error(error, error_size,
                          "Out of memory allocating RPC operations");
  }
  if (field_count != 0u) {
    out->rpc_fields = (databind_compiler_rpc_field_config *)calloc(
        field_count, sizeof(*out->rpc_fields));
    if (out->rpc_fields == NULL)
      return config_error(error, error_size,
                          "Out of memory allocating RPC fields");
  }
  if (error_count != 0u) {
    out->rpc_errors = (databind_compiler_rpc_error_config *)calloc(
        error_count, sizeof(*out->rpc_errors));
    if (out->rpc_errors == NULL)
      return config_error(error, error_size,
                          "Out of memory allocating RPC errors");
  }

  operations = json_object_get(section, "operations");
  for (oi = 0u; oi < operation_count; ++oi) {
    json_value_t *operation = json_array_get(operations, oi);
    json_value_t *fields;
    json_value_t *errors;
    databind_compiler_rpc_operation_config *op = &out->rpc_operations[oi];
    size_t i;

    if (object_keys_valid(
            operation, operation_keys,
            sizeof(operation_keys) / sizeof(operation_keys[0]),
            error, error_size) != 0)
      return -1;
    op->service_name = required_string(operation, "service", error, error_size);
    if (op->service_name == NULL) return -1;
    op->operation_name =
        required_string(operation, "operation", error, error_size);
    if (op->operation_name == NULL) return -1;
    if (optional_string(operation, "wire_method", &op->wire_method,
                        error, error_size) != 0)
      return -1;

    fields = json_object_get(operation, "fields");
    for (i = 0u; fields != NULL && i < json_array_size(fields); ++i, ++fi) {
      json_value_t *field = json_array_get(fields, i);
      databind_compiler_rpc_field_config *dst = &out->rpc_fields[fi];
      const char *direction;
      if (field == NULL || json_type(field) != JSON_OBJECT)
        return config_error(error, error_size,
                            "RPC field mapping must be an object");
      if (object_keys_valid(
              field, field_keys,
              sizeof(field_keys) / sizeof(field_keys[0]),
              error, error_size) != 0)
        return -1;
      direction = required_string(field, "direction", error, error_size);
      dst->schema_field = required_string(field, "field", error, error_size);
      if (direction == NULL || dst->schema_field == NULL) return -1;
      if (!parse_direction(direction, &dst->direction))
        return config_errorf(error, error_size,
                             "Unknown projection direction '%s'", direction);
      if (optional_string(field, "name", &dst->wire_name,
                          error, error_size) != 0 ||
          optional_ordinal(field, &dst->ordinal, error, error_size) != 0)
        return -1;
      dst->service_name = op->service_name;
      dst->operation_name = op->operation_name;
    }

    errors = json_object_get(operation, "errors");
    for (i = 0u; errors != NULL && i < json_array_size(errors); ++i, ++ei) {
      json_value_t *entry = json_array_get(errors, i);
      databind_compiler_rpc_error_config *dst = &out->rpc_errors[ei];
      int code;
      if (entry == NULL || json_type(entry) != JSON_OBJECT)
        return config_error(error, error_size,
                            "RPC error mapping must be an object");
      if (object_keys_valid(
              entry, error_keys,
              sizeof(error_keys) / sizeof(error_keys[0]),
              error, error_size) != 0)
        return -1;
      dst->error_type = required_string(entry, "type", error, error_size);
      if (dst->error_type == NULL ||
          optional_int(entry, "code", INT32_MIN, INT32_MAX, 0,
                       &code, error, error_size) != 0)
        return -1;
      if (json_object_get(entry, "code") == NULL || code == 0)
        return config_error(error, error_size,
                            "RPC error mapping requires nonzero code");
      dst->code = code;
      dst->service_name = op->service_name;
      dst->operation_name = op->operation_name;
    }
  }

  out->rpc.operations = out->rpc_operations;
  out->rpc.operation_count = operation_count;
  out->rpc.fields = out->rpc_fields;
  out->rpc.field_count = field_count;
  out->rpc.errors = out->rpc_errors;
  out->rpc.error_count = error_count;
  out->has_rpc = 1;
  return 0;
}

void databind_compiler_projection_config_dispose(
    databind_compiler_projection_config *config) {
  if (config == NULL) return;
  free(config->http_operations);
  free(config->http_fields);
  free(config->http_errors);
  free(config->rpc_operations);
  free(config->rpc_fields);
  free(config->rpc_errors);
  json_free((json_value_t *)config->json_root);
  memset(config, 0, sizeof(*config));
}

int databind_compiler_projection_config_load(
    const char *path,
    databind_compiler_projection_config *out,
    char *error,
    size_t error_size) {
  static const char *const root_keys[] = {"version", "http", "rpc"};
  json_value_t *root;
  json_value_t *version;
  uint64_t version_number;
  json_value_t *http;
  json_value_t *rpc;

  if (error != NULL && error_size != 0u) error[0] = '\0';
  if (path == NULL || path[0] == '\0' || out == NULL)
    return config_error(error, error_size,
                        "Projection config path/output is invalid");
  memset(out, 0, sizeof(*out));

  root = json_parse_file(path);
  if (root == NULL) {
    const char *parser_error = json_get_error();
    return config_errorf(
        error, error_size,
        "Could not parse projection config JSON: %s",
        parser_error != NULL ? parser_error : "unknown JSON error");
  }
  out->json_root = root;

  if (json_type(root) != JSON_OBJECT ||
      object_keys_valid(
          root, root_keys, sizeof(root_keys) / sizeof(root_keys[0]),
          error, error_size) != 0)
    goto fail;

  version = json_object_get(root, "version");
  if (!parse_u64_value(version, &version_number) || version_number != 1u) {
    config_error(error, error_size,
                 "Projection config version must be integer 1");
    goto fail;
  }

  http = json_object_get(root, "http");
  rpc = json_object_get(root, "rpc");
  if (http == NULL && rpc == NULL) {
    config_error(error, error_size,
                 "Projection config must contain http and/or rpc");
    goto fail;
  }
  if (http != NULL && parse_http(http, out, error, error_size) != 0)
    goto fail;
  if (rpc != NULL && parse_rpc(rpc, out, error, error_size) != 0)
    goto fail;

  return 0;

fail:
  databind_compiler_projection_config_dispose(out);
  return -1;
}
