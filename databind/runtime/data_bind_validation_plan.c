#include "data_bind_internal.h"

#include <errno.h>
#include <math.h>
#include <re.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct db_validation_numeric_limit {
  int has_i64;
  int has_u64;
  int has_f64;
  int64_t i64;
  uint64_t u64;
  double f64;
} db_validation_numeric_limit;

typedef struct db_validation_rule {
  char *field_name;
  DataBindConstraintKind kind;
  db_validation_numeric_limit numeric;
  int has_size_min;
  size_t size_min;
  int has_size_max;
  size_t size_max;
  re_t pattern;
} db_validation_rule;

struct DataBindValidationPlan {
  char *type_name;
  size_t rule_count;
  db_validation_rule *rules;
};

static DataBindStatus validation_error(
    DataBindError *error, DataBindStatus status, const char *path,
    const char *fmt, ...) {
  va_list ap;
  if (error != NULL && error->size >= sizeof(error->size)) {
    if (error->size >= offsetof(DataBindError, code) + sizeof(error->code))
      error->code = status;
    if (error->size >= offsetof(DataBindError, line) + sizeof(error->line))
      error->line = -1;
    if (error->size >= offsetof(DataBindError, column) + sizeof(error->column))
      error->column = -1;
    if (error->size >= offsetof(DataBindError, path) + sizeof(error->path))
      snprintf(error->path, sizeof(error->path), "%s", path != NULL ? path : "");
    if (error->size >= offsetof(DataBindError, message) + sizeof(error->message)) {
      va_start(ap, fmt);
      vsnprintf(error->message, sizeof(error->message), fmt, ap);
      va_end(ap);
    }
  }
  return status;
}

static void validation_error_clear(DataBindError *error) {
  if (error == NULL || error->size < sizeof(error->size)) return;
  if (error->size >= offsetof(DataBindError, code) + sizeof(error->code))
    error->code = DATA_BIND_OK;
  if (error->size >= offsetof(DataBindError, line) + sizeof(error->line))
    error->line = -1;
  if (error->size >= offsetof(DataBindError, column) + sizeof(error->column))
    error->column = -1;
  if (error->size >= offsetof(DataBindError, path) + sizeof(error->path))
    error->path[0] = '\0';
  if (error->size >= offsetof(DataBindError, message) + sizeof(error->message))
    error->message[0] = '\0';
}

static Node *constraint_child(Node *parent, const char *name) {
  size_t i;
  if (parent == NULL || name == NULL || parent->type != NODE_MAP) return NULL;
  for (i = 0u; i < parent->data.map.count; ++i) {
    Node *item = parent->data.map.items[i];
    if (item != NULL && item->name != NULL && strcmp(item->name, name) == 0)
      return item;
  }
  return NULL;
}

static const char *constraint_text(Node *parent, const char *name) {
  Node *child = constraint_child(parent, name);
  return child != NULL && child->type == NODE_STRING ? child->data.string_val : NULL;
}

static Node *constraint_list(DataBind *codec, const char *type_name, size_t field_index) {
  Node *field = data_bind_internal_schema_field_node(codec, type_name, field_index);
  Node *constraints;
  if (field == NULL) return NULL;
  constraints = constraint_child(field, "constraints");
  return constraints != NULL && constraints->type == NODE_LIST ? constraints : NULL;
}

static DataBindConstraintKind constraint_kind(const char *kind) {
  if (kind == NULL) return (DataBindConstraintKind)0;
  if (strcmp(kind, "min") == 0) return DATA_BIND_CONSTRAINT_MIN;
  if (strcmp(kind, "max") == 0) return DATA_BIND_CONSTRAINT_MAX;
  if (strcmp(kind, "size") == 0) return DATA_BIND_CONSTRAINT_SIZE;
  if (strcmp(kind, "pattern") == 0) return DATA_BIND_CONSTRAINT_PATTERN;
  return (DataBindConstraintKind)0;
}

static int parse_size_bound(const char *text, size_t *out) {
  const unsigned char *cursor;
  char *end = NULL;
  unsigned long long value;
  if (text == NULL || out == NULL || text[0] == '\0') return 0;
  for (cursor = (const unsigned char *)text; *cursor != '\0'; ++cursor)
    if (*cursor < '0' || *cursor > '9') return 0;
  errno = 0;
  value = strtoull(text, &end, 10);
  if (errno != 0 || end == text || end == NULL || *end != '\0' ||
      value > (unsigned long long)SIZE_MAX)
    return 0;
  *out = (size_t)value;
  return 1;
}

