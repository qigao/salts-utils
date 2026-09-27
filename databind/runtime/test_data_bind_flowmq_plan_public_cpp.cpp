#include <data_bind_flowmq_plan.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

static_assert(std::is_standard_layout<DataBindFlowMQChannelPlan>::value,
              "DataBindFlowMQChannelPlan must remain C-compatible");

int main() {
  DataBindFlowMQChannelPlan plan = DATA_BIND_FLOWMQ_CHANNEL_PLAN_INIT;
  if (plan.size != sizeof(DataBindFlowMQChannelPlan)) return 1;
  if (plan.abi_version != DATA_BIND_FLOWMQ_CHANNEL_PLAN_ABI_VERSION) return 2;
  if (plan.format != DATA_BIND_FORMAT_JSON) return 3;
  if (plan.pattern != DATA_BIND_FLOWMQ_CHANNEL_PUB_SUB) return 4;
  if (plan.native_binding != nullptr) return 5;
  return 0;
}
