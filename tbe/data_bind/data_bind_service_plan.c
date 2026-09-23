#include "data_bind_service_plan.h"

#include <cmeta/type_traits.h>

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef struct DataBindServicePlanEntryOwned {
  DataBindServicePlanEntry view;
  cserde_token default_token;
  int has_default_token;
} DataBindServicePlanEntryOwned;

struct DataBindServicePlan {
  char *service_name;
  char *operation_name;
  DataBindServiceProjection projection;
  const cmeta_function_desc *function;
  const TbeTypedDescriptor *request;
  const TbeTypedDescriptor *response;

  DataBindServicePlanEntryOwned *ingress;
  size_t ingress_count;
  DataBindServicePlanEntryOwned *egress;
  size_t egress_count;

  char **errors;
  size_t error_count;

  const cmeta_data_desc **param_data;
  unsigned char *param_ingress;
  unsigned char *param_egress;
  size_t param_count;

  size_t whole_request_param;
  size_t whole_response_param;
  int whole_response_return;
};

static size_t plan_out_size(size_t requested, size_t full_size) {
  return requested != 0u && requested < full_size ? requested : full_size;
}

static void plan_diag_clear(DataBindServicePlanDiagnostic *diagnostic) {
  size_t size;
  if (diagnostic == NULL) return;
  size = plan_out_size(diagnostic->size, sizeof(*diagnostic));
  memset(diagnostic, 0, size);
  if (size >= sizeof(size_t)) diagnostic->size = size;
  if (size >= offsetof(DataBindServicePlanDiagnostic, status) +
                  sizeof(diagnostic->status))
    diagnostic->status = DATA_BIND_OK;
}

static DataBindStatus plan_diag_fail(
    DataBindServicePlanDiagnostic *diagnostic, DataBindStatus status,
    const char *schema_field, const char *function_param,
    const char *fmt, ...) {
  va_list ap;
  size_t size;

  if (diagnostic == NULL) return status;
  size = plan_out_size(diagnostic->size, sizeof(*diagnostic));
  memset(diagnostic, 0, size);
  if (size >= sizeof(size_t)) diagnostic->size = size;
  if (size >= offsetof(DataBindServicePlanDiagnostic, status) +
                  sizeof(diagnostic->status))
    diagnostic->status = status;
  if (size >= offsetof(DataBindServicePlanDiagnostic, schema_field) +
                  sizeof(diagnostic->schema_field))
    snprintf(diagnostic->schema_field, sizeof(diagnostic->schema_field),
             "%s", schema_field != NULL ? schema_field : "");
  if (size >= offsetof(DataBindServicePlanDiagnostic, function_param) +
                  sizeof(diagnostic->function_param))
    snprintf(diagnostic->function_param, sizeof(diagnostic->function_param),
             "%s", function_param != NULL ? function_param : "");
  if (size >= offsetof(DataBindServicePlanDiagnostic, message) +
                  sizeof(diagnostic->message)) {
    va_start(ap, fmt);
    vsnprintf(diagnostic->message, sizeof(diagnostic->message), fmt, ap);
    va_end(ap);
  }
  return status;
}

static DataBindStatus plan_diag_from_error(
    DataBindServicePlanDiagnostic *diagnostic, DataBindStatus status,
    const char *schema_field, const char *function_param,
    const DataBindError *error, const char *fallback) {
  const char *message = fallback;
  if (error != NULL && error->size >=
                           offsetof(DataBindError, message) +
                               sizeof(error->message) &&
      error->message[0] != '\0')
    message = error->message;
  return plan_diag_fail(diagnostic, status, schema_field, function_param,
                        "%s", message != NULL ? message : "binding failure");
}

static char *plan_strdup(const char *text) {
  size_t len;
  char *copy;
  if (text == NULL) return NULL;
  len = strlen(text);
  if (len == SIZE_MAX) return NULL;
  copy = (char *)malloc(len + 1u);
  if (copy == NULL) return NULL;
  memcpy(copy, text, len + 1u);
  return copy;
}

static const cmeta_data_struct_shape *plan_struct_shape(
    const TbeTypedDescriptor *descriptor) {
  if (descriptor == NULL || descriptor->native_data == NULL ||
      descriptor->native_data->kind != CMETA_DATA_STRUCT ||
      descriptor->native_data->shape == NULL)
    return NULL;
  return (const cmeta_data_struct_shape *)descriptor->native_data->shape;
}

static const cmeta_data_field_desc *plan_native_field(
    const TbeTypedDescriptor *descriptor, const char *name) {
  const cmeta_data_struct_shape *shape = plan_struct_shape(descriptor);
  size_t i;
  if (shape == NULL || name == NULL) return NULL;
  for (i = 0u; i < shape->field_count; ++i) {
    if (shape->fields[i].name != NULL &&
        strcmp(shape->fields[i].name, name) == 0)
      return &shape->fields[i];
  }
  return NULL;
}

static const cmeta_type_desc *plan_param_value_type(
    const cmeta_param_desc *param, int *indirect) {
  if (indirect != NULL) *indirect = 0;
  if (param == NULL || param->type == NULL) return NULL;
  if (param->type->kind == CMETA_T_POINTER) {
    if (indirect != NULL) *indirect = 1;
    return param->type->pointee;
  }
  return param->type;
}

static size_t plan_param_index(
    const cmeta_function_desc *function, const char *name) {
  size_t i;
  if (function == NULL || name == NULL) return SIZE_MAX;
  for (i = 0u; i < function->param_count; ++i)
    if (function->params[i].name != NULL &&
        strcmp(function->params[i].name, name) == 0)
      return i;
  return SIZE_MAX;
}

static int plan_type_matches_data(const cmeta_type_desc *type,
                                  const cmeta_data_desc *data) {
  return type != NULL && data != NULL && data->storage_type != NULL &&
         cmeta_type_equal(type, data->storage_type);
}

