#include "data_bind_message_plan.h"
#include "data_bind_internal.h"
#include "data_bind_message_plan_internal.h"
#include "data_bind_native_internal.h"
#include "data_bind_validation_plan.h"
#include "data_bind_validation_plan_internal.h"

#include <cmeta/type_traits.h>

#include <errno.h>
#include <limits.h>
#include <math.h>
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

typedef enum DataBindMessagePlanMode {
  DATA_BIND_MESSAGE_PLAN_NATIVE = 1,
  DATA_BIND_MESSAGE_PLAN_OBJECT = 2
} DataBindMessagePlanMode;

struct DataBindMessagePlan {
  char *type_name;
  DataBindMessagePlanMode mode;
  const DataBindNativeTypeBinding *native;
  const cmeta_data_desc *object_data;
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

static const cmeta_data_struct_shape *message_object_struct_shape(
    const cmeta_data_desc *data) {
  if (!cmeta_data_desc_valid(data) || data->kind != CMETA_DATA_STRUCT ||
      data->shape == NULL)
    return NULL;
  return (const cmeta_data_struct_shape *)data->shape;
}

static DataBindStatus message_cmeta_status(cmeta_status status) {
  switch (status) {
  case CMETA_OK: return DATA_BIND_OK;
  case CMETA_INVALID_ARGUMENT: return DATA_BIND_ERR_INVALID_ARG;
  case CMETA_TYPE_MISMATCH: return DATA_BIND_ERR_TYPE_MISMATCH;
  case CMETA_OUT_OF_MEMORY: return DATA_BIND_ERR_OOM;
  case CMETA_CAPACITY_EXCEEDED: return DATA_BIND_ERR_LIMIT;
  case CMETA_TRAIT_MISSING:
  case CMETA_CALLBACK_ERROR:
  default:
    return DATA_BIND_ERR_RUNTIME;
  }
}

static int message_object_state_provider_valid(
    const DataBindMessageObjectStateProvider *provider) {
  return provider != NULL &&
         provider->size >= sizeof(*provider) &&
         provider->abi_version == DATA_BIND_MESSAGE_PLAN_ABI_VERSION &&
         provider->get_state != NULL &&
         provider->set_state != NULL;
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

enum { DATA_BIND_MESSAGE_PLAN_NATIVE_GRAPH_MAX_DEPTH = 32u };

static int message_schema_record_matches_native(
    DataBind *codec, const char *type_name,
    const cmeta_data_desc *native_data, unsigned depth);

static int message_logical_buffer_matches_native(
    const DataBindSchemaField *schema_field,
    const cmeta_data_desc *native_data) {
  const cmeta_data_buffer_ops *ops;
  const cmeta_data_buffer_shape *shape;

  if (schema_field == NULL || native_data == NULL ||
      !schema_field->has_cmeta_kind ||
      (schema_field->cmeta_kind != CMETA_DATA_STRING &&
       schema_field->cmeta_kind != CMETA_DATA_BYTES) ||
      native_data->kind != schema_field->cmeta_kind ||
      native_data->storage_type == NULL ||
      !cmeta_data_value_move_supported(native_data))
    return 0;

  ops = cmeta_data_buffer_ops_of(native_data);
  shape = (const cmeta_data_buffer_shape *)native_data->shape;
  return ops != NULL && shape != NULL &&
         shape->ownership == CMETA_DATA_BUFFER_OWNED &&
         ops->ownership == CMETA_DATA_BUFFER_OWNED &&
         ops->init_zero != NULL &&
         ops->restore_zero != NULL &&
         ops->move != NULL;
}

static int message_logical_sequence_matches_native(
    DataBind *codec, const DataBindSchemaField *schema_field,
    const cmeta_data_desc *native_data, unsigned depth) {
  const cmeta_data_collection_ops *ops;
  const cmeta_data_desc *element;

  if (codec == NULL || schema_field == NULL || native_data == NULL ||
      depth >= DATA_BIND_MESSAGE_PLAN_NATIVE_GRAPH_MAX_DEPTH ||
      !schema_field->is_collection ||
      schema_field->collection_kind == NULL ||
      strcmp(schema_field->collection_kind, "list") != 0 ||
      schema_field->inner_type == NULL ||
      schema_field->inner_type[0] == '\0' ||
      !schema_field->has_cmeta_kind ||
      schema_field->cmeta_kind != CMETA_DATA_SEQUENCE ||
      native_data->kind != CMETA_DATA_SEQUENCE ||
      native_data->storage_type == NULL ||
      !cmeta_data_value_move_supported(native_data) ||
      !cmeta_data_value_copy_supported(native_data))
    return 0;

  ops = cmeta_data_collection_ops_of(native_data);
  element = cmeta_data_collection_element_data(native_data);
  if (ops == NULL || element == NULL ||
      cmeta_data_construct_ops_of(native_data) == NULL ||
      ops->collector == NULL || ops->borrow == NULL ||
      !cmeta_data_desc_valid(element) ||
      element->kind != CMETA_DATA_STRUCT)
    return 0;

  return message_schema_record_matches_native(
      codec, schema_field->inner_type, element, depth + 1u);
}

static int message_logical_record_map_matches_native(
    DataBind *codec, const DataBindSchemaField *schema_field,
    const cmeta_data_desc *native_data, unsigned depth) {
  DataBindSchemaField key_field = DATA_BIND_SCHEMA_FIELD_INIT;
  const cmeta_data_map_ops *ops;
  const cmeta_data_desc *key;
  const cmeta_data_desc *value;

  if (codec == NULL || schema_field == NULL || native_data == NULL ||
      depth >= DATA_BIND_MESSAGE_PLAN_NATIVE_GRAPH_MAX_DEPTH ||
      !schema_field->is_collection || !schema_field->is_map ||
      schema_field->collection_kind == NULL ||
      strcmp(schema_field->collection_kind, "map") != 0 ||
      schema_field->key_type == NULL ||
      strcmp(schema_field->key_type, "string") != 0 ||
      schema_field->value_type == NULL ||
      schema_field->value_type[0] == '\0' ||
      !schema_field->has_cmeta_kind ||
      schema_field->cmeta_kind != CMETA_DATA_MAP ||
      native_data->kind != CMETA_DATA_MAP ||
      native_data->storage_type == NULL ||
      !cmeta_data_value_move_supported(native_data) ||
      !cmeta_data_value_copy_supported(native_data))
    return 0;

  ops = cmeta_data_map_ops_of(native_data);
  key = cmeta_data_map_key_data(native_data);
  value = cmeta_data_map_value_data(native_data);
  if (ops == NULL || key == NULL || value == NULL ||
      cmeta_data_construct_ops_of(native_data) == NULL ||
      ops->collector == NULL || ops->accept == NULL ||
      ops->borrow == NULL || ops->borrow->size == NULL ||
      ops->borrow->next == NULL ||
      !cmeta_data_desc_valid(key) ||
      !cmeta_data_desc_valid(value) ||
      value->kind != CMETA_DATA_STRUCT)
    return 0;

  /*
   * DataBind map keys are canonically string-valued. Reuse the same owned
   * buffer admission as a normal string field instead of comparing a concrete
   * tstr address or recovering erased native key storage.
   */
  key_field.has_cmeta_kind = 1;
  key_field.cmeta_kind = CMETA_DATA_STRING;
  if (!message_logical_buffer_matches_native(&key_field, key))
    return 0;

  return message_schema_record_matches_native(
      codec, schema_field->value_type, value, depth + 1u);
}

static int message_schema_field_matches_native(
    DataBind *codec, const char *type_name, size_t field_index,
    const DataBindSchemaField *schema_field,
    const cmeta_data_desc *native_data, unsigned depth) {
  const cmeta_data_desc *schema_data = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;

  if (codec == NULL || type_name == NULL || schema_field == NULL ||
      native_data == NULL ||
      depth >= DATA_BIND_MESSAGE_PLAN_NATIVE_GRAPH_MAX_DEPTH)
    return 0;

  if (schema_field->is_collection) {
    if (schema_field->collection_kind != NULL &&
        strcmp(schema_field->collection_kind, "list") == 0)
      return message_logical_sequence_matches_native(
          codec, schema_field, native_data, depth);
    if (schema_field->collection_kind != NULL &&
        strcmp(schema_field->collection_kind, "map") == 0)
      return message_logical_record_map_matches_native(
          codec, schema_field, native_data, depth);
    return 0;
  }

  schema_data = schema_field->cmeta_data;
  if (schema_data == NULL)
    (void)data_bind_schema_field_cmeta_data(
        codec, type_name, field_index, &schema_data, &error);

  if (schema_data != NULL)
    return message_data_semantically_equal(schema_data, native_data);

  return message_logical_buffer_matches_native(schema_field, native_data);
}

static int message_schema_record_matches_native(
    DataBind *codec, const char *type_name,
    const cmeta_data_desc *native_data, unsigned depth) {
  const cmeta_data_struct_shape *shape =
      message_object_struct_shape(native_data);
  DataBindSchemaType schema_type = DATA_BIND_SCHEMA_TYPE_INIT;
  size_t i;

  if (codec == NULL || type_name == NULL || type_name[0] == '\0' ||
      shape == NULL ||
      depth >= DATA_BIND_MESSAGE_PLAN_NATIVE_GRAPH_MAX_DEPTH ||
      !data_bind_schema_find_type(codec, type_name, &schema_type) ||
      schema_type.field_count !=
          data_bind_schema_field_count(codec, type_name) ||
      shape->field_count != schema_type.field_count)
    return 0;

  for (i = 0u; i < schema_type.field_count; ++i) {
    DataBindSchemaField schema_field = DATA_BIND_SCHEMA_FIELD_INIT;
    const cmeta_data_field_desc *native_field = NULL;
    size_t j;

    if (!data_bind_schema_field_at(codec, type_name, i, &schema_field))
      return 0;
    if (schema_field.is_optional || schema_field.is_nullable)
      return 0;

    for (j = 0u; j < shape->field_count; ++j) {
      if (shape->fields[j].name != NULL &&
          schema_field.name != NULL &&
          strcmp(shape->fields[j].name, schema_field.name) == 0) {
        native_field = &shape->fields[j];
        break;
      }
    }
    if (native_field == NULL || native_field->value == NULL ||
        !message_schema_field_matches_native(
            codec, type_name, i, &schema_field,
            native_field->value, depth + 1u))
      return 0;
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
    const DataBindNativeStateBinding *presence;
    const DataBindNativeStateBinding *null_state;
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

    if (!message_schema_field_matches_native(
            codec, type_name, i, &schema_field,
            native_field->value, 0u))
      return message_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, schema_field.name,
          "Native CMeta field '%s.%s' does not match admitted DataBind IDL semantics",
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

static DataBindStatus message_compile_object_fields(
    DataBind *codec,
    const char *type_name,
    const cmeta_data_desc *object_data,
    DataBindMessagePlan *plan,
    DataBindMessagePlanDiagnostic *diagnostic) {
  const cmeta_data_struct_shape *shape =
      message_object_struct_shape(object_data);
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
        "Object field count does not match DataBind IDL type '%s'", type_name);

  plan->field_count = schema_type.field_count;
  if (plan->field_count != 0u) {
    plan->fields = (DataBindMessageFieldPlan *)calloc(
        plan->field_count, sizeof(*plan->fields));
    if (plan->fields == NULL)
      return message_fail(
          diagnostic, DATA_BIND_ERR_OOM, type_name,
          "Could not allocate provider-backed MessagePlan fields");
  }

  for (i = 0u; i < plan->field_count; ++i) {
    DataBindSchemaField schema_field = DATA_BIND_SCHEMA_FIELD_INIT;
    const cmeta_data_field_desc *object_field;
    DataBindMessageFieldPlan *field = &plan->fields[i];

    if (!data_bind_schema_field_at(codec, type_name, i, &schema_field))
      return message_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, type_name,
          "Could not reflect field %zu of '%s'", i, type_name);

    object_field = NULL;
    {
      size_t j;
      for (j = 0u; j < shape->field_count; ++j) {
        if (shape->fields[j].name != NULL && schema_field.name != NULL &&
            strcmp(shape->fields[j].name, schema_field.name) == 0) {
          object_field = &shape->fields[j];
          break;
        }
      }
    }
    if (object_field == NULL || object_field->value == NULL)
      return message_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, schema_field.name,
          "Object type '%s' is missing field '%s'", type_name,
          schema_field.name != NULL ? schema_field.name : "");

    if (!message_schema_field_matches_native(
            codec, type_name, i, &schema_field,
            object_field->value, 0u))
      return message_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, schema_field.name,
          "Object CMeta field '%s.%s' does not match admitted DataBind IDL semantics",
          type_name, schema_field.name != NULL ? schema_field.name : "");

    field->name = message_strdup(schema_field.name);
    field->data = object_field->value;
    field->native_offset = CMETA_FIELD_DYNAMIC_OFFSET;
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
          "Could not copy provider-backed MessagePlan field metadata");

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
      if (info.field_kind == CMETA_DATA_STRING ||
          info.field_kind == CMETA_DATA_BYTES) {
        buffer_ops = cmeta_data_buffer_ops_of(field->data);
        if (buffer_ops == NULL || buffer_ops->read == NULL)
          return message_fail(
              diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name,
              "Native @Size requires a canonical readable buffer provider");
      } else if (info.field_kind == CMETA_DATA_SEQUENCE ||
                 info.field_kind == CMETA_DATA_SET) {
        const cmeta_data_collection_ops *collection_ops =
            cmeta_data_collection_ops_of(field->data);
        if (collection_ops == NULL || collection_ops->borrow == NULL ||
            collection_ops->borrow->size == NULL)
          return message_fail(
              diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name,
              "Native collection @Size requires canonical borrow-size metadata");
      } else if (info.field_kind == CMETA_DATA_MAP) {
        const cmeta_data_map_ops *map_ops =
            cmeta_data_map_ops_of(field->data);
        if (map_ops == NULL || map_ops->borrow == NULL ||
            map_ops->borrow->size == NULL)
          return message_fail(
              diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name,
              "Native map @Size requires canonical borrow-size metadata");
      } else {
        return message_fail(
            diagnostic, DATA_BIND_ERR_SCHEMA, info.field_name,
            "Native @Size requires canonical buffer or collection storage");
      }
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
  plan->mode = DATA_BIND_MESSAGE_PLAN_NATIVE;
  plan->native = native;
  plan->object_data = NULL;
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

DataBindStatus data_bind_message_plan_compile_object(
    DataBind *codec,
    const char *type_name,
    const cmeta_data_desc *object_data,
    DataBindMessagePlan **out_plan,
    DataBindMessagePlanDiagnostic *diagnostic) {
  DataBindMessagePlan *plan = NULL;
  DataBindStatus status;

  if (!message_diag_header_valid(diagnostic))
    return DATA_BIND_ERR_INVALID_ARG;
  message_diag_clear(diagnostic);
  if (out_plan != NULL) *out_plan = NULL;

  if (codec == NULL || type_name == NULL || type_name[0] == '\0' ||
      out_plan == NULL || !cmeta_data_desc_valid(object_data) ||
      object_data->kind != CMETA_DATA_STRUCT || object_data->shape == NULL)
    return message_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, type_name,
        "Invalid provider-backed object surface for DataBind IDL type '%s'",
        type_name != NULL ? type_name : "");

