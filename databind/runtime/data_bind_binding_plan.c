#include "data_bind_binding_plan.h"

#include <cmeta/type_traits.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct DataBindBindingPlanEntryOwned {
  DataBindBindingPlanEntry view;
  char *space;
  char *name;
  char *schema_field;
  char *function_param;
  char *default_value;
  char *format;
  cserde_token default_token;
  int has_default_token;
} DataBindBindingPlanEntryOwned;

struct DataBindBindingPlan {
  char *operation_id;
  char *projection_id;
  const cmeta_function_desc *function;
  const DataBindNativeTypeBinding *request;
  const DataBindNativeTypeBinding *response;

  DataBindBindingPlanEntryOwned *ingress;
  size_t ingress_count;
  DataBindBindingPlanEntryOwned *egress;
  size_t egress_count;

  char **errors;
  size_t error_count;

  const cmeta_data_desc **param_data;
  unsigned char *param_ingress;
  unsigned char *param_egress;
  size_t param_count;

  size_t request_root_param;
  int has_request_root_param;
  size_t response_root_param;
  int has_response_root_param;
  int response_uses_return;
};

static size_t plan_out_size(size_t requested, size_t full_size) {
  return requested != 0u && requested < full_size ? requested : full_size;
}

static int plan_diag_header_valid(
    const DataBindBindingPlanDiagnostic *diagnostic) {
  return diagnostic == NULL ||
         diagnostic->size >=
             offsetof(DataBindBindingPlanDiagnostic, status) +
                 sizeof(diagnostic->status);
}

static void plan_diag_clear(DataBindBindingPlanDiagnostic *diagnostic) {
  size_t size;
  if (diagnostic == NULL) return;
  size = plan_out_size(diagnostic->size, sizeof(*diagnostic));
  memset(diagnostic, 0, size);
  if (size >= sizeof(size_t)) diagnostic->size = size;
  if (size >= offsetof(DataBindBindingPlanDiagnostic, status) +
                  sizeof(diagnostic->status))
    diagnostic->status = DATA_BIND_OK;
}

static DataBindStatus plan_diag_fail(
    DataBindBindingPlanDiagnostic *diagnostic, DataBindStatus status,
    const char *schema_field, const char *function_param,
    const char *fmt, ...) {
  va_list ap;
  size_t size;

  if (diagnostic == NULL) return status;
  size = plan_out_size(diagnostic->size, sizeof(*diagnostic));
  memset(diagnostic, 0, size);
  if (size >= sizeof(size_t)) diagnostic->size = size;
  if (size >= offsetof(DataBindBindingPlanDiagnostic, status) +
                  sizeof(diagnostic->status))
    diagnostic->status = status;
  if (size >= offsetof(DataBindBindingPlanDiagnostic, schema_field) +
                  sizeof(diagnostic->schema_field))
    snprintf(diagnostic->schema_field, sizeof(diagnostic->schema_field),
             "%s", schema_field != NULL ? schema_field : "");
  if (size >= offsetof(DataBindBindingPlanDiagnostic, function_param) +
                  sizeof(diagnostic->function_param))
    snprintf(diagnostic->function_param, sizeof(diagnostic->function_param),
             "%s", function_param != NULL ? function_param : "");
  if (size >= offsetof(DataBindBindingPlanDiagnostic, message) +
                  sizeof(diagnostic->message)) {
    va_start(ap, fmt);
    vsnprintf(diagnostic->message, sizeof(diagnostic->message), fmt, ap);
    va_end(ap);
  }
  return status;
}