static DataBindStatus plan_validate_descriptor(
    const TbeTypedDescriptor *descriptor, const char *expected_name,
    DataBindServicePlanDiagnostic *diagnostic) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status;

  if (descriptor == NULL || descriptor->overlay == NULL ||
      descriptor->native_data == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA, expected_name, NULL,
                          "Missing native descriptor for schema type '%s'",
                          expected_name != NULL ? expected_name : "<unknown>");

  status = tbe_typed_descriptor_validate(descriptor, &error);
  if (status != DATA_BIND_OK)
    return plan_diag_from_error(diagnostic, status, expected_name, NULL,
                                &error, "Invalid native descriptor");

  if (descriptor->overlay->name == NULL || expected_name == NULL ||
      strcmp(descriptor->overlay->name, expected_name) != 0)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, expected_name, NULL,
        "Native descriptor '%s' does not match service schema type '%s'",
        descriptor->overlay->name != NULL ? descriptor->overlay->name
                                         : "<unnamed>",
        expected_name != NULL ? expected_name : "<unknown>");

  if (plan_struct_shape(descriptor) == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA, expected_name, NULL,
                          "Service request/response descriptors must be CMeta Struct roots");

  return DATA_BIND_OK;
}

static DataBindStatus plan_validate_schema_native_field(
    DataBind *codec, const char *record_name, size_t field_index,
    const DataBindSchemaField *field, const cmeta_data_field_desc *native_field,
    DataBindServicePlanDiagnostic *diagnostic) {
  const cmeta_data_desc *schema_data = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status;

  if (field == NULL || native_field == NULL || native_field->value == NULL ||
      native_field->value->storage_type == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                          field != NULL ? field->name : record_name, NULL,
                          "Native field metadata is incomplete");

  if (field->name == NULL || native_field->name == NULL ||
      strcmp(field->name, native_field->name) != 0)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
        field != NULL ? field->name : record_name, NULL,
        "Schema field '%s' does not match native field '%s'",
        field != NULL && field->name != NULL ? field->name : "<unnamed>",
        native_field->name != NULL ? native_field->name : "<unnamed>");

  status = data_bind_schema_field_cmeta_data(codec, record_name, field_index,
                                             &schema_data, &error);
  if (status == DATA_BIND_OK) {
    if (schema_data == NULL || schema_data->storage_type == NULL ||
        !cmeta_type_equal(schema_data->storage_type,
                          native_field->value->storage_type))
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field->name, NULL,
          "Schema field '%s' native storage disagrees with its typed descriptor",
          field->name);
  } else if (!field->has_cmeta_kind ||
             field->cmeta_kind != native_field->value->kind) {
    return plan_diag_from_error(
        diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field->name, NULL, &error,
        "Schema field semantic kind disagrees with native descriptor");
  }

  return DATA_BIND_OK;
}

static DataBindServiceBindingSource plan_http_source(const char *kind) {
  if (kind == NULL) return DATA_BIND_SERVICE_SOURCE_NONE;
  if (strcmp(kind, "path") == 0) return DATA_BIND_SERVICE_SOURCE_PATH;
  if (strcmp(kind, "query") == 0) return DATA_BIND_SERVICE_SOURCE_QUERY;
  if (strcmp(kind, "header") == 0) return DATA_BIND_SERVICE_SOURCE_HEADER;
  if (strcmp(kind, "cookie") == 0) return DATA_BIND_SERVICE_SOURCE_COOKIE;
  if (strcmp(kind, "body") == 0) return DATA_BIND_SERVICE_SOURCE_BODY;
  return DATA_BIND_SERVICE_SOURCE_NONE;
}

static DataBindStatus plan_compile_default_token(
    DataBindServicePlanEntryOwned *entry,
    DataBindServicePlanDiagnostic *diagnostic) {
  const char *text;
  char *end = NULL;

  if (entry == NULL || !entry->view.has_default) return DATA_BIND_OK;
  text = entry->view.default_value;
  if (text == NULL || entry->view.data == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                          entry != NULL ? entry->view.schema_field : NULL,
                          entry != NULL ? entry->view.function_param : NULL,
                          "Default metadata is incomplete");

  errno = 0;
  switch (entry->view.data->kind) {
  case CMETA_DATA_BOOL:
    entry->default_token.kind = CSERDE_BOOL;
    if (strcmp(text, "true") == 0 || strcmp(text, "1") == 0)
      entry->default_token.value.boolean = true;
    else if (strcmp(text, "false") == 0 || strcmp(text, "0") == 0)
      entry->default_token.value.boolean = false;
    else
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                            entry->view.schema_field,
                            entry->view.function_param,
                            "Boolean default '%s' is invalid", text);
    break;

  case CMETA_DATA_SINT: {
    long long value = strtoll(text, &end, 10);
    if (errno != 0 || end == text || end == NULL || *end != '\0')
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                            entry->view.schema_field,
                            entry->view.function_param,
                            "Signed default '%s' is invalid", text);
    entry->default_token.kind = CSERDE_SINT;
    entry->default_token.value.sint = (int64_t)value;
    break;
  }

  case CMETA_DATA_UINT: {
    unsigned long long value;
    if (text[0] == '-')
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                            entry->view.schema_field,
                            entry->view.function_param,
                            "Unsigned default '%s' is invalid", text);
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || end == NULL || *end != '\0')
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                            entry->view.schema_field,
                            entry->view.function_param,
                            "Unsigned default '%s' is invalid", text);
    entry->default_token.kind = CSERDE_UINT;
    entry->default_token.value.uint = (uint64_t)value;
    break;
  }

  case CMETA_DATA_FLOAT: {
    double value = strtod(text, &end);
    if (errno != 0 || end == text || end == NULL || *end != '\0')
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                            entry->view.schema_field,
                            entry->view.function_param,
                            "Floating default '%s' is invalid", text);
    entry->default_token.kind = CSERDE_FLOAT;
    entry->default_token.value.floating = value;
    break;
  }

  case CMETA_DATA_STRING:
    entry->default_token.kind = CSERDE_STRING;
    entry->default_token.value.slice.data =
        (const unsigned char *)entry->view.default_value;
    entry->default_token.value.slice.size = strlen(entry->view.default_value);
    entry->default_token.value.slice.lifetime = CSERDE_VIEW_STABLE;
    break;

  case CMETA_DATA_BYTES:
    entry->default_token.kind = CSERDE_BYTES;
    entry->default_token.value.slice.data =
        (const unsigned char *)entry->view.default_value;
    entry->default_token.value.slice.size = strlen(entry->view.default_value);
    entry->default_token.value.slice.lifetime = CSERDE_VIEW_STABLE;
    break;

  case CMETA_DATA_ENUM:
    if (text[0] == '-' || (text[0] >= '0' && text[0] <= '9')) {
      long long value = strtoll(text, &end, 10);
      if (errno == 0 && end != text && end != NULL && *end == '\0') {
        entry->default_token.kind =
            value < 0 ? CSERDE_SINT : CSERDE_UINT;
        if (value < 0)
          entry->default_token.value.sint = (int64_t)value;
        else
          entry->default_token.value.uint = (uint64_t)value;
        break;
      }
      errno = 0;
    }
    entry->default_token.kind = CSERDE_STRING;
    entry->default_token.value.slice.data =
        (const unsigned char *)entry->view.default_value;
    entry->default_token.value.slice.size = strlen(entry->view.default_value);
    entry->default_token.value.slice.lifetime = CSERDE_VIEW_STABLE;
    break;

  default:
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, entry->view.schema_field,
        entry->view.function_param,
        "Default for field '%s' uses an unsupported native semantic kind",
        entry->view.schema_field != NULL ? entry->view.schema_field
                                         : "<unnamed>");
  }

  entry->has_default_token = 1;
  return DATA_BIND_OK;
}

