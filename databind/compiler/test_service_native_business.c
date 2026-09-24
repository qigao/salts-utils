#include "service_native_generated.h"

int service_native_calc_add(
    const AddRequest_t *request,
    AddResponse_t *response) {
  if (request == NULL || response == NULL) return -1;
  response->sum = request->left * request->scale;
  return 0;
}
