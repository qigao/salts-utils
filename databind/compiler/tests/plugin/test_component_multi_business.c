#include "contract_multi.plugin.h"

int databind_12_ComponentOps_4_Calc_3_Add(
    const MathRequest *request, MathResponse *response) {
  if (request == NULL || response == NULL) return -1;
  response->value = request->lhs + request->rhs;
  return 0;
}

int databind_12_ComponentOps_4_Calc_8_Multiply(
    const MathRequest *request, MathResponse *response) {
  if (request == NULL || response == NULL) return -1;
  response->value = request->lhs * request->rhs;
  return 0;
}
