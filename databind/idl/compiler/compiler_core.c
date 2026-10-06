#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "compiler_core.h"

#include "database_schema.h"
#include "mustache.h"
#include "mustache_helpers.h"
#include "idl.h"
#include "idl_contract_internal.h"
#include "binary_contract_overlay.h"
#include "binary_reader_codegen.h"
#include "binary_layout_lowering.h"
#include "schema_cmeta.h"
#include <salts_cmeta_data.h>
#include <salts_cmeta_fixed_width.h>
#include "tbe_error.h"
#include "salts_fs.h"
#include "salts_uuid.h"
#include <tstr.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#define tbe_compiler_fdopen _fdopen
#define tbe_compiler_open _open
#define TBE_COMPILER_TEMP_OPEN_FLAGS (_O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY)
#define TBE_COMPILER_TEMP_OPEN_MODE (_S_IREAD | _S_IWRITE)
#else
#include <sys/stat.h>
#include <unistd.h>
#define tbe_compiler_fdopen fdopen
#define tbe_compiler_open open
#define TBE_COMPILER_TEMP_OPEN_FLAGS (O_WRONLY | O_CREAT | O_EXCL)
#define TBE_COMPILER_TEMP_OPEN_MODE 0666
#endif

#define TBE_COMPILER_TEMP_OUTPUT_ATTEMPTS 16u

static Node *tbe_compiler_find_child(Node *parent, const char *name) {
  if (!parent || !name) return NULL;

  if (parent->type == NODE_MAP) {
    for (size_t i = 0; i < parent->data.map.count; ++i) {
      Node *child = parent->data.map.items[i];
      if (child && child->name && strcmp(child->name, name) == 0) return child;
    }
  } else if (parent->type == NODE_LIST) {
    for (size_t i = 0; i < parent->data.list.count; ++i) {
      Node *child = parent->data.list.items[i];
      if (child && child->name && strcmp(child->name, name) == 0) return child;
    }
  }

  return NULL;
}

static const char *tbe_compiler_string_value(Node *parent, const char *name) {
  Node *child = tbe_compiler_find_child(parent, name);
  if (!child || child->type != NODE_STRING) return NULL;
  return child->data.string_val;
}

static int tbe_compiler_create_temporary_output(const char *output_path,
                                                char **out_temporary_path,
                                                FILE **out_file) {
  static const char temporary_prefix[] = ".tbe.";
  static const char temporary_suffix[] = ".tmp";
  const size_t output_length = strlen(output_path);
  const size_t prefix_length = sizeof(temporary_prefix) - 1u;
  const size_t suffix_length = sizeof(temporary_suffix);
  const size_t uuid_length = SALTS_UUID_STRING_LENGTH;
  size_t path_length;
  unsigned attempt;

  if (!out_temporary_path || !out_file || output_length >
      SIZE_MAX - prefix_length - uuid_length - suffix_length) {
    return -1;
  }
  *out_temporary_path = NULL;
  *out_file = NULL;
  path_length = output_length + prefix_length + uuid_length + suffix_length;

  for (attempt = 0; attempt < TBE_COMPILER_TEMP_OUTPUT_ATTEMPTS; ++attempt) {
    salts_uuid_t uuid;
    char uuid_text[SALTS_UUID_STRING_SIZE];
    char *temporary_path;
    FILE *file;
    int descriptor;

    if (salts_uuid_v4_generate(&uuid) != SALTS_OK ||
        salts_uuid_format(&uuid, uuid_text, sizeof(uuid_text)) != SALTS_OK) {
      return -1;
    }
    temporary_path = (char *)malloc(path_length);
    if (!temporary_path) return -1;
    memcpy(temporary_path, output_path, output_length);
    memcpy(temporary_path + output_length, temporary_prefix, prefix_length);
    memcpy(temporary_path + output_length + prefix_length, uuid_text, uuid_length);
    memcpy(temporary_path + output_length + prefix_length + uuid_length, temporary_suffix,
           suffix_length);

    descriptor = tbe_compiler_open(temporary_path, TBE_COMPILER_TEMP_OPEN_FLAGS,
                                   TBE_COMPILER_TEMP_OPEN_MODE);
    if (descriptor < 0) {
      free(temporary_path);
      if (errno == EEXIST) continue;
      return -1;
    }
    file = tbe_compiler_fdopen(descriptor, "wb");
    if (!file) {
#ifdef _WIN32
      _close(descriptor);
#else
      close(descriptor);
#endif
      salts_fs_unlink(temporary_path);
      free(temporary_path);
      return -1;
    }
    *out_temporary_path = temporary_path;
    *out_file = file;
    return 0;
  }
  return -1;
}

static int tbe_compiler_parse_size(const char *text, size_t *out) {
  char *end = NULL;
  unsigned long long value;

  if (!text || !text[0] || text[0] == '-' || !out) return 0;
  errno = 0;
  value = strtoull(text, &end, 10);
  if (errno == ERANGE || !end || *end != '\0' || value > SIZE_MAX) return 0;
  *out = (size_t)value;
  return 1;
}

static const char *tbe_compiler_attribute_value(Node *owner, const char *name) {
  Node *attributes;
  size_t i;

  if (!owner || !name) return NULL;
  attributes = tbe_compiler_find_child(owner, "attributes");
  if (!attributes || attributes->type != NODE_LIST) return NULL;
  for (i = 0; i < attributes->data.list.count; ++i) {
    Node *attribute = attributes->data.list.items[i];
    const char *attribute_name = tbe_compiler_string_value(attribute, "name");
    if (attribute_name && strcmp(attribute_name, name) == 0)
      return tbe_compiler_string_value(attribute, "value");
  }
  return NULL;
}

static size_t tbe_compiler_attribute_count(Node *owner, const char *name) {
  Node *attributes;
  size_t i, count = 0;
  if (!owner || !name) return 0;
  attributes = tbe_compiler_find_child(owner, "attributes");
  if (!attributes || attributes->type != NODE_LIST) return 0;
  for (i = 0; i < attributes->data.list.count; ++i) {
    Node *attribute = attributes->data.list.items[i];
    const char *attribute_name = tbe_compiler_string_value(attribute, "name");
    if (attribute_name && strcmp(attribute_name, name) == 0) ++count;
  }
  return count;
}

static int tbe_compiler_has_child(Node *parent, const char *name) {
  return tbe_compiler_find_child(parent, name) != NULL;
}

static void tbe_compiler_remove_children(Node *map, const char *name) {
  size_t out = 0;

  if (!map || map->type != NODE_MAP || !name) return;

  for (size_t i = 0; i < map->data.map.count; ++i) {
    Node *child = map->data.map.items[i];
    if (child && child->name && strcmp(child->name, name) == 0) {
      node_free(child);
      continue;
    }
    map->data.map.items[out++] = child;
  }

  map->data.map.count = out;
}

static int tbe_compiler_set_string(Node *map, const char *name, const char *value) {
  Node *node;

  if (!map || !name || !value) return -1;

  node = create_node_string(name, value);
  if (!node) return -1;

  tbe_compiler_remove_children(map, name);
  if (map_add(map, node) != 0) {
    node_free(node);
    return -1;
  }

  return 0;
}

typedef enum native_decode_cleanup_kind {
  NATIVE_DECODE_HEAP_FREE,
  NATIVE_DECODE_WORKSPACE_CLOSE,
  NATIVE_DECODE_FORMAT_PLAN_FREE,
  NATIVE_DECODE_READER_CLOSE,
  NATIVE_DECODE_VALUE_RESTORE
} native_decode_cleanup_kind;

typedef struct native_decode_cleanup_action {
  native_decode_cleanup_kind kind;
  const char *key;
  const char *live;
  const char *acquire_condition;
} native_decode_cleanup_action;

static tstr native_decode_cleanup_operation(
    native_decode_cleanup_kind kind, const char *schema_name) {
  tstr code = tstr_dup("");
  tstr appended;
  if (code == NULL) return NULL;
  switch (kind) {
  case NATIVE_DECODE_HEAP_FREE:
    appended = tstr_cat_fmt(code, "free(temporary_allocation);");
    break;
  case NATIVE_DECODE_WORKSPACE_CLOSE:
    appended = tstr_cat_fmt(code, "%s_message_workspace_close(&workspace);",
                            schema_name);
    break;
  case NATIVE_DECODE_FORMAT_PLAN_FREE:
    appended = tstr_cat_fmt(code, "data_bind_format_plan_free(format_plan);");
    break;
  case NATIVE_DECODE_READER_CLOSE:
    appended = tstr_cat_fmt(code, "(void)data_bind_format_reader_close(&reader);");
    break;
  case NATIVE_DECODE_VALUE_RESTORE:
    appended = tstr_cat_fmt(code,
        "(void)cmeta_data_value_restore_zero(binding.data, temporary);\n"
        "        memset(temporary, 0, object_size);");
    break;
  default:
    tstr_free(code);
    return NULL;
  }
  if (appended == NULL) tstr_free(code);
  return appended;
}

/*
 * Generation-only lexical ownership, ordered by acquisition. MessagePlan and
 * the canonical reader view are borrowed. A failed decode is rolled back by
 * MessagePlan; only its successful result transfers a live value here.
 */
static int tbe_compiler_prepare_native_text_cleanup(Node *root) {
  static const native_decode_cleanup_action actions[] = {
      {NATIVE_DECODE_HEAP_FREE, "heap", "temporary_allocation_live", NULL},
      {NATIVE_DECODE_WORKSPACE_CLOSE, "workspace", "workspace_live", NULL},
      {NATIVE_DECODE_FORMAT_PLAN_FREE, "format_plan", "format_plan_live", NULL},
      {NATIVE_DECODE_READER_CLOSE, "reader", "reader_live", NULL},
      {NATIVE_DECODE_VALUE_RESTORE, "value", "temporary_live",
       "status == DATA_BIND_OK"}};
  const size_t action_count = sizeof(actions) / sizeof(actions[0]);
  const char *schema_name = tbe_compiler_string_value(
      tbe_compiler_find_child(root, "schema"), "schema_name");
  Node *plan = NULL;
  Node *action_node = NULL;
  tstr declarations = NULL;
  tstr teardown = NULL;
  tstr operation = NULL;
  int result = -1;
  size_t index;
  enum { TRANSITION_CODE_BYTES = 128 };
  char transition[TRANSITION_CODE_BYTES];
  int written;

  if (schema_name == NULL || schema_name[0] == '\0') return -1;
  plan = create_node_map("native_text_cleanup");
  declarations = tstr_dup("");
  teardown = tstr_dup("");
  if (plan == NULL || declarations == NULL || teardown == NULL) goto cleanup;
  for (index = 0u; index < action_count; ++index) {
    const native_decode_cleanup_action *action = &actions[index];
    tstr appended = tstr_cat_fmt(declarations, "    int %s = 0;\n", action->live);
    if (appended == NULL) goto cleanup;
    declarations = appended;
    action_node = create_node_map(action->key);
    if (action_node == NULL) goto cleanup;
    written = action->acquire_condition != NULL
        ? snprintf(transition, sizeof(transition), "if (%s) %s = 1;",
                   action->acquire_condition, action->live)
        : snprintf(transition, sizeof(transition), "%s = 1;", action->live);
    if (written < 0 || (size_t)written >= sizeof(transition) ||
        tbe_compiler_set_string(action_node, "acquire", transition) != 0)
      goto cleanup;
    written = snprintf(transition, sizeof(transition), "%s = 0;", action->live);
    if (written < 0 || (size_t)written >= sizeof(transition) ||
        tbe_compiler_set_string(action_node, "release", transition) != 0 ||
        map_add(plan, action_node) != 0)
      goto cleanup;
    action_node = NULL;
  }
  for (index = action_count; index != 0u; --index) {
    const native_decode_cleanup_action *action = &actions[index - 1u];
    tstr appended;
    operation = native_decode_cleanup_operation(action->kind, schema_name);
    if (operation == NULL) goto cleanup;
    appended = tstr_cat_fmt(teardown,
        "    if (%s) {\n        %s = 0;\n        %s\n    }\n",
        action->live, action->live, operation);
    if (appended == NULL) goto cleanup;
    teardown = appended;
    tstr_free(operation);
    operation = NULL;
  }
  if (tbe_compiler_set_string(plan, "declarations", declarations) != 0 ||
      tbe_compiler_set_string(plan, "teardown", teardown) != 0)
    goto cleanup;
  tbe_compiler_remove_children(root, "native_text_cleanup");
  if (map_add(root, plan) != 0) goto cleanup;
  plan = NULL;
  result = 0;

cleanup:
  node_free(action_node);
  node_free(plan);
  tstr_free(operation);
  tstr_free(teardown);
  tstr_free(declarations);
  return result;
}

