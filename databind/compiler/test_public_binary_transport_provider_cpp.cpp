#include "binary_exec.socket.h"
#include "binary_exec.flowmq.h"

#include <data_bind_binary_reader.h>

#include <type_traits>

static_assert(std::is_standard_layout_v<DataBindBinaryReaderPlan>);
static_assert(std::is_standard_layout_v<DataBindBinaryReaderFieldPlan>);

int main() {
  const auto *provider =
      databind_binary_exec_binary_Event_databind_binary_provider();
  const auto *plan =
      databind_binary_exec_binary_Event_databind_binary_reader_plan();
  return provider != nullptr &&
         provider->format == DATA_BIND_FORMAT_BINARY &&
         plan != nullptr &&
         databind_binary_exec_socket_plan.message_type != nullptr &&
         databind_binary_exec_flowmq_channel_plan.message_type != nullptr
             ? 0
             : 1;
}