static char *plan_strdup(const char *text) {
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

static char *plan_operation_id(const char *service, const char *operation) {
  size_t a;
  size_t b;
  char *result;
  if (service == NULL || operation == NULL) return NULL;
  a = strlen(service);
  b = strlen(operation);
  if (a > SIZE_MAX - b - 2u) return NULL;
  result = (char *)malloc(a + b + 2u);
  if (result == NULL) return NULL;
  memcpy(result, service, a);
  result[a] = '.';
  memcpy(result + a + 1u, operation, b + 1u);
  return result;
}

static const cmeta_data_struct_shape *plan_struct_shape(
    const DataBindNativeTypeBinding *binding) {
  if (binding == NULL || binding->data == NULL ||
      binding->data->kind != CMETA_DATA_STRUCT ||
      binding->data->shape == NULL)
    return NULL;
  return (const cmeta_data_struct_shape *)binding->data->shape;
}

static const cmeta_data_field_desc *plan_native_field(
    const DataBindNativeTypeBinding *binding, const char *name) {
  const cmeta_data_struct_shape *shape = plan_struct_shape(binding);
  size_t i;
  if (shape == NULL || name == NULL) return NULL;
  for (i = 0u; i < shape->field_count; ++i) {
    const cmeta_data_field_desc *field = &shape->fields[i];
    if (field->name != NULL && strcmp(field->name, name) == 0)
      return field;
  }
  return NULL;
}

static const DataBindNativePresenceBinding *plan_presence(
    const DataBindNativeTypeBinding *binding, const char *name) {
  size_t i;
  if (binding == NULL || name == NULL || binding->presence == NULL)
    return NULL;
  for (i = 0u; i < binding->presence_count; ++i) {
    const DataBindNativePresenceBinding *presence = &binding->presence[i];
    if (presence->field_name != NULL &&
        strcmp(presence->field_name, name) == 0)
      return presence;
  }
  return NULL;
}

static int plan_data_semantically_equal(const cmeta_data_desc *left,
                                        const cmeta_data_desc *right) {
  if (left == right) return left != NULL && cmeta_data_desc_valid(left);
  if (!cmeta_data_desc_valid(left) || !cmeta_data_desc_valid(right) ||
      left->kind != right->kind || left->storage_type == NULL ||
      right->storage_type == NULL ||
      !cmeta_type_equal(left->storage_type, right->storage_type))
    return 0;
  if (left->stable_id != NULL && right->stable_id != NULL)
    return strcmp(left->stable_id, right->stable_id) == 0;
  return 1;
}

static DataBindStatus plan_validate_native_type(
    DataBind *codec, const DataBindNativeTypeBinding *binding,
    const char *expected_name, DataBindBindingPlanDiagnostic *diagnostic) {
  const cmeta_data_struct_shape *shape;
  DataBindSchemaType schema_type = DATA_BIND_SCHEMA_TYPE_INIT;
  size_t i;

  if (binding == NULL ||
      binding->size <
          offsetof(DataBindNativeTypeBinding, presence_count) +
              sizeof(binding->presence_count) ||
      binding->abi_version != DATA_BIND_BINDING_PLAN_ABI_VERSION ||
      binding->idl_type_name == NULL ||
      strcmp(binding->idl_type_name, expected_name) != 0 ||
      !cmeta_data_desc_valid(binding->data) ||
      binding->data->kind != CMETA_DATA_STRUCT ||
      binding->data->storage_type == NULL ||
      binding->data->shape == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA, expected_name, NULL,
                          "Invalid native binding for DataBind IDL type '%s'",
                          expected_name != NULL ? expected_name : "");

  if (!data_bind_schema_find_type(codec, expected_name, &schema_type) ||
      schema_type.field_count !=
          data_bind_schema_field_count(codec, expected_name))
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA, expected_name, NULL,
                          "IDL type '%s' is not a reflected record",
                          expected_name);

  shape = plan_struct_shape(binding);
  if (shape == NULL || shape->field_count != schema_type.field_count)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, expected_name, NULL,
        "Native field count does not match DataBind IDL type '%s'",
        expected_name);

  for (i = 0u; i < schema_type.field_count; ++i) {
    DataBindSchemaField schema_field = DATA_BIND_SCHEMA_FIELD_INIT;
    const cmeta_data_field_desc *native_field;
    const cmeta_data_desc *schema_data = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    if (!data_bind_schema_field_at(codec, expected_name, i, &schema_field))
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA, expected_name,
                            NULL, "Could not reflect field %zu of '%s'", i,
                            expected_name);

    native_field = plan_native_field(binding, schema_field.name);
    if (native_field == NULL || native_field->value == NULL)
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, schema_field.name, NULL,
          "Native type '%s' is missing field '%s'", expected_name,
          schema_field.name != NULL ? schema_field.name : "");

    schema_data = schema_field.cmeta_data;
    if (schema_data == NULL &&
        data_bind_schema_field_cmeta_data(codec, expected_name, i,
                                          &schema_data, &error) != DATA_BIND_OK)
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, schema_field.name, NULL,
          "IDL field '%s.%s' has no canonical CMeta data mapping",
          expected_name,
          schema_field.name != NULL ? schema_field.name : "");

    if (!plan_data_semantically_equal(schema_data, native_field->value))
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, schema_field.name, NULL,
          "Native CMeta field '%s.%s' does not match DataBind IDL semantics",
          expected_name,
          schema_field.name != NULL ? schema_field.name : "");

    if (schema_field.is_optional) {
      const DataBindNativePresenceBinding *presence =
          plan_presence(binding, schema_field.name);
      if (presence != NULL) {
        if (presence->size < sizeof(*presence) ||
            presence->bit > 7u ||
            presence->byte_offset >= binding->data->storage_type->size)
          return plan_diag_fail(
              diagnostic, DATA_BIND_ERR_SCHEMA, schema_field.name, NULL,
              "Invalid optional-presence layout for '%s.%s'", expected_name,
              schema_field.name);
      }
    }
  }

  return DATA_BIND_OK;
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

