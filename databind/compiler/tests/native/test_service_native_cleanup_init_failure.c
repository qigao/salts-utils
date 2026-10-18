#include <cmeta/data.h>
#include <cmeta_cmeta_data.h>
#include <cstl/byte_buffer.h>
#include <string.h>

#include "service_native_cleanup_records.h"

static cmeta_data_buffer_ops observed_tstr_ops;
static cmeta_data_desc observed_tstr_data;
static cmeta_data_buffer_ops failing_bytes_ops;
static cmeta_data_desc failing_bytes_data;
static char lifecycle_events[8];
static size_t lifecycle_event_count;
static size_t tstr_init_calls;
static size_t tstr_restore_calls;
static size_t bytes_init_calls;
static size_t bytes_restore_calls;

static void lifecycle_record(char event) {
  if (lifecycle_event_count < sizeof(lifecycle_events))
    lifecycle_events[lifecycle_event_count++] = event;
}

static void observed_tstr_restore_zero(void *object) {
  ++tstr_restore_calls;
  lifecycle_record('T');
  cmeta_tstr_header_cmeta_buffer_ops.restore_zero(object);
}

static cmeta_status observed_tstr_init_zero(void *object) {
  ++tstr_init_calls;
  lifecycle_record('t');
  return cmeta_tstr_header_cmeta_buffer_ops.init_zero(object);
}

/* init_zero reports a permitted lifecycle failure after establishing a value
 * that the canonical restore callback can safely return to semantic zero. */
static cmeta_status failing_bytes_init_zero(void *object) {
  cmeta_status status;
  ++bytes_init_calls;
  lifecycle_record('b');
  status = stl_byte_buffer_cmeta_buffer_ops.init_zero(object);
  return status == CMETA_OK ? CMETA_OUT_OF_MEMORY : status;
}

static void failing_bytes_restore_zero(void *object) {
  ++bytes_restore_calls;
  lifecycle_record('B');
  stl_byte_buffer_cmeta_buffer_ops.restore_zero(object);
}

/* Initialize descriptors at runtime: MSVC cannot use imported DLL data
 * addresses in C file-scope initializers. Their canonical identities/layouts
 * stay unchanged; only the test-local callbacks wrap canonical behavior. */
static void lifecycle_descriptors_setup(void) {
  observed_tstr_ops = cmeta_tstr_header_cmeta_buffer_ops;
  observed_tstr_ops.init_zero = observed_tstr_init_zero;
  observed_tstr_ops.restore_zero = observed_tstr_restore_zero;
  observed_tstr_data = cmeta_tstr_header_cmeta_data;
  observed_tstr_data.buffer_ops = &observed_tstr_ops;

  failing_bytes_ops = stl_byte_buffer_cmeta_buffer_ops;
  failing_bytes_ops.init_zero = failing_bytes_init_zero;
  failing_bytes_ops.restore_zero = failing_bytes_restore_zero;
  failing_bytes_data = stl_byte_buffer_cmeta_data;
  failing_bytes_data.buffer_ops = &failing_bytes_ops;

  lifecycle_event_count = 0u;
  tstr_init_calls = 0u;
  tstr_restore_calls = 0u;
  bytes_init_calls = 0u;
  bytes_restore_calls = 0u;
}

#define cmeta_tstr_cmeta_data observed_tstr_data
#define stl_byte_buffer_cmeta_data failing_bytes_data
#include "service_native_cleanup_generated.h"
#undef cmeta_tstr_cmeta_data
#undef stl_byte_buffer_cmeta_data
#include "tinytest.h"

suite("DataBind typed-error cleanup runtime init failure") {
  it("rolls back the accepted prefix when a valid bytes init callback fails") {
    MultiError_t payload = {0};
    bool detail_zero = false;
    bool bytes_zero = false;
    DataBindStatus status;

    lifecycle_descriptors_setup();
    status = databind_12_CleanupError_5_Store_4_Read__error_1_init(&payload);

    check_equal(status, DATA_BIND_ERR_RUNTIME);
    check_equal(tstr_init_calls, 1u);
    check_equal(bytes_init_calls, 1u);
    /* The CMeta facade cleans the failed bytes slot; generated init then
     * rolls back the previously accepted string field. */
    check_equal(bytes_restore_calls, 1u);
    check_equal(tstr_restore_calls, 1u);
    check_equal(lifecycle_event_count, 4u);
    check_equal(memcmp(lifecycle_events, "tbBT", 4u), 0);

    check_equal(
        cmeta_data_value_is_zero(
            &observed_tstr_data, &payload.detail, &detail_zero),
        CMETA_OK);
    check_true(detail_zero);
    check_equal(
        cmeta_data_value_is_zero(
            &stl_byte_buffer_cmeta_data, &payload.payload, &bytes_zero),
        CMETA_OK);
    check_true(bytes_zero);
  }
}
