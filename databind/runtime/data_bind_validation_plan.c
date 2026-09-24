#include "data_bind_validation_plan.h"
#include "data_bind_validation_plan_internal.h"
#include "data_bind_native_internal.h"

#include "re.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { DATA_BIND_VALIDATION_MAX_RULES = 40000u };

typedef enum DataBindValidationNumericDomain {
  DATA_BIND_VALIDATION_NUMERIC_NONE = 0,
  DATA_BIND_VALIDATION_NUMERIC_SIGNED,
  DATA_BIND_VALIDATION_NUMERIC_UNSIGNED,
  DATA_BIND_VALIDATION_NUMERIC_FLOAT
} DataBindValidationNumericDomain;

typedef struct DataBindValidationRule {
  char *field_name;
  DataBindSchemaConstraintKind kind;
  cmeta_data_kind field_kind;
  DataBindValidationNumericDomain numeric_domain;
  union {
    int64_t signed_value;
    uint64_t unsigned_value;
    double float_value;
  } numeric;
  int has_min;
  size_t min_size;
  int has_max;
  size_t max_size;
  re_t pattern;
} DataBindValidationRule;

typedef enum DataBindValidationChildKind {
  DATA_BIND_VALIDATION_CHILD_OBJECT = 1,
  DATA_BIND_VALIDATION_CHILD_SEQUENCE,
  DATA_BIND_VALIDATION_CHILD_SET,
  DATA_BIND_VALIDATION_CHILD_MAP_VALUES
} DataBindValidationChildKind;

typedef struct DataBindValidationChild {
  char *field_name;
  DataBindValidationChildKind kind;
  struct DataBindValidationPlan *plan;
} DataBindValidationChild;

struct DataBindValidationPlan {
  char *type_name;
  size_t rule_count;
  DataBindValidationRule *rules;
  size_t child_count;
  DataBindValidationChild *children;
};

static DataBindStatus validation_error(
    DataBindError *error, DataBindStatus status,
    const char *path, const char *message) {
  size_t size;
  if (error == NULL) return status;
  size = error->size != 0u && error->size < sizeof(*error)
             ? error->size
             : sizeof(*error);
  if (size >= offsetof(DataBindError, code) + sizeof(error->code))
    error->code = status;
  if (size >= offsetof(DataBindError, line) + sizeof(error->line))
    error->line = -1;
  if (size >= offsetof(DataBindError, column) + sizeof(error->column))
    error->column = -1;
  if (size >= offsetof(DataBindError, path) + sizeof(error->path))
    snprintf(error->path, sizeof(error->path), "%s",
             path != NULL ? path : "");
  if (size >= offsetof(DataBindError, message) + sizeof(error->message))
    snprintf(error->message, sizeof(error->message), "%s",
             message != NULL ? message : "");
  return status;
}

static void validation_error_clear(DataBindError *error) {
  (void)validation_error(error, DATA_BIND_OK, NULL, NULL);
}

