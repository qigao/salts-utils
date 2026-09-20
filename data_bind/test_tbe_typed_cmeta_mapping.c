#include "tbe_typed.h"
#include "tinytest.h"

#include <salts_cmeta_data.h>
#include <salts_cmeta_fixed_width.h>

#include <stdint.h>
#include <string.h>

typedef union ScalarStorage {
  int64_t signed_value;
  uint64_t unsigned_value;
  double floating_value;
  salts_uuid_t uuid_value;
  uint8_t fixed_value[16];
} ScalarStorage;

typedef struct DeferredNativeDomain {
  const char *schema;
  const cmeta_data_desc *data;
  cmeta_data_kind semantic_kind;
} DeferredNativeDomain;

static const cmeta_type_identity SCALAR_RECORD_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.ScalarStorage");

static DataBindStatus scalar_descriptor_status(const cmeta_data_desc *value) {
  const cmeta_type_desc root_type = {
      "ScalarStorage", sizeof(ScalarStorage), _Alignof(ScalarStorage),
      CMETA_T_OBJECT, NULL, NULL, &SCALAR_RECORD_ID};
  const cmeta_field_desc layout_field = {
      "value", value && value->storage_type ? value->storage_type->name : "invalid",
      0u, value && value->storage_type ? value->storage_type->size : 0u,
      value && value->storage_type ? value->storage_type->align : 1u,
      value ? value->storage_type : NULL, NULL};
  const cmeta_struct_desc layout = {
      "ScalarStorage", sizeof(ScalarStorage), _Alignof(ScalarStorage),
      &layout_field, 1u};
  const cmeta_data_field_desc data_field = {
      "test.ScalarStorage.value", "value", 0u, value};
  const cmeta_data_struct_shape shape = {&layout, &data_field, 1u};
  const cmeta_data_desc root_data = {
      sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
      "test.ScalarStorage.data", "ScalarStorage", CMETA_DATA_STRUCT,
      &root_type, &shape, NULL, NULL, NULL};
  const TbeTypedField overlay_field = {
      .name = "wire_value", .kind = TBE_TYPED_OBJECT,
      .wire_kind = TBE_TYPED_U8};
  const TbeTypedType overlay = {
      .name = "ScalarStorage", .size = 1u, .fields = &overlay_field,
      .field_count = 1u};
  const TbeTypedDescriptor descriptor =
      TBE_TYPED_DESCRIPTOR_INIT(&overlay, &root_data);
  DataBindError error = DATA_BIND_ERROR_INIT;
  return tbe_typed_descriptor_validate(&descriptor, &error);
}

static void expect_scalar(const cmeta_data_desc *data) {
  check_equal(scalar_descriptor_status(data), DATA_BIND_OK);
}

spec("TBE typed canonical scalar matching") {
  it("accepts the complete fixed-width integer and float CMeta slice") {
    expect_scalar(&salts_int8_cmeta_data);
    expect_scalar(&salts_uint8_cmeta_data);
    expect_scalar(&salts_int16_cmeta_data);
    expect_scalar(&salts_uint16_cmeta_data);
    expect_scalar(&salts_int32_cmeta_data);
    expect_scalar(&salts_uint32_cmeta_data);
    expect_scalar(&salts_int64_cmeta_data);
    expect_scalar(&salts_uint64_cmeta_data);
    expect_scalar(&cmeta_data_float);
    expect_scalar(&cmeta_data_double);
  }

  it("matches copied scalar descriptors by semantic identity") {
    cmeta_data_desc data = salts_int32_cmeta_data;
    cmeta_type_desc type = *data.storage_type;
    cmeta_type_identity identity = *type.identity;
    type.identity = &identity;
    data.storage_type = &type;

    check(&data != &salts_int32_cmeta_data);
    check(cmeta_type_equal(data.storage_type,
                          salts_int32_cmeta_data.storage_type));
    expect_scalar(&data);
  }

  it("accepts canonical fixed providers and rejects native Bool/container impostors") {
    static const DeferredNativeDomain deferred[] = {
        {"bool", &cmeta_data_bool, CMETA_DATA_BOOL},
    };
    static const cmeta_data_kind containers[] = {
        CMETA_DATA_SEQUENCE, CMETA_DATA_SET, CMETA_DATA_MAP};
    size_t i;

    for (i = 0; i < sizeof(deferred) / sizeof(deferred[0]); ++i) {
      info("schema=%s", deferred[i].schema);
      check_not_null(deferred[i].data);
      check_equal(deferred[i].semantic_kind,
                  strcmp(deferred[i].schema, "uuid") == 0
                      ? CMETA_DATA_CUSTOM
                      : deferred[i].data->kind);
      check_equal(scalar_descriptor_status(deferred[i].data),
                  DATA_BIND_ERR_SCHEMA);
    }
    expect_scalar(&salts_bool8_cmeta_data);
    expect_scalar(&salts_uuid_cmeta_data);
    for (i = 0; i < sizeof(containers) / sizeof(containers[0]); ++i) {
      cmeta_data_desc malformed = salts_int32_cmeta_data;
      malformed.kind = containers[i];
      check_equal(scalar_descriptor_status(&malformed), DATA_BIND_ERR_SCHEMA);
    }
    check_equal(scalar_descriptor_status(NULL), DATA_BIND_ERR_SCHEMA);
  }
}
