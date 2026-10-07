#include "installed_service.service_native.h"

int databind_10_ServiceSdk_4_Calc_3_Add(
    const AddRequest_t *request,
    AddResponse_t *response) {
  if (request == NULL || response == NULL) return -1;
  response->sum = request->left + request->scale;
  return 0;
}