  plan = (DataBindMessagePlan *)calloc(1u, sizeof(*plan));
  if (plan == NULL)
    return message_fail(
        diagnostic, DATA_BIND_ERR_OOM, type_name,
        "Could not allocate provider-backed DataBind MessagePlan");

  plan->type_name = message_strdup(type_name);
  plan->mode = DATA_BIND_MESSAGE_PLAN_OBJECT;
  plan->native = NULL;
  plan->object_data = object_data;
  if (plan->type_name == NULL) {
    status = message_fail(
        diagnostic, DATA_BIND_ERR_OOM, type_name,
        "Could not copy provider-backed MessagePlan identity");
    goto object_fail;
  }

  status = message_compile_object_fields(
      codec, type_name, object_data, plan, diagnostic);
  if (status != DATA_BIND_OK) goto object_fail;

  status = message_compile_validation(
      codec, type_name, plan, diagnostic);
  if (status != DATA_BIND_OK) goto object_fail;

  message_diag_clear(diagnostic);
  *out_plan = plan;
  return DATA_BIND_OK;

object_fail:
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

const cmeta_data_desc *data_bind_message_plan_object_data(
    const DataBindMessagePlan *plan) {
  return plan != NULL && plan->mode == DATA_BIND_MESSAGE_PLAN_OBJECT
             ? plan->object_data
             : NULL;
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
  if (plan->validation == NULL)
    return field->validation_rule_count == 0u
               ? DATA_BIND_OK
               : DATA_BIND_ERR_RUNTIME;

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
  return data_bind_validation_plan_internal_validate_native_child(
      plan->validation, field_name, field->data, source, error);
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


typedef struct MessagePrefixedReaderContext {
  cserde_reader *source;
  cserde_token first;
  int emitted_first;
} MessagePrefixedReaderContext;

static cserde_status message_prefixed_reader_next(
    void *context, cserde_token *out) {
  MessagePrefixedReaderContext *state =
      (MessagePrefixedReaderContext *)context;
  if (state == NULL || out == NULL || state->source == NULL)
    return CSERDE_INVALID_ARGUMENT;
  if (!state->emitted_first) {
    *out = state->first;
    state->emitted_first = 1;
    return CSERDE_OK;
  }
  return cserde_reader_next(state->source, out);
}

static const cserde_reader_ops MESSAGE_PREFIXED_READER_OPS = {
    offsetof(cserde_reader_ops, next) + sizeof(cserde_reader_next_fn),
    CSERDE_READER_OPS_ABI_VERSION,
    message_prefixed_reader_next};

typedef struct MessageSingleReaderContext {
  const cserde_token *token;
  int emitted;
} MessageSingleReaderContext;

static cserde_status message_single_reader_next(
    void *context, cserde_token *out) {
  MessageSingleReaderContext *state =
      (MessageSingleReaderContext *)context;
  if (state == NULL || out == NULL || state->token == NULL)
    return CSERDE_INVALID_ARGUMENT;
  if (state->emitted) return CSERDE_DONE;
  *out = *state->token;
  state->emitted = 1;
  return CSERDE_OK;
}

static const cserde_reader_ops MESSAGE_SINGLE_READER_OPS = {
    offsetof(cserde_reader_ops, next) + sizeof(cserde_reader_next_fn),
    CSERDE_READER_OPS_ABI_VERSION,
    message_single_reader_next};

static size_t message_bitmap_bytes(size_t field_count) {
  return field_count / 8u + (field_count % 8u != 0u ? 1u : 0u);
}

static int message_field_seen(const unsigned char *bitmap, size_t index) {
  return bitmap != NULL &&
         (bitmap[index / 8u] &
          (unsigned char)(1u << (index % 8u))) != 0u;
}

static void message_mark_field_seen(unsigned char *bitmap, size_t index) {
  bitmap[index / 8u] |=
      (unsigned char)(1u << (index % 8u));
}

static const DataBindMessageFieldPlan *message_field_slice(
    const DataBindMessagePlan *plan,
    const cserde_slice *name,
    size_t *out_index) {
  size_t i;
  if (plan == NULL || name == NULL ||
      (!cserde_view_lifetime_valid(name->lifetime)) ||
      (name->size != 0u && name->data == NULL))
    return NULL;

  for (i = 0u; i < plan->field_count; ++i) {
    const DataBindMessageFieldPlan *field = &plan->fields[i];
    size_t length;
    if (field->name == NULL) continue;
    length = strlen(field->name);
    if (length == name->size &&
        (length == 0u ||
         memcmp(field->name, name->data, length) == 0)) {
      if (out_index != NULL) *out_index = i;
      return field;
    }
  }
  return NULL;
}

static DataBindStatus message_reader_failure(
    DataBindMessagePlanDiagnostic *diagnostic,
    cserde_status source_status,
    const char *field,
    const char *context) {
  DataBindStatus status;
  const char *message;

  switch (source_status) {
  case CSERDE_DONE:
  case CSERDE_UNEXPECTED_END:
    status = DATA_BIND_ERR_PARSE;
    message = "Reader ended before one complete DataBind message";
    break;
  case CSERDE_INVALID_TOKEN:
    status = DATA_BIND_ERR_PARSE;
    message = "Reader produced an invalid CSerde token";
    break;
  case CSERDE_VALUE_OUT_OF_RANGE:
    status = DATA_BIND_ERR_TYPE_MISMATCH;
    message = "Reader value is outside the admitted source range";
    break;
  case CSERDE_LIMIT_EXCEEDED:
    status = DATA_BIND_ERR_LIMIT;
    message = "Reader source limit was exceeded";
    break;
  case CSERDE_UNSUPPORTED:
    status = DATA_BIND_ERR_SCHEMA;
    message = "Reader cannot represent the selected DataBind message";
    break;
  case CSERDE_SOURCE_ERROR:
  case CSERDE_SINK_ERROR:
    status = DATA_BIND_ERR_IO;
    message = "Reader source reported an I/O failure";
    break;
  case CSERDE_INVALID_ARGUMENT:
  case CSERDE_INVALID_STATE:
  case CSERDE_CALLBACK_ERROR:
  default:
    status = DATA_BIND_ERR_RUNTIME;
    message = "Reader entered an invalid runtime state";
    break;
  }

  return message_fail(
      diagnostic, status, field, "%s%s%s",
      context != NULL ? context : "",
      context != NULL && context[0] != '\0' ? ": " : "",
      message);
}

static DataBindStatus message_native_failure(
    DataBindMessagePlanDiagnostic *diagnostic,
    DataBindStatus status,
    const char *field,
    const DataBindNativeDiagnostic *native,
    const char *fallback) {
  return message_fail(
      diagnostic, status, field, "%s",
      native != NULL && native->error.message[0] != '\0'
          ? native->error.message
          : fallback);
}

static void message_set_presence(
    unsigned char *base,
    const DataBindMessageFieldPlan *field,
    int present) {
  unsigned char *state;
  if (base == NULL || field == NULL || !field->has_presence) return;
  state = base + field->presence_offset;
  if (present)
    *state |= (unsigned char)(1u << field->presence_bit);
  else
    *state &= (unsigned char)~(1u << field->presence_bit);
}

static void message_set_null(
    unsigned char *base,
    const DataBindMessageFieldPlan *field,
    int is_null) {
  unsigned char *state;
  if (base == NULL || field == NULL || !field->has_null) return;
  state = base + field->null_offset;
  if (is_null)
    *state |= (unsigned char)(1u << field->null_bit);
  else
    *state &= (unsigned char)~(1u << field->null_bit);
}

static DataBindStatus message_native_field_state(
    const DataBindMessageFieldPlan *field,
    const unsigned char *base,
    size_t source_bytes,
    DataBindMessageObjectFieldState *out_state,
    DataBindMessagePlanDiagnostic *diagnostic) {
  int present = 1;
  int is_null = 0;

  if (field == NULL || base == NULL || out_state == NULL)
    return message_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG,
        field != NULL ? field->name : NULL,
        "Invalid native MessagePlan state lookup");

  if (field->optional) {
    if (!field->has_presence || field->presence_bit >= 8u ||
        field->presence_offset >= source_bytes)
      return message_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, field->name,
          "Optional native field has no valid presence binding");
    present =
        (base[field->presence_offset] &
         (unsigned char)(1u << field->presence_bit)) != 0u;
    if (!present) {
      *out_state = DATA_BIND_MESSAGE_OBJECT_ABSENT;
      return DATA_BIND_OK;
    }
  }

