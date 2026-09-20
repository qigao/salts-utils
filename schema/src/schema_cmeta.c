#include "schema_cmeta.h"

#include <salts_cmeta_data.h>
#include <salts_cmeta_fixed_width.h>

#include <string.h>

typedef struct schema_cmeta_builtin_entry {
  const char *name;
  const cmeta_data_desc *data;
} schema_cmeta_builtin_entry_t;

typedef struct schema_cmeta_kind_entry {
  const char *name;
  cmeta_data_kind kind;
} schema_cmeta_kind_entry_t;

#define SCHEMA_CMETA_ENTRY(name_, data_) ((schema_cmeta_builtin_entry_t){name_, &(data_)})

/* MSVC C cannot use DLL-imported object addresses in file-scope initializers. */
static const schema_cmeta_builtin_entry_t *schema_cmeta_builtins(size_t *count) {
  static _Thread_local schema_cmeta_builtin_entry_t builtins[32];
  static _Thread_local int initialized;
  if (!initialized) {
    builtins[0] = SCHEMA_CMETA_ENTRY("bool", cmeta_data_bool);
    builtins[1] = SCHEMA_CMETA_ENTRY("int8_t", salts_int8_cmeta_data);
    builtins[2] = SCHEMA_CMETA_ENTRY("int8", salts_int8_cmeta_data);
    builtins[3] = SCHEMA_CMETA_ENTRY("i8", salts_int8_cmeta_data);
    builtins[4] = SCHEMA_CMETA_ENTRY("int16_t", salts_int16_cmeta_data);
    builtins[5] = SCHEMA_CMETA_ENTRY("int16", salts_int16_cmeta_data);
    builtins[6] = SCHEMA_CMETA_ENTRY("i16", salts_int16_cmeta_data);
    builtins[7] = SCHEMA_CMETA_ENTRY("int32_t", salts_int32_cmeta_data);
    builtins[8] = SCHEMA_CMETA_ENTRY("int32", salts_int32_cmeta_data);
    builtins[9] = SCHEMA_CMETA_ENTRY("i32", salts_int32_cmeta_data);
    builtins[10] = SCHEMA_CMETA_ENTRY("int64_t", salts_int64_cmeta_data);
    builtins[11] = SCHEMA_CMETA_ENTRY("int64", salts_int64_cmeta_data);
    builtins[12] = SCHEMA_CMETA_ENTRY("i64", salts_int64_cmeta_data);
    builtins[13] = SCHEMA_CMETA_ENTRY("uint8_t", salts_uint8_cmeta_data);
    builtins[14] = SCHEMA_CMETA_ENTRY("uint8", salts_uint8_cmeta_data);
    builtins[15] = SCHEMA_CMETA_ENTRY("u8", salts_uint8_cmeta_data);
    builtins[16] = SCHEMA_CMETA_ENTRY("byte", salts_uint8_cmeta_data);
    builtins[17] = SCHEMA_CMETA_ENTRY("uint16_t", salts_uint16_cmeta_data);
    builtins[18] = SCHEMA_CMETA_ENTRY("uint16", salts_uint16_cmeta_data);
    builtins[19] = SCHEMA_CMETA_ENTRY("u16", salts_uint16_cmeta_data);
    builtins[20] = SCHEMA_CMETA_ENTRY("uint32_t", salts_uint32_cmeta_data);
    builtins[21] = SCHEMA_CMETA_ENTRY("uint32", salts_uint32_cmeta_data);
    builtins[22] = SCHEMA_CMETA_ENTRY("u32", salts_uint32_cmeta_data);
    builtins[23] = SCHEMA_CMETA_ENTRY("uint64_t", salts_uint64_cmeta_data);
    builtins[24] = SCHEMA_CMETA_ENTRY("uint64", salts_uint64_cmeta_data);
    builtins[25] = SCHEMA_CMETA_ENTRY("u64", salts_uint64_cmeta_data);
    builtins[26] = SCHEMA_CMETA_ENTRY("float", cmeta_data_float);
    builtins[27] = SCHEMA_CMETA_ENTRY("f32", cmeta_data_float);
    builtins[28] = SCHEMA_CMETA_ENTRY("double", cmeta_data_double);
    builtins[29] = SCHEMA_CMETA_ENTRY("f64", cmeta_data_double);
    builtins[30] = SCHEMA_CMETA_ENTRY("uuid", salts_uuid_cmeta_data);
    initialized = 1;
  }
  *count = 31u;
  return builtins;
}

