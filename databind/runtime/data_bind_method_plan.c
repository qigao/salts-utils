#include "data_bind_method_plan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DATA_BIND_HTTP_CONTEXT_MASK \
  (DATA_BIND_HTTP_CONTEXT_DEADLINE | DATA_BIND_HTTP_CONTEXT_CANCELLATION | \
   DATA_BIND_HTTP_CONTEXT_REQUEST_ID | DATA_BIND_HTTP_CONTEXT_TRACE | \
   DATA_BIND_HTTP_CONTEXT_PRINCIPAL | DATA_BIND_HTTP_CONTEXT_METADATA)

typedef struct HttpProjectionState {
  const DataBindHttpProjectionConfig *config;
  unsigned char *used;
} HttpProjectionState;

typedef struct RpcProjectionState {
  const DataBindRpcProjectionConfig *config;
  unsigned char *used;
} RpcProjectionState;

struct DataBindHttpMethodPlan {
  char *method;
  char *route;
  int success_status;
  uint64_t context_flags;
  DataBindBindingPlan *binding;
  DataBindHttpErrorMapping *errors;
  size_t error_count;
};

struct DataBindRpcMethodPlan {
  char *wire_method;
  DataBindBindingPlan *binding;
  DataBindRpcErrorMapping *errors;
  size_t error_count;
};

static char *method_plan_strdup(const char *text) {
  size_t length;
  char *copy;
  if (text == NULL) return NULL;
  length = strlen(text);
  if (length == SIZE_MAX) return NULL;
  copy = (char *)malloc(length + 1u);
  if (copy == NULL) return NULL;
  memcpy(copy, text, length + 1u);
  return copy;
}

static char *method_plan_join(
    const char *prefix, const char *service,
    const char *separator, const char *operation) {
  size_t a, b, c, d;
  char *out;
  if (prefix == NULL || service == NULL || separator == NULL ||
      operation == NULL)
    return NULL;
  a = strlen(prefix);
  b = strlen(service);
  c = strlen(separator);
  d = strlen(operation);
  if (a > SIZE_MAX - b || a + b > SIZE_MAX - c ||
      a + b + c > SIZE_MAX - d - 1u)
    return NULL;
  out = (char *)malloc(a + b + c + d + 1u);
  if (out == NULL) return NULL;
  memcpy(out, prefix, a);
  memcpy(out + a, service, b);
  memcpy(out + a + b, separator, c);
  memcpy(out + a + b + c, operation, d + 1u);
  return out;
}

static size_t method_plan_out_size(size_t requested, size_t full) {
  return requested != 0u && requested < full ? requested : full;
}

static DataBindStatus method_plan_fail(
    DataBindBindingPlanDiagnostic *diagnostic,
    DataBindStatus status, const char *message) {
  size_t size;
  if (diagnostic == NULL) return status;
  size = method_plan_out_size(diagnostic->size, sizeof(*diagnostic));
  memset(diagnostic, 0, size);
  if (size >= sizeof(size_t)) diagnostic->size = size;
  if (size >= offsetof(DataBindBindingPlanDiagnostic, status) +
                  sizeof(diagnostic->status))
    diagnostic->status = status;
  if (size >= offsetof(DataBindBindingPlanDiagnostic, message) +
                  sizeof(diagnostic->message))
    snprintf(diagnostic->message, sizeof(diagnostic->message), "%s",
             message != NULL ? message : "DataBind MethodPlan failure");
  return status;
}

static int http_method_valid(const char *method) {
  static const char *const methods[] = {
      "GET", "HEAD", "POST", "PUT", "DELETE",
      "CONNECT", "OPTIONS", "TRACE", "PATCH"
  };
  size_t i;
  if (method == NULL || method[0] == '\0') return 0;
  for (i = 0u; i < sizeof(methods) / sizeof(methods[0]); ++i)
    if (strcmp(method, methods[i]) == 0) return 1;
  return 0;
}

