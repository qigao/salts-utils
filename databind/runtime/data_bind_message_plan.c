#include "data_bind_message_plan.h"
#include "data_bind_message_executor.h"
#include "data_bind_message_plan_internal.h"
#include "data_bind_native_internal.h"
#include "data_bind_validation_plan.h"
#include "data_bind_validation_plan_internal.h"

#include <cmeta/type_traits.h>

#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct DataBindMessageFieldPlan {
  char *name;
  char *default_value;
  const cmeta_data_desc *data;
  size_t native_offset;

  int optional;
  int nullable;
  int has_default;
  cserde_token default_token;
  int has_default_token;

  int has_presence;
  size_t presence_offset;
  unsigned presence_bit;

  int has_null;
  size_t null_offset;
  unsigned null_bit;

  size_t validation_rule_start;
  size_t validation_rule_count;
} DataBindMessageFieldPlan;

struct DataBindMessagePlan {
  char *type_name;
  const DataBindNativeTypeBinding *native;
  DataBindMessageFieldPlan *fields;
  size_t field_count;
  DataBindValidationPlan *validation;
};

static size_t message_out_size(size_t requested, size_t full) {
  return requested != 0u && requested < full ? requested : full;
}

static int message_diag_header_valid(
    const DataBindMessagePlanDiagnostic *diagnostic) {
  return diagnostic == NULL ||
         diagnostic->size >=
             offsetof(DataBindMessagePlanDiagnostic, status) +
                 sizeof(diagnostic->status);
}

static void message_diag_clear(DataBindMessagePlanDiagnostic *diagnostic) {
  size_t size;
  if (diagnostic == NULL) return;
  size = message_out_size(diagnostic->size, sizeof(*diagnostic));
  memset(diagnostic, 0, size);
  if (size >= sizeof(size_t)) diagnostic->size = size;
  if (size >= offsetof(DataBindMessagePlanDiagnostic, abi_version) +
                  sizeof(diagnostic->abi_version))
    diagnostic->abi_version = DATA_BIND_MESSAGE_PLAN_ABI_VERSION;
  if (size >= offsetof(DataBindMessagePlanDiagnostic, status) +
                  sizeof(diagnostic->status))
    diagnostic->status = DATA_BIND_OK;
}

static DataBindStatus message_fail(
    DataBindMessagePlanDiagnostic *diagnostic,
    DataBindStatus status,
    const char *field,
    const char *fmt,
    ...) {
  va_list ap;
  size_t size;

  if (diagnostic == NULL) return status;
  size = message_out_size(diagnostic->size, sizeof(*diagnostic));
  memset(diagnostic, 0, size);
  if (size >= sizeof(size_t)) diagnostic->size = size;
  if (size >= offsetof(DataBindMessagePlanDiagnostic, abi_version) +
                  sizeof(diagnostic->abi_version))
    diagnostic->abi_version = DATA_BIND_MESSAGE_PLAN_ABI_VERSION;
  if (size >= offsetof(DataBindMessagePlanDiagnostic, status) +
                  sizeof(diagnostic->status))
    diagnostic->status = status;
  if (size >= offsetof(DataBindMessagePlanDiagnostic, schema_field) +
                  sizeof(diagnostic->schema_field))
    snprintf(diagnostic->schema_field, sizeof(diagnostic->schema_field),
             "%s", field != NULL ? field : "");
  if (size >= offsetof(DataBindMessagePlanDiagnostic, message) +
                  sizeof(diagnostic->message)) {
    va_start(ap, fmt);
    vsnprintf(diagnostic->message, sizeof(diagnostic->message), fmt, ap);
    va_end(ap);
  }
  return status;
}

static char *message_strdup(const char *text) {
  size_t length;
  char *copy;
  if (text == NULL) return NULL;
  length = strlen(text);
  if (length == SIZE_MAX) return NULL;
  copy = (char *)malloc(length + 1u);
  if (copy != NULL) memcpy(copy, text, length + 1u);
  return copy;
}

static const cmeta_data_struct_shape *message_struct_shape(
    const DataBindNativeTypeBinding *binding) {
  if (binding == NULL || binding->data == NULL ||
      binding->data->kind != CMETA_DATA_STRUCT ||
      binding->data->shape == NULL)
    return NULL;
  return (const cmeta_data_struct_shape *)binding->data->shape;
}

static const cmeta_data_field_desc *message_native_field(
    const DataBindNativeTypeBinding *binding,
    const char *name) {
  const cmeta_data_struct_shape *shape = message_struct_shape(binding);
  size_t i;
  if (shape == NULL || name == NULL) return NULL;
  for (i = 0u; i < shape->field_count; ++i) {
    const cmeta_data_field_desc *field = &shape->fields[i];
    if (field->name != NULL && strcmp(field->name, name) == 0)
      return field;
  }
  return NULL;
}

static const DataBindNativeStateBinding *message_state_binding(
    const DataBindNativeStateBinding *bindings,
    size_t count,
    const char *name) {
  size_t i;
  if (bindings == NULL || name == NULL) return NULL;
  for (i = 0u; i < count; ++i)
    if (bindings[i].field_name != NULL &&
        strcmp(bindings[i].field_name, name) == 0)
      return &bindings[i];
  return NULL;
}

static int message_data_semantically_equal(
    const cmeta_data_desc *left,
    const cmeta_data_desc *right) {
  if (left == right) return left != NULL && cmeta_data_desc_valid(left);
  if (!cmeta_data_desc_valid(left) || !cmeta_data_desc_valid(right) ||
      left->kind != right->kind ||
      left->storage_type == NULL || right->storage_type == NULL ||
      !cmeta_type_equal(left->storage_type, right->storage_type))
    return 0;
  if (left->stable_id != NULL && right->stable_id != NULL)
    return strcmp(left->stable_id, right->stable_id) == 0;
  return 1;
}

static DataBindMessageFieldPlan *message_field(
    DataBindMessagePlan *plan,
    const char *name) {
  size_t i;
  if (plan == NULL || name == NULL) return NULL;
  for (i = 0u; i < plan->field_count; ++i)
    if (plan->fields[i].name != NULL &&
        strcmp(plan->fields[i].name, name) == 0)
      return &plan->fields[i];
  return NULL;
}

