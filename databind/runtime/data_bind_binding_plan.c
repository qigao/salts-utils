#include "data_bind_binding_plan.h"
#include "data_bind_validation_plan.h"
#include "data_bind_validation_plan_internal.h"

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
  size_t validation_rule_start;
  size_t validation_rule_count;
} DataBindBindingPlanEntryOwned;

struct DataBindBindingPlan {
  char *operation_id;
  char *projection_id;
  const cmeta_function_desc *function;
  const DataBindNativeTypeBinding *request;
  const DataBindNativeTypeBinding *response;

  DataBindBindingPlanEntryOwned *ingress;
  size_t ingress_count;
  DataBindValidationPlan *request_validation;
  DataBindBindingPlanEntryOwned *egress;
  size_t egress_count;
  DataBindValidationPlan *response_validation;

  DataBindBindingPlanEntryOwned *errors;
  size_t error_count;
  size_t error_param_index;
  size_t error_envelope_bytes;
  size_t error_kind_offset;
  size_t error_kind_bytes;
  int has_error_param;

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

static const DataBindNativeStateBinding *plan_state_binding(
    const DataBindNativeStateBinding *bindings, size_t count,
    const char *name) {
  size_t i;
  if (bindings == NULL || name == NULL) return NULL;
  for (i = 0u; i < count; ++i) {
    const DataBindNativeStateBinding *state = &bindings[i];
    if (state->field_name != NULL && strcmp(state->field_name, name) == 0)
      return state;
  }
  return NULL;
}

static const DataBindNativeStateBinding *plan_presence(
    const DataBindNativeTypeBinding *binding, const char *name) {
  return binding != NULL
             ? plan_state_binding(binding->presence, binding->presence_count, name)
             : NULL;
}

static const DataBindNativeStateBinding *plan_null(
    const DataBindNativeTypeBinding *binding, const char *name) {
  return binding != NULL
             ? plan_state_binding(binding->nulls, binding->null_count, name)
             : NULL;
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
          offsetof(DataBindNativeTypeBinding, null_count) +
              sizeof(binding->null_count) ||
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

  if ((binding->presence_count != 0u && binding->presence == NULL) ||
      (binding->null_count != 0u && binding->nulls == NULL))
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, expected_name, NULL,
        "Native state metadata count is nonzero without its binding array");