static void plan_entry_owned_clear(DataBindServicePlanEntryOwned *entry) {
  if (entry == NULL) return;
  free((void *)entry->view.schema_field);
  free((void *)entry->view.wire_name);
  free((void *)entry->view.function_param);
  free((void *)entry->view.default_value);
  free((void *)entry->view.format);
  memset(entry, 0, sizeof(*entry));
}

static DataBindStatus plan_entry_owned_init(
    DataBindServicePlanEntryOwned *entry,
    DataBindServicePlanDirection direction,
    DataBindServiceBindingSource source,
    DataBindServiceBindingTarget target,
    const DataBindSchemaField *field,
    const cmeta_data_field_desc *native_field,
    const cmeta_param_desc *param,
    size_t param_index, int parameter_indirect,
    const char *wire_name,
    DataBindServicePlanDiagnostic *diagnostic) {
  DataBindStatus status;

  if (entry == NULL || field == NULL || native_field == NULL ||
      native_field->value == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG,
                          field != NULL ? field->name : NULL,
                          param != NULL ? param->name : NULL,
                          "Invalid binding step construction");

  memset(entry, 0, sizeof(*entry));
  entry->view.size = sizeof(entry->view);
  entry->view.direction = direction;
  entry->view.source = source;
  entry->view.target = target;
  entry->view.function_param_index = param_index;
  entry->view.native_offset = native_field->offset;
  entry->view.parameter_indirect = parameter_indirect;
  entry->view.required = !field->is_optional && !field->has_default;
  entry->view.has_default = field->has_default;
  entry->view.data = native_field->value;

  entry->view.schema_field = plan_strdup(field->name);
  entry->view.wire_name = plan_strdup(
      wire_name != NULL ? wire_name :
      (field->binding_name != NULL ? field->binding_name : field->name));
  entry->view.function_param =
      param != NULL ? plan_strdup(param->name) : NULL;
  entry->view.default_value =
      field->has_default ? plan_strdup(field->default_value) : NULL;
  entry->view.format = field->format != NULL ? plan_strdup(field->format) : NULL;

  if (entry->view.schema_field == NULL || entry->view.wire_name == NULL ||
      (param != NULL && entry->view.function_param == NULL) ||
      (field->has_default && entry->view.default_value == NULL) ||
      (field->format != NULL && entry->view.format == NULL)) {
    plan_entry_owned_clear(entry);
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_OOM, field->name,
                          param != NULL ? param->name : NULL,
                          "Out of memory copying binding plan metadata");
  }

  status = plan_compile_default_token(entry, diagnostic);
  if (status != DATA_BIND_OK) {
    plan_entry_owned_clear(entry);
    return status;
  }
  return DATA_BIND_OK;
}

static void plan_entry_array_clear(DataBindServicePlanEntryOwned *entries,
                                   size_t count) {
  size_t i;
  if (entries == NULL) return;
  for (i = 0u; i < count; ++i) plan_entry_owned_clear(&entries[i]);
  free(entries);
}

void data_bind_service_plan_free(DataBindServicePlan *plan) {
  size_t i;
  if (plan == NULL) return;
  free(plan->service_name);
  free(plan->operation_name);
  plan_entry_array_clear(plan->ingress, plan->ingress_count);
  plan_entry_array_clear(plan->egress, plan->egress_count);
  for (i = 0u; i < plan->error_count; ++i) free(plan->errors[i]);
  free(plan->errors);
  free(plan->param_data);
  free(plan->param_ingress);
  free(plan->param_egress);
  free(plan);
}

static DataBindStatus plan_copy_errors(
    DataBind *codec, const char *service_name, const char *operation_name,
    size_t count, DataBindServicePlan *plan,
    DataBindServicePlanDiagnostic *diagnostic) {
  size_t i;
  if (count == 0u) return DATA_BIND_OK;
  plan->errors = (char **)calloc(count, sizeof(*plan->errors));
  if (plan->errors == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_OOM, NULL, NULL,
                          "Out of memory copying service error contracts");
  for (i = 0u; i < count; ++i) {
    const char *name = data_bind_service_operation_error_at(
        codec, service_name, operation_name, i);
    if (name == NULL)
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA, NULL, NULL,
                            "Service error reflection is incomplete");
    plan->errors[i] = plan_strdup(name);
    if (plan->errors[i] == NULL)
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_OOM, NULL, NULL,
                            "Out of memory copying service error contract");
    plan->error_count = i + 1u;
  }
  return DATA_BIND_OK;
}