static void tbe_compiler_pascal_identifier(const char *input, char *out, size_t out_size) {
  int capitalize = 1;
  size_t pos = 0;

  if (!out || out_size == 0) return;

  if (!input || !input[0]) {
    snprintf(out, out_size, "Field");
    return;
  }

  for (size_t i = 0; input[i] && pos + 1 < out_size; ++i) {
    char c = input[i];
    int is_alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    int is_digit = c >= '0' && c <= '9';

    if (!is_alpha && !is_digit) {
      capitalize = 1;
      continue;
    }

    if (pos == 0 && is_digit && pos + 1 < out_size) {
      out[pos++] = 'F';
    }

    if (capitalize && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    out[pos++] = c;
    capitalize = 0;
  }

  if (pos == 0) out[pos++] = 'F';
  out[pos] = '\0';
}

static int tbe_compiler_c_identifier_valid(const char *identifier) {
  static const char *const keywords[] = {
      "auto",     "break",   "case",     "char",    "const",    "continue",
      "default",  "do",      "double",   "else",    "enum",     "extern",
      "float",    "for",     "goto",     "if",      "inline",   "int",
      "long",     "register", "restrict", "return",  "short",    "signed",
      "sizeof",   "static",  "struct",   "switch",  "typedef",  "union",
      "unsigned", "void",    "volatile", "while",   "_Alignas", "_Alignof",
      "_Atomic",  "_Bool",   "_Complex", "_Generic", "_Imaginary", "_Noreturn",
      "_Static_assert", "_Thread_local"};
  size_t i;
  if (identifier == NULL ||
      !((identifier[0] >= 'A' && identifier[0] <= 'Z') ||
        (identifier[0] >= 'a' && identifier[0] <= 'z') || identifier[0] == '_'))
    return 0;
  for (i = 1; identifier[i] != '\0'; ++i)
    if (!((identifier[i] >= 'A' && identifier[i] <= 'Z') ||
          (identifier[i] >= 'a' && identifier[i] <= 'z') ||
          (identifier[i] >= '0' && identifier[i] <= '9') || identifier[i] == '_'))
      return 0;
  for (i = 0; i < sizeof(keywords) / sizeof(keywords[0]); ++i)
    if (strcmp(identifier, keywords[i]) == 0) return 0;
  return 1;
}

/* Backend spellings are compiler metadata. Scalar aliases and native identity
 * are resolved through the same canonical descriptors as parser/runtime. */
typedef struct tbe_compiler_scalar_projection {
  const cmeta_data_desc *data;
  const char *c_type;
  const char *cpp_type;
  const char *go_type;
  const char *rust_type;
  const char *ts_type;
  const char *python_type;
  const char *rfl_type;
  const char *native_data_symbol;
  const char *native_type_symbol;
} tbe_compiler_scalar_projection_t;

typedef enum tbe_compiler_native_requirement {
  TBE_COMPILER_NATIVE_FIXED_VALUE,
  TBE_COMPILER_NATIVE_ENUM_DOMAIN,
  DATABIND_COMPILER_NATIVE_MAP_PROVIDER,
  DATABIND_COMPILER_NATIVE_SEQUENCE_PROVIDER,
  DATABIND_COMPILER_NATIVE_SET_PROVIDER,
  TBE_COMPILER_NATIVE_OWNED_LIFECYCLE,
  TBE_COMPILER_NATIVE_OVERLAY_PRESENCE,
  TBE_COMPILER_NATIVE_OVERLAY_NULL,
  TBE_COMPILER_NATIVE_OVERLAY_PRESENCE_NULL,
  TBE_COMPILER_NATIVE_DEFERRED_CONTAINER
} tbe_compiler_native_requirement_t;

static const char *tbe_compiler_native_requirement_name(
    tbe_compiler_native_requirement_t requirement) {
  switch (requirement) {
    case TBE_COMPILER_NATIVE_FIXED_VALUE:
      return "fixed_value";
    case TBE_COMPILER_NATIVE_ENUM_DOMAIN:
      return "enum_domain";
    case DATABIND_COMPILER_NATIVE_MAP_PROVIDER:
      return "map_provider";
    case DATABIND_COMPILER_NATIVE_SEQUENCE_PROVIDER:
      return "sequence_provider";
    case DATABIND_COMPILER_NATIVE_SET_PROVIDER:
      return "set_provider";
    case TBE_COMPILER_NATIVE_OWNED_LIFECYCLE:
      return "owned_lifecycle";
    case TBE_COMPILER_NATIVE_OVERLAY_PRESENCE:
      return "overlay_presence";
    case TBE_COMPILER_NATIVE_OVERLAY_NULL:
      return "overlay_null";
    case TBE_COMPILER_NATIVE_OVERLAY_PRESENCE_NULL:
      return "overlay_presence_null";
    case TBE_COMPILER_NATIVE_DEFERRED_CONTAINER:
      return "deferred_container";
  }
  return NULL;
}

/* Native symbol spellings project the same canonical records into generated C. */
static const tbe_compiler_scalar_projection_t TBE_COMPILER_SCALAR_PROJECTIONS[] = {
    {&cmeta_data_bool, "uint8_t", "bool", "bool", "bool", "boolean", "bool", "boolean", "salts_bool8_cmeta_data", "salts_bool8_cmeta_type"},
    {&cmeta_data_int8, "int8_t", "std::int8_t", "int8", "i8", "number", "int", "int", "cmeta_data_int8", "cmeta_type_int8"},
    {&cmeta_data_uint8, "uint8_t", "std::uint8_t", "uint8", "u8", "number", "int", "int", "cmeta_data_uint8", "cmeta_type_uint8"},
    {&cmeta_data_int16, "int16_t", "std::int16_t", "int16", "i16", "number", "int", "int", "cmeta_data_int16", "cmeta_type_int16"},
    {&cmeta_data_uint16, "uint16_t", "std::uint16_t", "uint16", "u16", "number", "int", "int", "cmeta_data_uint16", "cmeta_type_uint16"},
    {&cmeta_data_int32, "int32_t", "std::int32_t", "int32", "i32", "number", "int", "int", "cmeta_data_int32", "cmeta_type_int32"},
    {&cmeta_data_uint32, "uint32_t", "std::uint32_t", "uint32", "u32", "number", "int", "int", "cmeta_data_uint32", "cmeta_type_uint32"},
    {&cmeta_data_int64, "int64_t", "std::int64_t", "int64", "i64", "number", "int", "long", "cmeta_data_int64", "cmeta_type_int64"},
    {&cmeta_data_uint64, "uint64_t", "std::uint64_t", "uint64", "u64", "number", "int", "uint64", "cmeta_data_uint64", "cmeta_type_uint64"},
    {&cmeta_data_float, "float", "float", "float32", "f32", "number", "float", "float", "cmeta_data_float", "cmeta_type_float"},
    {&cmeta_data_double, "double", "double", "float64", "f64", "number", "float", "double", "cmeta_data_double", "cmeta_type_double"},
};

static const tbe_compiler_scalar_projection_t *tbe_compiler_scalar_projection(const char *type) {
  const cmeta_data_desc *data = schema_cmeta_builtin_data(type);
  size_t i;
  if (!cmeta_data_desc_valid(data)) return NULL;
  for (i = 0; i < sizeof(TBE_COMPILER_SCALAR_PROJECTIONS) /
                          sizeof(TBE_COMPILER_SCALAR_PROJECTIONS[0]);
       ++i) {
    const tbe_compiler_scalar_projection_t *projection = &TBE_COMPILER_SCALAR_PROJECTIONS[i];
    if (cmeta_data_desc_equal(data, projection->data))
      return projection;
  }
  return NULL;
}

static const tbe_compiler_scalar_projection_t *tbe_compiler_integer_type(const char *type) {
  const tbe_compiler_scalar_projection_t *projection = tbe_compiler_scalar_projection(type);
  return projection != NULL &&
                 (projection->data->kind == CMETA_DATA_SINT ||
                  projection->data->kind == CMETA_DATA_UINT)
             ? projection : NULL;
}

static const char *tbe_compiler_cpp_scalar_type(const char *type) {
  const tbe_compiler_scalar_projection_t *projection;
  if (!type) return "std::any";
  projection = tbe_compiler_scalar_projection(type);
  if (projection) return projection->cpp_type;
  if (strcmp(type, "string") == 0) return "std::string";
  if (strcmp(type, "bytes") == 0) return "std::vector<std::uint8_t>";
  if (strcmp(type, "uuid") == 0) return "salts_uuid_t";
  return type;
}

static const char *tbe_compiler_go_scalar_type(const char *type) {
  const tbe_compiler_scalar_projection_t *projection;
  if (!type) return "any";
  projection = tbe_compiler_scalar_projection(type);
  if (projection) return projection->go_type;
  if (strcmp(type, "string") == 0) return "string";
  if (strcmp(type, "bytes") == 0) return "[]byte";
  if (strcmp(type, "uuid") == 0) return "[16]byte";
  return type;
}

static const char *tbe_compiler_ts_scalar_type(const char *type) {
  const tbe_compiler_scalar_projection_t *projection;
  if (!type) return "unknown";
  projection = tbe_compiler_scalar_projection(type);
  if (projection) return projection->ts_type;
  if (strcmp(type, "string") == 0) return "string";
  if (strcmp(type, "bytes") == 0) return "Uint8Array";
  if (strcmp(type, "uuid") == 0) return "string";
  return type;
}

static const char *tbe_compiler_python_scalar_type(const char *type) {
  const tbe_compiler_scalar_projection_t *projection;
  if (!type) return "Any";
  projection = tbe_compiler_scalar_projection(type);
  if (projection) return projection->python_type;
  if (strcmp(type, "string") == 0) return "str";
  if (strcmp(type, "bytes") == 0) return "bytes";
  if (strcmp(type, "uuid") == 0) return "str";
  return type;
}

static const char *tbe_compiler_rust_scalar_type(const char *type) {
  const tbe_compiler_scalar_projection_t *projection;
  if (!type) return "()";
  projection = tbe_compiler_scalar_projection(type);
  if (projection) return projection->rust_type;
  if (strcmp(type, "string") == 0) return "String";
  if (strcmp(type, "bytes") == 0) return "Vec<u8>";
  if (strcmp(type, "uuid") == 0) return "[u8; 16]";
  return type;
}

static const char *tbe_compiler_rfl_scalar_type(const char *type) {
  const tbe_compiler_scalar_projection_t *projection;
  if (!type) return "Object";
  projection = tbe_compiler_scalar_projection(type);
  if (projection) return projection->rfl_type;
  if (strcmp(type, "string") == 0) return "String";
  if (strcmp(type, "bytes") == 0) return "Bytes";
  if (strcmp(type, "uuid") == 0) return "UUID";
  return type;
}

static const char *tbe_compiler_typed_c_scalar(const char *type) {
  const tbe_compiler_scalar_projection_t *projection;
  if (!type) return NULL;
  projection = tbe_compiler_scalar_projection(type);
  if (projection) return projection->c_type;
  if (strcmp(type, "uuid") == 0) return "salts_uuid_t";
  return NULL;
}

static Node *tbe_compiler_find_record(Node *root, const char *list_name, const char *name) {
  Node *list = tbe_compiler_find_child(root, list_name);
  size_t i;
  if (!list || list->type != NODE_LIST || !name) return NULL;
  for (i = 0; i < list->data.list.count; ++i) {
    Node *record = list->data.list.items[i];
    const char *record_name = tbe_compiler_string_value(record, "name");
    if (record_name && strcmp(record_name, name) == 0) return record;
  }
  return NULL;
}

/* Resolve C storage spelling without introducing a second type taxonomy. */
static int tbe_compiler_native_named_c_type(Node *root, const char *type,
                                           char *c_type, size_t c_type_size) {
  const char *scalar = tbe_compiler_typed_c_scalar(type);
  int written;
  if (!type) return 0;
  if (strcmp(type, "string") == 0) scalar = "tstr";
  if (strcmp(type, "bytes") == 0) scalar = "tbe_bytes_t";
  if (scalar) {
    written = snprintf(c_type, c_type_size, "%s", scalar);
  } else if (tbe_compiler_find_record(root, "enums", type) ||
             tbe_compiler_find_record(root, "composites", type) ||
             tbe_compiler_find_record(root, "groups", type) ||
             tbe_compiler_find_record(root, "messages", type)) {
    written = snprintf(c_type, c_type_size, "%s_t", type);
  } else {
    return 0;
  }
  return written >= 0 && (size_t)written < c_type_size;
}

static const char *tbe_compiler_cpp_enum_underlying_type(const char *type) {
  return tbe_compiler_cpp_scalar_type((type && type[0]) ? type : "int32");
}

static const char *tbe_compiler_rust_enum_underlying_type(const char *type) {
  return tbe_compiler_rust_scalar_type((type && type[0]) ? type : "int32");
}

typedef const char *(*tbe_scalar_mapper_t)(const char *);

static void tbe_compiler_field_type(Node *field,
                                    const schema_cmeta_field_type *semantic,
                                    tbe_scalar_mapper_t scalar_mapper,
                                    const char *list_prefix,
                                    const char *list_suffix,
                                    const char *set_prefix,
                                    const char *set_suffix,
                                    const char *map_prefix,
                                    const char *map_separator,
                                    const char *map_suffix,
                                    char *out,
                                    size_t out_size) {
  const char *type = tbe_compiler_string_value(field, "type");

  if (!out || out_size == 0) return;

  if (semantic && semantic->kind == CMETA_DATA_SEQUENCE &&
      strcmp(semantic->schema_kind, "group") == 0) {
    const char *group_type = tbe_compiler_string_value(field, "group_type");
    snprintf(out, out_size, "%s%s%s", list_prefix, group_type ? group_type : "unknown", list_suffix);
    return;
  }

  if (semantic && cmeta_data_kind_is_container(semantic->kind)) {
    const char *inner_type = tbe_compiler_string_value(field, "inner_type");
    const char *key_type = tbe_compiler_string_value(field, "key_type");
    const char *value_type = tbe_compiler_string_value(field, "value_type");

    if (semantic->kind == CMETA_DATA_MAP) {
      snprintf(out, out_size, "%s%s%s%s%s", map_prefix,
               scalar_mapper(key_type ? key_type : "string"),
               map_separator,
               scalar_mapper(value_type ? value_type : inner_type),
               map_suffix);
    } else if (semantic->kind == CMETA_DATA_SET) {
      snprintf(out, out_size, "%s%s%s", set_prefix,
               scalar_mapper(inner_type ? inner_type : "unknown"),
               set_suffix);
    } else {
      snprintf(out, out_size, "%s%s%s", list_prefix,
               scalar_mapper(inner_type ? inner_type : "unknown"),
               list_suffix);
    }
    return;
  }

  if (semantic && semantic->kind == CMETA_DATA_BYTES &&
      tbe_compiler_has_child(field, "is_fixed_size")) {
    snprintf(out, out_size, "%s", scalar_mapper("bytes"));
    return;
  }

  snprintf(out, out_size, "%s", scalar_mapper(type));
}

/* Every public enum item/typedef/wrapper contains an underscore. These private
 * identifiers contain none, so even adversarial legal item names cannot be
 * macros for them. Length-delimited hex is injective across enum identifiers;
 * dynamic sizing also prevents long names from aliasing through truncation. */
static int tbe_compiler_set_enum_symbol(Node *target, const char *key,
                                         const char *name, const char *role) {
  static const char prefix[] = "tbeCmetaEnum";
  static const char hex[] = "0123456789abcdef";
  const size_t extra = sizeof(prefix) + 3u * sizeof(size_t) + 1u + strlen(role);
  size_t length;
  size_t capacity;
  size_t offset;
  size_t i;
  int written;
  int status;
  char *symbol;
  if (name == NULL) return -1;
  length = strlen(name);
  if (length > (SIZE_MAX - extra) / 2u) return -1;
  capacity = extra + 2u * length;
  symbol = (char *)malloc(capacity);
  if (symbol == NULL) return -1;
  written = snprintf(symbol, capacity, "%s%zux", prefix, length);
  if (written < 0 || (size_t)written >= capacity) {
    free(symbol);
    return -1;
  }
  offset = (size_t)written;
  for (i = 0u; i < length; ++i) {
    unsigned char byte = (unsigned char)name[i];
    symbol[offset++] = hex[byte >> 4u];
    symbol[offset++] = hex[byte & 15u];
  }
  memcpy(symbol + offset, role, strlen(role) + 1u);
  status = tbe_compiler_set_string(target, key, symbol);
  free(symbol);
  return status;
}

static int databind_compiler_annotate_named_native_semantic(
    Node *root, Node *target_node, const char *type_name,
    const char *type_key, const char *data_key) {
  const tbe_compiler_scalar_projection_t *scalar;
  Node *target;
  char symbol[256];

  if (!root || !target_node || !type_name || !type_key || !data_key)
    return -1;

  scalar = tbe_compiler_scalar_projection(type_name);
  if (scalar && scalar->native_data_symbol && scalar->native_type_symbol)
    return tbe_compiler_set_string(target_node, type_key,
                                   scalar->native_type_symbol) == 0 &&
                   tbe_compiler_set_string(target_node, data_key,
                                           scalar->native_data_symbol) == 0
               ? 0
               : -1;

  if (strcmp(type_name, "string") == 0)
    return tbe_compiler_set_string(target_node, type_key,
                                   "salts_tstr_cmeta_type") == 0 &&
                   tbe_compiler_set_string(target_node, data_key,
                                           "salts_tstr_cmeta_data") == 0
               ? 0
               : -1;

  if (strcmp(type_name, "uuid") == 0)
    return tbe_compiler_set_string(target_node, type_key,
                                   "salts_uuid_cmeta_type") == 0 &&
                   tbe_compiler_set_string(target_node, data_key,
                                           "salts_uuid_cmeta_data") == 0
               ? 0
               : -1;

  target = tbe_compiler_find_record(root, "enums", type_name);
  if (target) {
    return tbe_compiler_set_enum_symbol(target_node, type_key, type_name,
                                        "Type") == 0 &&
                   tbe_compiler_set_enum_symbol(target_node, data_key, type_name,
                                                "Data") == 0
               ? 0
               : -1;
  }

  target = tbe_compiler_find_record(root, "composites", type_name);
  if (!target) target = tbe_compiler_find_record(root, "groups", type_name);
  if (!target) target = tbe_compiler_find_record(root, "messages", type_name);
  if (!target) return -1;

  if (snprintf(symbol, sizeof(symbol), "%s_CMETA_TYPE", type_name) < 0 ||
      strlen(type_name) + strlen("_CMETA_TYPE") >= sizeof(symbol) ||
      tbe_compiler_set_string(target_node, type_key, symbol) != 0)
    return -1;
  if (snprintf(symbol, sizeof(symbol), "%s_CMETA_DATA", type_name) < 0 ||
      strlen(type_name) + strlen("_CMETA_DATA") >= sizeof(symbol) ||
      tbe_compiler_set_string(target_node, data_key, symbol) != 0)
    return -1;
  return 0;
}

static char *databind_compiler_enum_symbol_alloc(
    const char *name, const char *role) {
  static const char prefix[] = "tbeCmetaEnum";
  static const char hex[] = "0123456789abcdef";
  size_t length;
  size_t role_length;
  size_t capacity;
  size_t offset;
  size_t i;
  int written;
  char *symbol;

  if (name == NULL || role == NULL) return NULL;
  length = strlen(name);
  role_length = strlen(role);
  if (length > (SIZE_MAX - sizeof(prefix) - role_length - 48u) / 2u)
    return NULL;
  capacity = sizeof(prefix) + role_length + 48u + 2u * length;
  symbol = (char *)malloc(capacity);
  if (symbol == NULL) return NULL;
  written = snprintf(symbol, capacity, "%s%zux", prefix, length);
  if (written < 0 || (size_t)written >= capacity) {
    free(symbol);
    return NULL;
  }
  offset = (size_t)written;
  for (i = 0u; i < length; ++i) {
    unsigned char byte = (unsigned char)name[i];
    symbol[offset++] = hex[byte >> 4u];
    symbol[offset++] = hex[byte & 15u];
  }
  memcpy(symbol + offset, role, role_length + 1u);
  return symbol;
}

static int databind_compiler_annotate_named_native_refs(
    Node *root, Node *target_node, const char *type_name,
    const char *type_ref_key, const char *data_ref_key) {
  const tbe_compiler_scalar_projection_t *scalar;
  Node *target;
  char symbol[288];

  if (!root || !target_node || !type_name || !type_ref_key || !data_ref_key)
    return -1;

  if (strcmp(type_name, "string") == 0)
    return tbe_compiler_set_string(target_node, type_ref_key,
                                   "SALTS_TSTR_CMETA_TYPE_REF") == 0 &&
                   tbe_compiler_set_string(target_node, data_ref_key,
                                           "SALTS_TSTR_CMETA_DATA_REF") == 0
               ? 0
               : -1;

  scalar = tbe_compiler_scalar_projection(type_name);
  if (scalar && scalar->native_data_symbol && scalar->native_type_symbol) {
    if (snprintf(symbol, sizeof(symbol), "&%s", scalar->native_type_symbol) < 0 ||
        strlen(scalar->native_type_symbol) + 2u >= sizeof(symbol) ||
        tbe_compiler_set_string(target_node, type_ref_key, symbol) != 0)
      return -1;
    if (snprintf(symbol, sizeof(symbol), "&%s", scalar->native_data_symbol) < 0 ||
        strlen(scalar->native_data_symbol) + 2u >= sizeof(symbol) ||
        tbe_compiler_set_string(target_node, data_ref_key, symbol) != 0)
      return -1;
    return 0;
  }

  if (strcmp(type_name, "uuid") == 0)
    return tbe_compiler_set_string(target_node, type_ref_key,
                                   "&salts_uuid_cmeta_type") == 0 &&
                   tbe_compiler_set_string(target_node, data_ref_key,
                                           "&salts_uuid_cmeta_data") == 0
               ? 0
               : -1;

  target = tbe_compiler_find_record(root, "enums", type_name);
  if (target) {
    char *type_symbol = databind_compiler_enum_symbol_alloc(type_name, "Type");
    char *data_symbol = databind_compiler_enum_symbol_alloc(type_name, "Data");
    char *type_ref = NULL;
    char *data_ref = NULL;
    size_t type_ref_size;
    size_t data_ref_size;
    int ok = 0;

    if (type_symbol == NULL || data_symbol == NULL) goto enum_refs_done;
    type_ref_size = strlen(type_symbol) + 2u;
    data_ref_size = strlen(data_symbol) + 2u;
    type_ref = (char *)malloc(type_ref_size);
    data_ref = (char *)malloc(data_ref_size);
    if (type_ref == NULL || data_ref == NULL) goto enum_refs_done;
    if (snprintf(type_ref, type_ref_size, "&%s", type_symbol) < 0 ||
        snprintf(data_ref, data_ref_size, "&%s", data_symbol) < 0)
      goto enum_refs_done;
    ok = tbe_compiler_set_string(target_node, type_ref_key, type_ref) == 0 &&
         tbe_compiler_set_string(target_node, data_ref_key, data_ref) == 0;

enum_refs_done:
    free(type_symbol);
    free(data_symbol);
    free(type_ref);
    free(data_ref);
    return ok ? 0 : -1;
  }

  target = tbe_compiler_find_record(root, "composites", type_name);
  if (!target) target = tbe_compiler_find_record(root, "groups", type_name);
  if (!target) target = tbe_compiler_find_record(root, "messages", type_name);
  if (!target) return -1;

  if (snprintf(symbol, sizeof(symbol), "&%s_CMETA_TYPE", type_name) < 0 ||
      strlen(type_name) + strlen("&_CMETA_TYPE") >= sizeof(symbol) ||
      tbe_compiler_set_string(target_node, type_ref_key, symbol) != 0)
    return -1;
  if (snprintf(symbol, sizeof(symbol), "&%s_CMETA_DATA", type_name) < 0 ||
      strlen(type_name) + strlen("&_CMETA_DATA") >= sizeof(symbol) ||
      tbe_compiler_set_string(target_node, data_ref_key, symbol) != 0)
    return -1;
  return 0;
}

static int databind_compiler_annotate_map_value_provider(
    Node *root, Node *field, const char *value_type) {
  if (databind_compiler_annotate_named_native_semantic(
          root, field, value_type, "native_map_value_type_symbol",
          "native_map_value_data_symbol") != 0)
    return -1;
  return databind_compiler_annotate_named_native_refs(
      root, field, value_type, "native_map_value_type_ref",
      "native_map_value_data_ref");
}

typedef struct databind_generic_lower_context {
  Node *root;
  Node *types;
  const char *owner;
  const char *field;
} databind_generic_lower_context;

typedef struct databind_generic_storage {
  char c_type[256];
  char type_symbol[288];
  char data_symbol[288];
  char type_ref[320];
  char data_ref[320];
} databind_generic_storage;

static int databind_generic_copy_text(char *destination, size_t capacity,
                                      const char *source) {
  size_t length = source != NULL ? strlen(source) : 0u;
  if (source == NULL || length >= capacity) return 0;
  memcpy(destination, source, length + 1u);
  return 1;
}

/* Build-time projection only. The resulting graph names concrete SDK
 * providers; no item operation consults these logical names or Node objects. */
static int databind_generic_lower(databind_generic_lower_context *context,
                                  const IdlTypeRef *type,
                                  databind_generic_storage *out) {
  databind_generic_storage value = {0};
  Node *node = NULL;
  IdlTypeRef child;
  char leaf[256];
  char identity[320];
  int written;
  int is_map = type->collection_kind == IDL_COLLECTION_MAP;

  if (type->collection_kind == IDL_COLLECTION_NONE) {
    if (type->name_length >= sizeof(leaf)) return 0;
    memcpy(leaf, type->name, type->name_length);
    leaf[type->name_length] = '\0';
    /* Enum providers are translation-unit local and cannot yet be referenced
     * by a nested wrapper declaration in the public generated header. */
    if (tbe_compiler_find_record(context->root, "enums", leaf) != NULL) return 0;
    node = create_node_map(NULL);
    if (node == NULL) return 0;
    if (!tbe_compiler_native_named_c_type(context->root, leaf,
                                         out->c_type, sizeof(out->c_type)) ||
        databind_compiler_annotate_named_native_semantic(context->root, node,
            leaf, "type_symbol", "data_symbol") != 0 ||
        databind_compiler_annotate_named_native_refs(context->root, node,
            leaf, "type_ref", "data_ref") != 0 ||
        !databind_generic_copy_text(out->type_symbol, sizeof(out->type_symbol),
            tbe_compiler_string_value(node, "type_symbol")) ||
        !databind_generic_copy_text(out->data_symbol, sizeof(out->data_symbol),
            tbe_compiler_string_value(node, "data_symbol")) ||
        !databind_generic_copy_text(out->type_ref, sizeof(out->type_ref),
            tbe_compiler_string_value(node, "type_ref")) ||
        !databind_generic_copy_text(out->data_ref, sizeof(out->data_ref),
            tbe_compiler_string_value(node, "data_ref"))) {
      node_free(node);
      return 0;
    }
    node_free(node);
    return 1;
  }
  if ((type->collection_kind != IDL_COLLECTION_LIST && !is_map) ||
      context->types->data.list.count >= IDL_TYPE_REF_MAX_NODES ||
      (is_map && (type->argument_lengths[0] != sizeof("string") - 1u ||
                  memcmp(type->arguments[0], "string", sizeof("string") - 1u) != 0)) ||
      !idl_type_ref_parse(type->arguments[is_map ? 1u : 0u],
                          type->argument_lengths[is_map ? 1u : 0u], &child) ||
      !databind_generic_lower(context, &child, &value))
    return 0;

  written = snprintf(out->c_type, sizeof(out->c_type),
      "tbeNested_%zu_%s_%zu_%s_%zu_t", strlen(context->owner), context->owner,
      strlen(context->field), context->field, context->types->data.list.count);
  if (written < 0 || (size_t)written >= sizeof(out->c_type)) return 0;
  written = snprintf(out->type_symbol, sizeof(out->type_symbol),
                      "%s_cmeta_type", out->c_type);
  if (written < 0 || (size_t)written >= sizeof(out->type_symbol)) return 0;
  written = snprintf(out->data_symbol, sizeof(out->data_symbol), "%s_%s_data",
                      out->c_type, is_map ? "map" : "collection");
  if (written < 0 || (size_t)written >= sizeof(out->data_symbol)) return 0;
  written = snprintf(out->type_ref, sizeof(out->type_ref), "&%s", out->type_symbol);
  if (written < 0 || (size_t)written >= sizeof(out->type_ref)) return 0;
  written = snprintf(out->data_ref, sizeof(out->data_ref), "&%s", out->data_symbol);
  if (written < 0 || (size_t)written >= sizeof(out->data_ref)) return 0;
  written = snprintf(identity, sizeof(identity), "%s_ID", out->c_type);
  if (written < 0 || (size_t)written >= sizeof(identity)) return 0;

  node = create_node_map(NULL);
  if (node == NULL) return 0;
  if (tbe_compiler_set_string(node, "name", out->c_type) != 0 ||
      tbe_compiler_set_string(node, "identity", identity) != 0 ||
      tbe_compiler_set_string(node, "constructor", is_map
          ? "&stl_map_generic_desc" : "&stl_vec_generic_desc") != 0 ||
      tbe_compiler_set_string(node, "arity", is_map ? "2" : "1") != 0 ||
      tbe_compiler_set_string(node, "kind", is_map ? "Map" : "Vec") != 0 ||
      tbe_compiler_set_string(node, "value_c_type", value.c_type) != 0 ||
      tbe_compiler_set_string(node, "value_type_ref", value.type_ref) != 0 ||
      tbe_compiler_set_string(node, "value_data_ref", value.data_ref) != 0 ||
      tbe_compiler_set_string(node, "value_type_symbol", value.type_symbol) != 0 ||
      tbe_compiler_set_string(node, "value_data_symbol", value.data_symbol) != 0 ||
      tbe_compiler_set_string(node, "arg0", is_map
          ? "SALTS_TSTR_CMETA_TYPE_REF" : value.type_ref) != 0 ||
      (is_map && tbe_compiler_set_string(node, "arg1", value.type_ref) != 0) ||
      list_add(context->types, node) != 0) {
    node_free(node);
    return 0;
  }
  return 1;
}

static tstr databind_generic_declarations(Node *types) {
  tstr result = tstr_dup("");
  size_t index;
  if (result == NULL) return NULL;
  for (index = 0u; index < types->data.list.count; ++index) {
    Node *type = types->data.list.items[index];
    const char *name = tbe_compiler_string_value(type, "name");
    const char *identity = tbe_compiler_string_value(type, "identity");
    const char *kind = tbe_compiler_string_value(type, "kind");
    const char *value = tbe_compiler_string_value(type, "value_c_type");
    const char *value_type = tbe_compiler_string_value(type, "value_type_ref");
    const char *value_data = tbe_compiler_string_value(type, "value_data_ref");
    tstr appended = tstr_cat_fmt(result,
        "TBE_GENERATED_API extern const cmeta_type_identity %s;\n"
        "#ifdef __cplusplus\n"
        "cstl_typed_decl(%s, %s, %s%s);\n"
        "#else\n"
        "cmeta_type(%s, %s, %s%s, %s%s, %s, &%s);\n"
        "#endif\n", identity, kind, name,
        strcmp(kind, "Map") == 0 ? "tstr, " : "", value,
        kind, name, strcmp(kind, "Map") == 0 ? "tstr, " : "", value,
        strcmp(kind, "Map") == 0
            ? "SALTS_TSTR_CMETA_TYPE_REF, SALTS_TSTR_CMETA_DATA_REF, " : "",
        value_type, value_data, identity);
    if (appended == NULL) { tstr_free(result); return NULL; }
    result = appended;
  }
  return result;
}

static int databind_compiler_annotate_nested_generic(Node *root, Node *field) {
  const char *owner = tbe_compiler_string_value(field, "owner_name");
  const char *name = tbe_compiler_string_value(field, "name");
  const char *member = tbe_compiler_string_value(field, "c_name");
  const char *kind = tbe_compiler_string_value(field, "type");
  const char *inner = tbe_compiler_string_value(field, "inner_type");
  const char *key = tbe_compiler_string_value(field, "key_type");
  int is_map = tbe_compiler_has_child(field, "is_map");
  databind_generic_lower_context context = {root, NULL, owner, name};
  databind_generic_storage storage = {0};
  IdlTypeRef type;
  Node *global;
  Node *outer;
  Node *owner_record;
  tstr expression = NULL;
  tstr declarations = NULL;
  char declaration[512];
  size_t index;
  int status = 0;

  /* Re-annotation must not publish a previous success after partial lowering. */
  tbe_compiler_remove_children(field, "native_nested_generic");
  tbe_compiler_remove_children(field, "native_generic_declarations");
  tbe_compiler_remove_children(field, "native_generic_logical_type");
  if (owner == NULL || name == NULL || member == NULL || kind == NULL ||
      inner == NULL || (is_map && key == NULL)) return 0;
  expression = tstr_dup("");
  if (expression == NULL) goto cleanup;
  {
    tstr appended = is_map
        ? tstr_cat_fmt(expression, "%s<%s,%s>", kind, key, inner)
        : tstr_cat_fmt(expression, "%s<%s>", kind, inner);
    if (appended == NULL) goto cleanup;
    expression = appended;
  }
  context.types = create_node_list(NULL);
  if (context.types == NULL ||
      !idl_type_ref_parse(expression, tstr_len(expression), &type) ||
      !databind_generic_lower(&context, &type, &storage)) goto cleanup;
  declarations = databind_generic_declarations(context.types);
  if (declarations == NULL) goto cleanup;
  outer = context.types->data.list.items[context.types->data.list.count - 1u];
  if (snprintf(declaration, sizeof(declaration), "%s %s;", storage.c_type, member) < 0 ||
      strlen(storage.c_type) + strlen(member) + sizeof(" ;") > sizeof(declaration))
    goto cleanup;
  if (tbe_compiler_set_string(field, "native_generic_declarations", declarations) != 0 ||
      tbe_compiler_set_string(field, "native_generic_logical_type", expression) != 0 ||
      tbe_compiler_set_string(field, "typed_declaration", declaration) != 0 ||
      tbe_compiler_set_string(field, "typed_vector_type", storage.c_type) != 0 ||
      tbe_compiler_set_string(field, "native_c_type", storage.c_type) != 0 ||
      tbe_compiler_set_string(field, "native_type_symbol", storage.type_symbol) != 0 ||
      tbe_compiler_set_string(field, "native_data_symbol", storage.data_symbol) != 0 ||
      tbe_compiler_set_string(field, is_map ? "native_cstl_map" : "native_cstl_sequence", "1") != 0 ||
      tbe_compiler_set_string(field, "native_element_type_ref",
          tbe_compiler_string_value(outer, "value_type_ref")) != 0 ||
      tbe_compiler_set_string(field, "native_element_data_ref",
          tbe_compiler_string_value(outer, "value_data_ref")) != 0 ||
      (is_map && (tbe_compiler_set_string(field, "native_map_key_type_ref", "SALTS_TSTR_CMETA_TYPE_REF") != 0 ||
                  tbe_compiler_set_string(field, "native_map_key_data_ref", "SALTS_TSTR_CMETA_DATA_REF") != 0 ||
                  tbe_compiler_set_string(field, "native_map_value_type_ref",
                      tbe_compiler_string_value(outer, "value_type_ref")) != 0 ||
                  tbe_compiler_set_string(field, "native_map_value_data_ref",
                      tbe_compiler_string_value(outer, "value_data_ref")) != 0 ||
                  tbe_compiler_set_string(field, "native_map_value_data_symbol",
                      tbe_compiler_string_value(outer, "value_data_symbol")) != 0)))
    goto cleanup;
  global = tbe_compiler_find_child(root, "native_generic_types");
  if (global == NULL) {
    global = create_node_list("native_generic_types");
    if (global == NULL) goto cleanup;
    if (map_add(root, global) != 0) { node_free(global); goto cleanup; }
  }
  for (index = 0u; index < context.types->data.list.count; ++index) {
    if (list_add(global, context.types->data.list.items[index]) != 0) goto cleanup;
    context.types->data.list.items[index] = NULL;
  }
  owner_record = tbe_compiler_find_record(root, "composites", owner);
  if (owner_record == NULL) owner_record = tbe_compiler_find_record(root, "groups", owner);
  if (owner_record == NULL) owner_record = tbe_compiler_find_record(root, "messages", owner);
  if (owner_record == NULL ||
      tbe_compiler_set_string(owner_record, "native_cstl_storage", "1") != 0 ||
      tbe_compiler_set_string(field, "native_nested_generic", "1") != 0) goto cleanup;
  status = 1;
cleanup:
  tstr_free(declarations);
  tstr_free(expression);
  node_free(context.types);
  return status;
}

static void tbe_compiler_annotate_typed_field(Node *root, Node *field,
                                             const schema_cmeta_field_type *semantic) {
  const char *name = tbe_compiler_string_value(field, "name");
  const char *c_name = tbe_compiler_attribute_value(field, "c");
  const char *owner = tbe_compiler_string_value(field, "owner_name");
  const char *type = tbe_compiler_string_value(field, "type");
  char c_type[256] = {0};
  char declaration[512] = {0};
  char vector_type[256] = {0};

  if (!name || !owner) return;
  if (!c_name || !c_name[0]) c_name = name;
  tbe_compiler_set_string(field, "c_name", c_name);
  if (semantic && semantic->kind == CMETA_DATA_SEQUENCE &&
      strcmp(semantic->schema_kind, "group") == 0) {
    const char *group_type = tbe_compiler_string_value(field, "group_type");
    snprintf(c_type, sizeof(c_type), "%s_t", group_type ? group_type : "unknown");
    snprintf(vector_type, sizeof(vector_type), "%s_%s_vec_t", owner, name);
    snprintf(declaration, sizeof(declaration), "%s %s;", vector_type, c_name);
    tbe_compiler_set_string(field, "typed_element_c_type", c_type);
    tbe_compiler_set_string(field, "typed_vector_type", vector_type);
    tbe_compiler_set_string(field, "typed_needs_vector", "1");
    tbe_compiler_set_string(field, "typed_is_group", "1");
    tbe_compiler_set_string(field, "typed_declaration", declaration);
    (void)databind_compiler_annotate_named_native_semantic(
        root, field, group_type, "native_element_type_symbol",
        "native_element_data_symbol");
    (void)databind_compiler_annotate_named_native_refs(
        root, field, group_type, "native_element_type_ref",
        "native_element_data_ref");
    return;
  }
  if (semantic && semantic->kind == CMETA_DATA_BYTES) {
    if (tbe_compiler_has_child(field, "is_fixed_size")) {
      const char *count = tbe_compiler_string_value(field, "size_bytes");
      char base[768];
      char symbol[800];
      int written;
      snprintf(declaration, sizeof(declaration), "uint8_t %s[%s];", c_name,
               count ? count : "0");
      tbe_compiler_set_string(field, "typed_fixed_count", count ? count : "0");
      written = snprintf(base, sizeof(base), "tbe_fixed_bytes_%zu_%s_%zu_%s",
                         strlen(owner), owner, strlen(c_name), c_name);
      if (written < 0 || (size_t)written >= sizeof(base)) {
        tbe_compiler_set_string(field, "typed_declaration", declaration);
        return;
      }
      tbe_compiler_set_string(field, "native_fixed_bytes_name", base);
      written = snprintf(symbol, sizeof(symbol), "%s_storage", base);
      if (written < 0 || (size_t)written >= sizeof(symbol)) {
        tbe_compiler_set_string(field, "typed_declaration", declaration);
        return;
      }
      tbe_compiler_set_string(field, "native_c_type", symbol);
      written = snprintf(symbol, sizeof(symbol), "%s_cmeta_data", base);
      if (written < 0 || (size_t)written >= sizeof(symbol)) {
        tbe_compiler_set_string(field, "typed_declaration", declaration);
        return;
      }
      tbe_compiler_set_string(field, "native_data_symbol", symbol);
      written = snprintf(symbol, sizeof(symbol), "%s_cmeta_type", base);
      if (written < 0 || (size_t)written >= sizeof(symbol)) {
        tbe_compiler_set_string(field, "typed_declaration", declaration);
        return;
      }
      tbe_compiler_set_string(field, "native_type_symbol", symbol);
    } else {
      snprintf(declaration, sizeof(declaration), "stl_byte_buffer %s;", c_name);
      tbe_compiler_set_string(field, "typed_is_var_data", "1");
      tbe_compiler_set_string(field, "native_data_symbol",
                              "stl_byte_buffer_cmeta_data");
      tbe_compiler_set_string(field, "native_type_symbol",
                              "stl_byte_buffer_cmeta_type");
      tbe_compiler_set_string(field, "native_external", "1");
      tbe_compiler_set_string(field, "native_c_type", "stl_byte_buffer");
    }
    tbe_compiler_set_string(field, "typed_declaration", declaration);
    return;
  }
  if (semantic && cmeta_data_kind_is_container(semantic->kind)) {
    const char *inner = tbe_compiler_string_value(field, "inner_type");
    const char *storage_element = semantic->kind == CMETA_DATA_MAP
        ? tbe_compiler_string_value(field, "value_type")
        : inner;
    if (storage_element != NULL && strchr(storage_element, '<') != NULL) {
      (void)databind_compiler_annotate_nested_generic(root, field);
      return;
    }
    if (!tbe_compiler_native_named_c_type(root, storage_element, c_type,
                                           sizeof(c_type))) {
      snprintf(declaration, sizeof(declaration), "%s_t %s;", inner ? inner : "unknown", c_name);
      tbe_compiler_set_string(field, "typed_declaration", declaration);
      return;
    }
    tbe_compiler_set_string(field, "typed_element_c_type", c_type);
    (void)databind_compiler_annotate_named_native_semantic(
        root, field, storage_element ? storage_element : inner,
        "native_element_type_symbol", "native_element_data_symbol");
    (void)databind_compiler_annotate_named_native_refs(
        root, field, storage_element ? storage_element : inner,
        "native_element_type_ref", "native_element_data_ref");
    if (tbe_compiler_has_child(field, "is_fixed_size")) {
      const char *count = tbe_compiler_string_value(field, "length_field");
      char base[768];
      char symbol[800];
      int written;
      snprintf(declaration, sizeof(declaration), "%s %s[%s];", c_type, c_name,
               count ? count : "0");
      tbe_compiler_set_string(field, "typed_fixed_count", count ? count : "0");
      written = snprintf(base, sizeof(base), "tbe_fixed_array_%zu_%s_%zu_%s",
                         strlen(owner), owner, strlen(c_name), c_name);
      if (written < 0 || (size_t)written >= sizeof(base)) return;
      tbe_compiler_set_string(field, "native_fixed_array_name", base);
      written = snprintf(symbol, sizeof(symbol), "%s_storage", base);
      if (written < 0 || (size_t)written >= sizeof(symbol)) return;
      tbe_compiler_set_string(field, "native_c_type", symbol);
      written = snprintf(symbol, sizeof(symbol), "%s_cmeta_data", base);
      if (written < 0 || (size_t)written >= sizeof(symbol)) return;
      tbe_compiler_set_string(field, "native_data_symbol", symbol);
      written = snprintf(symbol, sizeof(symbol), "%s_cmeta_type", base);
      if (written < 0 || (size_t)written >= sizeof(symbol)) return;
      tbe_compiler_set_string(field, "native_type_symbol", symbol);
    } else if (semantic->kind == CMETA_DATA_MAP) {
      const char *key_type = tbe_compiler_string_value(field, "key_type");
      const char *value_type = tbe_compiler_string_value(field, "value_type");
      char value_c_type[256] = {0};
      const int value_supported = tbe_compiler_native_named_c_type(
          root, value_type ? value_type : inner, value_c_type, sizeof(value_c_type));
      char entry_type[256];
      if (key_type != NULL)
        (void)databind_compiler_annotate_named_native_refs(
            root, field, key_type, "native_map_key_type_ref",
            "native_map_key_data_ref");
      if (!value_supported || !key_type || strcmp(key_type, "string") != 0) {
        snprintf(declaration, sizeof(declaration), "/* unsupported map field %s */ uint8_t %s;",
                 name, c_name);
        tbe_compiler_set_string(field, "typed_declaration", declaration);
        return;
      }
      snprintf(entry_type, sizeof(entry_type), "%s_%s_entry_t", owner, name);
      snprintf(vector_type, sizeof(vector_type), "%s_%s_vec_t", owner, name);
      snprintf(declaration, sizeof(declaration), "%s %s;", vector_type, c_name);
      tbe_compiler_set_string(field, "typed_map_entry_type", entry_type);
      tbe_compiler_set_string(field, "typed_map_value_c_type", value_c_type);
      tbe_compiler_set_string(field, "typed_vector_type", vector_type);
      tbe_compiler_set_string(field, "typed_element_c_type", entry_type);
      tbe_compiler_set_string(field, "typed_needs_map_vector", "1");
      if (databind_compiler_annotate_map_value_provider(
              root, field, value_type ? value_type : inner) == 0) {
        const tbe_compiler_scalar_projection_t *value_scalar =
            tbe_compiler_scalar_projection(value_type ? value_type : inner);
        const int canonical_cstl_value =
            (value_scalar != NULL && value_scalar->native_data_symbol != NULL) ||
            (value_type != NULL && strcmp(value_type, "string") == 0);
        if (canonical_cstl_value &&
            tbe_compiler_string_value(field, "native_map_key_type_ref") != NULL &&
            tbe_compiler_string_value(field, "native_map_key_data_ref") != NULL &&
            tbe_compiler_string_value(field, "native_map_value_type_ref") != NULL &&
            tbe_compiler_string_value(field, "native_map_value_data_ref") != NULL) {
          char symbol[320];
          Node *owner_record;
          snprintf(vector_type, sizeof(vector_type), "%s_%s_map_t", owner, name);
          snprintf(declaration, sizeof(declaration), "%s %s;", vector_type, c_name);
          tbe_compiler_set_string(field, "typed_vector_type", vector_type);
          owner_record = tbe_compiler_find_record(root, "composites", owner);
          if (owner_record == NULL)
            owner_record = tbe_compiler_find_record(root, "groups", owner);
          if (owner_record == NULL)
            owner_record = tbe_compiler_find_record(root, "messages", owner);
          tbe_compiler_set_string(field, "native_cstl_map", "1");
          tbe_compiler_set_string(field, "native_cstl_map_explicit_refs", "1");
          if (owner_record != NULL)
            tbe_compiler_set_string(owner_record, "native_cstl_storage", "1");
          if (snprintf(symbol, sizeof(symbol), "%s_map_data", vector_type) >= 0 &&
              strlen(vector_type) + strlen("_map_data") < sizeof(symbol))
            tbe_compiler_set_string(field, "native_data_symbol", symbol);
          if (snprintf(symbol, sizeof(symbol), "%s_cmeta_type", vector_type) >= 0 &&
              strlen(vector_type) + strlen("_cmeta_type") < sizeof(symbol))
            tbe_compiler_set_string(field, "native_type_symbol", symbol);
          if (tbe_compiler_string_value(field, "native_data_symbol") != NULL &&
              tbe_compiler_string_value(field, "native_type_symbol") != NULL)
            tbe_compiler_set_string(field, "native_c_type", vector_type);
        }
      }
    } else {
      snprintf(vector_type, sizeof(vector_type), "%s_%s_vec_t", owner, name);
      snprintf(declaration, sizeof(declaration), "%s %s;", vector_type, c_name);
      tbe_compiler_set_string(field, "typed_vector_type", vector_type);
      tbe_compiler_set_string(field, "typed_needs_vector", "1");
      if (semantic->kind == CMETA_DATA_SEQUENCE &&
          tbe_compiler_has_child(field, "is_list") &&
          tbe_compiler_string_value(field, "native_element_type_ref") != NULL &&
          tbe_compiler_string_value(field, "native_element_data_ref") != NULL) {
        const tbe_compiler_scalar_projection_t *element_scalar =
            tbe_compiler_scalar_projection(storage_element);
        if ((element_scalar != NULL && element_scalar->native_data_symbol != NULL) ||
            (storage_element != NULL && strcmp(storage_element, "string") == 0)) {
          char symbol[320];
          Node *owner_record =
              tbe_compiler_find_record(root, "composites", owner);
          if (owner_record == NULL)
            owner_record = tbe_compiler_find_record(root, "groups", owner);
          if (owner_record == NULL)
            owner_record = tbe_compiler_find_record(root, "messages", owner);
          tbe_compiler_set_string(field, "native_cstl_sequence", "1");
          if (owner_record != NULL)
            tbe_compiler_set_string(owner_record, "native_cstl_storage", "1");
          tbe_compiler_set_string(
              field, "native_cstl_sequence_explicit_refs", "1");
          if (snprintf(symbol, sizeof(symbol), "%s_collection_data", vector_type) >= 0 &&
              strlen(vector_type) + strlen("_collection_data") < sizeof(symbol))
            tbe_compiler_set_string(field, "native_data_symbol", symbol);
          if (snprintf(symbol, sizeof(symbol), "%s_cmeta_type", vector_type) >= 0 &&
              strlen(vector_type) + strlen("_cmeta_type") < sizeof(symbol))
            tbe_compiler_set_string(field, "native_type_symbol", symbol);
          if (tbe_compiler_string_value(field, "native_data_symbol") != NULL &&
              tbe_compiler_string_value(field, "native_type_symbol") != NULL)
            tbe_compiler_set_string(field, "native_c_type", vector_type);
        }
      }

      if (semantic->kind == CMETA_DATA_SET &&
          tbe_compiler_has_child(field, "is_set") &&
          tbe_compiler_string_value(field, "native_element_type_ref") != NULL &&
          tbe_compiler_string_value(field, "native_element_data_ref") != NULL) {
        const tbe_compiler_scalar_projection_t *element_scalar =
            tbe_compiler_scalar_projection(storage_element);
        if ((element_scalar != NULL && element_scalar->native_data_symbol != NULL) ||
            (storage_element != NULL && strcmp(storage_element, "string") == 0)) {
          char symbol[320];
          Node *owner_record;
          snprintf(vector_type, sizeof(vector_type), "%s_%s_set_t", owner, name);
          snprintf(declaration, sizeof(declaration), "%s %s;", vector_type, c_name);
          tbe_compiler_set_string(field, "typed_vector_type", vector_type);
          owner_record =
              tbe_compiler_find_record(root, "composites", owner);
          if (owner_record == NULL)
            owner_record = tbe_compiler_find_record(root, "groups", owner);
          if (owner_record == NULL)
            owner_record = tbe_compiler_find_record(root, "messages", owner);
          tbe_compiler_set_string(field, "native_cstl_set", "1");
          if (owner_record != NULL)
            tbe_compiler_set_string(owner_record, "native_cstl_storage", "1");
          tbe_compiler_set_string(field, "native_cstl_set_explicit_refs", "1");
          if (snprintf(symbol, sizeof(symbol), "%s_collection_data", vector_type) >= 0 &&
              strlen(vector_type) + strlen("_collection_data") < sizeof(symbol))
            tbe_compiler_set_string(field, "native_data_symbol", symbol);
          if (snprintf(symbol, sizeof(symbol), "%s_cmeta_type", vector_type) >= 0 &&
              strlen(vector_type) + strlen("_cmeta_type") < sizeof(symbol))
            tbe_compiler_set_string(field, "native_type_symbol", symbol);
          if (tbe_compiler_string_value(field, "native_data_symbol") != NULL &&
              tbe_compiler_string_value(field, "native_type_symbol") != NULL)
            tbe_compiler_set_string(field, "native_c_type", vector_type);
        }
      }
    }
    tbe_compiler_set_string(field, "typed_declaration", declaration);
    return;
  }
  if (semantic && semantic->kind == CMETA_DATA_STRING) {
    snprintf(declaration, sizeof(declaration), "tstr %s;", c_name);
    tbe_compiler_set_string(field, "typed_is_var_data", "1");
    tbe_compiler_set_string(field, "native_data_symbol",
                            "salts_tstr_cmeta_data");
    tbe_compiler_set_string(field, "native_type_symbol",
                            "salts_tstr_cmeta_type");
    tbe_compiler_set_string(field, "native_external", "1");
    tbe_compiler_set_string(field, "native_c_type", "tstr");
    tbe_compiler_set_string(field, "typed_declaration", declaration);
    return;
  }
  if (!tbe_compiler_native_named_c_type(root, type, c_type, sizeof(c_type))) {
    snprintf(declaration, sizeof(declaration), "%s_t %s;", type ? type : "unknown", c_name);
    tbe_compiler_set_string(field, "typed_declaration", declaration);
    return;
  }
  snprintf(declaration, sizeof(declaration), "%s %s;", c_type, c_name);
  {
    const tbe_compiler_scalar_projection_t *scalar = tbe_compiler_scalar_projection(type);
    char symbol[256];
    if (scalar && scalar->native_data_symbol) {
      tbe_compiler_set_string(field, "native_data_symbol", scalar->native_data_symbol);
      tbe_compiler_set_string(field, "native_type_symbol", scalar->native_type_symbol);
      tbe_compiler_set_string(field, "native_external", "1");
      tbe_compiler_set_string(field, "native_c_type", c_type);
    } else if (semantic && salts_uuid_cmeta_data_valid(semantic->data)) {
      tbe_compiler_set_string(field, "native_data_symbol", "salts_uuid_cmeta_data");
      tbe_compiler_set_string(field, "native_type_symbol", "salts_uuid_cmeta_type");
      tbe_compiler_set_string(field, "native_external", "1");
      tbe_compiler_set_string(field, "native_c_type", c_type);
    } else if (tbe_compiler_find_record(root, "enums", type) != NULL) {
      if (tbe_compiler_set_enum_symbol(field, "native_data_symbol", type, "Data") == 0 &&
          tbe_compiler_set_enum_symbol(field, "native_type_symbol", type, "Type") == 0)
        tbe_compiler_set_string(field, "native_c_type", c_type);
    } else if (semantic && semantic->kind == CMETA_DATA_STRUCT) {
      snprintf(symbol, sizeof(symbol), "%s_CMETA_DATA", type);
      tbe_compiler_set_string(field, "native_data_symbol", symbol);
      snprintf(symbol, sizeof(symbol), "%s_CMETA_TYPE", type);
      tbe_compiler_set_string(field, "native_type_symbol", symbol);
      tbe_compiler_set_string(field, "native_c_type", c_type);
    }
  }
  tbe_compiler_set_string(field, "typed_declaration", declaration);
}

static void tbe_compiler_annotate_native_requirement(
    Node *root, Node *field, const schema_cmeta_field_type *semantic) {
  const char *type;
  tbe_compiler_native_requirement_t requirement;

  if (!root || !field || !semantic) return;
  type = tbe_compiler_string_value(field, "type");

  if (semantic->kind == CMETA_DATA_MAP &&
      tbe_compiler_string_value(field, "native_data_symbol") != NULL &&
      tbe_compiler_string_value(field, "native_type_symbol") != NULL &&
      tbe_compiler_string_value(field, "native_map_value_data_symbol") != NULL) {
    requirement = DATABIND_COMPILER_NATIVE_MAP_PROVIDER;
  } else if (semantic->kind == CMETA_DATA_SEQUENCE &&
             tbe_compiler_has_child(field, "native_fixed_array_name") &&
             tbe_compiler_string_value(field, "native_element_data_ref") != NULL &&
             tbe_compiler_string_value(field, "native_data_symbol") != NULL &&
             tbe_compiler_string_value(field, "native_type_symbol") != NULL) {
    requirement = DATABIND_COMPILER_NATIVE_SEQUENCE_PROVIDER;
  } else if (semantic->kind == CMETA_DATA_SEQUENCE &&
             tbe_compiler_has_child(field, "is_list") &&
             !tbe_compiler_has_child(field, "is_optional") &&
             !tbe_compiler_has_child(field, "is_nullable") &&
             tbe_compiler_string_value(field, "native_cstl_sequence") != NULL &&
             tbe_compiler_string_value(field, "native_element_type_ref") != NULL &&
             tbe_compiler_string_value(field, "native_element_data_ref") != NULL) {
    requirement = DATABIND_COMPILER_NATIVE_SEQUENCE_PROVIDER;
  } else if (semantic->kind == CMETA_DATA_SET &&
             tbe_compiler_has_child(field, "is_set") &&
             !tbe_compiler_has_child(field, "is_optional") &&
             !tbe_compiler_has_child(field, "is_nullable") &&
             tbe_compiler_string_value(field, "native_cstl_set") != NULL &&
             tbe_compiler_string_value(field, "native_element_type_ref") != NULL &&
             tbe_compiler_string_value(field, "native_element_data_ref") != NULL &&
             tbe_compiler_string_value(field, "native_data_symbol") != NULL &&
             tbe_compiler_string_value(field, "native_type_symbol") != NULL) {
    requirement = DATABIND_COMPILER_NATIVE_SET_PROVIDER;
  } else if (cmeta_data_kind_is_container(semantic->kind)) {
    requirement = TBE_COMPILER_NATIVE_DEFERRED_CONTAINER;
  } else if (tbe_compiler_has_child(field, "is_optional") &&
             tbe_compiler_has_child(field, "is_nullable")) {
    requirement = TBE_COMPILER_NATIVE_OVERLAY_PRESENCE_NULL;
  } else if (tbe_compiler_has_child(field, "is_nullable")) {
    requirement = TBE_COMPILER_NATIVE_OVERLAY_NULL;
  } else if (tbe_compiler_has_child(field, "is_optional")) {
    requirement = TBE_COMPILER_NATIVE_OVERLAY_PRESENCE;
  } else if (semantic->kind == CMETA_DATA_STRING ||
             (semantic->kind == CMETA_DATA_BYTES &&
              !tbe_compiler_has_child(field, "is_fixed_size")) ||
             (semantic->kind == CMETA_DATA_CUSTOM &&
              !salts_uuid_cmeta_data_valid(semantic->data))) {
    requirement = TBE_COMPILER_NATIVE_OWNED_LIFECYCLE;
  } else if (type && tbe_compiler_find_record(root, "enums", type)) {
    requirement = TBE_COMPILER_NATIVE_ENUM_DOMAIN;
  } else if (semantic->kind == CMETA_DATA_BOOL ||
             semantic->kind == CMETA_DATA_SINT ||
             semantic->kind == CMETA_DATA_UINT ||
             semantic->kind == CMETA_DATA_FLOAT ||
             (semantic->kind == CMETA_DATA_BYTES &&
              tbe_compiler_has_child(field, "is_fixed_size")) ||
             salts_uuid_cmeta_data_valid(semantic->data)) {
    requirement = TBE_COMPILER_NATIVE_FIXED_VALUE;
  } else {
    return;
  }

  tbe_compiler_set_string(
      field, "cmeta_native_requirement",
      tbe_compiler_native_requirement_name(requirement));
}

static const IdlField *tbe_compiler_contract_field(
    const IdlContract *contract, const char *owner_name,
    const char *field_name) {
  const IdlDataDecl *decl;
  size_t i;
  if (contract == NULL || owner_name == NULL || field_name == NULL) return NULL;
  decl = idl_contract_find_data(contract, owner_name);
  if (decl == NULL) return NULL;
  for (i = 0u; i < decl->field_count; ++i)
    if (decl->fields[i].name != NULL &&
        strcmp(decl->fields[i].name, field_name) == 0)
      return &decl->fields[i];
  return NULL;
}

static void tbe_compiler_annotate_field_types(
    Node *root, const IdlContract *contract,
    const char *owner_name, Node *field) {
  char type_buf[256];
  char field_name[128];
  const char *name = tbe_compiler_string_value(field, "name");
  const IdlField *typed_field =
      tbe_compiler_contract_field(contract, owner_name, name);
  schema_cmeta_field_type resolved;
  const schema_cmeta_field_type *semantic =
      schema_cmeta_field_resolve(contract, typed_field, &resolved)
          ? &resolved
          : NULL;

  tbe_compiler_pascal_identifier(name, field_name, sizeof(field_name));
  tbe_compiler_set_string(field, "go_name", field_name);

  tbe_compiler_field_type(field, semantic, tbe_compiler_cpp_scalar_type,
                          "std::vector<", ">", "std::set<", ">",
                          "std::map<", ", ", ">", type_buf, sizeof(type_buf));
  tbe_compiler_set_string(field, "cpp_type", type_buf);

  tbe_compiler_field_type(field, semantic, tbe_compiler_go_scalar_type,
                          "[]", "", "map[", "]struct{}",
                          "map[", "]", "", type_buf, sizeof(type_buf));
  tbe_compiler_set_string(field, "go_type", type_buf);

  tbe_compiler_field_type(field, semantic, tbe_compiler_ts_scalar_type,
                          "Array<", ">", "Set<", ">",
                          "Map<", ", ", ">", type_buf, sizeof(type_buf));
  tbe_compiler_set_string(field, "ts_type", type_buf);

  tbe_compiler_field_type(field, semantic, tbe_compiler_python_scalar_type,
                          "list[", "]", "set[", "]",
                          "dict[", ", ", "]", type_buf, sizeof(type_buf));
  tbe_compiler_set_string(field, "python_type", type_buf);

  tbe_compiler_field_type(field, semantic, tbe_compiler_rust_scalar_type,
                          "Vec<", ">", "std::collections::HashSet<", ">",
                          "std::collections::HashMap<", ", ", ">", type_buf, sizeof(type_buf));
  tbe_compiler_set_string(field, "rust_type", type_buf);

  tbe_compiler_field_type(field, semantic, tbe_compiler_rfl_scalar_type,
                          "List<", ">", "Set<", ">",
                          "Map<", ", ", ">", type_buf, sizeof(type_buf));
  tbe_compiler_set_string(field, "rfl_type", type_buf);
  tbe_compiler_annotate_typed_field(root, field, semantic);
  tbe_compiler_annotate_native_requirement(root, field, semantic);
}

static void tbe_compiler_annotate_record_list_types(
    Node *root, const IdlContract *contract, const char *list_name) {
  Node *list = tbe_compiler_find_child(root, list_name);
  if (!list || list->type != NODE_LIST) return;

  for (size_t i = 0; i < list->data.list.count; ++i) {
    Node *record = list->data.list.items[i];
    const char *owner_name = tbe_compiler_string_value(record, "name");
    Node *fields = tbe_compiler_find_child(record, "fields");
    if (!fields || fields->type != NODE_LIST) continue;

    for (size_t j = 0; j < fields->data.list.count; ++j) {
      tbe_compiler_annotate_field_types(
          root, contract, owner_name, fields->data.list.items[j]);
    }
  }
}

static const char *tbe_compiler_c_enum_underlying_type(const char *type, int is_flags) {
  const tbe_compiler_scalar_projection_t *integer_type;
  const char *fallback = is_flags ? "uint32_t" : "int32_t";

  if (!type || !type[0]) return fallback;
  integer_type = tbe_compiler_integer_type(type);
  if (integer_type) return integer_type->c_type;
  return fallback;
}

static void tbe_compiler_annotate_enum_types(Node *root) {
  Node *enums = tbe_compiler_find_child(root, "enums");
  if (!enums || enums->type != NODE_LIST) return;

  for (size_t i = 0; i < enums->data.list.count; ++i) {
    Node *enum_node = enums->data.list.items[i];
    const char *underlying = tbe_compiler_string_value(enum_node, "underlying_type");
    const int is_flags = tbe_compiler_has_child(enum_node, "is_flags");
    const tbe_compiler_scalar_projection_t *storage = tbe_compiler_integer_type(underlying);

    if (tbe_compiler_set_enum_symbol(enum_node, "native_enum_symbol",
          tbe_compiler_string_value(enum_node, "enum_name"), "") != 0)
      continue;
    if (storage) {
      char bits[4];
      snprintf(bits, sizeof(bits), "%u",
               ((const cmeta_data_integer_shape *)storage->data->shape)->bits);
      tbe_compiler_set_string(enum_node, "native_enum_bits", bits);
      tbe_compiler_set_string(enum_node, "native_enum_signedness",
          storage->data->kind == CMETA_DATA_SINT ? "CMETA_ENUM_SIGNED"
                                                 : "CMETA_ENUM_UNSIGNED");
      if (storage->data->kind == CMETA_DATA_SINT)
        tbe_compiler_set_string(enum_node, "native_enum_signed", "1");
      tbe_compiler_set_string(enum_node, "native_enum_supported", "1");
    }

    tbe_compiler_set_string(enum_node, "go_underlying_type",
                            tbe_compiler_go_scalar_type(underlying));
    tbe_compiler_set_string(enum_node, "cpp_underlying_type",
                            tbe_compiler_cpp_enum_underlying_type(underlying));
    tbe_compiler_set_string(enum_node, "rust_underlying_type",
                            tbe_compiler_rust_enum_underlying_type(underlying));
    tbe_compiler_set_string(enum_node, "rfl_underlying_type",
                            underlying && strcmp(underlying, "uint32") == 0 ? "long" :
                                tbe_compiler_rfl_scalar_type(underlying));
    tbe_compiler_set_string(enum_node, "c_underlying_type",
                            tbe_compiler_c_enum_underlying_type(underlying, is_flags));
  }
}

enum {
  TBE_COMPILER_CMETA_UNVISITED = 0,
  TBE_COMPILER_CMETA_VISITING = 1,
  TBE_COMPILER_CMETA_SUPPORTED = 2,
  TBE_COMPILER_CMETA_UNSUPPORTED = 3,
  TBE_COMPILER_CMETA_MAX_DEPTH = 32
};

typedef struct tbe_compiler_cmeta_classify_context_s {
  Node *root;
  Node **records;
  unsigned char *states;
  size_t *depths;
  size_t *native_depths;
  size_t *native_nodes;
  size_t count;
  int lifecycle;
} tbe_compiler_cmeta_classify_context_t;

static size_t tbe_compiler_cmeta_record_index(
    const tbe_compiler_cmeta_classify_context_t *context, const Node *record) {
  size_t i;
  if (!context || !record) return SIZE_MAX;
  for (i = 0; i < context->count; ++i)
    if (context->records[i] == record) return i;
  return SIZE_MAX;
}

static Node *tbe_compiler_find_any_record(Node *root, const char *name) {
  Node *record = tbe_compiler_find_record(root, "composites", name);
  if (!record) record = tbe_compiler_find_record(root, "groups", name);
  if (!record) record = tbe_compiler_find_record(root, "messages", name);
  return record;
}

typedef struct databind_generic_budget {
  size_t depth;
  size_t nodes;
  size_t record_depth;
} databind_generic_budget;

static int tbe_compiler_cmeta_classify_record(
    tbe_compiler_cmeta_classify_context_t *context, size_t index);

static int databind_generic_classify(
    tbe_compiler_cmeta_classify_context_t *context, const IdlTypeRef *type,
    databind_generic_budget *out) {
  Node *record;
  const tbe_compiler_scalar_projection_t *scalar;
  char name[256];
  size_t index;
  if (type->collection_kind != IDL_COLLECTION_NONE) {
    IdlTypeRef child;
    databind_generic_budget budget;
    int is_map = type->collection_kind == IDL_COLLECTION_MAP;
    size_t own_nodes = is_map ? 2u : 1u;
    if ((!is_map && type->collection_kind != IDL_COLLECTION_LIST) ||
        (is_map && (type->argument_lengths[0] != sizeof("string") - 1u ||
                    memcmp(type->arguments[0], "string", sizeof("string") - 1u) != 0)) ||
        !idl_type_ref_parse(type->arguments[is_map ? 1u : 0u],
                            type->argument_lengths[is_map ? 1u : 0u], &child) ||
        !databind_generic_classify(context, &child, &budget) ||
        budget.depth >= TBE_COMPILER_CMETA_MAX_DEPTH ||
        budget.nodes > SIZE_MAX - own_nodes)
      return 0;
    out->depth = budget.depth + 1u;
    out->nodes = budget.nodes + own_nodes;
    out->record_depth = budget.record_depth;
    return 1;
  }
  if (type->name_length >= sizeof(name)) return 0;
  memcpy(name, type->name, type->name_length);
  name[type->name_length] = '\0';
  record = tbe_compiler_find_any_record(context->root, name);
  if (record != NULL) {
    index = tbe_compiler_cmeta_record_index(context, record);
    if (index == SIZE_MAX ||
        !tbe_compiler_cmeta_classify_record(context, index) ||
        !tbe_compiler_has_child(record, "cmeta_lifecycle_supported") ||
        context->depths[index] >= TBE_COMPILER_CMETA_MAX_DEPTH)
      return 0;
    out->record_depth = context->depths[index] + 1u;
    out->depth = context->lifecycle ? out->record_depth : context->native_depths[index];
    out->nodes = context->lifecycle ? 1u : context->native_nodes[index];
    return out->depth != 0u && out->nodes != 0u;
  }
  scalar = tbe_compiler_scalar_projection(name);
  record = tbe_compiler_find_record(context->root, "enums", name);
  if (!((scalar != NULL && scalar->native_data_symbol != NULL) ||
        strcmp(name, "string") == 0 || strcmp(name, "uuid") == 0 ||
        (record != NULL && tbe_compiler_has_child(record, "native_enum_supported"))))
    return 0;
  out->depth = 1u;
  out->nodes = 1u;
  out->record_depth = 0u;
  return 1;
}

static int tbe_compiler_cmeta_classify_record(
    tbe_compiler_cmeta_classify_context_t *context, size_t index) {
  Node *record;
  Node *fields;
  size_t max_depth = 0;
  size_t native_depth = 1u;
  size_t native_nodes = 1u;
  const int native_budget =
      context != NULL && !context->lifecycle;
  size_t i;

  if (!context || index >= context->count) return 0;
  if (context->states[index] == TBE_COMPILER_CMETA_SUPPORTED) return 1;
  if (context->states[index] == TBE_COMPILER_CMETA_UNSUPPORTED ||
      context->states[index] == TBE_COMPILER_CMETA_VISITING)
    return 0;

  context->states[index] = TBE_COMPILER_CMETA_VISITING;
  record = context->records[index];
  fields = tbe_compiler_find_child(record, "fields");
  if (!fields || fields->type != NODE_LIST) goto unsupported;

  for (i = 0; i < fields->data.list.count; ++i) {
    Node *field = fields->data.list.items[i];
    const char *type = tbe_compiler_string_value(field, "type");
    const int is_map = tbe_compiler_has_child(field, "is_map");
    const int is_set = tbe_compiler_has_child(field, "is_set");
    const tbe_compiler_scalar_projection_t *scalar;
    Node *target;
    size_t target_index;

    if (!type ||
        (context->lifecycle &&
         (tbe_compiler_has_child(field, "is_optional") ||
          tbe_compiler_has_child(field, "is_nullable"))) ||
        (is_set && !tbe_compiler_has_child(field, "native_cstl_set")) ||
        (tbe_compiler_has_child(field, "is_group_field") &&
         !tbe_compiler_has_child(field, "native_cstl_sequence")))
      goto unsupported;

    if (tbe_compiler_has_child(field, "native_nested_generic")) {
      const char *expression = tbe_compiler_string_value(field, "native_generic_logical_type");
      IdlTypeRef logical;
      databind_generic_budget budget;
      if (expression == NULL ||
          !idl_type_ref_parse(expression, strlen(expression), &logical) ||
          !databind_generic_classify(context, &logical, &budget) ||
          budget.depth >= TBE_COMPILER_CMETA_MAX_DEPTH ||
          native_nodes > SIZE_MAX - budget.nodes)
        goto unsupported;
      if (budget.record_depth > max_depth) max_depth = budget.record_depth;
      if (native_budget) {
        native_nodes += budget.nodes;
        if (budget.depth + 1u > native_depth) native_depth = budget.depth + 1u;
      }
      continue;
    }

    if (tbe_compiler_has_child(field, "native_fixed_array_name")) {
      const char *inner_type = tbe_compiler_string_value(field, "inner_type");
      size_t count = 0u;
      if (inner_type == NULL ||
          !tbe_compiler_parse_size(tbe_compiler_string_value(field, "typed_fixed_count"), &count) ||
          count == 0u ||
          tbe_compiler_string_value(field, "native_element_data_ref") == NULL ||
          tbe_compiler_string_value(field, "native_data_symbol") == NULL ||
          tbe_compiler_string_value(field, "native_type_symbol") == NULL)
        goto unsupported;
      target = tbe_compiler_find_any_record(context->root, inner_type);
      if (target != NULL) {
        target_index = tbe_compiler_cmeta_record_index(context, target);
        /* An inline owning array requires full element copy/move/destroy.
         * Local presence overlays are not part of canonical record traits. */
        if (target_index == SIZE_MAX ||
            !tbe_compiler_cmeta_classify_record(context, target_index) ||
            !tbe_compiler_has_child(target, "cmeta_lifecycle_supported") ||
            context->depths[target_index] >= TBE_COMPILER_CMETA_MAX_DEPTH)
          goto unsupported;
        if (context->depths[target_index] + 1u > max_depth)
          max_depth = context->depths[target_index] + 1u;
        if (native_budget) {
          size_t candidate_depth;
          if (context->native_depths[target_index] > SIZE_MAX - 2u ||
              native_nodes > SIZE_MAX - 1u ||
              native_nodes + 1u > SIZE_MAX - context->native_nodes[target_index])
            goto unsupported;
          candidate_depth = context->native_depths[target_index] + 2u;
          native_nodes += 1u + context->native_nodes[target_index];
          if (candidate_depth > native_depth) native_depth = candidate_depth;
        }
      } else {
        Node *enum_type = tbe_compiler_find_record(context->root, "enums", inner_type);
        scalar = tbe_compiler_scalar_projection(inner_type);
        if (!((scalar != NULL && scalar->native_data_symbol != NULL) ||
              strcmp(inner_type, "string") == 0 || strcmp(inner_type, "uuid") == 0 ||
              (enum_type != NULL && tbe_compiler_has_child(enum_type, "native_enum_supported"))))
          goto unsupported;
        if (native_budget) {
          if (native_nodes > SIZE_MAX - 2u) goto unsupported;
          native_nodes += 2u;
          if (native_depth < 3u) native_depth = 3u;
        }
      }
      continue;
    }

    if (tbe_compiler_has_child(field, "is_list") ||
        tbe_compiler_has_child(field, "is_group_field")) {
      const char *inner_type =
          tbe_compiler_string_value(field, "inner_type");
      const char *requirement =
          tbe_compiler_string_value(field, "cmeta_native_requirement");
      if (inner_type == NULL ||
          requirement == NULL ||
          strcmp(requirement, "sequence_provider") != 0 ||
          tbe_compiler_string_value(field, "native_data_symbol") == NULL ||
          tbe_compiler_string_value(field, "native_type_symbol") == NULL ||
          tbe_compiler_string_value(field, "native_element_data_ref") == NULL ||
          tbe_compiler_string_value(field, "native_element_type_ref") == NULL)
        goto unsupported;

      /*
       * Canonical typed-CSTL sequence storage owns its element semantics
       * directly through the generated *_collection_data descriptor. Graph
       * and lifecycle qualification therefore stop at the container provider
       * for the builtin scalar/string slice. Do not recurse into a synthetic
       * element record.
       */
      if (tbe_compiler_has_child(field, "native_cstl_sequence")) {
        scalar = tbe_compiler_scalar_projection(inner_type);
        if ((scalar != NULL && scalar->native_data_symbol != NULL) ||
            strcmp(inner_type, "string") == 0) {
          if (native_budget) {
            if (native_nodes > SIZE_MAX - 2u) goto unsupported;
            native_nodes += 2u;
            if (native_depth < 3u) native_depth = 3u;
          }
          continue;
        }

        target = tbe_compiler_find_any_record(context->root, inner_type);
        if (target == NULL) goto unsupported;
        target_index = tbe_compiler_cmeta_record_index(context, target);
        if (target_index == SIZE_MAX ||
            !tbe_compiler_cmeta_classify_record(context, target_index) ||
            context->depths[target_index] >= TBE_COMPILER_CMETA_MAX_DEPTH)
          goto unsupported;
        if (context->depths[target_index] + 1u > max_depth)
          max_depth = context->depths[target_index] + 1u;
        if (native_budget) {
          size_t candidate_depth;
          if (context->native_depths == NULL || context->native_nodes == NULL ||
              context->native_depths[target_index] == 0u ||
              context->native_nodes[target_index] == 0u ||
              context->native_depths[target_index] > SIZE_MAX - 2u ||
              native_nodes > SIZE_MAX - 1u ||
              native_nodes + 1u > SIZE_MAX - context->native_nodes[target_index])
            goto unsupported;
          candidate_depth = context->native_depths[target_index] + 2u;
          native_nodes += 1u + context->native_nodes[target_index];
          if (candidate_depth > native_depth) native_depth = candidate_depth;
        }
        continue;
      }

      scalar = tbe_compiler_scalar_projection(inner_type);
      if ((scalar && scalar->native_data_symbol) ||
          strcmp(inner_type, "string") == 0 ||
          strcmp(inner_type, "uuid") == 0 ||
          tbe_compiler_find_record(context->root, "enums", inner_type) != NULL)
        goto unsupported;

      target = tbe_compiler_find_any_record(context->root, inner_type);
      if (!target) goto unsupported;
      target_index = tbe_compiler_cmeta_record_index(context, target);
      if (target_index == SIZE_MAX ||
          !tbe_compiler_cmeta_classify_record(context, target_index) ||
          context->depths[target_index] >= TBE_COMPILER_CMETA_MAX_DEPTH)
        goto unsupported;
      if (context->depths[target_index] + 1u > max_depth)
        max_depth = context->depths[target_index] + 1u;
      if (native_budget) {
        size_t candidate_depth;
        if (context->native_depths == NULL || context->native_nodes == NULL ||
            context->native_depths[target_index] == 0u ||
            context->native_nodes[target_index] == 0u ||
            context->native_depths[target_index] > SIZE_MAX - 2u ||
            native_nodes > SIZE_MAX - 1u ||
            native_nodes + 1u > SIZE_MAX - context->native_nodes[target_index])
          goto unsupported;
        candidate_depth = context->native_depths[target_index] + 2u;
        native_nodes += 1u + context->native_nodes[target_index];
        if (candidate_depth > native_depth) native_depth = candidate_depth;
      }
      continue;
    }

    if (is_set) {
      const char *inner_type =
          tbe_compiler_string_value(field, "inner_type");
      const char *requirement =
          tbe_compiler_string_value(field, "cmeta_native_requirement");
      if (inner_type == NULL ||
          requirement == NULL ||
          strcmp(requirement, "set_provider") != 0 ||
          tbe_compiler_string_value(field, "native_data_symbol") == NULL ||
          tbe_compiler_string_value(field, "native_type_symbol") == NULL ||
          tbe_compiler_string_value(field, "native_element_data_ref") == NULL ||
          tbe_compiler_string_value(field, "native_element_type_ref") == NULL ||
          !tbe_compiler_has_child(field, "native_cstl_set"))
        goto unsupported;

      scalar = tbe_compiler_scalar_projection(inner_type);
      if ((scalar != NULL && scalar->native_data_symbol != NULL) ||
          strcmp(inner_type, "string") == 0) {
        if (native_budget) {
          if (native_nodes > SIZE_MAX - 2u) goto unsupported;
          native_nodes += 2u;
          if (native_depth < 3u) native_depth = 3u;
        }
        continue;
      }
      goto unsupported;
    }

    if (tbe_compiler_has_child(field, "is_collection") && !is_map)
      goto unsupported;

    if (is_map) {
      const char *key_type = tbe_compiler_string_value(field, "key_type");
      const char *value_type = tbe_compiler_string_value(field, "value_type");
      if (!key_type || strcmp(key_type, "string") != 0 || !value_type ||
          tbe_compiler_string_value(field, "native_data_symbol") == NULL ||
          tbe_compiler_string_value(field, "native_type_symbol") == NULL ||
          tbe_compiler_string_value(field, "native_map_value_data_symbol") == NULL)
        goto unsupported;

      if (tbe_compiler_has_child(field, "native_cstl_map")) {
        if (tbe_compiler_string_value(field, "native_map_key_type_ref") == NULL ||
            tbe_compiler_string_value(field, "native_map_key_data_ref") == NULL ||
            tbe_compiler_string_value(field, "native_map_value_type_ref") == NULL ||
            tbe_compiler_string_value(field, "native_map_value_data_ref") == NULL)
          goto unsupported;
        scalar = tbe_compiler_scalar_projection(value_type);
        if ((scalar != NULL && scalar->native_data_symbol != NULL) ||
            strcmp(value_type, "string") == 0) {
          if (native_budget) {
            if (native_nodes > SIZE_MAX - 3u) goto unsupported;
            native_nodes += 3u;
            if (native_depth < 3u) native_depth = 3u;
          }
          continue;
        }

        target = tbe_compiler_find_any_record(context->root, value_type);
        if (target == NULL) goto unsupported;
        target_index = tbe_compiler_cmeta_record_index(context, target);
        if (target_index == SIZE_MAX ||
            !tbe_compiler_cmeta_classify_record(context, target_index) ||
            context->depths[target_index] >= TBE_COMPILER_CMETA_MAX_DEPTH)
          goto unsupported;
        if (context->depths[target_index] + 1u > max_depth)
          max_depth = context->depths[target_index] + 1u;
        if (native_budget) {
          size_t candidate_depth;
          if (context->native_depths == NULL || context->native_nodes == NULL ||
              context->native_depths[target_index] == 0u ||
              context->native_nodes[target_index] == 0u ||
              context->native_depths[target_index] > SIZE_MAX - 2u ||
              native_nodes > SIZE_MAX - 2u ||
              native_nodes + 2u > SIZE_MAX - context->native_nodes[target_index])
            goto unsupported;
          candidate_depth = context->native_depths[target_index] + 2u;
          native_nodes += 2u + context->native_nodes[target_index];
          if (candidate_depth > native_depth) native_depth = candidate_depth;
        }
        continue;
      }

      /* An unpromoted vector-map graph has no canonical mutable provider;
       * reflection alone does not authorize lifecycle operations. */
      if (context->lifecycle) goto unsupported;
      scalar = tbe_compiler_scalar_projection(value_type);
      if ((scalar && scalar->native_data_symbol) ||
          strcmp(value_type, "string") == 0 ||
          strcmp(value_type, "uuid") == 0) {
        if (native_budget) {
          if (native_nodes > SIZE_MAX - 3u) goto unsupported;
          native_nodes += 3u;
          if (native_depth < 3u) native_depth = 3u;
        }
        continue;
      }
      target = tbe_compiler_find_record(context->root, "enums", value_type);
      if (target) {
        if (!tbe_compiler_has_child(target, "native_enum_supported"))
          goto unsupported;
        if (native_budget) {
          if (native_nodes > SIZE_MAX - 3u) goto unsupported;
          native_nodes += 3u;
          if (native_depth < 3u) native_depth = 3u;
        }
        continue;
      }
      target = tbe_compiler_find_any_record(context->root, value_type);
      if (!target) goto unsupported;
      target_index = tbe_compiler_cmeta_record_index(context, target);
      if (target_index == SIZE_MAX ||
          !tbe_compiler_cmeta_classify_record(context, target_index) ||
          context->depths[target_index] >= TBE_COMPILER_CMETA_MAX_DEPTH)
        goto unsupported;
      if (context->depths[target_index] + 1u > max_depth)
        max_depth = context->depths[target_index] + 1u;
      if (native_budget) {
        size_t candidate_depth;
        if (context->native_depths == NULL || context->native_nodes == NULL ||
            context->native_depths[target_index] == 0u ||
            context->native_nodes[target_index] == 0u ||
            context->native_depths[target_index] > SIZE_MAX - 2u ||
            native_nodes > SIZE_MAX - 2u ||
            native_nodes + 2u > SIZE_MAX - context->native_nodes[target_index])
          goto unsupported;
        candidate_depth = context->native_depths[target_index] + 2u;
        native_nodes += 2u + context->native_nodes[target_index];
        if (candidate_depth > native_depth) native_depth = candidate_depth;
      }
      continue;
    }

    scalar = tbe_compiler_scalar_projection(type);
    if (scalar) {
      if (!scalar->native_data_symbol || !scalar->native_type_symbol)
        goto unsupported;
      if (native_budget) {
        if (native_nodes == SIZE_MAX) goto unsupported;
        ++native_nodes;
        if (native_depth < 2u) native_depth = 2u;
      }
      continue;
    }

    /* Native lifecycle admission requires the canonical requirement and
     * concrete CMeta provider refs. */
    if (context->lifecycle &&
        tbe_compiler_string_value(field, "cmeta_native_requirement") != NULL &&
        strcmp(tbe_compiler_string_value(field, "cmeta_native_requirement"),
               "fixed_value") == 0 &&
        tbe_compiler_string_value(field, "native_data_symbol") != NULL &&
        tbe_compiler_string_value(field, "native_type_symbol") != NULL) {
      if (native_budget) {
        if (native_nodes == SIZE_MAX) goto unsupported;
        ++native_nodes;
        if (native_depth < 2u) native_depth = 2u;
      }
      continue;
    }

    if (context->lifecycle &&
        tbe_compiler_string_value(field, "cmeta_native_requirement") != NULL &&
        strcmp(tbe_compiler_string_value(field, "cmeta_native_requirement"),
               "owned_lifecycle") == 0 &&
        tbe_compiler_string_value(field, "native_data_symbol") != NULL &&
        tbe_compiler_string_value(field, "native_type_symbol") != NULL) {
      if (native_budget) {
        if (native_nodes == SIZE_MAX) goto unsupported;
        ++native_nodes;
        if (native_depth < 2u) native_depth = 2u;
      }
      continue;
    }

    target = tbe_compiler_find_record(context->root, "enums", type);
    if (target) {
      if (!tbe_compiler_has_child(target, "native_enum_supported"))
        goto unsupported;
      if (native_budget) {
        if (native_nodes == SIZE_MAX) goto unsupported;
        ++native_nodes;
        if (native_depth < 2u) native_depth = 2u;
      }
      continue;
    }

    target = tbe_compiler_find_any_record(context->root, type);
    if (target) {
      target_index = tbe_compiler_cmeta_record_index(context, target);
      if (target_index == SIZE_MAX ||
          !tbe_compiler_cmeta_classify_record(context, target_index) ||
          context->depths[target_index] >= TBE_COMPILER_CMETA_MAX_DEPTH)
        goto unsupported;
      if (context->depths[target_index] + 1u > max_depth)
        max_depth = context->depths[target_index] + 1u;
      if (native_budget) {
        size_t candidate_depth;
        if (context->native_depths == NULL || context->native_nodes == NULL ||
            context->native_depths[target_index] == 0u ||
            context->native_nodes[target_index] == 0u ||
            context->native_depths[target_index] == SIZE_MAX ||
            native_nodes > SIZE_MAX - context->native_nodes[target_index])
          goto unsupported;
        candidate_depth = context->native_depths[target_index] + 1u;
        native_nodes += context->native_nodes[target_index];
        if (candidate_depth > native_depth) native_depth = candidate_depth;
      }
      continue;
    }

    if (!context->lifecycle &&
        tbe_compiler_string_value(field, "native_data_symbol") != NULL &&
        tbe_compiler_string_value(field, "native_type_symbol") != NULL) {
      if (native_budget) {
        if (native_nodes == SIZE_MAX) goto unsupported;
        ++native_nodes;
        if (native_depth < 2u) native_depth = 2u;
      }
      continue;
    }
    goto unsupported;
  }

  if (tbe_compiler_set_string(
          record, context->lifecycle
                      ? "cmeta_lifecycle_supported"
                      : "cmeta_graph_supported",
          "1") != 0)
    goto unsupported;
  context->depths[index] = max_depth;
  if (native_budget) {
    char depth_text[32];
    char nodes_text[32];
    if (context->native_depths == NULL || context->native_nodes == NULL ||
        snprintf(depth_text, sizeof(depth_text), "%zu", native_depth) < 0 ||
        snprintf(nodes_text, sizeof(nodes_text), "%zu", native_nodes) < 0)
      goto unsupported;
    context->native_depths[index] = native_depth;
    context->native_nodes[index] = native_nodes;
    if (tbe_compiler_set_string(
            record, "cmeta_native_descriptor_depth", depth_text) != 0 ||
        tbe_compiler_set_string(
            record, "cmeta_native_descriptor_nodes", nodes_text) != 0)
      goto unsupported;
  }
  context->states[index] = TBE_COMPILER_CMETA_SUPPORTED;
  return 1;

unsupported:
  tbe_compiler_remove_children(
      record, context->lifecycle
                  ? "cmeta_lifecycle_supported"
                  : "cmeta_graph_supported");
  if (native_budget) {
    tbe_compiler_remove_children(record, "cmeta_native_descriptor_depth");
    tbe_compiler_remove_children(record, "cmeta_native_descriptor_nodes");
    if (context->native_depths != NULL) context->native_depths[index] = 0u;
    if (context->native_nodes != NULL) context->native_nodes[index] = 0u;
  }
  context->states[index] = TBE_COMPILER_CMETA_UNSUPPORTED;
  return 0;
}

static void tbe_compiler_collect_cmeta_records(
    tbe_compiler_cmeta_classify_context_t *context, const char *list_name,
    size_t *offset) {
  Node *list = tbe_compiler_find_child(context->root, list_name);
  size_t i;
  if (!list || list->type != NODE_LIST) return;
  for (i = 0; i < list->data.list.count; ++i) {
    Node *record = list->data.list.items[i];
    tbe_compiler_remove_children(
        record, context->lifecycle
                    ? "cmeta_lifecycle_supported"
                    : "cmeta_graph_supported");
    if (!context->lifecycle) {
      tbe_compiler_remove_children(record, "cmeta_native_descriptor_depth");
      tbe_compiler_remove_children(record, "cmeta_native_descriptor_nodes");
    }
    context->records[(*offset)++] = record;
  }
}

static void tbe_compiler_annotate_cmeta_support(Node *root) {
  static const char *const lists[] = {"composites", "groups", "messages"};
  tbe_compiler_cmeta_classify_context_t context = {0};
  size_t i;
  size_t offset = 0;

  context.root = root;
  for (i = 0; i < sizeof(lists) / sizeof(lists[0]); ++i) {
    Node *list = tbe_compiler_find_child(root, lists[i]);
    if (list && list->type == NODE_LIST) context.count += list->data.list.count;
  }
  if (context.count == 0u) return;
  context.records = (Node **)calloc(context.count, sizeof(*context.records));
  context.states = (unsigned char *)calloc(context.count, sizeof(*context.states));
  context.depths = (size_t *)calloc(context.count, sizeof(*context.depths));
  context.native_depths =
      (size_t *)calloc(context.count, sizeof(*context.native_depths));
  context.native_nodes =
      (size_t *)calloc(context.count, sizeof(*context.native_nodes));
  if (!context.records || !context.states || !context.depths ||
      !context.native_depths || !context.native_nodes)
    goto cleanup;

  for (i = 0; i < sizeof(lists) / sizeof(lists[0]); ++i)
    tbe_compiler_collect_cmeta_records(&context, lists[i], &offset);
  for (i = 0; i < context.count; ++i)
    (void)tbe_compiler_cmeta_classify_record(&context, i);

cleanup:
  free(context.native_nodes);
  free(context.native_depths);
  free(context.depths);
  free(context.states);
  free(context.records);
}

static void tbe_compiler_annotate_cmeta_lifecycle_support(Node *root) {
  static const char *const lists[] = {"composites", "groups", "messages"};
  tbe_compiler_cmeta_classify_context_t context = {0};
  size_t i;
  size_t offset = 0;

  context.root = root;
  context.lifecycle = 1;
  for (i = 0; i < sizeof(lists) / sizeof(lists[0]); ++i) {
    Node *list = tbe_compiler_find_child(root, lists[i]);
    if (list && list->type == NODE_LIST) context.count += list->data.list.count;
  }
  if (context.count == 0u) return;
  context.records = (Node **)calloc(context.count, sizeof(*context.records));
  context.states = (unsigned char *)calloc(context.count, sizeof(*context.states));
  context.depths = (size_t *)calloc(context.count, sizeof(*context.depths));
  context.native_depths =
      (size_t *)calloc(context.count, sizeof(*context.native_depths));
  context.native_nodes =
      (size_t *)calloc(context.count, sizeof(*context.native_nodes));
  if (!context.records || !context.states || !context.depths ||
      !context.native_depths || !context.native_nodes)
    goto cleanup;

  for (i = 0; i < sizeof(lists) / sizeof(lists[0]); ++i)
    tbe_compiler_collect_cmeta_records(&context, lists[i], &offset);
  for (i = 0; i < context.count; ++i)
    (void)tbe_compiler_cmeta_classify_record(&context, i);

cleanup:
  free(context.native_nodes);
  free(context.native_depths);
  free(context.depths);
  free(context.states);
  free(context.records);
}

static void tbe_compiler_promote_record_cstl_containers(Node *root) {
  static const char *const lists[] = {"composites", "groups", "messages"};
  size_t list_index;

  if (root == NULL) return;

  for (list_index = 0u;
       list_index < sizeof(lists) / sizeof(lists[0]);
       ++list_index) {
    Node *owners = tbe_compiler_find_child(root, lists[list_index]);
    size_t owner_index;
    if (owners == NULL || owners->type != NODE_LIST) continue;

    for (owner_index = 0u; owner_index < owners->data.list.count;
         ++owner_index) {
      Node *owner = owners->data.list.items[owner_index];
      Node *fields = tbe_compiler_find_child(owner, "fields");
      size_t field_index;
      if (fields == NULL || fields->type != NODE_LIST) continue;

      for (field_index = 0u; field_index < fields->data.list.count;
           ++field_index) {
        Node *field = fields->data.list.items[field_index];
        const char *inner_type;
        const char *vector_type;
        Node *element_record;
        char symbol[320];

        if (tbe_compiler_has_child(field, "is_map") &&
            !tbe_compiler_has_child(field, "is_optional") &&
            !tbe_compiler_has_child(field, "is_nullable") &&
            !tbe_compiler_has_child(field, "native_cstl_map")) {
          const char *value_type =
              tbe_compiler_string_value(field, "value_type");
          const char *owner_name =
              tbe_compiler_string_value(field, "owner_name");
          const char *field_name =
              tbe_compiler_string_value(field, "c_name");
          Node *value_record =
              value_type != NULL
                  ? tbe_compiler_find_any_record(root, value_type)
                  : NULL;
          char map_type[256];
          char declaration[512];

          if (value_record != NULL &&
              tbe_compiler_has_child(value_record,
                                      "cmeta_lifecycle_supported") &&
              owner_name != NULL && field_name != NULL &&
              tbe_compiler_string_value(
                  field, "native_map_key_type_ref") != NULL &&
              tbe_compiler_string_value(
                  field, "native_map_key_data_ref") != NULL &&
              tbe_compiler_string_value(
                  field, "native_map_value_type_ref") != NULL &&
              tbe_compiler_string_value(
                  field, "native_map_value_data_ref") != NULL &&
              snprintf(map_type, sizeof(map_type), "%s_%s_map_t",
                       owner_name, field_name) >= 0 &&
              strlen(owner_name) + strlen(field_name) +
                      strlen("__map_t") <
                  sizeof(map_type) &&
              snprintf(declaration, sizeof(declaration), "%s %s;",
                       map_type, field_name) >= 0) {
            tbe_compiler_set_string(field, "native_cstl_map", "1");
            tbe_compiler_set_string(
                field, "native_cstl_map_explicit_refs", "1");
            tbe_compiler_set_string(owner, "native_cstl_storage", "1");
            tbe_compiler_set_string(field, "typed_vector_type", map_type);
            tbe_compiler_set_string(field, "typed_declaration", declaration);

            tbe_compiler_remove_children(field, "native_data_symbol");
            tbe_compiler_remove_children(field, "native_type_symbol");
            tbe_compiler_set_string(
                field, "cmeta_native_requirement", "map_provider");

            if (snprintf(symbol, sizeof(symbol), "%s_map_data",
                         map_type) >= 0 &&
                strlen(map_type) + strlen("_map_data") <
                    sizeof(symbol))
              tbe_compiler_set_string(
                  field, "native_data_symbol", symbol);

            if (snprintf(symbol, sizeof(symbol), "%s_cmeta_type",
                         map_type) >= 0 &&
                strlen(map_type) + strlen("_cmeta_type") <
                    sizeof(symbol))
              tbe_compiler_set_string(
                  field, "native_type_symbol", symbol);

            if (tbe_compiler_string_value(
                    field, "native_data_symbol") != NULL &&
                tbe_compiler_string_value(
                    field, "native_type_symbol") != NULL)
              tbe_compiler_set_string(
                  field, "native_c_type", map_type);
            continue;
          }
        }

        if ((!tbe_compiler_has_child(field, "is_list") &&
             !tbe_compiler_has_child(field, "is_group_field")) ||
            (!tbe_compiler_has_child(field, "is_group_field") &&
             (tbe_compiler_has_child(field, "is_optional") ||
              tbe_compiler_has_child(field, "is_nullable"))) ||
            tbe_compiler_has_child(field, "native_cstl_sequence"))
          continue;

        inner_type = tbe_compiler_string_value(field, "inner_type");
        vector_type = tbe_compiler_string_value(field, "typed_vector_type");
        if (inner_type == NULL || vector_type == NULL) continue;

        element_record = tbe_compiler_find_any_record(root, inner_type);
        if (element_record == NULL ||
            !tbe_compiler_has_child(element_record,
                                    "cmeta_lifecycle_supported"))
          continue;

        if (tbe_compiler_string_value(field, "native_element_type_ref") == NULL ||
            tbe_compiler_string_value(field, "native_element_data_ref") == NULL)
          continue;

        tbe_compiler_set_string(field, "native_cstl_sequence", "1");
        tbe_compiler_set_string(
            field, "native_cstl_sequence_explicit_refs", "1");
        tbe_compiler_set_string(owner, "native_cstl_storage", "1");

        tbe_compiler_remove_children(field, "native_data_symbol");
        tbe_compiler_remove_children(field, "native_type_symbol");
        tbe_compiler_set_string(
            field, "cmeta_native_requirement", "sequence_provider");

        if (snprintf(symbol, sizeof(symbol), "%s_collection_data",
                     vector_type) < 0 ||
            strlen(vector_type) + strlen("_collection_data") >=
                sizeof(symbol))
          continue;
        tbe_compiler_set_string(field, "native_data_symbol", symbol);

        if (snprintf(symbol, sizeof(symbol), "%s_cmeta_type",
                     vector_type) < 0 ||
            strlen(vector_type) + strlen("_cmeta_type") >=
                sizeof(symbol))
          continue;
        tbe_compiler_set_string(field, "native_type_symbol", symbol);
        tbe_compiler_set_string(field, "native_c_type", vector_type);
      }
    }
  }
}


static void tbe_compiler_annotate_cmeta_declared_generics(Node *root) {
  static const char *const lists[] = {"composites", "groups", "messages"};
  size_t list_index;

  if (root == NULL) return;

  for (list_index = 0u;
       list_index < sizeof(lists) / sizeof(lists[0]);
       ++list_index) {
    Node *owners = tbe_compiler_find_child(root, lists[list_index]);
    size_t owner_index;
    if (owners == NULL || owners->type != NODE_LIST) continue;

    for (owner_index = 0u; owner_index < owners->data.list.count;
         ++owner_index) {
      Node *owner = owners->data.list.items[owner_index];
      Node *fields = tbe_compiler_find_child(owner, "fields");
      size_t field_index;
      if (fields == NULL || fields->type != NODE_LIST) continue;

      for (field_index = 0u; field_index < fields->data.list.count;
           ++field_index) {
        Node *field = fields->data.list.items[field_index];
        const char *owner_name =
            tbe_compiler_string_value(field, "owner_name");
        const char *field_name =
            tbe_compiler_string_value(field, "c_name");
        const char *native_type =
            tbe_compiler_string_value(field, "native_type_symbol");
        const char *arg0 = NULL;
        const char *arg1 = NULL;
        const char *constructor = NULL;
        const char *arity = NULL;
        char symbol[320];

        tbe_compiler_remove_children(field, "native_declared_type_symbol");
        tbe_compiler_remove_children(field, "native_generic_constructor_ref");
        tbe_compiler_remove_children(field, "native_generic_arity");
        tbe_compiler_remove_children(field, "native_generic_arg0_type_ref");
        tbe_compiler_remove_children(field, "native_generic_arg1_type_ref");

        if (tbe_compiler_has_child(field, "native_cstl_sequence")) {
          constructor = "&stl_vec_generic_desc";
          arity = "1";
          arg0 = tbe_compiler_string_value(
              field, "native_element_type_ref");
        } else if (tbe_compiler_has_child(field, "native_cstl_set")) {
          constructor = "&stl_set_generic_desc";
          arity = "1";
          arg0 = tbe_compiler_string_value(
              field, "native_element_type_ref");
        } else if (tbe_compiler_has_child(field, "native_cstl_map")) {
          constructor = "&stl_map_generic_desc";
          arity = "2";
          arg0 = tbe_compiler_string_value(
              field, "native_map_key_type_ref");
          arg1 = tbe_compiler_string_value(
              field, "native_map_value_type_ref");
        } else {
          continue;
        }

        /*
         * cmeta_declared_type describes semantic generic identity independently
         * from storage construction. Generated DataBind fields use typed CSTL
         * wrappers as their physical storage, so construction intentionally
         * remains NULL; the wrapper's canonical DataDesc owns lifecycle.
         */
        if (owner_name == NULL || field_name == NULL || native_type == NULL ||
            constructor == NULL || arity == NULL || arg0 == NULL ||
            (arg1 == NULL && strcmp(arity, "2") == 0))
          continue;

        if (snprintf(symbol, sizeof(symbol), "%s_%s_CMETA_DECLARED_TYPE",
                     owner_name, field_name) < 0 ||
            strlen(owner_name) + strlen(field_name) +
                    strlen("__CMETA_DECLARED_TYPE") >=
                sizeof(symbol))
          continue;

        tbe_compiler_set_string(
            field, "native_declared_type_symbol", symbol);
        tbe_compiler_set_string(
            field, "native_generic_constructor_ref", constructor);
        tbe_compiler_set_string(field, "native_generic_arity", arity);
        tbe_compiler_set_string(
            field, "native_generic_arg0_type_ref", arg0);
        if (arg1 != NULL)
          tbe_compiler_set_string(
              field, "native_generic_arg1_type_ref", arg1);
      }
    }
  }
}

static void tbe_compiler_annotate_schema_types(Node *root) {
  Node *schema = tbe_compiler_find_child(root, "schema");
  const char *schema_name;
  char package_name[128];
  size_t out = 0;

  if (!schema) {
    schema = create_node_map("schema");
    if (!schema || map_add(root, schema) != 0) {
      node_free(schema);
      return;
    }
    tbe_compiler_set_string(schema, "schema_name", "GeneratedSchema");
    tbe_compiler_set_string(schema, "schema_attributes_rendered", "");
    tbe_compiler_set_string(schema, "schema_wire_big_endian_value", "0");
  }

  schema_name = tbe_compiler_string_value(schema, "schema_name");

  if (!schema_name || !schema_name[0]) {
    tbe_compiler_set_string(schema, "go_package_name", "generated");
    return;
  }

  for (size_t i = 0; schema_name[i] && out + 1 < sizeof(package_name); ++i) {
    char c = schema_name[i];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
      package_name[out++] = c;
    } else if (out > 0 && package_name[out - 1] != '_') {
      package_name[out++] = '_';
    }
  }

  if (out == 0 || (package_name[0] >= '0' && package_name[0] <= '9')) {
    snprintf(package_name, sizeof(package_name), "generated");
  } else {
    package_name[out] = '\0';
  }

  tbe_compiler_set_string(schema, "go_package_name", package_name);
}