  {
    const DataBindNativeStateBinding *sets[2] = {
        binding->presence, binding->nulls};
    const size_t counts[2] = {
        binding->presence_count, binding->null_count};
    const char *const labels[2] = {"presence", "null"};
    size_t set_index;

    for (set_index = 0u; set_index < 2u; ++set_index) {
      size_t state_index;
      for (state_index = 0u; state_index < counts[set_index]; ++state_index) {
        const DataBindNativeStateBinding *left =
            &sets[set_index][state_index];
        DataBindSchemaField reflected = DATA_BIND_SCHEMA_FIELD_INIT;
        int found = 0;
        size_t j;

        if (left->size < sizeof(*left) || left->field_name == NULL ||
            left->field_name[0] == '\0' || left->bit > 7u ||
            left->byte_offset >= binding->data->storage_type->size)
          return plan_diag_fail(
              diagnostic, DATA_BIND_ERR_SCHEMA, expected_name, NULL,
              "Invalid native %s-state metadata", labels[set_index]);

        for (j = 0u; j < shape->field_count; ++j) {
          const cmeta_data_field_desc *native_field = &shape->fields[j];
          size_t field_size;
          if (native_field->value == NULL ||
              native_field->value->storage_type == NULL)
            return plan_diag_fail(
                diagnostic, DATA_BIND_ERR_SCHEMA, expected_name, NULL,
                "Native field metadata is incomplete while validating %s state",
                labels[set_index]);
          field_size = native_field->value->storage_type->size;
          if (left->byte_offset >= native_field->offset &&
              left->byte_offset - native_field->offset < field_size)
            return plan_diag_fail(
                diagnostic, DATA_BIND_ERR_SCHEMA, left->field_name, NULL,
                "Native %s state storage overlaps field '%s'",
                labels[set_index],
                native_field->name != NULL ? native_field->name : "<unnamed>");
        }

        for (j = 0u; j < schema_type.field_count; ++j) {
          reflected = (DataBindSchemaField)DATA_BIND_SCHEMA_FIELD_INIT;
          if (data_bind_schema_field_at(codec, expected_name, j, &reflected) &&
              reflected.name != NULL &&
              strcmp(reflected.name, left->field_name) == 0) {
            found = set_index == 0u ? reflected.is_optional != 0
                                    : reflected.is_nullable != 0;
            break;
          }
        }
        if (!found)
          return plan_diag_fail(
              diagnostic, DATA_BIND_ERR_SCHEMA, left->field_name, NULL,
              "%s metadata references a non-%s IDL field",
              labels[set_index],
              set_index == 0u ? "optional" : "nullable");

        for (j = 0u; j < state_index; ++j) {
          const DataBindNativeStateBinding *right = &sets[set_index][j];
          if (strcmp(left->field_name, right->field_name) == 0 ||
              (left->byte_offset == right->byte_offset &&
               left->bit == right->bit))
            return plan_diag_fail(
                diagnostic, DATA_BIND_ERR_SCHEMA, left->field_name, NULL,
                "Duplicate native %s-state metadata", labels[set_index]);
        }

        if (set_index == 1u) {
          for (j = 0u; j < binding->presence_count; ++j) {
            const DataBindNativeStateBinding *right = &binding->presence[j];
            if (left->byte_offset == right->byte_offset &&
                left->bit == right->bit)
              return plan_diag_fail(
                  diagnostic, DATA_BIND_ERR_SCHEMA, left->field_name, NULL,
                  "Native presence and null state must not share one bit");
          }
        }
      }
    }
  }

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
      const DataBindNativeStateBinding *presence =
          plan_presence(binding, schema_field.name);
      if (presence == NULL)
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, schema_field.name, NULL,
            "Optional field '%s.%s' lacks native presence state",
            expected_name, schema_field.name);
    }
    if (schema_field.is_nullable) {
      const DataBindNativeStateBinding *null_state =
          plan_null(binding, schema_field.name);
      if (null_state == NULL)
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, schema_field.name, NULL,
            "Nullable field '%s.%s' lacks native null state",
            expected_name, schema_field.name);
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
    const DataBindNativeStateBinding *presence;
    const DataBindNativeStateBinding *null_state;
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
    null_state = plan_null(native->request, field.name);

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
      if (field.is_nullable && null_state == NULL)
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, field.name, param->name,
            "Nullable root request field '%s' lacks native null metadata",
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
      if (field.is_nullable)
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, field.name, param->name,
            "Direct nullable parameter '%s' has no native null-state "
            "representation", param->name);
      param_used[param_index] = 1u;
      plan->param_ingress[param_index] = 1u;
      plan->param_data[param_index] = native_field->value;
      presence = NULL;
      null_state = NULL;
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
    entry->nullable = field.is_nullable;

    if (presence != NULL) {
      entry->has_presence = 1;
      entry->presence_offset = presence->byte_offset;
      entry->presence_bit = presence->bit;
    }
    if (null_state != NULL) {
      entry->has_null = 1;
      entry->null_offset = null_state->byte_offset;
      entry->null_bit = null_state->bit;
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

static DataBindBindingPlanEntryOwned *plan_ingress_by_field(
    DataBindBindingPlan *plan, const char *field_name) {
  size_t i;
  if (plan == NULL || field_name == NULL) return NULL;
  for (i = 0u; i < plan->ingress_count; ++i) {
    DataBindBindingPlanEntryOwned *owned = &plan->ingress[i];
    if (owned->view.schema_field != NULL &&
        strcmp(owned->view.schema_field, field_name) == 0)
      return owned;
  }
  return NULL;
}

static DataBindStatus plan_compile_ingress_validation(
    DataBind *codec, const char *request_type, DataBindBindingPlan *plan,
    DataBindBindingPlanDiagnostic *diagnostic) {
  DataBindValidationPlan *validation = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;
  size_t rule_count;
  size_t rule_index;
  DataBindStatus status;

  if (codec == NULL || request_type == NULL || plan == NULL)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
        "Invalid BindingPlan ValidationPlan compile arguments");

  status = data_bind_validation_plan_compile(
      codec, request_type, &validation, &error);
  if (status != DATA_BIND_OK)
    return plan_diag_fail(
        diagnostic, status, error.path[0] != '\0' ? error.path : request_type,
        NULL, "%s",
        error.message[0] != '\0'
            ? error.message
            : "Could not compile request ValidationPlan");

  rule_count = data_bind_validation_plan_rule_count(validation);
  if (data_bind_validation_plan_internal_child_count(validation) != 0u) {
    data_bind_validation_plan_free(validation);
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, request_type, NULL,
        "Nested native ValidationPlan execution is not admitted by this "
        "BindingPlan slice");
  }

  if (rule_count == 0u) {
    data_bind_validation_plan_free(validation);
    return DATA_BIND_OK;
  }

  for (rule_index = 0u; rule_index < rule_count; ++rule_index) {
    DataBindValidationRuleInfo info = {0};
    DataBindBindingPlanEntryOwned *owned;
    const cmeta_data_buffer_ops *buffer_ops = NULL;

    if (!data_bind_validation_plan_internal_rule_info(
            validation, rule_index, &info) ||
        info.field_name == NULL) {
      data_bind_validation_plan_free(validation);
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, request_type, NULL,
          "Compiled ValidationPlan rule has no field identity");
    }

    owned = plan_ingress_by_field(plan, info.field_name);
    if (owned == NULL || owned->view.data == NULL) {
      data_bind_validation_plan_free(validation);
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name, NULL,
          "ValidationPlan field has no compiled ingress entry");
    }

    if (owned->view.data->kind != info.field_kind) {
      data_bind_validation_plan_free(validation);
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, info.field_name,
          owned->view.function_param,
          "ValidationPlan field kind does not match admitted native ingress");
    }

    if (info.kind == DATA_BIND_SCHEMA_CONSTRAINT_SIZE) {
      if (info.field_kind != CMETA_DATA_STRING &&
          info.field_kind != CMETA_DATA_BYTES) {
        data_bind_validation_plan_free(validation);
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name,
            owned->view.function_param,
            "Native @Size currently requires canonical string/bytes storage; "
            "container size validation awaits a canonical range provider");
      }
      buffer_ops = cmeta_data_buffer_ops_of(owned->view.data);
      if (buffer_ops == NULL || buffer_ops->read == NULL) {
        data_bind_validation_plan_free(validation);
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name,
            owned->view.function_param,
            "Native @Size requires a canonical readable buffer provider");
      }
    } else if (info.kind == DATA_BIND_SCHEMA_CONSTRAINT_PATTERN) {
      if (info.field_kind != CMETA_DATA_STRING) {
        data_bind_validation_plan_free(validation);
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name,
            owned->view.function_param,
            "Native @Pattern requires canonical string storage");
      }
      buffer_ops = cmeta_data_buffer_ops_of(owned->view.data);
      if (buffer_ops == NULL || buffer_ops->read == NULL) {
        data_bind_validation_plan_free(validation);
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name,
            owned->view.function_param,
            "Native @Pattern requires a canonical readable string provider");
      }
    } else if (info.kind != DATA_BIND_SCHEMA_CONSTRAINT_MIN &&
               info.kind != DATA_BIND_SCHEMA_CONSTRAINT_MAX) {
      data_bind_validation_plan_free(validation);
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name,
          owned->view.function_param,
          "ValidationPlan contains an unsupported native constraint kind");
    }

    if (owned->validation_rule_count == 0u) {
      owned->validation_rule_start = rule_index;
    } else if (owned->validation_rule_start +
                   owned->validation_rule_count !=
               rule_index) {
      data_bind_validation_plan_free(validation);
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name,
          owned->view.function_param,
          "ValidationPlan rules for one ingress field are not contiguous");
    }
    ++owned->validation_rule_count;
  }

  plan->request_validation = validation;
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
    const DataBindNativeStateBinding *presence;
    const DataBindNativeStateBinding *null_state;
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
    null_state = plan_null(native->response, field.name);

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
      if (field.is_nullable && null_state == NULL)
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, field.name, param->name,
            "Nullable root response field '%s' lacks native null metadata",
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
      if (field.is_nullable)
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, field.name, param->name,
            "Direct nullable OUT parameter '%s' has no native null-state "
            "representation", param->name);
      param_used[param_index] = 1u;
      plan->param_egress[param_index] = 1u;
      plan->param_data[param_index] = native_field->value;
      presence = NULL;
      null_state = NULL;
    } else if (field.is_optional && presence == NULL) {
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, field.name, NULL,
          "Optional returned response field '%s' lacks native presence metadata",
          field.name);
    } else if (field.is_nullable && null_state == NULL) {
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, field.name, NULL,
          "Nullable returned response field '%s' lacks native null metadata",
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
    entry->nullable = field.is_nullable;

    if (presence != NULL) {
      entry->has_presence = 1;
      entry->presence_offset = presence->byte_offset;
      entry->presence_bit = presence->bit;
    }
    if (null_state != NULL) {
      entry->has_null = 1;
      entry->null_offset = null_state->byte_offset;
      entry->null_bit = null_state->bit;
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


static DataBindBindingPlanEntryOwned *plan_egress_by_field(
    DataBindBindingPlan *plan, const char *field_name) {
  size_t i;
  if (plan == NULL || field_name == NULL) return NULL;
  for (i = 0u; i < plan->egress_count; ++i) {
    DataBindBindingPlanEntryOwned *owned = &plan->egress[i];
    if (owned->view.schema_field != NULL &&
        strcmp(owned->view.schema_field, field_name) == 0)
      return owned;
  }
  return NULL;
}

static DataBindStatus plan_compile_egress_validation(
    DataBind *codec, const char *response_type, DataBindBindingPlan *plan,
    DataBindBindingPlanDiagnostic *diagnostic) {
  DataBindValidationPlan *validation = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;
  size_t rule_count;
  size_t rule_index;
  DataBindStatus status;

  if (codec == NULL || response_type == NULL || plan == NULL)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
        "Invalid response ValidationPlan compile arguments");
  if (strcmp(response_type, "void") == 0) return DATA_BIND_OK;

  status = data_bind_validation_plan_compile(
      codec, response_type, &validation, &error);
  if (status != DATA_BIND_OK)
    return plan_diag_fail(
        diagnostic, status,
        error.path[0] != '\0' ? error.path : response_type,
        NULL, "%s",
        error.message[0] != '\0'
            ? error.message
            : "Could not compile response ValidationPlan");

  rule_count = data_bind_validation_plan_rule_count(validation);
  if (data_bind_validation_plan_internal_child_count(validation) != 0u) {
    data_bind_validation_plan_free(validation);
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, response_type, NULL,
        "Nested native response ValidationPlan execution is not admitted by "
        "this BindingPlan slice");
  }

  if (rule_count == 0u) {
    data_bind_validation_plan_free(validation);
    return DATA_BIND_OK;
  }

  for (rule_index = 0u; rule_index < rule_count; ++rule_index) {
    DataBindValidationRuleInfo info = {0};
    DataBindBindingPlanEntryOwned *owned;
    const cmeta_data_buffer_ops *buffer_ops = NULL;

    if (!data_bind_validation_plan_internal_rule_info(
            validation, rule_index, &info) ||
        info.field_name == NULL) {
      data_bind_validation_plan_free(validation);
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, response_type, NULL,
          "Compiled response ValidationPlan rule has no field identity");
    }

    owned = plan_egress_by_field(plan, info.field_name);
    if (owned == NULL || owned->view.data == NULL) {
      data_bind_validation_plan_free(validation);
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name, NULL,
          "ValidationPlan field has no compiled egress entry");
    }

    if (owned->view.data->kind != info.field_kind) {
      data_bind_validation_plan_free(validation);
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, info.field_name,
          owned->view.function_param,
          "ValidationPlan field kind does not match admitted native egress");
    }

    if (info.kind == DATA_BIND_SCHEMA_CONSTRAINT_SIZE) {
      if (info.field_kind != CMETA_DATA_STRING &&
          info.field_kind != CMETA_DATA_BYTES) {
        data_bind_validation_plan_free(validation);
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name,
            owned->view.function_param,
            "Native response @Size currently requires canonical string/bytes "
            "storage; container size validation awaits a canonical range "
            "provider");
      }
      buffer_ops = cmeta_data_buffer_ops_of(owned->view.data);
      if (buffer_ops == NULL || buffer_ops->read == NULL) {
        data_bind_validation_plan_free(validation);
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name,
            owned->view.function_param,
            "Native response @Size requires a canonical readable buffer "
            "provider");
      }
    } else if (info.kind == DATA_BIND_SCHEMA_CONSTRAINT_PATTERN) {
      if (info.field_kind != CMETA_DATA_STRING) {
        data_bind_validation_plan_free(validation);
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name,
            owned->view.function_param,
            "Native response @Pattern requires canonical string storage");
      }
      buffer_ops = cmeta_data_buffer_ops_of(owned->view.data);
      if (buffer_ops == NULL || buffer_ops->read == NULL) {
        data_bind_validation_plan_free(validation);
        return plan_diag_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name,
            owned->view.function_param,
            "Native response @Pattern requires a canonical readable string "
            "provider");
      }
    } else if (info.kind != DATA_BIND_SCHEMA_CONSTRAINT_MIN &&
               info.kind != DATA_BIND_SCHEMA_CONSTRAINT_MAX) {
      data_bind_validation_plan_free(validation);
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name,
          owned->view.function_param,
          "Response ValidationPlan contains an unsupported native constraint "
          "kind");
    }

    if (owned->validation_rule_count == 0u) {
      owned->validation_rule_start = rule_index;
    } else if (owned->validation_rule_start +
                   owned->validation_rule_count !=
               rule_index) {
      data_bind_validation_plan_free(validation);
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name,
          owned->view.function_param,
          "ValidationPlan rules for one egress field are not contiguous");
    }
    ++owned->validation_rule_count;
  }

  plan->response_validation = validation;
  return DATA_BIND_OK;
}

