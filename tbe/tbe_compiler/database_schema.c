#include "database_schema.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef enum database_integer_kind_e {
  DATABASE_INTEGER_NONE = 0,
  DATABASE_INTEGER_I8,
  DATABASE_INTEGER_U8,
  DATABASE_INTEGER_I16,
  DATABASE_INTEGER_U16,
  DATABASE_INTEGER_I32,
  DATABASE_INTEGER_U32,
  DATABASE_INTEGER_I64,
  DATABASE_INTEGER_U64
} database_integer_kind_t;

typedef struct database_string_builder_s {
  char *data;
  size_t length;
  size_t capacity;
} database_string_builder_t;

typedef struct database_primary_key_ref_s {
  size_t order;
  Node *column;
  const char *field_name;
} database_primary_key_ref_t;

enum {
  DATABASE_ANNOTATION_MESSAGE = 1u << 0,
  DATABASE_ANNOTATION_FIELD = 1u << 1
};

static const char *database_dialect_name(tbe_database_dialect_t dialect) {
  return dialect == TBE_DATABASE_DIALECT_POSTGRESQL ? "postgresql" : "sqlite";
}

static void database_set_diagnostic(tbe_database_schema_diagnostic_t *diagnostic,
                                    tbe_database_dialect_t dialect, const char *message_name,
                                    const char *field_name, const char *format, ...) {
  va_list arguments;

  if (!diagnostic) return;
  snprintf(diagnostic->dialect, sizeof(diagnostic->dialect), "%s", database_dialect_name(dialect));
  snprintf(diagnostic->message_name, sizeof(diagnostic->message_name), "%s",
           message_name && message_name[0] ? message_name : "<schema>");
  snprintf(diagnostic->field_name, sizeof(diagnostic->field_name), "%s",
           field_name && field_name[0] ? field_name : "<schema>");
  va_start(arguments, format);
  vsnprintf(diagnostic->context, sizeof(diagnostic->context), format, arguments);
  va_end(arguments);
}

static Node *database_find_child(const Node *parent, const char *name) {
  Node **items;
  size_t count;
  size_t index;

  if (!parent || !name) return NULL;
  if (parent->type == NODE_MAP) {
    items = parent->data.map.items;
    count = parent->data.map.count;
  } else if (parent->type == NODE_LIST) {
    items = parent->data.list.items;
    count = parent->data.list.count;
  } else {
    return NULL;
  }

  for (index = 0; index < count; ++index) {
    Node *child = items[index];
    if (child && child->name && strcmp(child->name, name) == 0) return child;
  }
  return NULL;
}

static const char *database_string_value(const Node *parent, const char *name) {
  Node *child = database_find_child(parent, name);
  return child && child->type == NODE_STRING ? child->data.string_val : NULL;
}

static const char *database_message_name(const Node *message) {
  const char *name = database_string_value(message, "message_name");
  return name ? name : database_string_value(message, "name");
}

static const char *database_diagnostic_text(const char *text) {
  return text && text[0] ? text : "<missing>";
}

static int database_has_child(const Node *parent, const char *name) {
  return database_find_child(parent, name) != NULL;
}

static Node *database_attributes(const Node *owner) {
  Node *attributes = database_find_child(owner, "attributes");
  return attributes && attributes->type == NODE_LIST ? attributes : NULL;
}

static size_t database_attribute_count(const Node *owner, const char *name) {
  Node *attributes = database_attributes(owner);
  size_t count = 0;
  size_t index;

  if (!attributes || !name) return 0;
  for (index = 0; index < attributes->data.list.count; ++index) {
    const char *attribute_name = database_string_value(attributes->data.list.items[index], "name");
    if (attribute_name && strcmp(attribute_name, name) == 0) ++count;
  }
  return count;
}

static const char *database_attribute_value(const Node *owner, const char *name) {
  Node *attributes = database_attributes(owner);
  size_t index;

  if (!attributes || !name) return NULL;
  for (index = 0; index < attributes->data.list.count; ++index) {
    Node *attribute = attributes->data.list.items[index];
    const char *attribute_name = database_string_value(attribute, "name");
    if (attribute_name && strcmp(attribute_name, name) == 0)
      return database_string_value(attribute, "value");
  }
  return NULL;
}

static int database_has_other_field_annotation(const Node *field, const char *allowed_name) {
  Node *attributes = database_attributes(field);
  size_t index;

  if (!attributes) return 0;
  for (index = 0; index < attributes->data.list.count; ++index) {
    const char *name = database_string_value(attributes->data.list.items[index], "name");
    if (name && strncmp(name, "db_", 3) == 0 && strcmp(name, allowed_name) != 0) return 1;
  }
  return 0;
}

static int database_annotation_allowed(const char *name, unsigned allowed_locations) {
  if (strcmp(name, "db_table") == 0) return (allowed_locations & DATABASE_ANNOTATION_MESSAGE) != 0;
  if (strcmp(name, "db_column") == 0 || strcmp(name, "db_primary_key") == 0 ||
      strcmp(name, "db_unique") == 0 || strcmp(name, "db_generated") == 0 ||
      strcmp(name, "db_ignore") == 0)
    return (allowed_locations & DATABASE_ANNOTATION_FIELD) != 0;
  return 0;
}

