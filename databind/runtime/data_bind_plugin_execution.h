#ifndef DATA_BIND_PLUGIN_EXECUTION_H
#define DATA_BIND_PLUGIN_EXECUTION_H

#include "data_bind_plugin_catalog.h"
#include "data_bind_native_binding.h"

#include <salts/plugin.h>

#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Admit one generated DataBind Plugin Service operation as the canonical exact
 * native execution descriptor.
 *
 * operation and function_export borrow Plugin-owned descriptors/code. The
 * caller must hold a live Salts Plugin lease covering admission and every use
 * of the returned execution descriptor, including binding, invocation, output
 * publication and native-value cleanup.
 *
 * This helper does not acquire/release registry leases and does not create a
 * second invocation ABI. It only proves that the generated catalog row and the
 * Plugin FUNCTION export describe the same Service operation.
 */
static inline int data_bind_plugin_operation_execution_admit(
    const DataBindPluginOperationBinding *operation,
    const cmeta_plugin_export *function_export,
    DataBindNativeExecution *out) {
  const cmeta_function_desc *function;
  const cmeta_function_abi_desc *abi;
  const cmeta_param_desc *request_param;
  const cmeta_param_desc *response_param;
  size_t expected_params;
  DataBindNativeExecution execution =
      DATA_BIND_NATIVE_EXECUTION_INIT;

  if (out == NULL) return 0;
  *out = execution;

  if (!data_bind_plugin_operation_binding_valid(operation) ||
      function_export == NULL ||
      function_export->struct_size != CMETA_PLUGIN_EXPORT_SIZE ||
      function_export->kind != CMETA_PLUGIN_EXPORT_FUNCTION ||
      function_export->export_id == NULL ||
      strcmp(function_export->export_id, operation->export_id) != 0 ||
      cmeta_plugin_export_require_function(
          function_export,
          function_export->contract_id,
          function_export->contract_version,
          0u) != CMETA_PLUGIN_OK)
    return 0;

  function = function_export->value.function.desc;
  abi = function_export->value.function.abi;
  if (!cmeta_function_desc_valid(function) ||
      !cmeta_function_abi_desc_valid(abi) ||
      !cmeta_function_desc_equal(operation->function, function) ||
      abi->function != function ||
      function_export->value.function.invoke == NULL ||
      !cmeta_type_equal(function->return_type, &cmeta_type_int))
    return 0;

  expected_params = operation->error_count == 0u ? 2u : 3u;
  if (function->param_count != expected_params ||
      abi->param_count != expected_params ||
      abi->return_carrier != CMETA_ABI_SCALAR ||
      cmeta_function_param_abi(abi, 0u) != CMETA_ABI_OBJECT_POINTER ||
      cmeta_function_param_abi(abi, 1u) != CMETA_ABI_OBJECT_POINTER ||
      (expected_params == 3u &&
       cmeta_function_param_abi(abi, 2u) != CMETA_ABI_OBJECT_POINTER))
    return 0;

  request_param = cmeta_function_param(function, 0u);
  response_param = cmeta_function_param(function, 1u);
  if (request_param == NULL || response_param == NULL ||
      request_param->type == NULL || response_param->type == NULL ||
      request_param->type->kind != CMETA_T_POINTER ||
      response_param->type->kind != CMETA_T_POINTER ||
      request_param->type->pointee == NULL ||
      response_param->type->pointee == NULL ||
      !cmeta_type_equal(
          request_param->type->pointee,
          operation->request.data->storage_type) ||
      !cmeta_type_equal(
          response_param->type->pointee,
          operation->response.data->storage_type))
    return 0;

  if (expected_params == 3u) {
    const cmeta_param_desc *error_param =
        cmeta_function_param(function, 2u);
    if (operation->error_param_index != 2u ||
        operation->error_envelope_bytes == 0u ||
        operation->errors == NULL ||
        error_param == NULL || error_param->type == NULL ||
        error_param->type->kind != CMETA_T_POINTER ||
        error_param->type->pointee == NULL ||
        error_param->type->pointee->size != operation->error_envelope_bytes)
      return 0;
  }

  execution.function = function;
  execution.abi = abi;
  execution.context = function_export->value.function.context;
  execution.invoke = function_export->value.function.invoke;
  if (!data_bind_native_execution_valid(&execution))
    return 0;

  *out = execution;
  return 1;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DATA_BIND_PLUGIN_EXECUTION_H */