static const DataBindMessageFieldPlan *message_field_const(
    const DataBindMessagePlan *plan,
    const char *name) {
  return message_field((DataBindMessagePlan *)plan, name);
}

static int message_state_metadata_valid(
    DataBind *codec,
    const char *type_name,
    const DataBindNativeTypeBinding *binding,
    const cmeta_data_struct_shape *shape,
    DataBindMessagePlanDiagnostic *diagnostic) {
  const DataBindNativeStateBinding *sets[2] = {
      binding->presence, binding->nulls};
  const size_t counts[2] = {
      binding->presence_count, binding->null_count};
  const char *const labels[2] = {"presence", "null"};
  DataBindSchemaType schema_type = DATA_BIND_SCHEMA_TYPE_INIT;
  size_t set_index;

  if (!data_bind_schema_find_type(codec, type_name, &schema_type))
    return 0;

  if ((binding->presence_count != 0u && binding->presence == NULL) ||
      (binding->null_count != 0u && binding->nulls == NULL)) {
    message_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, type_name,
        "Native state metadata count is nonzero without its binding array");
    return 0;
  }

  for (set_index = 0u; set_index < 2u; ++set_index) {
    size_t state_index;
    for (state_index = 0u; state_index < counts[set_index]; ++state_index) {
      const DataBindNativeStateBinding *left =
          &sets[set_index][state_index];
      int found = 0;
      size_t j;

      if (left->size < sizeof(*left) ||
          left->field_name == NULL || left->field_name[0] == '\0' ||
          left->bit > 7u ||
          left->byte_offset >= binding->data->storage_type->size) {
        message_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, type_name,
            "Invalid native %s-state metadata", labels[set_index]);
        return 0;
      }

      for (j = 0u; j < shape->field_count; ++j) {
        const cmeta_data_field_desc *native_field = &shape->fields[j];
        size_t field_size;
        if (native_field->value == NULL ||
            native_field->value->storage_type == NULL) {
          message_fail(
              diagnostic, DATA_BIND_ERR_SCHEMA, type_name,
              "Native field metadata is incomplete while validating %s state",
              labels[set_index]);
          return 0;
        }
        field_size = native_field->value->storage_type->size;
        if (left->byte_offset >= native_field->offset &&
            left->byte_offset - native_field->offset < field_size) {
          message_fail(
              diagnostic, DATA_BIND_ERR_SCHEMA, left->field_name,
              "Native %s state storage overlaps field '%s'",
              labels[set_index],
              native_field->name != NULL ? native_field->name : "<unnamed>");
          return 0;
        }
      }

      for (j = 0u; j < schema_type.field_count; ++j) {
        DataBindSchemaField reflected = DATA_BIND_SCHEMA_FIELD_INIT;
        if (data_bind_schema_field_at(codec, type_name, j, &reflected) &&
            reflected.name != NULL &&
            strcmp(reflected.name, left->field_name) == 0) {
          found = set_index == 0u ? reflected.is_optional != 0
                                  : reflected.is_nullable != 0;
          break;
        }
      }
      if (!found) {
        message_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, left->field_name,
            "%s metadata references a non-%s IDL field",
            labels[set_index],
            set_index == 0u ? "optional" : "nullable");
        return 0;
      }

      for (j = 0u; j < state_index; ++j) {
        const DataBindNativeStateBinding *right = &sets[set_index][j];
        if (strcmp(left->field_name, right->field_name) == 0 ||
            (left->byte_offset == right->byte_offset &&
             left->bit == right->bit)) {
          message_fail(
              diagnostic, DATA_BIND_ERR_SCHEMA, left->field_name,
              "Duplicate native %s-state metadata", labels[set_index]);
          return 0;
        }
      }

      if (set_index == 1u) {
        for (j = 0u; j < binding->presence_count; ++j) {
          const DataBindNativeStateBinding *right = &binding->presence[j];
          if (left->byte_offset == right->byte_offset &&
              left->bit == right->bit) {
            message_fail(
                diagnostic, DATA_BIND_ERR_SCHEMA, left->field_name,
                "Native presence and null state must not share one bit");
            return 0;
          }
        }
      }
    }
  }

  return 1;
}

static DataBindStatus message_compile_default_token(
    DataBindMessageFieldPlan *field,
    DataBindMessagePlanDiagnostic *diagnostic);