static void tbe_compiler_annotate_xml_flat_messages(Node *root) {
  Node *messages;
  size_t i;

  if (root == NULL) return;
  messages = tbe_compiler_find_child(root, "messages");
  if (messages == NULL || messages->type != NODE_LIST) return;

  for (i = 0u; i < messages->data.list.count; ++i) {
    Node *record = messages->data.list.items[i];
    Node *fields = tbe_compiler_find_child(record, "fields");
    size_t j;
    int supported = 1;
    int output_supported = 0;

    tbe_compiler_remove_children(record, "cmeta_native_xml_flat_supported");
    tbe_compiler_remove_children(record, "cmeta_native_xml_output_supported");

    /*
     * Flat XML publication composes two authorities:
     *   - CMeta owns the reflected physical fields;
     *   - DataBind MessagePlan ownss presence/null overlay state.
     *
     * Do not require whole-record cmeta_lifecycle_supported here: optional
     * and nullable overlay bytes are intentionally not CMeta fields. The
     * generated helper preflights physical move support at runtime and copies
     * overlay state through the exact native binding.
     */
    if (!tbe_compiler_has_child(record, "cmeta_graph_supported") ||
        tbe_compiler_string_value(
            record, "cmeta_native_descriptor_depth") == NULL ||
        tbe_compiler_string_value(
            record, "cmeta_native_descriptor_nodes") == NULL ||
        fields == NULL || fields->type != NODE_LIST)
      continue;

    output_supported =
        tbe_compiler_has_child(record, "cmeta_lifecycle_supported") ? 1 : 0;

    for (j = 0u; j < fields->data.list.count; ++j) {
      Node *field = fields->data.list.items[j];
      const char *type = tbe_compiler_string_value(field, "type");
      if (tbe_compiler_has_child(field, "is_collection") ||
          tbe_compiler_has_child(field, "is_group_field") ||
          (type != NULL &&
           (tbe_compiler_find_any_record(root, type) != NULL ||
            tbe_compiler_find_record(root, "unions", type) != NULL))) {
        supported = 0;
        output_supported = 0;
        break;
      }
      if (tbe_compiler_has_child(field, "is_nullable") ||
          tbe_compiler_has_child(field, "is_bytes"))
        output_supported = 0;
    }

    if (supported)
      (void)tbe_compiler_set_string(
          record, "cmeta_native_xml_flat_supported", "1");
    if (supported && output_supported)
      (void)tbe_compiler_set_string(
          record, "cmeta_native_xml_output_supported", "1");
  }
}