static DataBindStatus plan_compile_errors(
    DataBind *codec, const char *service_name, const char *operation_name,
    const DataBindServiceOperation *operation,
    const DataBindServiceNativeBinding *native,
    DataBindBindingPlan *plan, unsigned char *param_used,
    DataBindBindingPlanDiagnostic *diagnostic) {
  const cmeta_param_desc *param;
  size_t i;

  if (operation == NULL || native == NULL || plan == NULL ||
      param_used == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                          "Invalid typed-error BindingPlan arguments");

  if (operation->error_count == 0u) {
    if (native->size >= sizeof(*native) &&
        (native->errors != NULL || native->error_count != 0u ||
         native->error_envelope_bytes != 0u ||
         native->error_kind_bytes != 0u))
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, NULL, NULL,
          "Non-throws Service published typed-error native metadata");
    return DATA_BIND_OK;
  }

  if (native->size < sizeof(*native) || native->errors == NULL ||
      native->error_count != operation->error_count ||
      native->error_param_index >= native->function->param_count ||
      native->error_envelope_bytes == 0u ||
      native->error_kind_bytes != sizeof(uint32_t) ||
      native->error_kind_offset > native->error_envelope_bytes ||
      native->error_envelope_bytes - native->error_kind_offset <
          native->error_kind_bytes)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, NULL, NULL,
        "Typed-error Service native envelope metadata is incomplete");

  param = &native->function->params[native->error_param_index];
  if ((param->flags & CMETA_PARAM_DIRECTION_MASK) != CMETA_PARAM_OUT ||
      (param->flags & CMETA_PARAM_BORROWED) == 0 ||
      param->type == NULL || param->type->kind != CMETA_T_POINTER ||
      param->type->pointee == NULL ||
      param->type->pointee->size != native->error_envelope_bytes)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, NULL,
        param->name,
        "Typed-error function parameter does not match generated envelope ABI");

  if (param_used[native->error_param_index])
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, NULL, param->name,
        "Typed-error function parameter is already owned by another binding");

  plan->errors = (DataBindBindingPlanEntryOwned *)calloc(
      operation->error_count, sizeof(*plan->errors));
  if (plan->errors == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_OOM, NULL, param->name,
                          "Could not allocate typed-error BindingPlan entries");
  plan->error_count = operation->error_count;

  for (i = 0u; i < operation->error_count; ++i) {
    const DataBindNativeErrorBinding *binding = &native->errors[i];
    DataBindBindingPlanEntryOwned *owned = &plan->errors[i];
    DataBindBindingPlanEntry *entry = &owned->view;
    DataBindNativeTypeBinding payload_binding;
    const cmeta_data_desc *data = NULL;
    const char *name = data_bind_service_operation_error_at(
        codec, service_name, operation_name, i);
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindStatus status;

    if (name == NULL || binding->size < sizeof(*binding) ||
        binding->idl_type_name == NULL ||
        strcmp(binding->idl_type_name, name) != 0 ||
        binding->kind_value != (uint32_t)(i + 1u) ||
        binding->data_resolver == NULL ||
        binding->payload_offset > native->error_envelope_bytes)
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, name, param->name,
          "Typed-error native variant %zu does not match the throws contract",
          i);

    status = binding->data_resolver(&data, &error);
    if (status != DATA_BIND_OK || !cmeta_data_desc_valid(data) ||
        data->storage_type == NULL)
      return plan_diag_fail(
          diagnostic,
          status != DATA_BIND_OK ? status : DATA_BIND_ERR_SCHEMA,
          name, param->name, "%s",
          error.message[0] != '\0'
              ? error.message
              : "Typed-error payload CMeta descriptor is unavailable");

    payload_binding = (DataBindNativeTypeBinding)
        DATA_BIND_NATIVE_TYPE_BINDING_INIT(name, data);
    status = plan_validate_native_type(
        codec, &payload_binding, name, diagnostic);
    if (status != DATA_BIND_OK) return status;

    if (binding->payload_offset > native->error_envelope_bytes ||
        data->storage_type->size >
            native->error_envelope_bytes - binding->payload_offset)
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, name, param->name,
          "Typed-error payload lies outside the generated error envelope");

    *entry = (DataBindBindingPlanEntry)DATA_BIND_BINDING_PLAN_ENTRY_INIT;
    entry->direction = DATA_BIND_BINDING_EGRESS;
    entry->address.binding_class = DATA_BIND_BINDING_ERROR;
    entry->address.ordinal = i;
    entry->function_param_index = native->error_param_index;
    entry->data = data;
    entry->native_offset = binding->payload_offset;
    entry->parameter_indirect = 1;
    entry->required = 1;

    if (!plan_entry_set_strings(
            owned, "service.error", name, name, param->name, NULL, NULL))
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_OOM, name, param->name,
          "Could not copy typed-error BindingPlan metadata");
  }

  plan->error_param_index = native->error_param_index;
  plan->error_envelope_bytes = native->error_envelope_bytes;
  plan->error_kind_offset = native->error_kind_offset;
  plan->error_kind_bytes = native->error_kind_bytes;
  plan->has_error_param = 1;
  param_used[native->error_param_index] = 1u;
  plan->param_egress[native->error_param_index] = 1u;
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
    plan_entry_owned_clear(&plan->errors[i]);
  free(plan->errors);
  data_bind_validation_plan_free(plan->request_validation);
  data_bind_validation_plan_free(plan->response_validation);
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

  status = plan_compile_ingress_validation(
      codec, operation.request_type, plan, diagnostic);
  if (status != DATA_BIND_OK) goto fail;

  status = plan_compile_egress(codec, &operation, projection, native,
                               plan, param_used, diagnostic);
  if (status != DATA_BIND_OK) goto fail;

  status = plan_compile_egress_validation(
      codec, operation.response_type, plan, diagnostic);
  if (status != DATA_BIND_OK) goto fail;

  status = plan_compile_errors(
      codec, service_name, operation_name, &operation, native,
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
  return plan->errors[index].view.schema_field;
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

static const cserde_reader_ops PLAN_DEFAULT_READER_OPS = {
    offsetof(cserde_reader_ops, next) + sizeof(cserde_reader_next_fn),
    CSERDE_READER_OPS_ABI_VERSION,
    plan_default_reader_next};

static int plan_provider_valid_for_input(
    const DataBindBindingProvider *provider) {
  return provider != NULL && provider->size >= sizeof(*provider) &&
         provider->abi_version == DATA_BIND_BINDING_PLAN_ABI_VERSION &&
         provider->open_input != NULL;
}

static int plan_provider_valid_for_output(
    const DataBindBindingProvider *provider) {
  return provider != NULL && provider->size >= sizeof(*provider) &&
         provider->abi_version == DATA_BIND_BINDING_PLAN_ABI_VERSION &&
         provider->begin_output != NULL &&
         provider->write_output != NULL &&
         provider->commit_output != NULL &&
         provider->abort_output != NULL;
}

static DataBindStatus plan_runtime_fail_error(
    DataBindBindingPlanDiagnostic *diagnostic, DataBindStatus status,
    const DataBindBindingPlanEntry *entry, const DataBindError *error,
    const char *fallback) {
  const char *message = fallback;
  if (error != NULL && error->message[0] != '\0') message = error->message;
  return plan_diag_fail(
      diagnostic, status,
      entry != NULL ? entry->schema_field : NULL,
      entry != NULL ? entry->function_param : NULL,
      "%s", message != NULL ? message : "BindingPlan runtime failure");
}

static DataBindStatus plan_frame_preflight(
    const DataBindBindingPlan *plan,
    const DataBindBindingCallFrame *frame,
    DataBindBindingPlanDiagnostic *diagnostic) {
  size_t i;

  if (plan == NULL || frame == NULL || frame->size < sizeof(*frame))
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                          "Invalid BindingPlan call frame");

  if (frame->param_count < plan->param_count)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                          "Call frame exposes too few function parameters");

  if (plan->has_request_root_param) {
    size_t required = plan->request->data->storage_type->size;
    if (frame->request == NULL || frame->request_bytes < required)
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL,
          plan->function->params[plan->request_root_param].name,
          "Request root staging storage is missing or too small");
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
          "Function parameter '%s' staging storage is missing or too small",
          plan->function->params[i].name);
  }

  if (plan->response_uses_return) {
    size_t required = plan->response->data->storage_type->size;
    if (frame->return_value == NULL || frame->return_bytes < required)
      return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                            "Return-value staging storage is missing or too small");
  }

  if (plan->has_error_param &&
      (plan->error_param_index >= frame->param_count ||
       frame->params == NULL || frame->param_bytes == NULL ||
       frame->params[plan->error_param_index] == NULL ||
       frame->param_bytes[plan->error_param_index] <
           plan->error_envelope_bytes))
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL,
        plan->function->params[plan->error_param_index].name,
        "Typed-error envelope staging storage is missing or too small");

  return DATA_BIND_OK;
}