static int database_validate_db_annotations(const Node *owner, unsigned allowed_locations) {
  Node *attributes = database_attributes(owner);
  size_t index;

  if (!owner) return 0;
  if (!attributes) return 1;
  for (index = 0; index < attributes->data.list.count; ++index) {
    const char *name = database_string_value(attributes->data.list.items[index], "name");
    if (name && strncmp(name, "db_", 3) == 0 &&
        !database_annotation_allowed(name, allowed_locations))
      return 0;
  }
  return 1;
}

static int database_validate_message_field_annotations(const Node *message) {
  Node *fields = database_find_child(message, "fields");
  size_t index;

  if (!message || !fields || fields->type != NODE_LIST) return 0;
  for (index = 0; index < fields->data.list.count; ++index) {
    if (!database_validate_db_annotations(fields->data.list.items[index], DATABASE_ANNOTATION_FIELD))
      return 0;
  }
  return 1;
}

static int database_add_string(Node *map, const char *name, const char *value) {
  Node *child = create_node_string(name, value);
  if (!child) return -1;
  if (map_add(map, child) != 0) {
    node_free(child);
    return -1;
  }
  return 0;
}

static int database_add_node(Node *map, Node *child) {
  return child ? map_add(map, child) : -1;
}

static int database_string_builder_reserve(database_string_builder_t *builder, size_t extra) {
  size_t required;
  size_t new_capacity;
  char *new_data;

  if (!builder || extra > SIZE_MAX - builder->length - 1u) return 0;
  required = builder->length + extra + 1u;
  if (required <= builder->capacity) return 1;

  new_capacity = builder->capacity ? builder->capacity : 64u;
  while (new_capacity < required) {
    if (new_capacity > SIZE_MAX / 2u) {
      new_capacity = required;
      break;
    }
    new_capacity *= 2u;
  }
  new_data = (char *)realloc(builder->data, new_capacity);
  if (!new_data) return 0;
  builder->data = new_data;
  builder->capacity = new_capacity;
  return 1;
}

static int database_string_builder_append_n(database_string_builder_t *builder,
                                            const char *text, size_t length) {
  if (!text || !database_string_builder_reserve(builder, length)) return 0;
  memcpy(builder->data + builder->length, text, length);
  builder->length += length;
  builder->data[builder->length] = '\0';
  return 1;
}

static int database_string_builder_append(database_string_builder_t *builder, const char *text) {
  return text && database_string_builder_append_n(builder, text, strlen(text));
}

static int database_string_builder_append_token(database_string_builder_t *builder,
                                                const char *token) {
  if (builder->length && !database_string_builder_append(builder, " ")) return 0;
  return database_string_builder_append(builder, token);
}

static void database_string_builder_destroy(database_string_builder_t *builder) {
  if (!builder) return;
  free(builder->data);
  builder->data = NULL;
  builder->length = 0;
  builder->capacity = 0;
}

static char *database_quote_identifier(const char *identifier) {
  database_string_builder_t builder = {0};
  const char *cursor;

  if (!identifier || !identifier[0]) return NULL;
  if (!database_string_builder_append(&builder, "\"")) goto cleanup;
  for (cursor = identifier; *cursor; ++cursor) {
    if (*cursor == '\"' && !database_string_builder_append(&builder, "\"")) goto cleanup;
    if (!database_string_builder_append_n(&builder, cursor, 1)) goto cleanup;
  }
  if (!database_string_builder_append(&builder, "\"")) goto cleanup;
  return builder.data;

cleanup:
  database_string_builder_destroy(&builder);
  return NULL;
}

static int database_parse_positive_order(const char *text, size_t *out_order) {
  unsigned long long value;
  char *end = NULL;

  if (!text || !text[0] || text[0] == '-' || !out_order) return 0;
  errno = 0;
  value = strtoull(text, &end, 10);
  if (errno == ERANGE || !end || *end != '\0' || value == 0 || value > SIZE_MAX) return 0;
  *out_order = (size_t)value;
  return 1;
}

static database_integer_kind_t database_integer_kind(const char *type) {
  if (!type) return DATABASE_INTEGER_NONE;
  if (strcmp(type, "int8") == 0 || strcmp(type, "int8_t") == 0 || strcmp(type, "i8") == 0)
    return DATABASE_INTEGER_I8;
  if (strcmp(type, "uint8") == 0 || strcmp(type, "uint8_t") == 0 || strcmp(type, "u8") == 0 ||
      strcmp(type, "byte") == 0)
    return DATABASE_INTEGER_U8;
  if (strcmp(type, "int16") == 0 || strcmp(type, "int16_t") == 0 || strcmp(type, "i16") == 0)
    return DATABASE_INTEGER_I16;
  if (strcmp(type, "uint16") == 0 || strcmp(type, "uint16_t") == 0 || strcmp(type, "u16") == 0)
    return DATABASE_INTEGER_U16;
  if (strcmp(type, "int32") == 0 || strcmp(type, "int32_t") == 0 || strcmp(type, "i32") == 0)
    return DATABASE_INTEGER_I32;
  if (strcmp(type, "uint32") == 0 || strcmp(type, "uint32_t") == 0 || strcmp(type, "u32") == 0)
    return DATABASE_INTEGER_U32;
  if (strcmp(type, "int64") == 0 || strcmp(type, "int64_t") == 0 || strcmp(type, "i64") == 0)
    return DATABASE_INTEGER_I64;
  if (strcmp(type, "uint64") == 0 || strcmp(type, "uint64_t") == 0 || strcmp(type, "u64") == 0)
    return DATABASE_INTEGER_U64;
  return DATABASE_INTEGER_NONE;
}

