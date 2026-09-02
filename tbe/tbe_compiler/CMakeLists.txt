# =============================================================================
# Executable
# =============================================================================

set(TBE_COMPILER_TEMPLATE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_guest_adapter.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_lua_bind.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_structs.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_typed_source.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/cpp_types.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/go_types.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/python_dataclass.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/rfl_types.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/rust_structs.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/sqlite_schema.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/postgresql_schema.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/ts_types.mustache")

cmake_add_executable(tbe_compiler
  SOURCES main.c compiler_core.c database_schema.c mustache_helpers.c ${TBE_COMPILER_TEMPLATE_FILES}
  LIBS TurboParser::TbeSchema TurboParser::Parser TurboParser::Mustache Rocida::Core
  FOLDER "tbe/tbe_compiler")
target_include_directories(tbe_compiler
  PRIVATE ${CMAKE_SOURCE_DIR}/tbe/schema/src)
set_property(TARGET tbe_compiler APPEND PROPERTY LINK_DEPENDS
             ${TBE_COMPILER_TEMPLATE_FILES})

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
  SOURCES test_tbe_compiler.c compiler_core.c database_schema.c mustache_helpers.c
  LIBS TurboParser::TbeSchema Rocida::TinyTest TurboParser::Mustache Rocida::Core
  DEFS
    SCHEMA_EXAMPLE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/example.schema"
    C_STRUCT_TEMPLATE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/templates/c_structs.mustache"
    CPP_TYPES_TEMPLATE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/templates/cpp_types.mustache"
    GO_TYPES_TEMPLATE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/templates/go_types.mustache"
    PYTHON_DATACLASS_TEMPLATE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/templates/python_dataclass.mustache"
    RUST_STRUCTS_TEMPLATE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/templates/rust_structs.mustache"
    TS_TYPES_TEMPLATE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/templates/ts_types.mustache"
    POSTGRESQL_SECURITY_PROBE_SCHEMA_FILE="${CMAKE_CURRENT_SOURCE_DIR}/test_postgresql_security_probe.schema"
    TBE_COMPILER_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  FOLDER "tbe/tbe_compiler")
target_include_directories(test_tbe_compiler
  PRIVATE ${CMAKE_SOURCE_DIR}/tbe/schema/src)

if(BUILD_TESTING)
  find_package(unofficial-sqlite3 CONFIG REQUIRED)

  cmake_add_test(test_tbe_database_ddl
    SOURCES test_tbe_database_ddl.c compiler_core.c database_schema.c mustache_helpers.c
    LIBS
      TurboParser::TbeSchema
      Rocida::TinyTest
      TurboParser::Mustache
      Rocida::Core
      unofficial::sqlite3::sqlite3
    DEFS
      TEST_DATABASE_SCHEMA_FILE="${CMAKE_CURRENT_SOURCE_DIR}/test_database_schema.schema"
      TBE_COMPILER_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    FOLDER "tbe/tbe_compiler")
  target_include_directories(test_tbe_database_ddl
    PRIVATE ${CMAKE_SOURCE_DIR}/tbe/schema/src)
endif()

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
  LIBS test_tbe_typed_order_static Rocida::TinyTest
  INCLUDES "${TBE_TYPED_ORDER_GENERATED_DIR}"
  FOLDER "tbe/tbe_compiler")

cmake_add_test(test_tbe_typed_order_cpp
  SOURCES test_typed_order_cpp.cpp
  LIBS test_tbe_typed_order_shared Rocida::TinyTest
  INCLUDES "${TBE_TYPED_ORDER_GENERATED_DIR}"
  FOLDER "tbe/tbe_compiler")
target_compile_definitions(test_tbe_typed_order_cpp PRIVATE TBE_GENERATED_USE_SHARED)
set_target_properties(test_tbe_typed_order_cpp PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON)

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
