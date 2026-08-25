#include "tbe_cbind_capability.h"
#include "tinytest.h"

#include <stddef.h>

typedef struct capability_case {
  const char *spelling;
  const char *canonical_name;
  tbe_cbind_scalar_kind kind;
  unsigned int bits;
  int is_signed;
  int is_uuid;
  const char *c_storage;
  const char *cmeta_type_symbol;
  const char *cmeta_data_symbol;
} capability_case;

static void check_optional_string(const char *actual, const char *expected) {
  if (expected == NULL) {
    check_null(actual);
  } else {
    check_not_null(actual);
    if (actual != NULL) check_equal(actual, expected);
  }
}

spec("TbeCBind scalar capability matrix") {
  it("normalizes every specified scalar spelling to immutable generation metadata") {
    static const capability_case cases[] = {
        {"bool", "bool", TBE_CBIND_SCALAR_BOOL, 0u, 0, 0, "bool", "cmeta_type_bool",
         "cmeta_data_bool"},
        {"int8", "int8", TBE_CBIND_SCALAR_INTEGER, 8u, 1, 0, "int8_t", "turbo_int8_cmeta_type",
         "turbo_int8_cmeta_data"},
        {"int8_t", "int8", TBE_CBIND_SCALAR_INTEGER, 8u, 1, 0, "int8_t", "turbo_int8_cmeta_type",
         "turbo_int8_cmeta_data"},
        {"i8", "int8", TBE_CBIND_SCALAR_INTEGER, 8u, 1, 0, "int8_t", "turbo_int8_cmeta_type",
         "turbo_int8_cmeta_data"},
        {"uint8", "uint8", TBE_CBIND_SCALAR_INTEGER, 8u, 0, 0, "uint8_t", "turbo_uint8_cmeta_type",
         "turbo_uint8_cmeta_data"},
        {"uint8_t", "uint8", TBE_CBIND_SCALAR_INTEGER, 8u, 0, 0, "uint8_t",
         "turbo_uint8_cmeta_type", "turbo_uint8_cmeta_data"},
        {"u8", "uint8", TBE_CBIND_SCALAR_INTEGER, 8u, 0, 0, "uint8_t", "turbo_uint8_cmeta_type",
         "turbo_uint8_cmeta_data"},
        {"byte", "uint8", TBE_CBIND_SCALAR_INTEGER, 8u, 0, 0, "uint8_t", "turbo_uint8_cmeta_type",
         "turbo_uint8_cmeta_data"},
        {"int16", "int16", TBE_CBIND_SCALAR_INTEGER, 16u, 1, 0, "int16_t", "turbo_int16_cmeta_type",
         "turbo_int16_cmeta_data"},
        {"int16_t", "int16", TBE_CBIND_SCALAR_INTEGER, 16u, 1, 0, "int16_t",
         "turbo_int16_cmeta_type", "turbo_int16_cmeta_data"},
        {"i16", "int16", TBE_CBIND_SCALAR_INTEGER, 16u, 1, 0, "int16_t", "turbo_int16_cmeta_type",
         "turbo_int16_cmeta_data"},
        {"uint16", "uint16", TBE_CBIND_SCALAR_INTEGER, 16u, 0, 0, "uint16_t",
         "turbo_uint16_cmeta_type", "turbo_uint16_cmeta_data"},
        {"uint16_t", "uint16", TBE_CBIND_SCALAR_INTEGER, 16u, 0, 0, "uint16_t",
         "turbo_uint16_cmeta_type", "turbo_uint16_cmeta_data"},
        {"u16", "uint16", TBE_CBIND_SCALAR_INTEGER, 16u, 0, 0, "uint16_t",
         "turbo_uint16_cmeta_type", "turbo_uint16_cmeta_data"},
        {"int32", "int32", TBE_CBIND_SCALAR_INTEGER, 32u, 1, 0, "int32_t", "turbo_int32_cmeta_type",
         "turbo_int32_cmeta_data"},
        {"int32_t", "int32", TBE_CBIND_SCALAR_INTEGER, 32u, 1, 0, "int32_t",
         "turbo_int32_cmeta_type", "turbo_int32_cmeta_data"},
        {"i32", "int32", TBE_CBIND_SCALAR_INTEGER, 32u, 1, 0, "int32_t", "turbo_int32_cmeta_type",
         "turbo_int32_cmeta_data"},
        {"uint32", "uint32", TBE_CBIND_SCALAR_INTEGER, 32u, 0, 0, "uint32_t",
         "turbo_uint32_cmeta_type", "turbo_uint32_cmeta_data"},
        {"uint32_t", "uint32", TBE_CBIND_SCALAR_INTEGER, 32u, 0, 0, "uint32_t",
         "turbo_uint32_cmeta_type", "turbo_uint32_cmeta_data"},
        {"u32", "uint32", TBE_CBIND_SCALAR_INTEGER, 32u, 0, 0, "uint32_t",
         "turbo_uint32_cmeta_type", "turbo_uint32_cmeta_data"},
        {"int64", "int64", TBE_CBIND_SCALAR_INTEGER, 64u, 1, 0, "int64_t", "turbo_int64_cmeta_type",
         "turbo_int64_cmeta_data"},
        {"int64_t", "int64", TBE_CBIND_SCALAR_INTEGER, 64u, 1, 0, "int64_t",
         "turbo_int64_cmeta_type", "turbo_int64_cmeta_data"},
        {"i64", "int64", TBE_CBIND_SCALAR_INTEGER, 64u, 1, 0, "int64_t", "turbo_int64_cmeta_type",
         "turbo_int64_cmeta_data"},
        {"uint64", "uint64", TBE_CBIND_SCALAR_INTEGER, 64u, 0, 0, "uint64_t",
         "turbo_uint64_cmeta_type", "turbo_uint64_cmeta_data"},
        {"uint64_t", "uint64", TBE_CBIND_SCALAR_INTEGER, 64u, 0, 0, "uint64_t",
         "turbo_uint64_cmeta_type", "turbo_uint64_cmeta_data"},
        {"u64", "uint64", TBE_CBIND_SCALAR_INTEGER, 64u, 0, 0, "uint64_t",
         "turbo_uint64_cmeta_type", "turbo_uint64_cmeta_data"},
        {"float", "float", TBE_CBIND_SCALAR_FLOAT, 32u, 0, 0, "float", "cmeta_type_float",
         "cmeta_data_float"},
        {"double", "double", TBE_CBIND_SCALAR_FLOAT, 64u, 0, 0, "double", "cmeta_type_double",
         "cmeta_data_double"},
        {"string", "string", TBE_CBIND_SCALAR_STRING, 0u, 0, 0, "tstr", "turbo_tstr_cmeta_type",
         NULL},
        {"uuid", "uuid", TBE_CBIND_SCALAR_UUID, 0u, 0, 1, "turbo_uuid_t", "turbo_uuid_cmeta_type",
         "turbo_uuid_cmeta_data"},
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
      const capability_case *expected = &cases[index];
      const tbe_cbind_capability *actual = tbe_cbind_capability_find(expected->spelling);

      info("spelling: %s", expected->spelling);
      check_not_null(actual);
      if (actual == NULL) continue;
      check_equal(actual->canonical_name, expected->canonical_name);
      check_equal(actual->kind, expected->kind);
      check_equal(actual->bits, expected->bits);
      check_equal(actual->is_signed, expected->is_signed);
      check_equal(actual->is_uuid, expected->is_uuid);
      check_equal(actual->c_storage, expected->c_storage);
      check_equal(actual->cmeta_type_symbol, expected->cmeta_type_symbol);
      check_optional_string(actual->cmeta_data_symbol, expected->cmeta_data_symbol);
    }
  }

  it("rejects non-scalar and absent type names") {
    static const char *const rejected[] = {"bytes", "Order", "unknown", "f32", "f64", ""};
    size_t index;

    for (index = 0u; index < sizeof(rejected) / sizeof(rejected[0]); ++index) {
      info("rejected spelling: %s", rejected[index]);
      check_null(tbe_cbind_capability_find(rejected[index]));
    }
    check_null(tbe_cbind_capability_find(NULL));
  }
}