static DataBindStatus plan_native_init(
    const DataBindNativeOptions *options, const cmeta_data_desc *data,
    void *storage, size_t storage_bytes,
    DataBindBindingPlanDiagnostic *diagnostic,
    const char *schema_field, const char *function_param) {
  DataBindNativeDiagnostic native = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  DataBindStatus status;

  if (data == NULL || data->storage_type == NULL || storage == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                          schema_field, function_param,
                          "Native staging descriptor/storage is incomplete");

  status = data_bind_native_init(options, data, storage, storage_bytes, &native);
  if (status != DATA_BIND_OK)
    return plan_diag_fail(
        diagnostic, status, schema_field, function_param, "%s",
        native.error.message[0] != '\0'
            ? native.error.message
            : "Native staging initialization failed");
  return DATA_BIND_OK;
}

static void plan_native_clear_noexcept(
    const DataBindNativeOptions *options, const cmeta_data_desc *data,
    void *storage, size_t storage_bytes) {
  DataBindNativeDiagnostic native = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  if (data == NULL || storage == NULL) return;
  (void)data_bind_native_clear(options, data, storage, storage_bytes, &native);
}

static void *plan_ingress_destination(
    const DataBindBindingPlan *plan, const DataBindBindingPlanEntry *entry,
    DataBindBindingCallFrame *frame, size_t *out_bytes) {
  unsigned char *base = NULL;
  size_t bytes = 0u;

  if (plan->has_request_root_param &&
      entry->function_param_index == plan->request_root_param) {
    base = (unsigned char *)frame->request;
    bytes = frame->request_bytes;
  } else if (entry->function_param_index < frame->param_count &&
             frame->params != NULL && frame->param_bytes != NULL) {
    base = (unsigned char *)frame->params[entry->function_param_index];
    bytes = frame->param_bytes[entry->function_param_index];
  }

  if (base == NULL || bytes < entry->native_offset ||
      entry->data == NULL || entry->data->storage_type == NULL ||
      bytes - entry->native_offset < entry->data->storage_type->size)
    return NULL;

  if (out_bytes != NULL)
    *out_bytes = entry->data->storage_type->size;
  return base + entry->native_offset;
}

