#include <data_bind_socket_plan.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

static_assert(std::is_standard_layout<DataBindSocketPlan>::value,
              "DataBindSocketPlan must remain C-compatible standard layout");

int main() {
  DataBindSocketPlan plan = DATA_BIND_SOCKET_PLAN_INIT;
  if (plan.size != sizeof(DataBindSocketPlan)) return 1;
  if (plan.abi_version != DATA_BIND_SOCKET_PLAN_ABI_VERSION) return 2;
  if (plan.format != DATA_BIND_FORMAT_JSON) return 3;
  if (plan.mode != DATA_BIND_SOCKET_MODE_STREAM) return 4;
  if (plan.framing != DATA_BIND_SOCKET_FRAMING_LENGTH32_BE) return 5;
  return 0;
}