static DataBindStatus message_compile_fields(
    DataBind *codec,
    const char *type_name,
    const DataBindNativeTypeBinding *native,
    DataBindMessagePlan *plan,
    DataBindMessagePlanDiagnostic *diagnostic) {
  const cmeta_data_struct_shape *shape = message_struct_shape(native);
  DataBindSchemaType schema_type = DATA_BIND_SCHEMA_TYPE_INIT;
  size_t i;

  if (shape == NULL ||
      !data_bind_schema_find_type(codec, type_name, &schema_type) ||
      schema_type.field_count != data_bind_schema_field_count(codec, type_name))
    return message_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, type_name,
        "IDL type '%s' is not a reflected record", type_name);

  if (shape->field_count != schema_type.field_count)
    return message_fail(
        diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, type_name,
        "Native field count does not match DataBind IDL type '%s'", type_name);

  if (!message_state_metadata_valid(
          codec, type_name, native, shape, diagnostic))
    return diagnostic != NULL ? diagnostic->status : DATA_BIND_ERR_SCHEMA;

  plan->field_count = schema_type.field_count;
  if (plan->field_count != 0u) {
    plan->fields = (DataBindMessageFieldPlan *)calloc(
        plan->field_count, sizeof(*plan->fields));
    if (plan->fields == NULL)
      return message_fail(
          diagnostic, DATA_BIND_ERR_OOM, type_name,
          "Could not allocate MessagePlan fields");
  }

  for (i = 0u; i < plan->field_count; ++i) {
    DataBindSchemaField schema_field = DATA_BIND_SCHEMA_FIELD_INIT;
    const cmeta_data_field_desc *native_field;
    const cmeta_data_desc *schema_data = NULL;
    const DataBindNativeStateBinding *presence;
    const DataBindNativeStateBinding *null_state;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindMessageFieldPlan *field = &plan->fields[i];

    if (!data_bind_schema_field_at(codec, type_name, i, &schema_field))
      return message_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, type_name,
          "Could not reflect field %zu of '%s'", i, type_name);

    native_field = message_native_field(native, schema_field.name);
    if (native_field == NULL || native_field->value == NULL)
      return message_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, schema_field.name,
          "Native type '%s' is missing field '%s'", type_name,
          schema_field.name != NULL ? schema_field.name : "");

    schema_data = schema_field.cmeta_data;
    if (schema_data == NULL &&
        data_bind_schema_field_cmeta_data(
            codec, type_name, i, &schema_data, &error) != DATA_BIND_OK)
      return message_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, schema_field.name,
          "IDL field '%s.%s' has no canonical CMeta data mapping",
          type_name, schema_field.name != NULL ? schema_field.name : "");

    if (!message_data_semantically_equal(schema_data, native_field->value))
      return message_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, schema_field.name,
          "Native CMeta field '%s.%s' does not match DataBind IDL semantics",
          type_name, schema_field.name != NULL ? schema_field.name : "");

    field->name = message_strdup(schema_field.name);
    field->data = native_field->value;
    field->native_offset = native_field->offset;
    field->optional = schema_field.is_optional != 0;
    field->nullable = schema_field.is_nullable != 0;
    field->has_default = schema_field.has_default != 0;
    if (field->has_default && schema_field.default_value != NULL)
      field->default_value = message_strdup(schema_field.default_value);
    if (field->name == NULL ||
        (field->has_default && schema_field.default_value != NULL &&
         field->default_value == NULL))
      return message_fail(
          diagnostic, DATA_BIND_ERR_OOM, schema_field.name,
          "Could not copy MessagePlan field metadata");

    presence = message_state_binding(
        native->presence, native->presence_count, schema_field.name);
    null_state = message_state_binding(
        native->nulls, native->null_count, schema_field.name);

    if (field->optional) {
      if (presence == NULL)
        return message_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, schema_field.name,
            "Optional field '%s.%s' lacks native presence state",
            type_name, schema_field.name);
      field->has_presence = 1;
      field->presence_offset = presence->byte_offset;
      field->presence_bit = presence->bit;
    }
    if (field->nullable) {
      if (null_state == NULL)
        return message_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, schema_field.name,
            "Nullable field '%s.%s' lacks native null state",
            type_name, schema_field.name);
      field->has_null = 1;
      field->null_offset = null_state->byte_offset;
      field->null_bit = null_state->bit;
    }

    {
      DataBindStatus default_status =
          message_compile_default_token(field, diagnostic);
      if (default_status != DATA_BIND_OK) return default_status;
    }
  }

  return DATA_BIND_OK;
}

static DataBindStatus message_compile_default_token(
    DataBindMessageFieldPlan *field,
    DataBindMessagePlanDiagnostic *diagnostic) {
  const char *text;
  char *end = NULL;

  if (field == NULL || !field->has_default) return DATA_BIND_OK;
  text = field->default_value;
  if (text == NULL || field->data == NULL)
    return message_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA,
        field != NULL ? field->name : NULL,
        "Default metadata is incomplete");

  errno = 0;
  switch (field->data->kind) {
  case CMETA_DATA_BOOL:
    field->default_token.kind = CSERDE_BOOL;
    if (strcmp(text, "true") == 0 || strcmp(text, "1") == 0)
      field->default_token.value.boolean = true;
    else if (strcmp(text, "false") == 0 || strcmp(text, "0") == 0)
      field->default_token.value.boolean = false;
    else
      return message_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, field->name,
          "Boolean default '%s' is invalid", text);
    break;
  case CMETA_DATA_SINT: {
    long long value = strtoll(text, &end, 10);
    if (errno != 0 || end == text || end == NULL || *end != '\0')
      return message_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, field->name,
          "Signed default '%s' is invalid", text);
    field->default_token.kind = CSERDE_SINT;
    field->default_token.value.sint = (int64_t)value;
    break;
  }
  case CMETA_DATA_UINT: {
    unsigned long long value;
    if (text[0] == '-')
      return message_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, field->name,
          "Unsigned default '%s' is invalid", text);
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || end == NULL || *end != '\0')
      return message_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, field->name,
          "Unsigned default '%s' is invalid", text);
    field->default_token.kind = CSERDE_UINT;
    field->default_token.value.uint = (uint64_t)value;
    break;
  }
  case CMETA_DATA_FLOAT: {
    double value = strtod(text, &end);
    if (errno != 0 || end == text || end == NULL || *end != '\0')
      return message_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, field->name,
          "Floating default '%s' is invalid", text);
    field->default_token.kind = CSERDE_FLOAT;
    field->default_token.value.floating = value;
    break;
  }
  case CMETA_DATA_STRING:
  case CMETA_DATA_ENUM:
    field->default_token.kind = CSERDE_STRING;
    field->default_token.value.slice.data =
        (const unsigned char *)field->default_value;
    field->default_token.value.slice.size = strlen(field->default_value);
    field->default_token.value.slice.lifetime = CSERDE_VIEW_STABLE;
    break;
  case CMETA_DATA_BYTES:
    field->default_token.kind = CSERDE_BYTES;
    field->default_token.value.slice.data =
        (const unsigned char *)field->default_value;
    field->default_token.value.slice.size = strlen(field->default_value);
    field->default_token.value.slice.lifetime = CSERDE_VIEW_STABLE;
    break;
  default:
    return message_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, field->name,
        "Default for field '%s' uses unsupported native semantics",
        field->name != NULL ? field->name : "<unnamed>");
  }

  field->has_default_token = 1;
  return DATA_BIND_OK;
}