static const void *plan_egress_source(
    const DataBindBindingPlan *plan, const DataBindBindingPlanEntry *entry,
    const DataBindBindingCallFrame *frame, size_t *out_bytes) {
  const unsigned char *base = NULL;
  size_t bytes = 0u;

  if (entry->target_is_return) {
    base = (const unsigned char *)frame->return_value;
    bytes = frame->return_bytes;
  } else if (entry->function_param_index < frame->param_count &&
             frame->params != NULL && frame->param_bytes != NULL) {
    base = (const unsigned char *)frame->params[entry->function_param_index];
    bytes = frame->param_bytes[entry->function_param_index];
  }

  (void)plan;
  if (base == NULL || bytes < entry->native_offset ||
      entry->data == NULL || entry->data->storage_type == NULL ||
      bytes - entry->native_offset < entry->data->storage_type->size)
    return NULL;

  if (out_bytes != NULL)
    *out_bytes = entry->data->storage_type->size;
  return base + entry->native_offset;
}

static DataBindStatus plan_validate_ingress_value(
    const DataBindBindingPlan *plan,
    const DataBindBindingPlanEntryOwned *owned,
    const void *destination,
    DataBindBindingPlanDiagnostic *diagnostic) {
  const DataBindBindingPlanEntry *entry;
  size_t i;

  if (plan == NULL || owned == NULL || destination == NULL)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
        "Invalid native validation runtime arguments");
  if (owned->validation_rule_count == 0u) return DATA_BIND_OK;
  if (plan->request_validation == NULL)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_RUNTIME, owned->view.schema_field,
        owned->view.function_param,
        "Ingress validation binding has no ValidationPlan");

  entry = &owned->view;
  for (i = 0u; i < owned->validation_rule_count; ++i) {
    DataBindError error = DATA_BIND_ERROR_INIT;
    size_t rule_index = owned->validation_rule_start + i;
    DataBindStatus status =
        data_bind_validation_plan_internal_validate_native_rule(
            plan->request_validation, rule_index, entry->data,
            destination, &error);
    if (status != DATA_BIND_OK)
      return plan_runtime_fail_error(
          diagnostic, status, entry, &error,
          "Native input validation failed");
  }
  return DATA_BIND_OK;
}

static void plan_reset_request_state(
    const DataBindBindingPlan *plan, DataBindBindingCallFrame *frame) {
  size_t i;
  if (!plan->has_request_root_param || frame->request == NULL) return;
  for (i = 0u; i < plan->ingress_count; ++i) {
    const DataBindBindingPlanEntry *entry = &plan->ingress[i].view;
    if (entry->has_presence) {
      unsigned char *presence =
          (unsigned char *)frame->request + entry->presence_offset;
      *presence &=
          (unsigned char)~(1u << entry->presence_bit);
    }
    if (entry->has_null) {
      unsigned char *nulls =
          (unsigned char *)frame->request + entry->null_offset;
      *nulls &= (unsigned char)~(1u << entry->null_bit);
    }
  }
}