  if (field->nullable) {
    if (!field->has_null || field->null_bit >= 8u ||
        field->null_offset >= source_bytes)
      return message_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, field->name,
          "Nullable native field has no valid null binding");
    is_null =
        (base[field->null_offset] &
         (unsigned char)(1u << field->null_bit)) != 0u;
  }

  *out_state = is_null
                   ? DATA_BIND_MESSAGE_OBJECT_NULL
                   : DATA_BIND_MESSAGE_OBJECT_VALUE;
  return DATA_BIND_OK;
}

static const char *message_validation_diagnostic_field(
    const DataBindMessagePlan *plan,
    const DataBindMessageFieldPlan *field,
    const DataBindError *validation) {
  char direct_path[sizeof(((DataBindError *)0)->path)];
  int written;

  if (field == NULL) return validation != NULL ? validation->path : NULL;
  if (validation == NULL || validation->path[0] == '\0')
    return field->name;
  if (plan == NULL || plan->type_name == NULL || field->name == NULL)
    return validation->path;

  written = snprintf(
      direct_path, sizeof(direct_path), "%s.%s",
      plan->type_name, field->name);
  if (written >= 0 && (size_t)written < sizeof(direct_path) &&
      strcmp(direct_path, validation->path) == 0)
    return field->name;
  return validation->path;
}

