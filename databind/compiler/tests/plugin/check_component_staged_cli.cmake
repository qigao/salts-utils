# One IdlContract -> one atomic PLUGIN + COMPONENT output transaction.
foreach(_required IDLC SCHEMA TYPED_ERROR_SCHEMA WORK_DIR)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "Missing staged COMPONENT test input: ${_required}")
  endif()
endforeach()
set(_dir "${WORK_DIR}/positive")
file(MAKE_DIRECTORY "${_dir}")
set(_native "${_dir}/contract_multi_native.h")
set(_files
  "${_native}"
  "${_dir}/contract_multi.plugin.c"
  "${_dir}/contract_multi.plugin.h"
  "${_dir}/contract_multi.plugin_client.c"
  "${_dir}/contract_multi.plugin_client.h"
  "${_dir}/contract_multi.component.c"
  "${_dir}/contract_multi.component.h")
function(_render)
  execute_process(
    COMMAND "${IDLC}" "${SCHEMA}"
      --lang c --output "${_native}" --artifact-name contract_multi
      --artifacts "component,plugin" --component ComponentOps.MathProvider
      --artifact-version 1.2.3
    RESULT_VARIABLE _status ERROR_VARIABLE _stderr)
  if(NOT _status EQUAL 0)
    message(FATAL_ERROR "Canonical staged PLUGIN+COMPONENT failed: ${_stderr}")
  endif()
endfunction()
_render()
set(_hashes)
foreach(_path IN LISTS _files)
  if(NOT EXISTS "${_path}")
    message(FATAL_ERROR "Transaction omitted output: ${_path}")
  endif()
  file(SHA256 "${_path}" _hash)
  list(APPEND _hashes "${_hash}")
endforeach()
file(READ "${_dir}/contract_multi.component.h" _header)
file(READ "${_dir}/contract_multi.component.c" _source)
file(READ "${_dir}/contract_multi.plugin.h" _plugin)
foreach(_token
  "CMETA_INTERFACE(" "FR2" "invoke_Add" "invoke_Multiply"
  "MathRequest_native_cmeta_type" "MathResponse_native_cmeta_type"
  "CMETA_RESULT_VALUE")
  string(FIND "${_header}" "${_token}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "Missing native FR2 method: ${_token}")
  endif()
endforeach()
foreach(_token
  "CMETA_IMPLEMENTS(" "salts_component_provider_binding"
  ".invoke_Add =" ".invoke_Multiply =" "CMETA_PLUGIN_DECLARE(")
  string(FIND "${_source}" "${_token}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "Missing Provider binding: ${_token}")
  endif()
endforeach()
string(FIND "${_plugin}" "databind_12_ComponentOps_4_Calc_3_Add" _pos)
if(_pos EQUAL -1)
  message(FATAL_ERROR "Canonical PLUGIN native Service header is missing")
endif()
# Repeated identical selection reproduces all seven files byte-for-byte.
_render()
set(_index 0)
foreach(_path IN LISTS _files)
  file(SHA256 "${_path}" _now)
  list(GET _hashes ${_index} _old)
  if(NOT _now STREQUAL _old)
    message(FATAL_ERROR "Non-deterministic generated output: ${_path}")
  endif()
  math(EXPR _index "${_index}+1")
endforeach()

# The PLUGIN renderer can admit a real typed error, but the bounded FR2
# ComponentProvider must reject it. Roll back *all* staged outputs.
set(_negative "${WORK_DIR}/typed-error")
file(MAKE_DIRECTORY "${_negative}")
set(_neg_native "${_negative}/error_native.h")
set(_neg_component "${_negative}/error.component.c")
file(WRITE "${_neg_native}" "preexisting-native\n")
file(WRITE "${_neg_component}" "preexisting-component\n")
execute_process(
  COMMAND "${IDLC}" "${TYPED_ERROR_SCHEMA}"
    --lang c --output "${_neg_native}"
    --artifact-name error --artifacts "plugin,component"
    --component ErrorPlugin.StorePlugin --artifact-version 1.0.0
  RESULT_VARIABLE _failed ERROR_VARIABLE _error)
if(_failed EQUAL 0)
  message(FATAL_ERROR "Typed-error COMPONENT silently admitted")
endif()
file(READ "${_neg_native}" _native_after)
file(READ "${_neg_component}" _component_after)
if(NOT _native_after STREQUAL "preexisting-native\n" OR
   NOT _component_after STREQUAL "preexisting-component\n")
  message(FATAL_ERROR "Rollback overwrote an existing caller file")
endif()
foreach(_suffix plugin.c plugin.h plugin_client.c plugin_client.h component.h)
  if(EXISTS "${_negative}/error.${_suffix}")
    message(FATAL_ERROR "Rejected COMPONENT left partial output: ${_suffix}")
  endif()
endforeach()

# Selecting explicit Binary is a negative control, never a typed ABI fallback.
set(_reject "${WORK_DIR}/binary-reject")
file(MAKE_DIRECTORY "${_reject}")
execute_process(
  COMMAND "${IDLC}" "${SCHEMA}" --lang c
    --output "${_reject}/reject_native.h"
    --artifact-name reject --artifacts "plugin,component"
    --component ComponentOps.MathProvider
    --artifact-version 1.0.0 --binary-codec
  RESULT_VARIABLE _binary)
if(_binary EQUAL 0 OR EXISTS "${_reject}/reject_native.h")
  message(FATAL_ERROR "COMPONENT selected Binary fallback")
endif()
message(STATUS "Atomic Contract-only PLUGIN+COMPONENT FR2: PASS")
