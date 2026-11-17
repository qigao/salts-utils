#include "empty_service.service_native.h"

unsigned int empty_service_calls;
unsigned int empty_service_cleared;

int databind_12_EmptyService_5_Probe_4_Ping(
    const Empty_t *request, Empty_t *response,
    databind_12_EmptyService_5_Probe_4_Ping__error *error) {
  (void)request;
  (void)response;
  error->kind = 0u;
  ++empty_service_calls;
  return 0;
}

int databind_12_EmptyService_5_Probe_6_Status(const Empty_t *request, Status_t *response) {
  (void)request;
  ++empty_service_calls;
  response->value = 42u;
  return 0;
}

int databind_12_EmptyService_5_Probe_5_Clear(const Status_t *request, Empty_t *response) {
  (void)response;
  ++empty_service_calls;
  empty_service_cleared = request->value;
  return 0;
}
