#include "device.socket.h"
#include "tinytest.h"

spec("DataBind public SocketPlan frontend") {
  it("publishes only immutable Channel delivery facts") {
    check_equal(databind_device_socket_plan.abi_version,
                (uint32_t)DATA_BIND_SOCKET_PLAN_ABI_VERSION);
    check_equal(databind_device_socket_plan.channel_name,
                "Device.Telemetry");
    check_equal(databind_device_socket_plan.message_type,
                "TelemetryEvent");
    check_equal(databind_device_socket_plan.format,
                DATA_BIND_FORMAT_BINARY);
    check_equal(databind_device_socket_plan.mode,
                DATA_BIND_SOCKET_MODE_STREAM);
    check_equal(databind_device_socket_plan.framing,
                DATA_BIND_SOCKET_FRAMING_LENGTH32_BE);
    check_equal(databind_device_socket_plan.max_frame_bytes,
                (size_t)65536u);
  }
}