static DataBindStatus plan_projection_source(
    DataBindServiceProjection projection, const DataBindSchemaField *field,
    DataBindServiceBindingSource *out_source, const char **out_wire,
    DataBindServicePlanDiagnostic *diagnostic) {
  DataBindServiceBindingSource source;
  const char *wire;

  if (out_source == NULL || out_wire == NULL || field == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG,
                          field != NULL ? field->name : NULL, NULL,
                          "Invalid transport projection query");

  if (projection == DATA_BIND_SERVICE_PROJECTION_RPC) {
    *out_source = DATA_BIND_SERVICE_SOURCE_RPC_PARAM;
    *out_wire = field->name;
    return DATA_BIND_OK;
  }

  if (projection != DATA_BIND_SERVICE_PROJECTION_HTTP)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG,
                          field->name, NULL,
                          "Unknown service transport projection");

  source = plan_http_source(field->binding_kind);
  wire = field->binding_name;
  if (source == DATA_BIND_SERVICE_SOURCE_NONE || wire == NULL)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, field->name, NULL,
        "HTTP request field '%s' has no path/query/header/cookie/body binding",
        field->name != NULL ? field->name : "<unnamed>");

  *out_source = source;
  *out_wire = wire;
  return DATA_BIND_OK;
}

static DataBindStatus plan_compile_ingress(
    DataBind *codec, const DataBindServiceOperation *operation,
    DataBindServicePlan *plan,
    DataBindServicePlanDiagnostic *diagnostic) {
  const cmeta_function_desc *function = plan->function;
  const TbeTypedDescriptor *request = plan->request;
  const cmeta_data_struct_shape *shape = plan_struct_shape(request);
  size_t field_count = data_bind_schema_field_count(codec, operation->request_type);
  size_t i;
  size_t in_count = 0u;
  size_t whole_param = SIZE_MAX;

  if (shape == NULL || shape->field_count != field_count ||
      request->overlay == NULL || request->overlay->field_count != field_count)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, operation->request_type, NULL,
        "Request schema, typed overlay and CMeta Struct field counts disagree");

  for (i = 0u; i < function->param_count; ++i) {
    const cmeta_param_desc *param = &function->params[i];
    int indirect = 0;
    const cmeta_type_desc *value_type;
    if ((param->flags & CMETA_PARAM_IN) == 0u) continue;
    ++in_count;
    value_type = plan_param_value_type(param, &indirect);
    if (plan_type_matches_data(value_type, request->native_data)) {
      if (whole_param != SIZE_MAX)
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, operation->request_type,
            param->name, "Multiple function parameters match the complete request type");
      whole_param = i;
    }
  }

  if (whole_param != SIZE_MAX && in_count != 1u)
    whole_param = SIZE_MAX;

  plan->whole_request_param = whole_param;
  plan->ingress = (DataBindServicePlanEntryOwned *)calloc(
      field_count, sizeof(*plan->ingress));
  if (field_count != 0u && plan->ingress == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_OOM, operation->request_type,
                          NULL, "Out of memory compiling ingress binding plan");

  for (i = 0u; i < field_count; ++i) {
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    const cmeta_data_field_desc *native_field;
    DataBindServiceBindingSource source;
    const char *wire_name = NULL;
    const cmeta_param_desc *param = NULL;
    size_t param_index = SIZE_MAX;
    int indirect = 0;
    DataBindServiceBindingTarget target;
    DataBindStatus status;

    if (!data_bind_schema_field_at(codec, operation->request_type, i, &field))
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                            operation->request_type, NULL,
                            "Request field reflection failed at index %zu", i);

    native_field = plan_native_field(request, field.name);
    status = plan_validate_schema_native_field(
        codec, operation->request_type, i, &field, native_field, diagnostic);
    if (status != DATA_BIND_OK) return status;

    status = plan_projection_source(plan->projection, &field, &source,
                                    &wire_name, diagnostic);
    if (status != DATA_BIND_OK) return status;

    if (whole_param != SIZE_MAX) {
      param_index = whole_param;
      param = &function->params[param_index];
      (void)plan_param_value_type(param, &indirect);
      target = DATA_BIND_SERVICE_TARGET_REQUEST_FIELD;
      plan->param_ingress[param_index] = 1u;
    } else {
      param_index = plan_param_index(function, field.name);
      if (param_index == SIZE_MAX)
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field.name, NULL,
            "Schema field '%s' has no matching native function parameter",
            field.name);
      param = &function->params[param_index];
      if ((param->flags & CMETA_PARAM_IN) == 0u)
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field.name, param->name,
            "Schema field '%s' maps to function parameter '%s' without IN direction",
            field.name, param->name);
      if (!plan_type_matches_data(plan_param_value_type(param, &indirect),
                                  native_field->value))
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field.name, param->name,
            "Schema field '%s' type disagrees with function parameter '%s'",
            field.name, param->name);
      target = DATA_BIND_SERVICE_TARGET_FUNCTION_PARAM;
      plan->param_ingress[param_index] = 1u;
      if (plan->param_data[param_index] != NULL &&
          plan->param_data[param_index] != native_field->value)
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field.name, param->name,
            "Function parameter '%s' receives incompatible service fields",
            param->name);
      plan->param_data[param_index] = native_field->value;
    }

    status = plan_entry_owned_init(
        &plan->ingress[i], DATA_BIND_SERVICE_PLAN_INGRESS, source, target,
        &field, native_field, param, param_index, indirect, wire_name,
        diagnostic);
    if (status != DATA_BIND_OK) return status;
    plan->ingress_count = i + 1u;
  }

  for (i = 0u; i < function->param_count; ++i) {
    const cmeta_param_desc *param = &function->params[i];
    if ((param->flags & CMETA_PARAM_IN) != 0u &&
        !plan->param_ingress[i])
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, operation->request_type,
          param->name,
          "Function IN parameter '%s' has no request binding",
          param->name != NULL ? param->name : "<unnamed>");
  }

  return DATA_BIND_OK;
}

static int plan_function_has_outside_out(
    const cmeta_function_desc *function, size_t allowed) {
  size_t i;
  for (i = 0u; i < function->param_count; ++i) {
    if ((function->params[i].flags & CMETA_PARAM_OUT) != 0u && i != allowed)
      return 1;
  }
  return 0;
}