static int http_location_valid(
    DataBindBindingDirection direction, DataBindHttpFieldLocation location) {
  if (direction == DATA_BIND_BINDING_INGRESS)
    return location == DATA_BIND_HTTP_PATH ||
           location == DATA_BIND_HTTP_QUERY ||
           location == DATA_BIND_HTTP_HEADER ||
           location == DATA_BIND_HTTP_COOKIE ||
           location == DATA_BIND_HTTP_BODY;
  if (direction == DATA_BIND_BINDING_EGRESS)
    return location == DATA_BIND_HTTP_RESPONSE_HEADER ||
           location == DATA_BIND_HTTP_RESPONSE_BODY;
  return 0;
}

static int http_config_valid(const DataBindHttpProjectionConfig *config) {
  size_t i, j;
  if (config == NULL) return 1;
  if (config->size < sizeof(*config) ||
      config->abi_version != DATA_BIND_METHOD_PLAN_ABI_VERSION ||
      (config->field_count != 0u && config->fields == NULL) ||
      (config->error_count != 0u && config->errors == NULL) ||
      config->success_status < 100 || config->success_status > 599 ||
      (config->context_flags & ~DATA_BIND_HTTP_CONTEXT_MASK) != 0u)
    return 0;
  if (config->method != NULL && !http_method_valid(config->method))
    return 0;
  for (i = 0u; i < config->field_count; ++i) {
    const DataBindHttpFieldProjection *left = &config->fields[i];
    if (left->size < sizeof(*left) || left->schema_field == NULL ||
        left->schema_field[0] == '\0' ||
        !http_location_valid(left->direction, left->location) ||
        (left->wire_name != NULL && left->wire_name[0] == '\0'))
      return 0;
    for (j = 0u; j < i; ++j) {
      const DataBindHttpFieldProjection *right = &config->fields[j];
      if (left->direction == right->direction &&
          strcmp(left->schema_field, right->schema_field) == 0)
        return 0;
    }
  }
  for (i = 0u; i < config->error_count; ++i) {
    const DataBindHttpErrorMapping *left = &config->errors[i];
    if (left->size < sizeof(*left) || left->error_type == NULL ||
        left->error_type[0] == '\0' || left->status < 100 ||
        left->status > 599)
      return 0;
    for (j = 0u; j < i; ++j)
      if (strcmp(left->error_type, config->errors[j].error_type) == 0)
        return 0;
  }
  return 1;
}

static int rpc_config_valid(const DataBindRpcProjectionConfig *config) {
  size_t i, j;
  if (config == NULL) return 1;
  if (config->size < sizeof(*config) ||
      config->abi_version != DATA_BIND_METHOD_PLAN_ABI_VERSION ||
      (config->field_count != 0u && config->fields == NULL) ||
      (config->error_count != 0u && config->errors == NULL) ||
      (config->wire_method != NULL && config->wire_method[0] == '\0'))
    return 0;
  for (i = 0u; i < config->field_count; ++i) {
    const DataBindRpcFieldProjection *left = &config->fields[i];
    if (left->size < sizeof(*left) ||
        (left->direction != DATA_BIND_BINDING_INGRESS &&
         left->direction != DATA_BIND_BINDING_EGRESS) ||
        left->schema_field == NULL || left->schema_field[0] == '\0' ||
        (left->wire_name != NULL && left->wire_name[0] == '\0'))
      return 0;
    for (j = 0u; j < i; ++j) {
      const DataBindRpcFieldProjection *right = &config->fields[j];
      if (left->direction == right->direction &&
          strcmp(left->schema_field, right->schema_field) == 0)
        return 0;
    }
  }
  for (i = 0u; i < config->error_count; ++i) {
    const DataBindRpcErrorMapping *left = &config->errors[i];
    if (left->size < sizeof(*left) || left->error_type == NULL ||
        left->error_type[0] == '\0' || left->code == 0)
      return 0;
    for (j = 0u; j < i; ++j)
      if (strcmp(left->error_type, config->errors[j].error_type) == 0)
        return 0;
  }
  return 1;
}

