# Codegen conformance for one selected Service with two FR2 operations.
# A single generated ComponentDesc/Plugin ProviderBinding must cover both.
if(NOT DEFINED HEADER OR NOT DEFINED SOURCE OR
   NOT EXISTS "${HEADER}" OR NOT EXISTS "${SOURCE}")
  message(FATAL_ERROR "Missing multi-operation ComponentProvider generated outputs")
endif()
file(READ "${HEADER}" _h)
file(READ "${SOURCE}" _s)
if(DEFINED EXPECTED_PROVIDER_ABI_HEADER)
  string(FIND "${_h}" "#include <salts/${EXPECTED_PROVIDER_ABI_HEADER}>" _abi_pos)
  if(_abi_pos EQUAL -1)
    message(FATAL_ERROR "Generated multi-operation Provider includes the wrong ABI header; expected ${EXPECTED_PROVIDER_ABI_HEADER}")
  endif()
  if(EXPECTED_PROVIDER_ABI_HEADER STREQUAL "component_provider_abi.h")
    string(FIND "${_h}" "component_plugin_abi.h" _legacy_abi_pos)
    if(NOT _legacy_abi_pos EQUAL -1)
      message(FATAL_ERROR "ServiceHost multi-operation Provider must not include retired ComponentPlugin ABI")
    endif()
  endif()
endif()
foreach(_needle IN ITEMS
    "invoke_Add"
    "invoke_Multiply"
    "CMETA_INTERFACE("
    "CMETA_OBJECT_INTERFACE_ADAPTER("
    "FR2"
    "MathRequest_native_cmeta_type"
    "MathResponse_native_cmeta_type"
    "CMETA_PARAM_IN | CMETA_PARAM_BORROWED"
    "CMETA_PARAM_OUT | CMETA_PARAM_BORROWED"
    "CMETA_RESULT_VALUE")
  string(FIND "${_h}" "${_needle}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "No canonical multi-operation FR2 metadata: ${_needle}")
  endif()
endforeach()
foreach(_needle IN ITEMS
    "CMETA_STATIC_ASSERT("
    ".invoke_Add ="
    ".invoke_Multiply ="
    "salts_component_provider_binding"
    "ComponentOps.MathProvider"
    "SALTS_COMPONENT_PROVIDER_CONTRACT_ID"
    "CMETA_PLUGIN_DECLARE(")
  string(FIND "${_s}" "${_needle}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "Missing multi-operation DSO binding: ${_needle}")
  endif()
endforeach()
foreach(_forbidden IN ITEMS
    "CMETA_PLUGIN_EXPORT_FUNCTION"
    "dlopen("
    "LoadLibrary("
    "Salts::ServiceHost"
    "fallback")
  string(FIND "${_s}" "${_forbidden}" _pos)
  if(NOT _pos EQUAL -1)
    message(FATAL_ERROR "Disallowed second ownership/ABI path: ${_forbidden}")
  endif()
endforeach()
message(STATUS "Two exact FR2 methods share canonical ProviderBinding: PASS")
