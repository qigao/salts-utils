#include <salts_capture.h>
#include <tinytest.h>

suite("capture Android lifecycle") {
  it("starts the native screen lifecycle before Java produces frames") {
    salts_screen_capture_config_t config = {0, 30, 1, 0};
    salts_capture_t *capture = salts_screen_capture_create(&config);

    check_not_null(capture);
    if (!capture) return;

    check_equal(salts_capture_get_state(capture),
                SALTS_CAPTURE_STATE_STOPPED);
    check_equal(salts_capture_start(capture), SALTS_CAPTURE_OK);
    check_equal(salts_capture_get_state(capture),
                SALTS_CAPTURE_STATE_RUNNING);

    salts_capture_stop(capture);
    check_equal(salts_capture_get_state(capture),
                SALTS_CAPTURE_STATE_STOPPED);
    salts_capture_destroy(capture);
  }
}