static char *validation_strdup(const char *text) {
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

static void validation_path(
    char out[260], const char *type_name, const char *field_name) {
  snprintf(out, 260u, "%s.%s",
           type_name != NULL ? type_name : "",
           field_name != NULL ? field_name : "");
}

static DataBindStatus validation_schema_error(
    DataBindError *error, const char *type_name,
    const char *field_name, const char *message) {
  char path[260];
  validation_path(path, type_name, field_name);
  return validation_error(error, DATA_BIND_ERR_SCHEMA, path, message);
}

static int validation_path_field(
    char out[260], const char *prefix, const char *field_name) {
  int written = snprintf(
      out, 260u, "%s.%s",
      prefix != NULL ? prefix : "",
      field_name != NULL ? field_name : "");
  return written >= 0 && (size_t)written < 260u;
}

static int validation_path_index(
    char out[260], const char *field_path, size_t index) {
  int written = snprintf(
      out, 260u, "%s[%zu]",
      field_path != NULL ? field_path : "", index);
  return written >= 0 && (size_t)written < 260u;
}

static DataBindStatus validation_path_overflow(
    DataBindError *error, const char *prefix) {
  return validation_error(
      error, DATA_BIND_ERR_LIMIT, prefix, 
      "Validation diagnostic path exceeds the bounded capacity");
}

static DataBindStatus validation_type_error_at(
    const char *prefix, const DataBindValidationRule *rule,
    DataBindError *error, const char *message) {
  char path[260];
  if (!validation_path_field(
          path, prefix, rule != NULL ? rule->field_name : NULL))
    return validation_path_overflow(error, prefix);
  return validation_error(
      error, DATA_BIND_ERR_TYPE_MISMATCH, path, message);
}

static DataBindStatus validation_rule_error_at(
    const char *prefix, const DataBindValidationRule *rule,
    DataBindError *error, const char *message) {
  char path[260];
  if (!validation_path_field(
          path, prefix, rule != NULL ? rule->field_name : NULL))
    return validation_path_overflow(error, prefix);
  return validation_error(
      error, DATA_BIND_ERR_VALIDATION, path, message);
}

static int validation_integer_base(const char *text) {
  const char *cursor = text;
  if (cursor == NULL) return 10;
  if (*cursor == '+' || *cursor == '-') ++cursor;
  return cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')
             ? 0
             : 10;
}

static int parse_signed_bound(const char *text, int64_t *out) {
  char *end = NULL;
  long long value;
  if (text == NULL || out == NULL || text[0] == '\0') return 0;
  errno = 0;
  value = strtoll(text, &end, validation_integer_base(text));
  if (errno != 0 || end == text || end == NULL || *end != '\0')
    return 0;
  *out = (int64_t)value;
  return 1;
}

static int parse_unsigned_bound(const char *text, uint64_t *out) {
  char *end = NULL;
  unsigned long long value;
  if (text == NULL || out == NULL || text[0] == '\0' ||
      text[0] == '-')
    return 0;
  errno = 0;
  value = strtoull(text, &end, validation_integer_base(text));
  if (errno != 0 || end == text || end == NULL || *end != '\0')
    return 0;
  *out = (uint64_t)value;
  return 1;
}

static int parse_float_bound(const char *text, double *out) {
  char *end = NULL;
  double value;
  if (text == NULL || out == NULL || text[0] == '\0') return 0;
  errno = 0;
  value = strtod(text, &end);
  if (errno != 0 || end == text || end == NULL || *end != '\0' ||
      !isfinite(value))
    return 0;
  *out = value;
  return 1;
}

static int size_constraint_kind_supported(cmeta_data_kind kind) {
  return kind == CMETA_DATA_STRING ||
         kind == CMETA_DATA_BYTES ||
         kind == CMETA_DATA_SEQUENCE ||
         kind == CMETA_DATA_SET ||
         kind == CMETA_DATA_MAP;
}

static void validation_rule_clear(DataBindValidationRule *rule) {
  if (rule == NULL) return;
  free(rule->field_name);
  if (rule->pattern != NULL) re_destroy(rule->pattern);
  memset(rule, 0, sizeof(*rule));
}

void data_bind_validation_plan_free(DataBindValidationPlan *plan) {
  size_t i;
  if (plan == NULL) return;
  for (i = 0u; i < plan->rule_count; ++i)
    validation_rule_clear(&plan->rules[i]);
  for (i = 0u; i < plan->child_count; ++i) {
    free(plan->children[i].field_name);
    data_bind_validation_plan_free(plan->children[i].plan);
  }
  free(plan->children);
  free(plan->rules);
  free(plan->type_name);
  free(plan);
}

const char *data_bind_validation_plan_type_name(
    const DataBindValidationPlan *plan) {
  return plan != NULL ? plan->type_name : NULL;
}

size_t data_bind_validation_plan_rule_count(
    const DataBindValidationPlan *plan) {
  return plan != NULL ? plan->rule_count : 0u;
}

size_t data_bind_validation_plan_internal_child_count(
    const DataBindValidationPlan *plan) {
  return plan != NULL ? plan->child_count : 0u;
}

int data_bind_validation_plan_internal_rule_info(
    const DataBindValidationPlan *plan, size_t rule_index,
    DataBindValidationRuleInfo *out) {
  const DataBindValidationRule *rule;
  if (plan == NULL || out == NULL || rule_index >= plan->rule_count) return 0;
  rule = &plan->rules[rule_index];
  out->field_name = rule->field_name;
  out->kind = rule->kind;
  out->field_kind = rule->field_kind;
  return rule->field_name != NULL;
}

static DataBindStatus compile_numeric_rule(
    const DataBindSchemaField *field,
    const DataBindSchemaConstraint *constraint,
    DataBindValidationRule *rule,
    const char *type_name, DataBindError *error) {
  if (!field->has_cmeta_kind || constraint->value == NULL)
    return validation_schema_error(
        error, type_name, field->name,
        "Numeric validation requires canonical field type metadata");

  if (field->cmeta_kind == CMETA_DATA_SINT) {
    rule->numeric_domain = DATA_BIND_VALIDATION_NUMERIC_SIGNED;
    if (!parse_signed_bound(
            constraint->value, &rule->numeric.signed_value))
      return validation_schema_error(
          error, type_name, field->name,
          "Signed @Min/@Max operand is not an exact int64 value");
  } else if (field->cmeta_kind == CMETA_DATA_UINT) {
    rule->numeric_domain = DATA_BIND_VALIDATION_NUMERIC_UNSIGNED;
    if (!parse_unsigned_bound(
            constraint->value, &rule->numeric.unsigned_value))
      return validation_schema_error(
          error, type_name, field->name,
          "Unsigned @Min/@Max operand is not an exact uint64 value");
  } else if (field->cmeta_kind == CMETA_DATA_FLOAT) {
    rule->numeric_domain = DATA_BIND_VALIDATION_NUMERIC_FLOAT;
    if (!parse_float_bound(
            constraint->value, &rule->numeric.float_value))
      return validation_schema_error(
          error, type_name, field->name,
          "Floating @Min/@Max operand is not finite");
  } else {
    return validation_schema_error(
        error, type_name, field->name,
        "@Min/@Max requires a numeric scalar field");
  }
  return DATA_BIND_OK;
}

static DataBindStatus compile_size_rule(
    const DataBindSchemaField *field,
    const DataBindSchemaConstraint *constraint,
    DataBindValidationRule *rule,
    const char *type_name, DataBindError *error) {
  if (!field->has_cmeta_kind ||
      !size_constraint_kind_supported(field->cmeta_kind))
    return validation_schema_error(
        error, type_name, field->name,
        "@Size requires string, bytes, sequence, set, or map");
  if (!constraint->has_min && !constraint->has_max)
    return validation_schema_error(
        error, type_name, field->name,
        "@Size requires at least one bound");
  if (constraint->has_min && constraint->has_max &&
      constraint->min_size > constraint->max_size)
    return validation_schema_error(
        error, type_name, field->name,
        "@Size requires min <= max");
  rule->has_min = constraint->has_min;
  rule->min_size = constraint->min_size;
  rule->has_max = constraint->has_max;
  rule->max_size = constraint->max_size;
  return DATA_BIND_OK;
}

static DataBindStatus compile_pattern_rule(
    const DataBindSchemaField *field,
    const DataBindSchemaConstraint *constraint,
    DataBindValidationRule *rule,
    const char *type_name, DataBindError *error) {
  re_status_t status;
  if (!field->has_cmeta_kind ||
      field->cmeta_kind != CMETA_DATA_STRING)
    return validation_schema_error(
        error, type_name, field->name,
        "@Pattern requires a string field");
  if (constraint->pattern == NULL)
    return validation_schema_error(
        error, type_name, field->name,
        "@Pattern requires regex text");

  status = re_compile_n(
      constraint->pattern, strlen(constraint->pattern), NULL,
      &rule->pattern);
  if (status == RE_STATUS_NO_MEMORY)
    return validation_error(
        error, DATA_BIND_ERR_OOM, type_name,
        "Unable to compile validation regex");
  if (status != RE_STATUS_OK)
    return validation_schema_error(
        error, type_name, field->name,
        "Invalid or over-budget @Pattern expression");
  return DATA_BIND_OK;
}

enum { DATA_BIND_VALIDATION_MAX_DEPTH = 32u };

typedef struct DataBindValidationCompileContext {
  const char *stack[DATA_BIND_VALIDATION_MAX_DEPTH];
  size_t depth;
} DataBindValidationCompileContext;

typedef struct DataBindValidationChildSpec {
  DataBindValidationChildKind kind;
  const char *type_name;
} DataBindValidationChildSpec;

static int validation_record_kind(DataBindSchemaKind kind) {
  return kind == DATA_BIND_SCHEMA_MESSAGE ||
         kind == DATA_BIND_SCHEMA_COMPOSITE ||
         kind == DATA_BIND_SCHEMA_GROUP;
}

static int validation_child_spec(
    DataBind *codec, const DataBindSchemaField *field,
    DataBindValidationChildSpec *out) {
  DataBindSchemaType child_type = DATA_BIND_SCHEMA_TYPE_INIT;
  const char *candidate = NULL;
  DataBindValidationChildKind kind = 0;

  if (codec == NULL || field == NULL || out == NULL) return 0;
  memset(out, 0, sizeof(*out));

  if (field->is_group && field->group_type != NULL) {
    candidate = field->group_type;
    kind = DATA_BIND_VALIDATION_CHILD_SEQUENCE;
  } else if (field->is_map && field->value_type != NULL) {
    candidate = field->value_type;
    kind = DATA_BIND_VALIDATION_CHILD_MAP_VALUES;
  } else if (field->is_collection && field->inner_type != NULL) {
    candidate = field->inner_type;
    kind = field->collection_kind != NULL &&
                   strcmp(field->collection_kind, "set") == 0
               ? DATA_BIND_VALIDATION_CHILD_SET
               : DATA_BIND_VALIDATION_CHILD_SEQUENCE;
  } else if (field->has_cmeta_kind &&
             field->cmeta_kind == CMETA_DATA_STRUCT &&
             field->type != NULL) {
    candidate = field->type;
    kind = DATA_BIND_VALIDATION_CHILD_OBJECT;
  }

  if (candidate == NULL ||
      !data_bind_schema_find_type(codec, candidate, &child_type) ||
      !validation_record_kind(child_type.kind))
    return 0;

  out->kind = kind;
  out->type_name = candidate;
  return 1;
}

static int validation_compile_stack_contains(
    const DataBindValidationCompileContext *context,
    const char *type_name) {
  size_t i;
  if (context == NULL || type_name == NULL) return 0;
  for (i = 0u; i < context->depth; ++i)
    if (context->stack[i] != NULL &&
        strcmp(context->stack[i], type_name) == 0)
      return 1;
  return 0;
}

static int validation_plan_empty(const DataBindValidationPlan *plan) {
  return plan == NULL ||
         (plan->rule_count == 0u && plan->child_count == 0u);
}

static DataBindStatus validation_plan_compile_internal(
    DataBind *codec, const char *type_name,
    DataBindValidationPlan **out_plan, DataBindError *error,
    const DataBindValidationCompileContext *parent_context,
    size_t *remaining_rules) {
  DataBindSchemaType reflected_type = DATA_BIND_SCHEMA_TYPE_INIT;
  DataBindValidationCompileContext context = {0};
  DataBindValidationPlan *plan = NULL;
  size_t field_count;
  size_t rule_count = 0u;
  size_t field_index;
  size_t next_rule = 0u;

  if (out_plan != NULL) *out_plan = NULL;
  if (codec == NULL || type_name == NULL || type_name[0] == '\0' ||
      out_plan == NULL || remaining_rules == NULL)
    return validation_error(
        error, DATA_BIND_ERR_INVALID_ARG, NULL,
        "Invalid ValidationPlan compile arguments");

  if (parent_context != NULL) context = *parent_context;
  if (context.depth >= DATA_BIND_VALIDATION_MAX_DEPTH)
    return validation_error(
        error, DATA_BIND_ERR_LIMIT, type_name,
        "ValidationPlan nesting depth exceeds the bounded limit");
  if (validation_compile_stack_contains(&context, type_name))
    return validation_error(
        error, DATA_BIND_ERR_SCHEMA, type_name,
        "Recursive ValidationPlan type cycle is not supported");
  context.stack[context.depth++] = type_name;

  if (!data_bind_schema_find_type(codec, type_name, &reflected_type))
    return validation_error(
        error, DATA_BIND_ERR_TYPE_NOT_FOUND, type_name,
        "ValidationPlan type was not found");
  if (!validation_record_kind(reflected_type.kind))
    return validation_error(
        error, DATA_BIND_ERR_SCHEMA, type_name,
        "ValidationPlan requires a record type");

  field_count = data_bind_schema_field_count(codec, type_name);
  for (field_index = 0u; field_index < field_count; ++field_index) {
    size_t count =
        data_bind_schema_field_constraint_count(
            codec, type_name, field_index);
    if (count > *remaining_rules - rule_count)
      return validation_error(
          error, DATA_BIND_ERR_LIMIT, type_name,
          "ValidationPlan rule count exceeds the bounded limit");
    rule_count += count;
  }
  *remaining_rules -= rule_count;

  plan = (DataBindValidationPlan *)calloc(1u, sizeof(*plan));
  if (plan == NULL)
    return validation_error(
        error, DATA_BIND_ERR_OOM, type_name,
        "Unable to allocate ValidationPlan");
  plan->type_name = validation_strdup(type_name);
  if (plan->type_name == NULL) {
    data_bind_validation_plan_free(plan);
    return validation_error(
        error, DATA_BIND_ERR_OOM, type_name,
        "Unable to copy ValidationPlan type name");
  }
  if (rule_count != 0u) {
    plan->rules = (DataBindValidationRule *)calloc(
        rule_count, sizeof(*plan->rules));
    if (plan->rules == NULL) {
      data_bind_validation_plan_free(plan);
      return validation_error(
          error, DATA_BIND_ERR_OOM, type_name,
          "Unable to allocate ValidationPlan rules");
    }
  }
  if (field_count != 0u) {
    plan->children = (DataBindValidationChild *)calloc(
        field_count, sizeof(*plan->children));
    if (plan->children == NULL) {
      data_bind_validation_plan_free(plan);
      return validation_error(
          error, DATA_BIND_ERR_OOM, type_name,
          "Unable to allocate nested ValidationPlan bindings");
    }
  }
  plan->rule_count = rule_count;

  for (field_index = 0u; field_index < field_count; ++field_index) {
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    size_t constraint_count;
    size_t constraint_index;
    DataBindValidationChildSpec child_spec;

    if (!data_bind_schema_field_at(
            codec, type_name, field_index, &field) ||
        field.name == NULL) {
      data_bind_validation_plan_free(plan);
      return validation_error(
          error, DATA_BIND_ERR_SCHEMA, type_name,
          "ValidationPlan field reflection failed");
    }

    constraint_count =
        data_bind_schema_field_constraint_count(
            codec, type_name, field_index);
    for (constraint_index = 0u;
         constraint_index < constraint_count;
         ++constraint_index) {
      DataBindSchemaConstraint constraint =
          DATA_BIND_SCHEMA_CONSTRAINT_INIT;
      DataBindValidationRule *rule;
      DataBindStatus status;

      if (next_rule >= rule_count ||
          !data_bind_schema_field_constraint_at(
              codec, type_name, field_index, constraint_index,
              &constraint)) {
        data_bind_validation_plan_free(plan);
        return validation_schema_error(
            error, type_name, field.name,
            "Validation constraint reflection failed");
      }

      rule = &plan->rules[next_rule];
      rule->field_name = validation_strdup(field.name);
      rule->kind = constraint.kind;
      rule->field_kind = field.cmeta_kind;
      if (rule->field_name == NULL) {
        data_bind_validation_plan_free(plan);
        return validation_error(
            error, DATA_BIND_ERR_OOM, type_name,
            "Unable to copy validation field name");
      }

      if (constraint.kind == DATA_BIND_SCHEMA_CONSTRAINT_MIN ||
          constraint.kind == DATA_BIND_SCHEMA_CONSTRAINT_MAX) {
        status = compile_numeric_rule(
            &field, &constraint, rule, type_name, error);
      } else if (constraint.kind ==
                 DATA_BIND_SCHEMA_CONSTRAINT_SIZE) {
        status = compile_size_rule(
            &field, &constraint, rule, type_name, error);
      } else if (constraint.kind ==
                 DATA_BIND_SCHEMA_CONSTRAINT_PATTERN) {
        status = compile_pattern_rule(
            &field, &constraint, rule, type_name, error);
      } else {
        status = validation_schema_error(
            error, type_name, field.name,
            "Unknown reflected validation constraint");
      }

      if (status != DATA_BIND_OK) {
        data_bind_validation_plan_free(plan);
        return status;
      }
      ++next_rule;
    }

    if (validation_child_spec(codec, &field, &child_spec)) {
      DataBindValidationPlan *child_plan = NULL;
      DataBindStatus status = validation_plan_compile_internal(
          codec, child_spec.type_name, &child_plan, error,
          &context, remaining_rules);
      if (status != DATA_BIND_OK) {
        data_bind_validation_plan_free(plan);
        return status;
      }
      if (validation_plan_empty(child_plan)) {
        data_bind_validation_plan_free(child_plan);
      } else {
        DataBindValidationChild *child =
            &plan->children[plan->child_count];
        child->field_name = validation_strdup(field.name);
        child->kind = child_spec.kind;
        child->plan = child_plan;
        if (child->field_name == NULL) {
          data_bind_validation_plan_free(child_plan);
          child->plan = NULL;
          data_bind_validation_plan_free(plan);
          return validation_error(
              error, DATA_BIND_ERR_OOM, type_name,
              "Unable to copy nested validation field name");
        }
        ++plan->child_count;
      }
    }
  }

  plan->rule_count = next_rule;
  *out_plan = plan;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_validation_plan_compile(
    DataBind *codec, const char *type_name,
    DataBindValidationPlan **out_plan, DataBindError *error) {
  DataBindValidationCompileContext context = {0};
  size_t remaining_rules = DATA_BIND_VALIDATION_MAX_RULES;
  DataBindStatus status;

  if (out_plan != NULL) *out_plan = NULL;
  status = validation_plan_compile_internal(
      codec, type_name, out_plan, error, &context, &remaining_rules);
  if (status == DATA_BIND_OK) validation_error_clear(error);
  return status;
}

static DataBindStatus validation_numeric_value(
    const DataBindValidationRule *rule,
    const DataBindValue *value,
    int *comparison) {
  if (rule == NULL || value == NULL || comparison == NULL)
    return DATA_BIND_ERR_INVALID_ARG;

  if (rule->numeric_domain ==
      DATA_BIND_VALIDATION_NUMERIC_SIGNED) {
    int64_t actual;
    DataBindStatus status =
        data_bind_value_get_int64(value, &actual);
    if (status != DATA_BIND_OK) return status;
    *comparison =
        actual < rule->numeric.signed_value
            ? -1
            : (actual > rule->numeric.signed_value ? 1 : 0);
    return DATA_BIND_OK;
  }

  if (rule->numeric_domain ==
      DATA_BIND_VALIDATION_NUMERIC_UNSIGNED) {
    uint64_t actual;
    DataBindStatus status =
        data_bind_value_get_uint64(value, &actual);
    if (status != DATA_BIND_OK) return status;
    *comparison =
        actual < rule->numeric.unsigned_value
            ? -1
            : (actual > rule->numeric.unsigned_value ? 1 : 0);
    return DATA_BIND_OK;
  }

  if (rule->numeric_domain ==
      DATA_BIND_VALIDATION_NUMERIC_FLOAT) {
    double actual;
    DataBindStatus status =
        data_bind_value_get_double(value, &actual);
    if (status != DATA_BIND_OK || !isfinite(actual))
      return DATA_BIND_ERR_TYPE_MISMATCH;
    *comparison =
        actual < rule->numeric.float_value
            ? -1
            : (actual > rule->numeric.float_value ? 1 : 0);
    return DATA_BIND_OK;
  }

  return DATA_BIND_ERR_TYPE_MISMATCH;
}

static DataBindStatus validation_value_size(
    const DataBindValue *value, size_t *out_size) {
  DataBindValueKind kind;
  if (value == NULL || out_size == NULL)
    return DATA_BIND_ERR_INVALID_ARG;
  kind = data_bind_value_kind(value);

  if (kind == DATA_BIND_VALUE_STRING) {
    const char *text = NULL;
    return data_bind_value_get_string(
               value, &text, out_size);
  }
  if (kind == DATA_BIND_VALUE_BYTES) {
    const uint8_t *bytes = NULL;
    return data_bind_value_get_bytes(
               value, &bytes, out_size);
  }
  if (kind == DATA_BIND_VALUE_LIST ||
      kind == DATA_BIND_VALUE_SET ||
      kind == DATA_BIND_VALUE_MAP) {
    *out_size = data_bind_value_count(value);
    return DATA_BIND_OK;
  }
  return DATA_BIND_ERR_TYPE_MISMATCH;
}

static DataBindStatus validation_pattern_status_at(
    re_status_t status, DataBindError *error,
    const char *prefix, const DataBindValidationRule *rule);

static DataBindStatus validation_native_leaf_failure(
    const DataBindValidationPlan *plan,
    const DataBindValidationRule *rule,
    DataBindStatus status, DataBindError *error) {
  char path[260];
  if (plan == NULL || rule == NULL ||
      !validation_path_field(path, plan->type_name, rule->field_name))
    return validation_path_overflow(
        error, plan != NULL ? plan->type_name : NULL);
  return validation_error(
      error, status, path,
      status == DATA_BIND_ERR_SCHEMA
          ? "Native validation leaf is not admitted by the compiled contract"
          : "Native validation leaf could not be read");
}

static DataBindStatus validation_native_numeric(
    const DataBindValidationRule *rule, const cserde_token *token,
    int *comparison) {
  if (rule == NULL || token == NULL || comparison == NULL)
    return DATA_BIND_ERR_INVALID_ARG;

  if (rule->numeric_domain == DATA_BIND_VALIDATION_NUMERIC_SIGNED) {
    int64_t actual;
    if (token->kind != CSERDE_SINT) return DATA_BIND_ERR_TYPE_MISMATCH;
    actual = token->value.sint;
    *comparison =
        actual < rule->numeric.signed_value
            ? -1
            : (actual > rule->numeric.signed_value ? 1 : 0);
    return DATA_BIND_OK;
  }

  if (rule->numeric_domain == DATA_BIND_VALIDATION_NUMERIC_UNSIGNED) {
    uint64_t actual;
    if (token->kind != CSERDE_UINT) return DATA_BIND_ERR_TYPE_MISMATCH;
    actual = token->value.uint;
    *comparison =
        actual < rule->numeric.unsigned_value
            ? -1
            : (actual > rule->numeric.unsigned_value ? 1 : 0);
    return DATA_BIND_OK;
  }

  if (rule->numeric_domain == DATA_BIND_VALIDATION_NUMERIC_FLOAT) {
    double actual;
    if (token->kind != CSERDE_FLOAT ||
        !isfinite(token->value.floating))
      return DATA_BIND_ERR_TYPE_MISMATCH;
    actual = token->value.floating;
    *comparison =
        actual < rule->numeric.float_value
            ? -1
            : (actual > rule->numeric.float_value ? 1 : 0);
    return DATA_BIND_OK;
  }

  return DATA_BIND_ERR_TYPE_MISMATCH;
}

DataBindStatus data_bind_validation_plan_internal_validate_native_rule(
    const DataBindValidationPlan *plan, size_t rule_index,
    const cmeta_data_desc *data, const void *source, DataBindError *error) {
  const DataBindValidationRule *rule;
  cserde_token token = {0};
  DataBindStatus status;

  if (plan == NULL || data == NULL || source == NULL ||
      rule_index >= plan->rule_count)
    return validation_error(
        error, DATA_BIND_ERR_INVALID_ARG, NULL,
        "Invalid native ValidationPlan arguments");

  rule = &plan->rules[rule_index];
  if (data->kind != rule->field_kind)
    return validation_type_error_at(
        plan->type_name, rule, error,
        "Native value kind does not match compiled validation semantics");

  status = data_bind_native_leaf_token(data, source, &token);
  if (status != DATA_BIND_OK)
    return validation_native_leaf_failure(
        plan, rule, status, error);

  if (rule->kind == DATA_BIND_SCHEMA_CONSTRAINT_MIN ||
      rule->kind == DATA_BIND_SCHEMA_CONSTRAINT_MAX) {
    int comparison = 0;
    status = validation_native_numeric(rule, &token, &comparison);
    if (status != DATA_BIND_OK)
      return validation_type_error_at(
          plan->type_name, rule, error,
          "Native numeric value has the wrong runtime type");
    if ((rule->kind == DATA_BIND_SCHEMA_CONSTRAINT_MIN &&
         comparison < 0) ||
        (rule->kind == DATA_BIND_SCHEMA_CONSTRAINT_MAX &&
         comparison > 0))
      return validation_rule_error_at(
          plan->type_name, rule, error,
          rule->kind == DATA_BIND_SCHEMA_CONSTRAINT_MIN
              ? "Value is below @Min"
              : "Value exceeds @Max");
    validation_error_clear(error);
    return DATA_BIND_OK;
  }

  if (rule->kind == DATA_BIND_SCHEMA_CONSTRAINT_SIZE) {
    size_t actual;
    if (token.kind != CSERDE_STRING && token.kind != CSERDE_BYTES)
      return validation_type_error_at(
          plan->type_name, rule, error,
          "Native @Size requires a canonical string/bytes leaf");
    actual = token.value.slice.size;
    if ((rule->has_min && actual < rule->min_size) ||
        (rule->has_max && actual > rule->max_size))
      return validation_rule_error_at(
          plan->type_name, rule, error,
          "Value size violates @Size");
    validation_error_clear(error);
    return DATA_BIND_OK;
  }

  if (rule->kind == DATA_BIND_SCHEMA_CONSTRAINT_PATTERN) {
    re_match_result_t match = {0u, 0u};
    re_status_t regex_status;
    if (token.kind != CSERDE_STRING)
      return validation_type_error_at(
          plan->type_name, rule, error,
          "Native @Pattern requires a canonical string leaf");
    regex_status = re_matchn(
        rule->pattern, (const char *)token.value.slice.data,
        token.value.slice.size, NULL, &match);
    if (regex_status != RE_STATUS_OK)
      return validation_pattern_status_at(
          regex_status, error, plan->type_name, rule);
    if (match.index != 0u || match.length != token.value.slice.size)
      return validation_rule_error_at(
          plan->type_name, rule, error,
          "String does not fully satisfy @Pattern");
    validation_error_clear(error);
    return DATA_BIND_OK;
  }

  return validation_error(
      error, DATA_BIND_ERR_RUNTIME, plan->type_name,
      "ValidationPlan contains an unknown native rule");
}

static DataBindStatus validation_pattern_status_at(
    re_status_t status, DataBindError *error,
    const char *prefix, const DataBindValidationRule *rule) {
  char path[260];
  if (!validation_path_field(
          path, prefix, rule != NULL ? rule->field_name : NULL))
    return validation_path_overflow(error, prefix);
  if (status == RE_STATUS_NO_MATCH)
    return validation_error(
        error, DATA_BIND_ERR_VALIDATION, path,
        "String does not satisfy @Pattern");
  if (status == RE_STATUS_TEXT_LIMIT ||
      status == RE_STATUS_DEPTH_LIMIT ||
      status == RE_STATUS_STEP_LIMIT ||
      status == RE_STATUS_WORKSPACE_LIMIT)
    return validation_error(
        error, DATA_BIND_ERR_LIMIT, path,
        "Pattern evaluation exceeded bounded regex limits");
  return validation_error(
      error, DATA_BIND_ERR_RUNTIME, path,
      "Pattern evaluation failed");
}

static DataBindStatus validation_plan_validate_at(
    const DataBindValidationPlan *plan, const DataBindValue *value,
    const char *prefix, unsigned depth, DataBindError *error) {
  size_t i;

  if (plan == NULL || value == NULL || prefix == NULL)
    return validation_error(
        error, DATA_BIND_ERR_INVALID_ARG, prefix,
        "Invalid nested ValidationPlan execution arguments");
  if (depth > DATA_BIND_VALIDATION_MAX_DEPTH)
    return validation_error(
        error, DATA_BIND_ERR_LIMIT, prefix,
        "ValidationPlan execution depth exceeds the bounded limit");
  if (data_bind_value_kind(value) != DATA_BIND_VALUE_OBJECT)
    return validation_error(
        error, DATA_BIND_ERR_TYPE_MISMATCH, prefix,
        "ValidationPlan requires an object value");

  for (i = 0u; i < plan->rule_count; ++i) {
    const DataBindValidationRule *rule = &plan->rules[i];
    const DataBindValue *field =
        data_bind_value_get(value, rule->field_name);

    /* Presence/default/nullability have already normalized the contract.
     * ABSENT and explicit NULL are not ordinary value-constraint failures. */
    if (field == NULL ||
        data_bind_value_kind(field) == DATA_BIND_VALUE_NULL)
      continue;

    if (rule->kind == DATA_BIND_SCHEMA_CONSTRAINT_MIN ||
        rule->kind == DATA_BIND_SCHEMA_CONSTRAINT_MAX) {
      int comparison = 0;
      DataBindStatus status =
          validation_numeric_value(rule, field, &comparison);
      if (status != DATA_BIND_OK)
        return validation_type_error_at(
            prefix, rule, error,
            "Numeric constraint value has the wrong runtime type");
      if ((rule->kind == DATA_BIND_SCHEMA_CONSTRAINT_MIN &&
           comparison < 0) ||
          (rule->kind == DATA_BIND_SCHEMA_CONSTRAINT_MAX &&
           comparison > 0))
        return validation_rule_error_at(
            prefix, rule, error,
            rule->kind == DATA_BIND_SCHEMA_CONSTRAINT_MIN
                ? "Value is below @Min"
                : "Value exceeds @Max");
      continue;
    }

    if (rule->kind == DATA_BIND_SCHEMA_CONSTRAINT_SIZE) {
      size_t actual = 0u;
      DataBindStatus status =
          validation_value_size(field, &actual);
      if (status != DATA_BIND_OK)
        return validation_type_error_at(
            prefix, rule, error,
            "@Size value has the wrong runtime type");
      if ((rule->has_min && actual < rule->min_size) ||
          (rule->has_max && actual > rule->max_size))
        return validation_rule_error_at(
            prefix, rule, error,
            "Value size violates @Size");
      continue;
    }

    if (rule->kind == DATA_BIND_SCHEMA_CONSTRAINT_PATTERN) {
      const char *text = NULL;
      size_t length = 0u;
      re_match_result_t match = {0u, 0u};
      re_status_t status;

      if (data_bind_value_get_string(
              field, &text, &length) != DATA_BIND_OK)
        return validation_type_error_at(
            prefix, rule, error,
            "@Pattern value has the wrong runtime type");

      status = re_matchn(
          rule->pattern, text, length, NULL, &match);
      if (status != RE_STATUS_OK)
        return validation_pattern_status_at(
            status, error, prefix, rule);

      if (match.index != 0u || match.length != length)
        return validation_rule_error_at(
            prefix, rule, error,
            "String does not fully satisfy @Pattern");
      continue;
    }

    return validation_error(
        error, DATA_BIND_ERR_RUNTIME, prefix,
        "ValidationPlan contains an unknown rule");
  }

  for (i = 0u; i < plan->child_count; ++i) {
    const DataBindValidationChild *child = &plan->children[i];
    const DataBindValue *field =
        data_bind_value_get(value, child->field_name);
    char field_path[260];

    if (field == NULL ||
        data_bind_value_kind(field) == DATA_BIND_VALUE_NULL)
      continue;
    if (!validation_path_field(
            field_path, prefix, child->field_name))
      return validation_path_overflow(error, prefix);

    if (child->kind == DATA_BIND_VALIDATION_CHILD_OBJECT) {
      DataBindStatus status;
      if (data_bind_value_kind(field) != DATA_BIND_VALUE_OBJECT)
        return validation_error(
            error, DATA_BIND_ERR_TYPE_MISMATCH, field_path,
            "Nested ValidationPlan expected an object value");
      status = validation_plan_validate_at(
          child->plan, field, field_path, depth + 1u, error);
      if (status != DATA_BIND_OK) return status;
      continue;
    }

    if (child->kind == DATA_BIND_VALIDATION_CHILD_SEQUENCE ||
        child->kind == DATA_BIND_VALIDATION_CHILD_SET) {
      DataBindValueKind expected_kind =
          child->kind == DATA_BIND_VALIDATION_CHILD_SET
              ? DATA_BIND_VALUE_SET
              : DATA_BIND_VALUE_LIST;
      size_t count;
      size_t item_index;

      if (data_bind_value_kind(field) != expected_kind)
        return validation_error(
            error, DATA_BIND_ERR_TYPE_MISMATCH, field_path,
            child->kind == DATA_BIND_VALIDATION_CHILD_SET
                ? "Nested ValidationPlan expected a set value"
                : "Nested ValidationPlan expected a list value");

      count = data_bind_value_count(field);
      for (item_index = 0u; item_index < count; ++item_index) {
        const DataBindValue *item =
            data_bind_value_at(field, item_index);
        char item_path[260];
        DataBindStatus status;

        if (!validation_path_index(
                item_path, field_path, item_index))
          return validation_path_overflow(error, field_path);
        if (item == NULL ||
            data_bind_value_kind(item) != DATA_BIND_VALUE_OBJECT)
          return validation_error(
              error, DATA_BIND_ERR_TYPE_MISMATCH, item_path,
              "Nested ValidationPlan expected an object element");
        status = validation_plan_validate_at(
            child->plan, item, item_path, depth + 1u, error);
        if (status != DATA_BIND_OK) return status;
      }
      continue;
    }

    if (child->kind == DATA_BIND_VALIDATION_CHILD_MAP_VALUES) {
      size_t count;
      size_t item_index;

      if (data_bind_value_kind(field) != DATA_BIND_VALUE_MAP)
        return validation_error(
            error, DATA_BIND_ERR_TYPE_MISMATCH, field_path,
            "Nested ValidationPlan expected a map value");
      count = data_bind_value_count(field);
      for (item_index = 0u; item_index < count; ++item_index) {
        DataBindMapEntry entry =
            data_bind_value_map_entry_at(field, item_index);
        char item_path[260];
        DataBindStatus status;

        if (!validation_path_index(
                item_path, field_path, item_index))
          return validation_path_overflow(error, field_path);
        if (entry.value == NULL ||
            data_bind_value_kind(entry.value) != DATA_BIND_VALUE_OBJECT)
          return validation_error(
              error, DATA_BIND_ERR_TYPE_MISMATCH, item_path,
              "Nested ValidationPlan expected an object map value");
        status = validation_plan_validate_at(
            child->plan, entry.value, item_path, depth + 1u, error);
        if (status != DATA_BIND_OK) return status;
      }
      continue;
    }

    return validation_error(
        error, DATA_BIND_ERR_RUNTIME, field_path,
        "ValidationPlan contains an unknown nested binding");
  }

  return DATA_BIND_OK;
}

DataBindStatus data_bind_validation_plan_validate(
    const DataBindValidationPlan *plan,
    const DataBindValue *value, DataBindError *error) {
  DataBindStatus status;
  if (plan == NULL || value == NULL)
    return validation_error(
        error, DATA_BIND_ERR_INVALID_ARG, NULL,
        "Invalid ValidationPlan execution arguments");
  status = validation_plan_validate_at(
      plan, value, plan->type_name, 0u, error);
  if (status == DATA_BIND_OK) validation_error_clear(error);
  return status;
}
