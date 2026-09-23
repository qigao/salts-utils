#include "data_bind_binding_plan.h"

#include <cmeta/type_traits.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct DataBindBindingPlanEntryOwned {
  DataBindBindingPlanEntry pub;
  cserde_token default_token;
  int has_default_token;
} DataBindBindingPlanEntryOwned;

struct DataBindBindingPlan {
  char *operation_id;
  char *projection_id;
  const cmeta_function_desc *function;
  const cmeta_data_desc *request_data;
  const cmeta_data_desc *response_data;

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

typedef struct DefaultReaderContext {
  const cserde_token *token;
  int emitted;
} DefaultReaderContext;

static size_t binding_out_size(size_t requested, size_t full_size) {
  return requested != 0u && requested < full_size ? requested : full_size;
}

static int binding_diag_header_valid(
    const DataBindBindingPlanDiagnostic *diagnostic) {
  return diagnostic == NULL ||
         diagnostic->size >=
             offsetof(DataBindBindingPlanDiagnostic, status) +
                 sizeof(diagnostic->status);
}

static void binding_diag_clear(DataBindBindingPlanDiagnostic *diagnostic) {
  size_t size;
  if (diagnostic == NULL) return;
  size = binding_out_size(diagnostic->size, sizeof(*diagnostic));
  memset(diagnostic, 0, size);
  if (size >= sizeof(size_t)) diagnostic->size = size;
  if (size >= offsetof(DataBindBindingPlanDiagnostic, status) +
                  sizeof(diagnostic->status))
    diagnostic->status = DATA_BIND_OK;
}

static DataBindStatus binding_fail(
    DataBindBindingPlanDiagnostic *diagnostic, DataBindStatus status,
    const char *schema_field, const char *function_param,
    const char *fmt, ...) {
  va_list ap;
  size_t size;

  if (diagnostic == NULL) return status;
  size = binding_out_size(diagnostic->size, sizeof(*diagnostic));
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

static DataBindStatus binding_fail_error(
    DataBindBindingPlanDiagnostic *diagnostic, DataBindStatus status,
    const char *schema_field, const char *function_param,
    const DataBindError *error, const char *fallback) {
  const char *message = fallback;
  if (error != NULL &&
      error->size >= offsetof(DataBindError, message) + sizeof(error->message) &&
      error->message[0] != '\0')
    message = error->message;
  return binding_fail(diagnostic, status, schema_field, function_param, "%s",
                      message != NULL ? message : "binding failure");
}

static char *binding_strdup(const char *text) {
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

static char *binding_operation_id(const char *service_name,
                                  const char *operation_name) {
  size_t service_len;
  size_t operation_len;
  char *result;
  if (service_name == NULL || operation_name == NULL) return NULL;
  service_len = strlen(service_name);
  operation_len = strlen(operation_name);
  if (service_len > SIZE_MAX - operation_len - 2u) return NULL;
  result = (char *)malloc(service_len + operation_len + 2u);
  if (result == NULL) return NULL;
  memcpy(result, service_name, service_len);
  result[service_len] = '.';
  memcpy(result + service_len + 1u, operation_name, operation_len + 1u);
  return result;
}

static const cmeta_data_struct_shape *binding_struct_shape(
    const cmeta_data_desc *data) {
  if (!cmeta_data_desc_valid(data) || data->kind != CMETA_DATA_STRUCT ||
      data->shape == NULL)
    return NULL;
  return (const cmeta_data_struct_shape *)data->shape;
}

static const cmeta_data_field_desc *binding_native_field(
    const cmeta_data_desc *data, const char *name) {
  const cmeta_data_struct_shape *shape = binding_struct_shape(data);
  size_t i;
  if (shape == NULL || name == NULL) return NULL;
  for (i = 0u; i < shape->field_count; ++i) {
    if (shape->fields[i].name != NULL &&
        strcmp(shape->fields[i].name, name) == 0)
      return &shape->fields[i];
  }
  return NULL;
}

static const cmeta_type_desc *binding_param_value_type(
    const cmeta_param_desc *param, int *indirect) {
  if (indirect != NULL) *indirect = 0;
  if (param == NULL || param->type == NULL) return NULL;
  if (param->type->kind == CMETA_T_POINTER) {
    if (indirect != NULL) *indirect = 1;
    return param->type->pointee;
  }
  return param->type;
}

static size_t binding_param_index(
    const cmeta_function_desc *function, const char *name) {
  size_t i;
  if (function == NULL || name == NULL) return SIZE_MAX;
  for (i = 0u; i < function->param_count; ++i)
    if (function->params[i].name != NULL &&
        strcmp(function->params[i].name, name) == 0)
      return i;
  return SIZE_MAX;
}

static int binding_type_matches_data(const cmeta_type_desc *type,
                                     const cmeta_data_desc *data) {
  return type != NULL && data != NULL && data->storage_type != NULL &&
         cmeta_type_equal(type, data->storage_type);
}

static int binding_data_semantically_equal(const cmeta_data_desc *left,
                                           const cmeta_data_desc *right) {
  if (left == right) return left != NULL && cmeta_data_desc_valid(left);
  if (!cmeta_data_desc_valid(left) || !cmeta_data_desc_valid(right) ||
      left->kind != right->kind || left->storage_type == NULL ||
      right->storage_type == NULL ||
      !cmeta_type_equal(left->storage_type, right->storage_type))
    return 0;
  if (left->stable_id == NULL || right->stable_id == NULL)
    return 0;
  return strcmp(left->stable_id, right->stable_id) == 0;
}

static const DataBindNativePresenceBinding *binding_presence_find(
    const DataBindNativeRecordBinding *record, const char *field_name) {
  size_t i;
  if (record == NULL || field_name == NULL) return NULL;
  for (i = 0u; i < record->presence_count; ++i) {
    const DataBindNativePresenceBinding *presence = &record->presence[i];
    if (presence->field_name != NULL &&
        strcmp(presence->field_name, field_name) == 0)
      return presence;
  }
  return NULL;
}

static DataBindStatus binding_validate_record(
    DataBind *codec, const DataBindNativeRecordBinding *record,
    const char *expected_name, DataBindBindingPlanDiagnostic *diagnostic) {
  DataBindSchemaType schema_type = DATA_BIND_SCHEMA_TYPE_INIT;
  const cmeta_data_struct_shape *shape;
  const cmeta_struct_desc *layout;
  size_t i;
  size_t j;

  if (record == NULL || record->size < sizeof(*record) ||
      record->abi_version != DATA_BIND_BINDING_PLAN_ABI_VERSION ||
      record->schema_type == NULL || record->data == NULL)
    return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA, expected_name, NULL,
                        "Invalid native record binding");

  if (expected_name == NULL ||
      strcmp(record->schema_type, expected_name) != 0)
    return binding_fail(
        diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, expected_name, NULL,
        "Native record '%s' does not match service schema type '%s'",
        record->schema_type != NULL ? record->schema_type : "<unnamed>",
        expected_name != NULL ? expected_name : "<unknown>");

  shape = binding_struct_shape(record->data);
  if (shape == NULL || shape->layout == NULL ||
      record->data->storage_type == NULL)
    return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA, expected_name, NULL,
                        "Native record must expose a valid canonical CMeta Struct");

  layout = shape->layout;
  if (layout->size != record->data->storage_type->size ||
      layout->align != record->data->storage_type->align)
    return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA, expected_name, NULL,
                        "CMeta Struct layout disagrees with native storage type");

