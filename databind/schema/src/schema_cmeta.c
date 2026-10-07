#include "schema_cmeta.h"

#include <cmeta_cmeta_data.h>
#include <cmeta/data.h>
#include <cmeta/pp.h>

#include <string.h>

typedef struct schema_cmeta_builtin_entry {
  const char *name;
  const cmeta_data_desc *data;
} schema_cmeta_builtin_entry_t;

typedef struct schema_cmeta_kind_entry {
  const char *name;
  cmeta_data_kind kind;
} schema_cmeta_kind_entry_t;

#define SCHEMA_CMETA_SIGNED_ALIASES(M) \
  Schema(M, \
    ("int8_t", cmeta_data_int8), \
    ("int8", cmeta_data_int8), \
    ("i8", cmeta_data_int8), \
    ("int16_t", cmeta_data_int16), \
    ("int16", cmeta_data_int16), \
    ("i16", cmeta_data_int16), \
    ("int32_t", cmeta_data_int32), \
    ("int32", cmeta_data_int32), \
    ("i32", cmeta_data_int32), \
    ("int64_t", cmeta_data_int64), \
    ("int64", cmeta_data_int64), \
    ("i64", cmeta_data_int64))

#define SCHEMA_CMETA_UNSIGNED_ALIASES(M) \
  Schema(M, \
    ("uint8_t", cmeta_data_uint8), \
    ("uint8", cmeta_data_uint8), \
    ("u8", cmeta_data_uint8), \
    ("byte", cmeta_data_uint8), \
    ("uint16_t", cmeta_data_uint16), \
    ("uint16", cmeta_data_uint16), \
    ("u16", cmeta_data_uint16), \
    ("uint32_t", cmeta_data_uint32), \
    ("uint32", cmeta_data_uint32), \
    ("u32", cmeta_data_uint32), \
    ("uint64_t", cmeta_data_uint64), \
    ("uint64", cmeta_data_uint64), \
    ("u64", cmeta_data_uint64))

#define SCHEMA_CMETA_SCALAR_ALIASES(M) \
  Schema(M, \
    ("bool", cmeta_data_bool), \
    ("float", cmeta_data_float), \
    ("f32", cmeta_data_float), \
    ("double", cmeta_data_double), \
    ("f64", cmeta_data_double), \
    ("uuid", cmeta_uuid_cmeta_data))

#define SCHEMA_CMETA_BUILTINS(M) \
  Replay(SCHEMA_CMETA_SIGNED_ALIASES, M) \
  Replay(SCHEMA_CMETA_UNSIGNED_ALIASES, M) \
  Replay(SCHEMA_CMETA_SCALAR_ALIASES, M)
#define SCHEMA_CMETA_COUNT(name_, data_) + 1u
enum { SCHEMA_CMETA_BUILTIN_COUNT = 0u SCHEMA_CMETA_BUILTINS(SCHEMA_CMETA_COUNT) };
#undef SCHEMA_CMETA_COUNT

/* MSVC C cannot use DLL-imported object addresses in file-scope initializers. */
static const schema_cmeta_builtin_entry_t *schema_cmeta_builtins(size_t *count) {
  static _Thread_local schema_cmeta_builtin_entry_t builtins[SCHEMA_CMETA_BUILTIN_COUNT];
  static _Thread_local int initialized;
  if (!initialized) {
    size_t index = 0u;
#define SCHEMA_CMETA_ENTRY(name_, data_) \
    builtins[index++] = (schema_cmeta_builtin_entry_t){name_, &(data_)};
    SCHEMA_CMETA_BUILTINS(SCHEMA_CMETA_ENTRY)
#undef SCHEMA_CMETA_ENTRY
    initialized = 1;
  }
  *count = SCHEMA_CMETA_BUILTIN_COUNT;
  return builtins;
}

#undef SCHEMA_CMETA_BUILTINS
#undef SCHEMA_CMETA_SIGNED_ALIASES
#undef SCHEMA_CMETA_UNSIGNED_ALIASES
#undef SCHEMA_CMETA_SCALAR_ALIASES

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

static const char *schema_cmeta_declared_semantic(
    const IdlContract *contract, const char *name) {
  const IdlDataDecl *decl = NULL;
  size_t i;
  if (contract == NULL || name == NULL) return NULL;
  for (i = 0u; i < contract->data_count; ++i) {
    if (contract->data[i].name != NULL &&
        strcmp(contract->data[i].name, name) == 0) {
      decl = &contract->data[i];
      break;
    }
  }
  if (decl == NULL) return NULL;
  switch (decl->kind) {
  case IDL_DATA_MESSAGE: return "message";
  case IDL_DATA_COMPOSITE: return "composite";
  case IDL_DATA_GROUP: return "group";
  case IDL_DATA_ENUM: return decl->flags ? "flags" : "enum";
  case IDL_DATA_UNION: return "union";
  default: return NULL;
  }
}

int schema_cmeta_field_resolve(
    const IdlContract *contract, const IdlField *field,
    schema_cmeta_field_type *out) {
  schema_cmeta_field_type result;
  const char *semantic;
  const char *sequence_label = "list";

  if (field == NULL || field->name == NULL ||
      field->type_name == NULL || out == NULL)
    return 0;

  semantic = schema_cmeta_declared_semantic(contract, field->type_name);
  if (semantic == NULL) semantic = field->type_name;

  switch (field->collection_kind) {
  case IDL_COLLECTION_GROUP:
    semantic = "list";
    sequence_label = "group";
    break;
  case IDL_COLLECTION_ARRAY:
    semantic = "list";
    sequence_label = "array";
    break;
  case IDL_COLLECTION_LIST:
    semantic = "list";
    sequence_label = "list";
    break;
  case IDL_COLLECTION_SET:
    semantic = "set";
    break;
  case IDL_COLLECTION_MAP:
    semantic = "map";
    break;
  case IDL_COLLECTION_NONE:
  default:
    break;
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
    result.schema_kind =
        strcmp(semantic, "composite") == 0 ? "composite" :
        strcmp(semantic, "group") == 0 ? "group" : "message";
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