static int database_integer_is_signed(database_integer_kind_t kind) {
  return kind == DATABASE_INTEGER_I8 || kind == DATABASE_INTEGER_I16 ||
         kind == DATABASE_INTEGER_I32 || kind == DATABASE_INTEGER_I64;
}

static Node *database_find_enum(const Node *schema_root, const char *name) {
  Node *enums = database_find_child(schema_root, "enums");
  size_t index;

  if (!enums || enums->type != NODE_LIST || !name) return NULL;
  for (index = 0; index < enums->data.list.count; ++index) {
    Node *enum_node = enums->data.list.items[index];
    const char *enum_name = database_string_value(enum_node, "enum_name");
    if (enum_name && strcmp(enum_name, name) == 0) return enum_node;
  }
  return NULL;
}

static tbe_database_schema_status_t database_map_type(const Node *schema_root,
                                                       const Node *field,
                                                       tbe_database_dialect_t dialect,
                                                       const char **out_sql_type,
                                                       database_integer_kind_t *out_integer_kind,
                                                       int *out_bool) {
  const char *type = database_string_value(field, "type");
  database_integer_kind_t integer_kind;
  Node *enum_node;

  if (!schema_root || !field || !out_sql_type || !out_integer_kind || !out_bool)
    return TBE_DATABASE_SCHEMA_STATUS_INVALID_ARGUMENT;
  if (database_has_child(field, "is_collection") || database_has_child(field, "is_group_field") ||
      database_has_child(field, "is_composite_ref"))
    return TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA;

  *out_bool = 0;
  *out_integer_kind = DATABASE_INTEGER_NONE;
  if (!type) return TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA;
  if (strcmp(type, "bool") == 0) {
    *out_bool = 1;
    *out_sql_type = dialect == TBE_DATABASE_DIALECT_SQLITE ? "INTEGER" : "boolean";
    return TBE_DATABASE_SCHEMA_STATUS_OK;
  }

  integer_kind = database_integer_kind(type);
  if (integer_kind == DATABASE_INTEGER_NONE) {
    enum_node = database_find_enum(schema_root, type);
    if (enum_node) {
      const char *underlying_type = database_string_value(enum_node, "underlying_type");
      integer_kind = database_integer_kind(underlying_type ? underlying_type : "int32");
    }
  }
  if (integer_kind != DATABASE_INTEGER_NONE) {
    *out_integer_kind = integer_kind;
    if (dialect == TBE_DATABASE_DIALECT_SQLITE) {
      *out_sql_type = integer_kind == DATABASE_INTEGER_U64 ? "NUMERIC" : "INTEGER";
    } else {
      switch (integer_kind) {
        case DATABASE_INTEGER_I8:
        case DATABASE_INTEGER_U8:
        case DATABASE_INTEGER_I16:
          *out_sql_type = "smallint";
          break;
        case DATABASE_INTEGER_U16:
        case DATABASE_INTEGER_I32:
          *out_sql_type = "integer";
          break;
        case DATABASE_INTEGER_U32:
        case DATABASE_INTEGER_I64:
          *out_sql_type = "bigint";
          break;
        case DATABASE_INTEGER_U64:
          *out_sql_type = "numeric(20,0)";
          break;
        default:
          return TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA;
      }
    }
    return TBE_DATABASE_SCHEMA_STATUS_OK;
  }

  if (strcmp(type, "float") == 0 || strcmp(type, "f32") == 0) {
    *out_sql_type = dialect == TBE_DATABASE_DIALECT_SQLITE ? "REAL" : "real";
  } else if (strcmp(type, "double") == 0 || strcmp(type, "f64") == 0) {
    *out_sql_type = dialect == TBE_DATABASE_DIALECT_SQLITE ? "REAL" : "double precision";
  } else if (strcmp(type, "string") == 0) {
    *out_sql_type = dialect == TBE_DATABASE_DIALECT_SQLITE ? "TEXT" : "text";
  } else if (strcmp(type, "bytes") == 0) {
    *out_sql_type = dialect == TBE_DATABASE_DIALECT_SQLITE ? "BLOB" : "bytea";
  } else if (strcmp(type, "uuid") == 0) {
    *out_sql_type = dialect == TBE_DATABASE_DIALECT_SQLITE ? "TEXT" : "uuid";
  } else {
    return TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA;
  }
  return TBE_DATABASE_SCHEMA_STATUS_OK;
}

static const char *database_unsigned_max(database_integer_kind_t kind) {
  switch (kind) {
    case DATABASE_INTEGER_U8: return "255";
    case DATABASE_INTEGER_U16: return "65535";
    case DATABASE_INTEGER_U32: return "4294967295";
    case DATABASE_INTEGER_U64: return "18446744073709551615";
    default: return NULL;
  }
}

