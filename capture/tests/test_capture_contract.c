#include <salts_capture.h>
#include <tinytest.h>

#include <stdint.h>

suite("capture portable contract") {
  it("rounds rational frame rates to the nearest integer") {
    salts_video_native_mode_t ntsc = {
        1920, 1080, 30000u, 1001u, SALTS_VIDEO_CAPTURE_FORMAT_NV12, 1u};

    check_equal(salts_video_mode_fps(&ntsc), 30);
  }

  it("returns zero for an absent or unrepresentable frame rate") {
    salts_video_native_mode_t no_denominator = {
        640, 480, 30u, 0u, SALTS_VIDEO_CAPTURE_FORMAT_NV12, 2u};
    salts_video_native_mode_t too_large = {
        640, 480, UINT32_MAX, 1u, SALTS_VIDEO_CAPTURE_FORMAT_NV12, 3u};

    check_equal(salts_video_mode_fps(NULL), 0);
    check_equal(salts_video_mode_fps(&no_denominator), 0);
    check_equal(salts_video_mode_fps(&too_large), 0);
  }

  it("recognizes rounded broadcast and webcam frame rates") {
    salts_video_native_mode_t mode = {
        640, 480, 24000u, 1001u, SALTS_VIDEO_CAPTURE_FORMAT_NV12, 4u};

    check_equal(salts_video_mode_is_standard_fps(&mode), 1);
    mode.framerate_numerator = 30000u;
    check_equal(salts_video_mode_is_standard_fps(&mode), 1);
    mode.framerate_numerator = 60000u;
    check_equal(salts_video_mode_is_standard_fps(&mode), 1);
    mode.framerate_numerator = 23u;
    mode.framerate_denominator = 1u;
    check_equal(salts_video_mode_is_standard_fps(&mode), 0);
  }

  it("rejects invalid device adapter arguments") {
    salts_video_device_t *device = NULL;

    check_equal(salts_video_device_open(NULL, NULL),
                SALTS_CAPTURE_ERR_FORMAT);
    check_equal(salts_video_device_create_capture(NULL, NULL, NULL),
                SALTS_CAPTURE_ERR_FORMAT);
    check_null(device);
  }

  it("filters the default mode list without changing native identities") {
    salts_video_device_t *device = NULL;
    salts_video_native_mode_t modes[5];
    size_t count = 0u;

    check_equal(salts_video_device_open("fake", &device), SALTS_CAPTURE_OK);
    check_not_null(device);
    check_equal(salts_video_device_list_modes(device, modes, 5u, &count),
                SALTS_CAPTURE_OK);
    check_equal(count, (size_t)4u);
    check_equal(modes[0].mode_id, (uint64_t)2u);
    check_equal(modes[1].mode_id, (uint64_t)3u);
    check_equal(modes[2].mode_id, (uint64_t)4u);
    check_equal(modes[3].mode_id, (uint64_t)5u);
    salts_video_device_close(device);
  }

  it("honors caller capacity when listing every native mode") {
    salts_video_device_t *device = NULL;
    salts_video_native_mode_t modes[2];
    size_t count = 99u;

    check_equal(salts_video_device_open(NULL, &device), SALTS_CAPTURE_OK);
    check_equal(salts_video_device_list_modes_all(device, modes, 2u, &count),
                SALTS_CAPTURE_OK);
    check_equal(count, (size_t)2u);
    check_equal(modes[0].mode_id, (uint64_t)1u);
    check_equal(modes[1].mode_id, (uint64_t)2u);
    salts_video_device_close(device);
  }

  it("clears mode counts and capture outputs on invalid input") {
    salts_video_device_t *device = NULL;
    salts_video_native_mode_t mode = {
        640, 480, 30u, 1u, SALTS_VIDEO_CAPTURE_FORMAT_NV12, 1u};
    salts_capture_t *capture = (salts_capture_t *)(uintptr_t)1u;
    size_t count = 99u;

    check_equal(salts_video_device_list_modes(NULL, &mode, 1u, &count),
                SALTS_CAPTURE_ERR_FORMAT);
    check_equal(count, (size_t)0u);
    check_equal(salts_video_device_create_capture(NULL, &mode, &capture),
                SALTS_CAPTURE_ERR_FORMAT);
    check_null(capture);
    check_null(device);
  }
}
