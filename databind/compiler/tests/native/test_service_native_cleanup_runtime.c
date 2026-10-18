#include <cmeta/data_reflect.h>
#include <cstl/byte_buffer.h>
#include <string.h>

#include "service_native_cleanup_records.h"

typedef struct CleanupFailureView {
  int value;
} CleanupFailureView;

cmeta_reflect_data(CleanupFailureView, "test.databind.CleanupFailureView",
    cmeta_field(int, value)
);

/* Replace only the bytes lifecycle descriptor in this translation unit.
 * The canonical CMeta view contract rejects lifecycle operations naturally. */
#define stl_byte_buffer_cmeta_data CleanupFailureView__data_meta
#include "service_native_cleanup_generated.h"
#include "tinytest.h"

suite("DataBind typed-error cleanup runtime failure") {
  it("rolls back the accepted prefix when bytes initialization fails") {
    MultiError_t payload = {0};
    bool detail_zero = false;
    DataBindStatus status =
        databind_12_CleanupError_5_Store_4_Read__error_1_init(&payload);

    check_equal(status, DATA_BIND_ERR_RUNTIME);
    check_equal(
        cmeta_data_value_is_zero(
            &cmeta_tstr_cmeta_data, &payload.detail, &detail_zero),
        CMETA_OK);
    check_true(detail_zero);
  }

  it("preserves source when destination initialization fails before move") {
    MultiError_t source = {0};
    MultiError_t destination = {0};
    bool source_detail_zero = true;
    bool destination_detail_zero = false;
    DataBindStatus status;

    source.detail = tstr_dup("move rollback field");
    check_not_null(source.detail);

    status = databind_12_CleanupError_5_Store_4_Read__error_1_move(
        &destination, &source);

    check_equal(status, DATA_BIND_ERR_RUNTIME);
    check_equal(
        cmeta_data_value_is_zero(
            &cmeta_tstr_cmeta_data, &source.detail, &source_detail_zero),
        CMETA_OK);
    check_true(!source_detail_zero);
    check_equal(strcmp(source.detail, "move rollback field"), 0);
    check_equal(
        cmeta_data_value_is_zero(
            &cmeta_tstr_cmeta_data, &destination.detail,
            &destination_detail_zero),
        CMETA_OK);
    check_true(destination_detail_zero);

    check_equal(
        cmeta_data_value_restore_zero(
            &cmeta_tstr_cmeta_data, &source.detail),
        CMETA_OK);
  }


  it("preserves the selected error kind when payload initialization fails") {
    databind_12_CleanupError_5_Store_4_Read__error error = {0};
    bool detail_zero = false;
    DataBindStatus status =
        databind_12_CleanupError_5_Store_4_Read__error_select(
            &error, databind_12_CleanupError_5_Store_4_Read__ERROR_1);

    check_equal(status, DATA_BIND_ERR_RUNTIME);
    check_equal(
        error.kind, databind_12_CleanupError_5_Store_4_Read__ERROR_1);
    check_equal(
        cmeta_data_value_is_zero(
            &cmeta_tstr_cmeta_data,
            &error.payload.error_1.detail, &detail_zero),
        CMETA_OK);
    check_true(detail_zero);
  }

  it("retains error kind and failed carrier when typed-error clear fails") {
    databind_12_CleanupError_5_Store_4_Read__error error = {0};
    const unsigned char initial_bytes[] = {0x61u, 0x72u, 0x83u};
    stl_byte_buffer before = {0};
    bool detail_zero = false;
    DataBindStatus status;

    error.kind = databind_12_CleanupError_5_Store_4_Read__ERROR_1;
    check_equal(
        stl_byte_buffer_from(
            &error.payload.error_1.payload, initial_bytes,
            sizeof(initial_bytes), sizeof(initial_bytes)),
        STL_OK);
    error.payload.error_1.detail = tstr_dup("preserve kind on clear");
    check_not_null(error.payload.error_1.detail);
    before = error.payload.error_1.payload;

    status = databind_12_CleanupError_5_Store_4_Read__error_clear(&error);

    check_equal(status, DATA_BIND_ERR_RUNTIME);
    check_equal(
        error.kind, databind_12_CleanupError_5_Store_4_Read__ERROR_1);
    check_equal(
        memcmp(&error.payload.error_1.payload, &before, sizeof(before)), 0);
    check_equal(
        cmeta_data_value_is_zero(
            &cmeta_tstr_cmeta_data, &error.payload.error_1.detail,
            &detail_zero),
        CMETA_OK);
    check_true(detail_zero);

    stl_byte_buffer_destroy(&error.payload.error_1.payload);
  }


  it("preserves failed bytes carrier while clearing independent fields") {
    MultiError_t payload = {0};
    const unsigned char initial_bytes[] = {0x31u, 0x42u, 0x53u};
    stl_byte_buffer before = {0};
    bool detail_zero = false;
    DataBindStatus status;

    check_equal(
        stl_byte_buffer_from(&payload.payload, initial_bytes,
                            sizeof(initial_bytes), sizeof(initial_bytes)),
        STL_OK);
    payload.detail = tstr_dup("live cleanup field");
    check_not_null(payload.detail);
    before = payload.payload;

    status = databind_12_CleanupError_5_Store_4_Read__error_1_clear(&payload);

    check_equal(status, DATA_BIND_ERR_RUNTIME);
    check_equal(memcmp(&payload.payload, &before, sizeof(before)), 0);
    check_equal(
        cmeta_data_value_is_zero(
            &cmeta_tstr_cmeta_data, &payload.detail, &detail_zero),
        CMETA_OK);
    check_true(detail_zero);

    stl_byte_buffer_destroy(&payload.payload);
  }
}
