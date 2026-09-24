#include "data_bind.h"

#include <type_traits>

static_assert(
    std::is_standard_layout_v<DataBindComponent>,
    "Component reflection must remain C-compatible");
static_assert(
    std::is_standard_layout_v<DataBindComponentCapability>,
    "Component capability reflection must remain C-compatible");
static_assert(
    DATA_BIND_COMPONENT_CAPABILITY_SERVICE !=
        DATA_BIND_COMPONENT_CAPABILITY_UNKNOWN,
    "Service capability kind must be explicit");

int main() {
  DataBindComponent component = DATA_BIND_COMPONENT_INIT;
  DataBindComponentCapability capability =
      DATA_BIND_COMPONENT_CAPABILITY_INIT;
  return component.size == sizeof(DataBindComponent) &&
                 capability.size == sizeof(DataBindComponentCapability)
             ? 0
             : 1;
}
