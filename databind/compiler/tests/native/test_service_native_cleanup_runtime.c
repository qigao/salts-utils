#include <cmeta/data_reflect.h>
#include <cmeta_cmeta_data.h>
#include <cstl/byte_buffer.h>
#include <string.h>

#include "service_native_cleanup_records.h"

typedef struct CleanupFailureView {
  int value;
} CleanupFailureView;

cmeta_reflect_data(CleanupFailureView, "test.databind.CleanupFailureView",
    cmeta_field(int, value)
);

static char observed_tstr_events[32];
static size_t observed_tstr_event_count;
static size_t observed_tstr_init_calls;
static size_t observed_tstr_restore_calls;
static size_t observed_tstr_move_calls;

static void observed_tstr_record(char event) {
  if (observed_tstr_event_count < sizeof(observed_tstr_events))
    observed_tstr_events[observed_tstr_event_count++] = event;
}

static void observed_tstr_reset(void) {
  observed_tstr_event_count = 0u;
  observed_tstr_init_calls = 0u;
  observed_tstr_restore_calls = 0u;
  observed_tstr_move_calls = 0u;
}

static bool observed_tstr_is_zero(const void *object) {
  return cmeta_tstr_header_cmeta_buffer_ops.is_zero(object);
}

static cmeta_status observed_tstr_assign(
    void *object, const unsigned char *data, size_t size, size_t max_bytes) {
  return cmeta_tstr_header_cmeta_buffer_ops.assign(
      object, data, size, max_bytes);
}

static void observed_tstr_restore_zero(void *object) {
  ++observed_tstr_restore_calls;
  observed_tstr_record('T');
  cmeta_tstr_header_cmeta_buffer_ops.restore_zero(object);
}

static cmeta_status observed_tstr_read(
    const void *object, const unsigned char **out_data, size_t *out_size) {
  return cmeta_tstr_header_cmeta_buffer_ops.read(object, out_data, out_size);
}

static cmeta_status observed_tstr_init_zero(void *object) {
  ++observed_tstr_init_calls;
  observed_tstr_record('t');
  return cmeta_tstr_header_cmeta_buffer_ops.init_zero(object);
}

static void observed_tstr_move(void *destination, void *source) {
  ++observed_tstr_move_calls;
  observed_tstr_record('m');
  cmeta_tstr_header_cmeta_buffer_ops.move(destination, source);
}

static const cmeta_data_buffer_ops observed_tstr_buffer_ops = {
    sizeof(cmeta_data_buffer_ops), CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    &cmeta_tstr_header_cmeta_type, CMETA_DATA_BUFFER_OWNED,
    observed_tstr_is_zero, observed_tstr_assign, observed_tstr_restore_zero,
    observed_tstr_read, observed_tstr_init_zero, observed_tstr_move};

static const cmeta_data_desc observed_tstr_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "salts.tstr.data", "tstr", CMETA_DATA_STRING,
    &cmeta_tstr_header_cmeta_type, &cmeta_tstr_header_cmeta_shape,
    &observed_tstr_buffer_ops, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

/* Replace only the bytes lifecycle descriptor in this translation unit.
 * The canonical CMeta view contract rejects lifecycle operations naturally. */
#define cmeta_tstr_cmeta_data observed_tstr_data
#define stl_byte_buffer_cmeta_data CleanupFailureView__data_meta
#include "service_native_cleanup_generated.h"
#undef cmeta_tstr_cmeta_data
#undef stl_byte_buffer_cmeta_data
#include "tinytest.h"

suite("DataBind typed-error cleanup runtime failure") {
  it("rolls back the accepted prefix when bytes initialization fails") {
    MultiError_t payload = {0};
    bool detail_zero = false;
    DataBindStatus status;
    observed_tstr_reset();
    status = databind_12_CleanupError_5_Store_4_Read__error_1_init(&payload);

    check_equal(status, DATA_BIND_ERR_RUNTIME);
    check_equal(observed_tstr_init_calls, 1u);
    check_equal(observed_tstr_restore_calls, 1u);
    check_equal(observed_tstr_move_calls, 0u);
    check_equal(observed_tstr_event_count, 2u);
    check_equal(memcmp(observed_tstr_events, "tT", 2u), 0);
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

    observed_tstr_reset();
    status = databind_12_CleanupError_5_Store_4_Read__error_clear(&error);

    check_equal(status, DATA_BIND_ERR_RUNTIME);
    check_equal(observed_tstr_restore_calls, 1u);
    check_equal(observed_tstr_event_count, 1u);
    check_equal(observed_tstr_events[0], 'T');
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

    observed_tstr_reset();
    status = databind_12_CleanupError_5_Store_4_Read__error_1_clear(&payload);

    check_equal(status, DATA_BIND_ERR_RUNTIME);
    check_equal(observed_tstr_restore_calls, 1u);
    check_equal(observed_tstr_event_count, 1u);
    check_equal(observed_tstr_events[0], 'T');
    check_equal(memcmp(&payload.payload, &before, sizeof(before)), 0);
    check_equal(
        cmeta_data_value_is_zero(
            &cmeta_tstr_cmeta_data, &payload.detail, &detail_zero),
        CMETA_OK);
    check_true(detail_zero);

    stl_byte_buffer_destroy(&payload.payload);
  }
}