static size_t plan_find_param_by_name(
    const cmeta_function_desc *function, const char *name) {
  size_t i;
  if (function == NULL || name == NULL) return SIZE_MAX;
  for (i = 0u; i < function->param_count; ++i)
    if (function->params[i].name != NULL &&
        strcmp(function->params[i].name, name) == 0)
      return i;
  return SIZE_MAX;
}

static size_t plan_find_root_param(
    const cmeta_function_desc *function,
    const cmeta_type_desc *type,
    cmeta_param_flags exact_direction,
    int *indirect) {
  size_t i;
  size_t found = SIZE_MAX;
  int found_indirect = 0;

  if (function == NULL || type == NULL) return SIZE_MAX;

  for (i = 0u; i < function->param_count; ++i) {
    const cmeta_param_desc *param = &function->params[i];
    const cmeta_type_desc *value_type;
    int param_indirect = 0;

    if ((param->flags & CMETA_PARAM_DIRECTION_MASK) != exact_direction)
      continue;
    value_type = plan_param_value_type(param, &param_indirect);
    if (value_type == NULL || !cmeta_type_equal(value_type, type))
      continue;
    if (found != SIZE_MAX) return SIZE_MAX - 1u;
    found = i;
    found_indirect = param_indirect;
  }

  if (found != SIZE_MAX && indirect != NULL) *indirect = found_indirect;
  return found;
}

static int plan_param_type_matches_data(
    const cmeta_param_desc *param,
    const cmeta_data_desc *data,
    cmeta_param_flags exact_direction,
    int *indirect) {
  const cmeta_type_desc *value_type;
  if (param == NULL || !cmeta_data_desc_valid(data) ||
      (param->flags & CMETA_PARAM_DIRECTION_MASK) != exact_direction)
    return 0;
  value_type = plan_param_value_type(param, indirect);
  return value_type != NULL && data->storage_type != NULL &&
         cmeta_type_equal(value_type, data->storage_type);
}

static DataBindStatus plan_project_address(
    const DataBindBindingProjection *projection,
    const DataBindServiceOperation *operation,
    const DataBindSchemaField *field,
    DataBindBindingDirection direction,
    DataBindBindingAddress *out,
    DataBindBindingPlanDiagnostic *diagnostic) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status;

  if (projection == NULL || projection->size < sizeof(*projection) ||
      projection->abi_version != DATA_BIND_BINDING_PLAN_ABI_VERSION ||
      projection->id == NULL || projection->id[0] == '\0' ||
      projection->project_field == NULL || field == NULL || out == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG,
                          field != NULL ? field->name : NULL, NULL,
                          "Invalid BindingPlan projection adapter");

  *out = (DataBindBindingAddress)DATA_BIND_BINDING_ADDRESS_INIT;
  status = projection->project_field(
      projection->context, operation, field, direction, out, &error);
  if (status != DATA_BIND_OK)
    return plan_diag_fail(
        diagnostic, status, field->name, NULL,
        "%s", error.message[0] != '\0'
                  ? error.message
                  : "Projection adapter rejected logical field");

  if (out->size < sizeof(*out) ||
      out->binding_class < DATA_BIND_BINDING_VALUE ||
      out->binding_class > DATA_BIND_BINDING_ERROR)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                          field->name, NULL,
                          "Projection adapter returned invalid BindingAddress");

  return DATA_BIND_OK;
}

static void plan_entry_owned_clear(DataBindBindingPlanEntryOwned *entry) {
  if (entry == NULL) return;
  free(entry->space);
  free(entry->name);
  free(entry->schema_field);
  free(entry->function_param);
  free(entry->default_value);
  free(entry->format);
  memset(entry, 0, sizeof(*entry));
}

static int plan_entry_set_strings(
    DataBindBindingPlanEntryOwned *entry, const char *space, const char *name,
    const char *schema_field, const char *function_param,
    const char *default_value, const char *format) {
  entry->space = plan_strdup(space);
  entry->name = plan_strdup(name);
  entry->schema_field = plan_strdup(schema_field);
  entry->function_param = plan_strdup(function_param);
  entry->default_value = plan_strdup(default_value);
  entry->format = plan_strdup(format);

  if ((space != NULL && entry->space == NULL) ||
      (name != NULL && entry->name == NULL) ||
      (schema_field != NULL && entry->schema_field == NULL) ||
      (function_param != NULL && entry->function_param == NULL) ||
      (default_value != NULL && entry->default_value == NULL) ||
      (format != NULL && entry->format == NULL))
    return 0;

  entry->view.address.space = entry->space;
  entry->view.address.name = entry->name;
  entry->view.schema_field = entry->schema_field;
  entry->view.function_param = entry->function_param;
  entry->view.default_value = entry->default_value;
  entry->view.format = entry->format;
  return 1;
}