static int tbe_compiler_annotate_binary_reader_messages(
    Node *root, const IdlContract *contract,
    const Node *wire_ir) {
  Node *messages;
  size_t i;
  if (root == NULL || contract == NULL || wire_ir == NULL) return -1;
  messages = tbe_compiler_find_child(root, "messages");
  if (messages == NULL || messages->type != NODE_LIST) return 0;
  for (i = 0u; i < messages->data.list.count; ++i) {
    Node *record = messages->data.list.items[i];
    const char *name = tbe_compiler_string_value(record, "name");
    databind_binary_format_plan format = {0};
    databind_binary_execution_graph *graph = NULL;
    databind_binary_layout_status admission;
    tbe_error_t error = {0};
    tbe_compiler_remove_children(record, "binary_reader_supported");
    tbe_compiler_remove_children(record, "binary_overlay_supported");
    if (name == NULL) return -1;
    /* Native artifacts advertise Binary independently for each root. A
     * rejected wire shape receives the explicit unavailable Binary API;
     * infrastructure/allocation failures abort generation. */
    if (!databind_binary_format_plan_build_root(contract, wire_ir, name, &format, &error)) {
      if (error.code != TBE_ERR_SEMANTIC_ERROR) {
        fprintf(stderr, "Failed to project Binary root %s: %s\n", name, error.message);
        return -1;
      }
      continue;
    }
    admission = databind_binary_execution_graph_build(contract, &format, name, &graph, NULL);
    databind_binary_execution_graph_destroy(graph);
    databind_binary_format_plan_destroy(&format);
    if (admission == DATABIND_BINARY_LAYOUT_OUT_OF_MEMORY ||
        admission == DATABIND_BINARY_LAYOUT_INVALID_ARGUMENT ||
        admission == DATABIND_BINARY_LAYOUT_TYPE_NOT_FOUND) return -1;
    if (admission == DATABIND_BINARY_LAYOUT_OK) {
      if (tbe_compiler_set_string(record, "binary_reader_supported", "1") != 0)
        return -1;
      /* Canonical physical move and MessagePlan state publication also apply
       * to scalar/owned-buffer records without generated member lifecycles. */
      if (tbe_compiler_has_child(record, "cmeta_local_overlay_lifecycle") &&
          tbe_compiler_set_string(record, "binary_overlay_supported", "1") != 0)
        return -1;
    }
  }
  return 0;
}

