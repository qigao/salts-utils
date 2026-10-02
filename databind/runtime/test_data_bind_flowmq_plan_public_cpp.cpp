#include <data_bind_flowmq_plan.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

static_assert(std::is_standard_layout<DataBindFlowMQChannelPlan>::value,
              "DataBindFlowMQChannelPlan must remain C-compatible");
static_assert(std::is_standard_layout<DataBindFlowMQServicePlan>::value,
              "DataBindFlowMQServicePlan must remain C-compatible");

int main() {
  DataBindFlowMQChannelPlan plan = DATA_BIND_FLOWMQ_CHANNEL_PLAN_INIT;
  if (plan.size != sizeof(DataBindFlowMQChannelPlan)) return 1;
  if (plan.abi_version != DATA_BIND_FLOWMQ_CHANNEL_PLAN_ABI_VERSION) return 2;
  if (plan.format != DATA_BIND_FORMAT_JSON) return 3;
  if (plan.pattern != DATA_BIND_FLOWMQ_CHANNEL_PUB_SUB) return 4;
  if (plan.native_binding != nullptr) return 5;

  DataBindFlowMQServicePlan service = DATA_BIND_FLOWMQ_SERVICE_PLAN_INIT;
  if (service.size != sizeof(DataBindFlowMQServicePlan)) return 6;
  if (service.abi_version != DATA_BIND_FLOWMQ_SERVICE_PLAN_ABI_VERSION)
    return 7;
  if (service.ingress_format != DATA_BIND_FORMAT_JSON ||
      service.egress_format != DATA_BIND_FORMAT_JSON)
    return 8;
  if (service.pattern != DATA_BIND_FLOWMQ_SERVICE_REQ_REP) return 9;
  if (service.request_native_binding != nullptr ||
      service.response_native_binding != nullptr)
    return 10;
  return 0;
}
