# =============================================================================
# Executable
# =============================================================================

set(TBE_COMPILER_TEMPLATE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_cbind_source.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_guest_adapter.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_lua_bind.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_structs.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_typed_source.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/cpp_types.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/go_types.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/python_dataclass.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/rfl_types.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/rust_structs.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/ts_types.mustache")

cmake_add_executable(tbe_compiler
  SOURCES main.c compiler_core.c mustache_helpers.c ${TBE_COMPILER_TEMPLATE_FILES}
  LIBS TurboParser::TbeSchema TurboParser::Parser TurboParser::Mustache
  FOLDER "tbe/tbe_compiler")
set_property(TARGET tbe_compiler APPEND PROPERTY LINK_DEPENDS
             ${TBE_COMPILER_TEMPLATE_FILES})

function(tbe_copy_turbonet_runtime_deps target_name)
  if(WIN32)
    set(runtime_deps
        $<TARGET_FILE:TurboParser::Parser>
        $<TARGET_FILE:TurboParser::Mustache>
        $<TARGET_FILE:TurboUtils::Core>)
    if(ARGC GREATER 1 AND ARGV1 STREQUAL "WITH_DATABIND")
      list(APPEND runtime_deps $<TARGET_FILE:TurboParser::DataBind>)
    endif()
 
    add_custom_command(
      TARGET ${target_name}
      POST_BUILD
      COMMAND
        ${CMAKE_COMMAND} -E copy_if_different
        ${runtime_deps}
        $<TARGET_FILE_DIR:${target_name}>
      COMMENT "Copying TurboNet runtime DLLs for ${target_name}"
      VERBATIM)
  endif()
endfunction()

function(tbe_copy_turboutils_core_runtime_dep target_name)
  if(WIN32)
    add_custom_command(
      TARGET ${target_name}
      POST_BUILD
      COMMAND
        ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:TurboUtils::Core>
        $<TARGET_FILE_DIR:${target_name}>
      COMMENT "Copying TurboUtils Core runtime DLL for ${target_name}"
      VERBATIM)
  endif()
endfunction()

tbe_copy_turbonet_runtime_deps(tbe_compiler)

# =============================================================================
# Copy templates next to executable for easy access
# =============================================================================

add_custom_command(
  TARGET tbe_compiler
  POST_BUILD
  COMMAND
    ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/templates
    $<TARGET_FILE_DIR:tbe_compiler>/templates
  COMMENT "Copying template files to output directory")

install(
  TARGETS tbe_compiler
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
  DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/templates"
  DESTINATION ${CMAKE_INSTALL_BINDIR})

# =============================================================================
# Tests
# =============================================================================

cmake_add_test(test_tbe_compiler
  SOURCES test_tbe_compiler.c compiler_core.c mustache_helpers.c
  LIBS TurboParser::TbeSchema TurboUtils::TinyTest TurboParser::Mustache
  DEFS
    SCHEMA_EXAMPLE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/example.schema"
    C_STRUCT_TEMPLATE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/templates/c_structs.mustache"
    CPP_TYPES_TEMPLATE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/templates/cpp_types.mustache"
    GO_TYPES_TEMPLATE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/templates/go_types.mustache"
    PYTHON_DATACLASS_TEMPLATE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/templates/python_dataclass.mustache"
    RUST_STRUCTS_TEMPLATE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/templates/rust_structs.mustache"
    TS_TYPES_TEMPLATE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/templates/ts_types.mustache"
    TBE_COMPILER_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  FOLDER "tbe/tbe_compiler")
tbe_copy_turbonet_runtime_deps(test_tbe_compiler)

set(TBE_TYPED_ORDER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(TBE_TYPED_ORDER_HEADER "${TBE_TYPED_ORDER_GENERATED_DIR}/typed_order.h")
set(TBE_TYPED_ORDER_SOURCE "${TBE_TYPED_ORDER_GENERATED_DIR}/typed_order.c")
set(TBE_TYPED_ORDER_GUEST_SOURCE "${TBE_TYPED_ORDER_GENERATED_DIR}/typed_order_guest.c")
set(TBE_TYPED_ORDER_LUA_SOURCE "${TBE_TYPED_ORDER_GENERATED_DIR}/typed_order_lua.c")

add_custom_command(
  OUTPUT
    "${TBE_TYPED_ORDER_HEADER}"
    "${TBE_TYPED_ORDER_SOURCE}"
    "${TBE_TYPED_ORDER_GUEST_SOURCE}"
    "${TBE_TYPED_ORDER_LUA_SOURCE}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${TBE_TYPED_ORDER_GENERATED_DIR}"
  COMMAND
    $<TARGET_FILE:tbe_compiler>
    "${CMAKE_CURRENT_SOURCE_DIR}/test_typed_order.schema"
    --lang c
    --output "${TBE_TYPED_ORDER_HEADER}"
    --source-output "${TBE_TYPED_ORDER_SOURCE}"
    --guest-output "${TBE_TYPED_ORDER_GUEST_SOURCE}"
    --lua-output "${TBE_TYPED_ORDER_LUA_SOURCE}"
  DEPENDS
    tbe_compiler
    "${CMAKE_CURRENT_SOURCE_DIR}/test_typed_order.schema"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_structs.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_typed_source.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_guest_adapter.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_lua_bind.mustache"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "Generating strong typed Order C bindings"
  VERBATIM)

add_library(test_tbe_typed_order_static STATIC "${TBE_TYPED_ORDER_SOURCE}")
target_include_directories(test_tbe_typed_order_static
  PUBLIC "${TBE_TYPED_ORDER_GENERATED_DIR}")
target_link_libraries(test_tbe_typed_order_static PUBLIC TurboParser::DataBind)
set_target_properties(test_tbe_typed_order_static PROPERTIES FOLDER "tbe/tbe_compiler")

add_library(test_tbe_typed_order_shared SHARED "${TBE_TYPED_ORDER_SOURCE}")
target_compile_definitions(test_tbe_typed_order_shared PRIVATE TBE_GENERATED_BUILD_SHARED)
target_include_directories(test_tbe_typed_order_shared
  PUBLIC "${TBE_TYPED_ORDER_GENERATED_DIR}")
target_link_libraries(test_tbe_typed_order_shared PUBLIC TurboParser::DataBind)
set_target_properties(test_tbe_typed_order_shared PROPERTIES FOLDER "tbe/tbe_compiler")

cmake_add_test(test_tbe_typed_order
  SOURCES test_typed_order.c "${TBE_TYPED_ORDER_GUEST_SOURCE}"
  LIBS test_tbe_typed_order_static TurboUtils::TinyTest
  INCLUDES "${TBE_TYPED_ORDER_GENERATED_DIR}"
  FOLDER "tbe/tbe_compiler")
tbe_copy_turbonet_runtime_deps(test_tbe_typed_order WITH_DATABIND)

cmake_add_test(test_tbe_typed_order_cpp
  SOURCES test_typed_order_cpp.cpp
  LIBS test_tbe_typed_order_shared TurboUtils::TinyTest
  INCLUDES "${TBE_TYPED_ORDER_GENERATED_DIR}"
  FOLDER "tbe/tbe_compiler")
target_compile_definitions(test_tbe_typed_order_cpp PRIVATE TBE_GENERATED_USE_SHARED)
set_target_properties(test_tbe_typed_order_cpp PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON)
tbe_copy_turbonet_runtime_deps(test_tbe_typed_order_cpp WITH_DATABIND)

set(TBE_CBIND_SIDECAR_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_cbind_sidecar")
set(TBE_CBIND_SIDECAR_HEADER "${TBE_CBIND_SIDECAR_GENERATED_DIR}/cbind_sidecar.h")
set(TBE_CBIND_SIDECAR_TYPED_SOURCE "${TBE_CBIND_SIDECAR_GENERATED_DIR}/cbind_sidecar.c")
set(TBE_CBIND_SIDECAR_SOURCE "${TBE_CBIND_SIDECAR_GENERATED_DIR}/cbind_sidecar_cbind.c")

add_custom_command(
  OUTPUT
    "${TBE_CBIND_SIDECAR_HEADER}"
    "${TBE_CBIND_SIDECAR_TYPED_SOURCE}"
    "${TBE_CBIND_SIDECAR_SOURCE}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${TBE_CBIND_SIDECAR_GENERATED_DIR}"
  COMMAND
    $<TARGET_FILE:tbe_compiler>
    "${CMAKE_CURRENT_SOURCE_DIR}/test_cbind_sidecar.schema"
    --lang c
    --output "${TBE_CBIND_SIDECAR_HEADER}"
    --source-output "${TBE_CBIND_SIDECAR_TYPED_SOURCE}"
    --cbind-output "${TBE_CBIND_SIDECAR_SOURCE}"
  DEPENDS
    tbe_compiler
    "${CMAKE_CURRENT_SOURCE_DIR}/test_cbind_sidecar.schema"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_structs.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_typed_source.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_cbind_source.mustache"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "Generating typed CBind sidecar test records"
  VERBATIM)

add_library(test_tbe_cbind_sidecar_static STATIC
  "${TBE_CBIND_SIDECAR_HEADER}"
  "${TBE_CBIND_SIDECAR_TYPED_SOURCE}"
  "${TBE_CBIND_SIDECAR_SOURCE}")
target_include_directories(test_tbe_cbind_sidecar_static
  PUBLIC "${TBE_CBIND_SIDECAR_GENERATED_DIR}")
target_link_libraries(test_tbe_cbind_sidecar_static
  PUBLIC TurboParser::DataBind TurboUtils::CBind)
set_target_properties(test_tbe_cbind_sidecar_static PROPERTIES FOLDER "tbe/tbe_compiler")

cmake_add_test(test_tbe_cbind_sidecar
  SOURCES test_cbind_sidecar.c
  LIBS test_tbe_cbind_sidecar_static json_parser TurboUtils::TinyTest
  INCLUDES "${TBE_CBIND_SIDECAR_GENERATED_DIR}"
  FOLDER "tbe/tbe_compiler")
tbe_copy_turbonet_runtime_deps(test_tbe_cbind_sidecar WITH_DATABIND)

cmake_add_test(test_tbe_cbind_sidecar_cpp
  SOURCES test_cbind_sidecar_cpp.cpp
  LIBS test_tbe_cbind_sidecar_static TurboUtils::TinyTest
  INCLUDES "${TBE_CBIND_SIDECAR_GENERATED_DIR}"
  FOLDER "tbe/tbe_compiler")
set_target_properties(test_tbe_cbind_sidecar_cpp PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON)
tbe_copy_turbonet_runtime_deps(test_tbe_cbind_sidecar_cpp WITH_DATABIND)

set(TBE_CBIND_STANDALONE_SCHEMA
    "${CMAKE_CURRENT_SOURCE_DIR}/test_cbind_standalone.schema")
set(TBE_CBIND_STANDALONE_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated_cbind_standalone")
set(TBE_CBIND_STANDALONE_HEADER
    "${TBE_CBIND_STANDALONE_GENERATED_DIR}/cbind_standalone.h")
set(TBE_CBIND_STANDALONE_SOURCE
    "${TBE_CBIND_STANDALONE_GENERATED_DIR}/cbind_standalone.c")

add_custom_command(
  OUTPUT
    "${TBE_CBIND_STANDALONE_HEADER}"
    "${TBE_CBIND_STANDALONE_SOURCE}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${TBE_CBIND_STANDALONE_GENERATED_DIR}"
  COMMAND
    $<TARGET_FILE:tbe_compiler>
    "${TBE_CBIND_STANDALONE_SCHEMA}"
    --lang c
    --output "${TBE_CBIND_STANDALONE_HEADER}"
    --cbind-output "${TBE_CBIND_STANDALONE_SOURCE}"
  DEPENDS
    tbe_compiler
    "${TBE_CBIND_STANDALONE_SCHEMA}"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_structs.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_cbind_source.mustache"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "Generating standalone owning-string CBind records"
  VERBATIM)

add_library(test_tbe_cbind_standalone_static STATIC
  "${TBE_CBIND_STANDALONE_HEADER}"
  "${TBE_CBIND_STANDALONE_SOURCE}")
target_include_directories(test_tbe_cbind_standalone_static
  PUBLIC
    "${TBE_CBIND_STANDALONE_GENERATED_DIR}"
    "${CMAKE_SOURCE_DIR}/tbe/schema/include")
target_link_libraries(test_tbe_cbind_standalone_static
  PUBLIC TurboUtils::Core TurboUtils::CBind)
set_target_properties(test_tbe_cbind_standalone_static PROPERTIES
  FOLDER "tbe/tbe_compiler")

add_executable(test_tbe_cbind_standalone_c test_cbind_standalone.c)
target_link_libraries(test_tbe_cbind_standalone_c
  PRIVATE test_tbe_cbind_standalone_static)
set_target_properties(test_tbe_cbind_standalone_c PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  FOLDER "tbe/tbe_compiler")
tbe_copy_turboutils_core_runtime_dep(test_tbe_cbind_standalone_c)

add_executable(test_tbe_cbind_standalone_cpp test_cbind_standalone_cpp.cpp)
target_link_libraries(test_tbe_cbind_standalone_cpp
  PRIVATE test_tbe_cbind_standalone_static)
set_target_properties(test_tbe_cbind_standalone_cpp PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  FOLDER "tbe/tbe_compiler")
tbe_copy_turboutils_core_runtime_dep(test_tbe_cbind_standalone_cpp)

get_target_property(TBE_CBIND_STANDALONE_LINKS
                    test_tbe_cbind_standalone_static LINK_LIBRARIES)
get_target_property(TBE_CBIND_STANDALONE_INTERFACE
                    test_tbe_cbind_standalone_static INTERFACE_LINK_LIBRARIES)
get_target_property(TBE_CBIND_STANDALONE_SOURCES
                    test_tbe_cbind_standalone_static SOURCES)
get_target_property(TBE_CBIND_STANDALONE_INCLUDES
                    test_tbe_cbind_standalone_static INCLUDE_DIRECTORIES)
get_target_property(TBE_CBIND_STANDALONE_INTERFACE_INCLUDES
                    test_tbe_cbind_standalone_static INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(TBE_CBIND_STANDALONE_C_LINKS
                    test_tbe_cbind_standalone_c LINK_LIBRARIES)
get_target_property(TBE_CBIND_STANDALONE_C_SOURCES
                    test_tbe_cbind_standalone_c SOURCES)
get_target_property(TBE_CBIND_STANDALONE_CPP_LINKS
                    test_tbe_cbind_standalone_cpp LINK_LIBRARIES)
get_target_property(TBE_CBIND_STANDALONE_CPP_SOURCES
                    test_tbe_cbind_standalone_cpp SOURCES)
if(NOT TBE_CBIND_STANDALONE_LINKS STREQUAL
       "TurboUtils::Core;TurboUtils::CBind" OR
   NOT TBE_CBIND_STANDALONE_INTERFACE STREQUAL
       "TurboUtils::Core;TurboUtils::CBind" OR
   NOT TBE_CBIND_STANDALONE_SOURCES STREQUAL
       "${TBE_CBIND_STANDALONE_HEADER};${TBE_CBIND_STANDALONE_SOURCE}" OR
   NOT TBE_CBIND_STANDALONE_INCLUDES STREQUAL
       "${TBE_CBIND_STANDALONE_GENERATED_DIR};${CMAKE_SOURCE_DIR}/tbe/schema/include" OR
   NOT TBE_CBIND_STANDALONE_INTERFACE_INCLUDES STREQUAL
       "${TBE_CBIND_STANDALONE_GENERATED_DIR};${CMAKE_SOURCE_DIR}/tbe/schema/include" OR
   NOT TBE_CBIND_STANDALONE_C_LINKS STREQUAL
       "test_tbe_cbind_standalone_static" OR
   NOT TBE_CBIND_STANDALONE_C_SOURCES STREQUAL
       "test_cbind_standalone.c" OR
   NOT TBE_CBIND_STANDALONE_CPP_LINKS STREQUAL
       "test_tbe_cbind_standalone_static" OR
   NOT TBE_CBIND_STANDALONE_CPP_SOURCES STREQUAL
       "test_cbind_standalone_cpp.cpp")
  message(FATAL_ERROR "standalone CBind generated consumer contract changed")
endif()
string(TOLOWER
       "${TBE_CBIND_STANDALONE_LINKS};${TBE_CBIND_STANDALONE_INTERFACE};${TBE_CBIND_STANDALONE_SOURCES};${TBE_CBIND_STANDALONE_INCLUDES};${TBE_CBIND_STANDALONE_INTERFACE_INCLUDES};${TBE_CBIND_STANDALONE_C_LINKS};${TBE_CBIND_STANDALONE_C_SOURCES};${TBE_CBIND_STANDALONE_CPP_LINKS};${TBE_CBIND_STANDALONE_CPP_SOURCES}"
       TBE_CBIND_STANDALONE_CLOSURE_LOWER)
if(TBE_CBIND_STANDALONE_CLOSURE_LOWER MATCHES
   "databind|data_bind|tbe_typed|with_databind")
  message(FATAL_ERROR "standalone CBind generated consumers must remain DataBind-free")
endif()

set(TBE_CBIND_BENCHMARK_SCHEMA
    "${CMAKE_SOURCE_DIR}/tbe/tbe_cbind/test/benchmark_tbe_cbind.schema")
set(TBE_CBIND_BENCHMARK_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated_tbe_cbind_benchmark")
set(TBE_CBIND_BENCHMARK_HEADER
    "${TBE_CBIND_BENCHMARK_GENERATED_DIR}/tbe_cbind_benchmark.h")
set(TBE_CBIND_BENCHMARK_SIDECAR_SOURCE
    "${TBE_CBIND_BENCHMARK_GENERATED_DIR}/tbe_cbind_benchmark_cbind.c")

add_custom_command(
  OUTPUT
    "${TBE_CBIND_BENCHMARK_HEADER}"
    "${TBE_CBIND_BENCHMARK_SIDECAR_SOURCE}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${TBE_CBIND_BENCHMARK_GENERATED_DIR}"
  COMMAND
    $<TARGET_FILE:tbe_compiler>
    "${TBE_CBIND_BENCHMARK_SCHEMA}"
    --lang c
    --output "${TBE_CBIND_BENCHMARK_HEADER}"
    --cbind-output "${TBE_CBIND_BENCHMARK_SIDECAR_SOURCE}"
  DEPENDS
    tbe_compiler
    "${TBE_CBIND_BENCHMARK_SCHEMA}"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_structs.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_cbind_source.mustache"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "Generating CBind-only runtime benchmark records"
  VERBATIM)

add_library(benchmark_tbe_cbind_sidecar STATIC
  "${TBE_CBIND_BENCHMARK_HEADER}"
  "${TBE_CBIND_BENCHMARK_SIDECAR_SOURCE}")
target_include_directories(benchmark_tbe_cbind_sidecar
  PUBLIC
    "${TBE_CBIND_BENCHMARK_GENERATED_DIR}"
    "${CMAKE_SOURCE_DIR}/tbe/schema/include")
target_link_libraries(benchmark_tbe_cbind_sidecar
  PUBLIC TurboUtils::Core TurboUtils::CBind)
set_target_properties(benchmark_tbe_cbind_sidecar PROPERTIES
  FOLDER "tbe/tbe_cbind/benchmarks")

cmake_add_benchmark(benchmark_tbe_cbind
  SOURCES ${CMAKE_SOURCE_DIR}/tbe/tbe_cbind/test/benchmark_tbe_cbind.c
  LIBS TurboParser::TbeCBind benchmark_tbe_cbind_sidecar TurboUtils::TinyTest
  DEFS TBE_CBIND_BENCHMARK_SCHEMA_PATH="${TBE_CBIND_BENCHMARK_SCHEMA}"
  FOLDER "tbe/tbe_cbind/benchmarks")
tbe_copy_turbonet_runtime_deps(benchmark_tbe_cbind)

# Keep the benchmark dependency contract narrow. This complements the actual
# link-command inspection used by the benchmark verification procedure.
get_target_property(TBE_CBIND_BENCHMARK_SIDECAR_LINKS
                    benchmark_tbe_cbind_sidecar LINK_LIBRARIES)
get_target_property(TBE_CBIND_BENCHMARK_SIDECAR_INTERFACE
                    benchmark_tbe_cbind_sidecar INTERFACE_LINK_LIBRARIES)
get_target_property(TBE_CBIND_BENCHMARK_SIDECAR_SOURCES
                    benchmark_tbe_cbind_sidecar SOURCES)
get_target_property(TBE_CBIND_BENCHMARK_SIDECAR_INCLUDES
                    benchmark_tbe_cbind_sidecar INCLUDE_DIRECTORIES)
get_target_property(TBE_CBIND_BENCHMARK_SIDECAR_INTERFACE_INCLUDES
                    benchmark_tbe_cbind_sidecar INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(TBE_CBIND_BENCHMARK_LINKS benchmark_tbe_cbind LINK_LIBRARIES)
if(NOT TBE_CBIND_BENCHMARK_SIDECAR_LINKS STREQUAL
       "TurboUtils::Core;TurboUtils::CBind" OR
   NOT TBE_CBIND_BENCHMARK_SIDECAR_INTERFACE STREQUAL
       "TurboUtils::Core;TurboUtils::CBind" OR
   NOT TBE_CBIND_BENCHMARK_SIDECAR_SOURCES STREQUAL
       "${TBE_CBIND_BENCHMARK_HEADER};${TBE_CBIND_BENCHMARK_SIDECAR_SOURCE}" OR
   NOT TBE_CBIND_BENCHMARK_SIDECAR_INCLUDES STREQUAL
       "${TBE_CBIND_BENCHMARK_GENERATED_DIR};${CMAKE_SOURCE_DIR}/tbe/schema/include" OR
   NOT TBE_CBIND_BENCHMARK_SIDECAR_INTERFACE_INCLUDES STREQUAL
       "${TBE_CBIND_BENCHMARK_GENERATED_DIR};${CMAKE_SOURCE_DIR}/tbe/schema/include" OR
   NOT TBE_CBIND_BENCHMARK_LINKS STREQUAL
       "TurboParser::TbeCBind;benchmark_tbe_cbind_sidecar;TurboUtils::TinyTest")
  message(FATAL_ERROR "benchmark_tbe_cbind dependency contract changed")
endif()
string(TOLOWER
       "${TBE_CBIND_BENCHMARK_SIDECAR_LINKS};${TBE_CBIND_BENCHMARK_SIDECAR_INTERFACE};${TBE_CBIND_BENCHMARK_SIDECAR_SOURCES};${TBE_CBIND_BENCHMARK_SIDECAR_INCLUDES};${TBE_CBIND_BENCHMARK_SIDECAR_INTERFACE_INCLUDES};${TBE_CBIND_BENCHMARK_LINKS}"
       TBE_CBIND_BENCHMARK_CLOSURE_LOWER)
if(TBE_CBIND_BENCHMARK_CLOSURE_LOWER MATCHES "databind|data_bind|tbe_typed")
  message(FATAL_ERROR "benchmark_tbe_cbind must remain CBind-only")
endif()

find_program(TBE_WASM_GUEST_CLANG NAMES clang clang.exe)
if(TBE_WASM_GUEST_CLANG)
  set(TBE_WASM_ORDER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_wasm")
  set(TBE_WASM_ORDER_HEADER "${TBE_WASM_ORDER_GENERATED_DIR}/wasm_order.h")
  set(TBE_WASM_ORDER_SOURCE "${TBE_WASM_ORDER_GENERATED_DIR}/wasm_order_guest.c")
  set(TBE_WASM_ORDER_OBJECT "${TBE_WASM_ORDER_GENERATED_DIR}/wasm_order_guest.o")

  add_custom_command(
    OUTPUT "${TBE_WASM_ORDER_HEADER}" "${TBE_WASM_ORDER_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TBE_WASM_ORDER_GENERATED_DIR}"
    COMMAND
      $<TARGET_FILE:tbe_compiler>
      "${CMAKE_CURRENT_SOURCE_DIR}/test_typed_order.schema"
      --lang c
      --output "${TBE_WASM_ORDER_HEADER}"
      --guest-output "${TBE_WASM_ORDER_SOURCE}"
    DEPENDS
      tbe_compiler
      "${CMAKE_CURRENT_SOURCE_DIR}/test_typed_order.schema"
      "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_structs.mustache"
      "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_guest_adapter.mustache"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating freestanding Wasm Order guest bindings"
    VERBATIM)

  add_custom_command(
    OUTPUT "${TBE_WASM_ORDER_OBJECT}"
    COMMAND
      "${TBE_WASM_GUEST_CLANG}"
      --target=wasm32-unknown-unknown
      -DTBE_WASM_GUEST=1
      -O2
      -nostdlib
      -Wall
      -Wextra
      -Werror
      -c "${TBE_WASM_ORDER_SOURCE}"
      -I "${TBE_WASM_ORDER_GENERATED_DIR}"
      -I "${CMAKE_SOURCE_DIR}/tbe/schema/include"
      -o "${TBE_WASM_ORDER_OBJECT}"
    DEPENDS "${TBE_WASM_ORDER_HEADER}" "${TBE_WASM_ORDER_SOURCE}"
    COMMENT "Compiling generated Order adapter for wasm32-unknown-unknown"
    VERBATIM)

  add_custom_target(test_tbe_wasm_guest_compile DEPENDS "${TBE_WASM_ORDER_OBJECT}")
  add_dependencies(test_tbe_typed_order test_tbe_wasm_guest_compile)
  set_target_properties(test_tbe_wasm_guest_compile PROPERTIES FOLDER "tbe/tbe_compiler")
else()
  message(STATUS "clang not found; freestanding Wasm guest compile check is disabled")
endif()
