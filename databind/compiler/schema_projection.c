#include "schema_projection.h"
#include "native_service_projection.h"

#include <json_parser.h>
#include <cmeta_fs.h>
#include <cmeta_uuid.h>
#include <fmt.h>
#include <salts/error_codes.h>

#include <stdio.h>
#include <string.h>

enum { SCHEMA_PROJECTION_MAX_OPERATIONS = 1024u };

static int fail(char *error, size_t capacity, const char *message) {
  if (error != NULL && capacity != 0u) {
    size_t length = strlen(message);
    if (length >= capacity) length = capacity - 1u;
    memcpy(error, message, length);
    error[length] = '\0';
  }
  return -1;
}

/* Every insertion transfers ownership only on success. Failed additions release
 * their temporary immediately; the root owns all committed JSON values. */
static int put(json_value_t *object, const char *key, json_value_t *value) {
  if (value != NULL && json_object_add_checked(object, key, value)) return 1;
  json_free(value);
  return 0;
}

static int text(json_value_t *object, const char *key, const char *value) {
  return value != NULL && put(object, key, json_create_string(value));
}

static int append(json_value_t *array, json_value_t *value) {
  if (value != NULL && json_array_add_checked(array, value)) return 1;
  json_free(value);
  return 0;
}

static int app_annotation(const IdlAnnotation *annotation) {
  return annotation->name != NULL &&
      (strcmp(annotation->name, "inject") == 0 || strcmp(annotation->name, "http") == 0 ||
       strncmp(annotation->name, "app_", 4u) == 0);
}

static int no_app_annotations(const IdlAnnotation *annotations, size_t count) {
  for (size_t i = 0u; i < count; ++i)
    if (app_annotation(&annotations[i])) return 0;
  return 1;
}

static int operation_annotations_valid(const IdlOperation *operation) {
  if (!databind_compiler_http_policies_valid(operation)) return 0;
  static const struct { const char *name; size_t arity; int repeated; } definitions[] = {
      {"http", 2u, 0}, {"app_http_field", 4u, 1},
      {"app_http_status", 1u, 0}, {"app_http_formats", 2u, 0},
      {"app_rpc", 1u, 0}, {"app_http_policy", 1u, 1}};
  for (size_t i = 0u; i < operation->annotation_count; ++i) {
    const IdlAnnotation *a = &operation->annotations[i];
    if (!app_annotation(a)) continue;
    size_t d = 0u;
    for (; d < sizeof(definitions) / sizeof(definitions[0]); ++d)
      if (strcmp(a->name, definitions[d].name) == 0) break;
    if (d == sizeof(definitions) / sizeof(definitions[0]) || a->bare ||
        a->argument_count != definitions[d].arity ||
        (!definitions[d].repeated &&
         idl_annotation_count(operation->annotations, operation->annotation_count, a->name) != 1u))
      return 0;
    for (size_t arg = 0u; arg < a->argument_count; ++arg)
      if (a->arguments[arg] == NULL || a->arguments[arg][0] == '\0') return 0;
  }
  return 1;
}

static const IdlAnnotation *annotation(const IdlOperation *op, const char *name) {
  return idl_annotation_find(op->annotations, op->annotation_count, name, 0u);
}

static int json_xml(const char *format) {
  return strcmp(format, "json") == 0 || strcmp(format, "xml") == 0;
}

static json_value_t *operation_object(const IdlService *service, const IdlOperation *operation) {
  json_value_t *object = json_create_object();
  if (object != NULL && text(object, "service", service->name) &&
      text(object, "operation", operation->name))
    return object;
  json_free(object);
  return NULL;
}

static json_value_t *http_operation(const IdlService *service, const IdlOperation *op) {
  const IdlAnnotation *route = annotation(op, "http");
  const IdlAnnotation *status = annotation(op, "app_http_status");
  const IdlAnnotation *formats = annotation(op, "app_http_formats");
  json_value_t *object = NULL;
  json_value_t *fields = NULL;
  if (route == NULL) return NULL;
  object = operation_object(service, op);
  if (object == NULL || !text(object, "method", route->arguments[0]) ||
      !text(object, "route", route->arguments[1])) goto fail;
  if (formats != NULL && (!json_xml(formats->arguments[0]) ||
      !json_xml(formats->arguments[1]))) goto fail;
  if (!text(object, "ingress_format", formats != NULL ? formats->arguments[0] : "json") ||
      !text(object, "egress_format", formats != NULL ? formats->arguments[1] : "json")) goto fail;
  if (status != NULL) {
    const char *value = status->arguments[0];
    if (strlen(value) != 3u || value[0] < '1' || value[0] > '5' ||
        value[1] < '0' || value[1] > '9' || value[2] < '0' || value[2] > '9') goto fail;
    int number = (value[0] - '0') * 100 + (value[1] - '0') * 10 + value[2] - '0';
    if (!put(object, "success_status", json_create_int64(number))) goto fail;
  }
  fields = json_create_array();
  if (!put(object, "fields", fields)) goto fail;
  for (size_t i = 0u; i < op->annotation_count; ++i) {
    const IdlAnnotation *a = &op->annotations[i];
    if (strcmp(a->name, "app_http_field") != 0) continue;
    json_value_t *field = json_create_object();
    if (!append(fields, field)) goto fail;
    if (!text(field, "direction", a->arguments[0]) ||
        !text(field, "field", a->arguments[1]) ||
        !text(field, "location", a->arguments[2]) ||
        !text(field, "name", a->arguments[3])) goto fail;
  }
  return object;
fail:
  json_free(object);
  return NULL;
}