  if (!data_bind_schema_find_type(codec, expected_name, &schema_type))
    return binding_fail(diagnostic, DATA_BIND_ERR_TYPE_NOT_FOUND,
                        expected_name, NULL,
                        "Service schema type '%s' was not found", expected_name);
  if ((schema_type.kind != DATA_BIND_SCHEMA_MESSAGE &&
       schema_type.kind != DATA_BIND_SCHEMA_COMPOSITE &&
       schema_type.kind != DATA_BIND_SCHEMA_GROUP) ||
      schema_type.field_count != shape->field_count)
    return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA, expected_name, NULL,
                        "Native record field count disagrees with Service Schema");

  if (record->presence_count != 0u && record->presence == NULL)
    return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA, expected_name, NULL,
                        "Presence count is nonzero without presence metadata");

  for (i = 0u; i < shape->field_count; ++i) {
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    const cmeta_data_field_desc *native;
    const cmeta_field_desc *layout_field;
    const cmeta_data_desc *schema_data = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindStatus status;
    const DataBindNativePresenceBinding *presence;

    if (!data_bind_schema_field_at(codec, expected_name, i, &field) ||
        field.name == NULL)
      return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA, expected_name, NULL,
                          "Schema field reflection failed");

    native = binding_native_field(record->data, field.name);
    if (native == NULL || native->value == NULL ||
        native->value->storage_type == NULL ||
        !cmeta_data_desc_valid(native->value))
      return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA, field.name, NULL,
                          "Native field metadata is incomplete");

    layout_field = cmeta_struct_find_field(layout, field.name);
    if (layout_field == NULL || layout_field->type == NULL ||
        layout_field->offset != native->offset ||
        !cmeta_type_equal(layout_field->type, native->value->storage_type))
      return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA, field.name, NULL,
                          "Native field layout disagrees with CMeta Struct layout");

    status = data_bind_schema_field_cmeta_data(
        codec, expected_name, i, &schema_data, &error);
    if (status == DATA_BIND_OK) {
      if (!binding_data_semantically_equal(schema_data, native->value))
        return binding_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field.name, NULL,
            "Schema field canonical data disagrees with native CMeta field");
    } else if (!field.has_cmeta_kind ||
               field.cmeta_kind != native->value->kind) {
      return binding_fail_error(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field.name, NULL, &error,
          "Schema field kind disagrees with native CMeta field");
    }

    presence = binding_presence_find(record, field.name);
    if (field.is_optional) {
      if (presence == NULL || presence->size < sizeof(*presence) ||
          presence->bit_index >= 8u ||
          presence->byte_offset >= record->data->storage_type->size)
        return binding_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, field.name, NULL,
            "Optional field lacks bounded DataBind presence metadata");
    } else if (presence != NULL) {
      return binding_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, field.name, NULL,
          "Required field must not declare optional presence metadata");
    }
  }

  for (i = 0u; i < record->presence_count; ++i) {
    const DataBindNativePresenceBinding *left = &record->presence[i];
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    int found = 0;
    if (left->size < sizeof(*left) || left->field_name == NULL ||
        left->bit_index >= 8u ||
        left->byte_offset >= record->data->storage_type->size)
      return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA, expected_name, NULL,
                          "Invalid native presence metadata");
    for (j = 0u; j < schema_type.field_count; ++j) {
      field = (DataBindSchemaField)DATA_BIND_SCHEMA_FIELD_INIT;
      if (data_bind_schema_field_at(codec, expected_name, j, &field) &&
          field.name != NULL &&
          strcmp(field.name, left->field_name) == 0) {
        found = field.is_optional != 0;
        break;
      }
    }
    if (!found)
      return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                          left->field_name, NULL,
                          "Presence metadata references a non-optional field");
    for (j = 0u; j < i; ++j) {
      const DataBindNativePresenceBinding *right = &record->presence[j];
      if (strcmp(left->field_name, right->field_name) == 0 ||
          (left->byte_offset == right->byte_offset &&
           left->bit_index == right->bit_index))
        return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                            left->field_name, NULL,
                            "Duplicate native presence metadata");
    }
  }

  return DATA_BIND_OK;
}

static DataBindStatus binding_project(
    const DataBindBindingProjection *projection,
    const DataBindServiceOperation *operation,
    const DataBindSchemaField *field,
    DataBindBindingDirection direction,
    DataBindBindingProjectionSlot *slot,
    DataBindBindingPlanDiagnostic *diagnostic) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status;

  if (projection == NULL || projection->size < sizeof(*projection) ||
      projection->abi_version != DATA_BIND_BINDING_PLAN_ABI_VERSION ||
      projection->id == NULL || projection->id[0] == '\0' ||
      projection->project_field == NULL)
    return binding_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG,
                        field != NULL ? field->name : NULL, NULL,
                        "Invalid binding projection adapter");

  *slot = (DataBindBindingProjectionSlot)
      DATA_BIND_BINDING_PROJECTION_SLOT_INIT;
  status = projection->project_field(
      projection->context, operation, field, direction, slot, &error);
  if (status != DATA_BIND_OK)
    return binding_fail_error(
        diagnostic, status, field != NULL ? field->name : NULL, NULL,
        &error, "Projection adapter rejected service field");

  if (slot->size < sizeof(*slot) ||
      slot->binding_class < DATA_BIND_BINDING_VALUE ||
      slot->binding_class > DATA_BIND_BINDING_ERROR)
    return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                        field != NULL ? field->name : NULL, NULL,
                        "Projection adapter returned an invalid logical slot");
  return DATA_BIND_OK;
}