static DataBindStatus message_decode_value(
    const DataBindMessagePlan *plan,
    const DataBindMessageFieldPlan *field,
    const DataBindNativeOptions *native_options,
    DataBindNativeDecodeUsage *usage,
    cserde_reader *reader,
    unsigned char *destination,
    DataBindMessagePlanDiagnostic *diagnostic) {
  DataBindNativeDiagnostic native =
      DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  DataBindNativeOptions field_options;
  DataBindNativeDecodeUsage field_usage = {0u, 0u};
  DataBindError validation = DATA_BIND_ERROR_INIT;
  void *field_destination;
  DataBindStatus status;

  if (plan == NULL || field == NULL || native_options == NULL ||
      usage == NULL || reader == NULL || destination == NULL ||
      field->data == NULL || field->data->storage_type == NULL)
    return message_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG,
        field != NULL ? field->name : NULL,
        "Invalid MessagePlan field decode arguments");

  if (usage->items > native_options->max_items ||
      usage->owned_bytes > native_options->max_owned_bytes)
    return message_fail(
        diagnostic, DATA_BIND_ERR_LIMIT, field->name,
        "Whole-message native budget accounting is inconsistent");

  field_options = *native_options;
  field_options.max_items = native_options->max_items - usage->items;
  field_options.max_owned_bytes =
      native_options->max_owned_bytes - usage->owned_bytes;
  if (field_options.max_items == 0u)
    return message_fail(
        diagnostic, DATA_BIND_ERR_LIMIT, field->name,
        "Whole-message native item budget is exhausted");

  field_destination = destination + field->native_offset;
  status = data_bind_native_decode_usage(
      &field_options, field->data, reader,
      field_destination, field->data->storage_type->size,
      &field_usage, &native);
  if (status != DATA_BIND_OK)
    return message_native_failure(
        diagnostic, status, field->name, &native,
        "Native field decode failed");

  if (field_usage.items > field_options.max_items ||
      field_usage.owned_bytes > field_options.max_owned_bytes)
    return message_fail(
        diagnostic, DATA_BIND_ERR_RUNTIME, field->name,
        "Native field usage exceeded the admitted whole-message budget");
  usage->items += field_usage.items;
  usage->owned_bytes += field_usage.owned_bytes;

  status = data_bind_message_plan_internal_validate_field(
      plan, field->name, field_destination, &validation);
  if (status != DATA_BIND_OK)
    return message_fail(
        diagnostic, status,
        message_validation_diagnostic_field(plan, field, &validation),
        "%s",
        validation.message[0] != '\0'
            ? validation.message
            : "Native field validation failed");

  message_set_presence(destination, field, 1);
  message_set_null(destination, field, 0);
  return DATA_BIND_OK;
}

static DataBindStatus message_decode_prefixed_value(
    const DataBindMessagePlan *plan,
    const DataBindMessageFieldPlan *field,
    const DataBindNativeOptions *native_options,
    DataBindNativeDecodeUsage *usage,
    cserde_reader *source,
    const cserde_token *first,
    unsigned char *destination,
    DataBindMessagePlanDiagnostic *diagnostic) {
  MessagePrefixedReaderContext context;
  cserde_reader reader = {0};

  if (source == NULL || first == NULL)
    return message_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG,
        field != NULL ? field->name : NULL,
        "Invalid prefixed MessagePlan reader");

  context.source = source;
  context.first = *first;
  context.emitted_first = 0;
  if (cserde_reader_init(
          &reader, &MESSAGE_PREFIXED_READER_OPS, &context) != CSERDE_OK)
    return message_fail(
        diagnostic, DATA_BIND_ERR_RUNTIME,
        field != NULL ? field->name : NULL,
        "Could not initialize prefixed MessagePlan reader");

  return message_decode_value(
      plan, field, native_options, usage, &reader,
      destination, diagnostic);
}

static DataBindStatus message_decode_default(
    const DataBindMessagePlan *plan,
    const DataBindMessageFieldPlan *field,
    const DataBindNativeOptions *native_options,
    DataBindNativeDecodeUsage *usage,
    unsigned char *destination,
    DataBindMessagePlanDiagnostic *diagnostic) {
  MessageSingleReaderContext context;
  cserde_reader reader = {0};

  if (field == NULL || !field->has_default_token)
    return DATA_BIND_ERR_TYPE_NOT_FOUND;

  context.token = &field->default_token;
  context.emitted = 0;
  if (cserde_reader_init(
          &reader, &MESSAGE_SINGLE_READER_OPS, &context) != CSERDE_OK)
    return message_fail(
        diagnostic, DATA_BIND_ERR_RUNTIME, field->name,
        "Could not initialize compiled MessagePlan default reader");

  return message_decode_value(
      plan, field, native_options, usage, &reader,
      destination, diagnostic);
}

static DataBindStatus message_rollback(
    const DataBindMessagePlan *plan,
    const DataBindNativeOptions *native_options,
    void *destination,
    size_t destination_bytes,
    DataBindMessagePlanDiagnostic *diagnostic,
    DataBindStatus original_status) {
  DataBindNativeDiagnostic native =
      DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  DataBindStatus cleanup;

  cleanup = data_bind_native_clear(
      native_options, plan->native->data,
      destination, destination_bytes, &native);
  if (cleanup != DATA_BIND_OK)
    return message_native_failure(
        diagnostic, DATA_BIND_ERR_RUNTIME, NULL, &native,
        "MessagePlan rollback could not restore semantic zero");
  return original_status;
}

static DataBindStatus message_xml_text_coerce(
    const DataBindMessageFieldPlan *field,
    const DataBindNativeOptions *native_options,
    size_t workspace_prefix,
    const cserde_token *input,
    cserde_token *output,
    DataBindMessagePlanDiagnostic *diagnostic);

static DataBindStatus message_decode_native_impl(
    const DataBindMessagePlan *plan,
    const DataBindNativeOptions *native_options,
    DataBindFormat format,
    int format_aware,
    cserde_reader *reader,
    void *destination,
    size_t destination_bytes,
    DataBindMessagePlanDiagnostic *diagnostic) {
  DataBindNativeDiagnostic native =
      DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  DataBindNativeOptions field_options;
  DataBindNativeDecodeUsage usage = {1u, 0u};
  unsigned char *base = (unsigned char *)destination;
  unsigned char *bitmap;
  size_t bitmap_bytes;
  cserde_token token = {0};
  cserde_status reader_status;
  DataBindStatus status;
  size_t i;

  if (!message_diag_header_valid(diagnostic))
    return DATA_BIND_ERR_INVALID_ARG;
  message_diag_clear(diagnostic);

  if (format_aware &&
      format != DATA_BIND_FORMAT_JSON &&
      format != DATA_BIND_FORMAT_YAML &&
      format != DATA_BIND_FORMAT_CSV &&
      format != DATA_BIND_FORMAT_XML)
    return message_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL,
        "Unsupported native MessagePlan input format");

  if (plan == NULL || plan->native == NULL ||
      plan->native->data == NULL ||
      native_options == NULL || reader == NULL ||
      destination == NULL ||
      native_options->size < sizeof(*native_options) ||
      native_options->abi_version != DATA_BIND_NATIVE_ABI_VERSION ||
      native_options->workspace == NULL)
    return message_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL,
        "Invalid whole-message MessagePlan decode arguments");

  bitmap_bytes = message_bitmap_bytes(plan->field_count);
  if (bitmap_bytes > native_options->workspace_bytes)
    return message_fail(
        diagnostic, DATA_BIND_ERR_LIMIT, NULL,
        "Native workspace cannot hold MessagePlan field-state bitmap");

  status = data_bind_native_init(
      native_options, plan->native->data,
      destination, destination_bytes, &native);
  if (status != DATA_BIND_OK)
    return message_native_failure(
        diagnostic, status, NULL, &native,
        "MessagePlan native staging initialization failed");

  bitmap = (unsigned char *)native_options->workspace;
  if (bitmap_bytes != 0u) memset(bitmap, 0, bitmap_bytes);

  field_options = *native_options;
  field_options.workspace =
      (unsigned char *)native_options->workspace + bitmap_bytes;
  field_options.workspace_bytes =
      native_options->workspace_bytes - bitmap_bytes;

  reader_status = cserde_reader_next(reader, &token);
  if (reader_status != CSERDE_OK) {
    status = message_reader_failure(
        diagnostic, reader_status, NULL, "Message root");
    goto fail;
  }
  if (token.kind != CSERDE_MAP_BEGIN) {
    status = message_fail(
        diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, NULL,
        "MessagePlan requires a canonical CSerde MAP root");
    goto fail;
  }

  for (;;) {
    const DataBindMessageFieldPlan *field;
    size_t field_index = 0u;

    reader_status = cserde_reader_next(reader, &token);
    if (reader_status != CSERDE_OK) {
      status = message_reader_failure(
          diagnostic, reader_status, NULL, "Message field");
      goto fail;
    }
    if (token.kind == CSERDE_MAP_END) break;
    if (token.kind != CSERDE_STRING) {
      status = message_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, NULL,
          "Message field name must be a canonical CSerde STRING");
      goto fail;
    }

    field = message_field_slice(
        plan, &token.value.slice, &field_index);
    if (field == NULL) {
      status = message_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, NULL,
          "Unknown canonical DataBind message field");
      goto fail;
    }
    if (message_field_seen(bitmap, field_index)) {
      status = message_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field->name,
          "Duplicate canonical DataBind message field");
      goto fail;
    }

    reader_status = cserde_reader_next(reader, &token);
    if (reader_status != CSERDE_OK) {
      status = message_reader_failure(
          diagnostic, reader_status, field->name,
          "Message field value");
      goto fail;
    }

    if (token.kind == CSERDE_NULL) {
      if (!field->nullable || !field->has_null) {
        status = message_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field->name,
            "Explicit NULL is not admitted by the DataBind contract");
        goto fail;
      }
      message_set_presence(base, field, 1);
      message_set_null(base, field, 1);
    } else {
      cserde_token value_token = token;
      if (format_aware &&
          (format == DATA_BIND_FORMAT_CSV ||
           format == DATA_BIND_FORMAT_XML)) {
        status = message_xml_text_coerce(
            field, native_options, bitmap_bytes,
            &token, &value_token, diagnostic);
        if (status != DATA_BIND_OK) goto fail;
      }
      status = message_decode_prefixed_value(
          plan, field, &field_options, &usage, reader, &value_token,
          base, diagnostic);
      if (status != DATA_BIND_OK) goto fail;
    }

    message_mark_field_seen(bitmap, field_index);
  }

  for (i = 0u; i < plan->field_count; ++i) {
    const DataBindMessageFieldPlan *field = &plan->fields[i];
    if (message_field_seen(bitmap, i)) continue;

    if (field->has_default_token) {
      status = message_decode_default(
          plan, field, &field_options, &usage, base, diagnostic);
      if (status != DATA_BIND_OK) goto fail;
      continue;
    }

    if (field->optional) {
      message_set_presence(base, field, 0);
      message_set_null(base, field, 0);
      continue;
    }

    status = message_fail(
        diagnostic, DATA_BIND_ERR_TYPE_NOT_FOUND, field->name,
        "Required logical input is absent");
    goto fail;
  }

  message_diag_clear(diagnostic);
  return DATA_BIND_OK;

