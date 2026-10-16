#include "contract_plugin.component.h"

#include <cmeta/interface.h>
#include <type_traits>

/* The generated FR2 native vtable/projection is valid ISO C++17 as well as
 * ISO C11; native types are proven through the same CMeta X-list. */
using component_port =
    databind_11_WasmRuntime_4_Calc_3_Add__component_service;
static_assert(std::is_standard_layout<component_port>::value,
              "Component protocol must keep a C-compatible layout");

int main() {
  const cmeta_interface_desc *iface =
      databind_11_WasmRuntime_4_Calc_3_Add__component_service_interface();
  if (!cmeta_interface_desc_valid(iface) || iface->method_count != 1u)
    return 1;
  if (!cmeta_interface_method_reflection_valid(&iface->methods[0]) ||
      iface->methods[0].function->param_count != 2u)
    return 2;
  return 0;
}
