#if defined(CONSUME_SERIAL)
#include <turbo_serial.h>

int main(void) {
  turbo_serial_config_t config;
  turbo_serial_t *serial = NULL;

  turbo_serial_config_default(&config);
  if (config.baudrate != 115200 || config.bits != 8 ||
      config.parity != TURBO_SERIAL_PARITY_NONE) {
    return 1;
  }

  if (turbo_serial_create(&serial, &config) != TURBO_SERIAL_OK ||
      serial == NULL) {
    return 2;
  }
  turbo_serial_destroy(serial);
  return 0;
}
#elif defined(CONSUME_CAPTURE)
#include <turbo_capture.h>

int main(void) {
  const turbo_video_native_mode_t mode = {
      1920, 1080, 30000u, 1001u, TURBO_VIDEO_CAPTURE_FORMAT_NV12, 1u};
  return turbo_video_mode_fps(&mode) == 30 ? 0 : 1;
}
#else
#error "Select one install-consumer component"
#endif
