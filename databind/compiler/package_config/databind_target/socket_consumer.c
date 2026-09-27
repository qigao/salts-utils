#include "installed_device.socket.h"

#include <data_bind_socket_plan.h>

#include <string.h>

int main(void) {
  DataBindNativeTypeBinding native = {0};
  DataBindError error = DATA_BIND_ERROR_INIT;

  if (databind_installed_device_socket_plan.abi_version !=
          DATA_BIND_SOCKET_PLAN_ABI_VERSION ||
      databind_installed_device_socket_plan.channel_name == NULL ||
      strcmp(databind_installed_device_socket_plan.channel_name,
             "Device.Telemetry") != 0 ||
      databind_installed_device_socket_plan.message_type == NULL ||
      strcmp(databind_installed_device_socket_plan.message_type,
             "TelemetryEvent") != 0 ||
      databind_installed_device_socket_plan.format != DATA_BIND_FORMAT_BINARY ||
      databind_installed_device_socket_plan.mode != DATA_BIND_SOCKET_MODE_STREAM ||
      databind_installed_device_socket_plan.framing !=
          DATA_BIND_SOCKET_FRAMING_LENGTH32_BE ||
      databind_installed_device_socket_plan.max_frame_bytes != (size_t)65536u ||
      databind_installed_device_socket_plan.native_binding == NULL)
    return 1;

  if (databind_installed_device_socket_plan.native_binding(
          &native, &error) != DATA_BIND_OK)
    return 2;
  if (native.abi_version != DATA_BIND_NATIVE_BINDING_ABI_VERSION ||
      native.idl_type_name == NULL ||
      strcmp(native.idl_type_name, "TelemetryEvent") != 0 ||
      native.data == NULL)
    return 3;
  return 0;
}
