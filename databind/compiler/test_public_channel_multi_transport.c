#include "device_multi.socket.h"
#include "device_multi.flowmq.h"

#include "tinytest.h"

spec("DataBind multi-transport Channel projections") {
  it("composes Socket and FlowMQ headers in one C translation unit") {
    DataBindNativeTypeBinding socket_binding = {0};
    DataBindNativeTypeBinding flowmq_binding = {0};
    DataBindError socket_error = DATA_BIND_ERROR_INIT;
    DataBindError flowmq_error = DATA_BIND_ERROR_INIT;

    check_equal(databind_device_multi_socket_plan.channel_name,
                "Device.Telemetry");
    check_equal(databind_device_multi_flowmq_channel_plan.channel_name,
                "Device.Telemetry");
    check_not_null(databind_device_multi_socket_plan.native_binding);
    check_not_null(databind_device_multi_flowmq_channel_plan.native_binding);

    /*
     * Link-time composition is the symbol-collision gate. Do not compare
     * function addresses: MSVC /OPT:ICF may legally fold identical resolver
     * bodies to one address. Instead prove both independently callable symbols
     * resolve the same canonical payload binding.
     */
    check_equal(
        databind_device_multi_socket_plan.native_binding(
            &socket_binding, &socket_error),
        DATA_BIND_OK);
    check_equal(
        databind_device_multi_flowmq_channel_plan.native_binding(
            &flowmq_binding, &flowmq_error),
        DATA_BIND_OK);
    check_equal(socket_binding.idl_type_name, "TelemetryEvent");
    check_equal(flowmq_binding.idl_type_name, "TelemetryEvent");
    check_true(socket_binding.data == flowmq_binding.data);
    check_equal(socket_binding.presence_count, flowmq_binding.presence_count);
    check_equal(socket_binding.null_count, flowmq_binding.null_count);
  }
}