static const DataBindHttpFieldProjection *http_field_mapping(
    HttpProjectionState *state, DataBindBindingDirection direction,
    const char *field_name, size_t *index) {
  size_t i;
  if (index != NULL) *index = SIZE_MAX;
  if (state == NULL || state->config == NULL || field_name == NULL) return NULL;
  for (i = 0u; i < state->config->field_count; ++i) {
    const DataBindHttpFieldProjection *mapping = &state->config->fields[i];
    if (mapping->direction == direction &&
        strcmp(mapping->schema_field, field_name) == 0) {
      if (index != NULL) *index = i;
      return mapping;
    }
  }
  return NULL;
}

static const DataBindRpcFieldProjection *rpc_field_mapping(
    RpcProjectionState *state, DataBindBindingDirection direction,
    const char *field_name, size_t *index) {
  size_t i;
  if (index != NULL) *index = SIZE_MAX;
  if (state == NULL || state->config == NULL || field_name == NULL) return NULL;
  for (i = 0u; i < state->config->field_count; ++i) {
    const DataBindRpcFieldProjection *mapping = &state->config->fields[i];
    if (mapping->direction == direction &&
        strcmp(mapping->schema_field, field_name) == 0) {
      if (index != NULL) *index = i;
      return mapping;
    }
  }
  return NULL;
}

static DataBindStatus http_project_field(
    void *context, const DataBindServiceOperation *operation,
    const DataBindSchemaField *field, DataBindBindingDirection direction,
    DataBindBindingAddress *out, DataBindError *error) {
  HttpProjectionState *state = (HttpProjectionState *)context;
  const DataBindHttpFieldProjection *mapping;
  const char *space = NULL;
  const char *name;
  size_t mapping_index = SIZE_MAX;
  (void)operation;
  (void)error;

  if (field == NULL || field->name == NULL || out == NULL)
    return DATA_BIND_ERR_INVALID_ARG;

  mapping = http_field_mapping(state, direction, field->name, &mapping_index);
  *out = (DataBindBindingAddress)DATA_BIND_BINDING_ADDRESS_INIT;

  if (mapping == NULL) {
    if (direction == DATA_BIND_BINDING_INGRESS) {
      out->binding_class = DATA_BIND_BINDING_PAYLOAD;
      space = "http.body";
    } else {
      out->binding_class = DATA_BIND_BINDING_RESULT;
      space = "http.response.body";
    }
    name = field->name;
  } else {
    if (state->used != NULL && mapping_index != SIZE_MAX)
      state->used[mapping_index] = 1u;
    name = mapping->wire_name != NULL ? mapping->wire_name : field->name;
    out->ordinal = mapping->ordinal;
    switch (mapping->location) {
    case DATA_BIND_HTTP_PATH:
      out->binding_class = DATA_BIND_BINDING_VALUE;
      space = "http.path";
      break;
    case DATA_BIND_HTTP_QUERY:
      out->binding_class = DATA_BIND_BINDING_VALUE;
      space = "http.query";
      break;
    case DATA_BIND_HTTP_HEADER:
      out->binding_class = DATA_BIND_BINDING_METADATA;
      space = "http.header";
      break;
    case DATA_BIND_HTTP_COOKIE:
      out->binding_class = DATA_BIND_BINDING_METADATA;
      space = "http.cookie";
      break;
    case DATA_BIND_HTTP_BODY:
      out->binding_class = DATA_BIND_BINDING_PAYLOAD;
      space = "http.body";
      break;
    case DATA_BIND_HTTP_RESPONSE_HEADER:
      out->binding_class = DATA_BIND_BINDING_METADATA;
      space = "http.response.header";
      break;
    case DATA_BIND_HTTP_RESPONSE_BODY:
      out->binding_class = DATA_BIND_BINDING_RESULT;
      space = "http.response.body";
      break;
    default:
      return DATA_BIND_ERR_SCHEMA;
    }
  }

  out->space = space;
  out->name = name;
  return DATA_BIND_OK;
}