static DataBindStatus binding_default_token(
    DataBindBindingPlanEntryOwned *entry,
    DataBindBindingPlanDiagnostic *diagnostic) {
  const char *text;
  char *end = NULL;

  if (entry == NULL || !entry->pub.has_default) return DATA_BIND_OK;
  text = entry->pub.default_value;
  if (text == NULL || entry->pub.data == NULL)
    return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                        entry != NULL ? entry->pub.schema_field : NULL,
                        entry != NULL ? entry->pub.function_param : NULL,
                        "Default metadata is incomplete");

  errno = 0;
  switch (entry->pub.data->kind) {
  case CMETA_DATA_BOOL:
    entry->default_token.kind = CSERDE_BOOL;
    if (strcmp(text, "true") == 0 || strcmp(text, "1") == 0)
      entry->default_token.value.boolean = true;
    else if (strcmp(text, "false") == 0 || strcmp(text, "0") == 0)
      entry->default_token.value.boolean = false;
    else
      return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                          entry->pub.schema_field,
                          entry->pub.function_param,
                          "Boolean default '%s' is invalid", text);
    break;

  case CMETA_DATA_SINT: {
    long long value = strtoll(text, &end, 10);
    if (errno != 0 || end == text || end == NULL || *end != '\0')
      return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                          entry->pub.schema_field,
                          entry->pub.function_param,
                          "Signed default '%s' is invalid", text);
    entry->default_token.kind = CSERDE_SINT;
    entry->default_token.value.sint = (int64_t)value;
    break;
  }

  case CMETA_DATA_UINT: {
    unsigned long long value;
    if (text[0] == '-')
      return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                          entry->pub.schema_field,
                          entry->pub.function_param,
                          "Unsigned default '%s' is invalid", text);
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || end == NULL || *end != '\0')
      return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                          entry->pub.schema_field,
                          entry->pub.function_param,
                          "Unsigned default '%s' is invalid", text);
    entry->default_token.kind = CSERDE_UINT;
    entry->default_token.value.uint = (uint64_t)value;
    break;
  }

  case CMETA_DATA_FLOAT: {
    double value = strtod(text, &end);
    if (errno != 0 || end == text || end == NULL || *end != '\0')
      return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                          entry->pub.schema_field,
                          entry->pub.function_param,
                          "Floating default '%s' is invalid", text);
    entry->default_token.kind = CSERDE_FLOAT;
    entry->default_token.value.floating = value;
    break;
  }

  case CMETA_DATA_STRING:
  case CMETA_DATA_ENUM:
    entry->default_token.kind = CSERDE_STRING;
    entry->default_token.value.slice.data =
        (const unsigned char *)entry->pub.default_value;
    entry->default_token.value.slice.size =
        strlen(entry->pub.default_value);
    entry->default_token.value.slice.lifetime = CSERDE_VIEW_STABLE;
    break;

  case CMETA_DATA_BYTES:
    entry->default_token.kind = CSERDE_BYTES;
    entry->default_token.value.slice.data =
        (const unsigned char *)entry->pub.default_value;
    entry->default_token.value.slice.size =
        strlen(entry->pub.default_value);
    entry->default_token.value.slice.lifetime = CSERDE_VIEW_STABLE;
    break;

  default:
    return binding_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, entry->pub.schema_field,
        entry->pub.function_param,
        "Default for field '%s' uses an unsupported native semantic kind",
        entry->pub.schema_field != NULL ? entry->pub.schema_field
                                        : "<unnamed>");
  }

  entry->has_default_token = 1;
  return DATA_BIND_OK;
}

static void binding_entry_clear(DataBindBindingPlanEntryOwned *entry) {
  if (entry == NULL) return;
  free((void *)entry->pub.schema_field);
  free((void *)entry->pub.logical_name);
  free((void *)entry->pub.selector);
  free((void *)entry->pub.function_param);
  free((void *)entry->pub.default_value);
  free((void *)entry->pub.format);
  memset(entry, 0, sizeof(*entry));
}

static DataBindStatus binding_entry_init(
    DataBindBindingPlanEntryOwned *entry,
    DataBindBindingDirection direction,
    const DataBindBindingProjectionSlot *slot,
    DataBindBindingNativeTarget target,
    const DataBindSchemaField *field,
    const cmeta_data_field_desc *native_field,
    const cmeta_param_desc *param,
    size_t param_index, int parameter_indirect,
    size_t native_offset,
    const DataBindNativePresenceBinding *presence,
    DataBindBindingPlanDiagnostic *diagnostic) {
  DataBindStatus status;

  if (entry == NULL || slot == NULL || field == NULL ||
      native_field == NULL || native_field->value == NULL)
    return binding_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG,
                        field != NULL ? field->name : NULL,
                        param != NULL ? param->name : NULL,
                        "Invalid binding entry construction");

  memset(entry, 0, sizeof(*entry));
  entry->pub.size = sizeof(entry->pub);
  entry->pub.direction = direction;
  entry->pub.binding_class = slot->binding_class;
  entry->pub.target = target;
  entry->pub.function_param_index = param_index;
  entry->pub.native_offset = native_offset;
  entry->pub.parameter_indirect = parameter_indirect;
  entry->pub.required = !field->is_optional && !field->has_default;
  entry->pub.has_default = field->has_default;
  entry->pub.data = native_field->value;

  entry->pub.schema_field = binding_strdup(field->name);
  entry->pub.logical_name = binding_strdup(field->name);
  entry->pub.selector = slot->selector != NULL
                            ? binding_strdup(slot->selector)
                            : NULL;
  entry->pub.function_param =
      param != NULL ? binding_strdup(param->name) : NULL;
  entry->pub.default_value =
      field->has_default ? binding_strdup(field->default_value) : NULL;
  entry->pub.format =
      field->format != NULL ? binding_strdup(field->format) : NULL;

  if (entry->pub.schema_field == NULL ||
      entry->pub.logical_name == NULL ||
      (slot->selector != NULL && entry->pub.selector == NULL) ||
      (param != NULL && entry->pub.function_param == NULL) ||
      (field->has_default && entry->pub.default_value == NULL) ||
      (field->format != NULL && entry->pub.format == NULL)) {
    binding_entry_clear(entry);
    return binding_fail(diagnostic, DATA_BIND_ERR_OOM, field->name,
                        param != NULL ? param->name : NULL,
                        "Out of memory copying binding plan metadata");
  }

  if (presence != NULL) {
    entry->pub.has_presence = 1;
    entry->pub.presence_offset = presence->byte_offset;
    entry->pub.presence_bit = presence->bit_index;
  }

  status = binding_default_token(entry, diagnostic);
  if (status != DATA_BIND_OK) {
    binding_entry_clear(entry);
    return status;
  }
  return DATA_BIND_OK;
}

static void binding_entry_array_clear(
    DataBindBindingPlanEntryOwned *entries, size_t count) {
  size_t i;
  if (entries == NULL) return;
  for (i = 0u; i < count; ++i) binding_entry_clear(&entries[i]);
  free(entries);
}