static int tbe_compiler_append_binary_readers(
    const char *path, Node *root, const IdlContract *contract,
    const Node *wire_ir) {
  Node *messages;
  FILE *file;
  size_t i;
  if (path == NULL || root == NULL || contract == NULL ||
      wire_ir == NULL)
    return -1;
  messages = tbe_compiler_find_child(root, "messages");
  if (messages == NULL || messages->type != NODE_LIST) return 0;
  file = fopen(path, "ab");
  if (file == NULL) return -1;
  for (i = 0u; i < messages->data.list.count; ++i) {
    Node *record = messages->data.list.items[i];
    const char *name = tbe_compiler_string_value(record, "name");
    databind_binary_format_plan format = {0};
    tbe_error_t error = {0};
    int emitted;
    if (name == NULL ||
        !tbe_compiler_has_child(record, "binary_reader_supported"))
      continue;
    if (!databind_binary_format_plan_build_root(contract, wire_ir, name, &format, &error)) {
      fclose(file);
      return -1;
    }
    emitted = fputc('\n', file) != EOF
        ? databind_compiler_binary_reader_emit(file, contract, &format, name, name) : -1;
    databind_binary_format_plan_destroy(&format);
    if (emitted != 0) { fclose(file); return -1; }
  }
  return fclose(file) == 0 ? 0 : -1;
}

