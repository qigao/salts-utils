#include "tbe_typed_internal.h"
#include "tinytest.h"

#include <salts_cmeta_data.h>
#include <salts_cmeta_fixed_width.h>
#include <string.h>

static void expect_kind(const cmeta_data_desc *data, TbeTypedKind expected) {
  TbeTypedKind kind = TBE_TYPED_OBJECT;
  check_true(tbe_typed_kind_from_cmeta_data(data, &kind));
  check_equal(kind, expected);
}

spec("TBE typed CMeta scalar mapping") {
  it("maps canonical scalar descriptors by CMeta semantics") {
    expect_kind(&cmeta_data_bool, TBE_TYPED_BOOL);
    expect_kind(&salts_int8_cmeta_data, TBE_TYPED_I8);
    expect_kind(&salts_uint8_cmeta_data, TBE_TYPED_U8);
    expect_kind(&salts_int16_cmeta_data, TBE_TYPED_I16);
    expect_kind(&salts_uint16_cmeta_data, TBE_TYPED_U16);
    expect_kind(&salts_int32_cmeta_data, TBE_TYPED_I32);
    expect_kind(&salts_uint32_cmeta_data, TBE_TYPED_U32);
    expect_kind(&salts_int64_cmeta_data, TBE_TYPED_I64);
    expect_kind(&salts_uint64_cmeta_data, TBE_TYPED_U64);
    expect_kind(&cmeta_data_float, TBE_TYPED_F32);
    expect_kind(&cmeta_data_double, TBE_TYPED_F64);
    expect_kind(&salts_uuid_cmeta_data, TBE_TYPED_UUID);
  }

  it("uses semantic descriptor content rather than descriptor address") {
    cmeta_data_desc copied_i32 = salts_int32_cmeta_data;
    cmeta_data_desc copied_uuid = salts_uuid_cmeta_data;

    check_true(&copied_i32 != &salts_int32_cmeta_data);
    check_true(&copied_uuid != &salts_uuid_cmeta_data);
    expect_kind(&copied_i32, TBE_TYPED_I32);
    expect_kind(&copied_uuid, TBE_TYPED_UUID);
  }

  it("rejects unsupported or invalid descriptors without changing output") {
    cmeta_data_desc unsupported = cmeta_data_bool;
    TbeTypedKind kind = TBE_TYPED_MAP;

    unsupported.kind = CMETA_DATA_SEQUENCE;
    check_false(tbe_typed_kind_from_cmeta_data(&unsupported, &kind));
    check_equal(kind, TBE_TYPED_MAP);
    check_false(tbe_typed_kind_from_cmeta_data(NULL, &kind));
    check_equal(kind, TBE_TYPED_MAP);
    check_false(tbe_typed_kind_from_cmeta_data(&cmeta_data_bool, NULL));
  }
}
