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
#include "native_service_projection.h"
#include "wasm_projection.h"
#include "plugin_projection.h"
#include "native_source_ir.h"
#include "binary_layout_lowering.h"
#include "schema_cmeta.h"
#include <cmeta_cmeta_data.h>
#include <cmeta_cmeta_fixed_width.h>
#include "tbe_error.h"
#include "cmeta_fs.h"
#include "cmeta_uuid.h"
#include <tstr.h>
#include <cmeta/scope.h>
#include <cmeta/pp.h>

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
    cmeta_uuid_t uuid;
    char uuid_text[SALTS_UUID_STRING_SIZE];
    char *temporary_path;
    FILE *file;
    int descriptor;

    if (cmeta_uuid_v4_generate(&uuid) != SALTS_OK ||
        cmeta_uuid_format(&uuid, uuid_text, sizeof(uuid_text)) != SALTS_OK) {
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
      cmeta_fs_unlink(temporary_path);
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
    {&cmeta_data_bool, "uint8_t", "bool", "bool", "bool", "boolean", "bool", "boolean", "cmeta_bool8_cmeta_data", "cmeta_bool8_cmeta_type"},
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
  if (strcmp(type, "uuid") == 0) return "cmeta_uuid_t";
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
  if (strcmp(type, "uuid") == 0) return "cmeta_uuid_t";
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
                                   "cmeta_tstr_cmeta_type") == 0 &&
                   tbe_compiler_set_string(target_node, data_key,
                                           "cmeta_tstr_cmeta_data") == 0
               ? 0
               : -1;

  if (strcmp(type_name, "uuid") == 0)
    return tbe_compiler_set_string(target_node, type_key,
                                   "cmeta_uuid_cmeta_type") == 0 &&
                   tbe_compiler_set_string(target_node, data_key,
                                           "cmeta_uuid_cmeta_data") == 0
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
                                   "&cmeta_uuid_cmeta_type") == 0 &&
                   tbe_compiler_set_string(target_node, data_ref_key,
                                           "&cmeta_uuid_cmeta_data") == 0
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
                            "cmeta_tstr_cmeta_data");
    tbe_compiler_set_string(field, "native_type_symbol",
                            "cmeta_tstr_cmeta_type");
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
    } else if (semantic && cmeta_uuid_cmeta_data_valid(semantic->data)) {
      tbe_compiler_set_string(field, "native_data_symbol", "cmeta_uuid_cmeta_data");
      tbe_compiler_set_string(field, "native_type_symbol", "cmeta_uuid_cmeta_type");
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
              !cmeta_uuid_cmeta_data_valid(semantic->data))) {
    requirement = TBE_COMPILER_NATIVE_OWNED_LIFECYCLE;
  } else if (type && tbe_compiler_find_record(root, "enums", type)) {
    requirement = TBE_COMPILER_NATIVE_ENUM_DOMAIN;
  } else if (semantic->kind == CMETA_DATA_BOOL ||
             semantic->kind == CMETA_DATA_SINT ||
             semantic->kind == CMETA_DATA_UINT ||
             semantic->kind == CMETA_DATA_FLOAT ||
             (semantic->kind == CMETA_DATA_BYTES &&
              tbe_compiler_has_child(field, "is_fixed_size")) ||
             cmeta_uuid_cmeta_data_valid(semantic->data)) {
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

/* XML record projection preserves child elements and required sequences as
 * repeated field elements. Optional/nested sequences, variants and explicit
 * NULL have no lossless representation in this profile. Inspect the whole graph
 * before publishing generated entry points, not just the root fields. */
static int tbe_compiler_xml_record_supported(
    Node *root, Node *record, size_t depth, int *output_supported) {
  Node *fields = tbe_compiler_find_child(record, "fields");
  size_t i;
  if (depth > TBE_COMPILER_CMETA_MAX_DEPTH ||
      fields == NULL || fields->type != NODE_LIST)
    return 0;
  for (i = 0u; i < fields->data.list.count; ++i) {
    Node *field = fields->data.list.items[i];
    const char *type = tbe_compiler_string_value(field, "type");
    Node *child;
    if (tbe_compiler_has_child(field, "is_collection")) {
      type = tbe_compiler_string_value(field, "inner_type");
      if (tbe_compiler_has_child(field, "is_optional") ||
          tbe_compiler_has_child(field, "is_nullable") ||
          tbe_compiler_has_child(field, "is_map") || type == NULL ||
          strchr(type, '<') != NULL || strcmp(type, "bytes") == 0 ||
          !tbe_compiler_has_child(record, "cmeta_lifecycle_supported"))
        return 0;
    }
    child = type != NULL ? tbe_compiler_find_any_record(root, type) : NULL;
    if (tbe_compiler_has_child(field, "is_group_field") ||
        (type != NULL && tbe_compiler_find_record(root, "unions", type) != NULL))
      return 0;
    /* Keep the existing local-overlay lifecycle route for records whose
     * complete native value graph is not published. */
    if (child != NULL && !tbe_compiler_has_child(record, "cmeta_lifecycle_supported"))
      return 0;
    if (child != NULL &&
        !tbe_compiler_xml_record_supported(root, child, depth + 1u, output_supported))
      return 0;
    if (tbe_compiler_has_child(field, "is_nullable") ||
        tbe_compiler_has_child(field, "is_bytes"))
      *output_supported = 0;
  }
  return 1;
}

static void tbe_compiler_annotate_xml_record_messages(Node *root) {
  Node *messages;
  size_t i;

  if (root == NULL) return;
  messages = tbe_compiler_find_child(root, "messages");
  if (messages == NULL || messages->type != NODE_LIST) return;

  for (i = 0u; i < messages->data.list.count; ++i) {
    Node *record = messages->data.list.items[i];
    Node *fields = tbe_compiler_find_child(record, "fields");
    int supported;
    int output_supported = 0;

    tbe_compiler_remove_children(record, "cmeta_native_xml_record_supported");
    tbe_compiler_remove_children(record, "cmeta_native_xml_output_supported");

    /*
     * XML record publication composes two authorities:
     *   - CMeta owns the reflected physical fields;
     *   - DataBind MessagePlan owns presence/null overlay state.
     *
     * Scalar-only records retain their existing optional/nullable admission:
     * overlay bytes are intentionally not CMeta fields. The generated helper
     * preflights physical move support and copies the exact native state.
     * Nested records additionally require the complete lifecycle graph above.
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

    supported = tbe_compiler_xml_record_supported(
        root, record, 1u, &output_supported);

    if (supported)
      (void)tbe_compiler_set_string(
          record, "cmeta_native_xml_record_supported", "1");
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
        strcmp(native_data, "cmeta_uuid_cmeta_data") == 0 &&
        tbe_compiler_string_value(field, "native_type_symbol") != NULL &&
        strcmp(tbe_compiler_string_value(field, "native_type_symbol"),
               "cmeta_uuid_cmeta_type") == 0;
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
          strcmp(native_data, "cmeta_tstr_cmeta_data") == 0) ||
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
  tbe_compiler_annotate_xml_record_messages(root);
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

#define TBE_COMPILER_LANGUAGES(M) \
  Schema(M, \
    (TBE_COMPILER_LANG_C, "c", "templates/c/c_structs.mustache"), \
    (TBE_COMPILER_LANG_PYTHON, "python", "templates/python/python_dataclass.mustache"), \
    (TBE_COMPILER_LANG_RUST, "rust", "templates/rust/rust_structs.mustache"), \
    (TBE_COMPILER_LANG_CPP, "cpp", "templates/cpp/cpp_types.mustache"), \
    (TBE_COMPILER_LANG_GO, "go", "templates/go/go_types.mustache"), \
    (TBE_COMPILER_LANG_TS, "ts", "templates/typescript/ts_types.mustache"), \
    (TBE_COMPILER_LANG_SQLITE, "sqlite", "templates/sql/sqlite_schema.mustache"), \
    (TBE_COMPILER_LANG_POSTGRESQL, "postgresql", "templates/sql/postgresql_schema.mustache"))

int tbe_compiler_parse_language_name(const char *name, int64_t *out_lang_enum) {
  static const struct {
    const char *name;
    int64_t lang_enum;
  } languages[] = {
#define TBE_LANGUAGE_NAME(id_, name_, template_) {name_, id_},
      Replay(TBE_COMPILER_LANGUAGES, TBE_LANGUAGE_NAME)
#undef TBE_LANGUAGE_NAME
      {"cxx", TBE_COMPILER_LANG_CPP},
      {"py", TBE_COMPILER_LANG_PYTHON},
      {"typescript", TBE_COMPILER_LANG_TS},
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
#define TBE_LANGUAGE_CASE(id_, name_, template_) case id_: return name_;
    Replay(TBE_COMPILER_LANGUAGES, TBE_LANGUAGE_CASE)
#undef TBE_LANGUAGE_CASE
    default:
      return NULL;
  }
}

const char *tbe_compiler_resolve_template(const char *user_template,
                                          int64_t lang_enum) {
  if (user_template) return user_template;

  switch (lang_enum) {
#define TBE_LANGUAGE_TEMPLATE(id_, name_, template_) case id_: return template_;
    Replay(TBE_COMPILER_LANGUAGES, TBE_LANGUAGE_TEMPLATE)
#undef TBE_LANGUAGE_TEMPLATE
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
  if (cmeta_fs_path_join(path, path_size, resource_dir, relative_path) != 0) {
    fprintf(stderr, "Built-in template path is too long: %s\n", relative_path);
    return NULL;
  }
  return path;
}

#undef TBE_COMPILER_LANGUAGES

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

/* Binary is selected format admission, never part of the default typed C
 * Contract. An explicitly requested C codec/transport keeps its distinct
 * wire representation and strict layout constraints. */
typedef enum databind_compiler_format_admission {
  DATABIND_COMPILER_FORMAT_CONTRACT_ONLY = 0,
  DATABIND_COMPILER_FORMAT_BINARY = 1
} databind_compiler_format_admission;

static int databind_compiler_parse_contract_file_mode(
    const char *schema_path, Node **out_legacy_tree,
    IdlContract **out_contract, char **out_schema_data,
    databind_compiler_format_admission admission) {
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

  if (admission != DATABIND_COMPILER_FORMAT_CONTRACT_ONLY &&
      admission != DATABIND_COMPILER_FORMAT_BINARY) {
    fprintf(stderr, "Invalid compiler format admission\n");
    idl_contract_destroy(contract);
    free(schema_data);
    node_free(root);
    return 1;
  }

  if (admission == DATABIND_COMPILER_FORMAT_BINARY) {
    if (databind_binary_contract_apply(root, &parse_err) != 0) {
      fprintf(stderr, "Binary format error: %s\n", parse_err.message);
      idl_contract_destroy(contract);
      free(schema_data);
      node_free(root);
      return 1;
    }
    /* Ordinary C/legacy language output still consumes the Binary-derived
     * presentation tree. SQL uses only the typed Contract -> database IR. */
    tbe_compiler_annotate_language_types(contract, root);
  }

  *out_legacy_tree = root;
  *out_contract = contract;
  *out_schema_data = schema_data;
  return 0;
}

int databind_compiler_parse_contract_only_file(
    const char *schema_path, Node **out_legacy_tree,
    IdlContract **out_contract, char **out_schema_data) {
  return databind_compiler_parse_contract_file_mode(
      schema_path, out_legacy_tree, out_contract, out_schema_data,
      DATABIND_COMPILER_FORMAT_CONTRACT_ONLY);
}

int databind_compiler_parse_contract_file(
    const char *schema_path, Node **out_legacy_tree,
    IdlContract **out_contract, char **out_schema_data) {
  return databind_compiler_parse_contract_file_mode(
      schema_path, out_legacy_tree, out_contract, out_schema_data,
      DATABIND_COMPILER_FORMAT_BINARY);
}

int tbe_compiler_parse_schema_file(
    const char *schema_path, Node **out_root, char **out_schema_data) {
  IdlContract *contract = NULL;
  int status = databind_compiler_parse_contract_file(
      schema_path, out_root, &contract, out_schema_data);
  idl_contract_destroy(contract);
  return status;
}

/* Scope owns memory only. The body closes/publishes/discards output with
 * explicit error checks before this nofail lifecycle releases storage. */
typedef struct tbe_compiler_render_t {
  char *templ_data;
  MUSTACHE_TEMPLATE *templ;
  char *temporary_output_path;
} tbe_compiler_render_t;

static cmeta_status tbe_compiler_render_init(tbe_compiler_render_t *render) {
  *render = (tbe_compiler_render_t){0};
  return CMETA_OK;
}

static void tbe_compiler_render_restore(tbe_compiler_render_t *render) {
  free(render->temporary_output_path);
  mustache_release(render->templ);
  free(render->templ_data);
  *render = (tbe_compiler_render_t){0};
}

static void tbe_compiler_render_move(tbe_compiler_render_t *out,
    tbe_compiler_render_t *render) {
  *out = *render;
  *render = (tbe_compiler_render_t){0};
}

static const cmeta_type_identity TBE_COMPILER_RENDER_ID =
    CMETA_TYPE_ID_ATOM_INIT("databind.compiler.render");
static const cmeta_type_desc TBE_COMPILER_RENDER_TYPE = {
    .name = "tbe_compiler_render_t", .size = sizeof(tbe_compiler_render_t),
    .align = _Alignof(tbe_compiler_render_t), .kind = CMETA_T_OBJECT,
    .identity = &TBE_COMPILER_RENDER_ID};
CMETA_DEFINE_LIFECYCLE(tbe_compiler_render_t, &TBE_COMPILER_RENDER_TYPE,
    tbe_compiler_render_init, tbe_compiler_render_restore, tbe_compiler_render_move,
    CMETA_LIFECYCLE_INIT_NOFAIL | CMETA_LIFECYCLE_TRIVIAL_ZERO | CMETA_LIFECYCLE_MOVABLE);

static int tbe_compiler_render_owned(tbe_compiler_render_t *render,
    Node *root, const char *template_path, const char *output_path) {
  MUSTACHE_DATAPROVIDER provider = mustache_helpers_provider();
  MUSTACHE_RENDERER renderer = mustache_helpers_renderer();
  render->templ_data = tbe_compiler_read_file(template_path);
  FILE *out_file = stdout;
#ifndef _WIN32
  mode_t existing_output_mode = 0;
  int preserve_existing_output_mode = 0;
#endif
  int res = 1;

  if (!render->templ_data) {
    fprintf(stderr, "Failed to read template file: %s\n", template_path);
    return 1;
  }

  render->templ = mustache_compile(render->templ_data, strlen(render->templ_data), NULL, NULL, 0);
  if (!render->templ) {
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
    if (tbe_compiler_create_temporary_output(output_path, &render->temporary_output_path, &out_file) != 0) {
      fprintf(stderr, "Failed to create unique temporary output file for: %s\n", output_path);
      goto cleanup;
    }
  }

  if (mustache_process(render->templ, &renderer, out_file, &provider, root)
      != MUSTACHE_ERR_SUCCESS) {
    fprintf(stderr, "Failed to render mustache template: %s\n", template_path);
    goto cleanup;
  }

  if (out_file != stdout) {
    int flush_status = fflush(out_file);
    int close_status = fclose(out_file);
    out_file = NULL;
    if (flush_status != 0 || close_status != 0) {
      fprintf(stderr, "Failed to finalize temporary output file: %s\n", render->temporary_output_path);
      goto cleanup;
    }
#ifndef _WIN32
    if (preserve_existing_output_mode &&
        chmod(render->temporary_output_path, existing_output_mode) != 0) {
      fprintf(stderr, "Failed to preserve output file permissions: %s\n", output_path);
      goto cleanup;
    }
#endif
    if (cmeta_fs_rename(render->temporary_output_path, output_path) != 0) {
      fprintf(stderr, "Failed to replace output file: %s\n", output_path);
      goto cleanup;
    }
    free(render->temporary_output_path);
    render->temporary_output_path = NULL;
  }
  res = 0;

cleanup:
  /* Stream close and rollback are fallible effects, never nofail destructors.
   * A committed output has already consumed both stream and temporary path. */
  if (out_file != NULL && out_file != stdout && fclose(out_file) != 0) {
    fprintf(stderr, "Failed to close abandoned temporary output: %s\n", output_path);
    res = 1;
  }
  if (render->temporary_output_path != NULL && cmeta_fs_unlink(render->temporary_output_path) != 0) {
    fprintf(stderr, "Failed to remove temporary output file: %s\n", render->temporary_output_path);
    res = 1;
  }
  return res;
}

/* Reject target domains before touching any of the requested output paths. */
int tbe_compiler_render_file(Node *root, const char *template_path,
    const char *output_path) {
  int status;
  cmeta_scope(status, cmeta_autos((tbe_compiler_render_t, render)),
      cmeta_body(tbe_compiler_render_owned(&render, root, template_path, output_path)));
  return status;
}

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

/* Schema/Replay accepts at most sixteen rows per expansion in Salts 2.1. */
enum { TBE_COMPILER_CMETA_SCHEMA_ROWS = 16u };

static int tbe_compiler_prepare_cmeta_record(Node *record) {
  Node *fields = tbe_compiler_find_child(record, "fields");
  size_t i;
  if (fields == NULL || fields->type != NODE_LIST) return -1;
  for (i = 0u; i < fields->data.list.count; ++i) {
    Node *field = fields->data.list.items[i];
    char index[3u * sizeof(size_t) + 1u];
    int written = snprintf(index, sizeof(index), "%zu", i);
    if (written < 0 || (size_t)written >= sizeof(index) ||
        tbe_compiler_set_string(field, "cmeta_field_index", index) != 0)
      return -1;
    if (i % TBE_COMPILER_CMETA_SCHEMA_ROWS == 0u &&
        tbe_compiler_set_string(field, "cmeta_schema_first", "1") != 0)
      return -1;
    if ((i % TBE_COMPILER_CMETA_SCHEMA_ROWS == TBE_COMPILER_CMETA_SCHEMA_ROWS - 1u ||
         i == fields->data.list.count - 1u) &&
        tbe_compiler_set_string(field, "cmeta_schema_last", "1") != 0)
      return -1;
  }
  return 0;
}

static int tbe_compiler_prepare_cmeta_records(Node *root) {
  static const char *const sections[] = {"composites", "groups", "messages"};
  Node *records = create_node_list("cmeta_records");
  size_t section;
  if (records == NULL) return -1;
  /* This render-only snapshot is built after lowering, then owned by root.
   * It never feeds changes back into the canonical record lists. */
  for (section = 0u; section < sizeof(sections) / sizeof(sections[0]); ++section) {
    Node *list = tbe_compiler_find_child(root, sections[section]);
    size_t i;
    if (list == NULL) continue;
    if (list->type != NODE_LIST) goto fail;
    for (i = 0u; i < list->data.list.count; ++i) {
      Node *record = tbe_compiler_clone_canonical_node(list->data.list.items[i]);
      if (record == NULL || tbe_compiler_prepare_cmeta_record(record) != 0 ||
          list_add(records, record) != 0) {
        node_free(record);
        goto fail;
      }
    }
  }
  tbe_compiler_remove_children(root, "cmeta_records");
  if (map_add(root, records) != 0) goto fail;
  return 0;
fail:
  node_free(records);
  return -1;
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

/* Source-language presentation is projected solely from validated IdlContract.
 * Node is the Mustache adapter, never a second semantic type authority. */
enum { TBE_SOURCE_TYPE_CAPACITY = 4u * IDL_TYPE_REF_MAX_BYTES + 1u };

static int tbe_source_append(char *buffer, size_t capacity, size_t *used,
                             const char *text, size_t length) {
  if (buffer == NULL || used == NULL || text == NULL ||
      *used >= capacity || length >= capacity - *used)
    return 0;
  memcpy(buffer + *used, text, length);
  *used += length;
  buffer[*used] = '\0';
  return 1;
}

static int tbe_source_append_cstr(char *buffer, size_t capacity,
                                  size_t *used, const char *text) {
  return text != NULL &&
      tbe_source_append(buffer, capacity, used, text, strlen(text));
}

static int tbe_source_hashable_type(
    const IdlContract *contract, const char *expression, size_t length);

static int tbe_source_type_expression(
    const IdlContract *contract, const char *expression, size_t length,
    int64_t language, char *buffer, size_t capacity, size_t *used) {
  const int python = language == TBE_COMPILER_LANG_PYTHON;
  const int go = language == TBE_COMPILER_LANG_GO;
  const int rust = language == TBE_COMPILER_LANG_RUST;
  const int cpp = language == TBE_COMPILER_LANG_CPP;
  IdlTypeRef type;
  char name[IDL_TYPE_REF_MAX_BYTES + 1u];
  const char *mapped = NULL;
  const tbe_compiler_scalar_projection_t *scalar;
  const IdlDataDecl *decl;
  size_t i;

  if (contract == NULL || expression == NULL ||
      !idl_type_ref_parse(expression, length, &type))
    return 0;
  if (type.collection_kind != IDL_COLLECTION_NONE) {
    if ((go || rust || cpp) &&
        (type.collection_kind == IDL_COLLECTION_SET ||
         type.collection_kind == IDL_COLLECTION_MAP) &&
        !tbe_source_hashable_type(
            contract, type.arguments[0], type.argument_lengths[0]))
      return 0;
    const char *prefix = type.collection_kind == IDL_COLLECTION_LIST
        ? (go ? "[]" : cpp ? "std::vector<" : rust ? "Vec<" :
           python ? "list[" : "Array<")
        : type.collection_kind == IDL_COLLECTION_SET
        ? (go ? "map[" : cpp ? "std::set<" :
           rust ? "std::collections::HashSet<" :
           python ? "set[" : "Set<")
        : type.collection_kind == IDL_COLLECTION_MAP
        ? (go ? "map[" : cpp ? "std::map<" :
           rust ? "std::collections::HashMap<" :
           python ? "dict[" : "Map<") : NULL;
    if (!tbe_source_append_cstr(buffer, capacity, used, prefix))
      return 0;
    for (i = 0u; i < type.argument_count; ++i) {
      if (i != 0u && !tbe_source_append_cstr(
                           buffer, capacity, used, go ? "]" : ", "))
        return 0;
      if (!tbe_source_type_expression(
              contract, type.arguments[i], type.argument_lengths[i],
              language, buffer, capacity, used))
        return 0;
    }
    return tbe_source_append_cstr(buffer, capacity, used,
        go ? (type.collection_kind == IDL_COLLECTION_SET ? "]struct{}" : "")
           : python ? "]" : ">");
  }

  if (type.name_length == 0u || type.name_length >= sizeof(name))
    return 0;
  memcpy(name, type.name, type.name_length);
  name[type.name_length] = '\0';

  scalar = tbe_compiler_scalar_projection(name);
  if (scalar != NULL) {
    if (!python && !go && !rust && !cpp &&
        (scalar->data->kind == CMETA_DATA_SINT ||
         scalar->data->kind == CMETA_DATA_UINT) &&
        ((const cmeta_data_integer_shape *)scalar->data->shape)->bits == 64u)
      mapped = "bigint";
    else
      mapped = cpp ? scalar->cpp_type :
               rust ? scalar->rust_type :
               go ? scalar->go_type :
               python ? scalar->python_type : scalar->ts_type;
  } else if (strcmp(name, "varint") == 0 ||
             strcmp(name, "bigint") == 0) {
    /* C++/Go/Rust require explicit width and an owned BigInt contract. */
    if (go || rust || cpp) return 0;
    mapped = python ? "int" : "bigint";
  } else if (strcmp(name, "string") == 0 ||
             strcmp(name, "uuid") == 0) {
    mapped = cpp ? (strcmp(name, "uuid") == 0
                        ? "std::array<std::uint8_t, 16>" : "std::string")
           : rust ? (strcmp(name, "uuid") == 0 ? "[u8; 16]" : "String")
           : go ? (strcmp(name, "uuid") == 0 ? "[16]byte" : "string")
           : python ? "str" : "string";
  } else if (strcmp(name, "bytes") == 0) {
    mapped = cpp ? "std::vector<std::uint8_t>"
           : rust ? "Vec<u8>"
           : go ? "[]byte" : python ? "bytes" : "Uint8Array";
  } else {
    decl = idl_contract_find_data(contract, name);
    if (decl == NULL || decl->kind == IDL_DATA_UNION)
      return 0; /* Never guess a domain or union representation. */
    mapped = name;
  }
  return tbe_source_append_cstr(buffer, capacity, used, mapped);
}

/* Go/Rust hash keys and C++ ordered keys need stable value comparison.
 * Admit scalar integer/bool/string/UUID and enum domains; reject float,
 * composite records and mutable nested collections as keys. */
static int tbe_source_hashable_type(const IdlContract *contract,
                                  const char *expression, size_t length) {
  IdlTypeRef ref;
  char name[IDL_TYPE_REF_MAX_BYTES + 1u];
  const tbe_compiler_scalar_projection_t *scalar;
  const IdlDataDecl *decl;
  if (expression == NULL ||
      !idl_type_ref_parse(expression, length, &ref) ||
      ref.collection_kind != IDL_COLLECTION_NONE ||
      ref.name_length >= sizeof(name))
    return 0;
  memcpy(name, ref.name, ref.name_length);
  name[ref.name_length] = '\0';
  if (strcmp(name, "string") == 0 || strcmp(name, "uuid") == 0)
    return 1;
  scalar = tbe_compiler_scalar_projection(name);
  if (scalar != NULL)
    return scalar->data->kind == CMETA_DATA_BOOL ||
           scalar->data->kind == CMETA_DATA_SINT ||
           scalar->data->kind == CMETA_DATA_UINT;
  decl = idl_contract_find_data(contract, name);
  return decl != NULL && decl->kind == IDL_DATA_ENUM;
}

static int tbe_source_field_type(
    const IdlContract *contract, const IdlField *field,
    int64_t language, char output[TBE_SOURCE_TYPE_CAPACITY]) {
  const int python = language == TBE_COMPILER_LANG_PYTHON;
  const int go = language == TBE_COMPILER_LANG_GO;
  const int rust = language == TBE_COMPILER_LANG_RUST;
  const int cpp = language == TBE_COMPILER_LANG_CPP;
  size_t used = 0u;
  const char *inner;
  const char *prefix = NULL;
  if (contract == NULL || field == NULL)
    return 0;
  /* Omission and defaults need a dedicated presence/default representation.
   * Do not silently translate missing vs explicit null into Option<T>. */
  if ((python || go || rust || cpp) &&
      (field->optional || field->default_value != NULL))
    return 0;
  output[0] = '\0';
  if (go && field->nullable &&
      !tbe_source_append_cstr(output, TBE_SOURCE_TYPE_CAPACITY, &used, "*"))
    return 0;
  if (rust && field->nullable &&
      !tbe_source_append_cstr(output, TBE_SOURCE_TYPE_CAPACITY, &used, "Option<"))
    return 0;
  if (cpp && field->nullable &&
      !tbe_source_append_cstr(output, TBE_SOURCE_TYPE_CAPACITY, &used, "std::optional<"))
    return 0;
  if ((go || rust || cpp) &&
      field->collection_kind == IDL_COLLECTION_NONE &&
      field->length != NULL && field->length[0] != '\0') {
    size_t fixed_length;
    char fixed_text[48];
    int printed;
    if (field->type_name == NULL ||
        strcmp(field->type_name, "bytes") != 0 ||
        !tbe_compiler_parse_size(field->length, &fixed_length) ||
        fixed_length == 0u || fixed_length > 2147483647u)
      return 0;
    printed = snprintf(fixed_text, sizeof(fixed_text),
                       cpp ? "std::array<std::uint8_t, %zu>" :
                       rust ? "[u8; %zu]" : "[%zu]byte", fixed_length);
    return printed > 0 && (size_t)printed < sizeof(fixed_text) &&
           tbe_source_append_cstr(output, TBE_SOURCE_TYPE_CAPACITY,
                                  &used, fixed_text) &&
           (!(rust || cpp) || !field->nullable ||
            tbe_source_append_cstr(output, TBE_SOURCE_TYPE_CAPACITY,
                                   &used, ">"));
  }
  switch (field->collection_kind) {
  case IDL_COLLECTION_NONE:
    if (field->type_name == NULL ||
        !tbe_source_type_expression(
            contract, field->type_name, strlen(field->type_name),
            language, output, TBE_SOURCE_TYPE_CAPACITY, &used))
      return 0;
    break;
  case IDL_COLLECTION_ARRAY:
  case IDL_COLLECTION_LIST:
  case IDL_COLLECTION_GROUP:
  case IDL_COLLECTION_SET:
    prefix = field->collection_kind == IDL_COLLECTION_SET
        ? (go ? "map[" : cpp ? "std::set<" :
           rust ? "std::collections::HashSet<" :
           python ? "set[" : "Set<")
        : (go ? "[]" : cpp ? "std::vector<" :
           rust ? "Vec<" : python ? "list[" : "Array<");
    inner = field->inner_type;
    if ((go || rust || cpp) && field->collection_kind == IDL_COLLECTION_SET &&
        !tbe_source_hashable_type(
            contract, inner, inner != NULL ? strlen(inner) : 0u))
      return 0;
    if ((go || rust || cpp) && field->collection_kind == IDL_COLLECTION_ARRAY) {
      size_t fixed_length;
      char fixed_text[32];
      int printed;
      if (!tbe_compiler_parse_size(field->length, &fixed_length) ||
          fixed_length == 0u || fixed_length > 2147483647u)
        return 0;
      /* Normalize literal decimal; a spelling like 008 is invalid in Go.
       * Symbolic lengths cannot be represented as fixed Go type lengths. */
      printed = snprintf(fixed_text, sizeof(fixed_text), "%zu", fixed_length);
      if (printed < 0 || (size_t)printed >= sizeof(fixed_text))
        return 0;
      prefix = rust ? "[" : cpp ? "std::array<" : NULL;
      if (go &&
          (!tbe_source_append_cstr(output, TBE_SOURCE_TYPE_CAPACITY, &used, "[") ||
           !tbe_source_append_cstr(output, TBE_SOURCE_TYPE_CAPACITY, &used,
                                   fixed_text) ||
           !tbe_source_append_cstr(output, TBE_SOURCE_TYPE_CAPACITY, &used, "]")))
        return 0;
    }
    if (inner == NULL ||
        ((!go || field->collection_kind != IDL_COLLECTION_ARRAY) &&
         !tbe_source_append_cstr(
             output, TBE_SOURCE_TYPE_CAPACITY, &used, prefix)))
      return 0;
    if (!tbe_source_type_expression(
            contract, inner, strlen(inner),
            language, output, TBE_SOURCE_TYPE_CAPACITY, &used))
      return 0;
    if ((rust || cpp) && field->collection_kind == IDL_COLLECTION_ARRAY) {
      char count_text[40];
      size_t fixed_length;
      int printed;
      if (!tbe_compiler_parse_size(field->length, &fixed_length) ||
          fixed_length == 0u || fixed_length > 2147483647u)
        return 0;
      printed = snprintf(count_text, sizeof(count_text),
                         cpp ? ", %zu>" : "; %zu]", fixed_length);
      if (printed < 0 || (size_t)printed >= sizeof(count_text) ||
          !tbe_source_append_cstr(output, TBE_SOURCE_TYPE_CAPACITY, &used,
                                   count_text))
        return 0;
    } else if (!tbe_source_append_cstr(
        output, TBE_SOURCE_TYPE_CAPACITY, &used,
        go ? (field->collection_kind == IDL_COLLECTION_SET ? "]struct{}" : "")
           : python ? "]" : ">"))
      return 0;
    break;
  case IDL_COLLECTION_MAP:
    if (field->key_type == NULL || field->value_type == NULL ||
        ((go || rust || cpp) && !tbe_source_hashable_type(
            contract, field->key_type, strlen(field->key_type))) ||
        !tbe_source_append_cstr(output, TBE_SOURCE_TYPE_CAPACITY,
                                &used, go ? "map[" :
                                cpp ? "std::map<" :
                                rust ? "std::collections::HashMap<" :
                                python ? "dict[" : "Map<") ||
        !tbe_source_type_expression(
            contract, field->key_type, strlen(field->key_type),
            language, output, TBE_SOURCE_TYPE_CAPACITY, &used) ||
        !tbe_source_append_cstr(output, TBE_SOURCE_TYPE_CAPACITY,
                                &used, go ? "]" : ", ") ||
        !tbe_source_type_expression(
            contract, field->value_type, strlen(field->value_type),
            language, output, TBE_SOURCE_TYPE_CAPACITY, &used) ||
        !tbe_source_append_cstr(
            output, TBE_SOURCE_TYPE_CAPACITY, &used,
            go ? "" : python ? "]" : ">"))
      return 0;
    break;
  default:
    return 0;
  }
  return !field->nullable || go ||
      tbe_source_append_cstr(output, TBE_SOURCE_TYPE_CAPACITY, &used,
                             (rust || cpp) ? ">" : python ? " | None" : " | null");
}

static int tbe_go_export_name(const IdlField *field, Node *fields,
                               char output[256]) {
  size_t i;
  if (field == NULL || field->name == NULL || fields == NULL ||
      fields->type != NODE_LIST || strlen(field->name) >= 256u)
    return 0;
  tbe_compiler_pascal_identifier(field->name, output, 256u);
  if (output[0] == '\0')
    return 0;
  for (i = 0u; i < fields->data.list.count; ++i) {
    const char *previous = tbe_compiler_string_value(
        fields->data.list.items[i], "go_name");
    if (previous == NULL || strcmp(previous, output) == 0)
      return 0;
  }
  return 1;
}

/* Rust source names are emitted verbatim; never emit a keyword or
 * malformed identifier and hope the consumer compiler repairs it. */
static int tbe_rust_identifier_valid(const char *name) {
  static const char *const reserved[] = {
      "as", "async", "await", "break", "const", "continue", "crate",
      "dyn", "else", "enum", "extern", "false", "fn", "for", "if",
      "impl", "in", "let", "loop", "match", "mod", "move", "mut",
      "pub", "ref", "return", "self", "Self", "static", "struct",
      "super", "trait", "true", "type", "unsafe", "use", "where",
      "while", "abstract", "become", "box", "do", "final", "macro",
      "override", "priv", "try", "typeof", "unsized", "virtual",
      "yield", "_"
  };
  size_t i;
  if (name == NULL || name[0] == '\0' ||
      !((name[0] >= 'A' && name[0] <= 'Z') ||
        (name[0] >= 'a' && name[0] <= 'z') || name[0] == '_'))
    return 0;
  for (i = 1u; name[i] != '\0'; ++i)
    if (!((name[i] >= 'A' && name[i] <= 'Z') ||
          (name[i] >= 'a' && name[i] <= 'z') ||
          (name[i] >= '0' && name[i] <= '9') || name[i] == '_'))
      return 0;
  for (i = 0u; i < sizeof(reserved) / sizeof(reserved[0]); ++i)
    if (strcmp(name, reserved[i]) == 0)
      return 0;
  return 1;
}

/* A by-value Rust record cycle has no finite size. Vec/HashMap/HashSet
 * break recursive sizing; Option<T> alone does not. */
static int tbe_rust_record_visit(const IdlContract *contract, size_t index,
                                 unsigned char *states) {
  const IdlDataDecl *decl = &contract->data[index];
  size_t i;
  states[index] = 1u;
  for (i = 0u; i < decl->field_count; ++i) {
    const IdlField *field = &decl->fields[i];
    const IdlDataDecl *target;
    const char *name;
    size_t next;
    if (field->collection_kind != IDL_COLLECTION_NONE &&
        field->collection_kind != IDL_COLLECTION_ARRAY)
      continue;
    name = field->collection_kind == IDL_COLLECTION_ARRAY
        ? field->inner_type : field->type_name;
    if (name == NULL)
      continue;
    target = idl_contract_find_data(contract, name);
    if (target == NULL || target->kind == IDL_DATA_ENUM)
      continue;
    next = (size_t)(target - contract->data);
    if (next >= contract->data_count || states[next] == 1u ||
        (states[next] == 0u &&
         !tbe_rust_record_visit(contract, next, states))) {
      fprintf(stderr, "Rust by-value record cycle through %s.%s\n",
              decl->name, field->name);
      return 0;
    }
  }
  states[index] = 2u;
  return 1;
}

static int tbe_rust_validate_sized_records(const IdlContract *contract) {
  unsigned char *states;
  size_t i;
  if (contract == NULL)
    return 0;
  if (contract->data_count == 0u)
    return 1;
  states = (unsigned char *)calloc(contract->data_count, 1u);
  if (states == NULL)
    return 0;
  for (i = 0u; i < contract->data_count; ++i) {
    if (contract->data[i].kind != IDL_DATA_ENUM &&
        states[i] == 0u &&
        !tbe_rust_record_visit(contract, i, states))
      break;
  }
  free(states);
  return i == contract->data_count;
}

/* IdlContract validates integer values, but a leading-zero decimal
 * spelling like 008 is invalid as a Rust source integer literal. */
static int tbe_rust_enum_literal(const char *value,
                                 char *out, size_t capacity) {
  const char *digits;
  const char *p;
  size_t used = 0u;
  int negative;
  if (value == NULL || out == NULL || capacity == 0u)
    return 0;
  negative = value[0] == '-';
  digits = value + negative;
  if (digits[0] == '\0')
    return 0;
  for (p = digits; *p != '\0'; ++p)
    if (*p < '0' || *p > '9')
      return tbe_source_append_cstr(out, capacity, &used, value);
  while (digits[0] == '0' && digits[1] != '\0')
    ++digits;
  if (negative && strcmp(digits, "0") != 0 &&
      !tbe_source_append_cstr(out, capacity, &used, "-"))
    return 0;
  return tbe_source_append_cstr(out, capacity, &used, digits);
}

/* C++ declarations cannot use language keywords or implementation-reserved
 * identifiers. This admission is source-only; the Contract's logical identity
 * remains unchanged. */
static int tbe_cpp_identifier_valid(const char *name) {
  static const char *const cpp_keywords[] = {
      "alignas", "alignof", "and", "and_eq", "asm", "bitand", "bitor",
      "bool", "catch", "char8_t", "char16_t", "char32_t", "class",
      "compl", "concept", "const_cast", "constexpr", "consteval",
      "constinit", "decltype", "delete", "dynamic_cast", "explicit",
      "export", "false", "friend", "mutable", "namespace", "new",
      "noexcept", "not", "not_eq", "nullptr", "operator", "or",
      "or_eq", "private", "protected", "public", "reinterpret_cast",
      "requires", "static_assert", "static_cast", "template", "this",
      "thread_local", "throw", "true", "try", "typeid", "typename",
      "using", "virtual", "wchar_t", "xor", "xor_eq", "std"
  };
  size_t i;
  /* Types are emitted in the global namespace, where every leading
   * underscore identifier is reserved; std is already a namespace. */
  if (!tbe_compiler_c_identifier_valid(name) || name[0] == '_')
    return 0;
  for (i = 0u; i < sizeof(cpp_keywords) / sizeof(cpp_keywords[0]); ++i)
    if (strcmp(name, cpp_keywords[i]) == 0)
      return 0;
  return 1;
}

/* C++11+ integer literal suffixes preserve the complete uint64 domain;
 * INT64_MIN must be spelled as a subtraction to avoid out-of-range positive
 * literal parsing. Enum values are already validated in IdlContract. */
static int tbe_cpp_enum_literal(
    const char *value, const char *storage,
    char *out, size_t capacity) {
  const tbe_compiler_scalar_projection_t *integer =
      tbe_compiler_integer_type(storage);
  char decimal[96];
  unsigned bits;
  int printed;
  size_t i;
  if (integer == NULL || value == NULL ||
      !tbe_rust_enum_literal(value, decimal, sizeof(decimal)))
    return 0;
  for (i = (decimal[0] == '-'); decimal[i] != '\0'; ++i)
    if (decimal[i] < '0' || decimal[i] > '9')
      return 0;
  if (i == (size_t)(decimal[0] == '-') ||
      (decimal[0] == '-' && integer->data->kind != CMETA_DATA_SINT))
    return 0;
  bits = ((const cmeta_data_integer_shape *)integer->data->shape)->bits;
  if (strcmp(decimal, "-9223372036854775808") == 0)
    printed = snprintf(out, capacity, "(-9223372036854775807LL - 1LL)");
  else
    printed = snprintf(out, capacity, "%s%s", decimal,
        bits == 64u ? (integer->data->kind == CMETA_DATA_UINT ?
            "ULL" : "LL") : "");
  return printed > 0 && (size_t)printed < capacity;
}

static int tbe_source_render_field(
    const IdlContract *contract, const IdlField *field,
    int64_t language, Node *fields) {
  const int python = language == TBE_COMPILER_LANG_PYTHON;
  const int go = language == TBE_COMPILER_LANG_GO;
  const int rust = language == TBE_COMPILER_LANG_RUST;
  const int cpp = language == TBE_COMPILER_LANG_CPP;
  Node *node = create_node_map(NULL);
  char mapped[TBE_SOURCE_TYPE_CAPACITY];
  char go_name[256];
  if (node == NULL)
    return 0;
  if (field->name == NULL ||
      (cpp && (!tbe_cpp_identifier_valid(field->name) ||
               idl_contract_find_data(contract, field->name) != NULL)) ||
      (rust && !tbe_rust_identifier_valid(field->name)) ||
      (go && !tbe_go_export_name(field, fields, go_name)) ||
      !tbe_source_field_type(contract, field, language, mapped) ||
      tbe_compiler_set_string(node, "name", field->name) != 0 ||
      tbe_compiler_set_string(
          node, cpp ? "cpp_type" : rust ? "rust_type" :
                go ? "go_type" : python ? "python_type" : "ts_type",
          mapped) != 0 ||
      (go && tbe_compiler_set_string(node, "go_name", go_name) != 0) ||
      (!python && !go && !rust && !cpp && field->optional &&
       tbe_compiler_set_string(node, "ts_optional", "1") != 0) ||
      list_add(fields, node) != 0) {
    node_free(node);
    return 0;
  }
  return 1;
}

static int tbe_source_render_decl(
    const IdlContract *contract, const IdlDataDecl *decl,
    int64_t language, Node *list) {
  Node *node = create_node_map(NULL);
  Node *members = NULL;
  size_t i;
  if (node == NULL)
    return 0;
  if (decl->name == NULL ||
      (language == TBE_COMPILER_LANG_RUST &&
       (!tbe_rust_identifier_valid(decl->name) || decl->flags)) ||
      (language == TBE_COMPILER_LANG_CPP &&
       (!tbe_cpp_identifier_valid(decl->name) || decl->flags)) ||
      tbe_compiler_set_string(node, "name", decl->name) != 0)
    goto failed;
  if (decl->kind == IDL_DATA_ENUM) {
    members = create_node_list("items");
    if (members == NULL ||
        tbe_compiler_set_string(node, "enum_name", decl->name) != 0)
      goto failed;
    if (decl->underlying_type != NULL &&
        tbe_compiler_set_string(node, "underlying_type",
                                decl->underlying_type) != 0)
      goto failed;
    if (language == TBE_COMPILER_LANG_PYTHON && decl->flags &&
        tbe_compiler_set_string(node, "python_flags", "1") != 0)
      goto failed;
    if (language == TBE_COMPILER_LANG_GO ||
        language == TBE_COMPILER_LANG_RUST ||
        language == TBE_COMPILER_LANG_CPP) {
      const char *storage = decl->underlying_type != NULL
          ? decl->underlying_type : (decl->flags ? "uint32" : "int32");
      const tbe_compiler_scalar_projection_t *integer =
          tbe_compiler_integer_type(storage);
      if (integer == NULL ||
          tbe_compiler_set_string(
              node, language == TBE_COMPILER_LANG_CPP
                  ? "cpp_underlying_type" :
                  language == TBE_COMPILER_LANG_RUST
                  ? "rust_underlying_type" : "go_underlying_type",
              language == TBE_COMPILER_LANG_CPP ? integer->cpp_type :
              language == TBE_COMPILER_LANG_RUST
                  ? integer->rust_type : integer->go_type) != 0)
        goto failed;
    }
    if (map_add(node, members) != 0)
      goto failed;
    members = tbe_compiler_find_child(node, "items");
    for (i = 0u; i < decl->enum_item_count; ++i) {
      Node *entry = create_node_map(NULL);
      if (entry == NULL)
        goto failed;
      char literal[256];
      const int rust = language == TBE_COMPILER_LANG_RUST;
      const int cpp = language == TBE_COMPILER_LANG_CPP;
      if ((rust &&
           (!tbe_rust_identifier_valid(decl->enum_items[i].name) ||
            !tbe_rust_enum_literal(decl->enum_items[i].value,
                                   literal, sizeof(literal)))) ||
          (cpp &&
           (!tbe_cpp_identifier_valid(decl->enum_items[i].name) ||
            !tbe_cpp_enum_literal(decl->enum_items[i].value,
                decl->underlying_type, literal, sizeof(literal)))) ||
          tbe_compiler_set_string(entry, "name",
                                  decl->enum_items[i].name) != 0 ||
          tbe_compiler_set_string(entry, "value",
                                  (rust || cpp) ? literal :
                                  decl->enum_items[i].value) != 0 ||
          (cpp && tbe_compiler_set_string(entry, "c_literal", literal) != 0) ||
          list_add(members, entry) != 0) {
        node_free(entry);
        goto failed;
      }
    }
  } else {
    members = create_node_list("fields");
    if (members == NULL || map_add(node, members) != 0)
      goto failed;
    members = tbe_compiler_find_child(node, "fields");
    for (i = 0u; i < decl->field_count; ++i) {
      if (!tbe_source_render_field(
              contract, &decl->fields[i], language, members)) {
        fprintf(stderr,
                "%s cannot represent IDL field %s.%s; no artifact published\n",
                language == TBE_COMPILER_LANG_CPP ? "C++" :
                language == TBE_COMPILER_LANG_RUST ? "Rust" :
                language == TBE_COMPILER_LANG_GO ? "Go" :
                language == TBE_COMPILER_LANG_PYTHON ? "Python dataclass"
                                                     : "TypeScript",
                decl->name, decl->fields[i].name);
        goto failed;
      }
    }
  }
  if (list_add(list, node) != 0)
    goto failed;
  return 1;
failed:
  if (members != NULL && tbe_compiler_find_child(node, members->name) != members)
    node_free(members);
  node_free(node);
  return 0;
}

/* C++17 permits vector<T> for an incomplete T, but direct/array and
 * non-vector associative value types must be fully declared before use.
 * Source ordering is computed from immutable Contract references, not from
 * Binary layout or native CMeta lifecycle flags. */
static int tbe_cpp_type_depends_on(
    const char *text, const char *target) {
  IdlTypeRef ref;
  size_t i;
  if (text == NULL || target == NULL ||
      !idl_type_ref_parse(text, strlen(text), &ref))
    return -1;
  if (ref.collection_kind == IDL_COLLECTION_LIST)
    return 0;
  if (ref.collection_kind == IDL_COLLECTION_NONE)
    return ref.name_length == strlen(target) &&
           memcmp(ref.name, target, ref.name_length) == 0;
  for (i = 0u; i < ref.argument_count; ++i) {
    char nested[IDL_TYPE_REF_MAX_BYTES + 1u];
    int depends;
    if (ref.argument_lengths[i] >= sizeof(nested))
      return -1;
    memcpy(nested, ref.arguments[i], ref.argument_lengths[i]);
    nested[ref.argument_lengths[i]] = '\0';
    depends = tbe_cpp_type_depends_on(nested, target);
    if (depends != 0)
      return depends;
  }
  return 0;
}

static int tbe_cpp_field_depends_on(
    const IdlField *field, const char *target) {
  int depends;
  if (field == NULL || target == NULL)
    return -1;
  switch (field->collection_kind) {
  case IDL_COLLECTION_NONE:
    return tbe_cpp_type_depends_on(field->type_name, target);
  case IDL_COLLECTION_ARRAY:
  case IDL_COLLECTION_SET:
    return tbe_cpp_type_depends_on(field->inner_type, target);
  case IDL_COLLECTION_MAP:
    depends = tbe_cpp_type_depends_on(field->key_type, target);
    return depends == 0 ?
        tbe_cpp_type_depends_on(field->value_type, target) : depends;
  case IDL_COLLECTION_LIST:
    /* An incomplete T is permitted in std::vector<T> (C++17), but a
     * vector whose element is an associative template may instantiate a
     * pair/tree node requiring completed record values. Order those
     * declarations conservatively rather than publishing invalid C++. */
    if (field->inner_type != NULL) {
      IdlTypeRef element;
      if (!idl_type_ref_parse(field->inner_type,
                              strlen(field->inner_type), &element))
        return -1;
      if (element.collection_kind == IDL_COLLECTION_MAP ||
          element.collection_kind == IDL_COLLECTION_SET)
        return tbe_cpp_type_depends_on(field->inner_type, target);
    }
    return 0;
  case IDL_COLLECTION_GROUP:
    return 0; /* std::vector<T> may own incomplete T in C++17. */
  default:
    return -1;
  }
}

/* The per-kind lists remain available to custom Mustache templates. Built-in
 * C++ output uses cpp_records to guarantee complete definitions for direct
 * member dependencies even when declarations cross message/group/composite
 * sections. No clone is published until its Contract dependency is admitted. */
static int tbe_cpp_build_ordered_records(
    const IdlContract *contract, Node *root) {
  Node *ordered = create_node_list("cpp_records");
  Node *forwards = create_node_list("cpp_forward_declarations");
  unsigned char *emitted = NULL;
  size_t remaining = 0u;
  size_t i;
  if (ordered == NULL || forwards == NULL) {
    node_free(ordered);
    node_free(forwards);
    return 0;
  }
  if (map_add(root, ordered) != 0) {
    node_free(ordered);
    node_free(forwards);
    return 0;
  }
  if (map_add(root, forwards) != 0) {
    node_free(forwards);
    return 0;
  }
  if (contract->data_count != 0u) {
    emitted = (unsigned char *)calloc(contract->data_count, sizeof(*emitted));
    if (emitted == NULL)
      return 0;
  }
  for (i = 0u; i < contract->data_count; ++i) {
    const IdlDataDecl *decl = &contract->data[i];
    Node *forward;
    if (decl->kind == IDL_DATA_ENUM)
      continue;
    ++remaining;
    forward = create_node_map(NULL);
    if (forward == NULL ||
        tbe_compiler_set_string(forward, "name", decl->name) != 0 ||
        list_add(forwards, forward) != 0) {
      node_free(forward);
      free(emitted);
      return 0;
    }
  }
  while (remaining != 0u) {
    int advanced = 0;
    for (i = 0u; i < contract->data_count; ++i) {
      const IdlDataDecl *decl = &contract->data[i];
      int ready = 1;
      size_t j;
      if (decl->kind == IDL_DATA_ENUM || emitted[i])
        continue;
      for (j = 0u; j < decl->field_count && ready; ++j) {
        size_t candidate;
        for (candidate = 0u; candidate < contract->data_count; ++candidate) {
          const IdlDataDecl *other = &contract->data[candidate];
          int result;
          if (other->kind == IDL_DATA_ENUM || emitted[candidate])
            continue;
          result = tbe_cpp_field_depends_on(
              &decl->fields[j], other->name);
          if (result < 0) {
            free(emitted);
            return 0;
          }
          if (result > 0) {
            ready = 0;
            break;
          }
        }
      }
      if (!ready)
        continue;
      if (!tbe_source_render_decl(
              contract, decl, TBE_COMPILER_LANG_CPP, ordered)) {
        free(emitted);
        return 0;
      }
      emitted[i] = 1u;
      --remaining;
      advanced = 1;
    }
    if (!advanced) {
      fprintf(stderr,
              "C++ source cannot order by-value or associative record "
              "dependencies without an incomplete type\n");
      free(emitted);
      return 0;
    }
  }
  free(emitted);
  return 1;
}

static Node *tbe_source_render_ir(
    const IdlContract *contract, int64_t language) {
  Node *root = NULL;
  Node *schema = NULL;
  static const char *const lists[] = {
      "messages", "composites", "groups", "enums"
  };
  size_t i;
  if (contract == NULL ||
      (language != TBE_COMPILER_LANG_TS &&
       language != TBE_COMPILER_LANG_PYTHON &&
       language != TBE_COMPILER_LANG_GO &&
       language != TBE_COMPILER_LANG_RUST &&
       language != TBE_COMPILER_LANG_CPP))
    return NULL;
  if (language == TBE_COMPILER_LANG_RUST &&
      !tbe_rust_validate_sized_records(contract))
    return NULL;
  root = create_node_map(NULL);
  schema = create_node_map("schema");
  if (root == NULL || schema == NULL)
    goto failed;
  if (tbe_compiler_set_string(schema, "schema_name",
                              contract->name != NULL ? contract->name : "") != 0)
    goto failed;
  if (map_add(root, schema) != 0)
    goto failed;
  schema = NULL;
  for (i = 0u; i < sizeof(lists) / sizeof(lists[0]); ++i) {
    Node *list = create_node_list(lists[i]);
    if (list == NULL)
      goto failed;
    if (map_add(root, list) != 0) {
      node_free(list);
      goto failed;
    }
  }
  for (i = 0u; i < contract->data_count; ++i) {
    const IdlDataDecl *decl = &contract->data[i];
    const char *list_name =
        decl->kind == IDL_DATA_MESSAGE ? "messages" :
        decl->kind == IDL_DATA_COMPOSITE ? "composites" :
        decl->kind == IDL_DATA_GROUP ? "groups" :
        decl->kind == IDL_DATA_ENUM ? "enums" : NULL;
    Node *list = list_name != NULL
        ? tbe_compiler_find_child(root, list_name) : NULL;
    if (list == NULL) {
      fprintf(stderr,
              "%s projection does not implement Data kind for '%s'\n",
              language == TBE_COMPILER_LANG_CPP ? "C++" :
              language == TBE_COMPILER_LANG_RUST ? "Rust" :
              language == TBE_COMPILER_LANG_GO ? "Go" :
              language == TBE_COMPILER_LANG_PYTHON ? "Python" : "TypeScript",
              decl->name != NULL ? decl->name : "<unnamed>");
      goto failed;
    }
    if (!tbe_source_render_decl(contract, decl, language, list))
      goto failed;
  }
  if (language == TBE_COMPILER_LANG_CPP &&
      !tbe_cpp_build_ordered_records(contract, root))
    goto failed;
  if (language == TBE_COMPILER_LANG_GO) {
    /* Go package naming is presentation-only, derived from Contract schema. */
    tbe_compiler_annotate_schema_types(root);
    schema = tbe_compiler_find_child(root, "schema");
    if (schema == NULL ||
        tbe_compiler_string_value(schema, "go_package_name") == NULL)
      goto failed;
    schema = NULL; /* still owned by root */
  }
  return root;
failed:
  if (schema != NULL &&
      tbe_compiler_find_child(root, "schema") == schema)
    schema = NULL;
  node_free(schema);
  node_free(root);
  return NULL;
}

/* The built-in compiler's named outputs form one replacement unit.
 *
 * A render to each staging path can independently use render_file()'s secure
 * temporary-file + rename sequence. Only after all renders, generated source
 * appends and optional DSL/guest rendering have succeeded do we move any
 * caller-owned output.
 *
 * This is intentionally narrower than the selected projection backend set:
 * those callbacks may publish multiple files through backend-owned configs
 * and need a separate transaction API before cross-backend atomicity is real.
 */
enum { TBE_COMPILER_TRANSACTION_OUTPUT_LIMIT = 32u };

typedef struct tbe_compiler_staged_output {
  const char *final_path;
  char *staging_path;
  char *backup_path;
  int backup_reserved;
  int had_original;
  int original_moved;
  int published;
} tbe_compiler_staged_output;

typedef struct tbe_compiler_output_transaction {
  tbe_compiler_staged_output items[TBE_COMPILER_TRANSACTION_OUTPUT_LIMIT];
  size_t count;
} tbe_compiler_output_transaction;

static int tbe_compiler_txn_unlink(const char *path) {
  if (path == NULL) return 0;
  if (cmeta_fs_access(path, SALTS_FS_ACCESS_EXISTS) != 0) return 0;
  if (cmeta_fs_unlink(path) == SALTS_OK) return 0;
  fprintf(stderr, "Failed to remove compiler transaction file: %s\n", path);
  return -1;
}

static int tbe_compiler_txn_reserve(
    const char *final_path, char **out_path) {
  FILE *file = NULL;
  if (final_path == NULL || final_path[0] == '\0' ||
      out_path == NULL ||
      tbe_compiler_create_temporary_output(
          final_path, out_path, &file) != 0)
    return -1;
  if (fclose(file) != 0) {
    fprintf(stderr, "Failed to close reserved transaction file: %s\n",
            *out_path);
    return -1; /* The caller owns cleanup of the reserved path. */
  }
  return 0;
}

/* Match the frontend's lexical destination identity on Windows. */
static int tbe_compiler_txn_same_path(const char *a, const char *b) {
  if (a == NULL || b == NULL) return 0;
#ifdef _WIN32
  while (*a != '\0' && *b != '\0') {
    unsigned char x = (unsigned char)*a++;
    unsigned char y = (unsigned char)*b++;
    if (x == '\\') x = '/';
    if (y == '\\') y = '/';
    if (x >= 'A' && x <= 'Z') x = (unsigned char)(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = (unsigned char)(y - 'A' + 'a');
    if (x != y) return 0;
  }
  return *a == *b;
#else
  return strcmp(a, b) == 0;
#endif
}

static int tbe_compiler_txn_add(
    tbe_compiler_output_transaction *txn, const char *final_path) {
  tbe_compiler_staged_output *output;
  size_t i;
  if (txn == NULL || final_path == NULL || final_path[0] == '\0' ||
      txn->count >= TBE_COMPILER_TRANSACTION_OUTPUT_LIMIT)
    return -1;
  for (i = 0u; i < txn->count; ++i)
    if (tbe_compiler_txn_same_path(txn->items[i].final_path, final_path)) {
      fprintf(stderr, "Duplicate generated transaction output: %s\n", final_path);
      return -1;
    }
  output = &txn->items[txn->count++];
  output->final_path = final_path;
  if (tbe_compiler_txn_reserve(final_path, &output->staging_path) != 0) {
    fprintf(stderr, "Failed to stage compiler output: %s\n", final_path);
    return -1;
  }
  return 0;
}

/* IO rollback is fallible. Never hide it inside a nofail CMeta destructor.
 * An un-restorable original remains in its unique backup for recovery. */
static int tbe_compiler_txn_abort(
    tbe_compiler_output_transaction *txn) {
  size_t i;
  int failed = 0;
  if (txn == NULL) return -1;
  for (i = txn->count; i > 0u; --i) {
    tbe_compiler_staged_output *item = &txn->items[i - 1u];
    if (item->published &&
        tbe_compiler_txn_unlink(item->final_path) != 0)
      failed = 1;
    if (item->original_moved) {
      if (cmeta_fs_rename(item->backup_path, item->final_path) != SALTS_OK) {
        fprintf(stderr,
                "Failed restoring original %s; recover from %s\n",
                item->final_path, item->backup_path);
        failed = 1;
      } else {
        item->original_moved = 0;
      }
    }
    if (tbe_compiler_txn_unlink(item->staging_path) != 0)
      failed = 1;
    if (item->backup_reserved && !item->original_moved &&
        tbe_compiler_txn_unlink(item->backup_path) != 0)
      failed = 1;
    free(item->staging_path);
    free(item->backup_path);
  }
  *txn = (tbe_compiler_output_transaction){0};
  return failed ? -1 : 0;
}

static int tbe_compiler_txn_commit(
    tbe_compiler_output_transaction *txn) {
  size_t i;
  int cleanup_failed = 0;
  /* A single staged output needs the same backup/publish/rollback semantics
   * as a multi-output transaction; reject only empty transactions. */
  if (txn == NULL || txn->count == 0u) return -1;

  /* Prepare all backup slots before moving a single published file.
   * Every stage and backup is a unique O_EXCL sibling of the final path. */
  for (i = 0u; i < txn->count; ++i) {
    tbe_compiler_staged_output *item = &txn->items[i];
    struct stat info;
    /* Reject missing or non-regular staged outputs before touching any
     * caller-owned final path. Reservation alone is not proof of rendering;
     * backend callbacks must populate their declared outputs. */
    if (item->staging_path == NULL ||
        stat(item->staging_path, &info) != 0) {
      fprintf(stderr, "Missing staged compiler output: %s\n", item->final_path);
      goto rollback;
    }
#ifdef _WIN32
    if ((info.st_mode & _S_IFMT) != _S_IFREG) {
#else
    if (!S_ISREG(info.st_mode)) {
#endif
      fprintf(stderr, "Staged compiler output is not a regular file: %s\n",
              item->final_path);
      goto rollback;
    }
    if (stat(item->final_path, &info) != 0) {
      if (errno != ENOENT) {
        fprintf(stderr, "Failed to inspect output: %s\n", item->final_path);
        goto rollback;
      }
      continue;
    }
#ifdef _WIN32
    if ((info.st_mode & _S_IFMT) != _S_IFREG) {
#else
    if (!S_ISREG(info.st_mode)) {
#endif
      fprintf(stderr, "Compiler output is not a regular file: %s\n",
              item->final_path);
      goto rollback;
    }
#ifndef _WIN32
    if (chmod(item->staging_path, (mode_t)(info.st_mode & 0777)) != 0) {
      fprintf(stderr, "Failed to preserve output permissions: %s\n",
              item->final_path);
      goto rollback;
    }
#endif
    item->had_original = 1;
    if (tbe_compiler_txn_reserve(
            item->final_path, &item->backup_path) != 0) {
      /* reserve() may fail only after creating/closing a unique file.
       * Retain its ownership so rollback can remove that failed reservation. */
      item->backup_reserved = item->backup_path != NULL;
      fprintf(stderr, "Failed to reserve output backup: %s\n",
              item->final_path);
      goto rollback;
    }
    item->backup_reserved = 1;
  }

  for (i = 0u; i < txn->count; ++i) {
    tbe_compiler_staged_output *item = &txn->items[i];
    if (item->had_original) {
      if (cmeta_fs_rename(item->final_path,
                          item->backup_path) != SALTS_OK) {
        fprintf(stderr, "Failed to back up output: %s\n", item->final_path);
        goto rollback;
      }
      item->original_moved = 1;
    }
  }

  for (i = 0u; i < txn->count; ++i) {
    tbe_compiler_staged_output *item = &txn->items[i];
    if (cmeta_fs_rename(item->staging_path,
                        item->final_path) != SALTS_OK) {
      fprintf(stderr, "Failed to publish output: %s\n", item->final_path);
      goto rollback;
    }
    item->published = 1;
  }

  /* Cleanup failures must be visible even though all files were committed.
   * Preserve their new data rather than rolling back after publication. */
  for (i = 0u; i < txn->count; ++i) {
    tbe_compiler_staged_output *item = &txn->items[i];
    if (item->backup_reserved &&
        tbe_compiler_txn_unlink(item->backup_path) != 0)
      cleanup_failed = 1;
    free(item->staging_path);
    free(item->backup_path);
  }
  *txn = (tbe_compiler_output_transaction){0};
  return cleanup_failed ? -1 : 0;

rollback:
  if (tbe_compiler_txn_abort(txn) != 0)
    fprintf(stderr, "Compiler transaction rollback was incomplete\n");
  return -1;
}

/* Contract-only Native headers share the existing staged-output coordinator.
 * The legacy C/Wire entry remains separately Binary-admitted. */
int databind_compiler_generate_contract_native_header(
    const char *schema_path, const char *output_path) {
  tbe_compiler_output_transaction txn = {0};
  databind_native_source_ir ir = {0};
  Node *tree = NULL;
  IdlContract *contract = NULL;
  char *source = NULL;
  int status = -1;
  if (schema_path == NULL ||
      (output_path != NULL && output_path[0] == '\0')) return -1;
  if (databind_compiler_parse_contract_only_file(
          schema_path, &tree, &contract, &source) != 0)
    goto done;
  if (databind_native_source_ir_build(contract, &ir) != 0)
    goto done;
  /* Source-only stdout has no named filesystem transaction. Every output
   * path instead retains the rollback-aware staging/commit coordinator. */
  if (output_path == NULL) {
    status = databind_native_source_ir_write_stream(&ir, stdout);
    goto done;
  }
  if (tbe_compiler_txn_add(&txn, output_path) != 0)
    goto done;
  if (databind_native_source_ir_write_header(
          &ir, txn.items[0].staging_path) != 0)
    goto done;
  status = tbe_compiler_txn_commit(&txn);
done:
  if (txn.count != 0u && tbe_compiler_txn_abort(&txn) != 0)
    status = -1;
  databind_native_source_ir_destroy(&ir);
  idl_contract_destroy(contract);
  node_free(tree);
  free(source);
  return status;
}

/* All Contract-only artifacts use the same staged publication coordinator.
 * Native/Plugin/Wasm declare *every* output before rendering. A stage-safe
 * single-file artifact participates in the same transaction. None of these
 * artifacts implies Binary wire admission.
 *
 * The backend configuration retains final paths for generated #include
 * references; stage path pointers are owned by the transaction until commit. */
typedef struct databind_contract_artifact_stage {
  const databind_compiler_projection_backend *backend;
  size_t first_stage;
  size_t stage_count;
  int native;
  int plugin;
  int wasm;
} databind_contract_artifact_stage;

static int databind_compiler_generate_contract_native_bundle(
    const tbe_compiler_options_t *options) {
  tbe_compiler_output_transaction txn = {0};
  databind_native_source_ir native = {0};
  databind_compiler_projection_request *staged = NULL;
  databind_contract_artifact_stage *artifact_stages = NULL;
  databind_compiler_projection_input projection_input = {0};
  Node *tree = NULL;
  IdlContract *contract = NULL;
  char *source = NULL;
  char template_path[SALTS_FS_MAX_PATH];
  const char *dsl_stage = NULL;
  const char *dsl_template;
  size_t i;
  int status = -1;

  if (options == NULL || options->schema_path == NULL ||
      options->output_path == NULL || options->output_path[0] == '\0' ||
      options->projection_count > TBE_COMPILER_TRANSACTION_OUTPUT_LIMIT)
    return -1;

  if (options->projection_count != 0u) {
    if (options->projection_count > SIZE_MAX / sizeof(*staged) ||
        options->projection_count > SIZE_MAX / sizeof(*artifact_stages))
      return -1;
    staged = (databind_compiler_projection_request *)calloc(
        options->projection_count, sizeof(*staged));
    artifact_stages = (databind_contract_artifact_stage *)calloc(
        options->projection_count, sizeof(*artifact_stages));
    if (staged == NULL || artifact_stages == NULL)
      goto done;
    for (i = 0u; i < options->projection_count; ++i) {
      size_t j;
      const databind_compiler_projection_request *request =
          &options->projection_requests[i];
      databind_contract_artifact_stage *entry = &artifact_stages[i];
      if (request->id.axis !=
          DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT) {
        fprintf(stderr, "Contract-only C cannot select a Binary transport\n");
        goto done;
      }
      for (j = 0u; j < options->projection_backend_count; ++j)
        if (databind_compiler_projection_id_equal(
                request->id, options->projection_backends[j].id)) {
          entry->backend = &options->projection_backends[j];
          break;
        }
      if (entry->backend == NULL) goto done;
      /* True multi-file backends must use their audited staged API.
       * Other selected artifacts are admitted only with STAGED_SINGLE. */
      entry->native =
          entry->backend->output_policy == DATABIND_COMPILER_OUTPUT_STAGED_MULTI &&
          entry->backend->generate == databind_compiler_native_service_generate &&
          request->id.kind == DATABIND_COMPILER_ARTIFACT_NATIVE;
      entry->plugin =
          entry->backend->output_policy == DATABIND_COMPILER_OUTPUT_STAGED_MULTI &&
          entry->backend->generate == databind_compiler_plugin_generate &&
          request->id.kind == DATABIND_COMPILER_ARTIFACT_PLUGIN;
      entry->wasm =
          entry->backend->output_policy == DATABIND_COMPILER_OUTPUT_STAGED_MULTI &&
          entry->backend->generate == databind_compiler_wasm_generate &&
          request->id.kind == DATABIND_COMPILER_ARTIFACT_WASM;
      if (!entry->native && !entry->plugin && !entry->wasm &&
          entry->backend->output_policy !=
              DATABIND_COMPILER_OUTPUT_STAGED_SINGLE) {
        fprintf(stderr, "Artifact has no audited staged-output contract\n");
        goto done;
      }
      if ((entry->backend->generate == databind_compiler_native_service_generate ||
           entry->backend->generate == databind_compiler_plugin_generate ||
           entry->backend->generate == databind_compiler_wasm_generate) &&
          !entry->native && !entry->plugin && !entry->wasm) {
        fprintf(stderr, "Multi-output artifact backend has dishonest staging capability\n");
        goto done;
      }
      staged[i] = *request;
    }
  }

  if (databind_compiler_parse_contract_only_file(options->schema_path, &tree,
                                                  &contract, &source) != 0)
    goto done;
  if (databind_native_source_ir_build(contract, &native) != 0)
    goto done;
  if (tbe_compiler_txn_add(&txn, options->output_path) != 0)
    goto done;

  if (options->dsl_output_path != NULL) {
    if (tbe_compiler_txn_add(&txn, options->dsl_output_path) != 0)
      goto done;
    dsl_stage = txn.items[txn.count - 1u].staging_path;
  }

  /* Reserve *all* declared output paths before invoking any generator.
   * The transaction's duplicate/alias detection applies across artifacts,
   * DSL, primary header, and every secondary file. */
  for (i = 0u; i < options->projection_count; ++i) {
    const databind_compiler_projection_request *request =
        &options->projection_requests[i];
    databind_contract_artifact_stage *entry = &artifact_stages[i];
    const char *final_paths[4] = {0};
    size_t j;
    if (entry->native) {
      const databind_compiler_native_service_config *config =
          (const databind_compiler_native_service_config *)request->config;
      if (config == NULL) goto done;
      final_paths[0] = config->header_output;
      final_paths[1] = request->output;
      entry->stage_count = 2u;
    } else if (entry->plugin) {
      const databind_compiler_plugin_config *config =
          (const databind_compiler_plugin_config *)request->config;
      if (config == NULL) goto done;
      final_paths[0] = config->service_header_output;
      final_paths[1] = request->output;
      final_paths[2] = config->client_header_output;
      final_paths[3] = config->client_source_output;
      entry->stage_count = 4u;
    } else if (entry->wasm) {
      const databind_compiler_wasm_config *config =
          (const databind_compiler_wasm_config *)request->config;
      if (config == NULL) goto done;
      final_paths[0] = request->output;
      final_paths[1] = config->host_header_output;
      final_paths[2] = config->host_source_output;
      final_paths[3] = config->guest_header_output;
      entry->stage_count = 4u;
    } else {
      final_paths[0] = request->output;
      entry->stage_count = 1u;
    }
    entry->first_stage = txn.count;
    for (j = 0u; j < entry->stage_count; ++j)
      if (tbe_compiler_txn_add(&txn, final_paths[j]) != 0)
        goto done;
    if (entry->stage_count == 1u)
      staged[i].output = txn.items[entry->first_stage].staging_path;
  }

  if (databind_native_source_ir_write_header(
          &native, txn.items[0].staging_path) != 0)
    goto done;

  if (dsl_stage != NULL) {
    /* A render-only Node adapter is projected from the immutable Contract;
     * Binary overlay is never applied or consulted. */
    tbe_compiler_annotate_language_types(contract, tree);
    dsl_template = tbe_compiler_resolve_resource(
        options, "templates/reflection/rfl_types.mustache",
        template_path, sizeof(template_path));
    if (dsl_template == NULL ||
        tbe_compiler_render_file(tree, dsl_template, dsl_stage) != 0)
      goto done;
  }

  projection_input.contract = contract;
  projection_input.binary_format = NULL; /* No implicit Binary plan. */
  for (i = 0u; i < options->projection_count; ++i) {
    const databind_compiler_projection_request *request = &staged[i];
    const databind_contract_artifact_stage *entry = &artifact_stages[i];
    const size_t first = entry->first_stage;
    if (entry->native) {
      if (databind_compiler_native_service_render_staged(
              &projection_input, request,
              txn.items[first].staging_path,
              txn.items[first + 1u].staging_path) != 0)
        goto done;
    } else if (entry->plugin) {
      if (databind_compiler_plugin_render_staged(
              &projection_input, request,
              txn.items[first].staging_path,
              txn.items[first + 1u].staging_path,
              txn.items[first + 2u].staging_path,
              txn.items[first + 3u].staging_path) != 0)
        goto done;
    } else if (entry->wasm) {
      if (databind_compiler_wasm_render_staged(
              &projection_input, request,
              txn.items[first].staging_path,
              txn.items[first + 1u].staging_path,
              txn.items[first + 2u].staging_path,
              txn.items[first + 3u].staging_path) != 0)
        goto done;
    } else if (entry->backend->generate(
                   &projection_input, request,
                   entry->backend->context) != 0)
      goto done;
  }

  if (tbe_compiler_txn_commit(&txn) != 0)
    goto done;
  status = 0;

done:
  if (txn.count != 0u && tbe_compiler_txn_abort(&txn) != 0)
    status = -1;
  free(artifact_stages);
  free(staged);
  databind_native_source_ir_destroy(&native);
  idl_contract_destroy(contract);
  node_free(tree);
  free(source);
  return status;
}

static int tbe_compiler_output_paths_distinct(
    const tbe_compiler_options_t *options) {
  const char *paths[4];
  size_t i;
  size_t j;
  if (options == NULL) return 0;
  paths[0] = options->output_path;
  paths[1] = options->source_output_path;
  paths[2] = options->guest_output_path;
  paths[3] = options->dsl_output_path;
  for (i = 0u; i < 4u; ++i) {
    if (paths[i] == NULL) continue;
    if (paths[i][0] == '\0') return 0;
    for (j = 0u; j < i; ++j)
      if (paths[j] != NULL && strcmp(paths[i], paths[j]) == 0)
        return 0;
  }
  return 1;
}

/* Single-threaded task ownership. Projection views borrow semantic facts;
 * none of these resources escape a compiler invocation. */
typedef struct tbe_compiler_task_t {
  Node *root;
  Node *projection_root;
  Node *database_ir;
  Node *language_ir; /* TypeScript, projected only from immutable Contract IR. */
  IdlContract *contract;
  databind_binary_format_plan binary_format;
  char *schema_data;
} tbe_compiler_task_t;

static cmeta_status tbe_compiler_task_init(tbe_compiler_task_t *task) {
  *task = (tbe_compiler_task_t){0};
  return CMETA_OK;
}

static void tbe_compiler_task_restore(tbe_compiler_task_t *task) {
  databind_binary_format_plan_destroy(&task->binary_format);
  idl_contract_destroy(task->contract);
  free(task->schema_data);
  tbe_database_schema_destroy(task->database_ir);
  node_free(task->language_ir);
  node_free(task->projection_root);
  node_free(task->root);
  *task = (tbe_compiler_task_t){0};
}

static void tbe_compiler_task_move(tbe_compiler_task_t *out, tbe_compiler_task_t *task) {
  *out = *task;
  *task = (tbe_compiler_task_t){0};
}

static const cmeta_type_identity TBE_COMPILER_TASK_ID =
    CMETA_TYPE_ID_ATOM_INIT("databind.compiler.task");
static const cmeta_type_desc TBE_COMPILER_TASK_TYPE = {
    .name = "tbe_compiler_task_t", .size = sizeof(tbe_compiler_task_t),
    .align = _Alignof(tbe_compiler_task_t), .kind = CMETA_T_OBJECT,
    .identity = &TBE_COMPILER_TASK_ID};
CMETA_DEFINE_LIFECYCLE(tbe_compiler_task_t, &TBE_COMPILER_TASK_TYPE,
    tbe_compiler_task_init, tbe_compiler_task_restore, tbe_compiler_task_move,
    CMETA_LIFECYCLE_INIT_NOFAIL | CMETA_LIFECYCLE_TRIVIAL_ZERO | CMETA_LIFECYCLE_MOVABLE);

static int tbe_compiler_run_owned(tbe_compiler_task_t *task,
    const tbe_compiler_options_t *options) {
  char template_path[SALTS_FS_MAX_PATH];
  const char *resolved_template = NULL;
  const char *lang_name = tbe_compiler_language_name(options->lang_enum);
  tbe_compiler_output_transaction builtins = {0};
  databind_compiler_projection_request *transaction_requests = NULL;
  const char *primary_output_path = options->output_path;
  const char *source_output_path = options->source_output_path;
  const char *guest_output_path = options->guest_output_path;
  const char *dsl_output_path = options->dsl_output_path;
  int transactional_outputs = 0;
  int backend_transaction = 0;
  int native_transaction = 0;
  int wasm_transaction = 0;
  int plugin_transaction = 0;
  int seen_plugin = 0;
  const databind_compiler_plugin_config *plugin_config = NULL;
  const char *plugin_stages[4] = {0};
  int seen_wasm = 0;
  const databind_compiler_wasm_config *wasm_config = NULL;
  const char *wasm_stages[4] = {0};
  const databind_compiler_native_service_config *native_config = NULL;
  const char *native_header_stage = NULL;
  const char *native_source_stage = NULL;
  int database_language;
  int source_language;
  if (lang_name == NULL) {
    fprintf(stderr, "Unsupported compiler language enum: %lld\n",
            (long long)options->lang_enum);
    return 1;
  }
  database_language = tbe_compiler_is_database_language(options->lang_enum);
  source_language = options->lang_enum == TBE_COMPILER_LANG_TS ||
                    options->lang_enum == TBE_COMPILER_LANG_PYTHON ||
                    options->lang_enum == TBE_COMPILER_LANG_GO ||
                    options->lang_enum == TBE_COMPILER_LANG_RUST ||
                    options->lang_enum == TBE_COMPILER_LANG_CPP;
  if (!tbe_compiler_validate_options(options, lang_name)) return 1;
  if (!tbe_compiler_output_paths_distinct(options)) {
    fprintf(stderr,
            "Compiler output paths must be non-empty and distinct\n");
    return 1;
  }
  /* The existing projection dispatcher validates backend registration only
   * after rendering --output. Admission must happen before touching any
   * caller path, including C --source-output and guest artifacts. */
  if (!databind_compiler_projection_selection_valid(
          options->projection_requests, options->projection_count,
          options->projection_backends, options->projection_backend_count)) {
    fprintf(stderr,
            "Invalid projection selection or missing typed backend; "
            "no output published\n");
    return 1;
  }
  if (source_language && options->dsl_output_path != NULL) {
    fprintf(stderr, "--dsl-output requires native RulesForge lowering, not a source-only language\n");
    return 1;
  }
  if (options->binary_codec &&
      options->lang_enum != TBE_COMPILER_LANG_C) {
    fprintf(stderr, "--binary-codec requires --lang c\n");
    return 1;
  }
  if (options->lang_enum == TBE_COMPILER_LANG_C &&
      !options->binary_codec &&
      options->source_output_path == NULL &&
      options->guest_output_path == NULL &&
      !tbe_compiler_projection_requires_binary(options)) {
    if (options->template_path != NULL) {
      fprintf(stderr,
              "Custom C templates require explicit --binary-codec; "
              "NativeSourceIR never falls back to a Binary template\n");
      return 1;
    }
    /* Bare C, Contract DSL, and stage-safe typed artifact outputs never
     * construct a Binary-mutated Node or wire representation. */
    if (options->dsl_output_path != NULL ||
        options->projection_count != 0u)
      return databind_compiler_generate_contract_native_bundle(options) == 0
                 ? 0 : 1;
    return databind_compiler_generate_contract_native_header(
               options->schema_path, options->output_path) == 0 ? 0 : 1;
  }
  int status = databind_compiler_parse_contract_file_mode(
      options->schema_path, &task->root, &task->contract,
      &task->schema_data,
      (database_language || source_language)
          ? DATABIND_COMPILER_FORMAT_CONTRACT_ONLY
          : DATABIND_COMPILER_FORMAT_BINARY);
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
    task->projection_root = tbe_compiler_clone_canonical_node(task->root);
    if (task->projection_root == NULL) {
      fprintf(stderr, "Failed to preserve legacy projection view\n");
      return 1;
    }
    tbe_error_init(&format_error);
    if (tbe_compiler_projection_requires_binary(options)) {
      /* A selected Binary transport compiles wire annotations in its private
       * projection view. TypeScript and SQL never admit Binary implicitly. */
      if ((database_language || source_language) &&
          databind_binary_contract_apply(task->projection_root, &format_error) != 0) {
        fprintf(stderr, "Selected Binary projection rejected: %s\n",
                format_error.message);
        return 1;
      }
      if (!databind_binary_format_plan_build(
              task->contract, task->projection_root,
              &task->binary_format, &format_error)) {
        fprintf(stderr, "Failed to compile selected Binary format plan: %s\n",
                format_error.message);
        return 1;
      }
    }
  }

  if (!tbe_compiler_validate_enum_backend(task->contract, options)) {
    return 1;
  }

  if (source_language) {
    task->language_ir = tbe_source_render_ir(
        task->contract, options->lang_enum);
    if (task->language_ir == NULL) {
      fprintf(stderr, "Failed to construct source-language Contract rendering IR\n");
      return 1;
    }
  }

  if (database_language) {
    tbe_database_schema_diagnostic_t diagnostic;
    tbe_database_dialect_t dialect = options->lang_enum == TBE_COMPILER_LANG_SQLITE
                                         ? TBE_DATABASE_DIALECT_SQLITE
                                         : TBE_DATABASE_DIALECT_POSTGRESQL;
    tbe_database_schema_status_t database_status =
        tbe_database_schema_build_contract(
            task->contract, dialect, &task->database_ir, &diagnostic);
    if (database_status != TBE_DATABASE_SCHEMA_STATUS_OK) {
      fprintf(stderr,
              "Database schema validation failed for --lang %s: message=%s field=%s %s\n",
              lang_name, diagnostic.message_name, diagnostic.field_name, diagnostic.context);
      return 1;
    }
  } else if (!source_language) {
    if (tbe_compiler_set_string(task->root, "generated_header",
                                tbe_compiler_path_basename(options->output_path)) != 0) {
      return 1;
    }
    {
      char *schema_literal = tbe_compiler_escape_c_string(task->schema_data);
      if (!schema_literal || tbe_compiler_set_string(task->root, "schema_c_literal", schema_literal) != 0) {
        free(schema_literal);
        return 1;
      }
      free(schema_literal);
    }
  }

  if (options->source_output_path) {
    if (options->output_path == NULL || options->output_path[0] == '\0') {
      fprintf(stderr, "--source-output requires --output for the generated header\n");
      return 1;
    }
    if (strcmp(options->output_path, options->source_output_path) == 0) {
      fprintf(stderr, "--output and --source-output must name different files\n");
      return 1;
    }
    if (options->lang_enum != TBE_COMPILER_LANG_C || options->template_path != NULL) {
      fprintf(stderr, "--source-output is supported only for the built-in C generator\n");
      return 1;
    }
    if (!tbe_compiler_typed_schema_supported(task->root)) {
      return 1;
    }
    if (tbe_compiler_set_string(task->root, "typed_source_enabled", "1") != 0) {
      return 1;
    }
    if (tbe_compiler_annotate_binary_reader_messages(task->root, task->contract, task->projection_root) != 0) {
      fprintf(stderr, "Failed to evaluate generated Binary message admission\n");
      return 1;
    }
    if (tbe_compiler_prepare_cmeta_records(task->root) != 0) {
      fprintf(stderr, "Failed to prepare generated CMeta record schemas\n");
      return 1;
    }
  }

  if (options->guest_output_path) {
    if (options->output_path == NULL || options->output_path[0] == '\0') {
      fprintf(stderr, "--guest-output requires --output for the generated header\n");
      return 1;
    }
    if (strcmp(options->output_path, options->guest_output_path) == 0) {
      fprintf(stderr, "--output and --guest-output must name different files\n");
      return 1;
    }
    if (options->source_output_path != NULL &&
        strcmp(options->source_output_path, options->guest_output_path) == 0) {
      fprintf(stderr, "--source-output and --guest-output must name different files\n");
      return 1;
    }
    if (options->lang_enum != TBE_COMPILER_LANG_C || options->template_path != NULL) {
      fprintf(stderr, "--guest-output is supported only for the built-in C generator\n");
      return 1;
    }
    if (tbe_compiler_set_string(task->root, "guest_adapter_enabled", "1") != 0) {
      return 1;
    }
    if (options->source_output_path == NULL &&
        tbe_compiler_set_string(task->root, "guest_adapter_only", "1") != 0) {
      return 1;
    }
  }

  resolved_template = options->template_path;
  if (resolved_template == NULL) {
    resolved_template = tbe_compiler_resolve_resource(
        options, tbe_compiler_resolve_template(NULL, options->lang_enum), template_path,
        sizeof(template_path));
    if (resolved_template == NULL) {
      return 1;
    }
  }
  /* A backend can participate only after explicitly declaring that its
   * *single* named output can be redirected to a sibling stage pathname.
   * Plugin/Wasm/Native Service are multi-output and remain self-publishing;
   * never guess their secondary outputs from a request->output suffix. */
  backend_transaction = databind_compiler_projection_all_staged_single(
      options->projection_requests, options->projection_count,
      options->projection_backends, options->projection_backend_count);
  /* Native Service participates only with stage-safe peers. Unsupported
   * mixes fail before any caller-owned output is rendered. */
  if (options->projection_count != 0u &&
      options->projection_requests != NULL &&
      options->projection_backends != NULL) {
    size_t i;
    int seen_native = 0;
    int qualified = 1;
    for (i = 0u; i < options->projection_count; ++i) {
      size_t j;
      const databind_compiler_projection_request *request =
          &options->projection_requests[i];
      const databind_compiler_projection_backend *backend = NULL;
      for (j = 0u; j < options->projection_backend_count; ++j)
        if (databind_compiler_projection_id_equal(
                request->id, options->projection_backends[j].id)) {
          backend = &options->projection_backends[j];
          break;
        }
      if (backend == NULL) { qualified = 0; continue; }
      if (backend->generate == databind_compiler_native_service_generate &&
          request->id.axis == DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT &&
          request->id.kind == DATABIND_COMPILER_ARTIFACT_NATIVE) {
        native_config = (const databind_compiler_native_service_config *)request->config;
        if (seen_native || native_config == NULL ||
            native_config->header_output == NULL ||
            native_config->header_output[0] == '\\0') {
          qualified = 0;
        }
        seen_native = 1;
      } else if (backend->generate == databind_compiler_plugin_generate &&
                 request->id.axis == DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT &&
                 request->id.kind == DATABIND_COMPILER_ARTIFACT_PLUGIN) {
        plugin_config = (const databind_compiler_plugin_config *)request->config;
        if (seen_plugin || plugin_config == NULL ||
            plugin_config->service_header_output == NULL ||
            plugin_config->client_header_output == NULL ||
            plugin_config->client_source_output == NULL ||
            request->output == NULL || request->output[0] == '\0')
          qualified = 0;
        seen_plugin = 1;
      } else if (backend->generate == databind_compiler_wasm_generate &&
                 request->id.axis == DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT &&
                 request->id.kind == DATABIND_COMPILER_ARTIFACT_WASM) {
        wasm_config = (const databind_compiler_wasm_config *)request->config;
        if (seen_wasm || wasm_config == NULL ||
            wasm_config->host_header_output == NULL ||
            wasm_config->host_source_output == NULL ||
            wasm_config->guest_header_output == NULL ||
            request->output == NULL || request->output[0] == '\0')
          qualified = 0;
        seen_wasm = 1;
      } else if (backend->output_policy != DATABIND_COMPILER_OUTPUT_STAGED_SINGLE ||
                 request->output == NULL || request->output[0] == '\\0') {
        qualified = 0;
        continue;
      }
    }
    native_transaction = seen_native && qualified && options->output_path != NULL;
    wasm_transaction = seen_wasm && qualified && options->output_path != NULL;
    plugin_transaction = seen_plugin && qualified && options->output_path != NULL;
    if ((seen_native || seen_wasm || seen_plugin) && !qualified) {
      fprintf(stderr, "Multi-output projection requires all selected backends staged; no output published\n");
      return 1;
    }
    if (seen_plugin && !plugin_transaction) {
      fprintf(stderr, "Plugin requires a named shared output transaction; no output published\n");
      return 1;
    }
    if (seen_wasm && !wasm_transaction) {
      fprintf(stderr,
              "Wasm requires a named output and fully staged selected backends; no output published\n");
      return 1;
    }
    if (seen_native && !native_transaction) {
      fprintf(stderr,
              "Native Service requires a named output and fully staged selected backends; no output published\n");
      return 1;
    }
  }
  transactional_outputs = options->output_path != NULL &&
      (backend_transaction || native_transaction || wasm_transaction || plugin_transaction ||
       (options->projection_count == 0u &&
        (options->source_output_path != NULL ||
         options->guest_output_path != NULL ||
         options->dsl_output_path != NULL)));
  if (transactional_outputs) {
    if (tbe_compiler_txn_add(&builtins, options->output_path) != 0)
      goto builtin_stage_failure;
    primary_output_path = builtins.items[builtins.count - 1u].staging_path;
    if (options->source_output_path != NULL) {
      if (tbe_compiler_txn_add(&builtins, options->source_output_path) != 0)
        goto builtin_stage_failure;
      source_output_path = builtins.items[builtins.count - 1u].staging_path;
    }
    if (options->guest_output_path != NULL) {
      if (tbe_compiler_txn_add(&builtins, options->guest_output_path) != 0)
        goto builtin_stage_failure;
      guest_output_path = builtins.items[builtins.count - 1u].staging_path;
    }
    if (options->dsl_output_path != NULL) {
      if (tbe_compiler_txn_add(&builtins, options->dsl_output_path) != 0)
        goto builtin_stage_failure;
      dsl_output_path = builtins.items[builtins.count - 1u].staging_path;
    }
    if (backend_transaction || native_transaction || wasm_transaction || plugin_transaction) {
      size_t i;
      if (options->projection_count >
          SIZE_MAX / sizeof(*transaction_requests))
        goto builtin_stage_failure;
      transaction_requests = (databind_compiler_projection_request *)calloc(
          options->projection_count, sizeof(*transaction_requests));
      if (transaction_requests == NULL)
        goto builtin_stage_failure;
      for (i = 0u; i < options->projection_count; ++i) {
        transaction_requests[i] = options->projection_requests[i];
        if (tbe_compiler_txn_add(
                &builtins, transaction_requests[i].output) != 0)
          goto builtin_stage_failure;
        if ((native_transaction || wasm_transaction) &&
            transaction_requests[i].id.axis == DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT &&
            transaction_requests[i].id.kind == DATABIND_COMPILER_ARTIFACT_NATIVE) {
          native_source_stage = builtins.items[builtins.count - 1u].staging_path;
          /* Native rendering needs the original request output for include semantics. */
        } else if (plugin_transaction &&
                   transaction_requests[i].id.axis == DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT &&
                   transaction_requests[i].id.kind == DATABIND_COMPILER_ARTIFACT_PLUGIN) {
          plugin_stages[1] = builtins.items[builtins.count - 1u].staging_path;
        } else if (wasm_transaction &&
                   transaction_requests[i].id.axis == DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT &&
                   transaction_requests[i].id.kind == DATABIND_COMPILER_ARTIFACT_WASM) {
          wasm_stages[0] = builtins.items[builtins.count - 1u].staging_path;
        } else {
          transaction_requests[i].output =
              builtins.items[builtins.count - 1u].staging_path;
        }
      }
      if (plugin_transaction) {
        const char *secondary[] = {
            plugin_config->service_header_output,
            plugin_config->client_header_output,
            plugin_config->client_source_output};
        const size_t positions[] = {0u, 2u, 3u};
        size_t k;
        for (k = 0u; k < 3u; ++k) {
          if (tbe_compiler_txn_add(&builtins, secondary[k]) != 0)
            goto builtin_stage_failure;
          plugin_stages[positions[k]] =
              builtins.items[builtins.count - 1u].staging_path;
        }
      }
      if (wasm_transaction) {
        const char *secondary[] = {
            wasm_config->host_header_output, wasm_config->host_source_output,
            wasm_config->guest_header_output};
        size_t k;
        for (k = 0; k < 3u; ++k) {
          if (tbe_compiler_txn_add(&builtins, secondary[k]) != 0)
            goto builtin_stage_failure;
          wasm_stages[k + 1u] = builtins.items[builtins.count - 1u].staging_path;
        }
      }
      if (native_transaction) {
        if (tbe_compiler_txn_add(&builtins, native_config->header_output) != 0)
          goto builtin_stage_failure;
        native_header_stage = builtins.items[builtins.count - 1u].staging_path;
      }
    }
  }
  status = tbe_compiler_render_file(
      database_language ? task->database_ir :
      source_language ? task->language_ir : task->root,
      resolved_template, primary_output_path);

  if (status == 0 && options->source_output_path) {
    resolved_template = tbe_compiler_resolve_resource(
        options, "templates/c/c_typed_source.mustache", template_path, sizeof(template_path));
    status = resolved_template != NULL
                 ? tbe_compiler_render_file(task->root, resolved_template, source_output_path)
                 : 1;
    if (status == 0 &&
        tbe_compiler_append_member_lifecycles(source_output_path, task->root) != 0) {
      fprintf(stderr, "Failed to append generated member lifecycle functions\n");
      status = 1;
    }
    if (status == 0 &&
        tbe_compiler_append_binary_readers(
            source_output_path, task->root, task->contract, task->projection_root) != 0) {
      fprintf(stderr, "Failed to append generated Binary reader providers\n");
      status = 1;
    }
  }

  if (status == 0 && options->guest_output_path) {
    resolved_template = tbe_compiler_resolve_resource(
        options, "templates/c/c_guest_adapter.mustache", template_path, sizeof(template_path));
    status = resolved_template != NULL
                 ? tbe_compiler_render_file(task->root, resolved_template, guest_output_path)
                 : 1;
  }

  if (status == 0 && options->dsl_output_path) {
    resolved_template = tbe_compiler_resolve_resource(
        options, "templates/reflection/rfl_types.mustache", template_path, sizeof(template_path));
    if (resolved_template == NULL ||
        tbe_compiler_render_file(task->root, resolved_template, dsl_output_path) != 0) {
      status = 1;
    }
  }

  if (status == 0 && options->projection_count != 0u) {
    databind_compiler_projection_input projection_input = {
        .contract = task->contract,
        .binary_format = &task->binary_format};
    if (native_transaction || wasm_transaction || plugin_transaction) {
      size_t i;
      for (i = 0u; i < options->projection_count; ++i) {
        const databind_compiler_projection_request *request = &transaction_requests[i];
        size_t j;
        const databind_compiler_projection_backend *backend = NULL;
        for (j = 0u; j < options->projection_backend_count; ++j)
          if (databind_compiler_projection_id_equal(
                  request->id, options->projection_backends[j].id)) {
            backend = &options->projection_backends[j];
            break;
          }
        if (backend == NULL) { status = 1; break; }
        if (backend->generate == databind_compiler_native_service_generate) {
          if (databind_compiler_native_service_render_staged(
                  &projection_input, request, native_header_stage,
                  native_source_stage) != 0) { status = 1; break; }
        } else if (backend->generate == databind_compiler_plugin_generate) {
          if (databind_compiler_plugin_render_staged(&projection_input, request,
                  plugin_stages[0], plugin_stages[1], plugin_stages[2],
                  plugin_stages[3]) != 0) { status = 1; break; }
        } else if (backend->generate == databind_compiler_wasm_generate) {
          if (databind_compiler_wasm_render_staged(&projection_input, request,
                  wasm_stages[0], wasm_stages[1], wasm_stages[2],
                  wasm_stages[3]) != 0) { status = 1; break; }
        } else if (backend->generate(&projection_input, request,
                                      backend->context) != 0) {
          status = 1;
          break;
        }
      }
    } else if (databind_compiler_projection_run(
            &projection_input,
            transaction_requests != NULL
                ? transaction_requests : options->projection_requests,
            options->projection_count,
            options->projection_backends,
            options->projection_backend_count) != 0)
      status = 1;
  }

  if (transactional_outputs) {
    if (status != 0) {
      if (tbe_compiler_txn_abort(&builtins) != 0)
        fprintf(stderr, "Failed cleaning abandoned compiler outputs\n");
      free(transaction_requests);
      return 1;
    }
    status = tbe_compiler_txn_commit(&builtins);
    free(transaction_requests);
    return status == 0 ? 0 : 1;
  }
  return status;

builtin_stage_failure:
  if (tbe_compiler_txn_abort(&builtins) != 0)
    fprintf(stderr, "Failed cleaning compiler staging output files\n");
  free(transaction_requests);
  return 1;
}

int tbe_compiler_run(const tbe_compiler_options_t *options) {
  int status;
  cmeta_scope(status, cmeta_autos((tbe_compiler_task_t, task)),
      cmeta_body(tbe_compiler_run_owned(&task, options)));
  return status;
}