static void tbe_compiler_annotate_csv_flat_messages(Node *root) {
  Node *messages;
  size_t i;

  if (root == NULL) return;
  messages = tbe_compiler_find_child(root, "messages");
  if (messages == NULL || messages->type != NODE_LIST) return;

  for (i = 0u; i < messages->data.list.count; ++i) {
    Node *record = messages->data.list.items[i];
    Node *fields = tbe_compiler_find_child(record, "fields");
    size_t j;
    int supported = 1;

    tbe_compiler_remove_children(record, "cmeta_native_csv_flat_supported");

    /*
     * Canonical CSV v1 is deliberately narrower than the historical typed
     * route. Row selection is exact and flat headers are canonicalized through
     * FormatPlan, but nested paths, explicit NULL and invalid-scalar default
     * fallback are not approximated here.
     */
    if (!tbe_compiler_has_child(record, "cmeta_graph_supported") ||
        !tbe_compiler_has_child(record, "cmeta_lifecycle_supported") ||
        tbe_compiler_string_value(
            record, "cmeta_native_descriptor_depth") == NULL ||
        tbe_compiler_string_value(
            record, "cmeta_native_descriptor_nodes") == NULL ||
        fields == NULL || fields->type != NODE_LIST)
      continue;

    for (j = 0u; j < fields->data.list.count; ++j) {
      Node *field = fields->data.list.items[j];
      const char *type = tbe_compiler_string_value(field, "type");
      if (tbe_compiler_has_child(field, "is_collection") ||
          tbe_compiler_has_child(field, "is_group_field") ||
          tbe_compiler_has_child(field, "is_nullable") ||
          tbe_compiler_has_child(field, "has_default") ||
          (type != NULL &&
           (tbe_compiler_find_any_record(root, type) != NULL ||
            tbe_compiler_find_record(root, "unions", type) != NULL))) {
        supported = 0;
        break;
      }
    }

    if (supported)
      (void)tbe_compiler_set_string(
          record, "cmeta_native_csv_flat_supported", "1");
  }
}

static int tbe_compiler_has_local_overlay_lifecycle(Node *root, Node *record) {
  Node *fields = tbe_compiler_find_child(record, "fields");
  size_t j;
  int has_state = 0;

  if (!tbe_compiler_has_child(record, "cmeta_graph_supported") ||
      tbe_compiler_has_child(record, "cmeta_lifecycle_supported") ||
      fields == NULL || fields->type != NODE_LIST)
    return 0;

  for (j = 0u; j < fields->data.list.count; ++j) {
    Node *field = fields->data.list.items[j];
    const char *type = tbe_compiler_string_value(field, "type");
    const char *requirement =
        tbe_compiler_string_value(field, "cmeta_native_requirement");
    const char *native_data =
        tbe_compiler_string_value(field, "native_data_symbol");
    Node *nested_record =
        type != NULL ? tbe_compiler_find_any_record(root, type) : NULL;
    const int nested_overlay =
        nested_record != NULL &&
        tbe_compiler_has_child(nested_record, "cmeta_local_overlay_lifecycle");
    const int nested_lifecycle =
        nested_record != NULL &&
        tbe_compiler_has_child(nested_record, "cmeta_lifecycle_supported");
    Node *enum_record =
        type != NULL ? tbe_compiler_find_record(root, "enums", type) : NULL;
    const int native_enum =
        enum_record != NULL &&
        tbe_compiler_has_child(enum_record, "native_enum_supported") &&
        native_data != NULL &&
        tbe_compiler_string_value(field, "native_type_symbol") != NULL;
    const int native_uuid =
        type != NULL && strcmp(type, "uuid") == 0 &&
        native_data != NULL &&
        strcmp(native_data, "salts_uuid_cmeta_data") == 0 &&
        tbe_compiler_string_value(field, "native_type_symbol") != NULL &&
        strcmp(tbe_compiler_string_value(field, "native_type_symbol"),
               "salts_uuid_cmeta_type") == 0;
    const int fixed_bytes =
        type != NULL && strcmp(type, "bytes") == 0 &&
        tbe_compiler_has_child(field, "is_fixed_size") &&
        tbe_compiler_string_value(field, "native_fixed_bytes_name") != NULL &&
        native_data != NULL &&
        tbe_compiler_string_value(field, "native_type_symbol") != NULL;
    const int owned_storage =
        type != NULL && requirement != NULL && native_data != NULL &&
        (strcmp(requirement, "owned_lifecycle") == 0 ||
         strcmp(requirement, "overlay_presence") == 0 ||
         strcmp(requirement, "overlay_null") == 0 ||
         strcmp(requirement, "overlay_presence_null") == 0) &&
        ((strcmp(type, "string") == 0 &&
          strcmp(native_data, "salts_tstr_cmeta_data") == 0) ||
         (strcmp(type, "bytes") == 0 &&
          strcmp(native_data, "stl_byte_buffer_cmeta_data") == 0));
    const int owned_group =
        tbe_compiler_has_child(field, "is_group_field") &&
        tbe_compiler_has_child(field, "native_cstl_sequence") &&
        native_data != NULL &&
        tbe_compiler_string_value(field, "native_type_symbol") != NULL;
    const int fixed_array =
        tbe_compiler_has_child(field, "native_fixed_array_name") &&
        native_data != NULL &&
        tbe_compiler_string_value(field, "native_type_symbol") != NULL;
    if (type == NULL ||
        (tbe_compiler_has_child(field, "is_collection") && !fixed_array) ||
        (tbe_compiler_has_child(field, "is_group_field") && !owned_group) ||
        (tbe_compiler_scalar_projection(type) == NULL && !owned_storage &&
         !fixed_bytes && !nested_lifecycle && !nested_overlay && !native_enum &&
         !native_uuid && !owned_group && !fixed_array)) {
      return 0;
    }
    if (tbe_compiler_has_child(field, "is_optional") ||
        tbe_compiler_has_child(field, "is_nullable") || nested_overlay)
      has_state = 1;
  }

  /* Only local init/clear is admitted. Container move still requires a
   * separate protocol for the semantic state bits. */
  return has_state;
}

static void tbe_compiler_annotate_local_overlay_lifecycle(Node *root) {
  Node *messages = tbe_compiler_find_child(root, "messages");
  size_t i;
  int changed;
  if (messages == NULL || messages->type != NODE_LIST) return;
  for (i = 0u; i < messages->data.list.count; ++i)
    tbe_compiler_remove_children(messages->data.list.items[i],
                                 "cmeta_local_overlay_lifecycle");

  /* At most R admission passes, O(R^2 * F) including record-name lookup.
   * A published graph is required, so cycles and excessive depth stay closed. */
  do {
    changed = 0;
    for (i = 0u; i < messages->data.list.count; ++i) {
      Node *record = messages->data.list.items[i];
      if (!tbe_compiler_has_child(record, "cmeta_local_overlay_lifecycle") &&
          tbe_compiler_has_local_overlay_lifecycle(root, record) &&
          tbe_compiler_set_string(record, "cmeta_local_overlay_lifecycle", "1") == 0)
        changed = 1;
    }
  } while (changed);
}

