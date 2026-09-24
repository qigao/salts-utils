if(NOT DEFINED DATABINDC OR NOT DEFINED SCHEMA OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "DATABINDC, SCHEMA and OUTPUT are required")
endif()

file(REMOVE "${OUTPUT}")

execute_process(
  COMMAND "${DATABINDC}" "${SCHEMA}" --lang c --output "${OUTPUT}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr)

if(result EQUAL 0)
  file(REMOVE "${OUTPUT}")
  message(FATAL_ERROR "nullable codegen unexpectedly succeeded")
endif()

string(FIND "${stderr}" "has no native/projection lowering yet" diagnostic_pos)
if(diagnostic_pos EQUAL -1)
  file(REMOVE "${OUTPUT}")
  message(FATAL_ERROR
          "nullable codegen failed for the wrong reason\nstdout:\n${stdout}\nstderr:\n${stderr}")
endif()

file(REMOVE "${OUTPUT}")