#undef SCHEMA_CMETA_ENTRY

static const schema_cmeta_kind_entry_t SCHEMA_CMETA_KINDS[] = {
    {"string", CMETA_DATA_STRING},   {"bytes", CMETA_DATA_BYTES},
    {"uuid", CMETA_DATA_CUSTOM},     {"datetime", CMETA_DATA_CUSTOM},
    {"date", CMETA_DATA_CUSTOM},     {"time", CMETA_DATA_CUSTOM},
    {"duration", CMETA_DATA_CUSTOM}, {"decimal", CMETA_DATA_CUSTOM},
    {"bigint", CMETA_DATA_CUSTOM},   {"money", CMETA_DATA_CUSTOM},
    {"message", CMETA_DATA_STRUCT},  {"composite", CMETA_DATA_STRUCT},
    {"group", CMETA_DATA_STRUCT},    {"enum", CMETA_DATA_ENUM},
    {"flags", CMETA_DATA_ENUM},      {"union", CMETA_DATA_VARIANT},
    {"list", CMETA_DATA_SEQUENCE},   {"set", CMETA_DATA_SET},
    {"map", CMETA_DATA_MAP},
};

static int schema_cmeta_nonempty(const char *text) { return text != NULL && text[0] != '\0'; }

static bool schema_cmeta_enum_is_zero(const void *object) {
  int32_t value = 0;
  return object != NULL && memcmp(object, &value, sizeof(value)) == 0;
}

static cmeta_status schema_cmeta_enum_read(const void *object, int64_t *out) {
  int32_t value;
  if (object == NULL || out == NULL) return CMETA_INVALID_ARGUMENT;
  memcpy(&value, object, sizeof(value));
  *out = value;
  return CMETA_OK;
}

static cmeta_status schema_cmeta_enum_assign(void *object, int64_t value) {
  int32_t narrowed;
  if (object == NULL || value < INT32_MIN || value > INT32_MAX)
    return CMETA_INVALID_ARGUMENT;
  narrowed = (int32_t)value;
  memcpy(object, &narrowed, sizeof(narrowed));
  return CMETA_OK;
}

static void schema_cmeta_enum_restore_zero(void *object) {
  int32_t value = 0;
  if (object != NULL) memcpy(object, &value, sizeof(value));
}

const cmeta_data_desc *schema_cmeta_builtin_data(const char *name) {
  const schema_cmeta_builtin_entry_t *builtins;
  size_t builtin_count;
  size_t i;

  builtins = schema_cmeta_builtins(&builtin_count);

  if (name == NULL) return NULL;
  for (i = 0; i < builtin_count; ++i) {
    if (strcmp(name, builtins[i].name) == 0) return builtins[i].data;
  }
  return NULL;
}

int schema_cmeta_data_kind(const char *semantic, cmeta_data_kind *out_kind) {
  size_t i;
  const cmeta_data_desc *data;

  if (semantic == NULL || out_kind == NULL) return 0;
  for (i = 0; i < sizeof(SCHEMA_CMETA_KINDS) / sizeof(SCHEMA_CMETA_KINDS[0]); ++i) {
    if (strcmp(semantic, SCHEMA_CMETA_KINDS[i].name) == 0) {
      *out_kind = SCHEMA_CMETA_KINDS[i].kind;
      return 1;
    }
  }
  /* Domain/wire semantics above are distinct from adapter kind (UUID).
   * All ordinary scalar aliases derive kind from their canonical descriptor. */
  data = schema_cmeta_builtin_data(semantic);
  if (data != NULL) {
    *out_kind = data->kind;
    return 1;
  }
  return 0;
}

