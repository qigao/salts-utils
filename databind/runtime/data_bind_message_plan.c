#include "data_bind_message_plan.h"
#include "data_bind_message_plan_internal.h"
#include "data_bind_validation_plan.h"
#include "data_bind_validation_plan_internal.h"

#include <cmeta/type_traits.h>

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
  }

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