static DataBindStatus plan_compile_egress(
    DataBind *codec, const DataBindServiceOperation *operation,
    DataBindServicePlan *plan,
    DataBindServicePlanDiagnostic *diagnostic) {
  const cmeta_function_desc *function = plan->function;
  size_t field_count;
  const cmeta_data_struct_shape *shape;
  size_t whole_param = SIZE_MAX;
  int whole_return = 0;
  size_t i;

  if (strcmp(operation->response_type, "void") == 0) {
    if (!cmeta_type_equal(function->return_type, &cmeta_type_void))
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, operation->response_type,
          NULL, "Void service response requires a void native return type");
    for (i = 0u; i < function->param_count; ++i)
      if ((function->params[i].flags & CMETA_PARAM_OUT) != 0u)
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, operation->response_type,
            function->params[i].name,
            "Void service response cannot expose OUT parameter '%s'",
            function->params[i].name);
    return DATA_BIND_OK;
  }

  if (plan->response == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                          operation->response_type, NULL,
                          "Non-void service response requires a native descriptor");

  shape = plan_struct_shape(plan->response);
  field_count = data_bind_schema_field_count(codec, operation->response_type);
  if (shape == NULL || shape->field_count != field_count ||
      plan->response->overlay == NULL ||
      plan->response->overlay->field_count != field_count)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, operation->response_type, NULL,
        "Response schema, typed overlay and CMeta Struct field counts disagree");

  if (plan_type_matches_data(function->return_type,
                             plan->response->native_data)) {
    whole_return = 1;
    if (plan_function_has_outside_out(function, SIZE_MAX))
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, operation->response_type,
          NULL, "Whole response return cannot be combined with OUT parameters");
  } else {
    for (i = 0u; i < function->param_count; ++i) {
      const cmeta_param_desc *param = &function->params[i];
      int indirect = 0;
      if ((param->flags & CMETA_PARAM_OUT) == 0u) continue;
      if (plan_type_matches_data(plan_param_value_type(param, &indirect),
                                 plan->response->native_data)) {
        if (whole_param != SIZE_MAX)
          return plan_diag_fail(
              diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
              operation->response_type, param->name,
              "Multiple OUT parameters match the complete response type");
        whole_param = i;
      }
    }
    if (whole_param != SIZE_MAX) {
      if (!cmeta_type_equal(function->return_type, &cmeta_type_void))
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
            operation->response_type, function->params[whole_param].name,
            "Whole OUT response currently requires a void native return type");
      if (plan_function_has_outside_out(function, whole_param))
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
            operation->response_type, function->params[whole_param].name,
            "Whole OUT response cannot be combined with other OUT parameters");
      if ((function->params[whole_param].flags & CMETA_PARAM_IN) != 0u)
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
            operation->response_type, function->params[whole_param].name,
            "Whole response parameter must be OUT-only in binding plan v1");
      plan->param_data[whole_param] = plan->response->native_data;
      plan->param_egress[whole_param] = 1u;
    } else if (!cmeta_type_equal(function->return_type, &cmeta_type_void)) {
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, operation->response_type,
          NULL,
          "Field-mapped response requires void return or complete response return type");
    }
  }

  plan->whole_response_return = whole_return;
  plan->whole_response_param = whole_param;
  plan->egress = (DataBindServicePlanEntryOwned *)calloc(
      field_count, sizeof(*plan->egress));
  if (field_count != 0u && plan->egress == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_OOM,
                          operation->response_type, NULL,
                          "Out of memory compiling egress binding plan");

  for (i = 0u; i < field_count; ++i) {
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    const cmeta_data_field_desc *native_field;
    const cmeta_param_desc *param = NULL;
    size_t param_index = SIZE_MAX;
    int indirect = 0;
    DataBindServiceBindingTarget target;
    DataBindStatus status;

    if (!data_bind_schema_field_at(codec, operation->response_type, i, &field))
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                            operation->response_type, NULL,
                            "Response field reflection failed at index %zu", i);

    native_field = plan_native_field(plan->response, field.name);
    status = plan_validate_schema_native_field(
        codec, operation->response_type, i, &field, native_field, diagnostic);
    if (status != DATA_BIND_OK) return status;

    if (whole_return) {
      target = DATA_BIND_SERVICE_TARGET_RETURN_VALUE;
    } else if (whole_param != SIZE_MAX) {
      param_index = whole_param;
      param = &function->params[param_index];
      (void)plan_param_value_type(param, &indirect);
      target = DATA_BIND_SERVICE_TARGET_FUNCTION_PARAM;
    } else {
      param_index = plan_param_index(function, field.name);
      if (param_index == SIZE_MAX)
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field.name, NULL,
            "Response field '%s' has no matching OUT parameter", field.name);
      param = &function->params[param_index];
      if ((param->flags & CMETA_PARAM_OUT) == 0u)
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field.name, param->name,
            "Response field '%s' maps to function parameter '%s' without OUT direction",
            field.name, param->name);
      if (!plan_type_matches_data(plan_param_value_type(param, &indirect),
                                  native_field->value))
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field.name, param->name,
            "Response field '%s' type disagrees with OUT parameter '%s'",
            field.name, param->name);
      target = DATA_BIND_SERVICE_TARGET_FUNCTION_PARAM;
      plan->param_egress[param_index] = 1u;
      if (plan->param_data[param_index] != NULL &&
          plan->param_data[param_index] != native_field->value)
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field.name, param->name,
            "INOUT parameter '%s' maps incompatible request/response storage",
            param->name);
      plan->param_data[param_index] = native_field->value;
    }

    status = plan_entry_owned_init(
        &plan->egress[i], DATA_BIND_SERVICE_PLAN_EGRESS,
        DATA_BIND_SERVICE_SOURCE_RESULT, target, &field, native_field,
        param, param_index, indirect, field.name, diagnostic);
    if (status != DATA_BIND_OK) return status;
    plan->egress_count = i + 1u;
  }

  if (!whole_return && whole_param == SIZE_MAX) {
    for (i = 0u; i < function->param_count; ++i) {
      if ((function->params[i].flags & CMETA_PARAM_OUT) != 0u &&
          !plan->param_egress[i])
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
            operation->response_type, function->params[i].name,
            "Function OUT parameter '%s' has no response binding",
            function->params[i].name);
    }
  }

  return DATA_BIND_OK;
}