static const Node *schema_cmeta_child(const Node *map, const char *name) {
  size_t i;
  if (map == NULL || map->type != NODE_MAP) return NULL;
  for (i = 0; i < map->data.map.count; ++i) {
    const Node *child = map->data.map.items[i];
    if (child != NULL && child->name != NULL && strcmp(child->name, name) == 0) return child;
  }
  return NULL;
}

static const char *schema_cmeta_text(const Node *map, const char *name) {
  const Node *node = schema_cmeta_child(map, name);
  return node != NULL && node->type == NODE_STRING ? node->data.string_val : NULL;
}

static int schema_cmeta_flag(const Node *field, const char *name) {
  const char *value = schema_cmeta_text(field, name);
  return value != NULL && strcmp(value, "1") == 0;
}

static const char *schema_cmeta_named_semantic(const Node *root, const char *name) {
  static const struct {
    const char *list;
    const char *semantic;
  } declarations[] = {{"composites", "composite"},
                      {"messages", "message"},
                      {"groups", "group"},
                      {"enums", "enum"},
                      {"unions", "union"}};
  size_t i, j;
  if (name == NULL) return NULL;
  for (i = 0; i < sizeof(declarations) / sizeof(declarations[0]); ++i) {
    const Node *list = schema_cmeta_child(root, declarations[i].list);
    if (list == NULL || list->type != NODE_LIST) continue;
    for (j = 0; j < list->data.list.count; ++j) {
      const char *candidate = schema_cmeta_text(list->data.list.items[j], "name");
      if (candidate != NULL && strcmp(candidate, name) == 0) return declarations[i].semantic;
    }
  }
  return NULL;
}

int schema_cmeta_field_resolve(const Node *root, const Node *field, schema_cmeta_field_type *out) {
  schema_cmeta_field_type result;
  const char *semantic, *declared;
  const char *sequence_label = "list";
  if (root == NULL || root->type != NODE_MAP || field == NULL || field->type != NODE_MAP ||
      out == NULL)
    return 0;
  declared = schema_cmeta_text(field, "type");
  semantic = schema_cmeta_named_semantic(root, declared);
  if (semantic == NULL) semantic = declared;
  if (schema_cmeta_flag(field, "is_group_field")) {
    semantic = "list";
    sequence_label = "group";
  } else if (schema_cmeta_flag(field, "is_map")) semantic = "map";
  else if (schema_cmeta_flag(field, "is_set")) semantic = "set";
  else if (schema_cmeta_flag(field, "is_list")) semantic = "list";
  else if (schema_cmeta_flag(field, "is_collection")) {
    semantic = "list";
    sequence_label = "array";
  }
  if (!schema_cmeta_data_kind(semantic, &result.kind)) return 0;
  result.data = schema_cmeta_builtin_data(semantic);
  switch (result.kind) {
  case CMETA_DATA_BOOL:
  case CMETA_DATA_SINT:
  case CMETA_DATA_UINT:
  case CMETA_DATA_FLOAT:
    result.schema_kind = "scalar";
    break;
  case CMETA_DATA_SEQUENCE:
    result.data = &cmeta_data_sequence;
    result.schema_kind = sequence_label;
    break;
  case CMETA_DATA_SET:
    result.data = &cmeta_data_set;
    result.schema_kind = "set";
    break;
  case CMETA_DATA_MAP:
    result.data = &cmeta_data_map;
    result.schema_kind = "map";
    break;
  case CMETA_DATA_STRING:
    result.schema_kind = "string";
    break;
  case CMETA_DATA_BYTES:
    result.schema_kind = "bytes";
    break;
  case CMETA_DATA_ENUM:
    result.schema_kind = "enum";
    break;
  case CMETA_DATA_VARIANT:
    result.schema_kind = "union";
    break;
  case CMETA_DATA_STRUCT:
    result.schema_kind = strcmp(semantic, "composite") == 0 ? "composite"
                         : strcmp(semantic, "group") == 0   ? "group"
                                                            : "message";
    break;
  case CMETA_DATA_CUSTOM:
    result.schema_kind = "custom";
    break;
  default:
    return 0;
  }
  *out = result;
  return 1;
}