static int database_append_range_constraint(database_string_builder_t *constraints,
                                            const char *sql_column_name,
                                            database_integer_kind_t integer_kind) {
  const char *maximum = database_unsigned_max(integer_kind);
  if (!maximum) return 1;
  return database_string_builder_append_token(constraints, "CHECK (") &&
         database_string_builder_append(constraints, sql_column_name) &&
         database_string_builder_append(constraints, " BETWEEN 0 AND ") &&
         database_string_builder_append(constraints, maximum) &&
         database_string_builder_append(constraints, ")");
}

static int database_parse_signed_default(const char *text, database_integer_kind_t kind,
                                         long long *out_value) {
  char *end = NULL;
  long long value;

  if (!text || !text[0] || isspace((unsigned char)text[0])) return 0;
  errno = 0;
  value = strtoll(text, &end, 0);
  if (errno == ERANGE || !end || end == text || *end != '\0') return 0;
  switch (kind) {
    case DATABASE_INTEGER_I8:
      if (value < INT8_MIN || value > INT8_MAX) return 0;
      break;
    case DATABASE_INTEGER_I16:
      if (value < INT16_MIN || value > INT16_MAX) return 0;
      break;
    case DATABASE_INTEGER_I32:
      if (value < INT32_MIN || value > INT32_MAX) return 0;
      break;
    case DATABASE_INTEGER_I64:
      break;
    default:
      return 0;
  }
  if (out_value) *out_value = value;
  return 1;
}

static int database_parse_unsigned_default(const char *text, database_integer_kind_t kind,
                                           unsigned long long *out_value) {
  char *end = NULL;
  unsigned long long value;

  if (!text || !text[0] || text[0] == '-' || isspace((unsigned char)text[0])) return 0;
  errno = 0;
  value = strtoull(text, &end, 0);
  if (errno == ERANGE || !end || end == text || *end != '\0') return 0;
  switch (kind) {
    case DATABASE_INTEGER_U8:
      if (value > UINT8_MAX) return 0;
      break;
    case DATABASE_INTEGER_U16:
      if (value > UINT16_MAX) return 0;
      break;
    case DATABASE_INTEGER_U32:
      if (value > UINT32_MAX) return 0;
      break;
    case DATABASE_INTEGER_U64:
      break;
    default:
      return 0;
  }
  if (out_value) *out_value = value;
  return 1;
}

static int database_valid_decimal_float(const char *text) {
  const char *cursor = text;
  int digits = 0;

  if (!cursor || !cursor[0] || isspace((unsigned char)cursor[0])) return 0;
  if (*cursor == '+' || *cursor == '-') ++cursor;
  while (*cursor >= '0' && *cursor <= '9') {
    ++digits;
    ++cursor;
  }
  if (*cursor == '.') {
    ++cursor;
    while (*cursor >= '0' && *cursor <= '9') {
      ++digits;
      ++cursor;
    }
  }
  if (digits == 0) return 0;
  if (*cursor == 'e' || *cursor == 'E') {
    int exponent_digits = 0;
    ++cursor;
    if (*cursor == '+' || *cursor == '-') ++cursor;
    while (*cursor >= '0' && *cursor <= '9') {
      ++exponent_digits;
      ++cursor;
    }
    if (exponent_digits == 0) return 0;
  }
  return *cursor == '\0';
}

static int database_parse_float_default(const char *text, int is_f32) {
  char *end = NULL;
  double value;

  if (!database_valid_decimal_float(text)) return 0;
  errno = 0;
  value = strtod(text, &end);
  if (errno == ERANGE || !end || *end != '\0' || !isfinite(value)) return 0;
  return !is_f32 || (value >= -(double)FLT_MAX && value <= (double)FLT_MAX);
}

static const char *database_enum_value(const Node *enum_node, const char *member_name) {
  Node *items = database_find_child(enum_node, "items");
  size_t index;

  if (!items || items->type != NODE_LIST || !member_name) return NULL;
  for (index = 0; index < items->data.list.count; ++index) {
    Node *item = items->data.list.items[index];
    const char *name = database_string_value(item, "name");
    if (name && strcmp(name, member_name) == 0) return database_string_value(item, "value");
  }
  return NULL;
}

static tbe_database_schema_status_t database_append_integer_default(
    database_string_builder_t *constraints, const char *value, database_integer_kind_t integer_kind) {
  char normalized[32];

  if (database_integer_is_signed(integer_kind)) {
    long long parsed;
    if (!database_parse_signed_default(value, integer_kind, &parsed))
      return TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA;
    snprintf(normalized, sizeof(normalized), "%lld", parsed);
  } else {
    unsigned long long parsed;
    if (!database_parse_unsigned_default(value, integer_kind, &parsed))
      return TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA;
    snprintf(normalized, sizeof(normalized), "%llu", parsed);
  }
  if (!database_string_builder_append_token(constraints, "DEFAULT") ||
      !database_string_builder_append_token(constraints, normalized))
    return TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
  return TBE_DATABASE_SCHEMA_STATUS_OK;
}

