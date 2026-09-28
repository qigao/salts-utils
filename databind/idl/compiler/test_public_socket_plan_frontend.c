#include "device.socket.h"
#include "tinytest.h"

spec("DataBind public SocketPlan frontend") {
  it("publishes immutable delivery and exact native-binding facts") {
    DataBindNativeTypeBinding native =
        (DataBindNativeTypeBinding){0};
    DataBindError error = DATA_BIND_ERROR_INIT;

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
    check_not_null(databind_device_socket_plan.native_binding);
    check_equal(
        databind_device_socket_plan.native_binding(&native, &error),
        DATA_BIND_OK);
    check_equal(native.abi_version,
                (uint32_t)DATA_BIND_NATIVE_BINDING_ABI_VERSION);
    check_equal(native.idl_type_name, "TelemetryEvent");
    check_not_null(native.data);
    check_equal(native.presence_count, (size_t)0u);
    check_equal(native.null_count, (size_t)0u);
  }
}
