#include "wasm_execution.wasm.h"
#include "wasm_execution_native.h"
#include "wasm_execution_component_path.h"

#include <data_bind_native_binding.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>


static int read_file(const char *path, uint8_t **out, size_t *out_size) {
  FILE *file;
  long length;
  uint8_t *bytes;

  if (path == NULL || out == NULL || out_size == NULL) return 0;
  *out = NULL;
  *out_size = 0u;
  file = fopen(path, "rb");
  if (file == NULL) return 0;
  if (fseek(file, 0, SEEK_END) != 0 ||
      (length = ftell(file)) <= 0 ||
      fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return 0;
  }
  bytes = (uint8_t *)malloc((size_t)length);
  if (bytes == NULL) {
    fclose(file);
    return 0;
  }
  if (fread(bytes, 1u, (size_t)length, file) != (size_t)length ||
      fclose(file) != 0) {
    free(bytes);
    return 0;
  }
  *out = bytes;
  *out_size = (size_t)length;
  return 1;
}

static int invoke(
    const DataBindNativeExecution *execution,
    const AddRequest_t *request,
    AddResponse_t *response,
    int *status) {
  void *params[2];
  params[0] = (void *)request;
  params[1] = response;
  return execution != NULL && execution->invoke != NULL &&
         execution->invoke(
             execution->context, status, params, 2u);
}

int main(void) {
  uint8_t *component = NULL;
  size_t component_size = 0u;
  databind_wasm_execution_wasm_host host = {0};
  DataBindNativeExecution execution =
      DATA_BIND_NATIVE_EXECUTION_INIT;
  AddRequest_t request = {0};
  AddResponse_t response = {0};
  int status;

  if (!read_file(
          WASM_EXECUTION_COMPONENT_PATH,
          &component, &component_size))
    return 10;
  if (!databind_wasm_execution_wasm_host_init(
          &host, component, component_size)) {
    free(component);
    return 11;
  }
  if (!databind_11_WasmRuntime_4_Calc_3_Add__databind_wasm_execution(
          &host, &execution) ||
      !data_bind_native_execution_valid(&execution)) {
    databind_wasm_execution_wasm_host_destroy(&host);
    free(component);
    return 12;
  }
  if (execution.function !=
          databind_11_WasmRuntime_4_Calc_3_Add__databind_function() ||
      execution.function->result_flags != CMETA_RESULT_VALUE) {
    databind_wasm_execution_wasm_host_destroy(&host);
    free(component);
    return 13;
  }

  request.left = 3u;
  request.right = 4u;
  status = -999;
  if (!invoke(&execution, &request, &response, &status) ||
      status != 0 ||
      response.sum != 7u ||
      response.product != 12u) {
    databind_wasm_execution_wasm_host_destroy(&host);
    free(component);
    return 20;
  }

  request.left = 0u;
  request.right = 9u;
  response = (AddResponse_t){0};
  status = -999;
  if (!invoke(&execution, &request, &response, &status) ||
      status != -7 ||
      response.sum != 0u ||
      response.product != 0u) {
    databind_wasm_execution_wasm_host_destroy(&host);
    free(component);
    return 21;
  }

  request.left = UINT32_MAX;
  request.right = 1u;
  response = (AddResponse_t){0};
  status = 12345;
  if (invoke(&execution, &request, &response, &status) ||
      status != 12345) {
    databind_wasm_execution_wasm_host_destroy(&host);
    free(component);
    return 22;
  }

  databind_wasm_execution_wasm_host_destroy(&host);
  free(component);
  return 0;
}