size_t data_bind_schema_field_constraint_count(
    DataBind *codec, const char *type_name, size_t field_index) {
  Node *constraints = constraint_list(codec, type_name, field_index);
  return constraints != NULL ? constraints->data.list.count : 0u;
}

int data_bind_schema_field_constraint_at(
    DataBind *codec, const char *type_name, size_t field_index,
    size_t constraint_index, DataBindSchemaConstraint *out) {
  Node *field;
  Node *constraints;
  Node *constraint;
  DataBindConstraintKind kind;
  const char *min_text;
  const char *max_text;
  size_t size_min = 0u;
  size_t size_max = 0u;
  size_t out_size;

  if (out == NULL) return 0;
  field = data_bind_internal_schema_field_node(codec, type_name, field_index);
  constraints = constraint_list(codec, type_name, field_index);
  if (field == NULL || constraints == NULL ||
      constraint_index >= constraints->data.list.count) {
    size_t requested = out->size;
    size_t clear_size =
        requested != 0u && requested < sizeof(*out) ? requested : sizeof(*out);
    memset(out, 0, clear_size);
    if (clear_size >= sizeof(size_t)) out->size = clear_size;
    return 0;
  }

  constraint = constraints->data.list.items[constraint_index];
  kind = constraint_kind(constraint_text(constraint, "kind"));
  if (kind == 0) return 0;

  min_text = constraint_text(constraint, "min");
  max_text = constraint_text(constraint, "max");
  if (min_text != NULL && !parse_size_bound(min_text, &size_min)) return 0;
  if (max_text != NULL && !parse_size_bound(max_text, &size_max)) return 0;

  out_size = out->size != 0u && out->size < sizeof(*out) ? out->size : sizeof(*out);
  memset(out, 0, out_size);
#define VALIDATION_REFLECT_SET(field_name, value_expr)                                      \
  do {                                                                                      \
    if (offsetof(DataBindSchemaConstraint, field_name) <= out_size &&                       \
        sizeof(out->field_name) <= out_size - offsetof(DataBindSchemaConstraint, field_name)) \
      out->field_name = (value_expr);                                                        \
  } while (0)
  VALIDATION_REFLECT_SET(size, out_size);
  VALIDATION_REFLECT_SET(kind, kind);
  VALIDATION_REFLECT_SET(field_name, constraint_text(field, "name"));
  VALIDATION_REFLECT_SET(numeric_value, constraint_text(constraint, "value"));
  VALIDATION_REFLECT_SET(has_size_min, min_text != NULL);
  VALIDATION_REFLECT_SET(size_min, size_min);
  VALIDATION_REFLECT_SET(has_size_max, max_text != NULL);
  VALIDATION_REFLECT_SET(size_max, size_max);
  VALIDATION_REFLECT_SET(pattern, constraint_text(constraint, "pattern"));
#undef VALIDATION_REFLECT_SET
  return 1;
}

