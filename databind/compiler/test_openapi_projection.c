#include "compiler_core.h"
#include "openapi_projection.h"
#include "projection.h"

#include "json_parser.h"
#include "salts_fs.h"
#include "tinytest.h"

#include <string.h>

#ifndef OPENAPI_SCHEMA
#error "OPENAPI_SCHEMA is required"
#endif
#ifndef OPENAPI_OPTIONAL_PATH_SCHEMA
#error "OPENAPI_OPTIONAL_PATH_SCHEMA is required"
#endif
#ifndef OPENAPI_BYTES_SIZE_SCHEMA
#error "OPENAPI_BYTES_SIZE_SCHEMA is required"
#endif

static json_value_t *object_get(
    const json_value_t *object, const char *name) {
  json_value_t *value = json_object_get(object, name);
  check_not_null(value);
  return value;
}

static json_value_t *schema_property(
    const json_value_t *root, const char *schema, const char *field) {
  json_value_t *components = object_get(root, "components");
  json_value_t *schemas = object_get(components, "schemas");
  json_value_t *owner = object_get(schemas, schema);
  json_value_t *properties = object_get(owner, "properties");
  return object_get(properties, field);
}

static int generate_openapi(
    const char *schema_path,
    const char *output,
    const databind_compiler_http_projection_config *http) {
  Node *root = NULL;
  char *schema_data = NULL;
  databind_compiler_openapi_projection_config config = {http};
  databind_compiler_projection_request request = {
      .id = {DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT,
             DATABIND_COMPILER_ARTIFACT_OPENAPI},
      .output = output,
      .config = &config};
  databind_compiler_projection_backend backend =
      DATABIND_COMPILER_OPENAPI_BACKEND;
  int result;

  (void)salts_fs_unlink(output);
  if (tbe_compiler_parse_schema_file(
          schema_path, &root, &schema_data) != 0)
    return -2;
  result = databind_compiler_projection_run(
      root, &request, 1u, &backend, 1u);
  node_free(root);
  free(schema_data);
  return result;
}