static DataBindStatus message_compile_validation(
    DataBind *codec,
    const char *type_name,
    DataBindMessagePlan *plan,
    DataBindMessagePlanDiagnostic *diagnostic) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  size_t rule_count;
  size_t rule_index;
  DataBindStatus status;

  status = data_bind_validation_plan_compile(
      codec, type_name, &plan->validation, &error);
  if (status != DATA_BIND_OK)
    return message_fail(
        diagnostic, status,
        error.path[0] != '\0' ? error.path : type_name,
        "%s",
        error.message[0] != '\0'
            ? error.message
            : "Could not compile MessagePlan ValidationPlan");

  rule_count = data_bind_validation_plan_rule_count(plan->validation);
  if (data_bind_validation_plan_internal_child_count(plan->validation) != 0u)
    return message_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, type_name,
        "Nested native ValidationPlan execution is not admitted by this "
        "MessagePlan slice");

  for (rule_index = 0u; rule_index < rule_count; ++rule_index) {
    DataBindValidationRuleInfo info = {0};
    DataBindMessageFieldPlan *field;
    const cmeta_data_buffer_ops *buffer_ops = NULL;

    if (!data_bind_validation_plan_internal_rule_info(
            plan->validation, rule_index, &info) ||
        info.field_name == NULL)
      return message_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, type_name,
          "Compiled ValidationPlan rule has no field identity");

    field = message_field(plan, info.field_name);
    if (field == NULL || field->data == NULL)
      return message_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name,
          "ValidationPlan field has no compiled native MessagePlan field");

    if (field->data->kind != info.field_kind)
      return message_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, info.field_name,
          "ValidationPlan field kind does not match admitted native value");

    if (info.kind == DATA_BIND_SCHEMA_CONSTRAINT_SIZE) {
      if (info.field_kind != CMETA_DATA_STRING &&
          info.field_kind != CMETA_DATA_BYTES)
        return message_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name,
            "Native @Size currently requires canonical string/bytes storage; "
            "container size validation awaits a canonical range provider");
      buffer_ops = cmeta_data_buffer_ops_of(field->data);
      if (buffer_ops == NULL || buffer_ops->read == NULL)
        return message_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name,
            "Native @Size requires a canonical readable buffer provider");
    } else if (info.kind == DATA_BIND_SCHEMA_CONSTRAINT_PATTERN) {
      if (info.field_kind != CMETA_DATA_STRING)
        return message_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name,
            "Native @Pattern requires canonical string storage");
      buffer_ops = cmeta_data_buffer_ops_of(field->data);
      if (buffer_ops == NULL || buffer_ops->read == NULL)
        return message_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name,
            "Native @Pattern requires a canonical readable string provider");
    } else if (info.kind != DATA_BIND_SCHEMA_CONSTRAINT_MIN &&
               info.kind != DATA_BIND_SCHEMA_CONSTRAINT_MAX) {
      return message_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name,
          "ValidationPlan contains an unsupported native constraint kind");
    }

    if (field->validation_rule_count == 0u)
      field->validation_rule_start = rule_index;
    else if (field->validation_rule_start +
                 field->validation_rule_count != rule_index)
      return message_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name,
          "ValidationPlan rules for one field are not contiguous");
    ++field->validation_rule_count;
  }

  return DATA_BIND_OK;
}

void data_bind_message_plan_free(DataBindMessagePlan *plan) {
  size_t i;
  if (plan == NULL) return;
  for (i = 0u; i < plan->field_count; ++i) {
    free(plan->fields[i].name);
    free(plan->fields[i].default_value);
  }
  free(plan->fields);
  data_bind_validation_plan_free(plan->validation);
  free(plan->type_name);
  free(plan);
}

DataBindStatus data_bind_message_plan_compile(
    DataBind *codec,
    const char *type_name,
    const DataBindNativeTypeBinding *native,
    DataBindMessagePlan **out_plan,
    DataBindMessagePlanDiagnostic *diagnostic) {
  DataBindMessagePlan *plan = NULL;
  DataBindStatus status;

  if (!message_diag_header_valid(diagnostic))
    return DATA_BIND_ERR_INVALID_ARG;
  message_diag_clear(diagnostic);
  if (out_plan != NULL) *out_plan = NULL;

  if (codec == NULL || type_name == NULL || type_name[0] == '\0' ||
      native == NULL || out_plan == NULL ||
      native->size <
          offsetof(DataBindNativeTypeBinding, null_count) +
              sizeof(native->null_count) ||
      native->abi_version != DATA_BIND_NATIVE_BINDING_ABI_VERSION ||
      native->idl_type_name == NULL ||
      strcmp(native->idl_type_name, type_name) != 0 ||
      !cmeta_data_desc_valid(native->data) ||
      native->data->kind != CMETA_DATA_STRUCT ||
      native->data->storage_type == NULL ||
      native->data->shape == NULL)
    return message_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, type_name,
        "Invalid native binding for DataBind IDL type '%s'",
        type_name != NULL ? type_name : "");

  plan = (DataBindMessagePlan *)calloc(1u, sizeof(*plan));
  if (plan == NULL)
    return message_fail(
        diagnostic, DATA_BIND_ERR_OOM, type_name,
        "Could not allocate DataBind MessagePlan");

  plan->type_name = message_strdup(type_name);
  plan->native = native;
  if (plan->type_name == NULL) {
    status = message_fail(
        diagnostic, DATA_BIND_ERR_OOM, type_name,
        "Could not copy MessagePlan identity");
    goto fail;
  }

  status = message_compile_fields(
      codec, type_name, native, plan, diagnostic);
  if (status != DATA_BIND_OK) goto fail;

  status = message_compile_validation(
      codec, type_name, plan, diagnostic);
  if (status != DATA_BIND_OK) goto fail;

  message_diag_clear(diagnostic);
  *out_plan = plan;
  return DATA_BIND_OK;

fail:
  data_bind_message_plan_free(plan);
  return status;
}

const char *data_bind_message_plan_type_name(
    const DataBindMessagePlan *plan) {
  return plan != NULL ? plan->type_name : NULL;
}

const DataBindNativeTypeBinding *data_bind_message_plan_native_binding(
    const DataBindMessagePlan *plan) {
  return plan != NULL ? plan->native : NULL;
}

size_t data_bind_message_plan_field_count(
    const DataBindMessagePlan *plan) {
  return plan != NULL ? plan->field_count : 0u;
}

