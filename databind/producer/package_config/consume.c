#include "generated_adapter_plan.h"

#include <string.h>

int main(void) {
  const installed_preview1_adapter_function_plan *plan;

  if (installed_preview1_adapter_function_count != 1u) return 1;
  plan = &installed_preview1_adapter_functions[0];
  if (plan->source_ordinal != 0u ||
      strcmp(plan->function_name, "wasi.fixture") != 0 ||
      plan->param_count != 2u ||
      plan->param_carriers == NULL ||
      plan->param_carriers[0] != installed_preview1_adapter_carrier_u32 ||
      plan->param_carriers[1] != installed_preview1_adapter_carrier_u64 ||
      plan->return_carrier != installed_preview1_adapter_carrier_u32)
    return 2;
  return 0;
}