static DataBindStatus plan_compile_default_token(
    DataBindBindingPlanEntryOwned *owned,
    DataBindBindingPlanDiagnostic *diagnostic) {
  const char *text;
  char *end = NULL;
  const cmeta_data_desc *data;

  if (owned == NULL || !owned->view.has_default) return DATA_BIND_OK;
  text = owned->view.default_value;
  data = owned->view.data;
  if (text == NULL || data == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                          owned != NULL ? owned->view.schema_field : NULL,
                          owned != NULL ? owned->view.function_param : NULL,
                          "Default metadata is incomplete");

  errno = 0;
  switch (data->kind) {
  case CMETA_DATA_BOOL:
    owned->default_token.kind = CSERDE_BOOL;
    if (strcmp(text, "true") == 0 || strcmp(text, "1") == 0)
      owned->default_token.value.boolean = true;
    else if (strcmp(text, "false") == 0 || strcmp(text, "0") == 0)
      owned->default_token.value.boolean = false;
    else
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                            owned->view.schema_field,
                            owned->view.function_param,
                            "Boolean default '%s' is invalid", text);
    break;
  case CMETA_DATA_SINT: {
    long long value = strtoll(text, &end, 10);
    if (errno != 0 || end == text || end == NULL || *end != '\0')
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                            owned->view.schema_field,
                            owned->view.function_param,
                            "Signed default '%s' is invalid", text);
    owned->default_token.kind = CSERDE_SINT;
    owned->default_token.value.sint = (int64_t)value;
    break;
  }
  case CMETA_DATA_UINT: {
    unsigned long long value;
    if (text[0] == '-')
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                            owned->view.schema_field,
                            owned->view.function_param,
                            "Unsigned default '%s' is invalid", text);
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || end == NULL || *end != '\0')
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                            owned->view.schema_field,
                            owned->view.function_param,
                            "Unsigned default '%s' is invalid", text);
    owned->default_token.kind = CSERDE_UINT;
    owned->default_token.value.uint = (uint64_t)value;
    break;
  }
  case CMETA_DATA_FLOAT: {
    double value = strtod(text, &end);
    if (errno != 0 || end == text || end == NULL || *end != '\0')
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                            owned->view.schema_field,
                            owned->view.function_param,
                            "Floating default '%s' is invalid", text);
    owned->default_token.kind = CSERDE_FLOAT;
    owned->default_token.value.floating = value;
    break;
  }
  case CMETA_DATA_STRING:
  case CMETA_DATA_ENUM:
    owned->default_token.kind = CSERDE_STRING;
    owned->default_token.value.slice.data =
        (const unsigned char *)owned->view.default_value;
    owned->default_token.value.slice.size =
        strlen(owned->view.default_value);
    owned->default_token.value.slice.lifetime = CSERDE_VIEW_STABLE;
    break;
  case CMETA_DATA_BYTES:
    owned->default_token.kind = CSERDE_BYTES;
    owned->default_token.value.slice.data =
        (const unsigned char *)owned->view.default_value;
    owned->default_token.value.slice.size =
        strlen(owned->view.default_value);
    owned->default_token.value.slice.lifetime = CSERDE_VIEW_STABLE;
    break;
  default:
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, owned->view.schema_field,
        owned->view.function_param,
        "Default for field '%s' uses unsupported native semantics",
        owned->view.schema_field != NULL ? owned->view.schema_field
                                         : "<unnamed>");
  }

  owned->has_default_token = 1;
  return DATA_BIND_OK;
}

static int plan_return_value_safe(const cmeta_data_desc *data) {
  const cmeta_trait_flags required =
      CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY;
  if (!cmeta_data_desc_valid(data) || data->storage_type == NULL)
    return 0;
  return cmeta_type_require_traits(data->storage_type, required) == CMETA_OK;
}

