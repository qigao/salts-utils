#include "service_native_public.service_native.h"
#include "service_native_public_native.h"

#include <type_traits>

static_assert(std::is_standard_layout<DataBindServiceNativeBinding>::value,
              "Service native binding must remain C-compatible");

extern "C" int databind_native_service_public_cpp_probe(void) {
  DataBindNativeTypeBinding request = {};
  DataBindNativeTypeBinding response = {};
  DataBindServiceNativeBinding service = {};
  DataBindError error = DATA_BIND_ERROR_INIT;

  return databind_13_ServiceNative_4_Calc_3_Add__databind_native_binding(
             &request, &response, &service, &error) == DATA_BIND_OK &&
         service.function != nullptr &&
         service.function->result_flags == CMETA_RESULT_VALUE &&
         databind_13_ServiceNative_4_Calc_3_Add__databind_function() ==
             service.function &&
         databind_13_ServiceNative_4_Calc_3_Add__databind_function_abi() !=
             nullptr;
}


int main() {
  return databind_native_service_public_cpp_probe() ? 0 : 1;
}