spec("DataBind OpenAPI projection") {
  it("derives OpenAPI 3.1 from canonical Service HTTP and constraints") {
    static const char output[] = "test_openapi_projection_output.json";
    static const databind_compiler_http_operation_config operations[] = {{
        .service_name = "Users",
        .operation_name = "Create",
        .method = "PUT",
        .route = "/users/{id}",
        .success_status = 201,
        .context_flags = 0u,
        .ingress_format = DATA_BIND_FORMAT_JSON,
        .egress_format = DATA_BIND_FORMAT_JSON}};
    static const databind_compiler_http_field_config fields[] = {
        {"Users", "Create", DATABIND_COMPILER_PROJECTION_INGRESS,
         "id", DATABIND_COMPILER_HTTP_PATH, "id", SIZE_MAX},
        {"Users", "Create", DATABIND_COMPILER_PROJECTION_INGRESS,
         "nickname", DATABIND_COMPILER_HTTP_HEADER, "X-Nickname", SIZE_MAX},
        {"Users", "Create", DATABIND_COMPILER_PROJECTION_INGRESS,
         "locale", DATABIND_COMPILER_HTTP_QUERY, "lang", SIZE_MAX},
        {"Users", "Create", DATABIND_COMPILER_PROJECTION_EGRESS,
         "etag", DATABIND_COMPILER_HTTP_RESPONSE_HEADER, "ETag", SIZE_MAX}};
    static const databind_compiler_http_error_config errors[] = {{
        "Users", "Create", "NotFoundError", 404}};
    const databind_compiler_http_projection_config http = {
        .symbol_prefix = "openapi_fixture",
        .operations = operations,
        .operation_count = 1u,
        .fields = fields,
        .field_count = sizeof(fields) / sizeof(fields[0]),
        .errors = errors,
        .error_count = 1u};
    json_value_t *root;
    json_value_t *paths;
    json_value_t *route;
    json_value_t *put;
    json_value_t *parameters;
    json_value_t *request_body;
    json_value_t *content;
    json_value_t *media;
    json_value_t *body_schema;
    json_value_t *properties;
    json_value_t *name_schema;
    json_value_t *responses;
    json_value_t *success;
    json_value_t *headers;
    json_value_t *not_found;
    json_value_t *not_found_media;
    json_value_t *not_found_schema;
    json_value_t *id_schema;
    json_value_t *locale_schema;
    json_value_t *nickname_schema;
    json_value_t *nickname_type;
    json_value_t *required;
    size_t i;

    check_equal(generate_openapi(OPENAPI_SCHEMA, output, &http), 0);
    root = json_parse_file(output);
    check_not_null(root);
    if (root == NULL) return;

    check_equal(json_get_string(root, "openapi"), "3.1.0");

    paths = object_get(root, "paths");
    route = object_get(paths, "/users/{id}");
    put = object_get(route, "put");
    check_equal(json_get_string(put, "operationId"), "Users.Create");

    parameters = object_get(put, "parameters");
    check_equal(json_array_size(parameters), (size_t)3u);
    id_schema = locale_schema = nickname_schema = NULL;
    for (i = 0u; i < json_array_size(parameters); ++i) {
      json_value_t *parameter = json_array_get(parameters, i);
      const char *name = json_get_string(parameter, "name");
      const char *where = json_get_string(parameter, "in");
      json_value_t *field_schema = object_get(parameter, "schema");
      if (name != NULL && strcmp(name, "id") == 0) {
        check_equal(where, "path");
        check_true(json_get_bool(parameter, "required", false));
        id_schema = field_schema;
      } else if (name != NULL && strcmp(name, "lang") == 0) {
        check_equal(where, "query");
        check_false(json_get_bool(parameter, "required", true));
        locale_schema = field_schema;
      } else if (name != NULL && strcmp(name, "X-Nickname") == 0) {
        check_equal(where, "header");
        check_false(json_get_bool(parameter, "required", true));
        nickname_schema = field_schema;
      }
    }
    check_not_null(id_schema);
    check_not_null(locale_schema);
    check_not_null(nickname_schema);
    if (id_schema != NULL)
      check_equal(json_get_double(id_schema, "minimum", -1.0), 1.0);
    if (locale_schema != NULL)
      check_equal(json_get_string(locale_schema, "default"), "en");
    if (nickname_schema != NULL) {
      nickname_type = object_get(nickname_schema, "type");
      check_equal(json_array_size(nickname_type), (size_t)2u);
      check_equal(json_string(json_array_get(nickname_type, 0u)), "string");
      check_equal(json_string(json_array_get(nickname_type, 1u)), "null");
    }

    request_body = object_get(put, "requestBody");
    check_true(json_get_bool(request_body, "required", false));
    content = object_get(request_body, "content");
    media = object_get(content, "application/json");
    body_schema = object_get(media, "schema");
    properties = object_get(body_schema, "properties");
    name_schema = object_get(properties, "name");
    check_equal(json_get_double(name_schema, "minLength", -1.0), 2.0);
    check_equal(json_get_double(name_schema, "maxLength", -1.0), 16.0);
    check_equal(json_get_string(name_schema, "pattern"), "^[a-z]+$");
    required = object_get(body_schema, "required");
    check_equal(json_array_size(required), (size_t)1u);
    check_equal(json_string(json_array_get(required, 0u)), "name");

    responses = object_get(put, "responses");
    success = object_get(responses, "201");
    headers = object_get(success, "headers");
    check_not_null(object_get(headers, "ETag"));
    not_found = object_get(responses, "404");
    content = object_get(not_found, "content");
    not_found_media = object_get(content, "application/json");
    not_found_schema = object_get(not_found_media, "schema");
    check_equal(
        json_get_string(not_found_schema, "$ref"),
        "#/components/schemas/NotFoundError");

    {
      json_value_t *resource =
          schema_property(root, "NotFoundError", "resource");
      check_equal(json_get_double(resource, "maxLength", -1.0), 32.0);
    }

    json_free(root);
    (void)salts_fs_unlink(output);
  }

  it("fails closed for an optional path parameter") {
    static const char output[] = "test_openapi_optional_path_should_not_exist.json";
    static const databind_compiler_http_operation_config operations[] = {{
        .service_name = "Users",
        .operation_name = "Read",
        .method = "GET",
        .route = "/users/{id}",
        .success_status = 200,
        .ingress_format = DATA_BIND_FORMAT_JSON,
        .egress_format = DATA_BIND_FORMAT_JSON}};
    static const databind_compiler_http_field_config fields[] = {{
        "Users", "Read", DATABIND_COMPILER_PROJECTION_INGRESS,
        "id", DATABIND_COMPILER_HTTP_PATH, "id", SIZE_MAX}};
    const databind_compiler_http_projection_config http = {
        .operations = operations, .operation_count = 1u,
        .fields = fields, .field_count = 1u};

    check_equal(
        generate_openapi(OPENAPI_OPTIONAL_PATH_SCHEMA, output, &http), -1);
    check(salts_fs_access(output, SALTS_FS_ACCESS_EXISTS) != 0);
  }

  it("fails closed when decoded bytes Size cannot be represented losslessly") {
    static const char output[] = "test_openapi_bytes_size_should_not_exist.json";
    static const databind_compiler_http_operation_config operations[] = {{
        .service_name = "Blobs",
        .operation_name = "Put",
        .method = "POST",
        .route = "/blobs",
        .success_status = 200,
        .ingress_format = DATA_BIND_FORMAT_JSON,
        .egress_format = DATA_BIND_FORMAT_JSON}};
    const databind_compiler_http_projection_config http = {
        .operations = operations, .operation_count = 1u};

    check_equal(
        generate_openapi(OPENAPI_BYTES_SIZE_SCHEMA, output, &http), -1);
    check(salts_fs_access(output, SALTS_FS_ACCESS_EXISTS) != 0);
  }
}
