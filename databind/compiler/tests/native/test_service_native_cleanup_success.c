#include <cmeta/data_reflect.h>
#include <cmeta_cmeta_data.h>
#include <cstl/byte_buffer.h>
#include <stddef.h>
#include <string.h>

#include "service_native_cleanup_records.h"

static char lifecycle_events[32];
static size_t lifecycle_event_count;
static size_t tstr_init_calls;
static size_t tstr_restore_calls;
static size_t tstr_move_calls;
static size_t bytes_init_calls;
static size_t bytes_restore_calls;
static size_t bytes_move_calls;
static int lifecycle_event_overflow;

static void lifecycle_record(char event) {
  if (lifecycle_event_count >= sizeof(lifecycle_events)) {
    lifecycle_event_overflow = 1;
    return;
  }
  lifecycle_events[lifecycle_event_count++] = event;
}

static void lifecycle_reset(void) {
  lifecycle_event_count = 0u;
  tstr_init_calls = 0u;
  tstr_restore_calls = 0u;
  tstr_move_calls = 0u;
  bytes_init_calls = 0u;
  bytes_restore_calls = 0u;
  bytes_move_calls = 0u;
  lifecycle_event_overflow = 0;
}

static bool observed_tstr_is_zero(const void *object) {
  return cmeta_tstr_cmeta_buffer_ops.is_zero(object);
}

static cmeta_status observed_tstr_assign(
    void *object, const unsigned char *data, size_t size, size_t max_bytes) {
  return cmeta_tstr_cmeta_buffer_ops.assign(object, data, size, max_bytes);
}

static void observed_tstr_restore_zero(void *object) {
  ++tstr_restore_calls;
  lifecycle_record('T');
  cmeta_tstr_cmeta_buffer_ops.restore_zero(object);
}

static cmeta_status observed_tstr_read(
    const void *object, const unsigned char **out_data, size_t *out_size) {
  return cmeta_tstr_cmeta_buffer_ops.read(object, out_data, out_size);
}

static cmeta_status observed_tstr_init_zero(void *object) {
  ++tstr_init_calls;
  lifecycle_record('t');
  return cmeta_tstr_cmeta_buffer_ops.init_zero(object);
}