static void plan_cleanup_inputs(
    const DataBindBindingPlan *plan,
    const DataBindNativeOptions *options,
    DataBindBindingCallFrame *frame,
    size_t initialized_params,
    int request_initialized) {
  size_t i = initialized_params;

  while (i != 0u) {
    --i;
    if (plan->param_data[i] != NULL &&
        frame->params != NULL && frame->param_bytes != NULL)
      plan_native_clear_noexcept(
          options, plan->param_data[i], frame->params[i],
          frame->param_bytes[i]);
  }

  if (request_initialized) {
    plan_native_clear_noexcept(
        options, plan->request->data, frame->request, frame->request_bytes);
    plan_reset_request_state(plan, frame);
  }
}

DataBindStatus data_bind_binding_plan_bind_inputs(
    const DataBindBindingPlan *plan,
    const DataBindBindingProvider *provider,
    const DataBindNativeOptions *native_options,
    DataBindBindingCallFrame *frame,
    DataBindBindingPlanDiagnostic *diagnostic) {
  size_t i;
  size_t initialized_params = 0u;
  int request_initialized = 0;
  DataBindStatus status;

  if (!plan_diag_header_valid(diagnostic)) return DATA_BIND_ERR_INVALID_ARG;
  plan_diag_clear(diagnostic);

  if (plan == NULL || native_options == NULL || frame == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                          "Invalid BindingPlan ingress arguments");

  if (plan->ingress_count != 0u && !plan_provider_valid_for_input(provider))
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                          "Invalid BindingPlan input provider");

  status = plan_frame_preflight(plan, frame, diagnostic);
  if (status != DATA_BIND_OK) return status;

  if (plan->has_request_root_param) {
    status = plan_native_init(
        native_options, plan->request->data, frame->request,
        frame->request_bytes, diagnostic, NULL,
        plan->function->params[plan->request_root_param].name);
    if (status != DATA_BIND_OK) return status;
    request_initialized = 1;
    plan_reset_request_state(plan, frame);
  }

  for (i = 0u; i < plan->param_count; ++i) {
    if (plan->param_data[i] != NULL) {
      status = plan_native_init(
          native_options, plan->param_data[i], frame->params[i],
          frame->param_bytes[i], diagnostic, NULL,
          plan->function->params[i].name);
      if (status != DATA_BIND_OK) {
        plan_cleanup_inputs(
            plan, native_options, frame, i, request_initialized);
        return status;
      }
    }
    initialized_params = i + 1u;
  }

  for (i = 0u; i < plan->ingress_count; ++i) {
    DataBindBindingPlanEntryOwned *owned =
        (DataBindBindingPlanEntryOwned *)&plan->ingress[i];
    const DataBindBindingPlanEntry *entry = &owned->view;
    cserde_reader reader = {0};
    PlanDefaultReaderContext default_context = {0};
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindNativeDiagnostic native = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    void *destination;
    size_t destination_bytes = 0u;
    DataBindBindingValueState input_state = DATA_BIND_VALUE_STATE_ABSENT;

    destination =
        plan_ingress_destination(plan, entry, frame, &destination_bytes);
    if (destination == NULL) {
      status = plan_diag_fail(
          diagnostic, DATA_BIND_ERR_INVALID_ARG,
          entry->schema_field, entry->function_param,
          "Compiled ingress entry has no native staging destination");
      goto fail;
    }

    status = provider->open_input(
        provider->context, entry, &reader, &input_state, &error);
    if (status != DATA_BIND_OK) {
      status = plan_runtime_fail_error(
          diagnostic, status, entry, &error, "Input provider failed");
      goto fail;
    }
    if (input_state < DATA_BIND_VALUE_STATE_ABSENT ||
        input_state > DATA_BIND_VALUE_STATE_NULL) {
      status = plan_diag_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA,
          entry->schema_field, entry->function_param,
          "Input provider returned an invalid logical value state");
      goto fail;
    }

    if (input_state == DATA_BIND_VALUE_STATE_ABSENT) {
      if (owned->has_default_token) {
        default_context.token = &owned->default_token;
        if (cserde_reader_init(
                &reader, &PLAN_DEFAULT_READER_OPS,
                &default_context) != CSERDE_OK) {
          status = plan_diag_fail(
              diagnostic, DATA_BIND_ERR_RUNTIME,
              entry->schema_field, entry->function_param,
              "Could not initialize compiled default reader");
          goto fail;
        }
        input_state = DATA_BIND_VALUE_STATE_VALUE;
      } else if (!entry->required) {
        continue;
      } else {
        status = plan_diag_fail(
            diagnostic, DATA_BIND_ERR_TYPE_NOT_FOUND,
            entry->schema_field, entry->function_param,
            "Required logical input is absent");
        goto fail;
      }
    }

    if (input_state == DATA_BIND_VALUE_STATE_NULL) {
      if (!entry->nullable || !entry->has_null) {
        status = plan_diag_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
            entry->schema_field, entry->function_param,
            "Explicit NULL is not admitted by the DataBind contract");
        goto fail;
      }
      if (entry->has_presence) {
        unsigned char *presence =
            (unsigned char *)frame->request + entry->presence_offset;
        *presence |= (unsigned char)(1u << entry->presence_bit);
      }
      {
        unsigned char *nulls =
            (unsigned char *)frame->request + entry->null_offset;
        *nulls |= (unsigned char)(1u << entry->null_bit);
      }
      continue;
    }

    status = data_bind_native_decode(
        native_options, entry->data, &reader, destination,
        destination_bytes, &native);
    if (status != DATA_BIND_OK) {
      status = plan_diag_fail(
          diagnostic, status, entry->schema_field,
          entry->function_param, "%s",
          native.error.message[0] != '\0'
              ? native.error.message
              : "Native input decode failed");
      goto fail;
    }

    status = plan_validate_ingress_value(
        plan, owned, destination, diagnostic);
    if (status != DATA_BIND_OK) goto fail;

    if (entry->has_presence &&
        input_state == DATA_BIND_VALUE_STATE_VALUE) {
      unsigned char *presence =
          (unsigned char *)frame->request + entry->presence_offset;
      *presence |= (unsigned char)(1u << entry->presence_bit);
    }
    if (entry->has_null) {
      unsigned char *nulls =
          (unsigned char *)frame->request + entry->null_offset;
      *nulls &= (unsigned char)~(1u << entry->null_bit);
    }
  }

  plan_diag_clear(diagnostic);
  return DATA_BIND_OK;

fail:
  plan_cleanup_inputs(
      plan, native_options, frame, initialized_params, request_initialized);
  return status;
}