void data_bind_binding_plan_free(DataBindBindingPlan *plan) {
  size_t i;
  if (plan == NULL) return;
  free(plan->operation_id);
  free(plan->projection_id);
  binding_entry_array_clear(plan->ingress, plan->ingress_count);
  binding_entry_array_clear(plan->egress, plan->egress_count);
  for (i = 0u; i < plan->error_count; ++i) free(plan->errors[i]);
  free(plan->errors);
  free(plan->param_data);
  free(plan->param_ingress);
  free(plan->param_egress);
  free(plan);
}

static DataBindStatus binding_copy_errors(
    DataBind *codec, const char *service_name, const char *operation_name,
    size_t count, DataBindBindingPlan *plan,
    DataBindBindingPlanDiagnostic *diagnostic) {
  size_t i;
  if (count == 0u) return DATA_BIND_OK;
  plan->errors = (char **)calloc(count, sizeof(*plan->errors));
  if (plan->errors == NULL)
    return binding_fail(diagnostic, DATA_BIND_ERR_OOM, NULL, NULL,
                        "Out of memory copying service errors");
  for (i = 0u; i < count; ++i) {
    const char *name = data_bind_service_operation_error_at(
        codec, service_name, operation_name, i);
    if (name == NULL)
      return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA, NULL, NULL,
                          "Service error reflection is incomplete");
    plan->errors[i] = binding_strdup(name);
    if (plan->errors[i] == NULL)
      return binding_fail(diagnostic, DATA_BIND_ERR_OOM, NULL, NULL,
                          "Out of memory copying service error");
    plan->error_count = i + 1u;
  }
  return DATA_BIND_OK;
}

static DataBindStatus binding_validate_schema_native_field(
    DataBind *codec, const char *record_name, size_t field_index,
    const DataBindSchemaField *field,
    const cmeta_data_field_desc *native_field,
    DataBindBindingPlanDiagnostic *diagnostic) {
  const cmeta_data_desc *schema_data = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status;

  if (field == NULL || native_field == NULL ||
      native_field->value == NULL ||
      native_field->value->storage_type == NULL)
    return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                        field != NULL ? field->name : record_name, NULL,
                        "Native field metadata is incomplete");

  status = data_bind_schema_field_cmeta_data(
      codec, record_name, field_index, &schema_data, &error);
  if (status == DATA_BIND_OK) {
    if (!binding_data_semantically_equal(schema_data, native_field->value))
      return binding_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field->name, NULL,
          "Schema field canonical data disagrees with native CMeta field");
  } else if (!field->has_cmeta_kind ||
             field->cmeta_kind != native_field->value->kind) {
    return binding_fail_error(
        diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field->name, NULL,
        &error, "Schema field kind disagrees with native CMeta field");
  }
  return DATA_BIND_OK;
}

static DataBindStatus binding_compile_ingress(
    DataBind *codec, const DataBindServiceOperation *operation,
    const DataBindBindingProjection *projection,
    const DataBindNativeRecordBinding *request,
    DataBindBindingPlan *plan,
    DataBindBindingPlanDiagnostic *diagnostic) {
  const cmeta_function_desc *function = plan->function;
  const cmeta_data_struct_shape *shape =
      binding_struct_shape(request->data);
  size_t field_count =
      data_bind_schema_field_count(codec, operation->request_type);
  size_t i;
  size_t in_count = 0u;
  size_t root_param = SIZE_MAX;
  int root_indirect = 0;

  for (i = 0u; i < function->param_count; ++i) {
    const cmeta_param_desc *param = &function->params[i];
    int indirect = 0;
    const cmeta_type_desc *value_type;
    if ((param->flags & CMETA_PARAM_IN) == 0u) continue;
    ++in_count;
    value_type = binding_param_value_type(param, &indirect);
    if (binding_type_matches_data(value_type, request->data)) {
      if (root_param != SIZE_MAX)
        return binding_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, operation->request_type,
            param->name, "Multiple parameters match the complete request");
      root_param = i;
      root_indirect = indirect;
    }
  }

  if (root_param != SIZE_MAX && in_count != 1u)
    root_param = SIZE_MAX;

  plan->has_request_root_param = root_param != SIZE_MAX;
  plan->request_root_param = root_param;
  plan->ingress = (DataBindBindingPlanEntryOwned *)calloc(
      field_count, sizeof(*plan->ingress));
  if (field_count != 0u && plan->ingress == NULL)
    return binding_fail(diagnostic, DATA_BIND_ERR_OOM,
                        operation->request_type, NULL,
                        "Out of memory allocating ingress plan");

  for (i = 0u; i < field_count; ++i) {
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    DataBindBindingProjectionSlot slot =
        DATA_BIND_BINDING_PROJECTION_SLOT_INIT;
    const cmeta_data_field_desc *native_field;
    const DataBindNativePresenceBinding *presence;
    const cmeta_param_desc *param = NULL;
    size_t param_index = SIZE_MAX;
    int indirect = 0;
    DataBindBindingNativeTarget target;
    size_t native_offset;
    DataBindStatus status;

    if (!data_bind_schema_field_at(codec, operation->request_type, i, &field))
      return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                          operation->request_type, NULL,
                          "Request field reflection failed");

    native_field = binding_native_field(request->data, field.name);
    status = binding_validate_schema_native_field(
        codec, operation->request_type, i, &field, native_field, diagnostic);
    if (status != DATA_BIND_OK) return status;

    status = binding_project(
        projection, operation, &field, DATA_BIND_BINDING_INGRESS,
        &slot, diagnostic);
    if (status != DATA_BIND_OK) return status;

    presence = binding_presence_find(request, field.name);

    if (root_param != SIZE_MAX) {
      param_index = root_param;
      param = &function->params[param_index];
      indirect = root_indirect;
      target = DATA_BIND_BINDING_TARGET_REQUEST_FIELD;
      native_offset = native_field->offset;
      plan->param_ingress[param_index] = 1u;
    } else {
      param_index = binding_param_index(function, field.name);
      if (param_index == SIZE_MAX)
        return binding_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field.name, NULL,
            "Request field '%s' has no reflected function parameter",
            field.name);
      param = &function->params[param_index];
      if ((param->flags & CMETA_PARAM_IN) == 0u)
        return binding_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
            field.name, param->name,
            "Function parameter '%s' is not an IN parameter", param->name);
      if (!binding_type_matches_data(
              binding_param_value_type(param, &indirect),
              native_field->value))
        return binding_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
            field.name, param->name,
            "Request field '%s' type disagrees with parameter '%s'",
            field.name, param->name);
      target = DATA_BIND_BINDING_TARGET_FUNCTION_PARAM;
      native_offset = 0u;
      plan->param_ingress[param_index] = 1u;
      if (plan->param_data[param_index] != NULL &&
          !binding_data_semantically_equal(
              plan->param_data[param_index], native_field->value))
        return binding_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
            field.name, param->name,
            "Function parameter '%s' receives incompatible service fields",
            param->name);
      plan->param_data[param_index] = native_field->value;
      presence = NULL;
    }

    status = binding_entry_init(
        &plan->ingress[i], DATA_BIND_BINDING_INGRESS, &slot,
        target, &field, native_field, param, param_index, indirect,
        native_offset, presence, diagnostic);
    if (status != DATA_BIND_OK) return status;
    plan->ingress_count = i + 1u;
  }

  for (i = 0u; i < function->param_count; ++i) {
    const cmeta_param_desc *param = &function->params[i];
    if ((param->flags & CMETA_PARAM_IN) != 0u &&
        !plan->param_ingress[i])
      return binding_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
          operation->request_type, param->name,
          "Function IN parameter '%s' has no request binding", param->name);
  }

  (void)shape;
  return DATA_BIND_OK;
}