fail:
  return message_rollback(
      plan, native_options, destination, destination_bytes,
      diagnostic, status);
}

DataBindStatus data_bind_message_plan_decode_native(
    const DataBindMessagePlan *plan,
    const DataBindNativeOptions *native_options,
    cserde_reader *reader,
    void *destination,
    size_t destination_bytes,
    DataBindMessagePlanDiagnostic *diagnostic) {
  return message_decode_native_impl(
      plan, native_options, DATA_BIND_FORMAT_JSON, 0,
      reader, destination, destination_bytes, diagnostic);
}

DataBindStatus data_bind_message_plan_decode_native_format(
    const DataBindMessagePlan *plan,
    const DataBindNativeOptions *native_options,
    DataBindFormat format,
    cserde_reader *reader,
    void *destination,
    size_t destination_bytes,
    DataBindMessagePlanDiagnostic *diagnostic) {
  return message_decode_native_impl(
      plan, native_options, format, 1,
      reader, destination, destination_bytes, diagnostic);
}


static int message_object_compatible(
    const DataBindMessagePlan *plan,
    const cmeta_object_ref *object) {
  return plan != NULL &&
         plan->mode == DATA_BIND_MESSAGE_PLAN_OBJECT &&
         cmeta_data_desc_valid(plan->object_data) &&
         cmeta_object_ref_valid(object) &&
         cmeta_data_desc_equal(plan->object_data, object->data);
}

static DataBindStatus message_object_state_get(
    const DataBindMessageFieldPlan *field,
    const cmeta_object_ref *object,
    const DataBindMessageObjectStateProvider *provider,
    DataBindMessageObjectFieldState *out_state,
    DataBindMessagePlanDiagnostic *diagnostic) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status;

  if (out_state == NULL || field == NULL)
    return DATA_BIND_ERR_INVALID_ARG;

  *out_state = DATA_BIND_MESSAGE_OBJECT_VALUE;
  if (!field->optional && !field->nullable) return DATA_BIND_OK;

  if (!message_object_state_provider_valid(provider))
    return message_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, field->name,
        "Optional/nullable provider-backed field requires a DataBind object state provider");

  status = provider->get_state(
      provider->context, object, field->name, out_state, &error);
  if (status != DATA_BIND_OK)
    return message_fail(
        diagnostic, status, field->name, "%s",
        error.message[0] != '\0'
            ? error.message
            : "Object state provider read failed");

  if (*out_state != DATA_BIND_MESSAGE_OBJECT_ABSENT &&
      *out_state != DATA_BIND_MESSAGE_OBJECT_VALUE &&
      *out_state != DATA_BIND_MESSAGE_OBJECT_NULL)
    return message_fail(
        diagnostic, DATA_BIND_ERR_RUNTIME, field->name,
        "Object state provider returned an invalid field state");

  if (*out_state == DATA_BIND_MESSAGE_OBJECT_ABSENT && !field->optional)
    return message_fail(
        diagnostic, DATA_BIND_ERR_TYPE_NOT_FOUND, field->name,
        "Required provider-backed field is absent");
  if (*out_state == DATA_BIND_MESSAGE_OBJECT_NULL && !field->nullable)
    return message_fail(
        diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field->name,
        "Provider-backed field is NULL but not nullable");

  return DATA_BIND_OK;
}

static DataBindStatus message_object_state_set(
    const DataBindMessageFieldPlan *field,
    cmeta_object_ref *object,
    const DataBindMessageObjectStateProvider *provider,
    DataBindMessageObjectFieldState state,
    DataBindMessagePlanDiagnostic *diagnostic) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status;

  if (field == NULL || object == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (!field->optional && !field->nullable) {
    if (state != DATA_BIND_MESSAGE_OBJECT_VALUE)
      return message_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field->name,
          "Required non-nullable object field cannot carry DataBind state");
    return DATA_BIND_OK;
  }

  if (!message_object_state_provider_valid(provider))
    return message_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, field->name,
        "Optional/nullable provider-backed field requires a DataBind object state provider");

  status = provider->set_state(
      provider->context, object, field->name, state, &error);
  if (status != DATA_BIND_OK)
    return message_fail(
        diagnostic, status, field->name, "%s",
        error.message[0] != '\0'
            ? error.message
            : "Object state provider write failed");
  return DATA_BIND_OK;
}

static DataBindStatus message_object_field_workspace(
    const DataBindNativeOptions *options,
    size_t prefix_bytes,
    const DataBindMessageFieldPlan *field,
    void **out_storage,
    DataBindNativeOptions *out_options,
    DataBindMessagePlanDiagnostic *diagnostic) {
  uintptr_t start;
  uintptr_t aligned;
  size_t padding;
  size_t extent;
  size_t align;
  size_t used;

  if (out_storage != NULL) *out_storage = NULL;
  if (out_options != NULL) memset(out_options, 0, sizeof(*out_options));
  if (options == NULL || field == NULL || field->data == NULL ||
      field->data->storage_type == NULL || out_storage == NULL ||
      out_options == NULL || options->workspace == NULL ||
      prefix_bytes > options->workspace_bytes)
    return message_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG,
        field != NULL ? field->name : NULL,
        "Invalid provider-backed field workspace");

  extent = field->data->storage_type->size;
  align = field->data->storage_type->align;
  if (extent == 0u || align == 0u)
    return message_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, field->name,
        "Provider-backed field has invalid native storage metadata");

  start = (uintptr_t)((unsigned char *)options->workspace + prefix_bytes);
  padding = (size_t)(start % align);
  if (padding != 0u) padding = align - padding;
  if (padding > SIZE_MAX - prefix_bytes ||
      extent > SIZE_MAX - prefix_bytes - padding)
    return message_fail(
        diagnostic, DATA_BIND_ERR_LIMIT, field->name,
        "Provider-backed field workspace size overflow");

  used = prefix_bytes + padding + extent;
  if (used > options->workspace_bytes)
    return message_fail(
        diagnostic, DATA_BIND_ERR_LIMIT, field->name,
        "Native workspace cannot hold provider-backed field staging");

  aligned = start + padding;
  *out_storage = (void *)aligned;
  *out_options = *options;
  out_options->workspace =
      (unsigned char *)options->workspace + used;
  out_options->workspace_bytes = options->workspace_bytes - used;
  return DATA_BIND_OK;
}

