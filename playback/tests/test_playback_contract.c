#include <salts_playback.h>
#include <tinytest.h>

suite("playback public contract") {
  it("rejects malformed output and configuration") {
    salts_playback_t *playback = (salts_playback_t *)(uintptr_t)1u;
    salts_playback_config_t config = {48000u, 2u, SALTS_PLAYBACK_FORMAT_F32, 50u};

    check_equal(salts_playback_create(NULL, &config, NULL), SALTS_PLAYBACK_ERR_FORMAT);
    config.channels = 0u;
    check_equal(salts_playback_create(NULL, &config, &playback), SALTS_PLAYBACK_ERR_FORMAT);
    check_null(playback);
  }

  it("rejects malformed enumeration arguments") {
    salts_playback_device_t device;
    size_t count = 99u;

    check_equal(salts_playback_list_devices(NULL, 1u, &count), SALTS_PLAYBACK_ERR_FORMAT);
    check_equal(count, (size_t)0u);
    check_equal(salts_playback_list_devices(&device, 0u, &count), SALTS_PLAYBACK_ERR_FORMAT);
    check_equal(count, (size_t)0u);
    check_equal(salts_playback_get_default_device(NULL), SALTS_PLAYBACK_ERR_FORMAT);
  }

  it("handles null lifecycle inputs without side effects") {
    float volume = 9.0f;
    size_t written = 99u;

    check_equal(salts_playback_start(NULL), SALTS_PLAYBACK_ERR_FORMAT);
    check_equal(salts_playback_stop(NULL), SALTS_PLAYBACK_ERR_FORMAT);
    check_equal(salts_playback_pause(NULL), SALTS_PLAYBACK_ERR_FORMAT);
    check_equal(salts_playback_resume(NULL), SALTS_PLAYBACK_ERR_FORMAT);
    check_equal(salts_playback_set_volume(NULL, 1.0f), SALTS_PLAYBACK_ERR_FORMAT);
    check_equal(salts_playback_get_volume(NULL, &volume), SALTS_PLAYBACK_ERR_FORMAT);
    check_equal(volume, 0.0f);
    check_equal(salts_playback_write(NULL, "pcm", 3u, &written), SALTS_PLAYBACK_ERR_FORMAT);
    check_equal(written, (size_t)0u);
    check_equal(salts_playback_get_available(NULL), (size_t)0u);
    check_equal(salts_playback_get_buffered(NULL), (size_t)0u);
    check_equal(salts_playback_clear(NULL), SALTS_PLAYBACK_ERR_FORMAT);
    check_equal(salts_playback_drain(NULL, 1u), SALTS_PLAYBACK_ERR_FORMAT);
    check_equal(salts_playback_get_state(NULL), SALTS_PLAYBACK_STATE_ERROR);
    salts_playback_destroy(NULL);
  }
}
