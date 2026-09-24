#include "data_bind.h"

#include <type_traits>

static_assert(
    std::is_standard_layout_v<DataBindChannel>,
    "Channel reflection must remain C-compatible");
static_assert(
    DATA_BIND_COMPONENT_CAPABILITY_CHANNEL !=
        DATA_BIND_COMPONENT_CAPABILITY_SERVICE,
    "Channel and Service capability kinds must remain distinct");

int main() {
  DataBindChannel channel = DATA_BIND_CHANNEL_INIT;
  DataBindComponentCapability capability =
      DATA_BIND_COMPONENT_CAPABILITY_INIT;
  capability.kind = DATA_BIND_COMPONENT_CAPABILITY_CHANNEL;
  return channel.size == sizeof(DataBindChannel) &&
                 capability.size == sizeof(DataBindComponentCapability)
             ? 0
             : 1;
}
