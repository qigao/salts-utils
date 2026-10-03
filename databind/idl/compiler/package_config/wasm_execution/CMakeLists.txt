cmake_minimum_required(VERSION 3.20)
project(databind_wasm_execution_consumer C)

if(NOT DEFINED SALTS_UTILS_ROOT OR SALTS_UTILS_ROOT STREQUAL "")
  message(FATAL_ERROR "SALTS_UTILS_ROOT is required")
endif()
if(NOT DEFINED ENV{SALTS_ROOT} OR "$ENV{SALTS_ROOT}" STREQUAL "")
  message(FATAL_ERROR "SALTS_ROOT is required")
endif()
if(NOT DEFINED ENV{TURBOWASM_ROOT} OR "$ENV{TURBOWASM_ROOT}" STREQUAL "")
  message(FATAL_ERROR "TURBOWASM_ROOT is required")
endif()

file(TO_CMAKE_PATH "${SALTS_UTILS_ROOT}" _salts_utils_root)
file(TO_CMAKE_PATH "$ENV{TURBOWASM_ROOT}" _turbowasm_root)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(SaltsUtils CONFIG REQUIRED
  PATHS "${_salts_utils_root}"
  NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
find_package(TurboWasm CONFIG REQUIRED
  PATHS "${_turbowasm_root}"
  NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

if(NOT TARGET TurboWasm::Component)
  message(FATAL_ERROR "released TurboWasm package is missing TurboWasm::Component")
endif()

set(_core "${CMAKE_CURRENT_BINARY_DIR}/wasm_execution_core.wasm")
execute_process(
  COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/write_core.py"
          "${_core}"
  RESULT_VARIABLE _core_status)
if(NOT _core_status EQUAL 0 OR NOT EXISTS "${_core}")
  message(FATAL_ERROR "failed to generate WASM runtime core fixture")
endif()

salts_idl_target(
  TARGET wasm_execution_fixture
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/wasm_execution.schema"
  COMPONENT "WasmRuntime.Calculator"
  VERSION "1.0.0"
  ARTIFACT_NAME "wasm_execution"
  ARTIFACTS PLUGIN WASM
  WASM_CORE_MODULE "${_core}"
  SOURCES
    business.c)

file(GENERATE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/wasm_execution_component_path.h"
  CONTENT
    "#define WASM_EXECUTION_COMPONENT_PATH \"${wasm_execution_fixture_WASM_ARTIFACT}\"\n")

add_executable(wasm_execution_consumer main.c)
target_compile_features(wasm_execution_consumer PRIVATE c_std_11)
target_include_directories(wasm_execution_consumer PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(wasm_execution_consumer PRIVATE
  wasm_execution_fixture_wasm)
add_dependencies(wasm_execution_consumer
  wasm_execution_fixture_plugin)