static json_value_t *section(json_value_t *root, const char *name) {
  json_value_t *object = json_create_object();
  if (!put(root, name, object)) return NULL;
  json_value_t *operations = json_create_array();
  if (!put(object, "operations", operations)) return NULL;
  return operations;
}

/* Schema records stay transport-neutral. Application annotations describe
 * service dependencies or operation mappings; other scopes cannot drop them. */
static int placement_valid(const IdlContract *contract) {
  if (!no_app_annotations(contract->annotations, contract->annotation_count)) return 0;
  for (size_t i = 0u; i < contract->data_count; ++i) {
    const IdlDataDecl *d = &contract->data[i];
    if (!no_app_annotations(d->annotations, d->annotation_count)) return 0;
    for (size_t j = 0u; j < d->field_count; ++j)
      if (!no_app_annotations(d->fields[j].annotations, d->fields[j].annotation_count)) return 0;
  }
  for (size_t i = 0u; i < contract->component_count; ++i)
    if (!no_app_annotations(contract->components[i].annotations,
            contract->components[i].annotation_count)) return 0;
  for (size_t i = 0u; i < contract->channel_count; ++i)
    if (!no_app_annotations(contract->channels[i].annotations,
            contract->channels[i].annotation_count)) return 0;
  for (size_t i = 0u; i < contract->service_count; ++i)
    if (!databind_compiler_native_injection_valid(&contract->services[i])) return 0;
  return 1;
}

/* Parameter names do not distinguish endpoint identities. Admission below has
 * already checked that every placeholder has exactly one field binding. */
static int same_route(const char *left, const char *right) {
  while (*left != '\0' && *right != '\0') {
    if (*left != *right) return 0;
    if (*left == '{') {
      left = strchr(left, '}');
      right = strchr(right, '}');
      if (left == NULL || right == NULL) return 0;
    }
    ++left;
    ++right;
  }
  return *left == *right;
}

static int endpoints_unique(const databind_compiler_projection_config *config) {
  for (size_t i = 0u; i < config->http.operation_count; ++i)
    for (size_t j = 0u; j < i; ++j)
      if (strcmp(config->http_operations[i].method, config->http_operations[j].method) == 0 &&
          same_route(config->http_operations[i].route, config->http_operations[j].route))
        return 0;
  for (size_t i = 0u; i < config->rpc.operation_count; ++i)
    for (size_t j = 0u; j < i; ++j)
      if (strcmp(config->rpc_operations[i].wire_method, config->rpc_operations[j].wire_method) == 0)
        return 0;
  return 1;
}

int databind_compiler_schema_projection_build(
    const IdlContract *contract, unsigned transports,
    databind_compiler_projection_config *out, char *error, size_t error_size) {
  json_value_t *root = NULL, *http = NULL, *rpc = NULL;
  size_t count = 0u;
  if (out == NULL) return fail(error, error_size, "Missing projection output");
  memset(out, 0, sizeof(*out));
  if (error != NULL && error_size != 0u) error[0] = '\0';
  if (contract == NULL || transports == 0u ||
      (transports & ~(unsigned)(DATABIND_SCHEMA_PROJECTION_HTTP | DATABIND_SCHEMA_PROJECTION_RPC)) != 0u)
    return fail(error, error_size, "Schema projection requires HTTP and/or RPC");
  if (!placement_valid(contract))
    return fail(error, error_size, "Invalid application annotation: inject belongs on services, http and mappings on operations");
  root = json_create_object();
  if (root == NULL || !put(root, "version", json_create_int64(1))) goto invalid;
  if ((transports & DATABIND_SCHEMA_PROJECTION_HTTP) && (http = section(root, "http")) == NULL) goto invalid;
  if ((transports & DATABIND_SCHEMA_PROJECTION_RPC) && (rpc = section(root, "rpc")) == NULL) goto invalid;
  for (size_t i = 0u; i < contract->service_count; ++i) {
    const IdlService *service = &contract->services[i];
    for (size_t j = 0u; j < service->operation_count; ++j) {
      const IdlOperation *op = &service->operations[j];
      if (++count > SCHEMA_PROJECTION_MAX_OPERATIONS || !operation_annotations_valid(op)) goto invalid;
      if (http != NULL && !append(http, http_operation(service, op))) goto invalid;
      if (rpc != NULL) {
        const IdlAnnotation *method = annotation(op, "app_rpc");
        if (method == NULL) goto invalid;
        json_value_t *object = operation_object(service, op);
        if (!append(rpc, object)) goto invalid;
        if (!text(object, "wire_method", method->arguments[0]) ||
            !text(object, "ingress_format", "json") || !text(object, "egress_format", "json")) goto invalid;
      }
    }
  }
  if (count == 0u) goto invalid;
  if (databind_compiler_projection_config_from_json(root, out, error, error_size) != 0) return -1;
  if (!databind_compiler_method_plan_configs_valid(
          contract, out->has_http ? &out->http : NULL, out->has_rpc ? &out->rpc : NULL) ||
      !endpoints_unique(out)) {
    databind_compiler_projection_config_dispose(out);
    return fail(error, error_size, "Invalid or duplicate route, field placement, format or RPC method");
  }
  return 0;
invalid:
  json_free(root);
  return fail(error, error_size,
      "Missing, invalid, duplicate or unknown application annotation (maximum 1024 operations)");
}

