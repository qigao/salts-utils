#include <data_bind_binding_plan.h>

#include "installed_service.service_native.h"
#include "installed_service_native.h"

#include <type_traits>
#include "service_call_fixture.h"

#define CATALOG_SYMBOL(service, operation, symbol, request, response) + 1
static_assert(0 databind_installed_service_native_SERVICES(CATALOG_SYMBOL) == 1,
    "producer catalog must enumerate the installed native operations");
#undef CATALOG_SYMBOL
static_assert(std::is_same<decltype(&databind_installed_service_native_CODEC),
    DataBindStatus (*)(DataBind **, DataBindError *)>::value, "embedded codec factory ABI");

static_assert(std::is_standard_layout<DataBindServiceNativeBinding>::value,
              "installed Service binding must be C-compatible");
static_assert(std::is_standard_layout<DataBindBindingCallLifetime>::value,
              "installed call lifetime must be C-compatible");
static_assert(std::is_standard_layout<DataBindNativeExecution>::value,
              "installed Service execution must be C-compatible");
static_assert(
    std::is_standard_layout<cflow_function_typed_adapter_projection>::value,
    "installed typed CFlow projection must be C-compatible");

int main() {
  if (installed_service_call() != 0) return 3;
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
