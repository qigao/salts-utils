#include "tinytest.h"
#include "schema_cmeta.h"

#include <cmeta/data.h>
#include <salts_cmeta_data.h>
#include <salts_cmeta_fixed_width.h>

#include <stddef.h>

/* RED contract for semantic/schema-shape lowering. */
extern int schema_cmeta_data_kind(const char *semantic, cmeta_data_kind *out_kind);

suite("schema_cmeta") {
  describe("canonical builtin scalar lowering") {
    it("maps signed integer aliases to exact-width Core descriptors") {
      check_equal(schema_cmeta_builtin_data("int8"), &salts_int8_cmeta_data);
      check_equal(schema_cmeta_builtin_data("i8"), &salts_int8_cmeta_data);
      check_equal(schema_cmeta_builtin_data("int16"), &salts_int16_cmeta_data);
      check_equal(schema_cmeta_builtin_data("i16"), &salts_int16_cmeta_data);
      check_equal(schema_cmeta_builtin_data("int32"), &salts_int32_cmeta_data);
      check_equal(schema_cmeta_builtin_data("i32"), &salts_int32_cmeta_data);
      check_equal(schema_cmeta_builtin_data("int64"), &salts_int64_cmeta_data);
      check_equal(schema_cmeta_builtin_data("i64"), &salts_int64_cmeta_data);
    }

    it("maps unsigned integer aliases to exact-width Core descriptors") {
      check_equal(schema_cmeta_builtin_data("uint8"), &salts_uint8_cmeta_data);
      check_equal(schema_cmeta_builtin_data("u8"), &salts_uint8_cmeta_data);
      check_equal(schema_cmeta_builtin_data("byte"), &salts_uint8_cmeta_data);
      check_equal(schema_cmeta_builtin_data("uint16"), &salts_uint16_cmeta_data);
      check_equal(schema_cmeta_builtin_data("u16"), &salts_uint16_cmeta_data);
      check_equal(schema_cmeta_builtin_data("uint32"), &salts_uint32_cmeta_data);
      check_equal(schema_cmeta_builtin_data("u32"), &salts_uint32_cmeta_data);
      check_equal(schema_cmeta_builtin_data("uint64"), &salts_uint64_cmeta_data);
      check_equal(schema_cmeta_builtin_data("u64"), &salts_uint64_cmeta_data);
    }

    it("maps uuid to the process-wide canonical Core descriptor") {
      check_equal(schema_cmeta_builtin_data("uuid"), &salts_uuid_cmeta_data);
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