static int suffix(const char *path, const char *extension) {
  size_t length = path != NULL ? strlen(path) : 0u;
  size_t expected = strlen(extension);
  return length >= expected && strcmp(path + length - expected, extension) == 0;
}

int databind_compiler_schema_projection_export(
    const char *schema_path, const char *output_path, const char *transports,
    char *error, size_t error_size) {
  unsigned selected = 0u;
  cmeta_fs_buf_t source = {0};
  IdlContract *contract = NULL;
  IdlDiagnostic diagnostic = IDL_DIAGNOSTIC_INIT;
  databind_compiler_projection_config config = {0};
  char *json = NULL;
  tstr staging = NULL;
  FILE *file = NULL;
  int staging_created = 0;
  int status = -1;
  size_t length = 0u;
  cmeta_uuid_t uuid;
  char uuid_text[37];
  cmeta_fs_replace_state_t publication = SALTS_FS_REPLACE_NOT_PUBLISHED;
  if (transports != NULL) {
    if (strcmp(transports, "http") == 0) selected = DATABIND_SCHEMA_PROJECTION_HTTP;
    if (strcmp(transports, "rpc") == 0) selected = DATABIND_SCHEMA_PROJECTION_RPC;
    if (strcmp(transports, "http,rpc") == 0 || strcmp(transports, "rpc,http") == 0)
      selected = DATABIND_SCHEMA_PROJECTION_HTTP | DATABIND_SCHEMA_PROJECTION_RPC;
  }
  /* Different required extensions also prevent accidental in-place source
   * replacement, including Windows case/path aliases of the same filename. */
  if (!suffix(schema_path, ".schema") || !suffix(output_path, ".json") || selected == 0u)
    return fail(error, error_size, "Expected .schema input, .json output and transports http and/or rpc");
  if (cmeta_fs_read_file(schema_path, &source) != SALTS_OK)
    return fail(error, error_size, "Could not read schema");
  if (!idl_contract_parse(source.base, source.len, &contract, &diagnostic)) {
    fail(error, error_size, diagnostic.message);
    goto done;
  }
  if (databind_compiler_schema_projection_build(contract, selected, &config, error, error_size) != 0)
    goto done;
  json = json_serialize_pretty(config.json_root, &length);
  if (json == NULL || cmeta_uuid_v4_generate(&uuid) != SALTS_OK ||
      cmeta_uuid_format(&uuid, uuid_text, sizeof(uuid_text)) != SALTS_OK) goto io_error;
  staging = tstr_format("{}.{}.tmp", output_path, uuid_text);
  if (staging == NULL || (file = fopen(staging, "wbx")) == NULL) goto io_error;
  staging_created = 1;
  if (fwrite(json, 1u, length, file) != length) goto io_error;
  int close_status = fclose(file);
  file = NULL;
  if (close_status != 0) goto io_error;
  if (cmeta_fs_replace_durable(staging, output_path, &publication) != SALTS_OK) {
    fail(error, error_size, publication == SALTS_FS_REPLACE_NOT_PUBLISHED
        ? "Could not publish projection JSON" : "Projection JSON published; durability is unknown");
    goto done;
  }
  status = 0;
  goto done;
io_error:
  fail(error, error_size, "Could not stage projection JSON");
done:
  if (file != NULL) fclose(file);
  if (staging_created && publication == SALTS_FS_REPLACE_NOT_PUBLISHED &&
      cmeta_fs_unlink(staging) != SALTS_OK)
    fail(error, error_size, "Could not remove unpublished projection JSON staging file");
  tstr_free(staging);
  json_serialize_free(json);
  databind_compiler_projection_config_dispose(&config);
  idl_contract_destroy(contract);
  cmeta_fs_buf_free(&source);
  return status;
}
