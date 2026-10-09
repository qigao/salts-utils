cmake_minimum_required(VERSION 3.20)
project(databind_wasm_execution_consumer C)

enable_testing()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(NOT IS_DIRECTORY "$ENV{SALTS_UTILS_ROOT}")
  message(FATAL_ERROR "SALTS_UTILS_ROOT must name the installed SaltsUtils SDK")
endif()
if(NOT DEFINED ENV{SALTS_ROOT} OR "$ENV{SALTS_ROOT}" STREQUAL "")
  message(FATAL_ERROR "SALTS_ROOT is required")
endif()
if(NOT DEFINED ENV{TURBOWASM_ROOT} OR "$ENV{TURBOWASM_ROOT}" STREQUAL "")
  message(FATAL_ERROR "TURBOWASM_ROOT is required")
endif()

file(TO_CMAKE_PATH "$ENV{SALTS_UTILS_ROOT}" _salts_utils_root)
file(TO_CMAKE_PATH "$ENV{TURBOWASM_ROOT}" _turbowasm_root)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
unset(SaltsUtils_DIR CACHE)
find_package(SaltsUtils CONFIG REQUIRED
  PATHS "${_salts_utils_root}"
  NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
unset(TurboWasm_DIR CACHE)
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
  BINARY_CODEC
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
add_test(NAME wasm_execution_consumer COMMAND wasm_execution_consumer)
