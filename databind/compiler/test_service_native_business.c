#include "service_native_generated.h"

#include <stdint.h>

int databind_13_ServiceNative_4_Calc_3_Add(
    const AddRequest_t *request,
    AddResponse_t *response) {
  if (request == NULL || response == NULL) return -1;
  response->sum = request->left * request->scale;
  return 0;
}

int databind_13_ServiceNative_4_Calc_4_Find(
    const AddRequest_t *request,
    AddResponse_t *response,
    databind_13_ServiceNative_4_Calc_4_Find__error *error) {
  if (request == NULL || response == NULL || error == NULL) return -1;

  *error = (databind_13_ServiceNative_4_Calc_4_Find__error)
      databind_13_ServiceNative_4_Calc_4_Find__ERROR_INIT;

  if (request->left == UINT32_MAX)
    return -9;

  if (request->left == 0u) {
    error->kind = databind_13_ServiceNative_4_Calc_4_Find__ERROR_1;
    error->payload.error_1.id = request->scale;
    return 0;
  }
  if (request->left == 1u) {
    error->kind = databind_13_ServiceNative_4_Calc_4_Find__ERROR_2;
    error->payload.error_2.code = request->scale;
    return 0;
  }

  response->sum = request->left * request->scale;
  return 0;
}