static DataBindStatus message_object_decode_value(
    const DataBindMessagePlan *plan,
    const DataBindMessageFieldPlan *field,
    const DataBindNativeOptions *native_options,
    size_t workspace_prefix,
    cserde_reader *reader,
    cmeta_object_ref *object,
    const DataBindMessageObjectStateProvider *state_provider,
    DataBindMessagePlanDiagnostic *diagnostic) {
  DataBindNativeDiagnostic native = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  DataBindNativeOptions field_options;
  DataBindError validation = DATA_BIND_ERROR_INIT;
  void *temporary = NULL;
  DataBindStatus status;
  cmeta_status cmeta_result;

  status = message_object_field_workspace(
      native_options, workspace_prefix, field,
      &temporary, &field_options, diagnostic);
  if (status != DATA_BIND_OK) return status;

  status = data_bind_native_init(
      &field_options, field->data, temporary,
      field->data->storage_type->size, &native);
  if (status != DATA_BIND_OK)
    return message_native_failure(
        diagnostic, status, field->name, &native,
        "Provider-backed field temporary initialization failed");

  status = data_bind_native_decode(
      &field_options, field->data, reader, temporary,
      field->data->storage_type->size, &native);
  if (status != DATA_BIND_OK) {
    DataBindNativeDiagnostic cleanup = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    DataBindNativeDiagnostic failure = native;
    (void)data_bind_native_clear(
        &field_options, field->data, temporary,
        field->data->storage_type->size, &cleanup);
    return message_native_failure(
        diagnostic, status, field->name, &failure,
        "Provider-backed field decode failed");
  }

  status = data_bind_message_plan_internal_validate_field(
      plan, field->name, temporary, &validation);
  if (status != DATA_BIND_OK) {
    (void)data_bind_native_clear(
        &field_options, field->data, temporary,
        field->data->storage_type->size, &native);
    return message_fail(
        diagnostic, status, field->name, "%s",
        validation.message[0] != '\0'
            ? validation.message
            : "Provider-backed field validation failed");
  }

  cmeta_result = cmeta_object_field_assign(
      object, field->name, field->data, temporary);
  if (cmeta_result != CMETA_OK) {
    status = message_cmeta_status(cmeta_result);
    (void)data_bind_native_clear(
        &field_options, field->data, temporary,
        field->data->storage_type->size, &native);
    return message_fail(
        diagnostic, status, field->name,
        "CMeta object field assignment failed");
  }

  status = data_bind_native_clear(
      &field_options, field->data, temporary,
      field->data->storage_type->size, &native);
  if (status != DATA_BIND_OK)
    return message_native_failure(
        diagnostic, DATA_BIND_ERR_RUNTIME, field->name, &native,
        "Provider-backed field temporary cleanup failed");

  return message_object_state_set(
      field, object, state_provider,
      DATA_BIND_MESSAGE_OBJECT_VALUE, diagnostic);
}

static DataBindStatus message_object_decode_prefixed_value(
    const DataBindMessagePlan *plan,
    const DataBindMessageFieldPlan *field,
    const DataBindNativeOptions *native_options,
    size_t workspace_prefix,
    cserde_reader *source,
    const cserde_token *first,
    cmeta_object_ref *object,
    const DataBindMessageObjectStateProvider *state_provider,
    DataBindMessagePlanDiagnostic *diagnostic) {
  MessagePrefixedReaderContext context;
  cserde_reader reader = {0};

  if (source == NULL || first == NULL)
    return message_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG,
        field != NULL ? field->name : NULL,
        "Invalid provider-backed prefixed MessagePlan reader");

  context.source = source;
  context.first = *first;
  context.emitted_first = 0;
  if (cserde_reader_init(
          &reader, &MESSAGE_PREFIXED_READER_OPS, &context) != CSERDE_OK)
    return message_fail(
        diagnostic, DATA_BIND_ERR_RUNTIME,
        field != NULL ? field->name : NULL,
        "Could not initialize provider-backed prefixed reader");

  return message_object_decode_value(
      plan, field, native_options, workspace_prefix, &reader,
      object, state_provider, diagnostic);
}

static DataBindStatus message_object_decode_default(
    const DataBindMessagePlan *plan,
    const DataBindMessageFieldPlan *field,
    const DataBindNativeOptions *native_options,
    size_t workspace_prefix,
    cmeta_object_ref *object,
    const DataBindMessageObjectStateProvider *state_provider,
    DataBindMessagePlanDiagnostic *diagnostic) {
  MessageSingleReaderContext context;
  cserde_reader reader = {0};

  if (field == NULL || !field->has_default_token)
    return DATA_BIND_ERR_TYPE_NOT_FOUND;

  context.token = &field->default_token;
  context.emitted = 0;
  if (cserde_reader_init(
          &reader, &MESSAGE_SINGLE_READER_OPS, &context) != CSERDE_OK)
    return message_fail(
        diagnostic, DATA_BIND_ERR_RUNTIME, field->name,
        "Could not initialize provider-backed default reader");

  return message_object_decode_value(
      plan, field, native_options, workspace_prefix, &reader,
      object, state_provider, diagnostic);
}

DataBindStatus data_bind_message_plan_validate_object(
    const DataBindMessagePlan *plan,
    const cmeta_object_ref *object,
    const DataBindMessageObjectStateProvider *state_provider,
    DataBindError *error) {
  size_t i;

  if (error != NULL) *error = (DataBindError)DATA_BIND_ERROR_INIT;
  if (!message_object_compatible(plan, object))
    return DATA_BIND_ERR_INVALID_ARG;

  for (i = 0u; i < plan->field_count; ++i) {
    const DataBindMessageFieldPlan *field = &plan->fields[i];
    DataBindMessageObjectFieldState state;
    const cmeta_data_desc *value_data = NULL;
    const void *value = NULL;
    DataBindMessagePlanDiagnostic diagnostic =
        DATA_BIND_MESSAGE_PLAN_DIAGNOSTIC_INIT;
    DataBindStatus status;
    cmeta_status cmeta_result;

    status = message_object_state_get(
        field, object, state_provider, &state, &diagnostic);
    if (status != DATA_BIND_OK) {
      if (error != NULL) {
        error->code = status;
        snprintf(error->path, sizeof(error->path), "%s", field->name);
        snprintf(error->message, sizeof(error->message), "%s",
                 diagnostic.message);
      }
      return status;
    }
    if (state != DATA_BIND_MESSAGE_OBJECT_VALUE) continue;

    cmeta_result = cmeta_object_field_read(
        object, field->name, &value_data, &value);
    if (cmeta_result != CMETA_OK || value == NULL ||
        !cmeta_data_desc_equal(field->data, value_data)) {
      status = cmeta_result == CMETA_OK
                   ? DATA_BIND_ERR_TYPE_MISMATCH
                   : message_cmeta_status(cmeta_result);
      if (error != NULL) {
        error->code = status;
        snprintf(error->path, sizeof(error->path), "%s", field->name);
        snprintf(error->message, sizeof(error->message),
                 "CMeta object field read failed");
      }
      return status;
    }

    status = data_bind_message_plan_internal_validate_field(
        plan, field->name, value, error);
    if (status != DATA_BIND_OK) return status;
  }

  return DATA_BIND_OK;
}