static DataBindStatus rpc_project_field(
    void *context, const DataBindServiceOperation *operation,
    const DataBindSchemaField *field, DataBindBindingDirection direction,
    DataBindBindingAddress *out, DataBindError *error) {
  RpcProjectionState *state = (RpcProjectionState *)context;
  const DataBindRpcFieldProjection *mapping;
  size_t mapping_index = SIZE_MAX;
  (void)operation;
  (void)error;

  if (field == NULL || field->name == NULL || out == NULL)
    return DATA_BIND_ERR_INVALID_ARG;

  mapping = rpc_field_mapping(state, direction, field->name, &mapping_index);
  *out = (DataBindBindingAddress)DATA_BIND_BINDING_ADDRESS_INIT;
  if (mapping != NULL) {
    if (state->used != NULL && mapping_index != SIZE_MAX)
      state->used[mapping_index] = 1u;
    out->name = mapping->wire_name != NULL ? mapping->wire_name : field->name;
    out->ordinal = mapping->ordinal;
  } else {
    out->name = field->name;
  }

  if (direction == DATA_BIND_BINDING_INGRESS) {
    out->binding_class = DATA_BIND_BINDING_VALUE;
    out->space = "rpc.params";
  } else {
    out->binding_class = DATA_BIND_BINDING_RESULT;
    out->space = "rpc.result";
  }
  return DATA_BIND_OK;
}

static int config_fields_consumed(const unsigned char *used, size_t count) {
  size_t i;
  for (i = 0u; i < count; ++i)
    if (used == NULL || !used[i]) return 0;
  return 1;
}

static size_t route_placeholder_count(const char *route, const char *name) {
  const char *p;
  size_t count = 0u;
  size_t length;
  if (route == NULL || name == NULL || name[0] == '\0') return 0u;
  length = strlen(name);
  p = route;
  while ((p = strchr(p, '{')) != NULL) {
    const char *end = strchr(p + 1, '}');
    if (end == NULL) return 0u;
    if ((size_t)(end - p - 1) == length &&
        memcmp(p + 1, name, length) == 0)
      ++count;
    p = end + 1;
  }
  return count;
}

static int http_route_valid(
    const char *route, const DataBindHttpProjectionConfig *config) {
  const char *p;
  size_t i;
  if (route == NULL || route[0] != '/' ||
      strchr(route, '?') != NULL || strchr(route, '#') != NULL)
    return 0;

  for (i = 0u; config != NULL && i < config->field_count; ++i) {
    const DataBindHttpFieldProjection *mapping = &config->fields[i];
    if (mapping->direction == DATA_BIND_BINDING_INGRESS &&
        mapping->location == DATA_BIND_HTTP_PATH) {
      const char *wire = mapping->wire_name != NULL
                             ? mapping->wire_name
                             : mapping->schema_field;
      if (route_placeholder_count(route, wire) != 1u) return 0;
    }
  }

  p = route;
  while ((p = strchr(p, '{')) != NULL) {
    const char *end = strchr(p + 1, '}');
    size_t length;
    size_t matches = 0u;
    if (end == NULL || end == p + 1) return 0;
    length = (size_t)(end - p - 1);
    for (i = 0u; config != NULL && i < config->field_count; ++i) {
      const DataBindHttpFieldProjection *mapping = &config->fields[i];
      const char *wire;
      if (mapping->direction != DATA_BIND_BINDING_INGRESS ||
          mapping->location != DATA_BIND_HTTP_PATH)
        continue;
      wire = mapping->wire_name != NULL ? mapping->wire_name
                                        : mapping->schema_field;
      if (strlen(wire) == length && memcmp(wire, p + 1, length) == 0)
        ++matches;
    }
    if (matches != 1u) return 0;
    p = end + 1;
  }
  return 1;
}

static int http_error_status(
    const DataBindHttpProjectionConfig *config, const char *error_type,
    int *found) {
  size_t i;
  if (found != NULL) *found = 0;
  for (i = 0u; config != NULL && i < config->error_count; ++i) {
    if (strcmp(config->errors[i].error_type, error_type) == 0) {
      if (found != NULL) *found = 1;
      return config->errors[i].status;
    }
  }
  return 500;
}