DataBindStatus data_bind_service_plan_compile(
    DataBind *codec, const char *service_name, const char *operation_name,
    DataBindServiceProjection projection,
    const DataBindServiceNativeBinding *native,
    DataBindServicePlan **out_plan,
    DataBindServicePlanDiagnostic *diagnostic) {
  DataBindServiceOperation operation = DATA_BIND_SERVICE_OPERATION_INIT;
  DataBindServicePlan *plan = NULL;
  DataBindStatus status;

  if (out_plan != NULL) *out_plan = NULL;
  plan_diag_clear(diagnostic);
  if (codec == NULL || service_name == NULL || operation_name == NULL ||
      native == NULL || out_plan == NULL ||
      native->size < sizeof(*native) ||
      native->abi_version != DATA_BIND_SERVICE_PLAN_ABI_VERSION ||
      native->function == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                          "Invalid service binding compile arguments");

  if (!cmeta_function_desc_valid(native->function))
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA, NULL,
                          native->function->name,
                          "Invalid CMeta function descriptor");

  if (!data_bind_service_operation_find(codec, service_name, operation_name,
                                        &operation))
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_TYPE_NOT_FOUND, operation_name, NULL,
        "Service operation '%s.%s' was not found", service_name, operation_name);

  if (projection == DATA_BIND_SERVICE_PROJECTION_HTTP && !operation.has_http)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, operation_name, NULL,
        "Service operation '%s.%s' has no HTTP projection",
        service_name, operation_name);
  if (projection == DATA_BIND_SERVICE_PROJECTION_RPC && !operation.has_rpc)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, operation_name, NULL,
        "Service operation '%s.%s' has no RPC projection",
        service_name, operation_name);
  if (projection != DATA_BIND_SERVICE_PROJECTION_HTTP &&
      projection != DATA_BIND_SERVICE_PROJECTION_RPC)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG,
                          operation_name, NULL,
                          "Unknown service projection");

  status = plan_validate_descriptor(native->request, operation.request_type,
                                    diagnostic);
  if (status != DATA_BIND_OK) return status;

  if (strcmp(operation.response_type, "void") != 0) {
    status = plan_validate_descriptor(native->response, operation.response_type,
                                      diagnostic);
    if (status != DATA_BIND_OK) return status;
  } else if (native->response != NULL) {
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                          operation.response_type, NULL,
                          "Void service response must not provide a native response descriptor");
  }

  plan = (DataBindServicePlan *)calloc(1u, sizeof(*plan));
  if (plan == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_OOM, NULL, NULL,
                          "Out of memory allocating binding plan");

  plan->service_name = plan_strdup(service_name);
  plan->operation_name = plan_strdup(operation_name);
  plan->projection = projection;
  plan->function = native->function;
  plan->request = native->request;
  plan->response = native->response;
  plan->param_count = native->function->param_count;
  plan->whole_request_param = SIZE_MAX;
  plan->whole_response_param = SIZE_MAX;

  if (plan->service_name == NULL || plan->operation_name == NULL) {
    status = plan_diag_fail(diagnostic, DATA_BIND_ERR_OOM, NULL, NULL,
                            "Out of memory copying service identity");
    goto fail;
  }

  if (plan->param_count != 0u) {
    plan->param_data = (const cmeta_data_desc **)calloc(
        plan->param_count, sizeof(*plan->param_data));
    plan->param_ingress = (unsigned char *)calloc(
        plan->param_count, sizeof(*plan->param_ingress));
    plan->param_egress = (unsigned char *)calloc(
        plan->param_count, sizeof(*plan->param_egress));
    if (plan->param_data == NULL || plan->param_ingress == NULL ||
        plan->param_egress == NULL) {
      status = plan_diag_fail(diagnostic, DATA_BIND_ERR_OOM, NULL, NULL,
                              "Out of memory allocating parameter binding map");
      goto fail;
    }
  }

  status = plan_copy_errors(codec, service_name, operation_name,
                            operation.error_count, plan, diagnostic);
  if (status != DATA_BIND_OK) goto fail;

  status = plan_compile_ingress(codec, &operation, plan, diagnostic);
  if (status != DATA_BIND_OK) goto fail;

  status = plan_compile_egress(codec, &operation, plan, diagnostic);
  if (status != DATA_BIND_OK) goto fail;

  plan_diag_clear(diagnostic);
  *out_plan = plan;
  return DATA_BIND_OK;

fail:
  data_bind_service_plan_free(plan);
  return status;
}

size_t data_bind_service_plan_ingress_count(const DataBindServicePlan *plan) {
  return plan != NULL ? plan->ingress_count : 0u;
}

size_t data_bind_service_plan_egress_count(const DataBindServicePlan *plan) {
  return plan != NULL ? plan->egress_count : 0u;
}

static int plan_entry_copy(const DataBindServicePlanEntryOwned *entry,
                           DataBindServicePlanEntry *out) {
  size_t size;
  if (entry == NULL || out == NULL) return 0;
  size = plan_out_size(out->size, sizeof(*out));
  memset(out, 0, size);
  memcpy(out, &entry->view, size);
  if (size >= sizeof(size_t)) out->size = size;
  return 1;
}

int data_bind_service_plan_ingress_at(
    const DataBindServicePlan *plan, size_t index,
    DataBindServicePlanEntry *out) {
  if (plan == NULL || index >= plan->ingress_count) return 0;
  return plan_entry_copy(&plan->ingress[index], out);
}

int data_bind_service_plan_egress_at(
    const DataBindServicePlan *plan, size_t index,
    DataBindServicePlanEntry *out) {
  if (plan == NULL || index >= plan->egress_count) return 0;
  return plan_entry_copy(&plan->egress[index], out);
}

size_t data_bind_service_plan_error_count(const DataBindServicePlan *plan) {
  return plan != NULL ? plan->error_count : 0u;
}