static tbe_database_schema_status_t database_append_default(
    database_string_builder_t *constraints, const Node *schema_root, const Node *field,
    tbe_database_dialect_t dialect, database_integer_kind_t integer_kind, int is_bool) {
  const char *type = database_string_value(field, "type");
  const char *value = database_string_value(field, "default_value");
  Node *enum_node;
  database_string_builder_t literal = {0};
  const char *cursor;
  tbe_database_schema_status_t status = TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA;

  if (!database_has_child(field, "has_default")) return TBE_DATABASE_SCHEMA_STATUS_OK;
  if (!type || !value) return TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA;
  enum_node = database_find_enum(schema_root, type);
  if (enum_node) {
    const char *enum_value = database_enum_value(enum_node, value);
    if (!enum_value) return TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA;
    status = database_append_integer_default(constraints, enum_value, integer_kind);
  } else if (is_bool) {
    const char *sql_bool;
    if (strcmp(value, "true") != 0 && strcmp(value, "false") != 0)
      return TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA;
    sql_bool = dialect == TBE_DATABASE_DIALECT_SQLITE
                   ? (strcmp(value, "true") == 0 ? "1" : "0")
                   : (strcmp(value, "true") == 0 ? "TRUE" : "FALSE");
    status = database_string_builder_append_token(constraints, "DEFAULT") &&
                     database_string_builder_append_token(constraints, sql_bool)
                 ? TBE_DATABASE_SCHEMA_STATUS_OK
                 : TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
  } else if (integer_kind != DATABASE_INTEGER_NONE) {
    status = database_append_integer_default(constraints, value, integer_kind);
  } else if (strcmp(type, "float") == 0 || strcmp(type, "f32") == 0 ||
             strcmp(type, "double") == 0 || strcmp(type, "f64") == 0) {
    if (!database_parse_float_default(value, strcmp(type, "float") == 0 || strcmp(type, "f32") == 0))
      return TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA;
    status = database_string_builder_append_token(constraints, "DEFAULT") &&
                     database_string_builder_append_token(constraints, value)
                 ? TBE_DATABASE_SCHEMA_STATUS_OK
                 : TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
  } else if (strcmp(type, "string") == 0) {
    if (!database_string_builder_append(&literal, "'")) {
      status = TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
      goto cleanup;
    }
    for (cursor = value; *cursor; ++cursor) {
      if (*cursor == '\'' && !database_string_builder_append(&literal, "'")) {
        status = TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
        goto cleanup;
      }
      if (!database_string_builder_append_n(&literal, cursor, 1)) {
        status = TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
        goto cleanup;
      }
    }
    if (!database_string_builder_append(&literal, "'")) {
      status = TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
      goto cleanup;
    }
    status = database_string_builder_append_token(constraints, "DEFAULT") &&
                     database_string_builder_append_token(constraints, literal.data)
                 ? TBE_DATABASE_SCHEMA_STATUS_OK
                 : TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
  } else {
    return TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA;
  }

cleanup:
  database_string_builder_destroy(&literal);
  return status;
}

static void database_sort_primary_keys(database_primary_key_ref_t *refs, size_t count) {
  size_t index;
  for (index = 1; index < count; ++index) {
    database_primary_key_ref_t current = refs[index];
    size_t position = index;
    while (position > 0 && refs[position - 1].order > current.order) {
      refs[position] = refs[position - 1];
      --position;
    }
    refs[position] = current;
  }
}

static int database_mark_last(Node *list) {
  Node *last;
  if (!list || list->type != NODE_LIST || list->data.list.count == 0) return 1;
  last = list->data.list.items[list->data.list.count - 1u];
  return database_add_string(last, "is_last", "1") == 0;
}

