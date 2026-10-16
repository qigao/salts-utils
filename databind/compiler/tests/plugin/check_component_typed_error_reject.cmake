# Verify a real canonical typed-error Service cannot be projected as a
# simple borrowed FR2 method by silently dropping its error envelope.
if(NOT DEFINED TOOL OR NOT DEFINED SCHEMA OR NOT DEFINED OUT_DIR)
  message(FATAL_ERROR "typed-error reject test requires TOOL/SCHEMA/OUT_DIR")
endif()
file(MAKE_DIRECTORY "${OUT_DIR}")
set(_h "${OUT_DIR}/rejected.component.h")
set(_c "${OUT_DIR}/rejected.component.c")
file(REMOVE "${_h}" "${_c}")
execute_process(
  COMMAND "${TOOL}"
    "${SCHEMA}" "ErrorPlugin.StorePlugin"
    "error.plugin.h" "${_h}" "${_c}" "--verify-typed-error-reject"
  RESULT_VARIABLE _status
  OUTPUT_VARIABLE _stdout
  ERROR_VARIABLE _stderr)
if(NOT _status EQUAL 0)
  message(FATAL_ERROR "Canonical typed-error negative verifier failed: ${_stderr}")
endif()
if(EXISTS "${_h}" OR EXISTS "${_c}")
  message(FATAL_ERROR "Rejected typed-error created a partial artifact")
endif()
if(NOT _stderr MATCHES "Verified canonical typed-error rejection")
  message(FATAL_ERROR "No evidence of parsed typed-error/IR admission rejection: ${_stderr}")
endif()
message(STATUS "Canonical typed-error provider rejected with zero output")