const char *data_bind_service_plan_error_at(
    const DataBindServicePlan *plan, size_t index) {
  return plan != NULL && index < plan->error_count ? plan->errors[index] : NULL;
}

const cmeta_function_desc *data_bind_service_plan_function(
    const DataBindServicePlan *plan) {
  return plan != NULL ? plan->function : NULL;
}

static int provider_valid_for_input(const DataBindServiceProvider *provider) {
  return provider != NULL && provider->size >= sizeof(*provider) &&
         provider->abi_version == DATA_BIND_SERVICE_PLAN_ABI_VERSION &&
         provider->open_input != NULL;
}

static int provider_valid_for_output(const DataBindServiceProvider *provider) {
  return provider != NULL && provider->size >= sizeof(*provider) &&
         provider->abi_version == DATA_BIND_SERVICE_PLAN_ABI_VERSION &&
         provider->begin_output != NULL && provider->write_output != NULL &&
         provider->commit_output != NULL && provider->abort_output != NULL;
}

static DataBindStatus plan_frame_preflight(
    const DataBindServicePlan *plan, const DataBindServiceCallFrame *frame,
    DataBindServicePlanDiagnostic *diagnostic) {
  size_t i;

  if (plan == NULL || frame == NULL || frame->size < sizeof(*frame))
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                          "Invalid service call frame");
  if (frame->param_count < plan->param_count)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                          "Service call frame exposes too few function parameters");

  if (plan->whole_request_param != SIZE_MAX) {
    size_t bytes = plan->request->native_data->storage_type->size;
    if (frame->request == NULL || frame->request_bytes < bytes)
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG,
                            plan->request->overlay->name,
                            plan->function->params[plan->whole_request_param].name,
                            "Request staging storage is missing or too small");
  }

  for (i = 0u; i < plan->param_count; ++i) {
    const cmeta_data_desc *data = plan->param_data[i];
    if (data == NULL) continue;
    if (frame->params == NULL || frame->param_bytes == NULL ||
        frame->params[i] == NULL || data->storage_type == NULL ||
        frame->param_bytes[i] < data->storage_type->size)
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL,
          plan->function->params[i].name,
          "Native staging storage for function parameter '%s' is missing or too small",
          plan->function->params[i].name);
  }

  if (plan->whole_response_return) {
    size_t bytes = plan->response->native_data->storage_type->size;
    if (frame->return_value == NULL || frame->return_bytes < bytes)
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG,
                            plan->response->overlay->name, NULL,
                            "Return staging storage is missing or too small");
  }

  return DATA_BIND_OK;
}

static DataBindStatus plan_native_init(
    const DataBindNativeOptions *options, const cmeta_data_desc *data,
    void *storage, DataBindServicePlanDiagnostic *diagnostic,
    const char *schema_field, const char *function_param) {
  DataBindNativeDiagnostic native = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  DataBindStatus status;
  if (data == NULL || data->storage_type == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA, schema_field,
                          function_param, "Native binding descriptor is incomplete");
  status = data_bind_native_init(options, data, storage,
                                 data->storage_type->size, &native);
  if (status != DATA_BIND_OK)
    return plan_diag_from_error(diagnostic, status, schema_field,
                                function_param, &native.error,
                                "Native staging initialization failed");
  return DATA_BIND_OK;
}

static void plan_native_clear_best_effort(
    const DataBindNativeOptions *options, const cmeta_data_desc *data,
    void *storage) {
  DataBindNativeDiagnostic native = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  if (data == NULL || data->storage_type == NULL || storage == NULL) return;
  (void)data_bind_native_clear(options, data, storage,
                               data->storage_type->size, &native);
}

typedef struct PlanDefaultReaderContext {
  const cserde_token *token;
  int emitted;
} PlanDefaultReaderContext;

static cserde_status plan_default_reader_next(void *context,
                                               cserde_token *out) {
  PlanDefaultReaderContext *state = (PlanDefaultReaderContext *)context;
  if (state == NULL || out == NULL || state->token == NULL)
    return CSERDE_INVALID_ARGUMENT;
  if (state->emitted) return CSERDE_DONE;
  *out = *state->token;
  state->emitted = 1;
  return CSERDE_OK;
}

static const cserde_reader_ops plan_default_reader_ops = {
    offsetof(cserde_reader_ops, next) + sizeof(cserde_reader_next_fn),
    CSERDE_READER_OPS_ABI_VERSION,
    plan_default_reader_next};

static void *plan_entry_destination(
    const DataBindServicePlan *plan, const DataBindServicePlanEntry *entry,
    DataBindServiceCallFrame *frame) {
  unsigned char *base;
  if (entry->target == DATA_BIND_SERVICE_TARGET_REQUEST_FIELD) {
    base = (unsigned char *)frame->request;
  } else if (entry->target == DATA_BIND_SERVICE_TARGET_FUNCTION_PARAM &&
             entry->function_param_index < frame->param_count &&
             frame->params != NULL) {
    base = (unsigned char *)frame->params[entry->function_param_index];
  } else {
    return NULL;
  }
  (void)plan;
  return base != NULL ? base + entry->native_offset : NULL;
}

static const void *plan_entry_output(
    const DataBindServicePlanEntry *entry,
    const DataBindServiceCallFrame *frame) {
  const unsigned char *base;
  if (entry->target == DATA_BIND_SERVICE_TARGET_RETURN_VALUE) {
    base = (const unsigned char *)frame->return_value;
  } else if (entry->target == DATA_BIND_SERVICE_TARGET_FUNCTION_PARAM &&
             entry->function_param_index < frame->param_count &&
             frame->params != NULL) {
    base = (const unsigned char *)frame->params[entry->function_param_index];
  } else {
    return NULL;
  }
  return base != NULL ? base + entry->native_offset : NULL;
}

static void plan_cleanup_inputs(
    const DataBindServicePlan *plan, const DataBindNativeOptions *options,
    DataBindServiceCallFrame *frame, size_t params_initialized,
    int request_initialized) {
  size_t i = params_initialized;
  while (i != 0u) {
    --i;
    if (plan->param_data[i] != NULL && frame->params != NULL)
      plan_native_clear_best_effort(options, plan->param_data[i],
                                    frame->params[i]);
  }
  if (request_initialized)
    plan_native_clear_best_effort(options, plan->request->native_data,
                                  frame->request);
}