int schema_cmeta_generic_identity(cmeta_type_identity *out_identity,
                                  const cmeta_generic_desc *constructor,
                                  const cmeta_type_identity *const *args, size_t arity) {
  cmeta_type_identity identity;

  if (out_identity == NULL || !cmeta_type_application_valid(constructor, args, arity)) return 0;

  identity.form = CMETA_TYPE_APPLY;
  identity.stable_atom_id = NULL;
  identity.constructor = constructor;
  identity.base = NULL;
  identity.args = args;
  identity.arity = arity;

  if (!cmeta_type_identity_valid(&identity)) return 0;

  *out_identity = identity;
  return 1;
}

int schema_cmeta_struct_data(cmeta_data_desc *out_data, cmeta_data_struct_shape *out_shape,
                             const char *stable_id, const char *display_name,
                             const cmeta_type_desc *storage_type, const cmeta_struct_desc *layout,
                             const cmeta_data_field_desc *fields, size_t field_count) {
  cmeta_data_struct_shape shape = {0};
  cmeta_data_desc data = {0};

  if (out_data == NULL || out_shape == NULL || !schema_cmeta_nonempty(stable_id) ||
      !schema_cmeta_nonempty(display_name) || storage_type == NULL || layout == NULL)
    return 0;

  shape.layout = layout;
  shape.fields = fields;
  shape.field_count = field_count;
  data.struct_size = sizeof(data);
  data.abi_version = CMETA_DATA_DESC_ABI_VERSION;
  data.stable_id = stable_id;
  data.display_name = display_name;
  data.kind = CMETA_DATA_STRUCT;
  data.storage_type = storage_type;
  data.shape = &shape;
  data.buffer_ops = NULL;
  data.enum_ops = NULL;
  data.variant_ops = NULL;

  if (!cmeta_data_desc_valid(&data)) return 0;

  *out_shape = shape;
  data.shape = out_shape;
  *out_data = data;
  return 1;
}

int schema_cmeta_enum_data(cmeta_data_desc *out_data, cmeta_data_enum_shape *out_shape,
                           const char *stable_id, const char *display_name,
                           const cmeta_type_desc *storage_type, const cmeta_enum_desc *meta) {
  static _Thread_local cmeta_data_enum_ops enum_ops;
  cmeta_data_enum_shape shape = {0};
  cmeta_data_desc data = {0};

  if (out_data == NULL || out_shape == NULL || !schema_cmeta_nonempty(stable_id) ||
      !schema_cmeta_nonempty(display_name) || storage_type == NULL || meta == NULL)
    return 0;

  enum_ops = (cmeta_data_enum_ops){
      sizeof(cmeta_data_enum_ops), CMETA_DATA_ENUM_OPS_ABI_VERSION,
      storage_type, schema_cmeta_enum_is_zero, schema_cmeta_enum_read,
      schema_cmeta_enum_assign, schema_cmeta_enum_restore_zero};
  shape.meta = meta;
  data.struct_size = sizeof(data);
  data.abi_version = CMETA_DATA_DESC_ABI_VERSION;
  data.stable_id = stable_id;
  data.display_name = display_name;
  data.kind = CMETA_DATA_ENUM;
  data.storage_type = storage_type;
  data.shape = &shape;
  data.buffer_ops = NULL;
  data.enum_ops = &enum_ops;
  data.variant_ops = NULL;

  if (!cmeta_data_desc_valid(&data)) return 0;

  *out_shape = shape;
  data.shape = out_shape;
  *out_data = data;
  return 1;
}
