file(READ "${PROJECT_SOURCE_DIR}/tbe/tbe_compiler/CMakeLists.txt" COMPILER_CMAKE)

string(FIND "${COMPILER_CMAKE}"
  "set(DATABIND_COMPILER_TOOLING_TARGET" TOOLING_TARGET_POS)
if(TOOLING_TARGET_POS EQUAL -1)
  message(FATAL_ERROR
    "DataBind compiler must declare an explicit build-tool dependency boundary")
endif()

string(REGEX MATCH
  "cmake_add_executable\\(tbe_compiler[\\s\\S]*?\\)"
  COMPILER_BLOCK "${COMPILER_CMAKE}")
if(COMPILER_BLOCK STREQUAL "")
  message(FATAL_ERROR "Unable to locate tbe_compiler target definition")
endif()

foreach(FORBIDDEN IN ITEMS "Salts::CmdParser" "Salts::Mustache")
  string(FIND "${COMPILER_BLOCK}" "${FORBIDDEN}" POS)
  if(NOT POS EQUAL -1)
    message(FATAL_ERROR
      "tbe_compiler directly links SaltsUtils tooling instead of the explicit build-tool boundary: ${FORBIDDEN}")
  endif()
endforeach()

file(READ "${PROJECT_SOURCE_DIR}/cmake/DataBindConfig.cmake.in" DATABIND_CONFIG)
foreach(FORBIDDEN IN ITEMS "SaltsUtils" "SALTS_UTILS_ROOT")
  string(FIND "${DATABIND_CONFIG}" "${FORBIDDEN}" POS)
  if(NOT POS EQUAL -1)
    message(FATAL_ERROR
      "Base DataBind package config must not resolve SaltsUtils compiler tooling: ${FORBIDDEN}")
  endif()
endforeach()
