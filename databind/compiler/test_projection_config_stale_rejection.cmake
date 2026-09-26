cmake_minimum_required(VERSION 3.27)

foreach(REQUIRED IN ITEMS DATABINDC SCHEMA CONFIG WORK_DIR)
  if(NOT DEFINED ${REQUIRED} OR "${${REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "${REQUIRED} is required")
  endif()
endforeach()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(NATIVE "${WORK_DIR}/stale_native.h")
set(HTTP "${WORK_DIR}/stale.http.h")
file(REMOVE "${NATIVE}" "${HTTP}")

execute_process(
  COMMAND "${DATABINDC}"
          "${SCHEMA}"
          --lang c
          --output "${NATIVE}"
          --transports http
          --artifact-name stale
          --projection-config "${CONFIG}"
  RESULT_VARIABLE RESULT
  OUTPUT_VARIABLE STDOUT_TEXT
  ERROR_VARIABLE STDERR_TEXT)

if(RESULT EQUAL 0)
  message(FATAL_ERROR
          "stale projection config unexpectedly succeeded\n${STDOUT_TEXT}\n${STDERR_TEXT}")
endif()

if(EXISTS "${HTTP}")
  message(FATAL_ERROR
          "failed stale projection published HTTP artifact: ${HTTP}")
endif()

message(STATUS "stale projection config rejected before HTTP artifact publication")
