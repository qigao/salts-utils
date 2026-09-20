string(CONCAT FORBIDDEN_TARGET "Salts::C" "Bind")
string(CONCAT FORBIDDEN_INCLUDE "<c" "bind/")
string(CONCAT FORBIDDEN_MACRO "C" "BIND_")
string(CONCAT FORBIDDEN_SYMBOL "c" "bind_")
string(CONCAT FORBIDDEN_REVERSE_KIND "tbe_typed_kind_from_" "cmeta_data")
string(CONCAT FORBIDDEN_REVERSE_GRAPH "tbe_typed_cmeta_graph_" "validate")
string(CONCAT FORBIDDEN_REVERSE_RECORD "typed_cmeta_validate_" "record")
string(CONCAT FORBIDDEN_PARENT_TBE_ROOT "$" "{CMAKE_SOURCE_DIR}/tbe/")

file(GLOB_RECURSE POLICY_FILES LIST_DIRECTORIES FALSE
  "${PROJECT_SOURCE_DIR}/CMakeLists.txt"
  "${PROJECT_SOURCE_DIR}/*.cmake"
  "${PROJECT_SOURCE_DIR}/*.c"
  "${PROJECT_SOURCE_DIR}/*.h"
  "${PROJECT_SOURCE_DIR}/*.cpp"
  "${PROJECT_SOURCE_DIR}/*.hpp"
  "${PROJECT_SOURCE_DIR}/*.mustache")

foreach(FILE_PATH IN LISTS POLICY_FILES)
  file(RELATIVE_PATH RELATIVE_FILE_PATH "${PROJECT_SOURCE_DIR}" "${FILE_PATH}")
  if(FILE_PATH STREQUAL CMAKE_CURRENT_LIST_FILE OR
     RELATIVE_FILE_PATH MATCHES "^(salts|vcpkg)/" OR
     RELATIVE_FILE_PATH MATCHES "(^|/)(build[^/]*|install|bin|out|cmake-build-[^/]*|\\.vcpkg_installed|vcpkg_installed|conan-cache)/" OR
     RELATIVE_FILE_PATH MATCHES "^docs/superpowers/")
    continue()
  endif()
  file(READ "${FILE_PATH}" CONTENT)
  foreach(FORBIDDEN IN ITEMS "${FORBIDDEN_TARGET}" "${FORBIDDEN_INCLUDE}"
                             "${FORBIDDEN_MACRO}" "${FORBIDDEN_SYMBOL}"
                             "${FORBIDDEN_REVERSE_KIND}"
                             "${FORBIDDEN_REVERSE_GRAPH}"
                             "${FORBIDDEN_REVERSE_RECORD}"
                             "${FORBIDDEN_PARENT_TBE_ROOT}")
    string(FIND "${CONTENT}" "${FORBIDDEN}" POSITION)
    if(NOT POSITION EQUAL -1)
      message(FATAL_ERROR "DataBind-only dependency violation: ${FILE_PATH}")
    endif()
  endforeach()
endforeach()


set(DATABIND_CMAKE_ROOT "${PROJECT_SOURCE_DIR}/tbe")
file(GLOB_RECURSE DATABIND_CMAKE_FILES LIST_DIRECTORIES FALSE
  "${DATABIND_CMAKE_ROOT}/CMakeLists.txt"
  "${DATABIND_CMAKE_ROOT}/*.cmake")

foreach(FILE_PATH IN LISTS DATABIND_CMAKE_FILES)
  if(FILE_PATH STREQUAL CMAKE_CURRENT_LIST_FILE)
    continue()
  endif()
  file(READ "${FILE_PATH}" CONTENT)
  foreach(FORBIDDEN_HELPER IN ITEMS
          "cmake_config_target("
          "cmake_add_grammar("
          "cmake_add_source("
          "cmake_add_executable("
          "cmake_add_test("
          "cmake_add_benchmark("
          "SALTS_UTILS_HOST_LEMON_EXECUTABLE")
    string(FIND "${CONTENT}" "${FORBIDDEN_HELPER}" POSITION)
    if(NOT POSITION EQUAL -1)
      message(FATAL_ERROR
              "DataBind build-helper ownership violation: ${FILE_PATH}: ${FORBIDDEN_HELPER}")
    endif()
  endforeach()
endforeach()

if(NOT EXISTS "${DATABIND_CMAKE_ROOT}/cmake/DataBindBuild.cmake")
  message(FATAL_ERROR "DataBind-owned build helper module is missing")
endif()
file(READ "${DATABIND_CMAKE_ROOT}/cmake/DataBindBuild.cmake" DATABIND_BUILD_HELPERS)
string(FIND "${DATABIND_BUILD_HELPERS}"
            "DATABIND_HOST_LEMON_EXECUTABLE"
            DATABIND_HOST_LEMON_POSITION)
if(DATABIND_HOST_LEMON_POSITION EQUAL -1)
  message(FATAL_ERROR
          "DataBind build helper does not own the host Lemon configuration")
endif()
