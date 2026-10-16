#include "contract_plugin.component.h"
#include "contract_multi.component.h"
#include <cmeta/interface.h>
#include <type_traits>
/* Two independently generated NativeSourceIR headers share one exact native
 * text/bytes storage ABI and optional owned-span CSTL providers in a TU. */
static_assert(std::is_standard_layout<databind_native_text>::value,
              "canonical native text ABI must be one C-compatible type");
static_assert(std::is_standard_layout<databind_native_bytes>::value,
              "canonical native bytes ABI must be one C-compatible type");
static_assert(std::is_standard_layout<
    databind_11_WasmRuntime_4_Calc_3_Add__component_service>::value,
    "first generated native Interface must be standard layout");
static_assert(std::is_standard_layout<
    databind_12_ComponentOps_4_Calc_3_Add__component_service>::value,
    "second generated native Interface must be standard layout");

int main() {
  const cmeta_interface_desc *old_service =
      databind_11_WasmRuntime_4_Calc_3_Add__component_service_interface();
  const cmeta_interface_desc *multi_service =
      databind_12_ComponentOps_4_Calc_3_Add__component_service_interface();
  if (!cmeta_interface_borrowed_service_abi_valid(old_service) ||
      !cmeta_interface_borrowed_service_abi_valid(multi_service))
    return 1;
  if (old_service->method_count != 1u ||
      multi_service->method_count != 2u)
    return 2;
  for (size_t i = 0u; i < multi_service->method_count; ++i)
    if (!cmeta_interface_method_reflection_valid(
          &multi_service->methods[i]))
      return 3;
  return 0;
}