static DataBindStatus plan_compile_ingress(
    DataBind *codec, const DataBindServiceOperation *operation,
    const DataBindBindingProjection *projection,
    const DataBindServiceNativeBinding *native,
    DataBindBindingPlan *plan, unsigned char *param_used,
    DataBindBindingPlanDiagnostic *diagnostic) {
  const cmeta_data_struct_shape *shape = plan_struct_shape(native->request);
  size_t field_count =
      data_bind_schema_field_count(codec, operation->request_type);
  int root_indirect = 0;
  size_t root_param = plan_find_root_param(
      native->function, native->request->data->storage_type,
      CMETA_PARAM_IN, &root_indirect);
  size_t i;

  if (root_param == SIZE_MAX - 1u)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, operation->request_type, NULL,
        "More than one reflected IN parameter matches request type '%s'",
        operation->request_type);

  plan->has_request_root_param = root_param != SIZE_MAX;
  plan->request_root_param = root_param;
  plan->ingress = (DataBindBindingPlanEntryOwned *)calloc(
      field_count, sizeof(*plan->ingress));
  if (field_count != 0u && plan->ingress == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_OOM, NULL, NULL,
                          "Could not allocate ingress BindingPlan");

  for (i = 0u; i < field_count; ++i) {
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    DataBindBindingPlanEntryOwned *owned = &plan->ingress[i];
    DataBindBindingPlanEntry *entry = &owned->view;
    DataBindBindingAddress address = DATA_BIND_BINDING_ADDRESS_INIT;
    const cmeta_data_field_desc *native_field;
    const DataBindNativePresenceBinding *presence;
    const cmeta_param_desc *param;
    size_t param_index;
    int indirect = 0;
    DataBindStatus status;

    if (!data_bind_schema_field_at(codec, operation->request_type, i, &field))
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA, NULL, NULL,
                            "Could not reflect request field %zu", i);

    native_field = &shape->fields[i];
    if (native_field->name == NULL ||
        strcmp(native_field->name, field.name) != 0)
      native_field = plan_native_field(native->request, field.name);
    if (native_field == NULL || native_field->value == NULL)
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
                            field.name, NULL,
                            "Native request field '%s' is unavailable",
                            field.name);

    status = plan_project_address(
        projection, operation, &field, DATA_BIND_BINDING_INGRESS,
        &address, diagnostic);
    if (status != DATA_BIND_OK) return status;
    address.ordinal = i;

    presence = plan_presence(native->request, field.name);

    if (root_param != SIZE_MAX) {
      param_index = root_param;
      param = &native->function->params[param_index];
      indirect = root_indirect;
      param_used[param_index] = 1u;
      plan->param_ingress[param_index] = 1u;

      if (field.is_optional && presence == NULL)
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, field.name, param->name,
            "Optional root request field '%s' lacks native presence metadata",
            field.name);
    } else {
      param_index = plan_find_param_by_name(native->function, field.name);
      if (param_index == SIZE_MAX)
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field.name, NULL,
            "No reflected IN parameter binds request field '%s'", field.name);
      param = &native->function->params[param_index];
      if (!plan_param_type_matches_data(param, native_field->value,
                                        CMETA_PARAM_IN, &indirect))
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field.name, param->name,
            "Reflected IN parameter '%s' does not match request field '%s'",
            param->name, field.name);
      if (param_used[param_index])
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, field.name, param->name,
            "Function parameter '%s' is bound more than once", param->name);
      if (field.is_optional && !field.has_default)
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, field.name, param->name,
            "Direct optional parameter '%s' has no native presence/default "
            "representation", param->name);
      param_used[param_index] = 1u;
      plan->param_ingress[param_index] = 1u;
      plan->param_data[param_index] = native_field->value;
      presence = NULL;
    }

    *entry = (DataBindBindingPlanEntry)DATA_BIND_BINDING_PLAN_ENTRY_INIT;
    entry->direction = DATA_BIND_BINDING_INGRESS;
    entry->address = address;
    entry->function_param_index = param_index;
    entry->data = native_field->value;
    entry->native_offset =
        root_param != SIZE_MAX ? native_field->offset : 0u;
    entry->parameter_indirect = indirect;
    entry->required = !field.is_optional && !field.has_default;
    entry->has_default = field.has_default;

    if (presence != NULL) {
      entry->has_presence = 1;
      entry->presence_offset = presence->byte_offset;
      entry->presence_bit = presence->bit;
    }

    if (!plan_entry_set_strings(
            owned, address.space, address.name, field.name, param->name,
            field.has_default ? field.default_value : NULL, field.format))
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_OOM, field.name,
                            param->name,
                            "Could not copy ingress BindingPlan metadata");
    plan->ingress_count = i + 1u;

    status = plan_compile_default_token(owned, diagnostic);
    if (status != DATA_BIND_OK) return status;
  }

  return DATA_BIND_OK;
}