DataBindStatus data_bind_message_plan_internal_validate_field(
    const DataBindMessagePlan *plan,
    const char *field_name,
    const void *source,
    DataBindError *error) {
  const DataBindMessageFieldPlan *field;
  size_t i;

  if (error != NULL) *error = (DataBindError)DATA_BIND_ERROR_INIT;
  if (plan == NULL || field_name == NULL || source == NULL)
    return DATA_BIND_ERR_INVALID_ARG;

  field = message_field_const(plan, field_name);
  if (field == NULL) return DATA_BIND_ERR_SCHEMA;
  if (field->validation_rule_count == 0u) return DATA_BIND_OK;
  if (plan->validation == NULL) return DATA_BIND_ERR_RUNTIME;

  for (i = 0u; i < field->validation_rule_count; ++i) {
    DataBindStatus status =
        data_bind_validation_plan_internal_validate_native_rule(
            plan->validation,
            field->validation_rule_start + i,
            field->data,
            source,
            error);
    if (status != DATA_BIND_OK) return status;
  }
  return DATA_BIND_OK;
}

const cserde_token *data_bind_message_plan_internal_default_token(
    const DataBindMessagePlan *plan,
    const char *field_name) {
  const DataBindMessageFieldPlan *field =
      message_field_const(plan, field_name);
  return field != NULL && field->has_default_token
             ? &field->default_token
             : NULL;
}

DataBindStatus data_bind_message_plan_validate_native(
    const DataBindMessagePlan *plan,
    const void *source,
    size_t source_bytes,
    DataBindError *error) {
  const unsigned char *base = (const unsigned char *)source;
  size_t i;

  if (error != NULL) *error = (DataBindError)DATA_BIND_ERROR_INIT;
  if (plan == NULL || source == NULL ||
      plan->native == NULL || plan->native->data == NULL ||
      plan->native->data->storage_type == NULL ||
      source_bytes < plan->native->data->storage_type->size)
    return DATA_BIND_ERR_INVALID_ARG;

  for (i = 0u; i < plan->field_count; ++i) {
    const DataBindMessageFieldPlan *field = &plan->fields[i];
    if (field->validation_rule_count == 0u) continue;

    if (field->has_presence) {
      const unsigned char *presence = base + field->presence_offset;
      if ((*presence & (unsigned char)(1u << field->presence_bit)) == 0u)
        continue;
    }
    if (field->has_null) {
      const unsigned char *nulls = base + field->null_offset;
      if ((*nulls & (unsigned char)(1u << field->null_bit)) != 0u)
        continue;
    }

    {
      DataBindStatus status =
          data_bind_message_plan_internal_validate_field(
              plan, field->name, base + field->native_offset, error);
      if (status != DATA_BIND_OK) return status;
    }
  }

  return DATA_BIND_OK;
}


static int message_decode_diag_valid(
    const DataBindMessageDecodeDiagnostic *diagnostic) {
  return diagnostic == NULL ||
         (diagnostic->size >= sizeof(*diagnostic) &&
          diagnostic->abi_version == DATA_BIND_MESSAGE_EXECUTOR_ABI_VERSION);
}

static void message_decode_diag_clear(
    DataBindMessageDecodeDiagnostic *diagnostic) {
  if (diagnostic == NULL) return;
  diagnostic->error = (DataBindError)DATA_BIND_ERROR_INIT;
  diagnostic->source_status = CSERDE_OK;
}

static DataBindStatus message_decode_fail(
    DataBindMessageDecodeDiagnostic *diagnostic,
    DataBindStatus status,
    cserde_status source_status,
    const char *path,
    const char *fmt,
    ...) {
  va_list ap;
  if (diagnostic == NULL) return status;
  diagnostic->error = (DataBindError)DATA_BIND_ERROR_INIT;
  diagnostic->source_status = source_status;
  diagnostic->error.code = status;
  if (path != NULL)
    snprintf(diagnostic->error.path, sizeof(diagnostic->error.path),
             "%s", path);
  va_start(ap, fmt);
  vsnprintf(diagnostic->error.message, sizeof(diagnostic->error.message),
            fmt, ap);
  va_end(ap);
  return status;
}

static DataBindStatus message_decode_native_fail(
    DataBindMessageDecodeDiagnostic *diagnostic,
    DataBindStatus status,
    const DataBindNativeDiagnostic *native,
    const char *fallback_path,
    const char *fallback_message) {
  if (diagnostic == NULL) return status;
  diagnostic->error = (DataBindError)DATA_BIND_ERROR_INIT;
  diagnostic->source_status =
      native != NULL ? native->source_status : CSERDE_OK;
  if (native != NULL)
    diagnostic->error = native->error;
  diagnostic->error.code = status;
  if (diagnostic->error.path[0] == '\0' && fallback_path != NULL)
    snprintf(diagnostic->error.path, sizeof(diagnostic->error.path),
             "%s", fallback_path);
  if (diagnostic->error.message[0] == '\0' && fallback_message != NULL)
    snprintf(diagnostic->error.message, sizeof(diagnostic->error.message),
             "%s", fallback_message);
  return status;
}

static DataBindStatus message_decode_reader_fail(
    DataBindMessageDecodeDiagnostic *diagnostic,
    cserde_status source_status,
    const char *path,
    const char *context) {
  DataBindStatus status;
  const char *reason;
  switch (source_status) {
  case CSERDE_LIMIT_EXCEEDED:
    status = DATA_BIND_ERR_LIMIT;
    reason = "CSerde source limit was exceeded";
    break;
  case CSERDE_VALUE_OUT_OF_RANGE:
    status = DATA_BIND_ERR_TYPE_MISMATCH;
    reason = "CSerde source value is outside the admitted range";
    break;
  case CSERDE_UNSUPPORTED:
    status = DATA_BIND_ERR_SCHEMA;
    reason = "CSerde source value is unsupported by this message contract";
    break;
  case CSERDE_SOURCE_ERROR:
    status = DATA_BIND_ERR_IO;
    reason = "CSerde source reported an I/O failure";
    break;
  case CSERDE_DONE:
  case CSERDE_UNEXPECTED_END:
  case CSERDE_INVALID_TOKEN:
    status = DATA_BIND_ERR_PARSE;
    reason = "CSerde source ended or produced an invalid message token";
    break;
  case CSERDE_INVALID_ARGUMENT:
  case CSERDE_INVALID_STATE:
  case CSERDE_CALLBACK_ERROR:
  case CSERDE_SINK_ERROR:
  default:
    status = DATA_BIND_ERR_RUNTIME;
    reason = "CSerde source entered an invalid runtime state";
    break;
  }
  return message_decode_fail(
      diagnostic, status, source_status, path, "%s: %s",
      context != NULL ? context : "Message decode failed", reason);
}

