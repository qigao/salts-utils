#include <cmeta/data_reflect.h>
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

  it("moves a prior field back when a later bytes move fails") {
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
    check_false(source_detail_zero);
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

  it("reports cleanup failure from clear after restoring the remaining field") {
    MultiError_t payload = {0};
    bool detail_zero = false;
    DataBindStatus status;
    payload.detail = tstr_dup("live cleanup field");
    check_not_null(payload.detail);

    status = databind_12_CleanupError_5_Store_4_Read__error_1_clear(&payload);

    check_equal(status, DATA_BIND_ERR_RUNTIME);
    check_equal(
        cmeta_data_value_is_zero(
            &cmeta_tstr_cmeta_data, &payload.detail, &detail_zero),
        CMETA_OK);
    check_true(detail_zero);
  }
}