static DataBindStatus plan_egress_value(
    const DataBindBindingPlan *plan,
    const DataBindBindingPlanEntryOwned *owned,
    const DataBindBindingCallFrame *frame,
    DataBindBindingValueState *out_state,
    const void **out_source,
    size_t *out_source_bytes,
    DataBindBindingPlanDiagnostic *diagnostic) {
  const DataBindBindingPlanEntry *entry;
  const unsigned char *state_base = NULL;
  const void *source = NULL;
  size_t source_bytes = 0u;
  int present = 1;
  int is_null = 0;
  DataBindBindingValueState state = DATA_BIND_VALUE_STATE_VALUE;

  if (plan == NULL || owned == NULL || frame == NULL ||
      out_state == NULL || out_source == NULL || out_source_bytes == NULL)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
        "Invalid egress value classification arguments");

  entry = &owned->view;
  if (entry->has_presence || entry->has_null) {
    if (entry->target_is_return) {
      state_base = (const unsigned char *)frame->return_value;
    } else if (entry->function_param_index < frame->param_count &&
               frame->params != NULL) {
      state_base =
          (const unsigned char *)frame->params[entry->function_param_index];
    }
    if (state_base == NULL)
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_INVALID_ARG,
          entry->schema_field, entry->function_param,
          "Output DataBind state storage is unavailable");
  }

  if (entry->has_presence)
    present = (state_base[entry->presence_offset] &
               (unsigned char)(1u << entry->presence_bit)) != 0u;
  if (entry->has_null)
    is_null = (state_base[entry->null_offset] &
               (unsigned char)(1u << entry->null_bit)) != 0u;

  if (!present) {
    if (is_null)
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA,
          entry->schema_field, entry->function_param,
          "Native output state has NULL set while presence is ABSENT");
    state = DATA_BIND_VALUE_STATE_ABSENT;
  } else if (is_null) {
    if (!entry->nullable)
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA,
          entry->schema_field, entry->function_param,
          "Native output state is NULL for a non-null DataBind field");
    state = DATA_BIND_VALUE_STATE_NULL;
  } else {
    source = plan_egress_source(plan, entry, frame, &source_bytes);
    if (source == NULL)
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_INVALID_ARG,
          entry->schema_field, entry->function_param,
          "Compiled egress entry has no native source");
  }

  *out_state = state;
  *out_source = source;
  *out_source_bytes = source_bytes;
  return DATA_BIND_OK;
}

static DataBindStatus plan_validate_egress_value(
    const DataBindBindingPlan *plan,
    const DataBindBindingPlanEntryOwned *owned,
    const void *source,
    DataBindBindingPlanDiagnostic *diagnostic) {
  const DataBindBindingPlanEntry *entry;
  size_t i;

  if (plan == NULL || owned == NULL || source == NULL)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
        "Invalid response validation runtime arguments");
  if (owned->validation_rule_count == 0u) return DATA_BIND_OK;
  if (plan->response_validation == NULL)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_RUNTIME, owned->view.schema_field,
        owned->view.function_param,
        "Egress validation binding has no ValidationPlan");

  entry = &owned->view;
  for (i = 0u; i < owned->validation_rule_count; ++i) {
    DataBindError error = DATA_BIND_ERROR_INIT;
    size_t rule_index = owned->validation_rule_start + i;
    DataBindStatus status =
        data_bind_validation_plan_internal_validate_native_rule(
            plan->response_validation, rule_index, entry->data,
            source, &error);
    if (status != DATA_BIND_OK)
      return plan_runtime_fail_error(
          diagnostic, status, entry, &error,
          "Native response validation failed");
  }
  return DATA_BIND_OK;
}

static DataBindStatus plan_write_response_outputs(
    const DataBindBindingPlan *plan,
    const DataBindBindingProvider *provider,
    const DataBindBindingCallFrame *frame,
    DataBindBindingPlanDiagnostic *diagnostic) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status;
  size_t i;

  if (!plan_diag_header_valid(diagnostic)) return DATA_BIND_ERR_INVALID_ARG;
  plan_diag_clear(diagnostic);

  if (plan == NULL || frame == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                          "Invalid BindingPlan output arguments");

  if (plan->egress_count != 0u && !plan_provider_valid_for_output(provider))
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                          "Invalid BindingPlan output provider");

  status = plan_frame_preflight(plan, frame, diagnostic);
  if (status != DATA_BIND_OK) return status;
  if (plan->egress_count == 0u) return DATA_BIND_OK;

  /*
   * Validate the complete native response before the provider observes any
   * output transaction. ABSENT/NULL skip value constraints; only VALUE executes
   * the compiled canonical ValidationPlan.
   */
  for (i = 0u; i < plan->egress_count; ++i) {
    const DataBindBindingPlanEntryOwned *owned = &plan->egress[i];
    DataBindBindingValueState output_state = DATA_BIND_VALUE_STATE_VALUE;
    const void *source = NULL;
    size_t source_bytes = 0u;

    status = plan_egress_value(
        plan, owned, frame, &output_state, &source, &source_bytes, diagnostic);
    if (status != DATA_BIND_OK) return status;
    (void)source_bytes;

    if (output_state == DATA_BIND_VALUE_STATE_VALUE) {
      status = plan_validate_egress_value(
          plan, owned, source, diagnostic);
      if (status != DATA_BIND_OK) return status;
    }
  }

  status = provider->begin_output(provider->context, &error);
  if (status != DATA_BIND_OK)
    return plan_runtime_fail_error(
        diagnostic, status, NULL, &error,
        "Output transaction could not begin");

  for (i = 0u; i < plan->egress_count; ++i) {
    const DataBindBindingPlanEntryOwned *owned = &plan->egress[i];
    const DataBindBindingPlanEntry *entry = &owned->view;
    const void *source = NULL;
    size_t source_bytes = 0u;
    DataBindBindingValueState output_state = DATA_BIND_VALUE_STATE_VALUE;

    status = plan_egress_value(
        plan, owned, frame, &output_state, &source, &source_bytes, diagnostic);
    if (status != DATA_BIND_OK) {
      provider->abort_output(provider->context);
      return status;
    }

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    status = provider->write_output(
        provider->context, entry, output_state, source, source_bytes, &error);
    if (status != DATA_BIND_OK) {
      provider->abort_output(provider->context);
      return plan_runtime_fail_error(
          diagnostic, status, entry, &error, "Output provider write failed");
    }
  }

  error = (DataBindError)DATA_BIND_ERROR_INIT;
  status = provider->commit_output(provider->context, &error);
  if (status != DATA_BIND_OK) {
    provider->abort_output(provider->context);
    return plan_runtime_fail_error(
        diagnostic, status, NULL, &error,
        "Output transaction commit failed");
  }

  plan_diag_clear(diagnostic);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_binding_plan_write_outputs(
    const DataBindBindingPlan *plan,
    const DataBindBindingProvider *provider,
    const DataBindBindingCallFrame *frame,
    DataBindBindingPlanDiagnostic *diagnostic) {
  if (!plan_diag_header_valid(diagnostic)) return DATA_BIND_ERR_INVALID_ARG;
  plan_diag_clear(diagnostic);
  if (plan != NULL && plan->has_error_param)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL,
        plan->function != NULL && plan->error_param_index < plan->function->param_count
            ? plan->function->params[plan->error_param_index].name
            : NULL,
        "Throws Service outcomes must use data_bind_binding_plan_write_outcome");
  return plan_write_response_outputs(plan, provider, frame, diagnostic);
}


