#include "generated_adapter_plan.h"

#include <string.h>

int main(void) {
  const installed_preview1_adapter_function_plan *plan;

  if (installed_preview1_adapter_function_count != 1u) return 1;
  plan = &installed_preview1_adapter_functions[0];
  if (plan->source_ordinal != 0u ||
      strcmp(plan->function_name, "wasi.fixture") != 0 ||
      plan->param_count != 2u ||
      plan->params == NULL ||
      strcmp(plan->params[0].name, "fd") != 0 ||
      plan->params[0].flags != 1u ||
      plan->params[0].carrier != installed_preview1_adapter_carrier_u32 ||
      strcmp(plan->params[1].name, "offset") != 0 ||
      plan->params[1].flags != 1u ||
      plan->params[1].carrier != installed_preview1_adapter_carrier_u64 ||
      plan->return_carrier != installed_preview1_adapter_carrier_u32 ||
      plan->effects == 0u ||
      plan->properties != 0u)
    return 2;
  return 0;
}