static int message_size_add(size_t left, size_t right, size_t *out) {
  if (out == NULL || right > SIZE_MAX - left) return 0;
  *out = left + right;
  return 1;
}

static size_t message_seen_bytes(const DataBindMessagePlan *plan) {
  if (plan == NULL || plan->field_count == 0u) return 0u;
  if (plan->field_count > SIZE_MAX - 7u) return SIZE_MAX;
  return (plan->field_count + 7u) / 8u;
}

static int message_seen_test(const unsigned char *seen, size_t index) {
  return seen != NULL &&
         (seen[index / 8u] &
          (unsigned char)(1u << (unsigned)(index % 8u))) != 0u;
}

static void message_seen_set(unsigned char *seen, size_t index) {
  seen[index / 8u] |=
      (unsigned char)(1u << (unsigned)(index % 8u));
}

static DataBindMessageFieldPlan *message_field_slice(
    const DataBindMessagePlan *plan,
    const unsigned char *data,
    size_t size,
    size_t *out_index) {
  size_t i;
  if (out_index != NULL) *out_index = SIZE_MAX;
  if (plan == NULL || (data == NULL && size != 0u)) return NULL;
  for (i = 0u; i < plan->field_count; ++i) {
    DataBindMessageFieldPlan *field =
        &((DataBindMessagePlan *)plan)->fields[i];
    size_t name_size =
        field->name != NULL ? strlen(field->name) : 0u;
    if (name_size == size &&
        (size == 0u || memcmp(field->name, data, size) == 0)) {
      if (out_index != NULL) *out_index = i;
      return field;
    }
  }
  return NULL;
}

static void message_state_clear_all(
    const DataBindMessagePlan *plan,
    void *destination) {
  unsigned char *base = (unsigned char *)destination;
  size_t i;
  if (plan == NULL || destination == NULL) return;
  for (i = 0u; i < plan->field_count; ++i) {
    const DataBindMessageFieldPlan *field = &plan->fields[i];
    if (field->has_presence)
      base[field->presence_offset] &=
          (unsigned char)~(1u << field->presence_bit);
    if (field->has_null)
      base[field->null_offset] &=
          (unsigned char)~(1u << field->null_bit);
  }
}

static void message_state_publish_value(
    const DataBindMessageFieldPlan *field,
    void *destination) {
  unsigned char *base = (unsigned char *)destination;
  if (field->has_presence)
    base[field->presence_offset] |=
        (unsigned char)(1u << field->presence_bit);
  if (field->has_null)
    base[field->null_offset] &=
        (unsigned char)~(1u << field->null_bit);
}

static void message_state_publish_null(
    const DataBindMessageFieldPlan *field,
    void *destination) {
  unsigned char *base = (unsigned char *)destination;
  if (field->has_presence)
    base[field->presence_offset] |=
        (unsigned char)(1u << field->presence_bit);
  if (field->has_null)
    base[field->null_offset] |=
        (unsigned char)(1u << field->null_bit);
}

static int message_workspace_partition(
    const DataBindNativeOptions *options,
    const DataBindMessageDecodeRequirements *requirements,
    unsigned char **out_seen,
    void **out_native_workspace,
    size_t *out_native_workspace_bytes) {
  uintptr_t address;
  size_t padding;
  size_t offset;
  size_t alignment;

  if (options == NULL || requirements == NULL ||
      out_seen == NULL || out_native_workspace == NULL ||
      out_native_workspace_bytes == NULL ||
      options->workspace == NULL)
    return 0;

  alignment = requirements->workspace_alignment;
  if (alignment == 0u) return 0;
  if (requirements->field_tracking_bytes > options->workspace_bytes)
    return 0;

  offset = requirements->field_tracking_bytes;
  address = (uintptr_t)((unsigned char *)options->workspace + offset);
  padding = (size_t)(address % alignment);
  if (padding != 0u) padding = alignment - padding;
  if (padding > options->workspace_bytes - offset)
    return 0;
  offset += padding;
  if (requirements->native_decode_bytes >
      options->workspace_bytes - offset)
    return 0;

  *out_seen = (unsigned char *)options->workspace;
  *out_native_workspace =
      (unsigned char *)options->workspace + offset;
  *out_native_workspace_bytes =
      options->workspace_bytes - offset;
  return 1;
}

