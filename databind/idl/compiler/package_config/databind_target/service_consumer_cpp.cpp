#include <data_bind_binding_plan.h>

#include "installed_service.service_native.h"
#include "installed_service_native.h"

#include <type_traits>

static_assert(std::is_standard_layout<DataBindServiceNativeBinding>::value,
              "installed Service binding must be C-compatible");
static_assert(std::is_standard_layout<DataBindNativeExecution>::value,
              "installed Service execution must be C-compatible");
static_assert(
    std::is_standard_layout<cflow_function_typed_adapter_projection>::value,
    "installed typed CFlow projection must be C-compatible");

int main() {
  DataBindNativeTypeBinding request{};
  DataBindNativeTypeBinding response{};
  DataBindServiceNativeBinding service{};
  DataBindError error = DATA_BIND_ERROR_INIT;
  if (databind_10_ServiceSdk_4_Calc_3_Add__databind_native_binding(
          &request, &response, &service, &error) != DATA_BIND_OK ||
      service.function == nullptr ||
      databind_10_ServiceSdk_4_Calc_3_Add__databind_function() !=
          service.function)
    return 1;

  return data_bind_service_native_error_restore_zero(
             &service, nullptr, 0u, &error) == DATA_BIND_OK
             ? 0
             : 2;
}