static int databind_nested_generic_lifecycle_supported(Node *root, Node *field) {
  const char *expression = tbe_compiler_string_value(field, "native_generic_logical_type");
  IdlTypeRef type;
  char name[256];
  Node *record;
  const tbe_compiler_scalar_projection_t *scalar;
  if (expression == NULL ||
      !idl_type_ref_parse(expression, strlen(expression), &type)) return 0;
  while (type.collection_kind != IDL_COLLECTION_NONE) {
    int is_map = type.collection_kind == IDL_COLLECTION_MAP;
    if ((!is_map && type.collection_kind != IDL_COLLECTION_LIST) ||
        (is_map && (type.argument_lengths[0] != sizeof("string") - 1u ||
                    memcmp(type.arguments[0], "string", sizeof("string") - 1u) != 0)) ||
        !idl_type_ref_parse(type.arguments[is_map ? 1u : 0u],
                            type.argument_lengths[is_map ? 1u : 0u], &type)) return 0;
  }
  if (type.name_length >= sizeof(name)) return 0;
  memcpy(name, type.name, type.name_length);
  name[type.name_length] = '\0';
  record = tbe_compiler_find_any_record(root, name);
  if (record != NULL)
    return tbe_compiler_has_child(record, "cmeta_lifecycle_supported");
  scalar = tbe_compiler_scalar_projection(name);
  return (scalar != NULL && scalar->native_data_symbol != NULL) ||
         strcmp(name, "string") == 0 || strcmp(name, "uuid") == 0;
}

static int tbe_compiler_member_lifecycle_field(Node *root, Node *field) {
  const char *type = tbe_compiler_string_value(field, "type");
  Node *record;
  if (type == NULL) return 0;
  /* Nested declarations name storage before record traits are classified.
   * Local presence cleanup cannot authorize an owning element copy. */
  if (tbe_compiler_has_child(field, "native_nested_generic") &&
      !databind_nested_generic_lifecycle_supported(root, field)) return 0;
  if (tbe_compiler_has_child(field, "native_fixed_array_name") ||
      tbe_compiler_has_child(field, "native_fixed_bytes_name")) {
    size_t count = 0u;
    if (!tbe_compiler_parse_size(
            tbe_compiler_string_value(field, "typed_fixed_count"), &count) ||
        count == 0u)
      return 0;
    if (tbe_compiler_has_child(field, "native_fixed_array_name")) {
      const char *inner = tbe_compiler_string_value(field, "inner_type");
      if (inner == NULL ||
          tbe_compiler_string_value(field, "native_element_type_ref") == NULL ||
          tbe_compiler_string_value(field, "native_element_data_ref") == NULL)
        return 0;
      record = tbe_compiler_find_any_record(root, inner);
      if (record != NULL &&
          !tbe_compiler_has_child(record, "cmeta_lifecycle_supported"))
        return 0;
    }
  }
  /* Storage promotion has already proved the exact element/key/value traits.
   * A semantic overlay may keep the parent graph unpublished without taking
   * away the individual container provider's owning lifecycle. */
  if (tbe_compiler_has_child(field, "native_cstl_sequence") ||
      tbe_compiler_has_child(field, "native_cstl_set") ||
      tbe_compiler_has_child(field, "native_cstl_map") ||
      tbe_compiler_has_child(field, "native_fixed_array_name"))
    return tbe_compiler_string_value(field, "native_data_symbol") != NULL &&
           tbe_compiler_string_value(field, "native_type_symbol") != NULL;
  if (tbe_compiler_has_child(field, "is_collection") ||
      tbe_compiler_has_child(field, "is_group_field")) return 0;
  record = tbe_compiler_find_any_record(root, type);
  if (record != NULL)
    return tbe_compiler_has_child(record, "cmeta_lifecycle_supported") ||
           tbe_compiler_has_child(record, "cmeta_local_overlay_lifecycle") ||
           tbe_compiler_has_child(record, "cmeta_member_lifecycle");
  if (tbe_compiler_string_value(field, "native_data_symbol") == NULL ||
      tbe_compiler_string_value(field, "native_type_symbol") == NULL)
    return 0;
  record = tbe_compiler_find_record(root, "enums", type);
  return tbe_compiler_scalar_projection(type) != NULL ||
         (record != NULL && tbe_compiler_has_child(record, "native_enum_supported")) ||
         strcmp(type, "string") == 0 || strcmp(type, "uuid") == 0 ||
         (strcmp(type, "bytes") == 0 &&
          (!tbe_compiler_has_child(field, "is_fixed_size") ||
           tbe_compiler_has_child(field, "native_fixed_bytes_name")));
}

static int tbe_compiler_requires_member_lifecycle(Node *root, Node *fields) {
  size_t i;
  for (i = 0u; i < fields->data.list.count; ++i) {
    Node *field = fields->data.list.items[i];
    Node *nested = tbe_compiler_find_any_record(
        root, tbe_compiler_string_value(field, "type"));
    /* Keep generated init/clear delegated to the exact fixed provider,
     * including records whose local presence state prevents container traits. */
    if (tbe_compiler_string_value(field, "native_fixed_bytes_name") != NULL ||
        tbe_compiler_string_value(field, "native_fixed_array_name") != NULL ||
        (nested != NULL && tbe_compiler_has_child(nested, "cmeta_member_lifecycle")))
      return 1;
  }
  return 0;
}

static void tbe_compiler_annotate_member_lifecycle(Node *root) {
  static const char *const sections[] = {"composites", "groups", "messages"};
  size_t section_index;
  int changed;
  for (section_index = 0u; section_index < sizeof(sections) / sizeof(sections[0]);
       ++section_index) {
    Node *records = tbe_compiler_find_child(root, sections[section_index]);
    size_t i;
    if (records == NULL || records->type != NODE_LIST) continue;
    for (i = 0u; i < records->data.list.count; ++i)
      tbe_compiler_remove_children(records->data.list.items[i], "cmeta_member_lifecycle");
  }
  /* Each pass admits at least one record or stops: O(R^2 * F) including name
   * lookup for R records and F fields. Cycles never acquire a lifecycle
   * authority through this closure; runtime uses exact generated calls. */
  do {
    changed = 0;
    for (section_index = 0u; section_index < sizeof(sections) / sizeof(sections[0]);
         ++section_index) {
      Node *records = tbe_compiler_find_child(root, sections[section_index]);
      size_t i;
      if (records == NULL || records->type != NODE_LIST) continue;
      for (i = 0u; i < records->data.list.count; ++i) {
        Node *record = records->data.list.items[i];
        Node *fields = tbe_compiler_find_child(record, "fields");
        size_t j;
        if (tbe_compiler_has_child(record, "cmeta_member_lifecycle") ||
            fields == NULL || fields->type != NODE_LIST)
          continue;
        if (tbe_compiler_has_child(record, "cmeta_graph_supported") &&
            !tbe_compiler_requires_member_lifecycle(root, fields))
          continue;
        for (j = 0u; j < fields->data.list.count; ++j)
          if (!tbe_compiler_member_lifecycle_field(root, fields->data.list.items[j]))
            break;
        if (j == fields->data.list.count &&
            tbe_compiler_set_string(record, "cmeta_member_lifecycle", "1") == 0)
          changed = 1;
      }
    }
  } while (changed);
}

static int tbe_compiler_append_member_lifecycles(const char *path, Node *root) {
  static const char *const sections[] = {"composites", "groups", "messages"};
  static const char *const operations[] = {"init", "clear"};
  Node *schema = tbe_compiler_find_child(root, "schema");
  const char *schema_name = tbe_compiler_string_value(schema, "schema_name");
  FILE *file;
  size_t section_index;
  int failed;
  if (schema_name == NULL) return -1;
  file = fopen(path, "ab");
  if (file == NULL) return -1;
  for (section_index = 0u; section_index < sizeof(sections) / sizeof(sections[0]);
       ++section_index) {
    Node *records = tbe_compiler_find_child(root, sections[section_index]);
    size_t i;
    if (records == NULL || records->type != NODE_LIST) continue;
    for (i = 0u; i < records->data.list.count; ++i) {
      Node *record = records->data.list.items[i];
      Node *fields = tbe_compiler_find_child(record, "fields");
      const char *name = tbe_compiler_string_value(record, "name");
      size_t operation;
      if (!tbe_compiler_has_child(record, "cmeta_member_lifecycle")) continue;
      for (operation = 0u; operation < sizeof(operations) / sizeof(operations[0]);
           ++operation) {
        size_t j;
        fprintf(file, "\nvoid %s_%s(%s_t *object) {\n"
                      "    if (object == NULL) return;\n"
                      "    %s_CMETA_INIT_ALL();\n",
                name, operations[operation], name, schema_name);
        if (operation == 0u) fprintf(file, "    memset(object, 0, sizeof(*object));\n");
        for (j = 0u; j < fields->data.list.count; ++j) {
          Node *field = fields->data.list.items[j];
          const char *type = tbe_compiler_string_value(field, "type");
          const char *member = tbe_compiler_string_value(field, "c_name");
          Node *nested = tbe_compiler_find_any_record(root, type);
          if (nested != NULL &&
              tbe_compiler_has_child(nested, "cmeta_member_lifecycle")) {
            fprintf(file, "    %s_%s(&object->%s);\n", type, operations[operation], member);
          } else {
            /* The public void lifecycle cannot report a broken generated
             * provider contract. Match CMeta's destroy-only fail-fast rule. */
            fprintf(file, "    if (cmeta_data_value_%s(&%s, &object->%s) != CMETA_OK) abort();\n",
                    operation == 0u ? "init_zero" : "restore_zero",
                    tbe_compiler_string_value(field, "native_data_symbol"), member);
          }
        }
        if (operation != 0u) fprintf(file, "    memset(object, 0, sizeof(*object));\n");
        fprintf(file, "}\n");
      }
    }
  }
  failed = ferror(file);
  return fclose(file) == 0 && !failed ? 0 : -1;
}

void tbe_compiler_annotate_language_types(
    const IdlContract *contract, Node *root) {
  if (contract == NULL || root == NULL) return;
  tbe_compiler_remove_children(root, "native_generic_types");
  tbe_compiler_annotate_schema_types(root);
  tbe_compiler_annotate_enum_types(root);
  tbe_compiler_annotate_record_list_types(root, contract, "composites");
  tbe_compiler_annotate_record_list_types(root, contract, "groups");
  tbe_compiler_annotate_record_list_types(root, contract, "messages");
  tbe_compiler_annotate_record_list_types(root, contract, "unions");
  tbe_compiler_annotate_cmeta_lifecycle_support(root);
  tbe_compiler_promote_record_cstl_containers(root);
  tbe_compiler_annotate_cmeta_declared_generics(root);
  tbe_compiler_annotate_cmeta_lifecycle_support(root);
  tbe_compiler_annotate_cmeta_support(root);
  tbe_compiler_annotate_local_overlay_lifecycle(root);
  tbe_compiler_annotate_member_lifecycle(root);
  tbe_compiler_annotate_xml_flat_messages(root);
  tbe_compiler_annotate_csv_flat_messages(root);
}

static const char *tbe_compiler_path_basename(const char *path) {
  const char *base = path;
  const char *p;
  if (!path) return "generated.h";
  for (p = path; *p; ++p)
    if (*p == '/' || *p == '\\') base = p + 1;
  return base;
}

static char *tbe_compiler_escape_c_string(const char *text) {
  size_t len = text ? strlen(text) : 0;
  size_t capacity = len > (SIZE_MAX - 1) / 2 ? 0 : len * 2 + 1;
  char *out;
  size_t i;
  size_t used = 0;
  if (capacity == 0) return NULL;
  out = (char *)malloc(capacity);
  if (!out) return NULL;
  for (i = 0; i < len; ++i) {
    const char *escape = NULL;
    char ch = text[i];
    if (ch == '\\') escape = "\\\\";
    else if (ch == '"') escape = "\\\"";
    else if (ch == '\n') escape = "\\n";
    else if (ch == '\r') escape = "\\r";
    else if (ch == '\t') escape = "\\t";
    if (escape) {
      size_t n = strlen(escape);
      if (used + n + 1 > capacity) {
        size_t next = capacity * 2;
        char *grown;
        if (next < used + n + 1) next = used + n + 1;
        grown = (char *)realloc(out, next);
        if (!grown) {
          free(out);
          return NULL;
        }
        out = grown;
        capacity = next;
      }
      memcpy(out + used, escape, n);
      used += n;
    } else {
      if (used + 2 > capacity) {
        size_t next = capacity * 2;
        char *grown = (char *)realloc(out, next);
        if (!grown) {
          free(out);
          return NULL;
        }
        out = grown;
        capacity = next;
      }
      out[used++] = ch;
    }
  }
  out[used] = '\0';
  return out;
}

static int tbe_compiler_native_lifecycle_supported(Node *record) {
  /* A container marker describes individual storage, not the complete record.
   * Every exposed void lifecycle must have a proven whole-record authority. */
  return tbe_compiler_has_child(record, "cmeta_member_lifecycle") ||
         (tbe_compiler_has_child(record, "cmeta_graph_supported") &&
          (tbe_compiler_has_child(record, "cmeta_lifecycle_supported") ||
           tbe_compiler_has_child(record, "cmeta_local_overlay_lifecycle") ||
           tbe_compiler_has_child(record, "native_cstl_storage")));
}

static int tbe_compiler_native_field_type_supported(Node *root, Node *field) {
  const char *type = tbe_compiler_string_value(field, "type");
  char c_type[256];
  if (tbe_compiler_has_child(field, "native_nested_generic"))
    return databind_nested_generic_lifecycle_supported(root, field) &&
           tbe_compiler_string_value(field, "native_generic_declarations") != NULL &&
           tbe_compiler_string_value(field, "native_type_symbol") != NULL &&
           tbe_compiler_string_value(field, "native_data_symbol") != NULL;
  if (tbe_compiler_has_child(field, "is_group_field")) return 1;
  if (tbe_compiler_has_child(field, "is_collection")) {
    if (tbe_compiler_has_child(field, "is_map")) {
      const char *key = tbe_compiler_string_value(field, "key_type");
      if (!key || strcmp(key, "string") != 0) return 0;
      type = tbe_compiler_string_value(field, "value_type");
    } else {
      type = tbe_compiler_string_value(field, "inner_type");
    }
  }
  return tbe_compiler_native_named_c_type(root, type, c_type, sizeof(c_type));
}

static int tbe_compiler_typed_list_supported(Node *root, const char *list_name) {
  Node *list = tbe_compiler_find_child(root, list_name);
  size_t i;
  if (!list || list->type != NODE_LIST) return 1;
  for (i = 0; i < list->data.list.count; ++i) {
    Node *record = list->data.list.items[i];
    Node *fields = tbe_compiler_find_child(record, "fields");
    size_t j;
    if (!fields || fields->type != NODE_LIST) {
      fprintf(stderr, "Native C source record %s lacks field metadata\n",
              tbe_compiler_string_value(record, "name"));
      return 0;
    }
    for (j = 0; j < fields->data.list.count; ++j) {
      Node *field = fields->data.list.items[j];
      const char *c_name = tbe_compiler_string_value(field, "c_name");
      size_t k;
      if (!tbe_compiler_native_field_type_supported(root, field)) {
        fprintf(stderr, "Typed C serde does not support field %s.%s of type %s\n",
                tbe_compiler_string_value(field, "owner_name"),
                tbe_compiler_string_value(field, "name"),
                tbe_compiler_string_value(field, "type"));
        return 0;
      }
      /*
       * Native typed-source generation has one container storage authority:
       * canonical Salts CSTL providers. Historical raw-vec/private providers
       * are no longer a fallback. Any dynamic container/group shape that has
       * not been admitted to a canonical CSTL profile fails before rendering.
       */
      if (tbe_compiler_has_child(field, "typed_needs_map_vector") &&
          !tbe_compiler_has_child(field, "native_cstl_map")) {
        fprintf(stderr,
                "Typed C source field %s.%s lacks canonical CSTL Map storage\n",
                tbe_compiler_string_value(field, "owner_name"),
                tbe_compiler_string_value(field, "name"));
        return 0;
      }
      if (tbe_compiler_has_child(field, "typed_needs_vector") &&
          !tbe_compiler_has_child(field, "native_cstl_sequence") &&
          !tbe_compiler_has_child(field, "native_cstl_set")) {
        fprintf(stderr,
                "Typed C source field %s.%s lacks canonical CSTL sequence/set storage\n",
                tbe_compiler_string_value(field, "owner_name"),
                tbe_compiler_string_value(field, "name"));
        return 0;
      }
      if (tbe_compiler_attribute_count(field, "c") > 1u) {
        fprintf(stderr, "Typed C field %s.%s has multiple c member mappings\n",
                tbe_compiler_string_value(field, "owner_name"),
                tbe_compiler_string_value(field, "name"));
        return 0;
      }
      if (!tbe_compiler_c_identifier_valid(c_name)) {
        fprintf(stderr, "Typed C field %s.%s uses invalid member name %s\n",
                tbe_compiler_string_value(field, "owner_name"),
                tbe_compiler_string_value(field, "name"), c_name ? c_name : "(null)");
        return 0;
      }
      if (tbe_compiler_has_child(field, "is_optional")) {
        const char *bit_text = tbe_compiler_string_value(field, "optional_bit_index");
        const char *bitmap_text = tbe_compiler_string_value(record, "presence_bitmap_bytes");
        size_t bit;
        size_t bitmap_size;
        if (!tbe_compiler_parse_size(bit_text, &bit) ||
            !tbe_compiler_parse_size(bitmap_text, &bitmap_size) || bitmap_size == 0u ||
            bit / 8u >= bitmap_size) {
          fprintf(stderr, "Typed C optional field %s.%s has invalid presence metadata\n",
                  tbe_compiler_string_value(field, "owner_name"),
                  tbe_compiler_string_value(field, "name"));
          return 0;
        }
      }
      if (tbe_compiler_has_child(field, "is_nullable")) {
        const char *bit_text = tbe_compiler_string_value(field, "nullable_bit_index");
        const char *bitmap_text = tbe_compiler_string_value(record, "null_bitmap_bytes");
        size_t bit;
        size_t bitmap_size;
        if (!tbe_compiler_parse_size(bit_text, &bit) ||
            !tbe_compiler_parse_size(bitmap_text, &bitmap_size) || bitmap_size == 0u ||
            bit / 8u >= bitmap_size) {
          fprintf(stderr, "Typed C nullable field %s.%s has invalid null metadata\n",
                  tbe_compiler_string_value(field, "owner_name"),
                  tbe_compiler_string_value(field, "name"));
          return 0;
        }
      }
      if (c_name &&
          (strcmp(c_name, "_presence") == 0 || strcmp(c_name, "_nulls") == 0)) {
        fprintf(stderr, "Typed C field %s.%s uses reserved state member name %s\n",
                tbe_compiler_string_value(field, "owner_name"),
                tbe_compiler_string_value(field, "name"), c_name);
        return 0;
      }
      for (k = j + 1; k < fields->data.list.count; ++k) {
        const char *other_c_name =
            tbe_compiler_string_value(fields->data.list.items[k], "c_name");
        if (c_name && other_c_name && strcmp(c_name, other_c_name) == 0) {
          fprintf(stderr, "Typed C fields %s.%s and %s share member name %s\n",
                  tbe_compiler_string_value(field, "owner_name"),
                  tbe_compiler_string_value(field, "name"),
                  tbe_compiler_string_value(fields->data.list.items[k], "name"), c_name);
          return 0;
        }
      }
    }
    /* Preserve specific field diagnostics before checking record ownership. */
    if (!tbe_compiler_native_lifecycle_supported(record)) {
      fprintf(stderr, "Native C source record %s lacks canonical lifecycle support\n",
              tbe_compiler_string_value(record, "name"));
      return 0;
    }
  }
  return 1;
}

static int tbe_compiler_typed_schema_supported(Node *root) {
  Node *unions = tbe_compiler_find_child(root, "unions");
  if (unions && unions->type == NODE_LIST && unions->data.list.count != 0) {
    fprintf(stderr, "Typed C serde does not yet support union declarations\n");
    return 0;
  }
  return tbe_compiler_typed_list_supported(root, "composites") &&
         tbe_compiler_typed_list_supported(root, "groups") &&
         tbe_compiler_typed_list_supported(root, "messages");
}

char *tbe_compiler_read_file(const char *filename) {
  FILE *f = fopen(filename, "rb");
  if (!f) return NULL;

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }

  long file_size = ftell(f);
  if (file_size < 0) {
    fclose(f);
    return NULL;
  }

  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return NULL;
  }

  size_t size = (size_t)file_size;
  char *dat = (char *)malloc(size + 1);
  if (!dat) {
    fclose(f);
    return NULL;
  }

  size_t bytes_read = fread(dat, 1, size, f);
  fclose(f);
  if (bytes_read != size) {
    free(dat);
    return NULL;
  }

  dat[size] = '\0';
  return dat;
}

