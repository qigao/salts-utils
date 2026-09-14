#include "compiler_core.h"

#include "database_schema.h"
#include "mustache.h"
#include "mustache_helpers.h"
#include "schema_parser_dsl.h"
#include "schema_cmeta.h"
#include <salts_cmeta_data.h>
#include <salts_cmeta_fixed_width.h>
#include "tbe_error.h"
#include "salts_fs.h"
#include "salts_uuid.h"

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
  const char *typed_kind;
  const char *native_data_symbol;
  const char *native_type_symbol;
} tbe_compiler_scalar_projection_t;

typedef enum tbe_compiler_native_requirement {
  TBE_COMPILER_NATIVE_FIXED_VALUE,
  TBE_COMPILER_NATIVE_ENUM_DOMAIN,
  TBE_COMPILER_NATIVE_OWNED_LIFECYCLE,
  TBE_COMPILER_NATIVE_OVERLAY_PRESENCE,
  TBE_COMPILER_NATIVE_DEFERRED_CONTAINER
} tbe_compiler_native_requirement_t;

static const char *tbe_compiler_native_requirement_name(
    tbe_compiler_native_requirement_t requirement) {
  switch (requirement) {
    case TBE_COMPILER_NATIVE_FIXED_VALUE:
      return "fixed_value";
    case TBE_COMPILER_NATIVE_ENUM_DOMAIN:
      return "enum_domain";
    case TBE_COMPILER_NATIVE_OWNED_LIFECYCLE:
      return "owned_lifecycle";
    case TBE_COMPILER_NATIVE_OVERLAY_PRESENCE:
      return "overlay_presence";
    case TBE_COMPILER_NATIVE_DEFERRED_CONTAINER:
      return "deferred_container";
  }
  return NULL;
}

/* Native symbol spellings project the same canonical records into generated C. */
static const tbe_compiler_scalar_projection_t TBE_COMPILER_SCALAR_PROJECTIONS[] = {
    {&cmeta_data_bool, "uint8_t", "bool", "bool", "bool", "boolean", "bool", "boolean", "TBE_TYPED_BOOL", "salts_bool8_cmeta_data", "salts_bool8_cmeta_type"},
    {&salts_int8_cmeta_data, "int8_t", "std::int8_t", "int8", "i8", "number", "int", "int", "TBE_TYPED_I8", "salts_int8_cmeta_data", "salts_int8_cmeta_type"},
    {&salts_uint8_cmeta_data, "uint8_t", "std::uint8_t", "uint8", "u8", "number", "int", "int", "TBE_TYPED_U8", "salts_uint8_cmeta_data", "salts_uint8_cmeta_type"},
    {&salts_int16_cmeta_data, "int16_t", "std::int16_t", "int16", "i16", "number", "int", "int", "TBE_TYPED_I16", "salts_int16_cmeta_data", "salts_int16_cmeta_type"},
    {&salts_uint16_cmeta_data, "uint16_t", "std::uint16_t", "uint16", "u16", "number", "int", "int", "TBE_TYPED_U16", "salts_uint16_cmeta_data", "salts_uint16_cmeta_type"},
    {&salts_int32_cmeta_data, "int32_t", "std::int32_t", "int32", "i32", "number", "int", "int", "TBE_TYPED_I32", "salts_int32_cmeta_data", "salts_int32_cmeta_type"},
    {&salts_uint32_cmeta_data, "uint32_t", "std::uint32_t", "uint32", "u32", "number", "int", "int", "TBE_TYPED_U32", "salts_uint32_cmeta_data", "salts_uint32_cmeta_type"},
    {&salts_int64_cmeta_data, "int64_t", "std::int64_t", "int64", "i64", "number", "int", "long", "TBE_TYPED_I64", "salts_int64_cmeta_data", "salts_int64_cmeta_type"},
    {&salts_uint64_cmeta_data, "uint64_t", "std::uint64_t", "uint64", "u64", "number", "int", "uint64", "TBE_TYPED_U64", "salts_uint64_cmeta_data", "salts_uint64_cmeta_type"},
    {&cmeta_data_float, "float", "float", "float32", "f32", "number", "float", "float", "TBE_TYPED_F32", "cmeta_data_float", "cmeta_type_float"},
    {&cmeta_data_double, "double", "double", "float64", "f64", "number", "float", "double", "TBE_TYPED_F64", "cmeta_data_double", "cmeta_type_double"},
};

