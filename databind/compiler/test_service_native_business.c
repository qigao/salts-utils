#include "service_native_generated.h"

int databind_13_ServiceNative_4_Calc_3_Add(
    const AddRequest_t *request,
    AddResponse_t *response) {
  if (request == NULL || response == NULL) return -1;
  response->sum = request->left * request->scale;
  return 0;
}