static DataBindStatus message_xml_text_coerce(
    const DataBindMessageFieldPlan *field,
    const DataBindNativeOptions *native_options,
    size_t workspace_prefix,
    const cserde_token *input,
    cserde_token *output,
    DataBindMessagePlanDiagnostic *diagnostic) {
  const char *text;
  size_t length;

  if (field == NULL || native_options == NULL || input == NULL || output == NULL)
    return DATA_BIND_ERR_INVALID_ARG;
  *output = *input;
  if (input->kind != CSERDE_STRING || field->data == NULL)
    return DATA_BIND_OK;

  text = (const char *)input->value.slice.data;
  length = input->value.slice.size;
  if (length != 0u && text == NULL)
    return message_fail(
        diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field->name,
        "XML textual scalar has no source bytes");

  switch (field->data->kind) {
  case CMETA_DATA_BOOL:
    output->kind = CSERDE_BOOL;
    if ((length == 4u && memcmp(text, "true", 4u) == 0) ||
        (length == 3u && memcmp(text, "yes", 3u) == 0) ||
        (length == 1u && text[0] == '1')) {
      output->value.boolean = true;
      return DATA_BIND_OK;
    }
    if ((length == 5u && memcmp(text, "false", 5u) == 0) ||
        (length == 2u && memcmp(text, "no", 2u) == 0) ||
        (length == 1u && text[0] == '0')) {
      output->value.boolean = false;
      return DATA_BIND_OK;
    }
    return message_fail(
        diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field->name,
        "XML Boolean text does not match the canonical field type");

  case CMETA_DATA_SINT: {
    uint64_t magnitude = 0u;
    int negative = 0;
    int64_t value;
    if (!data_bind_internal_parse_integer_magnitude(
            text, length, (uint64_t)INT64_MAX + UINT64_C(1), 1,
            &magnitude, &negative) ||
        (!negative && magnitude > (uint64_t)INT64_MAX))
      return message_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field->name,
          "XML signed integer text does not match the canonical field type");
    value = negative
                ? (magnitude == (uint64_t)INT64_MAX + UINT64_C(1)
                       ? INT64_MIN
                       : -(int64_t)magnitude)
                : (int64_t)magnitude;
    output->kind = CSERDE_SINT;
    output->value.sint = value;
    return DATA_BIND_OK;
  }

  case CMETA_DATA_UINT: {
    uint64_t value = 0u;
    int negative = 0;
    if (!data_bind_internal_parse_integer_magnitude(
            text, length, UINT64_MAX, 0, &value, &negative) ||
        negative)
      return message_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field->name,
          "XML unsigned integer text does not match the canonical field type");
    output->kind = CSERDE_UINT;
    output->value.uint = value;
    return DATA_BIND_OK;
  }

  case CMETA_DATA_FLOAT: {
    char *scratch;
    char *end = NULL;
    double value;
    int valid;
    if (native_options->workspace == NULL ||
        workspace_prefix > native_options->workspace_bytes ||
        length == SIZE_MAX ||
        length + 1u > native_options->workspace_bytes - workspace_prefix)
      return message_fail(
          diagnostic, DATA_BIND_ERR_LIMIT, field->name,
          "Native workspace cannot hold XML floating-point text");

    /*
     * Use the tail, not the field-staging prefix. The staging address is
     * alignment-dependent and may otherwise overlap this text on MSVC/Win64.
     * The text is dead after conversion and is restored to zero before native
     * decode so later staging may safely reuse the same bytes.
     */
    scratch = (char *)native_options->workspace +
              native_options->workspace_bytes - (length + 1u);
    if (length != 0u) memcpy(scratch, text, length);
    scratch[length] = '\0';
    errno = 0;
    value = strtod(scratch, &end);
    valid = errno != ERANGE && end != scratch &&
            end == scratch + length && isfinite(value);
    memset(scratch, 0, length + 1u);
    if (!valid)
      return message_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field->name,
          "XML floating-point text does not match the canonical field type");
    output->kind = CSERDE_FLOAT;
    output->value.floating = value;
    return DATA_BIND_OK;
  }

  default:
    return DATA_BIND_OK;
  }
}

static DataBindStatus message_decode_object_impl(
    const DataBindMessagePlan *plan,
    const DataBindNativeOptions *native_options,
    DataBindFormat format,
    int format_aware,
    cserde_reader *reader,
    cmeta_object_ref *object,
    const DataBindMessageObjectStateProvider *state_provider,
    DataBindMessagePlanDiagnostic *diagnostic) {
  unsigned char *bitmap;
  size_t bitmap_bytes;
  cserde_token token = {0};
  cserde_status reader_status;
  DataBindStatus status;
  size_t i;

  if (!message_diag_header_valid(diagnostic))
    return DATA_BIND_ERR_INVALID_ARG;
  message_diag_clear(diagnostic);

  if (format_aware &&
      format != DATA_BIND_FORMAT_JSON &&
      format != DATA_BIND_FORMAT_YAML &&
      format != DATA_BIND_FORMAT_XML)
    return message_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL,
        "Unsupported provider-backed MessagePlan input format");

  if (!message_object_compatible(plan, object) ||
      native_options == NULL || reader == NULL ||
      native_options->size < sizeof(*native_options) ||
      native_options->abi_version != DATA_BIND_NATIVE_ABI_VERSION ||
      native_options->workspace == NULL)
    return message_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL,
        "Invalid provider-backed MessagePlan decode arguments");

  if ((plan->field_count != 0u) && object->field_provider == NULL)
    return message_fail(
        diagnostic, DATA_BIND_ERR_SCHEMA, NULL,
        "Provider-backed MessagePlan requires a CMeta field provider");

  bitmap_bytes = message_bitmap_bytes(plan->field_count);
  if (bitmap_bytes > native_options->workspace_bytes)
    return message_fail(
        diagnostic, DATA_BIND_ERR_LIMIT, NULL,
        "Native workspace cannot hold provider-backed field-state bitmap");
  bitmap = (unsigned char *)native_options->workspace;
  if (bitmap_bytes != 0u) memset(bitmap, 0, bitmap_bytes);

  reader_status = cserde_reader_next(reader, &token);
  if (reader_status != CSERDE_OK)
    return message_reader_failure(
        diagnostic, reader_status, NULL, "Message root");
  if (token.kind != CSERDE_MAP_BEGIN)
    return message_fail(
        diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, NULL,
        "Provider-backed MessagePlan requires a canonical CSerde MAP root");

  for (;;) {
    const DataBindMessageFieldPlan *field;
    size_t field_index = 0u;

    reader_status = cserde_reader_next(reader, &token);
    if (reader_status != CSERDE_OK)
      return message_reader_failure(
          diagnostic, reader_status, NULL, "Message field");
    if (token.kind == CSERDE_MAP_END) break;
    if (token.kind != CSERDE_STRING)
      return message_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, NULL,
          "Message field name must be a canonical CSerde STRING");

    field = message_field_slice(plan, &token.value.slice, &field_index);
    if (field == NULL)
      return message_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, NULL,
          "Unknown canonical DataBind message field");
    if (message_field_seen(bitmap, field_index))
      return message_fail(
          diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field->name,
          "Duplicate canonical DataBind message field");

    reader_status = cserde_reader_next(reader, &token);
    if (reader_status != CSERDE_OK)
      return message_reader_failure(
          diagnostic, reader_status, field->name,
          "Message field value");

    if (token.kind == CSERDE_NULL) {
      if (!field->nullable)
        return message_fail(
            diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, field->name,
            "Explicit NULL is not admitted by the DataBind contract");
      status = message_object_state_set(
          field, object, state_provider,
          DATA_BIND_MESSAGE_OBJECT_NULL, diagnostic);
    } else {
      cserde_token value_token = token;
      if (format_aware && format == DATA_BIND_FORMAT_XML) {
        status = message_xml_text_coerce(
            field, native_options, bitmap_bytes,
            &token, &value_token, diagnostic);
        if (status != DATA_BIND_OK) return status;
      }
      status = message_object_decode_prefixed_value(
          plan, field, native_options, bitmap_bytes,
          reader, &value_token, object, state_provider, diagnostic);
    }
    if (status != DATA_BIND_OK) return status;
    message_mark_field_seen(bitmap, field_index);
  }

  for (i = 0u; i < plan->field_count; ++i) {
    const DataBindMessageFieldPlan *field = &plan->fields[i];
    if (message_field_seen(bitmap, i)) continue;

    if (field->has_default_token) {
      status = message_object_decode_default(
          plan, field, native_options, bitmap_bytes,
          object, state_provider, diagnostic);
      if (status != DATA_BIND_OK) return status;
      continue;
    }

    if (field->optional) {
      status = message_object_state_set(
          field, object, state_provider,
          DATA_BIND_MESSAGE_OBJECT_ABSENT, diagnostic);
      if (status != DATA_BIND_OK) return status;
      continue;
    }

    return message_fail(
        diagnostic, DATA_BIND_ERR_TYPE_NOT_FOUND, field->name,
        "Required logical input is absent");
  }

  message_diag_clear(diagnostic);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_message_plan_decode_object(
    const DataBindMessagePlan *plan,
    const DataBindNativeOptions *native_options,
    cserde_reader *reader,
    cmeta_object_ref *object,
    const DataBindMessageObjectStateProvider *state_provider,
    DataBindMessagePlanDiagnostic *diagnostic) {
  return message_decode_object_impl(
      plan, native_options, DATA_BIND_FORMAT_JSON, 0, reader, object,
      state_provider, diagnostic);
}

DataBindStatus data_bind_message_plan_decode_object_format(
    const DataBindMessagePlan *plan,
    const DataBindNativeOptions *native_options,
    DataBindFormat format,
    cserde_reader *reader,
    cmeta_object_ref *object,
    const DataBindMessageObjectStateProvider *state_provider,
    DataBindMessagePlanDiagnostic *diagnostic) {
  return message_decode_object_impl(
      plan, native_options, format, 1, reader, object,
      state_provider, diagnostic);
}