static char *validation_strdup(const char *text) {
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

static int parse_numeric_limit(
    const char *text, db_validation_numeric_limit *out) {
  char *end = NULL;
  long long signed_value;
  unsigned long long unsigned_value;
  double float_value;

  if (text == NULL || out == NULL || text[0] == '\0') return 0;
  memset(out, 0, sizeof(*out));

  errno = 0;
  signed_value = strtoll(text, &end, 10);
  if (errno == 0 && end != text && end != NULL && *end == '\0') {
    out->has_i64 = 1;
    out->i64 = (int64_t)signed_value;
  }

  if (text[0] != '-') {
    errno = 0;
    end = NULL;
    unsigned_value = strtoull(text, &end, 10);
    if (errno == 0 && end != text && end != NULL && *end == '\0') {
      out->has_u64 = 1;
      out->u64 = (uint64_t)unsigned_value;
    }
  }

  errno = 0;
  end = NULL;
  float_value = strtod(text, &end);
  if (errno == 0 && end != text && end != NULL && *end == '\0' &&
      isfinite(float_value)) {
    out->has_f64 = 1;
    out->f64 = float_value;
  }

  return out->has_i64 || out->has_u64 || out->has_f64;
}

static void validation_rule_clear(db_validation_rule *rule) {
  if (rule == NULL) return;
  free(rule->field_name);
  re_destroy(rule->pattern);
  memset(rule, 0, sizeof(*rule));
}

void data_bind_validation_plan_free(DataBindValidationPlan *plan) {
  size_t i;
  if (plan == NULL) return;
  for (i = 0u; i < plan->rule_count; ++i)
    validation_rule_clear(&plan->rules[i]);
  free(plan->rules);
  free(plan->type_name);
  free(plan);
}

static DataBindStatus compile_error(
    DataBindError *error, DataBindStatus status, const char *type_name,
    const char *field_name, const char *message) {
  char path[260];
  snprintf(path, sizeof(path), "%s.%s",
           type_name != NULL ? type_name : "",
           field_name != NULL ? field_name : "");
  return validation_error(error, status, path, "%s", message);
}

DataBindStatus data_bind_validation_plan_compile(
    DataBind *codec, const char *type_name, DataBindValidationPlan **out_plan,
    DataBindError *error) {
  DataBindValidationPlan *plan = NULL;
  size_t field_count;
  size_t field_index;
  size_t rule_count = 0u;
  size_t next_rule = 0u;

  if (out_plan != NULL) *out_plan = NULL;
  if (codec == NULL || type_name == NULL || type_name[0] == '\0' ||
      out_plan == NULL)
    return validation_error(error, DATA_BIND_ERR_INVALID_ARG, NULL,
                            "Invalid ValidationPlan compile arguments");

  field_count = data_bind_schema_field_count(codec, type_name);
  if (field_count == 0u) {
    DataBindSchemaType type = DATA_BIND_SCHEMA_TYPE_INIT;
    if (!data_bind_schema_find_type(codec, type_name, &type))
      return validation_error(error, DATA_BIND_ERR_TYPE_NOT_FOUND, type_name,
                              "Validation type '%s' was not found", type_name);
  }

  for (field_index = 0u; field_index < field_count; ++field_index) {
    size_t count =
        data_bind_schema_field_constraint_count(codec, type_name, field_index);
    if (count > SIZE_MAX - rule_count)
      return validation_error(error, DATA_BIND_ERR_LIMIT, type_name,
                              "Validation rule count overflow");
    rule_count += count;
  }
  if (rule_count > SIZE_MAX / sizeof(db_validation_rule))
    return validation_error(error, DATA_BIND_ERR_LIMIT, type_name,
                            "Validation rule allocation overflow");

  plan = (DataBindValidationPlan *)calloc(1u, sizeof(*plan));
  if (plan == NULL)
    return validation_error(error, DATA_BIND_ERR_OOM, type_name,
                            "Unable to allocate ValidationPlan");
  plan->type_name = validation_strdup(type_name);
  if (plan->type_name == NULL) {
    data_bind_validation_plan_free(plan);
    return validation_error(error, DATA_BIND_ERR_OOM, type_name,
                            "Unable to copy ValidationPlan type identity");
  }
  if (rule_count != 0u) {
    plan->rules =
        (db_validation_rule *)calloc(rule_count, sizeof(*plan->rules));
    if (plan->rules == NULL) {
      data_bind_validation_plan_free(plan);
      return validation_error(error, DATA_BIND_ERR_OOM, type_name,
                              "Unable to allocate ValidationPlan rules");
    }
  }
  plan->rule_count = rule_count;

  for (field_index = 0u; field_index < field_count; ++field_index) {
    size_t count =
        data_bind_schema_field_constraint_count(codec, type_name, field_index);
    size_t constraint_index;
    for (constraint_index = 0u; constraint_index < count; ++constraint_index) {
      DataBindSchemaConstraint reflected = DATA_BIND_SCHEMA_CONSTRAINT_INIT;
      db_validation_rule *rule;
      re_status_t regex_status;

      if (!data_bind_schema_field_constraint_at(
              codec, type_name, field_index, constraint_index, &reflected)) {
        data_bind_validation_plan_free(plan);
        return validation_error(error, DATA_BIND_ERR_SCHEMA, type_name,
                                "Unable to reflect canonical Constraint IR");
      }
      if (next_rule >= rule_count) {
        data_bind_validation_plan_free(plan);
        return validation_error(error, DATA_BIND_ERR_SCHEMA, type_name,
                                "Constraint IR changed during plan compilation");
      }

      rule = &plan->rules[next_rule];
      rule->kind = reflected.kind;
      rule->field_name = validation_strdup(reflected.field_name);
      if (rule->field_name == NULL) {
        data_bind_validation_plan_free(plan);
        return validation_error(error, DATA_BIND_ERR_OOM, type_name,
                                "Unable to copy validation field identity");
      }

      if (rule->kind == DATA_BIND_CONSTRAINT_MIN ||
          rule->kind == DATA_BIND_CONSTRAINT_MAX) {
        if (!parse_numeric_limit(reflected.numeric_value, &rule->numeric)) {
          data_bind_validation_plan_free(plan);
          return compile_error(error, DATA_BIND_ERR_SCHEMA, type_name,
                               reflected.field_name,
                               "Canonical numeric constraint is not representable");
        }
      } else if (rule->kind == DATA_BIND_CONSTRAINT_SIZE) {
        rule->has_size_min = reflected.has_size_min;
        rule->size_min = reflected.size_min;
        rule->has_size_max = reflected.has_size_max;
        rule->size_max = reflected.size_max;
        if (!rule->has_size_min && !rule->has_size_max) {
          data_bind_validation_plan_free(plan);
          return compile_error(error, DATA_BIND_ERR_SCHEMA, type_name,
                               reflected.field_name,
                               "Canonical size constraint has no bound");
        }
      } else if (rule->kind == DATA_BIND_CONSTRAINT_PATTERN) {
        if (reflected.pattern == NULL) {
          data_bind_validation_plan_free(plan);
          return compile_error(error, DATA_BIND_ERR_SCHEMA, type_name,
                               reflected.field_name,
                               "Canonical pattern constraint has no pattern");
        }
        regex_status = re_compile_n(
            reflected.pattern, strlen(reflected.pattern), NULL, &rule->pattern);
        if (regex_status != RE_STATUS_OK) {
          DataBindStatus status =
              regex_status == RE_STATUS_NO_MEMORY ? DATA_BIND_ERR_OOM
                                                  : DATA_BIND_ERR_SCHEMA;
          data_bind_validation_plan_free(plan);
          return compile_error(error, status, type_name, reflected.field_name,
                               "Pattern constraint could not be compiled");
        }
      } else {
        data_bind_validation_plan_free(plan);
        return compile_error(error, DATA_BIND_ERR_SCHEMA, type_name,
                             reflected.field_name,
                             "Unknown canonical validation constraint");
      }
      ++next_rule;
    }
  }

  plan->rule_count = next_rule;
  *out_plan = plan;
  validation_error_clear(error);
  return DATA_BIND_OK;
}

size_t data_bind_validation_plan_rule_count(const DataBindValidationPlan *plan) {
  return plan != NULL ? plan->rule_count : 0u;
}

static int compare_signed(
    int64_t value, const db_validation_numeric_limit *limit) {
  if (limit->has_i64) {
    if (value < limit->i64) return -1;
    if (value > limit->i64) return 1;
    return 0;
  }
  if (limit->has_u64) {
    uint64_t converted;
    if (value < 0) return -1;
    converted = (uint64_t)value;
    if (converted < limit->u64) return -1;
    if (converted > limit->u64) return 1;
    return 0;
  }
  if ((double)value < limit->f64) return -1;
  if ((double)value > limit->f64) return 1;
  return 0;
}

static int compare_unsigned(
    uint64_t value, const db_validation_numeric_limit *limit) {
  if (limit->has_u64) {
    if (value < limit->u64) return -1;
    if (value > limit->u64) return 1;
    return 0;
  }
  if (limit->has_i64) {
    uint64_t converted;
    if (limit->i64 < 0) return 1;
    converted = (uint64_t)limit->i64;
    if (value < converted) return -1;
    if (value > converted) return 1;
    return 0;
  }
  if ((double)value < limit->f64) return -1;
  if ((double)value > limit->f64) return 1;
  return 0;
}

static int compare_number(
    const DataBindValue *value, const db_validation_numeric_limit *limit,
    int *out_compare) {
  if (value == NULL || limit == NULL || out_compare == NULL) return 0;
  switch (value->kind) {
  case DATA_BIND_VALUE_INT:
    *out_compare = compare_signed((int64_t)value->data.int_val, limit);
    return 1;
  case DATA_BIND_VALUE_INT64:
    *out_compare = compare_signed(value->data.int64_val, limit);
    return 1;
  case DATA_BIND_VALUE_UINT64:
    *out_compare = compare_unsigned(value->data.uint64_val, limit);
    return 1;
  case DATA_BIND_VALUE_DOUBLE:
    if (!limit->has_f64 || !isfinite(value->data.double_val)) return 0;
    if (value->data.double_val < limit->f64)
      *out_compare = -1;
    else if (value->data.double_val > limit->f64)
      *out_compare = 1;
    else
      *out_compare = 0;
    return 1;
  default:
    return 0;
  }
}

static int value_size(const DataBindValue *value, size_t *out_size) {
  if (value == NULL || out_size == NULL) return 0;
  switch (value->kind) {
  case DATA_BIND_VALUE_STRING:
    *out_size = value->data.string_val.len;
    return 1;
  case DATA_BIND_VALUE_BYTES:
    *out_size = value->data.bytes_val.len;
    return 1;
  case DATA_BIND_VALUE_LIST:
  case DATA_BIND_VALUE_SET:
  case DATA_BIND_VALUE_MAP:
    *out_size = data_bind_value_count(value);
    return 1;
  default:
    return 0;
  }
}

static DataBindStatus validation_fail(
    const DataBindValidationPlan *plan, const db_validation_rule *rule,
    DataBindError *error, const char *message) {
  char path[260];
  snprintf(path, sizeof(path), "%s.%s",
           plan != NULL && plan->type_name != NULL ? plan->type_name : "",
           rule != NULL && rule->field_name != NULL ? rule->field_name : "");
  return validation_error(error, DATA_BIND_ERR_VALIDATION, path, "%s", message);
}

DataBindStatus data_bind_validation_plan_validate(
    const DataBindValidationPlan *plan, const DataBindValue *value,
    DataBindError *error) {
  size_t i;
  if (plan == NULL || value == NULL)
    return validation_error(error, DATA_BIND_ERR_INVALID_ARG, NULL,
                            "Invalid ValidationPlan execution arguments");
  if (value->kind != DATA_BIND_VALUE_OBJECT)
    return validation_error(error, DATA_BIND_ERR_TYPE_MISMATCH, plan->type_name,
                            "ValidationPlan requires an object value");

  for (i = 0u; i < plan->rule_count; ++i) {
    const db_validation_rule *rule = &plan->rules[i];
    const DataBindValue *field = data_bind_value_get(value, rule->field_name);
    if (field == NULL || field->kind == DATA_BIND_VALUE_NULL) continue;

    if (rule->kind == DATA_BIND_CONSTRAINT_MIN ||
        rule->kind == DATA_BIND_CONSTRAINT_MAX) {
      int comparison = 0;
      if (!compare_number(field, &rule->numeric, &comparison))
        return validation_fail(plan, rule, error,
                               "Numeric constraint applied to a non-numeric value");
      if ((rule->kind == DATA_BIND_CONSTRAINT_MIN && comparison < 0) ||
          (rule->kind == DATA_BIND_CONSTRAINT_MAX && comparison > 0))
        return validation_fail(
            plan, rule, error,
            rule->kind == DATA_BIND_CONSTRAINT_MIN
                ? "Value is below the minimum"
                : "Value exceeds the maximum");
    } else if (rule->kind == DATA_BIND_CONSTRAINT_SIZE) {
      size_t actual = 0u;
      if (!value_size(field, &actual))
        return validation_fail(plan, rule, error,
                               "Size constraint applied to an unsupported value");
      if ((rule->has_size_min && actual < rule->size_min) ||
          (rule->has_size_max && actual > rule->size_max))
        return validation_fail(plan, rule, error,
                               "Value size is outside the allowed range");
    } else if (rule->kind == DATA_BIND_CONSTRAINT_PATTERN) {
      re_match_result_t match = {0};
      re_status_t status;
      if (field->kind != DATA_BIND_VALUE_STRING)
        return validation_fail(plan, rule, error,
                               "Pattern constraint requires a string value");
      status = re_matchn(rule->pattern, field->data.string_val.ptr,
                         field->data.string_val.len, NULL, &match);
      if (status != RE_STATUS_OK || match.index != 0u ||
          match.length != field->data.string_val.len)
        return validation_fail(plan, rule, error,
                               "String does not match the required pattern");
    }
  }

  validation_error_clear(error);
  return DATA_BIND_OK;
}