DataBindStatus data_bind_service_plan_bind_inputs(
    const DataBindServicePlan *plan,
    const DataBindServiceProvider *provider,
    const DataBindNativeOptions *native_options,
    DataBindServiceCallFrame *frame,
    DataBindServicePlanDiagnostic *diagnostic) {
  size_t i;
  size_t params_initialized = 0u;
  int request_initialized = 0;
  DataBindStatus status;

  plan_diag_clear(diagnostic);
  if (!provider_valid_for_input(provider) || native_options == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                          "Invalid service input provider or native options");

  status = plan_frame_preflight(plan, frame, diagnostic);
  if (status != DATA_BIND_OK) return status;

  if (plan->whole_request_param != SIZE_MAX) {
    status = plan_native_init(native_options, plan->request->native_data,
                              frame->request, diagnostic,
                              plan->request->overlay->name,
                              plan->function->params[
                                  plan->whole_request_param].name);
    if (status != DATA_BIND_OK) return status;
    request_initialized = 1;
  }

  for (i = 0u; i < plan->param_count; ++i) {
    if (plan->param_data[i] != NULL) {
      status = plan_native_init(native_options, plan->param_data[i],
                                frame->params[i], diagnostic, NULL,
                                plan->function->params[i].name);
      if (status != DATA_BIND_OK) {
        plan_cleanup_inputs(plan, native_options, frame, i,
                            request_initialized);
        return status;
      }
    }
    params_initialized = i + 1u;
  }

  for (i = 0u; i < plan->ingress_count; ++i) {
    const DataBindServicePlanEntryOwned *owned = &plan->ingress[i];
    const DataBindServicePlanEntry *entry = &owned->view;
    void *destination = plan_entry_destination(plan, entry, frame);
    cserde_reader reader = {0};
    int present = 0;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindNativeDiagnostic native = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;

    if (destination == NULL) {
      status = plan_diag_fail(
          diagnostic, DATA_BIND_ERR_INVALID_ARG, entry->schema_field,
          entry->function_param, "Binding destination is unavailable");
      goto fail;
    }

    status = provider->open_input(provider->context, entry, &reader,
                                  &present, &error);
    if (status != DATA_BIND_OK) {
      status = plan_diag_from_error(diagnostic, status, entry->schema_field,
                                    entry->function_param, &error,
                                    "Input provider failed");
      goto fail;
    }

    if (!present) {
      if (owned->has_default_token) {
        PlanDefaultReaderContext context = {&owned->default_token, 0};
        if (cserde_reader_init(&reader, &plan_default_reader_ops,
                               &context) != CSERDE_OK) {
          status = plan_diag_fail(
              diagnostic, DATA_BIND_ERR_RUNTIME, entry->schema_field,
              entry->function_param, "Could not initialize default reader");
          goto fail;
        }
        status = data_bind_native_decode(
            native_options, entry->data, &reader, destination,
            entry->data->storage_type->size, &native);
      } else if (!entry->required) {
        continue;
      } else {
        status = plan_diag_fail(
            diagnostic, DATA_BIND_ERR_TYPE_NOT_FOUND, entry->schema_field,
            entry->function_param,
            "Required service input '%s' is absent",
            entry->wire_name != NULL ? entry->wire_name : entry->schema_field);
        goto fail;
      }
    } else {
      status = data_bind_native_decode(
          native_options, entry->data, &reader, destination,
          entry->data->storage_type->size, &native);
    }

    if (status != DATA_BIND_OK) {
      status = plan_diag_from_error(
          diagnostic, status, entry->schema_field, entry->function_param,
          &native.error, "Native service input decode failed");
      goto fail;
    }
  }

  plan_diag_clear(diagnostic);
  return DATA_BIND_OK;

fail:
  plan_cleanup_inputs(plan, native_options, frame, params_initialized,
                      request_initialized);
  return status;
}

DataBindStatus data_bind_service_plan_write_outputs(
    const DataBindServicePlan *plan,
    const DataBindServiceProvider *provider,
    const DataBindServiceCallFrame *frame,
    DataBindServicePlanDiagnostic *diagnostic) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status;
  size_t i;

  plan_diag_clear(diagnostic);
  if (plan == NULL || frame == NULL || frame->size < sizeof(*frame) ||
      !provider_valid_for_output(provider))
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                          "Invalid service output plan/provider/frame");

  status = plan_frame_preflight(plan, frame, diagnostic);
  if (status != DATA_BIND_OK) return status;

  status = provider->begin_output(provider->context, &error);
  if (status != DATA_BIND_OK)
    return plan_diag_from_error(diagnostic, status, NULL, NULL, &error,
                                "Output transaction could not begin");

  for (i = 0u; i < plan->egress_count; ++i) {
    const DataBindServicePlanEntry *entry = &plan->egress[i].view;
    const void *value = plan_entry_output(entry, frame);
    if (value == NULL || entry->data == NULL ||
        entry->data->storage_type == NULL) {
      provider->abort_output(provider->context);
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_INVALID_ARG, entry->schema_field,
          entry->function_param, "Service output storage is unavailable");
    }

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    status = provider->write_output(
        provider->context, entry, value, entry->data->storage_type->size,
        &error);
    if (status != DATA_BIND_OK) {
      provider->abort_output(provider->context);
      return plan_diag_from_error(diagnostic, status, entry->schema_field,
                                  entry->function_param, &error,
                                  "Output provider rejected service value");
    }
  }

  error = (DataBindError)DATA_BIND_ERROR_INIT;
  status = provider->commit_output(provider->context, &error);
  if (status != DATA_BIND_OK) {
    provider->abort_output(provider->context);
    return plan_diag_from_error(diagnostic, status, NULL, NULL, &error,
                                "Output transaction commit failed");
  }

  plan_diag_clear(diagnostic);
  return DATA_BIND_OK;
}
