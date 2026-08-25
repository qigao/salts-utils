cmake_minimum_required(VERSION 3.20)

foreach(required_variable IN ITEMS
    TP_MAIN_BINARY_DIR TP_FIXTURE_SOURCE_DIR TP_PROBE_MODULE TP_GENERATOR)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${required_variable}")
  endif()
endforeach()

set(fixture_binary_dir
  "${TP_MAIN_BINARY_DIR}/cmake/turboutils_cbind_v2_missing_surface")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --fresh
    -S "${TP_FIXTURE_SOURCE_DIR}"
    -B "${fixture_binary_dir}"
    -G "${TP_GENERATOR}"
    "-DTP_PROBE_MODULE=${TP_PROBE_MODULE}"
  RESULT_VARIABLE configure_result
  OUTPUT_VARIABLE configure_output
  ERROR_VARIABLE configure_error)
set(configure_log "${configure_output}\n${configure_error}")

if(configure_result EQUAL 0)
  message(FATAL_ERROR
    "The intentionally incomplete TurboUtils fixture unexpectedly configured")
endif()
if(NOT configure_log MATCHES "requires TurboUtils fixed-width integer" OR
   NOT configure_log MATCHES "enum adapter APIs" OR
   NOT configure_log MATCHES "canonical UUID Core metadata" OR
   NOT configure_log MATCHES "Rebuild and" OR
   NOT configure_log MATCHES "install TurboUtils")
  message(FATAL_ERROR
    "The incomplete TurboUtils fixture did not produce the actionable dependency-floor diagnostic:\n"
    "${configure_log}")
endif()

message(STATUS
  "The incomplete TurboUtils fixture was rejected with the required actionable diagnostic")
