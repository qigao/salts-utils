#include "tinytest.h"

#include <cmeta/data.h>
#include <salts_cmeta_data.h>
#include <salts_cmeta_fixed_width.h>

#include <stddef.h>

/* RED contract: production declaration/implementation follows after this test
 * proves the schema layer must reuse Salts Core canonical descriptors. */
extern const cmeta_data_desc *schema_cmeta_builtin_data(const char *name);

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
}