static DataBindStatus plan_compile_egress(
    DataBind *codec, const DataBindServiceOperation *operation,
    const DataBindBindingProjection *projection,
    const DataBindServiceNativeBinding *native,
    DataBindBindingPlan *plan, unsigned char *param_used,
    DataBindBindingPlanDiagnostic *diagnostic) {
  const cmeta_data_struct_shape *shape;
  size_t field_count;
  size_t root_param = SIZE_MAX;
  int root_indirect = 0;
  int use_return = 0;
  size_t i;

  if (strcmp(operation->response_type, "void") == 0) {
    if (native->function->return_type == NULL ||
        !cmeta_type_equal(native->function->return_type, &cmeta_type_void))
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
          operation->response_type, NULL,
          "void Service response requires a void native return type");
    return DATA_BIND_OK;
  }
  if (native->response == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                          operation->response_type, NULL,
                          "Service response requires a native type binding");

  shape = plan_struct_shape(native->response);
  field_count = data_bind_schema_field_count(codec, operation->response_type);

  root_param = plan_find_root_param(
      native->function, native->response->data->storage_type,
      CMETA_PARAM_OUT, &root_indirect);
  if (root_param == SIZE_MAX - 1u)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, operation->response_type, NULL,
        "More than one reflected OUT parameter matches response type '%s'",
        operation->response_type);

  if (native->function->return_type != NULL &&
      native->function->return_type->kind == CMETA_T_POINTER &&
      native->function->return_type->pointee != NULL &&
      cmeta_type_equal(native->function->return_type->pointee,
                       native->response->data->storage_type))
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, operation->response_type, NULL,
        "Pointer return for response '%s' has no canonical ownership contract; "
        "use caller-provided OUT storage",
        operation->response_type);

  if (native->function->return_type != NULL &&
      native->function->return_type->kind != CMETA_T_POINTER &&
      cmeta_type_equal(native->function->return_type,
                       native->response->data->storage_type)) {
    if (!plan_return_value_safe(native->response->data))
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
          operation->response_type, NULL,
          "By-value response '%s' lacks trivial copy/destroy ownership traits",
          operation->response_type);
    use_return = 1;
  }

  if (root_param != SIZE_MAX && use_return)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, operation->response_type, NULL,
        "Response '%s' is represented by both return value and OUT parameter",
        operation->response_type);

  plan->response_uses_return = use_return;
  plan->has_response_root_param = root_param != SIZE_MAX;
  plan->response_root_param = root_param;

  plan->egress = (DataBindBindingPlanEntryOwned *)calloc(
      field_count, sizeof(*plan->egress));
  if (field_count != 0u && plan->egress == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_OOM, NULL, NULL,
                          "Could not allocate egress BindingPlan");

  for (i = 0u; i < field_count; ++i) {
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    DataBindBindingPlanEntryOwned *owned = &plan->egress[i];
    DataBindBindingPlanEntry *entry = &owned->view;
    DataBindBindingAddress address = DATA_BIND_BINDING_ADDRESS_INIT;
    const cmeta_data_field_desc *native_field;
    const DataBindNativePresenceBinding *presence;
    const cmeta_param_desc *param = NULL;
    size_t param_index = SIZE_MAX;
    int indirect = 0;
    DataBindStatus status;

    if (!data_bind_schema_field_at(codec, operation->response_type, i, &field))
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA, NULL, NULL,
                            "Could not reflect response field %zu", i);

    native_field = &shape->fields[i];
    if (native_field->name == NULL ||
        strcmp(native_field->name, field.name) != 0)
      native_field = plan_native_field(native->response, field.name);
    if (native_field == NULL || native_field->value == NULL)
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
                            field.name, NULL,
                            "Native response field '%s' is unavailable",
                            field.name);

    status = plan_project_address(
        projection, operation, &field, DATA_BIND_BINDING_EGRESS,
        &address, diagnostic);
    if (status != DATA_BIND_OK) return status;
    address.ordinal = i;

    presence = plan_presence(native->response, field.name);

    if (root_param != SIZE_MAX) {
      param_index = root_param;
      param = &native->function->params[param_index];
      indirect = root_indirect;
      param_used[param_index] = 1u;
      plan->param_egress[param_index] = 1u;
      plan->param_data[param_index] = native->response->data;

      if (field.is_optional && presence == NULL)
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, field.name, param->name,
            "Optional root response field '%s' lacks native presence metadata",
            field.name);
    } else if (!use_return) {
      param_index = plan_find_param_by_name(native->function, field.name);
      if (param_index == SIZE_MAX)
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field.name, NULL,
            "No reflected OUT parameter binds response field '%s'", field.name);
      param = &native->function->params[param_index];
      if (!plan_param_type_matches_data(param, native_field->value,
                                        CMETA_PARAM_OUT, &indirect))
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field.name, param->name,
            "Reflected OUT parameter '%s' does not match response field '%s'",
            param->name, field.name);
      if (param_used[param_index])
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, field.name, param->name,
            "Function parameter '%s' is bound more than once", param->name);
      if (field.is_optional)
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, field.name, param->name,
            "Direct optional OUT parameter '%s' has no native presence "
            "representation", param->name);
      param_used[param_index] = 1u;
      plan->param_egress[param_index] = 1u;
      plan->param_data[param_index] = native_field->value;
      presence = NULL;
    } else if (field.is_optional && presence == NULL) {
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, field.name, NULL,
          "Optional returned response field '%s' lacks native presence metadata",
          field.name);
    }

    *entry = (DataBindBindingPlanEntry)DATA_BIND_BINDING_PLAN_ENTRY_INIT;
    entry->direction = DATA_BIND_BINDING_EGRESS;
    entry->address = address;
    entry->function_param_index = param_index;
    entry->data = native_field->value;
    entry->native_offset =
        (root_param != SIZE_MAX || use_return) ? native_field->offset : 0u;
    entry->parameter_indirect = indirect;
    entry->target_is_return = use_return;
    entry->required = 1;

    if (presence != NULL) {
      entry->has_presence = 1;
      entry->presence_offset = presence->byte_offset;
      entry->presence_bit = presence->bit;
    }

    if (!plan_entry_set_strings(
            owned, address.space, address.name, field.name,
            param != NULL ? param->name : NULL, NULL, field.format))
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_OOM, field.name,
                            param != NULL ? param->name : NULL,
                            "Could not copy egress BindingPlan metadata");
    plan->egress_count = i + 1u;
  }

  return DATA_BIND_OK;
}

