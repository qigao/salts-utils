#include "tbe_cbind_capability.h"

#include <stddef.h>
#include <string.h>

enum tbe_cbind_capability_index {
  TBE_CBIND_CAPABILITY_BOOL = 0,
  TBE_CBIND_CAPABILITY_INT8,
  TBE_CBIND_CAPABILITY_UINT8,
  TBE_CBIND_CAPABILITY_INT16,
  TBE_CBIND_CAPABILITY_UINT16,
  TBE_CBIND_CAPABILITY_INT32,
  TBE_CBIND_CAPABILITY_UINT32,
  TBE_CBIND_CAPABILITY_INT64,
  TBE_CBIND_CAPABILITY_UINT64,
  TBE_CBIND_CAPABILITY_FLOAT,
  TBE_CBIND_CAPABILITY_DOUBLE,
  TBE_CBIND_CAPABILITY_STRING,
  TBE_CBIND_CAPABILITY_UUID
};

static const tbe_cbind_capability TBE_CBIND_CAPABILITIES[] = {
    {"bool", TBE_CBIND_SCALAR_BOOL, 0u, 0, 0, "bool", "cmeta_type_bool", "cmeta_data_bool"},
    {"int8", TBE_CBIND_SCALAR_INTEGER, 8u, 1, 0, "int8_t", "turbo_int8_cmeta_type",
     "turbo_int8_cmeta_data"},
    {"uint8", TBE_CBIND_SCALAR_INTEGER, 8u, 0, 0, "uint8_t", "turbo_uint8_cmeta_type",
     "turbo_uint8_cmeta_data"},
    {"int16", TBE_CBIND_SCALAR_INTEGER, 16u, 1, 0, "int16_t", "turbo_int16_cmeta_type",
     "turbo_int16_cmeta_data"},
    {"uint16", TBE_CBIND_SCALAR_INTEGER, 16u, 0, 0, "uint16_t", "turbo_uint16_cmeta_type",
     "turbo_uint16_cmeta_data"},
    {"int32", TBE_CBIND_SCALAR_INTEGER, 32u, 1, 0, "int32_t", "turbo_int32_cmeta_type",
     "turbo_int32_cmeta_data"},
    {"uint32", TBE_CBIND_SCALAR_INTEGER, 32u, 0, 0, "uint32_t", "turbo_uint32_cmeta_type",
     "turbo_uint32_cmeta_data"},
    {"int64", TBE_CBIND_SCALAR_INTEGER, 64u, 1, 0, "int64_t", "turbo_int64_cmeta_type",
     "turbo_int64_cmeta_data"},
    {"uint64", TBE_CBIND_SCALAR_INTEGER, 64u, 0, 0, "uint64_t", "turbo_uint64_cmeta_type",
     "turbo_uint64_cmeta_data"},
    {"float", TBE_CBIND_SCALAR_FLOAT, 32u, 0, 0, "float", "cmeta_type_float", "cmeta_data_float"},
    {"double", TBE_CBIND_SCALAR_FLOAT, 64u, 0, 0, "double", "cmeta_type_double",
     "cmeta_data_double"},
    {"string", TBE_CBIND_SCALAR_STRING, 0u, 0, 0, "tstr", "turbo_tstr_cmeta_type", NULL},
    {"uuid", TBE_CBIND_SCALAR_UUID, 0u, 0, 1, "turbo_uuid_t", "turbo_uuid_cmeta_type",
     "turbo_uuid_cmeta_data"},
};

typedef struct tbe_cbind_capability_alias {
  const char *name;
  unsigned int capability_index;
} tbe_cbind_capability_alias;

static const tbe_cbind_capability_alias TBE_CBIND_CAPABILITY_ALIASES[] = {
    {"bool", TBE_CBIND_CAPABILITY_BOOL},       {"int8", TBE_CBIND_CAPABILITY_INT8},
    {"int8_t", TBE_CBIND_CAPABILITY_INT8},     {"i8", TBE_CBIND_CAPABILITY_INT8},
    {"uint8", TBE_CBIND_CAPABILITY_UINT8},     {"uint8_t", TBE_CBIND_CAPABILITY_UINT8},
    {"u8", TBE_CBIND_CAPABILITY_UINT8},        {"byte", TBE_CBIND_CAPABILITY_UINT8},
    {"int16", TBE_CBIND_CAPABILITY_INT16},     {"int16_t", TBE_CBIND_CAPABILITY_INT16},
    {"i16", TBE_CBIND_CAPABILITY_INT16},       {"uint16", TBE_CBIND_CAPABILITY_UINT16},
    {"uint16_t", TBE_CBIND_CAPABILITY_UINT16}, {"u16", TBE_CBIND_CAPABILITY_UINT16},
    {"int32", TBE_CBIND_CAPABILITY_INT32},     {"int32_t", TBE_CBIND_CAPABILITY_INT32},
    {"i32", TBE_CBIND_CAPABILITY_INT32},       {"uint32", TBE_CBIND_CAPABILITY_UINT32},
    {"uint32_t", TBE_CBIND_CAPABILITY_UINT32}, {"u32", TBE_CBIND_CAPABILITY_UINT32},
    {"int64", TBE_CBIND_CAPABILITY_INT64},     {"int64_t", TBE_CBIND_CAPABILITY_INT64},
    {"i64", TBE_CBIND_CAPABILITY_INT64},       {"uint64", TBE_CBIND_CAPABILITY_UINT64},
    {"uint64_t", TBE_CBIND_CAPABILITY_UINT64}, {"u64", TBE_CBIND_CAPABILITY_UINT64},
    {"float", TBE_CBIND_CAPABILITY_FLOAT},     {"double", TBE_CBIND_CAPABILITY_DOUBLE},
    {"string", TBE_CBIND_CAPABILITY_STRING},   {"uuid", TBE_CBIND_CAPABILITY_UUID},
};

const tbe_cbind_capability *tbe_cbind_capability_find(const char *type_name) {
  size_t index;
  if (type_name == NULL) return NULL;
  for (index = 0u;
       index < sizeof(TBE_CBIND_CAPABILITY_ALIASES) / sizeof(TBE_CBIND_CAPABILITY_ALIASES[0]);
       ++index) {
    const tbe_cbind_capability_alias *alias = &TBE_CBIND_CAPABILITY_ALIASES[index];
    if (strcmp(type_name, alias->name) == 0)
      return &TBE_CBIND_CAPABILITIES[alias->capability_index];
  }
  return NULL;
}

size_t tbe_cbind_capability_count(void) {
  return sizeof(TBE_CBIND_CAPABILITIES) / sizeof(TBE_CBIND_CAPABILITIES[0]);
}

const tbe_cbind_capability *tbe_cbind_capability_at(size_t index) {
  return index < tbe_cbind_capability_count() ? &TBE_CBIND_CAPABILITIES[index] : NULL;
}

size_t tbe_cbind_capability_spelling_count(void) {
  return sizeof(TBE_CBIND_CAPABILITY_ALIASES) / sizeof(TBE_CBIND_CAPABILITY_ALIASES[0]);
}

const char *tbe_cbind_capability_spelling_at(size_t index) {
  return index < tbe_cbind_capability_spelling_count() ? TBE_CBIND_CAPABILITY_ALIASES[index].name
                                                        : NULL;
}