static DataBindStatus message_writer_failure(
    DataBindMessagePlanDiagnostic *diagnostic,
    cserde_status writer_status,
    const char *field,
    const char *context) {
  DataBindStatus status =
      writer_status == CSERDE_LIMIT_EXCEEDED
          ? DATA_BIND_ERR_LIMIT
          : writer_status == CSERDE_SINK_ERROR
                ? DATA_BIND_ERR_IO
                : DATA_BIND_ERR_RUNTIME;
  return message_fail(
      diagnostic, status, field, "%s: CSerde writer failed (%d)",
      context != NULL ? context : "Message encode",
      (int)writer_status);
}

static DataBindStatus message_write_token(
    cserde_writer *writer,
    const cserde_token *token,
    DataBindMessagePlanDiagnostic *diagnostic,
    const char *field,
    const char *context) {
  cserde_status status = cserde_writer_write(writer, token);
  return status == CSERDE_OK
             ? DATA_BIND_OK
             : message_writer_failure(
                   diagnostic, status, field, context);
}

DataBindStatus data_bind_message_plan_encode_native(
    const DataBindMessagePlan *plan,
    const DataBindNativeOptions *native_options,
    const void *source,
    size_t source_bytes,
    cserde_writer *writer,
    DataBindMessagePlanDiagnostic *diagnostic) {
  const unsigned char *base = (const unsigned char *)source;
  cserde_token token = {0};
  size_t i;
  DataBindStatus status;
  size_t required_bytes;

  if (!message_diag_header_valid(diagnostic))
    return DATA_BIND_ERR_INVALID_ARG;
  message_diag_clear(diagnostic);

  required_bytes =
      plan != NULL && plan->native != NULL && plan->native->data != NULL &&
              plan->native->data->storage_type != NULL
          ? plan->native->data->storage_type->size
          : 0u;
  if (plan == NULL || plan->mode != DATA_BIND_MESSAGE_PLAN_NATIVE ||
      plan->native == NULL || plan->native->data == NULL ||
      plan->native->data->storage_type == NULL ||
      native_options == NULL || source == NULL || writer == NULL ||
      native_options->size < sizeof(*native_options) ||
      native_options->abi_version != DATA_BIND_NATIVE_ABI_VERSION ||
      required_bytes == 0u || source_bytes < required_bytes)
    return message_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL,
        "Invalid native MessagePlan encode arguments");

  token.kind = CSERDE_MAP_BEGIN;
  status = message_write_token(
      writer, &token, diagnostic, NULL, "Message root begin");
  if (status != DATA_BIND_OK) return status;

  for (i = 0u; i < plan->field_count; ++i) {
    const DataBindMessageFieldPlan *field = &plan->fields[i];
    DataBindMessageObjectFieldState state;
    const void *value;
    DataBindNativeDiagnostic native = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    DataBindError validation = DATA_BIND_ERROR_INIT;

    status = message_native_field_state(
        field, base, source_bytes, &state, diagnostic);
    if (status != DATA_BIND_OK) return status;
    if (state == DATA_BIND_MESSAGE_OBJECT_ABSENT) continue;

    token = (cserde_token){0};
    token.kind = CSERDE_STRING;
    token.value.slice.data = (const unsigned char *)field->name;
    token.value.slice.size = strlen(field->name);
    token.value.slice.lifetime = CSERDE_VIEW_STABLE;
    status = message_write_token(
        writer, &token, diagnostic, field->name, "Message field name");
    if (status != DATA_BIND_OK) return status;

    if (state == DATA_BIND_MESSAGE_OBJECT_NULL) {
      token = (cserde_token){0};
      token.kind = CSERDE_NULL;
      status = message_write_token(
          writer, &token, diagnostic, field->name, "Message NULL value");
      if (status != DATA_BIND_OK) return status;
      continue;
    }

    if (field->data == NULL || field->data->storage_type == NULL ||
        field->native_offset > source_bytes ||
        field->data->storage_type->size >
            source_bytes - field->native_offset)
      return message_fail(
          diagnostic, DATA_BIND_ERR_SCHEMA, field->name,
          "Native MessagePlan field storage is outside the source object");

    value = base + field->native_offset;
    status = data_bind_message_plan_internal_validate_field(
        plan, field->name, value, &validation);
    if (status != DATA_BIND_OK)
      return message_fail(
          diagnostic, status,
          message_validation_diagnostic_field(plan, field, &validation),
          "%s",
          validation.message[0] != '\0'
              ? validation.message
              : "Native field validation failed");

    status = data_bind_native_encode(
        native_options, field->data, value,
        field->data->storage_type->size, writer, &native);
    if (status != DATA_BIND_OK)
      return message_native_failure(
          diagnostic, status, field->name, &native,
          "Native field encode failed");
  }

  token = (cserde_token){0};
  token.kind = CSERDE_MAP_END;
  status = message_write_token(
      writer, &token, diagnostic, NULL, "Message root end");
  if (status != DATA_BIND_OK) return status;

  message_diag_clear(diagnostic);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_message_plan_encode_object(
    const DataBindMessagePlan *plan,
    const DataBindNativeOptions *native_options,
    const cmeta_object_ref *object,
    const DataBindMessageObjectStateProvider *state_provider,
    cserde_writer *writer,
    DataBindMessagePlanDiagnostic *diagnostic) {
  cserde_token token = {0};
  size_t i;
  DataBindStatus status;

  if (!message_diag_header_valid(diagnostic))
    return DATA_BIND_ERR_INVALID_ARG;
  message_diag_clear(diagnostic);

  if (!message_object_compatible(plan, object) ||
      native_options == NULL || writer == NULL ||
      native_options->size < sizeof(*native_options) ||
      native_options->abi_version != DATA_BIND_NATIVE_ABI_VERSION)
    return message_fail(
        diagnostic, DATA_BIND_ERR_INVALID_ARG, NULL,
        "Invalid provider-backed MessagePlan encode arguments");

  token.kind = CSERDE_MAP_BEGIN;
  status = message_write_token(
      writer, &token, diagnostic, NULL, "Message root begin");
  if (status != DATA_BIND_OK) return status;

  for (i = 0u; i < plan->field_count; ++i) {
    const DataBindMessageFieldPlan *field = &plan->fields[i];
    DataBindMessageObjectFieldState state;
    const cmeta_data_desc *value_data = NULL;
    const void *value = NULL;
    DataBindNativeDiagnostic native = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    DataBindError validation = DATA_BIND_ERROR_INIT;
    cmeta_status cmeta_result;

    status = message_object_state_get(
        field, object, state_provider, &state, diagnostic);
    if (status != DATA_BIND_OK) return status;
    if (state == DATA_BIND_MESSAGE_OBJECT_ABSENT) continue;

    token.kind = CSERDE_STRING;
    token.value.slice.data = (const unsigned char *)field->name;
    token.value.slice.size = strlen(field->name);
    token.value.slice.lifetime = CSERDE_VIEW_STABLE;
    status = message_write_token(
        writer, &token, diagnostic, field->name, "Message field name");
    if (status != DATA_BIND_OK) return status;

    if (state == DATA_BIND_MESSAGE_OBJECT_NULL) {
      token = (cserde_token){0};
      token.kind = CSERDE_NULL;
      status = message_write_token(
          writer, &token, diagnostic, field->name, "Message NULL value");
      if (status != DATA_BIND_OK) return status;
      continue;
    }

    cmeta_result = cmeta_object_field_read(
        object, field->name, &value_data, &value);
    if (cmeta_result != CMETA_OK || value == NULL ||
        !cmeta_data_desc_equal(field->data, value_data))
      return message_fail(
          diagnostic,
          cmeta_result == CMETA_OK
              ? DATA_BIND_ERR_TYPE_MISMATCH
              : message_cmeta_status(cmeta_result),
          field->name,
          "CMeta object field read does not match MessagePlan semantics");

    status = data_bind_message_plan_internal_validate_field(
        plan, field->name, value, &validation);
    if (status != DATA_BIND_OK)
      return message_fail(
          diagnostic, status, field->name, "%s",
          validation.message[0] != '\0'
              ? validation.message
              : "Provider-backed field validation failed");

    status = data_bind_native_encode(
        native_options, field->data, value,
        field->data->storage_type->size, writer, &native);
    if (status != DATA_BIND_OK)
      return message_native_failure(
          diagnostic, status, field->name, &native,
          "Provider-backed field encode failed");
  }

  token = (cserde_token){0};
  token.kind = CSERDE_MAP_END;
  status = message_write_token(
      writer, &token, diagnostic, NULL, "Message root end");
  if (status != DATA_BIND_OK) return status;

  message_diag_clear(diagnostic);
  return DATA_BIND_OK;
}