static DataBindStatus plan_copy_errors(
    DataBind *codec, const char *service_name, const char *operation_name,
    const DataBindServiceOperation *operation, DataBindBindingPlan *plan,
    DataBindBindingPlanDiagnostic *diagnostic) {
  size_t i;
  plan->error_count = operation->error_count;
  if (plan->error_count == 0u) return DATA_BIND_OK;

  plan->errors = (char **)calloc(plan->error_count, sizeof(*plan->errors));
  if (plan->errors == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_OOM, NULL, NULL,
                          "Could not allocate typed-error list");

  for (i = 0u; i < plan->error_count; ++i) {
    const char *name = data_bind_service_operation_error_at(
        codec, service_name, operation_name, i);
    plan->errors[i] = plan_strdup(name);
    if (name == NULL || plan->errors[i] == NULL)
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_OOM, NULL, NULL,
                            "Could not copy typed-error metadata");
  }
  return DATA_BIND_OK;
}

void data_bind_binding_plan_free(DataBindBindingPlan *plan) {
  size_t i;
  if (plan == NULL) return;
  for (i = 0u; i < plan->ingress_count; ++i)
    plan_entry_owned_clear(&plan->ingress[i]);
  for (i = 0u; i < plan->egress_count; ++i)
    plan_entry_owned_clear(&plan->egress[i]);
  for (i = 0u; i < plan->error_count; ++i)
    free(plan->errors[i]);
  free(plan->errors);
  free(plan->param_data);
  free(plan->param_ingress);
  free(plan->param_egress);
  free(plan->ingress);
  free(plan->egress);
  free(plan->operation_id);
  free(plan->projection_id);
  free(plan);
}