static int binding_has_other_out(
    const cmeta_function_desc *function, size_t allowed) {
  size_t i;
  for (i = 0u; i < function->param_count; ++i)
    if ((function->params[i].flags & CMETA_PARAM_OUT) != 0u &&
        i != allowed)
      return 1;
  return 0;
}

static int binding_return_safe(const cmeta_data_desc *response) {
  const cmeta_type_desc *type;
  const cmeta_trait_flags required =
      CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY;
  if (!cmeta_data_desc_valid(response) || response->storage_type == NULL)
    return 0;
  type = response->storage_type;
  return cmeta_type_require_traits(type, required) == CMETA_OK;
}

static DataBindStatus binding_compile_egress(
    DataBind *codec, const DataBindServiceOperation *operation,
    const DataBindBindingProjection *projection,
    const DataBindNativeRecordBinding *response,
    DataBindBindingPlan *plan,
    DataBindBindingPlanDiagnostic *diagnostic) {
  const cmeta_function_desc *function = plan->function;
  size_t field_count;
  size_t root_param = SIZE_MAX;
  int root_indirect = 0;
  int use_return = 0;
  size_t i;
  DataBindStatus status;

  if (strcmp(operation->response_type, "void") == 0) {
    if (!cmeta_type_equal(function->return_type, &cmeta_type_void))
      return binding_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
          operation->response_type, NULL,
          "Void service response requires a void C return type");
    if (binding_has_other_out(function, SIZE_MAX))
      return binding_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
          operation->response_type, NULL,
          "Void service response cannot expose OUT parameters");
    return DATA_BIND_OK;
  }

  field_count =
      data_bind_schema_field_count(codec, operation->response_type);
  plan->egress = (DataBindBindingPlanEntryOwned *)calloc(
      field_count, sizeof(*plan->egress));
  if (field_count != 0u && plan->egress == NULL)
    return binding_fail(diagnostic, DATA_BIND_ERR_OOM,
                        operation->response_type, NULL,
                        "Out of memory allocating egress plan");

  if (binding_type_matches_data(function->return_type, response->data)) {
    if (!binding_return_safe(response->data))
      return binding_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
          operation->response_type, NULL,
          "By-value response return requires trivial copy/destroy ownership");
    if (binding_has_other_out(function, SIZE_MAX))
      return binding_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
          operation->response_type, NULL,
          "By-value response return cannot be combined with OUT parameters");
    use_return = 1;
  } else {
    for (i = 0u; i < function->param_count; ++i) {
      const cmeta_param_desc *param = &function->params[i];
      int indirect = 0;
      if ((param->flags & CMETA_PARAM_OUT) == 0u) continue;
      if (binding_type_matches_data(
              binding_param_value_type(param, &indirect),
              response->data)) {
        if (root_param != SIZE_MAX)
          return binding_fail(
              diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
              operation->response_type, param->name,
              "Multiple OUT parameters match the complete response");
        root_param = i;
        root_indirect = indirect;
      }
    }
    if (root_param != SIZE_MAX) {
      if ((function->params[root_param].flags & CMETA_PARAM_IN) != 0u)
        return binding_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
            operation->response_type, function->params[root_param].name,
            "Complete response parameter must be OUT-only in BindingPlan v1");
      if (!cmeta_type_equal(function->return_type, &cmeta_type_void) ||
          binding_has_other_out(function, root_param))
        return binding_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
            operation->response_type, function->params[root_param].name,
            "Complete OUT response requires void return and no other OUT parameter");
      plan->param_data[root_param] = response->data;
      plan->param_egress[root_param] = 1u;
    } else if (!cmeta_type_equal(function->return_type, &cmeta_type_void)) {
      return binding_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
          operation->response_type, NULL,
          "Field-mapped response requires void return or admitted root response");
    }
  }

  plan->response_uses_return = use_return;
  plan->has_response_root_param = root_param != SIZE_MAX;
  plan->response_root_param = root_param;

  for (i = 0u; i < field_count; ++i) {
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    DataBindBindingProjectionSlot slot =
        DATA_BIND_BINDING_PROJECTION_SLOT_INIT;
    const cmeta_data_field_desc *native_field;
    const DataBindNativePresenceBinding *presence;
    const cmeta_param_desc *param = NULL;
    size_t param_index = SIZE_MAX;
    int indirect = 0;
    DataBindBindingNativeTarget target;
    size_t native_offset;

    if (!data_bind_schema_field_at(codec, operation->response_type, i, &field))
      return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                          operation->response_type, NULL,
                          "Response field reflection failed");

    native_field = binding_native_field(response->data, field.name);
    status = binding_validate_schema_native_field(
        codec, operation->response_type, i, &field, native_field, diagnostic);
    if (status != DATA_BIND_OK) return status;

    status = binding_project(
        projection, operation, &field, DATA_BIND_BINDING_EGRESS,
        &slot, diagnostic);
    if (status != DATA_BIND_OK) return status;

    presence = binding_presence_find(response, field.name);

    if (use_return) {
      target = DATA_BIND_BINDING_TARGET_RETURN_FIELD;
      native_offset = native_field->offset;
    } else if (root_param != SIZE_MAX) {
      param_index = root_param;
      param = &function->params[param_index];
      indirect = root_indirect;
      target = DATA_BIND_BINDING_TARGET_FUNCTION_PARAM;
      native_offset = native_field->offset;
    } else {
      param_index = binding_param_index(function, field.name);
      if (param_index == SIZE_MAX)
        return binding_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field.name, NULL,
            "Response field '%s' has no reflected OUT parameter", field.name);
      param = &function->params[param_index];
      if ((param->flags & CMETA_PARAM_OUT) == 0u)
        return binding_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
            field.name, param->name,
            "Function parameter '%s' is not an OUT parameter", param->name);
      if (!binding_type_matches_data(
              binding_param_value_type(param, &indirect),
              native_field->value))
        return binding_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
            field.name, param->name,
            "Response field '%s' type disagrees with parameter '%s'",
            field.name, param->name);
      target = DATA_BIND_BINDING_TARGET_FUNCTION_PARAM;
      native_offset = 0u;
      plan->param_egress[param_index] = 1u;
      if (plan->param_data[param_index] != NULL &&
          !binding_data_semantically_equal(
              plan->param_data[param_index], native_field->value))
        return binding_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
            field.name, param->name,
            "INOUT parameter '%s' maps incompatible service fields",
            param->name);
      plan->param_data[param_index] = native_field->value;
      presence = NULL;
    }

    status = binding_entry_init(
        &plan->egress[i], DATA_BIND_BINDING_EGRESS, &slot,
        target, &field, native_field, param, param_index, indirect,
        native_offset, presence, diagnostic);
    if (status != DATA_BIND_OK) return status;
    plan->egress_count = i + 1u;
  }

  if (!use_return && root_param == SIZE_MAX) {
    for (i = 0u; i < function->param_count; ++i) {
      if ((function->params[i].flags & CMETA_PARAM_OUT) != 0u &&
          !plan->param_egress[i])
        return binding_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH,
            operation->response_type, function->params[i].name,
            "Function OUT parameter '%s' has no response binding",
            function->params[i].name);
    }
  }

  return DATA_BIND_OK;
}

