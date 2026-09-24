#include "data_bind_validation_plan.h"

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

static DataBindStatus validation_type_error(
    const DataBindValidationPlan *plan,
    const DataBindValidationRule *rule,
    DataBindError *error, const char *message) {
  char path[260];
  validation_path(path,
                  plan != NULL ? plan->type_name : NULL,
                  rule != NULL ? rule->field_name : NULL);
  return validation_error(
      error, DATA_BIND_ERR_TYPE_MISMATCH, path, message);
}

static DataBindStatus validation_rule_error(
    const DataBindValidationPlan *plan,
    const DataBindValidationRule *rule,
    DataBindError *error, const char *message) {
  char path[260];
  validation_path(path,
                  plan != NULL ? plan->type_name : NULL,
                  rule != NULL ? rule->field_name : NULL);
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

DataBindStatus data_bind_validation_plan_compile(
    DataBind *codec, const char *type_name,
    DataBindValidationPlan **out_plan, DataBindError *error) {
  DataBindSchemaType reflected_type = DATA_BIND_SCHEMA_TYPE_INIT;
  DataBindValidationPlan *plan = NULL;
  size_t field_count;
  size_t rule_count = 0u;
  size_t field_index;
  size_t next_rule = 0u;

  if (out_plan != NULL) *out_plan = NULL;
  if (codec == NULL || type_name == NULL || type_name[0] == '\0' ||
      out_plan == NULL)
    return validation_error(
        error, DATA_BIND_ERR_INVALID_ARG, NULL,
        "Invalid ValidationPlan compile arguments");

  if (!data_bind_schema_find_type(codec, type_name, &reflected_type))
    return validation_error(
        error, DATA_BIND_ERR_TYPE_NOT_FOUND, type_name,
        "ValidationPlan type was not found");
  if (reflected_type.kind != DATA_BIND_SCHEMA_MESSAGE &&
      reflected_type.kind != DATA_BIND_SCHEMA_COMPOSITE &&
      reflected_type.kind != DATA_BIND_SCHEMA_GROUP)
    return validation_error(
        error, DATA_BIND_ERR_SCHEMA, type_name,
        "ValidationPlan requires a record type");

  field_count = data_bind_schema_field_count(codec, type_name);
  for (field_index = 0u; field_index < field_count; ++field_index) {
    size_t count =
        data_bind_schema_field_constraint_count(
            codec, type_name, field_index);
    if (count > DATA_BIND_VALIDATION_MAX_RULES - rule_count)
      return validation_error(
          error, DATA_BIND_ERR_LIMIT, type_name,
          "ValidationPlan rule count exceeds the bounded limit");
    rule_count += count;
  }

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
  plan->rule_count = rule_count;

  for (field_index = 0u; field_index < field_count; ++field_index) {
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    size_t constraint_count;
    size_t constraint_index;

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
  }

  plan->rule_count = next_rule;
  *out_plan = plan;
  validation_error_clear(error);
  return DATA_BIND_OK;
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

static DataBindStatus validation_pattern_status(
    re_status_t status, DataBindError *error,
    const DataBindValidationPlan *plan,
    const DataBindValidationRule *rule) {
  char path[260];
  validation_path(path, plan->type_name, rule->field_name);
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

DataBindStatus data_bind_validation_plan_validate(
    const DataBindValidationPlan *plan,
    const DataBindValue *value, DataBindError *error) {
  size_t i;

  if (plan == NULL || value == NULL)
    return validation_error(
        error, DATA_BIND_ERR_INVALID_ARG, NULL,
        "Invalid ValidationPlan execution arguments");
  if (data_bind_value_kind(value) != DATA_BIND_VALUE_OBJECT)
    return validation_error(
        error, DATA_BIND_ERR_TYPE_MISMATCH,
        plan->type_name,
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
        return validation_type_error(
            plan, rule, error,
            "Numeric constraint value has the wrong runtime type");
      if ((rule->kind == DATA_BIND_SCHEMA_CONSTRAINT_MIN &&
           comparison < 0) ||
          (rule->kind == DATA_BIND_SCHEMA_CONSTRAINT_MAX &&
           comparison > 0))
        return validation_rule_error(
            plan, rule, error,
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
        return validation_type_error(
            plan, rule, error,
            "@Size value has the wrong runtime type");
      if ((rule->has_min && actual < rule->min_size) ||
          (rule->has_max && actual > rule->max_size))
        return validation_rule_error(
            plan, rule, error,
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
        return validation_type_error(
            plan, rule, error,
            "@Pattern value has the wrong runtime type");

      status = re_matchn(
          rule->pattern, text, length, NULL, &match);
      if (status != RE_STATUS_OK)
        return validation_pattern_status(
            status, error, plan, rule);

      /* Java-style Pattern semantics: the entire logical string must match. */
      if (match.index != 0u || match.length != length)
        return validation_rule_error(
            plan, rule, error,
            "String does not fully satisfy @Pattern");
      continue;
    }

    return validation_error(
        error, DATA_BIND_ERR_RUNTIME, plan->type_name,
        "ValidationPlan contains an unknown rule");
  }

  validation_error_clear(error);
  return DATA_BIND_OK;
}