static const tbe_compiler_scalar_projection_t *tbe_compiler_scalar_projection(const char *type) {
  const cmeta_data_desc *data = schema_cmeta_builtin_data(type);
  size_t i;
  if (!cmeta_data_desc_valid(data)) return NULL;
  for (i = 0; i < sizeof(TBE_COMPILER_SCALAR_PROJECTIONS) /
                          sizeof(TBE_COMPILER_SCALAR_PROJECTIONS[0]);
       ++i) {
    const tbe_compiler_scalar_projection_t *projection = &TBE_COMPILER_SCALAR_PROJECTIONS[i];
    if (data->kind == projection->data->kind &&
        cmeta_type_identity_equal(data->storage_type->identity,
                                  projection->data->storage_type->identity))
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

static const char *tbe_compiler_typed_kind(const char *type) {
  const tbe_compiler_scalar_projection_t *projection;
  if (!type) return NULL;
  projection = tbe_compiler_scalar_projection(type);
  if (projection) return projection->typed_kind;
  if (strcmp(type, "uuid") == 0) return "TBE_TYPED_UUID";
  return NULL;
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

static const char *tbe_compiler_typed_named_kind(Node *root, const char *type,
                                                 char *c_type, size_t c_type_size,
                                                 char *descriptor, size_t descriptor_size) {
  const char *kind = tbe_compiler_typed_kind(type);
  const char *scalar = tbe_compiler_typed_c_scalar(type);
  Node *record;
  if (type && strcmp(type, "string") == 0) {
    snprintf(c_type, c_type_size, "tstr");
    descriptor[0] = '\0';
    return "TBE_TYPED_STRING";
  }
  if (type && strcmp(type, "bytes") == 0) {
    snprintf(c_type, c_type_size, "tbe_bytes_t");
    descriptor[0] = '\0';
    return "TBE_TYPED_BYTES";
  }
  if (kind && scalar) {
    snprintf(c_type, c_type_size, "%s", scalar);
    descriptor[0] = '\0';
    return kind;
  }
  record = tbe_compiler_find_record(root, "enums", type);
  if (record) {
    snprintf(c_type, c_type_size, "%s_t", type);
    descriptor[0] = '\0';
    return "TBE_TYPED_ENUM";
  }
  record = tbe_compiler_find_record(root, "composites", type);
  if (!record) record = tbe_compiler_find_record(root, "groups", type);
  if (!record) record = tbe_compiler_find_record(root, "messages", type);
  if (record) {
    snprintf(c_type, c_type_size, "%s_t", type);
    snprintf(descriptor, descriptor_size, "&%s_TYPED_TYPE", type);
    return "TBE_TYPED_OBJECT";
  }
  c_type[0] = '\0';
  descriptor[0] = '\0';
  return NULL;
}

static const char *tbe_compiler_typed_wire_kind(Node *root, const char *type,
                                                const char *fallback) {
  Node *record = tbe_compiler_find_record(root, "enums", type);
  if (record) {
    const char *underlying = tbe_compiler_string_value(record, "underlying_type");
    const char *kind = tbe_compiler_typed_kind(underlying ? underlying : "int32");
    return kind ? kind : "TBE_TYPED_I32";
  }
  return fallback;
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

static void tbe_compiler_annotate_typed_field(Node *root, Node *field,
                                             const schema_cmeta_field_type *semantic) {
  const char *name = tbe_compiler_string_value(field, "name");
  const char *c_name = tbe_compiler_attribute_value(field, "c");
  const char *owner = tbe_compiler_string_value(field, "owner_name");
  const char *type = tbe_compiler_string_value(field, "type");
  char c_type[256] = {0};
  char descriptor[256] = {0};
  char declaration[512] = {0};
  char vector_type[256] = {0};
  const char *kind = NULL;

  if (!name || !owner) return;
  if (!c_name || !c_name[0]) c_name = name;
  tbe_compiler_set_string(field, "c_name", c_name);
  if (semantic && semantic->kind == CMETA_DATA_SEQUENCE &&
      strcmp(semantic->schema_kind, "group") == 0) {
    const char *group_type = tbe_compiler_string_value(field, "group_type");
    snprintf(c_type, sizeof(c_type), "%s_t", group_type ? group_type : "unknown");
    snprintf(descriptor, sizeof(descriptor), "&%s_TYPED_TYPE", group_type ? group_type : "unknown");
    snprintf(vector_type, sizeof(vector_type), "%s_%s_vec_t", owner, name);
    snprintf(declaration, sizeof(declaration), "%s %s;", vector_type, c_name);
    tbe_compiler_set_string(field, "typed_kind", "TBE_TYPED_LIST");
    tbe_compiler_set_string(field, "typed_element_kind", "TBE_TYPED_OBJECT");
    tbe_compiler_set_string(field, "typed_element_wire_kind", "TBE_TYPED_OBJECT");
    tbe_compiler_set_string(field, "typed_element_c_type", c_type);
    tbe_compiler_set_string(field, "typed_object_descriptor", descriptor);
    tbe_compiler_set_string(field, "typed_vector_type", vector_type);
    tbe_compiler_set_string(field, "typed_needs_vector", "1");
    tbe_compiler_set_string(field, "typed_is_group", "1");
    tbe_compiler_set_string(field, "typed_declaration", declaration);
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
      tbe_compiler_set_string(field, "typed_kind", "TBE_TYPED_FIXED_BYTES");
      tbe_compiler_set_string(field, "typed_wire_kind", "TBE_TYPED_FIXED_BYTES");
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
      snprintf(declaration, sizeof(declaration), "tbe_bytes_t %s;", c_name);
      tbe_compiler_set_string(field, "typed_kind", "TBE_TYPED_BYTES");
      tbe_compiler_set_string(field, "typed_is_var_data", "1");
    }
    tbe_compiler_set_string(field, "typed_declaration", declaration);
    return;
  }
  if (semantic && cmeta_data_kind_is_container(semantic->kind)) {
    const char *inner = tbe_compiler_string_value(field, "inner_type");
    kind = tbe_compiler_typed_named_kind(root, inner, c_type, sizeof(c_type), descriptor,
                                         sizeof(descriptor));
    if (!kind) {
      snprintf(declaration, sizeof(declaration), "%s_t %s;", inner ? inner : "unknown", c_name);
      tbe_compiler_set_string(field, "typed_declaration", declaration);
      return;
    }
    tbe_compiler_set_string(field, "typed_element_kind", kind);
    tbe_compiler_set_string(field, "typed_element_wire_kind",
                            tbe_compiler_typed_wire_kind(root, inner, kind));
    tbe_compiler_set_string(field, "typed_element_c_type", c_type);
    if (descriptor[0]) tbe_compiler_set_string(field, "typed_object_descriptor", descriptor);
    if (tbe_compiler_has_child(field, "is_fixed_size")) {
      const char *count = tbe_compiler_string_value(field, "length_field");
      snprintf(declaration, sizeof(declaration), "%s %s[%s];", c_type, c_name,
               count ? count : "0");
      tbe_compiler_set_string(field, "typed_kind", "TBE_TYPED_FIXED_ARRAY");
      tbe_compiler_set_string(field, "typed_fixed_count", count ? count : "0");
    } else if (semantic->kind == CMETA_DATA_MAP) {
      const char *key_type = tbe_compiler_string_value(field, "key_type");
      const char *value_type = tbe_compiler_string_value(field, "value_type");
      char value_c_type[256] = {0};
      char value_descriptor[256] = {0};
      const char *value_kind = tbe_compiler_typed_named_kind(
          root, value_type ? value_type : inner, value_c_type, sizeof(value_c_type),
          value_descriptor, sizeof(value_descriptor));
      char entry_type[256];
      if (!value_kind || !key_type || strcmp(key_type, "string") != 0) {
        snprintf(declaration, sizeof(declaration), "/* unsupported map field %s */ uint8_t %s;",
                 name, c_name);
        tbe_compiler_set_string(field, "typed_declaration", declaration);
        return;
      }
      snprintf(entry_type, sizeof(entry_type), "%s_%s_entry_t", owner, name);
      snprintf(vector_type, sizeof(vector_type), "%s_%s_vec_t", owner, name);
      snprintf(declaration, sizeof(declaration), "%s %s;", vector_type, c_name);
      tbe_compiler_set_string(field, "typed_kind", "TBE_TYPED_MAP");
      tbe_compiler_set_string(field, "typed_map_entry_type", entry_type);
      tbe_compiler_set_string(field, "typed_map_value_kind", value_kind);
      tbe_compiler_set_string(field, "typed_map_value_wire_kind",
                              tbe_compiler_typed_wire_kind(
                                  root, value_type ? value_type : inner, value_kind));
      tbe_compiler_set_string(field, "typed_map_value_c_type", value_c_type);
      if (value_descriptor[0])
        tbe_compiler_set_string(field, "typed_map_value_descriptor", value_descriptor);
      tbe_compiler_set_string(field, "typed_vector_type", vector_type);
      tbe_compiler_set_string(field, "typed_element_c_type", entry_type);
      tbe_compiler_set_string(field, "typed_needs_map_vector", "1");
    } else {
      snprintf(vector_type, sizeof(vector_type), "%s_%s_vec_t", owner, name);
      snprintf(declaration, sizeof(declaration), "%s %s;", vector_type, c_name);
      tbe_compiler_set_string(field, "typed_kind",
                              semantic->kind == CMETA_DATA_SET ? "TBE_TYPED_SET"
                                                              : "TBE_TYPED_LIST");
      tbe_compiler_set_string(field, "typed_vector_type", vector_type);
      tbe_compiler_set_string(field, "typed_needs_vector", "1");
    }
    tbe_compiler_set_string(field, "typed_declaration", declaration);
    return;
  }
  if (semantic && semantic->kind == CMETA_DATA_STRING) {
    snprintf(declaration, sizeof(declaration), "tstr %s;", c_name);
    tbe_compiler_set_string(field, "typed_kind", "TBE_TYPED_STRING");
    tbe_compiler_set_string(field, "typed_is_var_data", "1");
    tbe_compiler_set_string(field, "typed_declaration", declaration);
    return;
  }
  kind = tbe_compiler_typed_named_kind(root, type, c_type, sizeof(c_type), descriptor,
                                       sizeof(descriptor));
  if (!kind) {
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
      tbe_compiler_set_string(field, "native_c_type", c_type);
    } else if (semantic && salts_uuid_cmeta_data_valid(semantic->data)) {
      tbe_compiler_set_string(field, "native_data_symbol", "salts_uuid_cmeta_data");
      tbe_compiler_set_string(field, "native_type_symbol", "salts_uuid_cmeta_type");
      tbe_compiler_set_string(field, "native_c_type", c_type);
    } else if (strcmp(kind, "TBE_TYPED_OBJECT") == 0 || strcmp(kind, "TBE_TYPED_ENUM") == 0) {
      snprintf(symbol, sizeof(symbol), "%s_CMETA_DATA", type);
      tbe_compiler_set_string(field, "native_data_symbol", symbol);
      snprintf(symbol, sizeof(symbol), "%s_CMETA_TYPE", type);
      tbe_compiler_set_string(field, "native_type_symbol", symbol);
      tbe_compiler_set_string(field, "native_c_type", c_type);
    }
  }
  tbe_compiler_set_string(field, "typed_kind", kind);
  tbe_compiler_set_string(field, "typed_wire_kind",
                          tbe_compiler_typed_wire_kind(root, type, kind));
  if (descriptor[0]) tbe_compiler_set_string(field, "typed_object_descriptor", descriptor);
  tbe_compiler_set_string(field, "typed_declaration", declaration);
}

static void tbe_compiler_annotate_native_requirement(
    Node *root, Node *field, const schema_cmeta_field_type *semantic) {
  const char *type;
  tbe_compiler_native_requirement_t requirement;

  if (!root || !field || !semantic) return;
  type = tbe_compiler_string_value(field, "type");

  if (cmeta_data_kind_is_container(semantic->kind)) {
    requirement = TBE_COMPILER_NATIVE_DEFERRED_CONTAINER;
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

static void tbe_compiler_annotate_field_types(Node *root, Node *field) {
  char type_buf[256];
  char field_name[128];
  const char *name = tbe_compiler_string_value(field, "name");
  schema_cmeta_field_type resolved;
  const schema_cmeta_field_type *semantic =
      schema_cmeta_field_resolve(root, field, &resolved) ? &resolved : NULL;

  if (semantic) {
    snprintf(type_buf, sizeof(type_buf), "%d", (int)semantic->kind);
    tbe_compiler_set_string(field, "cmeta_kind", type_buf);
    tbe_compiler_set_string(field, "cmeta_schema_kind", semantic->schema_kind);
    if (semantic->data)
      tbe_compiler_set_string(field, "cmeta_data_id", semantic->data->stable_id);
  }

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

static void tbe_compiler_annotate_record_list_types(Node *root, const char *list_name) {
  Node *list = tbe_compiler_find_child(root, list_name);
  if (!list || list->type != NODE_LIST) return;

  for (size_t i = 0; i < list->data.list.count; ++i) {
    Node *record = list->data.list.items[i];
    Node *fields = tbe_compiler_find_child(record, "fields");
    if (!fields || fields->type != NODE_LIST) continue;

    for (size_t j = 0; j < fields->data.list.count; ++j) {
      tbe_compiler_annotate_field_types(root, fields->data.list.items[j]);
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
      tbe_compiler_set_string(enum_node, "typed_cmeta_runtime_supported", "1");
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
  size_t count;
  int runtime;
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

static int tbe_compiler_cmeta_classify_record(
    tbe_compiler_cmeta_classify_context_t *context, size_t index) {
  Node *record;
  Node *fields;
  size_t max_depth = 0;
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
    const char *kind = tbe_compiler_string_value(field, "typed_kind");
    const tbe_compiler_scalar_projection_t *scalar;
    Node *target;
    size_t target_index;

    if (!type || !kind ||
        tbe_compiler_has_child(field, "is_optional") ||
        tbe_compiler_has_child(field, "is_collection") ||
        tbe_compiler_has_child(field, "is_list") ||
        tbe_compiler_has_child(field, "is_set") ||
        tbe_compiler_has_child(field, "is_map") ||
        tbe_compiler_has_child(field, "is_group_field"))
      goto unsupported;

    scalar = tbe_compiler_scalar_projection(type);
    if (scalar) {
      if (!scalar->native_data_symbol || !scalar->native_type_symbol)
        goto unsupported;
      if (context->runtime && scalar->data->kind != CMETA_DATA_BOOL &&
          scalar->data->kind != CMETA_DATA_SINT &&
          scalar->data->kind != CMETA_DATA_UINT &&
          scalar->data->kind != CMETA_DATA_FLOAT)
        goto unsupported;
      continue;
    }

    if (context->runtime &&
        tbe_compiler_string_value(field, "cmeta_native_requirement") != NULL &&
        strcmp(tbe_compiler_string_value(field, "cmeta_native_requirement"),
               "fixed_value") == 0 &&
        tbe_compiler_string_value(field, "native_data_symbol") != NULL &&
        tbe_compiler_string_value(field, "native_type_symbol") != NULL &&
        (strcmp(kind, "TBE_TYPED_UUID") == 0 ||
         strcmp(kind, "TBE_TYPED_FIXED_BYTES") == 0))
      continue;

    target = tbe_compiler_find_record(context->root, "enums", type);
    if (target) {
      const char *marker = context->runtime ? "typed_cmeta_runtime_supported"
                                            : "native_enum_supported";
      if (!tbe_compiler_has_child(target, marker))
        goto unsupported;
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
      continue;
    }

    if (!context->runtime &&
        tbe_compiler_string_value(field, "native_data_symbol") != NULL &&
        tbe_compiler_string_value(field, "native_type_symbol") != NULL)
      continue;
    goto unsupported;
  }

  if (tbe_compiler_set_string(
          record, context->runtime ? "typed_cmeta_runtime_supported"
                                   : "cmeta_graph_supported",
          "1") != 0)
    goto unsupported;
  if (context->runtime) {
    for (i = 0; i < fields->data.list.count; ++i) {
      Node *field = fields->data.list.items[i];
      const char *type = tbe_compiler_string_value(field, "type");
      Node *target = tbe_compiler_find_any_record(context->root, type);
      if (tbe_compiler_set_string(field, "typed_cmeta_runtime_supported", "1") != 0)
        goto unsupported;
      if (target) {
        const char *overlay = tbe_compiler_string_value(field, "typed_object_descriptor");
        if (!overlay ||
            tbe_compiler_set_string(field, "typed_nested_overlay", overlay) != 0)
          goto unsupported;
      }
    }
  }
  context->depths[index] = max_depth;
  context->states[index] = TBE_COMPILER_CMETA_SUPPORTED;
  return 1;

unsupported:
  tbe_compiler_remove_children(
      record, context->runtime ? "typed_cmeta_runtime_supported"
                               : "cmeta_graph_supported");
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
    Node *fields = tbe_compiler_find_child(record, "fields");
    size_t j;
    tbe_compiler_remove_children(
        record, context->runtime ? "typed_cmeta_runtime_supported"
                                 : "cmeta_graph_supported");
    if (context->runtime && fields && fields->type == NODE_LIST)
      for (j = 0; j < fields->data.list.count; ++j) {
        tbe_compiler_remove_children(fields->data.list.items[j],
                                     "typed_cmeta_runtime_supported");
        tbe_compiler_remove_children(fields->data.list.items[j],
                                     "typed_nested_overlay");
      }
    context->records[(*offset)++] = record;
  }
}

static void tbe_compiler_annotate_cmeta_support(Node *root, int runtime) {
  static const char *const lists[] = {"composites", "groups", "messages"};
  tbe_compiler_cmeta_classify_context_t context = {0};
  size_t i;
  size_t offset = 0;

  context.root = root;
  context.runtime = runtime;
  for (i = 0; i < sizeof(lists) / sizeof(lists[0]); ++i) {
    Node *list = tbe_compiler_find_child(root, lists[i]);
    if (list && list->type == NODE_LIST) context.count += list->data.list.count;
  }
  if (context.count == 0u) return;
  context.records = (Node **)calloc(context.count, sizeof(*context.records));
  context.states = (unsigned char *)calloc(context.count, sizeof(*context.states));
  context.depths = (size_t *)calloc(context.count, sizeof(*context.depths));
  if (!context.records || !context.states || !context.depths) goto cleanup;

  for (i = 0; i < sizeof(lists) / sizeof(lists[0]); ++i)
    tbe_compiler_collect_cmeta_records(&context, lists[i], &offset);
  for (i = 0; i < context.count; ++i)
    (void)tbe_compiler_cmeta_classify_record(&context, i);

cleanup:
  free(context.depths);
  free(context.states);
  free(context.records);
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

void tbe_compiler_annotate_language_types(Node *root) {
  tbe_compiler_annotate_schema_types(root);
  tbe_compiler_annotate_enum_types(root);
  tbe_compiler_annotate_record_list_types(root, "composites");
  tbe_compiler_annotate_record_list_types(root, "groups");
  tbe_compiler_annotate_record_list_types(root, "messages");
  tbe_compiler_annotate_record_list_types(root, "unions");
  tbe_compiler_annotate_cmeta_support(root, 1);
  tbe_compiler_annotate_cmeta_support(root, 0);
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

static int tbe_compiler_typed_list_supported(Node *root, const char *list_name) {
  Node *list = tbe_compiler_find_child(root, list_name);
  size_t i;
  if (!list || list->type != NODE_LIST) return 1;
  for (i = 0; i < list->data.list.count; ++i) {
    Node *record = list->data.list.items[i];
    Node *fields = tbe_compiler_find_child(record, "fields");
    size_t j;
    if (!fields || fields->type != NODE_LIST) continue;
    for (j = 0; j < fields->data.list.count; ++j) {
      Node *field = fields->data.list.items[j];
      const char *c_name = tbe_compiler_string_value(field, "c_name");
      size_t k;
      if (!tbe_compiler_string_value(field, "typed_kind")) {
        fprintf(stderr, "Typed C serde does not support field %s.%s of type %s\n",
                tbe_compiler_string_value(field, "owner_name"),
                tbe_compiler_string_value(field, "name"),
                tbe_compiler_string_value(field, "type"));
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
      if (c_name && strcmp(c_name, "_presence") == 0) {
        fprintf(stderr, "Typed C field %s.%s uses reserved member name _presence\n",
                tbe_compiler_string_value(field, "owner_name"),
                tbe_compiler_string_value(field, "name"));
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

static Node *tbe_compiler_find_typed_record(Node *root, const char *name) {
  Node *record = tbe_compiler_find_record(root, "composites", name);
  if (!record) record = tbe_compiler_find_record(root, "groups", name);
  if (!record) record = tbe_compiler_find_record(root, "messages", name);
  return record;
}

static int tbe_compiler_prepare_lua_operations(Node *root) {
  Node *messages = tbe_compiler_find_child(root, "messages");
  size_t operation_count = 0;
  size_t import_count = 0;
  size_t i;

  if (!messages || messages->type != NODE_LIST) return 1;

  for (i = 0; i < messages->data.list.count; ++i) {
    Node *request = messages->data.list.items[i];
    const size_t operation_name_count =
        tbe_compiler_attribute_count(request, "lua_operation");
    const size_t import_name_count =
        tbe_compiler_attribute_count(request, "lua_import");
    const size_t response_count = tbe_compiler_attribute_count(request, "lua_response");
    const size_t async_count = tbe_compiler_attribute_count(request, "lua_async");
    const char *name_attribute;
    const char *binding_kind;
    const char *binding_name;
    const char *response_type;
    const char *async_value;
    int is_import;
    size_t previous;

    if (operation_name_count == 0u && import_name_count == 0u &&
        response_count == 0u && async_count == 0u) {
      continue;
    }
    if (import_name_count != 0u) {
      fprintf(stderr,
              "Lua import message %s is unsupported; bind Lua coroutines "
              "through the C11 Lua API\n",
              tbe_compiler_string_value(request, "name"));
      return 0;
    }
    if (operation_name_count != 0u && import_name_count != 0u) {
      fprintf(stderr,
              "Lua binding message %s cannot declare both lua_operation and "
              "lua_import\n",
              tbe_compiler_string_value(request, "name"));
      return 0;
    }
    is_import = import_name_count != 0u;
    name_attribute = is_import ? "lua_import" : "lua_operation";
    binding_kind = is_import ? "import" : "operation";
    if ((is_import ? import_name_count : operation_name_count) != 1u ||
        response_count != 1u) {
      fprintf(stderr,
              "Lua %s message %s requires exactly one %s and one lua_response "
              "attribute\n",
              binding_kind, tbe_compiler_string_value(request, "name"),
              name_attribute);
      return 0;
    }
    if (async_count > 1u) {
      fprintf(stderr, "Lua %s message %s allows at most one lua_async attribute\n",
              binding_kind, tbe_compiler_string_value(request, "name"));
      return 0;
    }

    binding_name = tbe_compiler_attribute_value(request, name_attribute);
    response_type = tbe_compiler_attribute_value(request, "lua_response");
    async_value = tbe_compiler_attribute_value(request, "lua_async");
    if (!tbe_compiler_c_identifier_valid(binding_name)) {
      fprintf(stderr, "Lua %s %s uses an invalid C/Lua identifier\n", binding_kind,
              binding_name ? binding_name : "(null)");
      return 0;
    }
    if (!tbe_compiler_find_typed_record(root, response_type)) {
      fprintf(stderr, "Lua %s %s references unknown response type %s\n",
              binding_kind, binding_name,
              response_type ? response_type : "(null)");
      return 0;
    }

    for (previous = 0; previous < i; ++previous) {
      Node *other = messages->data.list.items[previous];
      const char *other_name =
          tbe_compiler_attribute_value(other, name_attribute);
      if (other_name && strcmp(binding_name, other_name) == 0) {
        fprintf(stderr, "Lua %s name %s is declared more than once\n",
                binding_kind, binding_name);
        return 0;
      }
    }

    if (async_count == 1u &&
        (!async_value || strcmp(async_value, "future") != 0)) {
      fprintf(stderr,
              "Lua %s %s requires lua_async(future) when asynchronous\n",
              binding_kind, binding_name);
      return 0;
    }

    if (is_import) {
      if (tbe_compiler_set_string(request, "lua_import_enabled", "1") != 0 ||
          tbe_compiler_set_string(request, "lua_import_name", binding_name) != 0 ||
          tbe_compiler_set_string(request, "lua_response_type", response_type) != 0) {
        return 0;
      }
      if (async_count == 1u &&
          (tbe_compiler_set_string(request, "lua_async_enabled", "1") != 0 ||
           tbe_compiler_set_string(root, "has_lua_async_imports", "1") != 0)) {
        return 0;
      }
      ++import_count;
    } else {
      if (tbe_compiler_set_string(request, "lua_operation_enabled", "1") != 0 ||
          tbe_compiler_set_string(request, "lua_operation_name", binding_name) != 0 ||
          tbe_compiler_set_string(request, "lua_response_type", response_type) != 0) {
        return 0;
      }
      if (async_count == 1u &&
          (tbe_compiler_set_string(request, "lua_async_enabled", "1") != 0 ||
           tbe_compiler_set_string(root, "has_lua_async_operations", "1") != 0)) {
        return 0;
      }
      ++operation_count;
    }
  }

  if (operation_count != 0u &&
      tbe_compiler_set_string(root, "has_lua_operations", "1") != 0) {
    return 0;
  }
  if (import_count != 0u &&
      tbe_compiler_set_string(root, "has_lua_imports", "1") != 0) {
    return 0;
  }
  if ((operation_count != 0u || import_count != 0u) &&
      tbe_compiler_set_string(root, "has_lua_bindings", "1") != 0) {
    return 0;
  }
  return 1;
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
          lang_name, "--lua-output", options->lua_output_path) ||
      !tbe_compiler_validate_database_output_option(
          lang_name, "--dsl-output", options->dsl_output_path)) {
    return 0;
  }
  return 1;
}

int tbe_compiler_parse_schema_file(const char *schema_path, Node **out_root,
                                   char **out_schema_data) {
  tbe_error_t parse_err;
  Node *root = NULL;
  char *schema_data = tbe_compiler_read_file(schema_path);
  if (!schema_data) {
    fprintf(stderr, "Failed to read schema file: %s\n", schema_path);
    return 1;
  }

  root = create_node_map(NULL);
  if (!root) {
    fprintf(stderr, "Failed to allocate root node\n");
    free(schema_data);
    return 1;
  }

  if (parse_schema(schema_data, strlen(schema_data), root, &parse_err) != 0) {
    if (parse_err.line >= 0) {
      fprintf(stderr, "Parse error at line %d: %s\n", parse_err.line,
              parse_err.message);
    } else {
      fprintf(stderr, "Parse error: %s\n", parse_err.message);
    }
    free(schema_data);
    node_free(root);
    return 1;
  }

  tbe_compiler_annotate_language_types(root);

  *out_root = root;
  *out_schema_data = schema_data;
  return 0;
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
static int tbe_compiler_validate_enum_backend(Node *root,
                                             const tbe_compiler_options_t *options) {
  Node *enums = tbe_compiler_find_child(root, "enums");
  if (!enums || enums->type != NODE_LIST) return 1;
  for (size_t i = 0; i < enums->data.list.count; ++i) {
    Node *node = enums->data.list.items[i];
    const char *storage = tbe_compiler_string_value(node, "underlying_type");
    const char *name = tbe_compiler_string_value(node, "enum_name");
    if (!storage || !tbe_compiler_integer_type(storage)) {
      fprintf(stderr, "Missing canonical enum storage for %s\n", name ? name : "<unnamed>");
      return 0;
    }
    if (options->lang_enum == TBE_COMPILER_LANG_TS &&
        (strcmp(storage, "int64") == 0 || strcmp(storage, "uint64") == 0)) {
      fprintf(stderr, "TypeScript numeric enum %s cannot preserve %s storage\n", name, storage);
      return 0;
    }
    if (options->dsl_output_path && !tbe_compiler_has_child(node, "is_ordinal")) {
      fprintf(stderr, "RulesForge enum %s requires sequential values starting at zero\n", name);
      return 0;
    }
  }
  return 1;
}

int tbe_compiler_run(const tbe_compiler_options_t *options) {
  Node *root = NULL;
  Node *database_ir = NULL;
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
  int status = tbe_compiler_parse_schema_file(options->schema_path, &root,
                                              &schema_data);
  if (status != 0) return status;
  if (!tbe_compiler_validate_enum_backend(root, options)) {
    status = 1;
    goto cleanup;
  }

  if (database_language) {
    tbe_database_schema_diagnostic_t diagnostic;
    tbe_database_dialect_t dialect = options->lang_enum == TBE_COMPILER_LANG_SQLITE
                                         ? TBE_DATABASE_DIALECT_SQLITE
                                         : TBE_DATABASE_DIALECT_POSTGRESQL;
    tbe_database_schema_status_t database_status =
        tbe_database_schema_build(root, dialect, &database_ir, &diagnostic);
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

  if (options->lua_output_path) {
    if (options->output_path == NULL || options->output_path[0] == '\0') {
      fprintf(stderr, "--lua-output requires --output for the generated header\n");
      status = 1;
      goto cleanup;
    }
    if (options->source_output_path == NULL || options->source_output_path[0] == '\0') {
      fprintf(stderr, "--lua-output requires --source-output for typed metadata\n");
      status = 1;
      goto cleanup;
    }
    if (strcmp(options->output_path, options->lua_output_path) == 0 ||
        strcmp(options->source_output_path, options->lua_output_path) == 0 ||
        (options->guest_output_path != NULL &&
         strcmp(options->guest_output_path, options->lua_output_path) == 0) ||
        (options->dsl_output_path != NULL &&
         strcmp(options->dsl_output_path, options->lua_output_path) == 0)) {
      fprintf(stderr, "--lua-output must name a distinct file\n");
      status = 1;
      goto cleanup;
    }
    if (options->lang_enum != TBE_COMPILER_LANG_C || options->template_path != NULL) {
      fprintf(stderr, "--lua-output is supported only for the built-in C generator\n");
      status = 1;
      goto cleanup;
    }
    if (tbe_compiler_set_string(root, "lua_output_enabled", "1") != 0) {
      status = 1;
      goto cleanup;
    }
    if (!tbe_compiler_prepare_lua_operations(root)) {
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
  }

  if (status == 0 && options->guest_output_path) {
    resolved_template = tbe_compiler_resolve_resource(
        options, "templates/c_guest_adapter.mustache", template_path, sizeof(template_path));
    status = resolved_template != NULL
                 ? tbe_compiler_render_file(root, resolved_template, options->guest_output_path)
                 : 1;
  }

  if (status == 0 && options->lua_output_path) {
    resolved_template = tbe_compiler_resolve_resource(
        options, "templates/c_lua_bind.mustache", template_path, sizeof(template_path));
    status = resolved_template != NULL
                 ? tbe_compiler_render_file(root, resolved_template, options->lua_output_path)
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

cleanup:
  free(schema_data);
  tbe_database_schema_destroy(database_ir);
  node_free(root);
  return status;
}
