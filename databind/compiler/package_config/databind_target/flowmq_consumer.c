#include "installed_flow.flowmq.h"

#include <data_bind_flowmq_plan.h>

#include <string.h>

int main(void) {
  return databind_installed_flow_flowmq_channel_plan.abi_version ==
             DATA_BIND_FLOWMQ_CHANNEL_PLAN_ABI_VERSION &&
         databind_installed_flow_flowmq_channel_plan.channel_name != NULL &&
         strcmp(databind_installed_flow_flowmq_channel_plan.channel_name,
                "Device.Telemetry") == 0 &&
         databind_installed_flow_flowmq_channel_plan.message_type != NULL &&
         strcmp(databind_installed_flow_flowmq_channel_plan.message_type,
                "TelemetryEvent") == 0 &&
         databind_installed_flow_flowmq_channel_plan.format ==
             DATA_BIND_FORMAT_BINARY &&
         databind_installed_flow_flowmq_channel_plan.pattern ==
             DATA_BIND_FLOWMQ_CHANNEL_PUB_SUB &&
         databind_installed_flow_flowmq_channel_plan.max_payload_bytes ==
             (size_t)65536u &&
         databind_installed_flow_flowmq_channel_plan.native_binding != NULL
             ? 0
             : 1;
}
