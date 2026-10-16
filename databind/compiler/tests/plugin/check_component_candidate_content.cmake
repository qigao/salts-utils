# Regression for compiler-private #620 candidate source-only emission.
# Never compile/link an unpublished component runtime in the ordinary SDK job.
if(NOT DEFINED HEADER OR NOT DEFINED SOURCE OR
   NOT EXISTS "${HEADER}" OR NOT EXISTS "${SOURCE}")
  message(FATAL_ERROR "ComponentProvider candidate outputs are missing")
endif()
file(READ "${HEADER}" _header)
file(READ "${SOURCE}" _source)

foreach(_needle IN ITEMS
    "CMETA_INTERFACE("
    "CMETA_OBJECT_INTERFACE_ADAPTER("
    "AddRequest_native_cmeta_type"
    "AddResponse_native_cmeta_type"
    "FR2"
    "CMETA_PARAM_IN | CMETA_PARAM_BORROWED"
    "CMETA_PARAM_OUT | CMETA_PARAM_BORROWED"
    "CMETA_ABI_OBJECT_POINTER"
    "CMETA_RESULT_VALUE"
    "unknown")
  string(FIND "${_header}" "${_needle}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "Missing exact FR2 header contract: ${_needle}")
  endif()
endforeach()

foreach(_needle IN ITEMS
    "CMETA_STATIC_ASSERT("
    "CMETA_TYPE_MATCHES("
    "CMETA_IMPLEMENTS("
    "CMETA_COMPONENT_PROVIDES"
    "salts_component_provider_binding"
    "WasmRuntime.Calculator"
    "cmeta_object_borrow("
    "SALTS_COMPONENT_PROVIDER_CONTRACT_ID"
    "SALTS_COMPONENT_PROVIDER_CONTRACT_VERSION"
    "CMETA_PLUGIN_DECLARE(")
  string(FIND "${_source}" "${_needle}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "Missing exact ComponentProvider contract: ${_needle}")
  endif()
endforeach()

foreach(_forbidden IN ITEMS
    "CMETA_PLUGIN_EXPORT_FUNCTION"
    "Salts::ServiceHost"
    "Salts::Configurator"
    "dlopen("
    "LoadLibrary("
    "fallback")
  string(FIND "${_source}" "${_forbidden}" _pos)
  if(NOT _pos EQUAL -1)
    message(FATAL_ERROR "Generator introduced forbidden runtime/ABI path: ${_forbidden}")
  endif()
endforeach()
message(STATUS "Canonical ComponentProvider candidate FR2 generation: PASS")