static tbe_database_schema_status_t database_build_table(
    const Node *schema_root, const Node *message, tbe_database_dialect_t dialect,
    Node **out_table, tbe_database_schema_diagnostic_t *diagnostic) {
  const char *table_name = database_attribute_value(message, "db_table");
  const char *message_name = database_message_name(message);
  Node *fields = database_find_child(message, "fields");
  Node *table = NULL;
  Node *columns = NULL;
  Node *primary_key_columns = NULL;
  database_primary_key_ref_t *primary_keys = NULL;
  const char **column_names = NULL;
  size_t primary_key_count = 0;
  size_t field_index;
  Node *identity_column = NULL;
  const char *identity_field_name = NULL;
  database_integer_kind_t identity_kind = DATABASE_INTEGER_NONE;
  tbe_database_schema_status_t status = TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA;
  char *sql_table_name = NULL;

  if (!out_table || !schema_root || !message || !table_name || !table_name[0] ||
      !fields || fields->type != NODE_LIST) {
    database_set_diagnostic(diagnostic, dialect, message_name, "<message>",
                            "annotation=db_table requires one non-empty value");
    return TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA;
  }
  *out_table = NULL;
  if (!database_validate_db_annotations(message, DATABASE_ANNOTATION_MESSAGE) ||
      database_attribute_count(message, "db_table") != 1u) {
    database_set_diagnostic(diagnostic, dialect, message_name, "<message>",
                            "annotation=db_table is invalid or duplicated");
    return TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA;
  }
  if (fields->data.list.count > SIZE_MAX / sizeof(*primary_keys) ||
      fields->data.list.count > SIZE_MAX / sizeof(*column_names))
    return TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;

  sql_table_name = database_quote_identifier(table_name);
  if (!sql_table_name) {
    status = TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
    goto cleanup;
  }
  table = create_node_map(NULL);
  columns = create_node_list("db_columns");
  primary_key_columns = create_node_list("db_primary_key_columns");
  primary_keys = fields->data.list.count ?
                     (database_primary_key_ref_t *)calloc(fields->data.list.count, sizeof(*primary_keys)) : NULL;
  column_names = fields->data.list.count ?
                     (const char **)calloc(fields->data.list.count, sizeof(*column_names)) : NULL;
  if (!table || !columns || !primary_key_columns ||
      (fields->data.list.count && (!primary_keys || !column_names))) {
    status = TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
    goto cleanup;
  }
  if (database_add_string(table, "sql_table_name", sql_table_name) != 0) {
    status = TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
    goto cleanup;
  }
  if (database_add_node(table, columns) != 0) {
    status = TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
    goto cleanup;
  }
  columns = NULL;
  if (database_add_node(table, primary_key_columns) != 0) {
    status = TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
    goto cleanup;
  }
  primary_key_columns = NULL;

  for (field_index = 0; field_index < fields->data.list.count; ++field_index) {
    const Node *field = fields->data.list.items[field_index];
    const char *field_name = database_string_value(field, "name");
    const char *column_name;
    const char *primary_key_order;
    const char *generated_value;
    const char *sql_type;
    database_integer_kind_t integer_kind;
    int is_bool;
    int is_primary_key;
    int is_generated;
    int is_unique;
    int is_optional;
    size_t order = 0;
    char *sql_column_name = NULL;
    Node *column = NULL;
    database_string_builder_t constraints = {0};
    size_t seen_index;

    if (!field || !field_name ||
        !database_validate_db_annotations(field, DATABASE_ANNOTATION_FIELD)) {
      database_set_diagnostic(diagnostic, dialect, message_name, field_name,
                              "annotation=db_* is invalid for a field");
      goto cleanup;
    }
    if (database_attribute_count(field, "db_ignore") > 1u ||
        (database_attribute_count(field, "db_ignore") == 1u &&
         (strcmp(database_attribute_value(field, "db_ignore"), "1") != 0 ||
          database_has_other_field_annotation(field, "db_ignore")))) {
      database_set_diagnostic(diagnostic, dialect, message_name, field_name,
                              "annotation=db_ignore must be 1 and cannot be combined");
      goto cleanup;
    }
    if (database_attribute_count(field, "db_ignore") == 1u) continue;

    if (database_attribute_count(field, "db_column") > 1u ||
        database_attribute_count(field, "db_primary_key") > 1u ||
        database_attribute_count(field, "db_unique") > 1u ||
        database_attribute_count(field, "db_generated") > 1u) {
      database_set_diagnostic(diagnostic, dialect, message_name, field_name,
                              "annotation=db_* is duplicated");
      goto cleanup;
    }
    column_name = database_attribute_value(field, "db_column");
    if (!column_name) column_name = field_name;
    if (!column_name[0]) {
      database_set_diagnostic(diagnostic, dialect, message_name, field_name,
                              "annotation=db_column requires one non-empty value");
      goto cleanup;
    }
    for (seen_index = 0; seen_index < field_index; ++seen_index) {
      if (column_names[seen_index] && strcmp(column_names[seen_index], column_name) == 0) {
        database_set_diagnostic(diagnostic, dialect, message_name, field_name,
                                "annotation=db_column duplicates column %s", column_name);
        goto cleanup;
      }
    }
    column_names[field_index] = column_name;

    primary_key_order = database_attribute_value(field, "db_primary_key");
    is_primary_key = primary_key_order != NULL;
    if (is_primary_key && !database_parse_positive_order(primary_key_order, &order)) {
      database_set_diagnostic(diagnostic, dialect, message_name, field_name,
                              "annotation=db_primary_key requires a positive order");
      goto cleanup;
    }
    generated_value = database_attribute_value(field, "db_generated");
    is_generated = generated_value != NULL;
    if (is_generated && strcmp(generated_value, "identity") != 0) {
      database_set_diagnostic(diagnostic, dialect, message_name, field_name,
                              "annotation=db_generated only accepts identity");
      goto cleanup;
    }
    is_unique = database_attribute_value(field, "db_unique") != NULL;
    if (is_unique && strcmp(database_attribute_value(field, "db_unique"), "1") != 0) {
      database_set_diagnostic(diagnostic, dialect, message_name, field_name,
                              "annotation=db_unique only accepts 1");
      goto cleanup;
    }
    is_optional = database_has_child(field, "is_optional");
    if ((is_primary_key || is_generated) && is_optional) {
      database_set_diagnostic(diagnostic, dialect, message_name, field_name,
                              "annotation=%s cannot be combined with optional",
                              is_generated ? "db_generated" : "db_primary_key");
      goto cleanup;
    }

    status = database_map_type(schema_root, field, dialect, &sql_type, &integer_kind, &is_bool);
    if (status != TBE_DATABASE_SCHEMA_STATUS_OK) {
      if (status == TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA)
        database_set_diagnostic(diagnostic, dialect, message_name, field_name,
                                "type=%s cannot be persisted without db_ignore(1)",
                                database_diagnostic_text(database_string_value(field, "type")));
      goto cleanup;
    }
    if (is_generated && (!is_primary_key || !database_integer_is_signed(integer_kind) || is_unique ||
                         database_has_child(field, "has_default"))) {
      status = TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA;
      database_set_diagnostic(diagnostic, dialect, message_name, field_name,
                              "annotation=db_generated requires one signed integer primary key without default or db_unique");
      goto cleanup;
    }

    sql_column_name = database_quote_identifier(column_name);
    column = create_node_map(NULL);
    if (!sql_column_name || !column) {
      free(sql_column_name);
      node_free(column);
      status = TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
      goto cleanup;
    }
    if ((!is_optional || is_primary_key) &&
        !database_string_builder_append_token(&constraints, "NOT NULL")) {
      status = TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
      goto field_cleanup;
    }
    if (is_generated) {
      if (!database_string_builder_append_token(&constraints, "PRIMARY KEY") ||
          !database_string_builder_append_token(
              &constraints, dialect == TBE_DATABASE_DIALECT_SQLITE
                                ? "AUTOINCREMENT"
                                : "GENERATED BY DEFAULT AS IDENTITY")) {
        status = TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
        goto field_cleanup;
      }
    }
    if (is_unique && !database_string_builder_append_token(&constraints, "UNIQUE")) {
      status = TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
      goto field_cleanup;
    }
    status = database_append_default(&constraints, schema_root, field, dialect, integer_kind, is_bool);
    if (status != TBE_DATABASE_SCHEMA_STATUS_OK) {
      if (status == TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA)
        database_set_diagnostic(diagnostic, dialect, message_name, field_name,
                                "type=%s default=%s is invalid",
                                database_diagnostic_text(database_string_value(field, "type")),
                                database_diagnostic_text(database_string_value(field, "default_value")));
      goto field_cleanup;
    }
    if (is_bool && dialect == TBE_DATABASE_DIALECT_SQLITE) {
      if (!database_string_builder_append_token(&constraints, "CHECK (") ||
          !database_string_builder_append(&constraints, sql_column_name) ||
          !database_string_builder_append(&constraints, " IN (0, 1))")) {
        status = TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
        goto field_cleanup;
      }
    }
    if (!database_append_range_constraint(&constraints, sql_column_name, integer_kind)) {
      status = TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
      goto field_cleanup;
    }
    if (database_add_string(column, "sql_column_name", sql_column_name) != 0 ||
        database_add_string(column, "sql_type", sql_type) != 0 ||
        database_add_string(column, "sql_constraints", constraints.data ? constraints.data : "") != 0 ||
        list_add(database_find_child(table, "db_columns"), column) != 0) {
      status = TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
      goto field_cleanup;
    }
    if (is_primary_key) {
      primary_keys[primary_key_count].order = order;
      primary_keys[primary_key_count].column = column;
      primary_keys[primary_key_count].field_name = field_name;
      ++primary_key_count;
    }
    if (is_generated) {
      identity_column = column;
      identity_field_name = field_name;
      identity_kind = integer_kind;
    }
    free(sql_column_name);
    database_string_builder_destroy(&constraints);
    continue;

field_cleanup:
    free(sql_column_name);
    database_string_builder_destroy(&constraints);
    node_free(column);
    goto cleanup;
  }

  if (database_find_child(table, "db_columns")->data.list.count == 0) {
    database_set_diagnostic(diagnostic, dialect, message_name, "<table>",
                            "annotation=db_ignore leaves no persisted columns");
    goto cleanup;
  }
  for (field_index = 0; field_index < primary_key_count; ++field_index) {
    size_t other_index;
    if (primary_keys[field_index].order > primary_key_count) {
      database_set_diagnostic(diagnostic, dialect, message_name,
                              primary_keys[field_index].field_name,
                              "annotation=db_primary_key order has a gap");
      goto cleanup;
    }
    for (other_index = field_index + 1; other_index < primary_key_count; ++other_index) {
      if (primary_keys[field_index].order == primary_keys[other_index].order) {
        database_set_diagnostic(diagnostic, dialect, message_name,
                                primary_keys[other_index].field_name,
                                "annotation=db_primary_key order is duplicated");
        goto cleanup;
      }
    }
  }
  if (identity_column && (primary_key_count != 1u || primary_keys[0].order != 1u ||
                          !database_integer_is_signed(identity_kind))) {
    database_set_diagnostic(diagnostic, dialect, message_name,
                            identity_field_name,
                            "annotation=db_generated requires a single primary key with order 1");
    goto cleanup;
  }
  database_sort_primary_keys(primary_keys, primary_key_count);
  for (field_index = 0; field_index < primary_key_count; ++field_index) {
    Node *primary_key_column = create_node_map(NULL);
    const char *sql_column_name = database_string_value(primary_keys[field_index].column, "sql_column_name");
    if (!primary_key_column || !sql_column_name ||
        database_add_string(primary_key_column, "sql_column_name", sql_column_name) != 0 ||
        list_add(database_find_child(table, "db_primary_key_columns"), primary_key_column) != 0) {
      node_free(primary_key_column);
      status = TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
      goto cleanup;
    }
  }
  if (primary_key_count > 1u && database_add_string(table, "has_composite_primary_key", "1") != 0) {
    status = TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
    goto cleanup;
  }
  if (!database_mark_last(database_find_child(table, "db_columns")) ||
      !database_mark_last(database_find_child(table, "db_primary_key_columns"))) {
    status = TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
    goto cleanup;
  }
  *out_table = table;
  table = NULL;
  status = TBE_DATABASE_SCHEMA_STATUS_OK;

cleanup:
  if (status == TBE_DATABASE_SCHEMA_STATUS_OK && *out_table == NULL)
    status = TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA;
  free(sql_table_name);
  free(primary_keys);
  free(column_names);
  node_free(columns);
  node_free(primary_key_columns);
  node_free(table);
  return status;
}