DataBindStatus data_bind_message_plan_measure_decode(
    const DataBindMessagePlan *plan,
    const DataBindNativeOptions *native_options,
    DataBindMessageDecodeRequirements *requirements,
    DataBindMessageDecodeDiagnostic *diagnostic) {
  DataBindNativeRequirements native =
      DATA_BIND_NATIVE_REQUIREMENTS_INIT;
  DataBindNativeDiagnostic native_diagnostic =
      DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  DataBindMessageDecodeRequirements candidate =
      DATA_BIND_MESSAGE_DECODE_REQUIREMENTS_INIT;
  size_t seen;
  size_t total;
  size_t padding;
  DataBindStatus status;

  if (!message_decode_diag_valid(diagnostic))
    return DATA_BIND_ERR_INVALID_ARG;
  message_decode_diag_clear(diagnostic);

  if (plan == NULL || plan->native == NULL ||
      plan->native->data == NULL ||
      plan->native->data->storage_type == NULL ||
      native_options == NULL ||
      requirements == NULL ||
      requirements->size < sizeof(*requirements) ||
      requirements->abi_version != DATA_BIND_MESSAGE_EXECUTOR_ABI_VERSION)
    return message_decode_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG, CSERDE_OK,
        plan != NULL ? plan->type_name : NULL,
        "Invalid MessagePlan decode measurement arguments");

  status = data_bind_native_measure(
      native_options, plan->native->data,
      &native, &native_diagnostic);
  if (status != DATA_BIND_OK)
    return message_decode_native_fail(
        diagnostic, status, &native_diagnostic,
        plan->type_name, "Native message measurement failed");

  seen = message_seen_bytes(plan);
  if (seen == SIZE_MAX ||
      native.workspace_alignment == 0u) {
    return message_decode_fail(
        diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK,
        plan->type_name,
        "MessagePlan decode workspace size overflow");
  }

  padding = native.workspace_alignment - 1u;
  if (!message_size_add(seen, padding, &total) ||
      !message_size_add(total, native.decode_bytes, &total))
    return message_decode_fail(
        diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK,
        plan->type_name,
        "MessagePlan decode workspace size overflow");

  candidate.destination_bytes =
      plan->native->data->storage_type->size;
  candidate.field_tracking_bytes = seen;
  candidate.native_decode_bytes = native.decode_bytes;
  candidate.workspace_alignment = native.workspace_alignment;
  candidate.workspace_bytes = total;

  *requirements = candidate;
  message_decode_diag_clear(diagnostic);
  return DATA_BIND_OK;
}

static DataBindStatus message_decode_native_value(
    const DataBindMessagePlan *plan,
    const DataBindMessageFieldPlan *field,
    const DataBindNativeOptions *child_options,
    cserde_reader *reader,
    const cserde_token *first_token,
    void *destination,
    size_t max_buffer_bytes,
    DataBindNativeDecodeUsage *usage,
    DataBindMessageDecodeDiagnostic *diagnostic) {
  DataBindNativeDiagnostic native =
      DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  DataBindStatus status;
  void *field_destination;

  if (plan == NULL || field == NULL ||
      child_options == NULL || reader == NULL ||
      first_token == NULL || destination == NULL || usage == NULL)
    return message_decode_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG, CSERDE_OK,
        field != NULL ? field->name : NULL,
        "Invalid MessagePlan native field decode arguments");

  field_destination =
      (unsigned char *)destination + field->native_offset;
  status = data_bind_native_decode_from_token_internal(
      child_options, field->data, reader, first_token,
      field_destination, field->data->storage_type->size,
      max_buffer_bytes, usage, &native);
  if (status != DATA_BIND_OK)
    return message_decode_native_fail(
        diagnostic, status, &native, field->name,
        "Native MessagePlan field decode failed");
  return DATA_BIND_OK;
}