DataBindStatus data_bind_binding_plan_compile_service(
    DataBind *codec, const char *service_name, const char *operation_name,
    const DataBindBindingProjection *projection,
    const DataBindServiceNativeBinding *native,
    DataBindBindingPlan **out_plan,
    DataBindBindingPlanDiagnostic *diagnostic) {
  DataBindServiceOperation operation = DATA_BIND_SERVICE_OPERATION_INIT;
  DataBindBindingPlan *plan = NULL;
  unsigned char *param_used = NULL;
  DataBindStatus status;
  size_t i;

  if (!plan_diag_header_valid(diagnostic)) return DATA_BIND_ERR_INVALID_ARG;
  plan_diag_clear(diagnostic);

  if (out_plan != NULL) *out_plan = NULL;
  if (codec == NULL || service_name == NULL || service_name[0] == '\0' ||
      operation_name == NULL || operation_name[0] == '\0' ||
      projection == NULL || projection->size < sizeof(*projection) ||
      projection->abi_version != DATA_BIND_BINDING_PLAN_ABI_VERSION ||
      projection->id == NULL || projection->id[0] == '\0' ||
      projection->project_field == NULL ||
      native == NULL || out_plan == NULL ||
      native->size <
          offsetof(DataBindServiceNativeBinding, response) +
              sizeof(native->response) ||
      native->abi_version != DATA_BIND_BINDING_PLAN_ABI_VERSION ||
      !cmeta_function_desc_valid(native->function) ||
      native->request == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                          "Invalid DataBind Service BindingPlan arguments");

  if (!data_bind_service_operation_find(codec, service_name, operation_name,
                                        &operation))
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_TYPE_NOT_FOUND, NULL, NULL,
        "DataBind Service operation '%s.%s' was not found",
        service_name, operation_name);

  status = plan_validate_native_type(
      codec, native->request, operation.request_type, diagnostic);
  if (status != DATA_BIND_OK) return status;

  if (strcmp(operation.response_type, "void") != 0) {
    if (native->response == NULL)
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                            operation.response_type, NULL,
                            "Response native binding is required");
    status = plan_validate_native_type(
        codec, native->response, operation.response_type, diagnostic);
    if (status != DATA_BIND_OK) return status;
  } else if (native->response != NULL) {
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, operation.response_type, NULL,
        "void Service response must not publish a native response binding");
  }

  plan = (DataBindBindingPlan *)calloc(1u, sizeof(*plan));
  if (plan == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_OOM, NULL, NULL,
                          "Could not allocate DataBind BindingPlan");

  plan->operation_id = plan_operation_id(service_name, operation_name);
  plan->projection_id = plan_strdup(projection->id);
  plan->function = native->function;
  plan->request = native->request;
  plan->response = native->response;
  if (plan->operation_id == NULL || plan->projection_id == NULL) {
    status = plan_diag_fail(diagnostic, DATA_BIND_ERR_OOM, NULL, NULL,
                            "Could not copy BindingPlan identity");
    goto fail;
  }

  plan->param_count = native->function->param_count;
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
                              "Could not allocate function runtime binding map");
      goto fail;
    }
  }

  param_used = (unsigned char *)calloc(
      native->function->param_count != 0u ? native->function->param_count : 1u,
      sizeof(*param_used));
  if (param_used == NULL) {
    status = plan_diag_fail(diagnostic, DATA_BIND_ERR_OOM, NULL, NULL,
                            "Could not allocate function binding map");
    goto fail;
  }

  status = plan_compile_ingress(codec, &operation, projection, native,
                                plan, param_used, diagnostic);
  if (status != DATA_BIND_OK) goto fail;

  status = plan_compile_egress(codec, &operation, projection, native,
                               plan, param_used, diagnostic);
  if (status != DATA_BIND_OK) goto fail;

  for (i = 0u; i < native->function->param_count; ++i) {
    if (!param_used[i]) {
      status = plan_diag_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, NULL,
          native->function->params[i].name,
          "Reflected function parameter '%s' is not owned by the Service "
          "contract; context injection is not admitted by this compiler slice",
          native->function->params[i].name);
      goto fail;
    }
  }

  status = plan_copy_errors(codec, service_name, operation_name, &operation,
                            plan, diagnostic);
  if (status != DATA_BIND_OK) goto fail;

  free(param_used);
  plan_diag_clear(diagnostic);
  *out_plan = plan;
  return DATA_BIND_OK;

fail:
  free(param_used);
  data_bind_binding_plan_free(plan);
  return status;
}

const char *data_bind_binding_plan_operation_id(
    const DataBindBindingPlan *plan) {
  return plan != NULL ? plan->operation_id : NULL;
}

const char *data_bind_binding_plan_projection_id(
    const DataBindBindingPlan *plan) {
  return plan != NULL ? plan->projection_id : NULL;
}

const cmeta_function_desc *data_bind_binding_plan_function(
    const DataBindBindingPlan *plan) {
  return plan != NULL ? plan->function : NULL;
}

size_t data_bind_binding_plan_ingress_count(
    const DataBindBindingPlan *plan) {
  return plan != NULL ? plan->ingress_count : 0u;
}

size_t data_bind_binding_plan_egress_count(
    const DataBindBindingPlan *plan) {
  return plan != NULL ? plan->egress_count : 0u;
}

static int plan_entry_copy(
    const DataBindBindingPlanEntryOwned *owned,
    DataBindBindingPlanEntry *out) {
  size_t size;
  if (owned == NULL || out == NULL || out->size < sizeof(size_t)) return 0;
  size = plan_out_size(out->size, sizeof(*out));
  memcpy(out, &owned->view, size);
  if (size >= sizeof(size_t)) out->size = size;
  return 1;
}

int data_bind_binding_plan_ingress_at(
    const DataBindBindingPlan *plan, size_t index,
    DataBindBindingPlanEntry *out) {
  if (plan == NULL || index >= plan->ingress_count) return 0;
  return plan_entry_copy(&plan->ingress[index], out);
}

int data_bind_binding_plan_egress_at(
    const DataBindBindingPlan *plan, size_t index,
    DataBindBindingPlanEntry *out) {
  if (plan == NULL || index >= plan->egress_count) return 0;
  return plan_entry_copy(&plan->egress[index], out);
}

size_t data_bind_binding_plan_error_count(
    const DataBindBindingPlan *plan) {
  return plan != NULL ? plan->error_count : 0u;
}

const char *data_bind_binding_plan_error_at(
    const DataBindBindingPlan *plan, size_t index) {
  if (plan == NULL || index >= plan->error_count) return NULL;
  return plan->errors[index];
}
