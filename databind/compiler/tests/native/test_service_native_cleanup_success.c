#include <cmeta/data_reflect.h>
#include <cstl/byte_buffer.h>
#include <string.h>

#include "service_native_cleanup_records.h"
#include "service_native_cleanup_generated.h"
#include "tinytest.h"

suite("DataBind typed-error cleanup runtime success") {
  it("initializes and clears a canonical managed payload") {
    MultiError_t payload = {0};
    bool detail_zero = false;
    bool bytes_zero = false;

    check_equal(
        databind_12_CleanupError_5_Store_4_Read__error_1_init(&payload),
        DATA_BIND_OK);
    check_equal(
        cmeta_data_value_is_zero(
            &cmeta_tstr_cmeta_data, &payload.detail, &detail_zero),
        CMETA_OK);
    check_true(detail_zero);
    check_equal(
        cmeta_data_value_is_zero(
            &stl_byte_buffer_cmeta_data, &payload.payload, &bytes_zero),
        CMETA_OK);
    check_true(bytes_zero);

    check_equal(
        databind_12_CleanupError_5_Store_4_Read__error_1_clear(&payload),
        DATA_BIND_OK);
    check_equal(
        cmeta_data_value_is_zero(
            &cmeta_tstr_cmeta_data, &payload.detail, &detail_zero),
        CMETA_OK);
    check_true(detail_zero);
    check_equal(
        cmeta_data_value_is_zero(
            &stl_byte_buffer_cmeta_data, &payload.payload, &bytes_zero),
        CMETA_OK);
    check_true(bytes_zero);
  }

  it("moves canonical managed fields and clears the source") {
    MultiError_t source = {0};
    MultiError_t destination = {0};
    const unsigned char bytes[] = {0x11u, 0x22u, 0x33u};
    bool source_detail_zero = false;
    bool source_bytes_zero = false;
    bool destination_detail_zero = true;
    bool destination_bytes_zero = true;

    check_equal(
        databind_12_CleanupError_5_Store_4_Read__error_1_init(&source),
        DATA_BIND_OK);
    check_equal(
        databind_12_CleanupError_5_Store_4_Read__error_1_init(&destination),
        DATA_BIND_OK);

    source.detail = tstr_dup("successful move");
    check_not_null(source.detail);
    check_equal(
        stl_byte_buffer_resize(&source.payload, sizeof(bytes)),
        STL_OK);
    memcpy(stl_byte_buffer_data(&source.payload), bytes, sizeof(bytes));

    check_equal(
        databind_12_CleanupError_5_Store_4_Read__error_1_move(
            &destination, &source),
        DATA_BIND_OK);
    check_equal(
        cmeta_data_value_is_zero(
            &cmeta_tstr_cmeta_data, &source.detail, &source_detail_zero),
        CMETA_OK);
    check_true(source_detail_zero);
    check_equal(
        cmeta_data_value_is_zero(
            &stl_byte_buffer_cmeta_data, &source.payload, &source_bytes_zero),
        CMETA_OK);
    check_true(source_bytes_zero);
    check_equal(
        cmeta_data_value_is_zero(
            &cmeta_tstr_cmeta_data, &destination.detail,
            &destination_detail_zero),
        CMETA_OK);
    check_true(!destination_detail_zero);
    check_equal(
        cmeta_data_value_is_zero(
            &stl_byte_buffer_cmeta_data, &destination.payload,
            &destination_bytes_zero),
        CMETA_OK);
    check_true(!destination_bytes_zero);

    check_equal(
        databind_12_CleanupError_5_Store_4_Read__error_1_clear(&source),
        DATA_BIND_OK);
    check_equal(
        databind_12_CleanupError_5_Store_4_Read__error_1_clear(&destination),
        DATA_BIND_OK);
  }
}
