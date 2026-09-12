#include "schema_cmeta.h"

#include <salts_cmeta_data.h>
#include <salts_cmeta_fixed_width.h>
#include <string.h>

typedef struct SchemaCMetaBuiltin {
  const char *name;
  const cmeta_data_desc *data;
} SchemaCMetaBuiltin;

const cmeta_data_desc *schema_cmeta_builtin_data(const char *name) {
  static const SchemaCMetaBuiltin builtins[] = {
    {"bool", &cmeta_data_bool},
    {"int8", &salts_int8_cmeta_data}, {"i8", &salts_int8_cmeta_data},
    {"int16", &salts_int16_cmeta_data}, {"i16", &salts_int16_cmeta_data},
    {"int32", &salts_int32_cmeta_data}, {"i32", &salts_int32_cmeta_data},
    {"int64", &salts_int64_cmeta_data}, {"i64", &salts_int64_cmeta_data},
    {"uint8", &salts_uint8_cmeta_data}, {"u8", &salts_uint8_cmeta_data},
    {"byte", &salts_uint8_cmeta_data},
    {"uint16", &salts_uint16_cmeta_data}, {"u16", &salts_uint16_cmeta_data},
    {"uint32", &salts_uint32_cmeta_data}, {"u32", &salts_uint32_cmeta_data},
    {"uint64", &salts_uint64_cmeta_data}, {"u64", &salts_uint64_cmeta_data},
    {"float", &cmeta_data_float}, {"f32", &cmeta_data_float},
    {"double", &cmeta_data_double}, {"f64", &cmeta_data_double},
    {"uuid", &salts_uuid_cmeta_data}
  };
  size_t i;

  if (name == NULL) return NULL;
  for (i = 0; i < sizeof(builtins) / sizeof(builtins[0]); ++i) {
    if (strcmp(name, builtins[i].name) == 0) return builtins[i].data;
  }
  return NULL;
}

int schema_cmeta_data_kind(const char *semantic, cmeta_data_kind *out_kind) {
  static const struct { const char *name; cmeta_data_kind kind; } kinds[] = {
    {"bool", CMETA_DATA_BOOL},
    {"int8", CMETA_DATA_SINT}, {"i8", CMETA_DATA_SINT},
    {"int16", CMETA_DATA_SINT}, {"i16", CMETA_DATA_SINT},
    {"int32", CMETA_DATA_SINT}, {"i32", CMETA_DATA_SINT},
    {"int64", CMETA_DATA_SINT}, {"i64", CMETA_DATA_SINT},
    {"uint8", CMETA_DATA_UINT}, {"u8", CMETA_DATA_UINT}, {"byte", CMETA_DATA_UINT},
    {"uint16", CMETA_DATA_UINT}, {"u16", CMETA_DATA_UINT},
    {"uint32", CMETA_DATA_UINT}, {"u32", CMETA_DATA_UINT},
    {"uint64", CMETA_DATA_UINT}, {"u64", CMETA_DATA_UINT},
    {"float", CMETA_DATA_FLOAT}, {"f32", CMETA_DATA_FLOAT},
    {"double", CMETA_DATA_FLOAT}, {"f64", CMETA_DATA_FLOAT},
    {"string", CMETA_DATA_STRING}, {"bytes", CMETA_DATA_BYTES},
    {"uuid", CMETA_DATA_CUSTOM},
    {"datetime", CMETA_DATA_CUSTOM}, {"date", CMETA_DATA_CUSTOM},
    {"time", CMETA_DATA_CUSTOM}, {"duration", CMETA_DATA_CUSTOM},
    {"decimal", CMETA_DATA_CUSTOM}, {"bigint", CMETA_DATA_CUSTOM},
    {"money", CMETA_DATA_CUSTOM},
    {"message", CMETA_DATA_STRUCT}, {"composite", CMETA_DATA_STRUCT},
    {"group", CMETA_DATA_STRUCT},
    {"enum", CMETA_DATA_ENUM}, {"flags", CMETA_DATA_ENUM},
    {"union", CMETA_DATA_VARIANT},
    {"list", CMETA_DATA_SEQUENCE}, {"set", CMETA_DATA_SET},
    {"map", CMETA_DATA_MAP}
  };
  size_t i;
  if (semantic == NULL || out_kind == NULL) return 0;
  for (i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
    if (strcmp(semantic, kinds[i].name) == 0) {
      *out_kind = kinds[i].kind;
      return 1;
    }
  }
  return 0;
}

int schema_cmeta_struct_data(cmeta_data_desc *out_data,
                             cmeta_data_struct_shape *out_shape,
                             const char *stable_id,
                             const char *display_name,
                             const cmeta_type_desc *storage_type,
                             const cmeta_struct_desc *layout,
                             const cmeta_data_field_desc *fields,
                             size_t field_count) {
  cmeta_data_struct_shape shape = {layout, fields, field_count};
  cmeta_data_desc data = {sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
                          stable_id, display_name, CMETA_DATA_STRUCT,
                          storage_type, &shape, NULL, NULL, NULL};

  if (out_data == NULL || out_shape == NULL ||
      !cmeta_data_desc_valid(&data)) return 0;
  *out_shape = shape;
  data.shape = out_shape;
  *out_data = data;
  return 1;
}

int schema_cmeta_enum_data(cmeta_data_desc *out_data,
                           cmeta_data_enum_shape *out_shape,
                           const char *stable_id,
                           const char *display_name,
                           const cmeta_type_desc *storage_type,
                           const cmeta_enum_desc *meta) {
  cmeta_data_enum_shape shape = {meta};
  cmeta_data_desc data = {sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
                          stable_id, display_name, CMETA_DATA_ENUM,
                          storage_type, &shape, NULL, NULL, NULL};

  if (out_data == NULL || out_shape == NULL ||
      !cmeta_data_desc_valid(&data)) return 0;
  *out_shape = shape;
  data.shape = out_shape;
  *out_data = data;
  return 1;
}


int schema_cmeta_generic_identity(cmeta_type_identity *out_identity,
                                  const cmeta_generic_desc *constructor,
                                  const cmeta_type_identity *const *args,
                                  size_t arity) {
  cmeta_type_identity identity = {
      CMETA_TYPE_APPLY,
      NULL,
      constructor,
      args,
      arity
  };

  if (out_identity == NULL || !cmeta_type_identity_valid(&identity)) return 0;
  *out_identity = identity;
  return 1;
}
