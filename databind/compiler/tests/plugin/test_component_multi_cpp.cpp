#include "contract_multi.component.h"
#include <cmeta/interface.h>
#include <type_traits>

using multi_port =
    databind_12_ComponentOps_4_Calc_3_Add__component_service;
static_assert(std::is_standard_layout<multi_port>::value,
              "CMeta multi-service Interface must have native C layout");

int main() {
  const cmeta_interface_desc *iface =
      databind_12_ComponentOps_4_Calc_3_Add__component_service_interface();
  if (!cmeta_interface_desc_valid(iface) || iface->method_count != 2u ||
      !cmeta_interface_borrowed_service_abi_valid(iface))
    return 1;
  for (size_t i = 0u; i < iface->method_count; ++i)
    if (!cmeta_interface_method_reflection_valid(&iface->methods[i]) ||
        iface->methods[i].function->param_count != 2u)
      return 2;
  return 0;
}