static DataBindStatus message_admit_logical_item(
    const DataBindNativeOptions *options,
    DataBindNativeDecodeUsage *usage,
    const char *path,
    DataBindMessageDecodeDiagnostic *diagnostic) {
  if (options == NULL || usage == NULL)
    return DATA_BIND_ERR_INVALID_ARG;
  if (usage->items >= options->max_items)
    return message_decode_fail(
        diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK, path,
        "Decoded message item count exceeds configured limit");
  ++usage->items;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_message_plan_clear_native(
    const DataBindMessagePlan *plan,
    const DataBindNativeOptions *native_options,
    void *destination,
    size_t destination_bytes,
    DataBindMessageDecodeDiagnostic *diagnostic) {
  DataBindNativeDiagnostic native =
      DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  DataBindStatus status;

  if (!message_decode_diag_valid(diagnostic))
    return DATA_BIND_ERR_INVALID_ARG;
  message_decode_diag_clear(diagnostic);

  if (plan == NULL || plan->native == NULL ||
      plan->native->data == NULL ||
      native_options == NULL || destination == NULL)
    return message_decode_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG, CSERDE_OK,
        plan != NULL ? plan->type_name : NULL,
        "Invalid MessagePlan clear arguments");

  status = data_bind_native_clear(
      native_options, plan->native->data,
      destination, destination_bytes, &native);
  message_state_clear_all(plan, destination);
  if (status != DATA_BIND_OK)
    return message_decode_native_fail(
        diagnostic, status, &native, plan->type_name,
        "MessagePlan native clear failed");

  message_decode_diag_clear(diagnostic);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_message_plan_decode(
    const DataBindMessagePlan *plan,
    const DataBindNativeOptions *native_options,
    cserde_reader *reader,
    void *destination,
    size_t destination_bytes,
    size_t max_buffer_bytes,
    DataBindMessageDecodeDiagnostic *diagnostic) {
  DataBindMessageDecodeRequirements requirements =
      DATA_BIND_MESSAGE_DECODE_REQUIREMENTS_INIT;
  DataBindNativeDiagnostic native =
      DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  DataBindNativeOptions child_options;
  DataBindNativeDecodeUsage usage = {1u, 0u};
  unsigned char *seen = NULL;
  void *child_workspace = NULL;
  size_t child_workspace_bytes = 0u;
  cserde_token token = {0};
  DataBindError validation_error = DATA_BIND_ERROR_INIT;
  DataBindStatus status;
  int initialized = 0;
  size_t i;

  if (!message_decode_diag_valid(diagnostic))
    return DATA_BIND_ERR_INVALID_ARG;
  message_decode_diag_clear(diagnostic);

  if (plan == NULL || native_options == NULL ||
      reader == NULL || destination == NULL ||
      plan->native == NULL || plan->native->data == NULL ||
      plan->native->data->storage_type == NULL)
    return message_decode_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG, CSERDE_OK,
        plan != NULL ? plan->type_name : NULL,
        "Invalid MessagePlan decode arguments");

  status = data_bind_message_plan_measure_decode(
      plan, native_options, &requirements, diagnostic);
  if (status != DATA_BIND_OK) return status;

  if (destination_bytes < requirements.destination_bytes)
    return message_decode_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG, CSERDE_OK,
        plan->type_name, "MessagePlan destination storage is too small");
  if (native_options->workspace == NULL ||
      native_options->workspace_bytes < requirements.workspace_bytes)
    return message_decode_fail(
        diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK,
        plan->type_name, "MessagePlan workspace is too small");
  if (native_options->max_items == 0u)
    return message_decode_fail(
        diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK,
        plan->type_name, "MessagePlan item budget must be nonzero");

  status = data_bind_native_init(
      native_options, plan->native->data,
      destination, destination_bytes, &native);
  if (status != DATA_BIND_OK)
    return message_decode_native_fail(
        diagnostic, status, &native, plan->type_name,
        "MessagePlan native initialization failed");
  initialized = 1;
  message_state_clear_all(plan, destination);

  if (!message_workspace_partition(
          native_options, &requirements, &seen,
          &child_workspace, &child_workspace_bytes)) {
    status = message_decode_fail(
        diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK,
        plan->type_name, "MessagePlan workspace partition failed");
    goto fail;
  }
  if (requirements.field_tracking_bytes != 0u)
    memset(seen, 0, requirements.field_tracking_bytes);

  child_options = *native_options;
  child_options.workspace = child_workspace;
  child_options.workspace_bytes = child_workspace_bytes;
  child_options.max_depth =
      native_options->max_depth != 0u
          ? native_options->max_depth - 1u
          : 0u;

  {
    cserde_status reader_status =
        cserde_reader_next(reader, &token);
    if (reader_status != CSERDE_OK) {
      status = message_decode_reader_fail(
          diagnostic, reader_status, plan->type_name,
          "Could not read MessagePlan object opener");
      goto fail;
    }
  }
  if (token.kind != CSERDE_MAP_BEGIN) {
    status = message_decode_fail(
        diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, CSERDE_OK,
        plan->type_name,
        "MessagePlan requires a CSerde map/object root");
    goto fail;
  }

  for (;;) {
    DataBindMessageFieldPlan *field;
    size_t field_index = SIZE_MAX;
    cserde_status reader_status =
        cserde_reader_next(reader, &token);

    if (reader_status != CSERDE_OK) {
      status = message_decode_reader_fail(
          diagnostic, reader_status, plan->type_name,
          "Could not read MessagePlan field key");
      goto fail;
    }
    if (token.kind == CSERDE_MAP_END) break;
    if (token.kind != CSERDE_STRING) {
      status = message_decode_fail(
          diagnostic, DATA_BIND_ERR_PARSE, CSERDE_OK,
          plan->type_name,
          "MessagePlan object field key must be a string");
      goto fail;
    }

    field = message_field_slice(
        plan, token.value.slice.data,
        token.value.slice.size, &field_index);
    if (field == NULL) {
      status = message_decode_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK,
          plan->type_name,
          "MessagePlan input contains an unknown field");
      goto fail;
    }
    if (message_seen_test(seen, field_index)) {
      status = message_decode_fail(
          diagnostic, DATA_BIND_ERR_PARSE, CSERDE_OK,
          field->name,
          "MessagePlan input repeats field '%s'", field->name);
      goto fail;
    }

    reader_status = cserde_reader_next(reader, &token);
    if (reader_status != CSERDE_OK) {
      status = message_decode_reader_fail(
          diagnostic, reader_status, field->name,
          "Could not read MessagePlan field value");
      goto fail;
    }

    if (native_options->max_depth <= 1u) {
      status = message_decode_fail(
          diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK,
          field->name,
          "MessagePlan field exceeds configured depth limit");
      goto fail;
    }

    if (token.kind == CSERDE_NULL) {
      if (!field->nullable || !field->has_null) {
        status = message_decode_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, CSERDE_OK,
            field->name,
            "Explicit NULL is not admitted by the DataBind contract");
        goto fail;
      }
      status = message_admit_logical_item(
          native_options, &usage, field->name, diagnostic);
      if (status != DATA_BIND_OK) goto fail;
      message_state_publish_null(field, destination);
    } else {
      status = message_decode_native_value(
          plan, field, &child_options, reader, &token,
          destination, max_buffer_bytes, &usage, diagnostic);
      if (status != DATA_BIND_OK) goto fail;
      message_state_publish_value(field, destination);
    }

    message_seen_set(seen, field_index);
  }

  for (i = 0u; i < plan->field_count; ++i) {
    DataBindMessageFieldPlan *field = &plan->fields[i];
    if (message_seen_test(seen, i)) continue;

    if (field->has_default_token) {
      if (native_options->max_depth <= 1u) {
        status = message_decode_fail(
            diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK,
            field->name,
            "MessagePlan default exceeds configured depth limit");
        goto fail;
      }
      status = message_decode_native_value(
          plan, field, &child_options, reader,
          &field->default_token, destination,
          max_buffer_bytes, &usage, diagnostic);
      if (status != DATA_BIND_OK) goto fail;
      message_state_publish_value(field, destination);
      message_seen_set(seen, i);
    } else if (field->optional) {
      /* ABSENT: semantic-zero native value with state bits already clear. */
      continue;
    } else {
      status = message_decode_fail(
          diagnostic, DATA_BIND_ERR_TYPE_NOT_FOUND, CSERDE_OK,
          field->name,
          "Required MessagePlan field '%s' is absent", field->name);
      goto fail;
    }
  }

  status = data_bind_message_plan_validate_native(
      plan, destination, destination_bytes, &validation_error);
  if (status != DATA_BIND_OK) {
    if (diagnostic != NULL) {
      diagnostic->error = validation_error;
      diagnostic->error.code = status;
      diagnostic->source_status = CSERDE_OK;
    }
    goto fail;
  }

  message_decode_diag_clear(diagnostic);
  return DATA_BIND_OK;

fail:
  if (initialized) {
    DataBindNativeDiagnostic cleanup =
        DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    DataBindStatus cleanup_status =
        data_bind_native_clear(
            native_options, plan->native->data,
            destination, destination_bytes, &cleanup);
    message_state_clear_all(plan, destination);
    if (cleanup_status != DATA_BIND_OK)
      return message_decode_native_fail(
          diagnostic, DATA_BIND_ERR_RUNTIME, &cleanup,
          plan->type_name,
          "MessagePlan rollback did not restore semantic-zero");
  }
  return status;
}