static int plan_outcome_header_valid(const DataBindBindingOutcome *outcome) {
  return outcome == NULL ||
         outcome->size >=
             offsetof(DataBindBindingOutcome, kind) + sizeof(outcome->kind);
}

static void plan_outcome_set(
    DataBindBindingOutcome *outcome, DataBindBindingOutcomeKind kind,
    int native_status, size_t typed_error_index, const char *typed_error) {
  DataBindBindingOutcome value = DATA_BIND_BINDING_OUTCOME_INIT;
  size_t size;
  if (outcome == NULL) return;
  size = plan_out_size(outcome->size, sizeof(*outcome));
  value.size = size;
  value.kind = kind;
  value.native_status = native_status;
  value.typed_error_index = typed_error_index;
  value.typed_error = typed_error;
  memcpy(outcome, &value, size);
}

static DataBindStatus plan_read_typed_error_kind(
    const DataBindBindingPlan *plan, const DataBindBindingCallFrame *frame,
    uint32_t *out_kind, DataBindBindingPlanDiagnostic *diagnostic) {
  const unsigned char *base;
  uint32_t kind = 0u;

  if (out_kind == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                          "Typed-error kind output is required");
  *out_kind = 0u;
  if (!plan->has_error_param) return DATA_BIND_OK;

  if (plan->error_kind_bytes != sizeof(kind) ||
      plan->error_param_index >= frame->param_count ||
      frame->params == NULL || frame->param_bytes == NULL ||
      frame->params[plan->error_param_index] == NULL ||
      frame->param_bytes[plan->error_param_index] <
          plan->error_envelope_bytes)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL,
        plan->function->params[plan->error_param_index].name,
        "Typed-error envelope staging storage is unavailable");

  base = (const unsigned char *)frame->params[plan->error_param_index];
  memcpy(&kind, base + plan->error_kind_offset, sizeof(kind));
  if (kind > plan->error_count)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, NULL,
        plan->function->params[plan->error_param_index].name,
        "Typed-error kind %u is outside the compiled throws contract",
        (unsigned)kind);

  *out_kind = kind;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_binding_plan_write_outcome(
    const DataBindBindingPlan *plan,
    const DataBindBindingProvider *provider,
    const DataBindBindingCallFrame *frame,
    int native_status,
    DataBindBindingOutcome *outcome,
    DataBindBindingPlanDiagnostic *diagnostic) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status;
  uint32_t error_kind = 0u;
  const DataBindBindingPlanEntry *entry;
  const void *source;
  size_t source_bytes = 0u;
  size_t error_index;

  if (!plan_diag_header_valid(diagnostic) ||
      !plan_outcome_header_valid(outcome))
    return DATA_BIND_ERR_INVALID_ARG;
  plan_diag_clear(diagnostic);
  plan_outcome_set(
      outcome, DATA_BIND_BINDING_OUTCOME_NONE, 0, SIZE_MAX, NULL);

  if (plan == NULL || frame == NULL)
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                          "Invalid BindingPlan outcome arguments");

  status = plan_frame_preflight(plan, frame, diagnostic);
  if (status != DATA_BIND_OK) return status;

  status = plan_read_typed_error_kind(
      plan, frame, &error_kind, diagnostic);
  if (status != DATA_BIND_OK) return status;

  if (native_status != 0) {
    if (error_kind != 0u)
      return plan_diag_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, NULL,
          plan->has_error_param
              ? plan->function->params[plan->error_param_index].name
              : NULL,
          "Native/business status and typed Service error are both set");
    plan_outcome_set(
        outcome, DATA_BIND_BINDING_OUTCOME_NATIVE_STATUS,
        native_status, SIZE_MAX, NULL);
    plan_diag_clear(diagnostic);
    return DATA_BIND_OK;
  }

  if (error_kind == 0u) {
    status = plan_write_response_outputs(
        plan, provider, frame, diagnostic);
    if (status == DATA_BIND_OK)
      plan_outcome_set(
          outcome, DATA_BIND_BINDING_OUTCOME_SUCCESS, 0, SIZE_MAX, NULL);
    return status;
  }

  if (!plan_provider_valid_for_output(provider))
    return plan_diag_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                          "Invalid typed-error output provider");

  error_index = (size_t)error_kind - 1u;
  entry = &plan->errors[error_index].view;
  source = plan_egress_source(plan, entry, frame, &source_bytes);
  if (source == NULL)
    return plan_diag_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG,
        entry->schema_field, entry->function_param,
        "Compiled typed-error entry has no native source");

  status = provider->begin_output(provider->context, &error);
  if (status != DATA_BIND_OK)
    return plan_runtime_fail_error(
        diagnostic, status, entry, &error,
        "Typed-error output transaction could not begin");

  error = (DataBindError)DATA_BIND_ERROR_INIT;
  status = provider->write_output(
      provider->context, entry, DATA_BIND_VALUE_STATE_VALUE,
      source, source_bytes, &error);
  if (status != DATA_BIND_OK) {
    provider->abort_output(provider->context);
    return plan_runtime_fail_error(
        diagnostic, status, entry, &error,
        "Typed-error output provider write failed");
  }

  error = (DataBindError)DATA_BIND_ERROR_INIT;
  status = provider->commit_output(provider->context, &error);
  if (status != DATA_BIND_OK) {
    provider->abort_output(provider->context);
    return plan_runtime_fail_error(
        diagnostic, status, entry, &error,
        "Typed-error output transaction commit failed");
  }

  plan_outcome_set(
      outcome, DATA_BIND_BINDING_OUTCOME_TYPED_ERROR, 0,
      error_index, entry->schema_field);
  plan_diag_clear(diagnostic);
  return DATA_BIND_OK;
}
