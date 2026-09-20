cmake_minimum_required(VERSION 3.27)

if(NOT DEFINED DATABIND_SOURCE_ROOT OR "${DATABIND_SOURCE_ROOT}" STREQUAL "")
  message(FATAL_ERROR "DATABIND_SOURCE_ROOT is required")
endif()
if(NOT IS_DIRECTORY "${DATABIND_SOURCE_ROOT}")
  message(FATAL_ERROR "DATABIND_SOURCE_ROOT is not a directory: ${DATABIND_SOURCE_ROOT}")
endif()
file(REAL_PATH "${DATABIND_SOURCE_ROOT}" DATABIND_SOURCE_ROOT)

string(CONCAT FORBIDDEN_TARGET "Salts::C" "Bind")
string(CONCAT FORBIDDEN_INCLUDE "<c" "bind/")
string(CONCAT FORBIDDEN_MACRO "C" "BIND_")
string(CONCAT FORBIDDEN_SYMBOL "c" "bind_")
string(CONCAT FORBIDDEN_REVERSE_KIND "tbe_typed_kind_from_" "cmeta_data")
string(CONCAT FORBIDDEN_REVERSE_GRAPH "tbe_typed_cmeta_graph_" "validate")
string(CONCAT FORBIDDEN_REVERSE_RECORD "typed_cmeta_validate_" "record")
string(CONCAT FORBIDDEN_PARENT_TBE_ROOT "$" "{CMAKE_SOURCE_DIR}/tbe/")
string(CONCAT FORBIDDEN_PARENT_RE2C "$" "{RE2C_EXECUTABLE}")
string(CONCAT FORBIDDEN_PARENT_LEMPAR "$" "{LEMPAR}")
string(CONCAT FORBIDDEN_PARENT_LEMON_TARGET "$<TARGET_FILE:" "lemon>")

file(GLOB_RECURSE POLICY_FILES LIST_DIRECTORIES FALSE
  "${DATABIND_SOURCE_ROOT}/CMakeLists.txt"
  "${DATABIND_SOURCE_ROOT}/*.cmake"
  "${DATABIND_SOURCE_ROOT}/*.c"
  "${DATABIND_SOURCE_ROOT}/*.h"
  "${DATABIND_SOURCE_ROOT}/*.cpp"
  "${DATABIND_SOURCE_ROOT}/*.hpp"
  "${DATABIND_SOURCE_ROOT}/*.mustache")

foreach(FILE_PATH IN LISTS POLICY_FILES)
  file(RELATIVE_PATH RELATIVE_FILE_PATH "${DATABIND_SOURCE_ROOT}" "${FILE_PATH}")
  if(FILE_PATH STREQUAL CMAKE_CURRENT_LIST_FILE OR
     RELATIVE_FILE_PATH MATCHES "(^|/)(build[^/]*|install|bin|out|cmake-build-[^/]*|\\.vcpkg_installed|vcpkg_installed|conan-cache)/")
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

file(GLOB_RECURSE DATABIND_CMAKE_FILES LIST_DIRECTORIES FALSE
  "${DATABIND_SOURCE_ROOT}/CMakeLists.txt"
  "${DATABIND_SOURCE_ROOT}/*.cmake")

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
          "SALTS_UTILS_HOST_LEMON_EXECUTABLE"
          "${FORBIDDEN_PARENT_RE2C}"
          "${FORBIDDEN_PARENT_LEMPAR}"
          "${FORBIDDEN_PARENT_LEMON_TARGET}"
          "set(lemon_depends lemon)")
    string(FIND "${CONTENT}" "${FORBIDDEN_HELPER}" POSITION)
    if(NOT POSITION EQUAL -1)
      message(FATAL_ERROR
              "DataBind build-helper ownership violation: ${FILE_PATH}: ${FORBIDDEN_HELPER}")
    endif()
  endforeach()
endforeach()

foreach(REQUIRED_BUILD_MODULE IN ITEMS
        "cmake/DataBindTools.cmake"
        "cmake/DataBindBuild.cmake")
  if(NOT EXISTS "${DATABIND_SOURCE_ROOT}/${REQUIRED_BUILD_MODULE}")
    message(FATAL_ERROR "DataBind build module is missing: ${REQUIRED_BUILD_MODULE}")
  endif()
endforeach()

file(READ "${DATABIND_SOURCE_ROOT}/cmake/DataBindTools.cmake" DATABIND_TOOL_INPUTS)
foreach(REQUIRED_TOOL_INPUT IN ITEMS
        "DATABIND_RE2C_EXECUTABLE"
        "DATABIND_LEMPAR"
        "DATABIND_HOST_LEMON_EXECUTABLE"
        "DATABIND_LEMON_TARGET"
        "DATABIND_LEMON_COMMAND"
        "DATABIND_LEMON_DEPENDS")
  string(FIND "${DATABIND_TOOL_INPUTS}"
              "${REQUIRED_TOOL_INPUT}"
              TOOL_INPUT_POSITION)
  if(TOOL_INPUT_POSITION EQUAL -1)
    message(FATAL_ERROR
            "DataBind tool-input contract is missing: ${REQUIRED_TOOL_INPUT}")
  endif()