int tbe_compiler_parse_language_name(const char *name, int64_t *out_lang_enum) {
  static const struct {
    const char *name;
    int64_t lang_enum;
  } languages[] = {
      {"c", TBE_COMPILER_LANG_C},
      {"cpp", TBE_COMPILER_LANG_CPP},
      {"cxx", TBE_COMPILER_LANG_CPP},
      {"go", TBE_COMPILER_LANG_GO},
      {"rust", TBE_COMPILER_LANG_RUST},
      {"python", TBE_COMPILER_LANG_PYTHON},
      {"py", TBE_COMPILER_LANG_PYTHON},
      {"ts", TBE_COMPILER_LANG_TS},
      {"typescript", TBE_COMPILER_LANG_TS},
      {"sqlite", TBE_COMPILER_LANG_SQLITE},
      {"postgresql", TBE_COMPILER_LANG_POSTGRESQL},
      {"postgres", TBE_COMPILER_LANG_POSTGRESQL},
  };
  size_t i;

  if (!name || !out_lang_enum) return -1;

  for (i = 0; i < sizeof(languages) / sizeof(languages[0]); ++i) {
    if (strcmp(name, languages[i].name) == 0) {
      *out_lang_enum = languages[i].lang_enum;
      return 0;
    }
  }

  return -1;
}

static const char *tbe_compiler_language_name(int64_t lang_enum) {
  switch (lang_enum) {
    case TBE_COMPILER_LANG_C:
      return "c";
    case TBE_COMPILER_LANG_PYTHON:
      return "python";
    case TBE_COMPILER_LANG_RUST:
      return "rust";
    case TBE_COMPILER_LANG_CPP:
      return "cpp";
    case TBE_COMPILER_LANG_GO:
      return "go";
    case TBE_COMPILER_LANG_TS:
      return "ts";
    case TBE_COMPILER_LANG_SQLITE:
      return "sqlite";
    case TBE_COMPILER_LANG_POSTGRESQL:
      return "postgresql";
    default:
      return NULL;
  }
}

const char *tbe_compiler_resolve_template(const char *user_template,
                                          int64_t lang_enum) {
  if (user_template) return user_template;

  switch (lang_enum) {
    case TBE_COMPILER_LANG_C:
      return "templates/c_structs.mustache";
    case TBE_COMPILER_LANG_PYTHON:
      return "templates/python_dataclass.mustache";
    case TBE_COMPILER_LANG_RUST:
      return "templates/rust_structs.mustache";
    case TBE_COMPILER_LANG_CPP:
      return "templates/cpp_types.mustache";
    case TBE_COMPILER_LANG_GO:
      return "templates/go_types.mustache";
    case TBE_COMPILER_LANG_TS:
      return "templates/ts_types.mustache";
    case TBE_COMPILER_LANG_SQLITE:
      return "templates/sqlite_schema.mustache";
    case TBE_COMPILER_LANG_POSTGRESQL:
      return "templates/postgresql_schema.mustache";
    default:
      return NULL;
  }
}

static const char *tbe_compiler_resolve_resource(const tbe_compiler_options_t *options,
                                                 const char *relative_path, char *path,
                                                 size_t path_size) {
  const char *resource_dir = options->resource_dir;
#ifdef TBE_COMPILER_RESOURCE_DIR
  if (resource_dir == NULL || resource_dir[0] == '\0') resource_dir = TBE_COMPILER_RESOURCE_DIR;
#endif
  if (resource_dir == NULL || resource_dir[0] == '\0') {
    fprintf(stderr, "Built-in template resource directory is unavailable\n");
    return NULL;
  }
  if (salts_fs_path_join(path, path_size, resource_dir, relative_path) != 0) {
    fprintf(stderr, "Built-in template path is too long: %s\n", relative_path);
    return NULL;
  }
  return path;
}

static int tbe_compiler_is_database_language(int64_t lang_enum) {
  return lang_enum == TBE_COMPILER_LANG_SQLITE ||
         lang_enum == TBE_COMPILER_LANG_POSTGRESQL;
}

static int tbe_compiler_validate_database_output_option(const char *lang_name,
                                                        const char *option_name,
                                                        const char *option_value) {
  if (option_value == NULL) return 1;
  fprintf(stderr,
          "%s is supported only for the built-in C generator and cannot be combined with "
          "--lang %s\n",
          option_name, lang_name);
  return 0;
}

static int tbe_compiler_validate_options(const tbe_compiler_options_t *options,
                                         const char *lang_name) {
  if (options == NULL || lang_name == NULL) return 0;
  if (!tbe_compiler_is_database_language(options->lang_enum)) return 1;

  if (options->output_path == NULL || options->output_path[0] == '\0') {
    fprintf(stderr, "--lang %s requires explicit --output\n", lang_name);
    return 0;
  }
  if (!tbe_compiler_validate_database_output_option(
          lang_name, "--source-output", options->source_output_path) ||
      !tbe_compiler_validate_database_output_option(
          lang_name, "--guest-output", options->guest_output_path) ||
      !tbe_compiler_validate_database_output_option(
          lang_name, "--dsl-output", options->dsl_output_path)) {
    return 0;
  }
  return 1;
}

int databind_compiler_parse_contract_file(
    const char *schema_path, Node **out_legacy_tree,
    IdlContract **out_contract, char **out_schema_data) {
  tbe_error_t parse_err;
  IdlDiagnostic contract_error = IDL_DIAGNOSTIC_INIT;
  IdlContract *contract = NULL;
  Node *root = NULL;
  char *schema_data;

  if (out_legacy_tree != NULL) *out_legacy_tree = NULL;
  if (out_contract != NULL) *out_contract = NULL;
  if (out_schema_data != NULL) *out_schema_data = NULL;
  if (schema_path == NULL || out_legacy_tree == NULL ||
      out_contract == NULL || out_schema_data == NULL)
    return 1;

  schema_data = tbe_compiler_read_file(schema_path);
  if (!schema_data) {
    fprintf(stderr, "Failed to read IDL file: %s\n", schema_path);
    return 1;
  }

  root = create_node_map(NULL);
  if (!root) {
    fprintf(stderr, "Failed to allocate IDL frontend tree\n");
    free(schema_data);
    return 1;
  }

  if (idl_parse(schema_data, strlen(schema_data), root, &parse_err) != 0) {
    if (parse_err.line >= 0) {
      fprintf(stderr, "IDL parse error at line %d: %s\n", parse_err.line,
              parse_err.message);
    } else {
      fprintf(stderr, "IDL parse error: %s\n", parse_err.message);
    }
    free(schema_data);
    node_free(root);
    return 1;
  }

  /* Freeze format-neutral semantic truth before any TBE overlay mutates the
   * legacy rendering tree. */
  if (!idl_contract_build_from_tree(root, &contract, &contract_error)) {
    fprintf(stderr, "Failed to build typed IDL Contract IR: %s\n",
            contract_error.message);
    free(schema_data);
    node_free(root);
    return 1;
  }

  if (databind_binary_contract_apply(root, &parse_err) != 0) {
    fprintf(stderr, "TBE format error: %s\n", parse_err.message);
    idl_contract_destroy(contract);
    free(schema_data);
    node_free(root);
    return 1;
  }

  tbe_compiler_annotate_language_types(contract, root);

  *out_legacy_tree = root;
  *out_contract = contract;
  *out_schema_data = schema_data;
  return 0;
}

int tbe_compiler_parse_schema_file(
    const char *schema_path, Node **out_root, char **out_schema_data) {
  IdlContract *contract = NULL;
  int status = databind_compiler_parse_contract_file(
      schema_path, out_root, &contract, out_schema_data);
  idl_contract_destroy(contract);
  return status;
}

int tbe_compiler_render_file(Node *root, const char *template_path,
                             const char *output_path) {
  MUSTACHE_DATAPROVIDER provider = mustache_helpers_provider();
  MUSTACHE_RENDERER renderer = mustache_helpers_renderer();
  char *templ_data = tbe_compiler_read_file(template_path);
  MUSTACHE_TEMPLATE *templ = NULL;
  FILE *out_file = stdout;
  char *temporary_output_path = NULL;
  int temporary_output_open = 0;
  int temporary_output_owned = 0;
#ifndef _WIN32
  mode_t existing_output_mode = 0;
  int preserve_existing_output_mode = 0;
#endif
  int res = 1;

  if (!templ_data) {
    fprintf(stderr, "Failed to read template file: %s\n", template_path);
    return 1;
  }

  templ = mustache_compile(templ_data, strlen(templ_data), NULL, NULL, 0);
  if (!templ) {
    fprintf(stderr, "Failed to compile mustache template\n");
    goto cleanup;
  }

  if (output_path) {
#ifndef _WIN32
    struct stat output_status;
    if (stat(output_path, &output_status) == 0) {
      existing_output_mode = (mode_t)(output_status.st_mode & 0777);
      preserve_existing_output_mode = 1;
    } else if (errno != ENOENT) {
      fprintf(stderr, "Failed to inspect existing output file: %s\n", output_path);
      goto cleanup;
    }
#endif
    if (tbe_compiler_create_temporary_output(output_path, &temporary_output_path, &out_file) != 0) {
      fprintf(stderr, "Failed to create unique temporary output file for: %s\n", output_path);
      goto cleanup;
    }
    temporary_output_open = 1;
    temporary_output_owned = 1;
  }

  if (mustache_process(templ, &renderer, out_file, &provider, root)
      != MUSTACHE_ERR_SUCCESS) {
    fprintf(stderr, "Failed to render mustache template: %s\n", template_path);
    goto cleanup;
  }

  if (out_file != stdout) {
    int flush_status = fflush(out_file);
    int close_status = fclose(out_file);
    out_file = NULL;
    temporary_output_open = 0;
    if (flush_status != 0 || close_status != 0) {
      fprintf(stderr, "Failed to finalize temporary output file: %s\n", temporary_output_path);
      goto cleanup;
    }
#ifndef _WIN32
    if (preserve_existing_output_mode &&
        chmod(temporary_output_path, existing_output_mode) != 0) {
      fprintf(stderr, "Failed to preserve output file permissions: %s\n", output_path);
      goto cleanup;
    }
#endif
    if (salts_fs_rename(temporary_output_path, output_path) != 0) {
      fprintf(stderr, "Failed to replace output file: %s\n", output_path);
      goto cleanup;
    }
    temporary_output_owned = 0;
  }
  res = 0;

cleanup:
  if (temporary_output_open && out_file != NULL) fclose(out_file);
  if (temporary_output_owned) salts_fs_unlink(temporary_output_path);
  free(temporary_output_path);
  mustache_release(templ);
  free(templ_data);
  return res;
}

/* Reject target domains before touching any of the requested output paths. */
static int tbe_compiler_enum_is_ordinal(const IdlDataDecl *type) {
  size_t i;
  char expected[32];

  if (type == NULL || type->kind != IDL_DATA_ENUM ||
      type->enum_items == NULL || type->enum_item_count == 0u)
    return 0;

  for (i = 0u; i < type->enum_item_count; ++i) {
    int written = snprintf(expected, sizeof(expected), "%zu", i);
    if (written <= 0 || (size_t)written >= sizeof(expected) ||
        type->enum_items[i].value == NULL ||
        strcmp(type->enum_items[i].value, expected) != 0)
      return 0;
  }
  return 1;
}

static int tbe_compiler_validate_enum_backend(
    const IdlContract *contract,
    const tbe_compiler_options_t *options) {
  size_t i;

  if (contract == NULL || options == NULL) return 0;

  for (i = 0u; i < contract->data_count; ++i) {
    const IdlDataDecl *type = &contract->data[i];
    const char *storage;
    const char *name;

    if (type->kind != IDL_DATA_ENUM) continue;

    storage = type->underlying_type;
    name = type->name;
    if (storage == NULL || !tbe_compiler_integer_type(storage)) {
      fprintf(stderr, "Missing canonical enum storage for %s\n",
              name != NULL ? name : "<unnamed>");
      return 0;
    }
    if (options->lang_enum == TBE_COMPILER_LANG_TS &&
        (strcmp(storage, "int64") == 0 || strcmp(storage, "uint64") == 0)) {
      fprintf(stderr, "TypeScript numeric enum %s cannot preserve %s storage\n",
              name != NULL ? name : "<unnamed>", storage);
      return 0;
    }
    if (options->dsl_output_path && !tbe_compiler_enum_is_ordinal(type)) {
      fprintf(stderr,
              "RulesForge enum %s requires sequential values starting at zero\n",
              name != NULL ? name : "<unnamed>");
      return 0;
    }
  }
  return 1;
}

static Node *tbe_compiler_clone_canonical_node(const Node *node) {
  Node *copy = NULL;
  size_t i;

  if (node == NULL) return NULL;

  switch (node->type) {
  case NODE_STRING:
    return create_node_string(
        node->name,
        node->data.string_val != NULL ? node->data.string_val : "");

  case NODE_LIST:
    copy = create_node_list(node->name);
    if (copy == NULL) return NULL;
    for (i = 0u; i < node->data.list.count; ++i) {
      Node *child =
          tbe_compiler_clone_canonical_node(node->data.list.items[i]);
      if (child == NULL || list_add(copy, child) != 0) {
        node_free(child);
        node_free(copy);
        return NULL;
      }
    }
    return copy;

  case NODE_MAP:
    copy = create_node_map(node->name);
    if (copy == NULL) return NULL;
    for (i = 0u; i < node->data.map.count; ++i) {
      Node *child =
          tbe_compiler_clone_canonical_node(node->data.map.items[i]);
      if (child == NULL || map_add(copy, child) != 0) {
        node_free(child);
        node_free(copy);
        return NULL;
      }
    }
    return copy;

  case NODE_ROOT:
  default:
    return NULL;
  }
}

static int tbe_compiler_projection_requires_binary(
    const tbe_compiler_options_t *options) {
  size_t i;
  if (options == NULL) return 0;
  for (i = 0u; i < options->projection_count; ++i)
    if (options->projection_requests[i].id.axis ==
        DATABIND_COMPILER_PROJECTION_AXIS_TRANSPORT)
      return 1;
  return 0;
}

int tbe_compiler_run(const tbe_compiler_options_t *options) {
  Node *root = NULL;
  Node *projection_root = NULL;
  Node *database_ir = NULL;
  IdlContract *contract = NULL;
  databind_binary_format_plan binary_format = {0};
  char *schema_data = NULL;
  char template_path[SALTS_FS_MAX_PATH];
  const char *resolved_template = NULL;
  const char *lang_name = tbe_compiler_language_name(options->lang_enum);
  int database_language;
  if (lang_name == NULL) {
    fprintf(stderr, "Unsupported compiler language enum: %lld\n",
            (long long)options->lang_enum);
    return 1;
  }
  database_language = tbe_compiler_is_database_language(options->lang_enum);
  if (!tbe_compiler_validate_options(options, lang_name)) return 1;
  int status = databind_compiler_parse_contract_file(
      options->schema_path, &root, &contract, &schema_data);
  if (status != 0) return status;

  /*
   * 'contract' is the immutable, format-neutral semantic authority.
   * 'root' is a legacy TBE/rendering view retained only while projections and
   * templates are migrated to IdlContract.
   *
   * Preserve a separate legacy projection view because ordinary source
   * renderers may add output-only annotations. No new semantic fact may be
   * introduced through this Node tree.
   */
  if (options->projection_count != 0u || options->source_output_path != NULL) {
    tbe_error_t format_error;
    projection_root = tbe_compiler_clone_canonical_node(root);
    if (projection_root == NULL) {
      fprintf(stderr, "Failed to preserve legacy projection view\n");
      status = 1;
      goto cleanup;
    }
    tbe_error_init(&format_error);
    if (tbe_compiler_projection_requires_binary(options) &&
        !databind_binary_format_plan_build(
            contract, projection_root, &binary_format, &format_error)) {
      fprintf(stderr, "Failed to compile Binary format plan: %s\n", format_error.message);
      status = 1;
      goto cleanup;
    }
  }

  if (!tbe_compiler_validate_enum_backend(contract, options)) {
    status = 1;
    goto cleanup;
  }

  if (database_language) {
    tbe_database_schema_diagnostic_t diagnostic;
    tbe_database_dialect_t dialect = options->lang_enum == TBE_COMPILER_LANG_SQLITE
                                         ? TBE_DATABASE_DIALECT_SQLITE
                                         : TBE_DATABASE_DIALECT_POSTGRESQL;
    tbe_database_schema_status_t database_status =
        tbe_database_schema_build_contract(
            contract, dialect, &database_ir, &diagnostic);
    if (database_status != TBE_DATABASE_SCHEMA_STATUS_OK) {
      fprintf(stderr,
              "Database schema validation failed for --lang %s: message=%s field=%s %s\n",
              lang_name, diagnostic.message_name, diagnostic.field_name, diagnostic.context);
      status = 1;
      goto cleanup;
    }
  } else {
    if (tbe_compiler_set_string(root, "generated_header",
                                tbe_compiler_path_basename(options->output_path)) != 0) {
      status = 1;
      goto cleanup;
    }
    {
      char *schema_literal = tbe_compiler_escape_c_string(schema_data);
      if (!schema_literal || tbe_compiler_set_string(root, "schema_c_literal", schema_literal) != 0) {
        free(schema_literal);
        status = 1;
        goto cleanup;
      }
      free(schema_literal);
    }
  }

  if (options->source_output_path) {
    if (options->output_path == NULL || options->output_path[0] == '\0') {
      fprintf(stderr, "--source-output requires --output for the generated header\n");
      status = 1;
      goto cleanup;
    }
    if (strcmp(options->output_path, options->source_output_path) == 0) {
      fprintf(stderr, "--output and --source-output must name different files\n");
      status = 1;
      goto cleanup;
    }
    if (options->lang_enum != TBE_COMPILER_LANG_C || options->template_path != NULL) {
      fprintf(stderr, "--source-output is supported only for the built-in C generator\n");
      status = 1;
      goto cleanup;
    }
    if (!tbe_compiler_typed_schema_supported(root)) {
      status = 1;
      goto cleanup;
    }
    if (tbe_compiler_set_string(root, "typed_source_enabled", "1") != 0) {
      status = 1;
      goto cleanup;
    }
    if (tbe_compiler_annotate_binary_reader_messages(root, contract, projection_root) != 0) {
      fprintf(stderr, "Failed to evaluate generated Binary message admission\n");
      status = 1;
      goto cleanup;
    }
    if (tbe_compiler_prepare_native_text_cleanup(root) != 0) {
      fprintf(stderr, "Failed to prepare generated native text cleanup plan\n");
      status = 1;
      goto cleanup;
    }
  }

  if (options->guest_output_path) {
    if (options->output_path == NULL || options->output_path[0] == '\0') {
      fprintf(stderr, "--guest-output requires --output for the generated header\n");
      status = 1;
      goto cleanup;
    }
    if (strcmp(options->output_path, options->guest_output_path) == 0) {
      fprintf(stderr, "--output and --guest-output must name different files\n");
      status = 1;
      goto cleanup;
    }
    if (options->source_output_path != NULL &&
        strcmp(options->source_output_path, options->guest_output_path) == 0) {
      fprintf(stderr, "--source-output and --guest-output must name different files\n");
      status = 1;
      goto cleanup;
    }
    if (options->lang_enum != TBE_COMPILER_LANG_C || options->template_path != NULL) {
      fprintf(stderr, "--guest-output is supported only for the built-in C generator\n");
      status = 1;
      goto cleanup;
    }
    if (tbe_compiler_set_string(root, "guest_adapter_enabled", "1") != 0) {
      status = 1;
      goto cleanup;
    }
    if (options->source_output_path == NULL &&
        tbe_compiler_set_string(root, "guest_adapter_only", "1") != 0) {
      status = 1;
      goto cleanup;
    }
  }

  resolved_template = options->template_path;
  if (resolved_template == NULL) {
    resolved_template = tbe_compiler_resolve_resource(
        options, tbe_compiler_resolve_template(NULL, options->lang_enum), template_path,
        sizeof(template_path));
    if (resolved_template == NULL) {
      status = 1;
      goto cleanup;
    }
  }
  status = tbe_compiler_render_file(database_language ? database_ir : root,
                                    resolved_template, options->output_path);

  if (status == 0 && options->source_output_path) {
    resolved_template = tbe_compiler_resolve_resource(
        options, "templates/c_typed_source.mustache", template_path, sizeof(template_path));
    status = resolved_template != NULL
                 ? tbe_compiler_render_file(root, resolved_template, options->source_output_path)
                 : 1;
    if (status == 0 &&
        tbe_compiler_append_member_lifecycles(options->source_output_path, root) != 0) {
      fprintf(stderr, "Failed to append generated member lifecycle functions\n");
      status = 1;
    }
    if (status == 0 &&
        tbe_compiler_append_binary_readers(
            options->source_output_path, root, contract, projection_root) != 0) {
      fprintf(stderr, "Failed to append generated Binary reader providers\n");
      status = 1;
    }
  }

  if (status == 0 && options->guest_output_path) {
    resolved_template = tbe_compiler_resolve_resource(
        options, "templates/c_guest_adapter.mustache", template_path, sizeof(template_path));
    status = resolved_template != NULL
                 ? tbe_compiler_render_file(root, resolved_template, options->guest_output_path)
                 : 1;
  }

  if (status == 0 && options->dsl_output_path) {
    resolved_template = tbe_compiler_resolve_resource(
        options, "templates/rfl_types.mustache", template_path, sizeof(template_path));
    if (resolved_template == NULL ||
        tbe_compiler_render_file(root, resolved_template, options->dsl_output_path) != 0) {
      status = 1;
    }
  }

  if (status == 0 && options->projection_count != 0u) {
    databind_compiler_projection_input projection_input = {
        .contract = contract,
        .binary_format = &binary_format};
    if (databind_compiler_projection_run(
            &projection_input,
            options->projection_requests,
            options->projection_count,
            options->projection_backends,
            options->projection_backend_count) != 0)
      status = 1;
  }

cleanup:
  databind_binary_format_plan_destroy(&binary_format);
  idl_contract_destroy(contract);
  free(schema_data);
  tbe_database_schema_destroy(database_ir);
  node_free(projection_root);
  node_free(root);
  return status;
}