static int rpc_error_code(
    const DataBindRpcProjectionConfig *config, const char *error_type,
    int *found) {
  size_t i;
  if (found != NULL) *found = 0;
  for (i = 0u; config != NULL && i < config->error_count; ++i) {
    if (strcmp(config->errors[i].error_type, error_type) == 0) {
      if (found != NULL) *found = 1;
      return config->errors[i].code;
    }
  }
  return -32000;
}

static int http_errors_build(
    DataBindHttpMethodPlan *plan, const DataBindHttpProjectionConfig *config) {
  size_t count = data_bind_binding_plan_error_count(plan->binding);
  size_t i, j;
  unsigned char *used = NULL;
  plan->error_count = count;
  if (count != 0u) {
    plan->errors = (DataBindHttpErrorMapping *)calloc(count, sizeof(*plan->errors));
    if (plan->errors == NULL) return 0;
  }
  if (config != NULL && config->error_count != 0u) {
    used = (unsigned char *)calloc(config->error_count, 1u);
    if (used == NULL) return 0;
  }
  for (i = 0u; i < count; ++i) {
    const char *error_type = data_bind_binding_plan_error_at(plan->binding, i);
    int found = 0;
    plan->errors[i] = (DataBindHttpErrorMapping)DATA_BIND_HTTP_ERROR_MAPPING_INIT;
    plan->errors[i].error_type = error_type;
    plan->errors[i].status = http_error_status(config, error_type, &found);
    if (found)
      for (j = 0u; j < config->error_count; ++j)
        if (strcmp(config->errors[j].error_type, error_type) == 0)
          used[j] = 1u;
  }
  if (!config_fields_consumed(used, config != NULL ? config->error_count : 0u)) {
    free(used);
    return 0;
  }
  free(used);
  return 1;
}

static int rpc_errors_build(
    DataBindRpcMethodPlan *plan, const DataBindRpcProjectionConfig *config) {
  size_t count = data_bind_binding_plan_error_count(plan->binding);
  size_t i, j;
  unsigned char *used = NULL;
  plan->error_count = count;
  if (count != 0u) {
    plan->errors = (DataBindRpcErrorMapping *)calloc(count, sizeof(*plan->errors));
    if (plan->errors == NULL) return 0;
  }
  if (config != NULL && config->error_count != 0u) {
    used = (unsigned char *)calloc(config->error_count, 1u);
    if (used == NULL) return 0;
  }
  for (i = 0u; i < count; ++i) {
    const char *error_type = data_bind_binding_plan_error_at(plan->binding, i);
    int found = 0;
    plan->errors[i] = (DataBindRpcErrorMapping)DATA_BIND_RPC_ERROR_MAPPING_INIT;
    plan->errors[i].error_type = error_type;
    plan->errors[i].code = rpc_error_code(config, error_type, &found);
    if (found)
      for (j = 0u; j < config->error_count; ++j)
        if (strcmp(config->errors[j].error_type, error_type) == 0)
          used[j] = 1u;
  }
  if (!config_fields_consumed(used, config != NULL ? config->error_count : 0u)) {
    free(used);
    return 0;
  }
  free(used);
  return 1;
}

static int artifact_name_equal(
    const char *left, const char *right) {
  return left != NULL && right != NULL && strcmp(left, right) == 0;
}

const DataBindHttpProjectionConfig *
data_bind_http_projection_artifact_find(
    const DataBindHttpProjectionArtifact *artifact,
    const char *service_name, const char *operation_name) {
  size_t i;
  if (artifact == NULL || artifact->size < sizeof(*artifact) ||
      artifact->abi_version != DATA_BIND_METHOD_PLAN_ABI_VERSION ||
      (artifact->entry_count != 0u && artifact->entries == NULL) ||
      service_name == NULL || operation_name == NULL)
    return NULL;
  for (i = 0u; i < artifact->entry_count; ++i) {
    const DataBindHttpProjectionArtifactEntry *entry = &artifact->entries[i];
    if (entry->size >= sizeof(*entry) &&
        artifact_name_equal(entry->service_name, service_name) &&
        artifact_name_equal(entry->operation_name, operation_name) &&
        http_config_valid(&entry->config))
      return &entry->config;
  }
  return NULL;
}