DataBindStatus data_bind_binding_plan_compile_service(
    DataBind *codec, const char *service_name, const char *operation_name,
    const DataBindBindingProjection *projection,
    const DataBindServiceNativeBinding *native,
    DataBindBindingPlan **out_plan,
    DataBindBindingPlanDiagnostic *diagnostic) {
  DataBindServiceOperation operation = DATA_BIND_SERVICE_OPERATION_INIT;
  DataBindBindingPlan *plan = NULL;
  DataBindStatus status;

  if (out_plan != NULL) *out_plan = NULL;
  if (!binding_diag_header_valid(diagnostic))
    return DATA_BIND_ERR_INVALID_ARG;
  binding_diag_clear(diagnostic);

  if (codec == NULL || service_name == NULL || operation_name == NULL ||
      projection == NULL || native == NULL || out_plan == NULL ||
      native->size < sizeof(*native) ||
      native->abi_version != DATA_BIND_BINDING_PLAN_ABI_VERSION ||
      native->function == NULL || native->request == NULL)
    return binding_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                        "Invalid BindingPlan compile arguments");

  if (!cmeta_function_desc_valid(native->function))
    return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA, NULL,
                        native->function->name,
                        "Invalid CMeta function descriptor");

  if (projection->size < sizeof(*projection) ||
      projection->abi_version != DATA_BIND_BINDING_PLAN_ABI_VERSION ||
      projection->id == NULL || projection->project_field == NULL)
    return binding_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                        "Invalid logical projection adapter");

  if (!data_bind_service_operation_find(
          codec, service_name, operation_name, &operation))
    return binding_fail(
        diagnostic, DATA_BIND_ERR_TYPE_NOT_FOUND, operation_name, NULL,
        "Service operation '%s.%s' was not found",
        service_name, operation_name);

  status = binding_validate_record(
      codec, native->request, operation.request_type, diagnostic);
  if (status != DATA_BIND_OK) return status;

  if (strcmp(operation.response_type, "void") != 0) {
    if (native->response == NULL)
      return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                          operation.response_type, NULL,
                          "Non-void service response lacks native record binding");
    status = binding_validate_record(
        codec, native->response, operation.response_type, diagnostic);
    if (status != DATA_BIND_OK) return status;
  } else if (native->response != NULL) {
    return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                        operation.response_type, NULL,
                        "Void service response must not provide native response binding");
  }

  plan = (DataBindBindingPlan *)calloc(1u, sizeof(*plan));
  if (plan == NULL)
    return binding_fail(diagnostic, DATA_BIND_ERR_OOM, NULL, NULL,
                        "Out of memory allocating BindingPlan");

  plan->operation_id =
      binding_operation_id(service_name, operation_name);
  plan->projection_id = binding_strdup(projection->id);
  plan->function = native->function;
  plan->request_data = native->request->data;
  plan->response_data =
      native->response != NULL ? native->response->data : NULL;
  plan->param_count = native->function->param_count;

  if (plan->operation_id == NULL || plan->projection_id == NULL) {
    status = binding_fail(diagnostic, DATA_BIND_ERR_OOM, NULL, NULL,
                          "Out of memory copying BindingPlan identity");
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
      status = binding_fail(
          diagnostic, DATA_BIND_ERR_OOM, NULL, NULL,
          "Out of memory allocating function binding map");
      goto fail;
    }
  }

  status = binding_copy_errors(
      codec, service_name, operation_name, operation.error_count,
      plan, diagnostic);
  if (status != DATA_BIND_OK) goto fail;

  status = binding_compile_ingress(
      codec, &operation, projection, native->request, plan, diagnostic);
  if (status != DATA_BIND_OK) goto fail;

  status = binding_compile_egress(
      codec, &operation, projection, native->response, plan, diagnostic);
  if (status != DATA_BIND_OK) goto fail;

  binding_diag_clear(diagnostic);
  *out_plan = plan;
  return DATA_BIND_OK;

fail:
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

static int binding_entry_copy(
    const DataBindBindingPlanEntryOwned *entry,
    DataBindBindingPlanEntry *out) {
  size_t size;
  if (entry == NULL || out == NULL) return 0;
  size = binding_out_size(out->size, sizeof(*out));
  memset(out, 0, size);
  memcpy(out, &entry->pub, size);
  if (size >= sizeof(size_t)) out->size = size;
  return 1;
}

int data_bind_binding_plan_ingress_at(
    const DataBindBindingPlan *plan, size_t index,
    DataBindBindingPlanEntry *out) {
  if (plan == NULL || out == NULL || index >= plan->ingress_count) return 0;
  return binding_entry_copy(&plan->ingress[index], out);
}

int data_bind_binding_plan_egress_at(
    const DataBindBindingPlan *plan, size_t index,
    DataBindBindingPlanEntry *out) {
  if (plan == NULL || out == NULL || index >= plan->egress_count) return 0;
  return binding_entry_copy(&plan->egress[index], out);
}

size_t data_bind_binding_plan_error_count(
    const DataBindBindingPlan *plan) {
  return plan != NULL ? plan->error_count : 0u;
}