static void observed_tstr_move(void *destination, void *source) {
  ++tstr_move_calls;
  lifecycle_record('m');
  cmeta_tstr_cmeta_buffer_ops.move(destination, source);
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

static bool observed_bytes_is_zero(const void *object) {
  return stl_byte_buffer_cmeta_buffer_ops.is_zero(object);
}

static cmeta_status observed_bytes_assign(
    void *object, const unsigned char *data, size_t size, size_t max_bytes) {
  return stl_byte_buffer_cmeta_buffer_ops.assign(
      object, data, size, max_bytes);
}

static void observed_bytes_restore_zero(void *object) {
  ++bytes_restore_calls;
  lifecycle_record('B');
  stl_byte_buffer_cmeta_buffer_ops.restore_zero(object);
}

static cmeta_status observed_bytes_read(
    const void *object, const unsigned char **out_data, size_t *out_size) {
  return stl_byte_buffer_cmeta_buffer_ops.read(object, out_data, out_size);
}

static cmeta_status observed_bytes_init_zero(void *object) {
  ++bytes_init_calls;
  lifecycle_record('b');
  return stl_byte_buffer_cmeta_buffer_ops.init_zero(object);
}

static void observed_bytes_move(void *destination, void *source) {
  ++bytes_move_calls;
  lifecycle_record('M');
  stl_byte_buffer_cmeta_buffer_ops.move(destination, source);
}

static const cmeta_data_buffer_ops observed_bytes_buffer_ops = {
    sizeof(cmeta_data_buffer_ops), CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    &stl_byte_buffer_cmeta_type, CMETA_DATA_BUFFER_OWNED,
    observed_bytes_is_zero, observed_bytes_assign, observed_bytes_restore_zero,
    observed_bytes_read, observed_bytes_init_zero, observed_bytes_move};

static const cmeta_data_desc observed_bytes_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "cstl.byte_buffer.data", "stl_byte_buffer", CMETA_DATA_BYTES,
    &stl_byte_buffer_cmeta_type, &stl_byte_buffer_cmeta_shape,
    &observed_bytes_buffer_ops, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

/* Valid canonical providers with observation wrappers; callbacks still
 * delegate to the no-fail canonical lifecycle implementation. */
#define cmeta_tstr_cmeta_data observed_tstr_data
#define stl_byte_buffer_cmeta_data observed_bytes_data
#include "service_native_cleanup_generated.h"
#undef cmeta_tstr_cmeta_data
#undef stl_byte_buffer_cmeta_data
#include "tinytest.h"

suite("DataBind typed-error cleanup runtime success") {
  it("initializes and clears each canonical managed field exactly once") {
    MultiError_t payload = {0};
    bool detail_zero = false;
    bool bytes_zero = false;
    const char expected_events[] = {'t', 'b', 'B', 'T'};

    lifecycle_reset();
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

    check_equal(tstr_init_calls, 1u);
    check_equal(bytes_init_calls, 1u);
    check_equal(tstr_restore_calls, 1u);
    check_equal(bytes_restore_calls, 1u);
    check_equal(tstr_move_calls, 0u);
    check_equal(bytes_move_calls, 0u);
    check_true(!lifecycle_event_overflow);
    check_equal(lifecycle_event_count, sizeof(expected_events));
    check_equal(memcmp(lifecycle_events, expected_events,
                       sizeof(expected_events)), 0);
  }

  it("moves each managed field once and preserves destination contents") {
    databind_12_CleanupError_5_Store_4_Read__error source = {0};
    databind_12_CleanupError_5_Store_4_Read__error destination = {0};
    const unsigned char bytes[] = {0x11u, 0x22u, 0x33u};
    const char move_events[] = {'t', 'b', 'm', 'M'};
    const char cleanup_events[] = {'t', 'b', 'm', 'M', 'B', 'T'};
    bool source_detail_zero = false;
    bool source_bytes_zero = false;
    bool destination_detail_zero = true;
    bool destination_bytes_zero = true;

    check_equal(
        databind_12_CleanupError_5_Store_4_Read__error_select(
            &source, databind_12_CleanupError_5_Store_4_Read__ERROR_1),
        DATA_BIND_OK);
    source.payload.error_1.detail = tstr_dup("successful move");
    check_not_null(source.payload.error_1.detail);
    check_equal(
        stl_byte_buffer_resize(
            &source.payload.error_1.payload, sizeof(bytes)), STL_OK);
    memcpy(stl_byte_buffer_data(&source.payload.error_1.payload),
           bytes, sizeof(bytes));

    lifecycle_reset();
    check_equal(
        databind_12_CleanupError_5_Store_4_Read__error_move(
            &destination, &source),
        DATA_BIND_OK);
    check_equal(
        source.kind, databind_12_CleanupError_5_Store_4_Read__ERROR_NONE);
    check_equal(
        cmeta_data_value_is_zero(
            &cmeta_tstr_cmeta_data,
            &source.payload.error_1.detail, &source_detail_zero),
        CMETA_OK);
    check_true(source_detail_zero);
    check_equal(
        cmeta_data_value_is_zero(
            &stl_byte_buffer_cmeta_data,
            &source.payload.error_1.payload, &source_bytes_zero),
        CMETA_OK);
    check_true(source_bytes_zero);
    check_equal(destination.kind,
                databind_12_CleanupError_5_Store_4_Read__ERROR_1);
    check_equal(
        cmeta_data_value_is_zero(
            &cmeta_tstr_cmeta_data,
            &destination.payload.error_1.detail, &destination_detail_zero),
        CMETA_OK);
    check_true(!destination_detail_zero);
    check_equal(strcmp(destination.payload.error_1.detail, "successful move"), 0);
    check_equal(
        cmeta_data_value_is_zero(
            &stl_byte_buffer_cmeta_data,
            &destination.payload.error_1.payload, &destination_bytes_zero),
        CMETA_OK);
    check_true(!destination_bytes_zero);
    check_equal(memcmp(stl_byte_buffer_data(
                           &destination.payload.error_1.payload),
                       bytes, sizeof(bytes)), 0);

    check_equal(tstr_init_calls, 1u);
    check_equal(bytes_init_calls, 1u);
    check_equal(tstr_move_calls, 1u);
    check_equal(bytes_move_calls, 1u);
    check_equal(tstr_restore_calls, 0u);
    check_equal(bytes_restore_calls, 0u);
    check_true(!lifecycle_event_overflow);
    check_equal(lifecycle_event_count, sizeof(move_events));
    check_equal(memcmp(lifecycle_events, move_events, sizeof(move_events)), 0);

    check_equal(
        databind_12_CleanupError_5_Store_4_Read__error_clear(&source),
        DATA_BIND_OK);
    check_equal(
        databind_12_CleanupError_5_Store_4_Read__error_clear(&destination),
        DATA_BIND_OK);
    check_equal(tstr_restore_calls, 1u);
    check_equal(bytes_restore_calls, 1u);
    check_equal(lifecycle_event_count, sizeof(cleanup_events));
    check_equal(memcmp(lifecycle_events, cleanup_events,
                       sizeof(cleanup_events)), 0);
  }
}