const DataBindRpcProjectionConfig *
data_bind_rpc_projection_artifact_find(
    const DataBindRpcProjectionArtifact *artifact,
    const char *service_name, const char *operation_name) {
  size_t i;
  if (artifact == NULL || artifact->size < sizeof(*artifact) ||
      artifact->abi_version != DATA_BIND_METHOD_PLAN_ABI_VERSION ||
      (artifact->entry_count != 0u && artifact->entries == NULL) ||
      service_name == NULL || operation_name == NULL)
    return NULL;
  for (i = 0u; i < artifact->entry_count; ++i) {
    const DataBindRpcProjectionArtifactEntry *entry = &artifact->entries[i];
    if (entry->size >= sizeof(*entry) &&
        artifact_name_equal(entry->service_name, service_name) &&
        artifact_name_equal(entry->operation_name, operation_name) &&
        rpc_config_valid(&entry->config))
      return &entry->config;
  }
  return NULL;
}

DataBindStatus data_bind_http_method_plan_compile_service(
    DataBind *codec, const char *service_name, const char *operation_name,
    const DataBindHttpProjectionConfig *config,
    const DataBindServiceNativeBinding *native,
    DataBindHttpMethodPlan **out_plan,
    DataBindBindingPlanDiagnostic *diagnostic) {
  DataBindHttpMethodPlan *plan = NULL;
  DataBindBindingProjection projection = DATA_BIND_BINDING_PROJECTION_INIT;
  HttpProjectionState state;
  unsigned char *used = NULL;
  char *default_route = NULL;
  const char *method = config != NULL && config->method != NULL
                           ? config->method : "POST";
  const char *route;
  DataBindStatus status;

  if (out_plan != NULL) *out_plan = NULL;
  if (codec == NULL || service_name == NULL || operation_name == NULL ||
      native == NULL || out_plan == NULL || !http_config_valid(config))
    return method_plan_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG,
                            "Invalid HTTP MethodPlan arguments");

  default_route = method_plan_join("/", service_name, "/", operation_name);
  route = config != NULL && config->route != NULL ? config->route : default_route;
  if (default_route == NULL || !http_method_valid(method) ||
      !http_route_valid(route, config)) {
    free(default_route);
    return method_plan_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                            "Invalid HTTP method/route projection");
  }

  if (config != NULL && config->field_count != 0u) {
    used = (unsigned char *)calloc(config->field_count, 1u);
    if (used == NULL) {
      free(default_route);
      return method_plan_fail(diagnostic, DATA_BIND_ERR_OOM,
                              "Could not allocate HTTP projection state");
    }
  }
  state.config = config;
  state.used = used;
  projection.id = "http";
  projection.context = &state;
  projection.project_field = http_project_field;

  plan = (DataBindHttpMethodPlan *)calloc(1u, sizeof(*plan));
  if (plan == NULL) {
    free(used);
    free(default_route);
    return method_plan_fail(diagnostic, DATA_BIND_ERR_OOM,
                            "Could not allocate HTTP MethodPlan");
  }

  status = data_bind_binding_plan_compile_service(
      codec, service_name, operation_name, &projection, native,
      &plan->binding, diagnostic);
  if (status != DATA_BIND_OK) goto fail;

  if (!config_fields_consumed(
          used, config != NULL ? config->field_count : 0u)) {
    status = method_plan_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA,
        "HTTP projection references a field not owned by the Service operation");
    goto fail;
  }

  plan->method = method_plan_strdup(method);
  plan->route = method_plan_strdup(route);
  plan->success_status = config != NULL ? config->success_status : 200;
  plan->context_flags = config != NULL ? config->context_flags
                                       : DATA_BIND_HTTP_CONTEXT_NONE;
  if (plan->method == NULL || plan->route == NULL ||
      !http_errors_build(plan, config)) {
    status = method_plan_fail(diagnostic, DATA_BIND_ERR_OOM,
                              "Could not materialize HTTP MethodPlan");
    goto fail;
  }

  free(used);
  free(default_route);
  *out_plan = plan;
  return DATA_BIND_OK;