tbe_database_schema_status_t tbe_database_schema_build(
    const Node *schema_root, tbe_database_dialect_t dialect, Node **out_database_ir,
    tbe_database_schema_diagnostic_t *out_diagnostic) {
  Node *messages;
  Node *database_ir = NULL;
  Node *tables = NULL;
  const char **table_names = NULL;
  size_t table_count = 0;
  size_t message_index;
  tbe_database_schema_status_t status = TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA;

  database_set_diagnostic(out_diagnostic, dialect, "<schema>", "<schema>",
                          "schema validation did not complete");
  if (!out_database_ir || !schema_root || schema_root->type != NODE_MAP) {
    database_set_diagnostic(out_diagnostic, dialect, "<schema>", "<schema>",
                            "invalid database schema build arguments");
    return TBE_DATABASE_SCHEMA_STATUS_INVALID_ARGUMENT;
  }
  *out_database_ir = NULL;
  if (dialect != TBE_DATABASE_DIALECT_SQLITE && dialect != TBE_DATABASE_DIALECT_POSTGRESQL) {
    database_set_diagnostic(out_diagnostic, dialect, "<schema>", "<schema>",
                            "unsupported database dialect");
    return TBE_DATABASE_SCHEMA_STATUS_INVALID_ARGUMENT;
  }
  messages = database_find_child(schema_root, "messages");
  if (!messages || messages->type != NODE_LIST) {
    database_set_diagnostic(out_diagnostic, dialect, "<schema>", "<schema>",
                            "annotation=db_table requires a message list");
    return TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA;
  }
  if (messages->data.list.count > SIZE_MAX / sizeof(*table_names))
    return TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;

  database_ir = create_node_map(NULL);
  tables = create_node_list("db_tables");
  table_names = messages->data.list.count ?
                    (const char **)calloc(messages->data.list.count, sizeof(*table_names)) : NULL;
  if (!database_ir || !tables || (messages->data.list.count && !table_names)) {
    status = TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
    goto cleanup;
  }
  if (database_add_node(database_ir, tables) != 0) {
    status = TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
    goto cleanup;
  }
  tables = NULL;

  for (message_index = 0; message_index < messages->data.list.count; ++message_index) {
    const Node *message = messages->data.list.items[message_index];
    const char *table_name = database_attribute_value(message, "db_table");
    Node *table;
    size_t previous_index;

    if (!database_validate_db_annotations(message, DATABASE_ANNOTATION_MESSAGE) ||
        !database_validate_message_field_annotations(message)) {
      database_set_diagnostic(out_diagnostic, dialect, database_message_name(message), "<message>",
                              "annotation=db_* is invalid for this location");
      goto cleanup;
    }
    if (!table_name) continue;
    if (database_attribute_count(message, "db_table") != 1u || !table_name[0]) {
      database_set_diagnostic(out_diagnostic, dialect, database_message_name(message), "<message>",
                              "annotation=db_table requires one non-empty value");
      goto cleanup;
    }
    for (previous_index = 0; previous_index < table_count; ++previous_index) {
      if (strcmp(table_names[previous_index], table_name) == 0) {
        database_set_diagnostic(out_diagnostic, dialect, database_message_name(message), "<message>",
                                "annotation=db_table duplicates table %s", table_name);
        goto cleanup;
      }
    }
    status = database_build_table(schema_root, message, dialect, &table, out_diagnostic);
    if (status != TBE_DATABASE_SCHEMA_STATUS_OK) goto cleanup;
    if (list_add(database_find_child(database_ir, "db_tables"), table) != 0) {
      node_free(table);
      status = TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
      goto cleanup;
    }
    table_names[table_count++] = table_name;
  }
  if (table_count == 0) {
    database_set_diagnostic(out_diagnostic, dialect, "<schema>", "<schema>",
                            "annotation=db_table is required for database output");
    goto cleanup;
  }
  if (!database_mark_last(database_find_child(database_ir, "db_tables"))) {
    status = TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY;
    goto cleanup;
  }

  *out_database_ir = database_ir;
  database_ir = NULL;
  status = TBE_DATABASE_SCHEMA_STATUS_OK;

cleanup:
  if (status == TBE_DATABASE_SCHEMA_STATUS_OK && *out_database_ir == NULL)
    status = TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA;
  free(table_names);
  node_free(tables);
  node_free(database_ir);
  return status;
}

void tbe_database_schema_destroy(Node *database_ir) {
  node_free(database_ir);
}
