#include "tinytest.h"
#include "schema_cmeta.h"

#include <cmeta/data.h>
#include <salts_cmeta_data.h>
#include <salts_cmeta_fixed_width.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void check_fixed_width_descriptor(const char *name,
                                         const char *stable_id,
                                         cmeta_data_kind kind,
                                         uint8_t bits) {
  const cmeta_data_desc *data = schema_cmeta_builtin_data(name);

  check_true(data != NULL);
  if (data == NULL) return;

  check_equal(data->struct_size, sizeof(cmeta_data_desc));
  check_equal(data->abi_version, CMETA_DATA_DESC_ABI_VERSION);
  check_true(data->stable_id != NULL);
  if (data->stable_id != NULL) check_true(strcmp(data->stable_id, stable_id) == 0);
  check_equal(data->kind, kind);
  check_true(data->storage_type != NULL);
  check_true(data->shape != NULL);
  if (data->shape != NULL)
    check_equal(((const cmeta_data_integer_shape *)data->shape)->bits, bits);
}

suite("schema_cmeta") {
  describe("canonical builtin scalar lowering") {
    it("maps signed integer aliases to exact-width Core descriptor semantics") {
      check_fixed_width_descriptor("int8", "salts.int8.data", CMETA_DATA_SINT, 8u);
      check_fixed_width_descriptor("i8", "salts.int8.data", CMETA_DATA_SINT, 8u);
      check_fixed_width_descriptor("int16", "salts.int16.data", CMETA_DATA_SINT, 16u);
      check_fixed_width_descriptor("i16", "salts.int16.data", CMETA_DATA_SINT, 16u);
      check_fixed_width_descriptor("int32", "salts.int32.data", CMETA_DATA_SINT, 32u);
      check_fixed_width_descriptor("i32", "salts.int32.data", CMETA_DATA_SINT, 32u);
      check_fixed_width_descriptor("int64", "salts.int64.data", CMETA_DATA_SINT, 64u);
      check_fixed_width_descriptor("i64", "salts.int64.data", CMETA_DATA_SINT, 64u);
    }

    it("maps unsigned integer aliases to exact-width Core descriptor semantics") {
      check_fixed_width_descriptor("uint8", "salts.uint8.data", CMETA_DATA_UINT, 8u);
      check_fixed_width_descriptor("u8", "salts.uint8.data", CMETA_DATA_UINT, 8u);
      check_fixed_width_descriptor("byte", "salts.uint8.data", CMETA_DATA_UINT, 8u);
      check_fixed_width_descriptor("uint16", "salts.uint16.data", CMETA_DATA_UINT, 16u);
      check_fixed_width_descriptor("u16", "salts.uint16.data", CMETA_DATA_UINT, 16u);
      check_fixed_width_descriptor("uint32", "salts.uint32.data", CMETA_DATA_UINT, 32u);
      check_fixed_width_descriptor("u32", "salts.uint32.data", CMETA_DATA_UINT, 32u);
      check_fixed_width_descriptor("uint64", "salts.uint64.data", CMETA_DATA_UINT, 64u);
      check_fixed_width_descriptor("u64", "salts.uint64.data", CMETA_DATA_UINT, 64u);
    }

    it("maps uuid to the process-wide canonical Core descriptor") {
      check_true(schema_cmeta_builtin_data("uuid") == &salts_uuid_cmeta_data);
      check_true(salts_uuid_cmeta_data_valid(schema_cmeta_builtin_data("uuid")));
    }

    it("rejects unsupported or invalid scalar names explicitly") {
      check_null(schema_cmeta_builtin_data("varint"));
      check_null(schema_cmeta_builtin_data("not-a-type"));
      check_null(schema_cmeta_builtin_data(NULL));
    }
  }

  describe("schema semantic lowering") {
    it("maps every supported scalar family to a CMeta semantic kind") {
      struct KindCase { const char *name; cmeta_data_kind kind; };
      static const struct KindCase cases[] = {
        {"bool", CMETA_DATA_BOOL}, {"i8", CMETA_DATA_SINT},
        {"int64", CMETA_DATA_SINT}, {"u8", CMETA_DATA_UINT},
        {"uint64", CMETA_DATA_UINT}, {"float", CMETA_DATA_FLOAT},
        {"double", CMETA_DATA_FLOAT}, {"string", CMETA_DATA_STRING},
        {"bytes", CMETA_DATA_BYTES}, {"uuid", CMETA_DATA_CUSTOM},
        {"datetime", CMETA_DATA_CUSTOM}, {"date", CMETA_DATA_CUSTOM},
        {"time", CMETA_DATA_CUSTOM}, {"duration", CMETA_DATA_CUSTOM},
        {"decimal", CMETA_DATA_CUSTOM}, {"bigint", CMETA_DATA_CUSTOM},
        {"money", CMETA_DATA_CUSTOM}
      };
      size_t i;
      for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        cmeta_data_kind kind = CMETA_DATA_BOOL;
        check_true(schema_cmeta_data_kind(cases[i].name, &kind));
        check_equal(kind, cases[i].kind);
      }
    }

    it("maps structural and collection schema semantics without choosing CSTL storage") {
      struct KindCase { const char *name; cmeta_data_kind kind; };
      static const struct KindCase cases[] = {
        {"message", CMETA_DATA_STRUCT}, {"composite", CMETA_DATA_STRUCT},
        {"group", CMETA_DATA_STRUCT}, {"enum", CMETA_DATA_ENUM},
        {"flags", CMETA_DATA_ENUM}, {"union", CMETA_DATA_VARIANT},
        {"list", CMETA_DATA_SEQUENCE}, {"set", CMETA_DATA_SET},
        {"map", CMETA_DATA_MAP}
      };
      size_t i;
      for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        cmeta_data_kind kind = CMETA_DATA_BOOL;
        check_true(schema_cmeta_data_kind(cases[i].name, &kind));
        check_equal(kind, cases[i].kind);
      }
    }

    it("fails unsupported semantics deterministically without changing output") {
      cmeta_data_kind kind = CMETA_DATA_MAP;
      check_false(schema_cmeta_data_kind("result", &kind));
      check_equal(kind, CMETA_DATA_MAP);
      check_false(schema_cmeta_data_kind("not-a-type", &kind));
      check_equal(kind, CMETA_DATA_MAP);
      check_false(schema_cmeta_data_kind(NULL, &kind));
      check_equal(kind, CMETA_DATA_MAP);
      check_false(schema_cmeta_data_kind("bool", NULL));
    }
  }
}