endforeach()

file(READ "${DATABIND_SOURCE_ROOT}/cmake/DataBindBuild.cmake" DATABIND_BUILD_HELPERS)
foreach(REQUIRED_BUILD_INPUT IN ITEMS
        "DATABIND_RE2C_EXECUTABLE"
        "DATABIND_LEMPAR"
        "DATABIND_LEMON_COMMAND"
        "DATABIND_LEMON_DEPENDS")
  string(FIND "${DATABIND_BUILD_HELPERS}"
              "${REQUIRED_BUILD_INPUT}"
              BUILD_INPUT_POSITION)
  if(BUILD_INPUT_POSITION EQUAL -1)
    message(FATAL_ERROR
            "DataBind build helper is missing explicit tool input: ${REQUIRED_BUILD_INPUT}")
  endif()
endforeach()

set(DATABIND_MONOCYPHER_ROOT "${DATABIND_SOURCE_ROOT}/vendor/monocypher")
foreach(REQUIRED_MONOCYPHER_FILE IN ITEMS CMakeLists.txt monocypher.c monocypher.h)
  if(NOT EXISTS "${DATABIND_MONOCYPHER_ROOT}/${REQUIRED_MONOCYPHER_FILE}")
    message(FATAL_ERROR
            "DataBind private Monocypher ownership is incomplete: ${REQUIRED_MONOCYPHER_FILE}")
  endif()
endforeach()

file(READ "${DATABIND_SOURCE_ROOT}/data_bind/CMakeLists.txt" DATABIND_RUNTIME_CMAKE)
string(FIND "${DATABIND_RUNTIME_CMAKE}" "databind_monocypher"
            DATABIND_MONOCYPHER_TARGET_POSITION)
if(DATABIND_MONOCYPHER_TARGET_POSITION EQUAL -1)
  message(FATAL_ERROR
          "DataBind runtime no longer links its private Monocypher target")
endif()

file(READ "${DATABIND_MONOCYPHER_ROOT}/CMakeLists.txt" DATABIND_MONOCYPHER_CMAKE)
string(FIND "${DATABIND_MONOCYPHER_CMAKE}"
            "add_library(databind_monocypher STATIC"
            DATABIND_MONOCYPHER_OWNER_POSITION)
if(DATABIND_MONOCYPHER_OWNER_POSITION EQUAL -1)
  message(FATAL_ERROR
          "DataBind private Monocypher target ownership is missing")
endif()

foreach(LICENSE_FILE IN ITEMS monocypher.c monocypher.h)
  file(READ "${DATABIND_MONOCYPHER_ROOT}/${LICENSE_FILE}" LICENSE_CONTENT)
  string(FIND "${LICENSE_CONTENT}"
              "SPDX-License-Identifier: BSD-2-Clause OR CC0-1.0"
              LICENSE_POSITION)
  if(LICENSE_POSITION EQUAL -1)
    message(FATAL_ERROR
            "Monocypher license provenance missing from ${LICENSE_FILE}")
  endif()
endforeach()

foreach(REQUIRED_PACKAGE_ASSET IN ITEMS
        "cmake/DataBindConfig.cmake.in"
        "cmake/DataBindPackage.cmake"
        "tests/package_config/CMakeLists.txt"
        "tests/package_config/databind_repeated_find/CMakeLists.txt"
        "tests/package_config/databind_adapters/CMakeLists.txt")
  if(NOT EXISTS "${DATABIND_SOURCE_ROOT}/${REQUIRED_PACKAGE_ASSET}")
    message(FATAL_ERROR
            "DataBind package ownership is incomplete: ${REQUIRED_PACKAGE_ASSET}")
  endif()
endforeach()

file(READ "${DATABIND_SOURCE_ROOT}/cmake/DataBindPackage.cmake"
          DATABIND_PACKAGE_MODULE)
foreach(REQUIRED_PACKAGE_FRAGMENT IN ITEMS
        "configure_package_config_file("
        "export("
        "DataBindTargets"
        "DataBindAdapterTargets"
        "COMPONENT DataBind")
  string(FIND "${DATABIND_PACKAGE_MODULE}"
              "${REQUIRED_PACKAGE_FRAGMENT}"
              PACKAGE_FRAGMENT_POSITION)
  if(PACKAGE_FRAGMENT_POSITION EQUAL -1)
    message(FATAL_ERROR
            "DataBind package module missing ownership fragment: ${REQUIRED_PACKAGE_FRAGMENT}")
  endif()
endforeach()

message(STATUS "DataBind internal dependency contract passed")