const char *data_bind_binding_plan_error_at(
    const DataBindBindingPlan *plan, size_t index) {
  return plan != NULL && index < plan->error_count
             ? plan->errors[index]
             : NULL;
}

static int binding_provider_valid_for_input(
    const DataBindBindingProvider *provider) {
  return provider != NULL && provider->size >= sizeof(*provider) &&
         provider->abi_version == DATA_BIND_BINDING_PLAN_ABI_VERSION &&
         provider->open_input != NULL;
}

static int binding_provider_valid_for_output(
    const DataBindBindingProvider *provider) {
  return provider != NULL && provider->size >= sizeof(*provider) &&
         provider->abi_version == DATA_BIND_BINDING_PLAN_ABI_VERSION &&
         provider->begin_output != NULL &&
         provider->write_output != NULL &&
         provider->commit_output != NULL &&
         provider->abort_output != NULL;
}

static DataBindStatus binding_frame_preflight(
    const DataBindBindingPlan *plan,
    const DataBindBindingCallFrame *frame,
    DataBindBindingPlanDiagnostic *diagnostic) {
  size_t i;

  if (plan == NULL || frame == NULL || frame->size < sizeof(*frame))
    return binding_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                        "Invalid BindingPlan call frame");

  if (frame->param_count < plan->param_count)
    return binding_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                        "Call frame exposes too few function parameters");

  if (plan->has_request_root_param) {
    size_t bytes = plan->request_data->storage_type->size;
    if (frame->request == NULL || frame->request_bytes < bytes)
      return binding_fail(
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
      return binding_fail(
          diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL,
          plan->function->params[i].name,
          "Function parameter '%s' staging storage is missing or too small",
          plan->function->params[i].name);
  }

  if (plan->response_uses_return) {
    size_t bytes = plan->response_data->storage_type->size;
    if (frame->return_value == NULL || frame->return_bytes < bytes)
      return binding_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                          "Return-value staging storage is missing or too small");
  }

  return DATA_BIND_OK;
}

static DataBindStatus binding_native_init(
    const DataBindNativeOptions *options, const cmeta_data_desc *data,
    void *storage, DataBindBindingPlanDiagnostic *diagnostic,
    const char *schema_field, const char *function_param) {
  DataBindNativeDiagnostic native = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  DataBindStatus status;
  if (data == NULL || data->storage_type == NULL)
    return binding_fail(diagnostic, DATA_BIND_ERR_SCHEMA,
                        schema_field, function_param,
                        "Native binding descriptor is incomplete");
  status = data_bind_native_init(
      options, data, storage, data->storage_type->size, &native);
  if (status != DATA_BIND_OK)
    return binding_fail_error(
        diagnostic, status, schema_field, function_param,
        &native.error, "Native staging initialization failed");
  return DATA_BIND_OK;
}

static void binding_native_clear_noexcept(
    const DataBindNativeOptions *options, const cmeta_data_desc *data,
    void *storage) {
  DataBindNativeDiagnostic native = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  if (data == NULL || data->storage_type == NULL || storage == NULL) return;
  (void)data_bind_native_clear(
      options, data, storage, data->storage_type->size, &native);
}

static cserde_status default_reader_next(void *context, cserde_token *out) {
  DefaultReaderContext *state = (DefaultReaderContext *)context;
  if (state == NULL || out == NULL || state->token == NULL)
    return CSERDE_INVALID_ARGUMENT;
  if (state->emitted) return CSERDE_DONE;
  *out = *state->token;
  state->emitted = 1;
  return CSERDE_OK;
}

static const cserde_reader_ops DEFAULT_READER_OPS = {
    offsetof(cserde_reader_ops, next) + sizeof(cserde_reader_next_fn),
    CSERDE_READER_OPS_ABI_VERSION,
    default_reader_next};

static void *binding_entry_destination(
    const DataBindBindingPlanEntry *entry,
    DataBindBindingCallFrame *frame) {
  unsigned char *base = NULL;
  if (entry->target == DATA_BIND_BINDING_TARGET_REQUEST_FIELD) {
    base = (unsigned char *)frame->request;
  } else if (entry->target == DATA_BIND_BINDING_TARGET_FUNCTION_PARAM &&
             entry->function_param_index < frame->param_count &&
             frame->params != NULL) {
    base = (unsigned char *)frame->params[entry->function_param_index];
  }
  return base != NULL ? base + entry->native_offset : NULL;
}

static const void *binding_entry_output(
    const DataBindBindingPlanEntry *entry,
    const DataBindBindingCallFrame *frame) {
  const unsigned char *base = NULL;
  if (entry->target == DATA_BIND_BINDING_TARGET_RETURN_FIELD) {
    base = (const unsigned char *)frame->return_value;
  } else if (entry->target == DATA_BIND_BINDING_TARGET_FUNCTION_PARAM &&
             entry->function_param_index < frame->param_count &&
             frame->params != NULL) {
    base = (const unsigned char *)frame->params[entry->function_param_index];
  }
  return base != NULL ? base + entry->native_offset : NULL;
}

static void binding_cleanup_inputs(
    const DataBindBindingPlan *plan,
    const DataBindNativeOptions *options,
    DataBindBindingCallFrame *frame,
    size_t params_initialized,
    int request_initialized) {
  size_t i = params_initialized;
  while (i != 0u) {
    --i;
    if (plan->param_data[i] != NULL && frame->params != NULL)
      binding_native_clear_noexcept(
          options, plan->param_data[i], frame->params[i]);
  }
  if (request_initialized) {
    binding_native_clear_noexcept(
        options, plan->request_data, frame->request);
    for (i = 0u; i < plan->ingress_count; ++i) {
      const DataBindBindingPlanEntry *entry = &plan->ingress[i].pub;
      if (entry->has_presence && frame->request != NULL) {
        unsigned char *presence =
            (unsigned char *)frame->request + entry->presence_offset;
        *presence &=
            (unsigned char)~(1u << entry->presence_bit);
      }
    }
  }
}

