#include "installed_device.socket.h"

#include <data_bind_socket_plan.h>

#include <string.h>

int main(void) {
  return installed_device_socket_plan.abi_version ==
             DATA_BIND_SOCKET_PLAN_ABI_VERSION &&
         installed_device_socket_plan.channel_name != NULL &&
         strcmp(installed_device_socket_plan.channel_name,
                "Device.Telemetry") == 0 &&
         installed_device_socket_plan.message_type != NULL &&
         strcmp(installed_device_socket_plan.message_type,
                "TelemetryEvent") == 0 &&
         installed_device_socket_plan.format == DATA_BIND_FORMAT_BINARY &&
         installed_device_socket_plan.mode == DATA_BIND_SOCKET_MODE_STREAM &&
         installed_device_socket_plan.framing ==
             DATA_BIND_SOCKET_FRAMING_LENGTH32_BE &&
         installed_device_socket_plan.max_frame_bytes == (size_t)65536u
             ? 0
             : 1;
}
