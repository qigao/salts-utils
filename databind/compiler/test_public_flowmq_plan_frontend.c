#include "device.flowmq.h"
#include "tinytest.h"

spec("DataBind public FlowMQ ChannelPlan frontend") {
  it("publishes only canonical Channel delivery facts") {
    check_equal(databind_device_flowmq_channel_plan.abi_version,
                (uint32_t)DATA_BIND_FLOWMQ_CHANNEL_PLAN_ABI_VERSION);
    check_equal(databind_device_flowmq_channel_plan.channel_name,
                "Device.Telemetry");
    check_equal(databind_device_flowmq_channel_plan.message_type,
                "TelemetryEvent");
    check_equal(databind_device_flowmq_channel_plan.format,
                DATA_BIND_FORMAT_BINARY);
    check_equal(databind_device_flowmq_channel_plan.pattern,
                DATA_BIND_FLOWMQ_CHANNEL_PUB_SUB);
    check_equal(databind_device_flowmq_channel_plan.max_payload_bytes,
                (size_t)65536u);
    check_not_null(databind_device_flowmq_channel_plan.native_binding);
  }
}