fail:
  free(used);
  free(default_route);
  data_bind_http_method_plan_free(plan);
  return status;
}

void data_bind_http_method_plan_free(DataBindHttpMethodPlan *plan) {
  if (plan == NULL) return;
  free(plan->method);
  free(plan->route);
  free(plan->errors);
  data_bind_binding_plan_free(plan->binding);
  free(plan);
}

const char *data_bind_http_method_plan_method(
    const DataBindHttpMethodPlan *plan) {
  return plan != NULL ? plan->method : NULL;
}

const char *data_bind_http_method_plan_route(
    const DataBindHttpMethodPlan *plan) {
  return plan != NULL ? plan->route : NULL;
}

int data_bind_http_method_plan_success_status(
    const DataBindHttpMethodPlan *plan) {
  return plan != NULL ? plan->success_status : 0;
}

uint64_t data_bind_http_method_plan_context_flags(
    const DataBindHttpMethodPlan *plan) {
  return plan != NULL ? plan->context_flags : 0u;
}

const DataBindBindingPlan *data_bind_http_method_plan_binding(
    const DataBindHttpMethodPlan *plan) {
  return plan != NULL ? plan->binding : NULL;
}

size_t data_bind_http_method_plan_error_count(
    const DataBindHttpMethodPlan *plan) {
  return plan != NULL ? plan->error_count : 0u;
}

int data_bind_http_method_plan_error_at(
    const DataBindHttpMethodPlan *plan, size_t index,
    DataBindHttpErrorMapping *out) {
  size_t size;
  if (plan == NULL || index >= plan->error_count || out == NULL ||
      out->size < sizeof(size_t))
    return 0;
  size = method_plan_out_size(out->size, sizeof(*out));
  memcpy(out, &plan->errors[index], size);
  out->size = size;
  return 1;
}

int data_bind_http_method_plan_status_for_outcome(
    const DataBindHttpMethodPlan *plan,
    const DataBindBindingOutcome *outcome,
    int *out_status) {
  size_t index;
  if (plan == NULL || outcome == NULL || out_status == NULL ||
      outcome->size <
          offsetof(DataBindBindingOutcome, typed_error) +
              sizeof(outcome->typed_error))
    return 0;

  if (outcome->kind == DATA_BIND_BINDING_OUTCOME_SUCCESS) {
    *out_status = plan->success_status;
    return 1;
  }

  if (outcome->kind != DATA_BIND_BINDING_OUTCOME_TYPED_ERROR)
    return 0;

  index = outcome->typed_error_index;
  if (index >= plan->error_count)
    return 0;
  if (outcome->typed_error != NULL &&
      strcmp(outcome->typed_error, plan->errors[index].error_type) != 0)
    return 0;

  *out_status = plan->errors[index].status;
  return 1;
}

