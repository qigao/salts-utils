#include "wasm_contract.plugin.h"

/* Exact Contract-only Component/Plugin provider ABI; no Binary Record_t. */
int databind_11_WasmRuntime_4_Calc_3_Add(
    const AddRequest *request, AddResponse *response) {
  if (!request || !response) return -1;
  response->sum = request->left + request->right;
  response->product = request->left * request->right;
  return 0;
}