DataBindStatus data_bind_binding_plan_bind_inputs(
    const DataBindBindingPlan *plan,
    const DataBindBindingProvider *provider,
    const DataBindNativeOptions *native_options,
    DataBindBindingCallFrame *frame,
    DataBindBindingPlanDiagnostic *diagnostic) {
  size_t i;
  size_t params_initialized = 0u;
  int request_initialized = 0;
  DataBindStatus status;

  if (!binding_diag_header_valid(diagnostic))
    return DATA_BIND_ERR_INVALID_ARG;
  binding_diag_clear(diagnostic);

  if (plan == NULL || native_options == NULL || frame == NULL)
    return binding_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                        "Invalid BindingPlan input arguments");

  if (plan->ingress_count != 0u &&
      !binding_provider_valid_for_input(provider))
    return binding_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                        "Invalid logical input provider");

  status = binding_frame_preflight(plan, frame, diagnostic);
  if (status != DATA_BIND_OK) return status;

  if (plan->has_request_root_param) {
    status = binding_native_init(
        native_options, plan->request_data, frame->request, diagnostic,
        NULL, plan->function->params[plan->request_root_param].name);
    if (status != DATA_BIND_OK) return status;
    request_initialized = 1;
  }

  for (i = 0u; i < plan->param_count; ++i) {
    if (plan->param_data[i] != NULL) {
      status = binding_native_init(
          native_options, plan->param_data[i], frame->params[i],
          diagnostic, NULL, plan->function->params[i].name);
      if (status != DATA_BIND_OK) {
        binding_cleanup_inputs(
            plan, native_options, frame, i, request_initialized);
        return status;
      }
    }
    params_initialized = i + 1u;
  }

  for (i = 0u; i < plan->ingress_count; ++i) {
    const DataBindBindingPlanEntryOwned *owned = &plan->ingress[i];
    const DataBindBindingPlanEntry *entry = &owned->pub;
    void *destination = binding_entry_destination(entry, frame);
    cserde_reader reader = {0};
    int present = 0;
    const int *unused = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindNativeDiagnostic native = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    int provider_present;

    (void)unused;
    if (destination == NULL) {
      status = binding_fail(
          diagnostic, DATA_BIND_ERR_INVALID_ARG,
          entry->schema_field, entry->function_param,
          "Compiled ingress entry has no call-frame destination");
      goto fail;
    }

    status = provider->open_input(
        provider->context, entry, &reader, &present, &error);
    if (status != DATA_BIND_OK) {
      status = binding_fail_error(
          diagnostic, status, entry->schema_field, entry->function_param,
          &error, "Input provider failed");
      goto fail;
    }
    provider_present = present;

    if (!present) {
      if (owned->has_default_token) {
        DefaultReaderContext context = {&owned->default_token, 0};
        if (cserde_reader_init(
                &reader, &DEFAULT_READER_OPS, &context) != CSERDE_OK) {
          status = binding_fail(
              diagnostic, DATA_BIND_ERR_RUNTIME,
              entry->schema_field, entry->function_param,
              "Could not initialize compiled default reader");
          goto fail;
        }
      } else if (!entry->required) {
        continue;
      } else {
        status = binding_fail(
            diagnostic, DATA_BIND_ERR_TYPE_NOT_FOUND,
            entry->schema_field, entry->function_param,
            "Required logical input '%s' is absent",
            entry->selector != NULL ? entry->selector
                                    : entry->logical_name);
        goto fail;
      }
    }

    status = data_bind_native_decode(
        native_options, entry->data, &reader, destination,
        entry->data->storage_type->size, &native);
    if (status != DATA_BIND_OK) {
      status = binding_fail_error(
          diagnostic, status, entry->schema_field, entry->function_param,
          &native.error, "Native input decode failed");
      goto fail;
    }

    if (entry->has_presence && provider_present) {
      unsigned char *presence =
          (unsigned char *)frame->request + entry->presence_offset;
      *presence |= (unsigned char)(1u << entry->presence_bit);
    }
  }

  binding_diag_clear(diagnostic);
  return DATA_BIND_OK;

fail:
  binding_cleanup_inputs(
      plan, native_options, frame, params_initialized, request_initialized);
  return status;
}

DataBindStatus data_bind_binding_plan_write_outputs(
    const DataBindBindingPlan *plan,
    const DataBindBindingProvider *provider,
    const DataBindBindingCallFrame *frame,
    DataBindBindingPlanDiagnostic *diagnostic) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status;
  size_t i;

  if (!binding_diag_header_valid(diagnostic))
    return DATA_BIND_ERR_INVALID_ARG;
  binding_diag_clear(diagnostic);

  if (plan == NULL || frame == NULL)
    return binding_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                        "Invalid BindingPlan output arguments");

  if (plan->egress_count != 0u &&
      !binding_provider_valid_for_output(provider))
    return binding_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL, NULL,
                        "Invalid logical output provider");

  status = binding_frame_preflight(plan, frame, diagnostic);
  if (status != DATA_BIND_OK) return status;
  if (plan->egress_count == 0u) return DATA_BIND_OK;

  status = provider->begin_output(provider->context, &error);
  if (status != DATA_BIND_OK)
    return binding_fail_error(diagnostic, status, NULL, NULL, &error,
                              "Output transaction could not begin");

  for (i = 0u; i < plan->egress_count; ++i) {
    const DataBindBindingPlanEntry *entry = &plan->egress[i].pub;
    const void *value;

    if (entry->has_presence) {
      const unsigned char *base;
      if (entry->target == DATA_BIND_BINDING_TARGET_RETURN_FIELD) {
        base = (const unsigned char *)frame->return_value;
      } else if (entry->target ==
                     DATA_BIND_BINDING_TARGET_FUNCTION_PARAM &&
                 entry->function_param_index < frame->param_count &&
                 frame->params != NULL) {
        base = (const unsigned char *)
            frame->params[entry->function_param_index];
      } else {
        base = NULL;
      }
      if (base == NULL) {
        provider->abort_output(provider->context);
        return binding_fail(
            diagnostic, DATA_BIND_ERR_INVALID_ARG,
            entry->schema_field, entry->function_param,
            "Optional response presence storage is unavailable");
      }
      if ((((const unsigned char *)
                base)[entry->presence_offset] &
           (unsigned char)(1u << entry->presence_bit)) == 0u)
        continue;
    }

    value = binding_entry_output(entry, frame);
    if (value == NULL || entry->data == NULL ||
        entry->data->storage_type == NULL) {
      provider->abort_output(provider->context);
      return binding_fail(
          diagnostic, DATA_BIND_ERR_INVALID_ARG,
          entry->schema_field, entry->function_param,
          "Compiled egress entry has no call-frame storage");
    }

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    status = provider->write_output(
        provider->context, entry, value,
        entry->data->storage_type->size, &error);
    if (status != DATA_BIND_OK) {
      provider->abort_output(provider->context);
      return binding_fail_error(
          diagnostic, status, entry->schema_field, entry->function_param,
          &error, "Output provider write failed");
    }
  }

  error = (DataBindError)DATA_BIND_ERROR_INIT;
  status = provider->commit_output(provider->context, &error);
  if (status != DATA_BIND_OK) {
    provider->abort_output(provider->context);
    return binding_fail_error(diagnostic, status, NULL, NULL, &error,
                              "Output transaction commit failed");
  }

  binding_diag_clear(diagnostic);
  return DATA_BIND_OK;
}
