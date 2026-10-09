#include "installed_contract_service.service_native.h"

int databind_11_WasmRuntime_4_Calc_3_Add(
    const AddRequest *request, AddResponse *response) {
  if (request == NULL || response == NULL) return -1;
  response->sum = request->left + request->right;
  response->product = request->left * request->right;
  return 0;
}
