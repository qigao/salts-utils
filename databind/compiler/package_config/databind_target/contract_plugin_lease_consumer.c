#include "contract_plugin.plugin_client.h"
#include <salts/plugin.h>

#include <stddef.h>
#include <stdint.h>

#ifndef GENERATED_CONTRACT_PLUGIN_PATH
#error "GENERATED_CONTRACT_PLUGIN_PATH must identify the generated Plugin DSO"
#endif

/* Verify the actual dynamically loaded Contract-only Plugin ABI, rather than
 * a linked provider query or a legacy Binary-backed catalog. */
int main(void) {
  cmeta_plugin_registry registry = {0};
  cmeta_plugin_registry_config config = {.capacity = 2u};
  cmeta_plugin_ref ref = {0};
  cmeta_plugin_lifecycle_info lifecycle = {0};
  databind_plugin_client_11_WasmRuntime_10_Calculator client = {0};
  AddRequest request = {0};
  AddResponse response = {0};
  int result = -1;
  bool quiescent = true;

  if (cmeta_plugin_registry_init(&registry, &config) != CMETA_PLUGIN_OK)
    return 1;
  if (cmeta_plugin_registry_load(
          &registry, GENERATED_CONTRACT_PLUGIN_PATH, &ref) != CMETA_PLUGIN_OK ||
      cmeta_plugin_registry_start(&registry, ref) != CMETA_PLUGIN_OK)
    return 2;
  if (databind_plugin_client_11_WasmRuntime_10_Calculator_open(
          &registry, ref, &client) != CMETA_PLUGIN_OK ||
      !databind_plugin_client_11_WasmRuntime_10_Calculator_valid(&client))
    return 3;
  if (cmeta_plugin_registry_get_lifecycle(
          &registry, ref, &lifecycle) != CMETA_PLUGIN_OK ||
      lifecycle.active_leases != 1u)
    return 4;

  request.left = 8u;
  request.right = 3u;
  if (databind_11_WasmRuntime_4_Calc_3_Add_plugin_client_call(
          &client, &request, &response, &result) != CMETA_PLUGIN_OK ||
      result != 0 || response.sum != 11u || response.product != 24u)
    return 5;
  request.left = 13u;
  request.right = 7u;
  response = (AddResponse){0};
  result = -1;
  if (databind_11_WasmRuntime_4_Calc_3_Add_plugin_client_call(
          &client, &request, &response, &result) != CMETA_PLUGIN_OK ||
      result != 0 || response.sum != 20u || response.product != 91u)
    return 6;

  if (cmeta_plugin_registry_request_stop(&registry, ref) != CMETA_PLUGIN_OK ||
      cmeta_plugin_registry_unload(&registry, ref) != CMETA_PLUGIN_BUSY ||
      cmeta_plugin_registry_poll_quiescent(
          &registry, ref, &quiescent) != CMETA_PLUGIN_OK || quiescent)
    return 7;
  if (databind_plugin_client_11_WasmRuntime_10_Calculator_close(&client) !=
          CMETA_PLUGIN_OK ||
      databind_plugin_client_11_WasmRuntime_10_Calculator_valid(&client) ||
      cmeta_plugin_registry_get_lifecycle(
          &registry, ref, &lifecycle) != CMETA_PLUGIN_OK ||
      lifecycle.active_leases != 0u)
    return 8;
  result = 123;
  if (databind_11_WasmRuntime_4_Calc_3_Add_plugin_client_call(
          &client, &request, &response, &result) !=
          CMETA_PLUGIN_INVALID_ARGUMENT || result != 123)
    return 9;
  if (cmeta_plugin_registry_poll_quiescent(
          &registry, ref, &quiescent) != CMETA_PLUGIN_OK || !quiescent ||
      cmeta_plugin_registry_unload(&registry, ref) != CMETA_PLUGIN_OK)
    return 10;
  if (databind_plugin_client_11_WasmRuntime_10_Calculator_open(
          &registry, ref, &client) != CMETA_PLUGIN_STALE ||
      cmeta_plugin_registry_destroy(&registry) != CMETA_PLUGIN_OK)
    return 11;
  return 0;
}