DataBindStatus data_bind_rpc_method_plan_compile_service(
    DataBind *codec, const char *service_name, const char *operation_name,
    const DataBindRpcProjectionConfig *config,
    const DataBindServiceNativeBinding *native,
    DataBindRpcMethodPlan **out_plan,
    DataBindBindingPlanDiagnostic *diagnostic) {
  DataBindRpcMethodPlan *plan = NULL;
  DataBindBindingProjection projection = DATA_BIND_BINDING_PROJECTION_INIT;
  RpcProjectionState state;
  unsigned char *used = NULL;
  char *default_wire_method = NULL;
  const char *wire_method;
  DataBindStatus status;

  if (out_plan != NULL) *out_plan = NULL;
  if (codec == NULL || service_name == NULL || operation_name == NULL ||
      native == NULL || out_plan == NULL || !rpc_config_valid(config))
    return method_plan_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG,
                            "Invalid RPC MethodPlan arguments");

  default_wire_method =
      method_plan_join("", service_name, ".", operation_name);
  wire_method = config != NULL && config->wire_method != NULL
                    ? config->wire_method : default_wire_method;
  if (default_wire_method == NULL || wire_method == NULL ||
      wire_method[0] == '\0') {
    free(default_wire_method);
    return method_plan_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                            "Invalid RPC wire method projection");
  }

  if (config != NULL && config->field_count != 0u) {
    used = (unsigned char *)calloc(config->field_count, 1u);
    if (used == NULL) {
      free(default_wire_method);
      return method_plan_fail(diagnostic, DATA_BIND_ERR_OOM,
                              "Could not allocate RPC projection state");
    }
  }
  state.config = config;
  state.used = used;
  projection.id = "rpc";
  projection.context = &state;
  projection.project_field = rpc_project_field;

  plan = (DataBindRpcMethodPlan *)calloc(1u, sizeof(*plan));
  if (plan == NULL) {
    free(used);
    free(default_wire_method);
    return method_plan_fail(diagnostic, DATA_BIND_ERR_OOM,
                            "Could not allocate RPC MethodPlan");
  }

  status = data_bind_binding_plan_compile_service(
      codec, service_name, operation_name, &projection, native,
      &plan->binding, diagnostic);
  if (status != DATA_BIND_OK) goto fail;

  if (!config_fields_consumed(
          used, config != NULL ? config->field_count : 0u)) {
    status = method_plan_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA,
        "RPC projection references a field not owned by the Service operation");
    goto fail;
  }

  plan->wire_method = method_plan_strdup(wire_method);
  if (plan->wire_method == NULL || !rpc_errors_build(plan, config)) {
    status = method_plan_fail(diagnostic, DATA_BIND_ERR_OOM,
                              "Could not materialize RPC MethodPlan");
    goto fail;
  }

  free(used);
  free(default_wire_method);
  *out_plan = plan;
  return DATA_BIND_OK;

fail:
  free(used);
  free(default_wire_method);
  data_bind_rpc_method_plan_free(plan);
  return status;
}

void data_bind_rpc_method_plan_free(DataBindRpcMethodPlan *plan) {
  if (plan == NULL) return;
  free(plan->wire_method);
  free(plan->errors);
  data_bind_binding_plan_free(plan->binding);
  free(plan);
}

const char *data_bind_rpc_method_plan_wire_method(
    const DataBindRpcMethodPlan *plan) {
  return plan != NULL ? plan->wire_method : NULL;
}

const DataBindBindingPlan *data_bind_rpc_method_plan_binding(
    const DataBindRpcMethodPlan *plan) {
  return plan != NULL ? plan->binding : NULL;
}

size_t data_bind_rpc_method_plan_error_count(
    const DataBindRpcMethodPlan *plan) {
  return plan != NULL ? plan->error_count : 0u;
}

int data_bind_rpc_method_plan_error_at(
    const DataBindRpcMethodPlan *plan, size_t index,
    DataBindRpcErrorMapping *out) {
  size_t size;
  if (plan == NULL || index >= plan->error_count || out == NULL ||
      out->size < sizeof(size_t))
    return 0;
  size = method_plan_out_size(out->size, sizeof(*out));
  memcpy(out, &plan->errors[index], size);
  out->size = size;
  return 1;
}

int data_bind_rpc_method_plan_code_for_outcome(
    const DataBindRpcMethodPlan *plan,
    const DataBindBindingOutcome *outcome,
    int *out_code) {
  size_t index;
  if (plan == NULL || outcome == NULL || out_code == NULL ||
      outcome->size <
          offsetof(DataBindBindingOutcome, typed_error) +
              sizeof(outcome->typed_error))
    return 0;

  if (outcome->kind == DATA_BIND_BINDING_OUTCOME_SUCCESS) {
    *out_code = 0;
    return 1;
  }

  if (outcome->kind != DATA_BIND_BINDING_OUTCOME_TYPED_ERROR)
    return 0;

  index = outcome->typed_error_index;
  if (index >= plan->error_count)
    return 0;
  if (outcome->typed_error != NULL &&
      strcmp(outcome->typed_error, plan->errors[index].error_type) != 0)
    return 0;

  *out_code = plan->errors[index].code;
  return 1;
}
