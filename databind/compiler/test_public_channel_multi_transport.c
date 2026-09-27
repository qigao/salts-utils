#include "device_multi.socket.h"
#include "device_multi.flowmq.h"

#include "tinytest.h"

spec("DataBind multi-transport Channel projections") {
  it("composes Socket and FlowMQ headers in one C translation unit") {
    check_equal(databind_device_multi_socket_plan.channel_name,
                "Device.Telemetry");
    check_equal(databind_device_multi_flowmq_channel_plan.channel_name,
                "Device.Telemetry");
    check_not_null(databind_device_multi_socket_plan.native_binding);
    check_not_null(databind_device_multi_flowmq_channel_plan.native_binding);
    check_true(databind_device_multi_socket_plan.native_binding !=
               databind_device_multi_flowmq_channel_plan.native_binding);
  }
}
