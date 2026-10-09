add_test(NAME test_databind_projection_config_stale_rejected
  COMMAND "${CMAKE_COMMAND}"
    "-DDATABINDC=$<TARGET_FILE:salts_idlc>"
    "-DSCHEMA=${DATABIND_COMPILER_TEST_SOURCE_DIR}/http-rpc/test_method_plan_projection.schema"
    "-DCONFIG=${DATABIND_COMPILER_TEST_SOURCE_DIR}/http-rpc/test_method_plan_projection_config_stale.json"
    "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/projection-config-stale"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/test_projection_config_stale_rejection.cmake")
set_tests_properties(test_databind_projection_config_stale_rejected
  PROPERTIES LABELS "databind;compiler;projection-config")

# Reuse the canonical executable Wasm Core fixture for real artifact
# transaction tests instead of constructing synthetic bytes in C.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(DATABIND_PROJECTION_WASM_CORE
    "${CMAKE_CURRENT_BINARY_DIR}/projection_wasm_execution_core.wasm")
execute_process(
  COMMAND "${Python3_EXECUTABLE}"
    "${DATABIND_COMPILER_SOURCE_DIR}/package_config/wasm_execution/write_core.py"
    "${DATABIND_PROJECTION_WASM_CORE}"
  RESULT_VARIABLE _projection_wasm_core_status)
if(NOT _projection_wasm_core_status EQUAL 0 OR
   NOT EXISTS "${DATABIND_PROJECTION_WASM_CORE}")
  message(FATAL_ERROR "Could not prepare canonical Wasm Core transaction fixture")
endif()

cmake_add_test(test_databind_projection_registry
  SOURCES
    test_projection_registry.c
    "${DATABIND_COMPILER_SOURCE_DIR}/projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/compiler_core.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/native_source_ir.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/native_service_projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/wasm_projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/plugin_projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/service_native.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/binary_reader_codegen.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/database_schema.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/mustache_helpers.c"
  LIBS
    Salts::IDL
    Salts::TinyTest
    ${DATABIND_COMPILER_TOOLING_TARGET}
    databind_binding_compiler
    Salts::Core
  DEFS
    SCHEMA_EXAMPLE_FILE="${DATABIND_COMPILER_TEST_SOURCE_DIR}/core/example.schema"
    SCHEMA_NATIVE_SERVICE_FILE="${DATABIND_COMPILER_TEST_SOURCE_DIR}/native/test_service_native_lowering.schema"
    SCHEMA_WASM_EXECUTION_FILE="${DATABIND_COMPILER_SOURCE_DIR}/package_config/wasm_execution/wasm_execution.schema"
    WASM_CORE_FIXTURE_FILE="${DATABIND_PROJECTION_WASM_CORE}"
    TBE_COMPILER_RESOURCE_DIR="${DATABIND_COMPILER_SOURCE_DIR}"
  INCLUDES
    ${DATABIND_SOURCE_ROOT}/idl/contract
    ${DATABIND_COMPILER_SOURCE_DIR}
  FOLDER "databind/compiler/tests/projection")
set_target_properties(test_databind_projection_registry PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(test_databind_projection_frontend
  SOURCES
    test_projection_frontend.c
    "${DATABIND_COMPILER_SOURCE_DIR}/projection_frontend.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/projection_config.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/service_native.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/native_service_projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/plugin_projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/wasm_projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/openapi_projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/method_plan_projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/socket_plan_projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/flowmq_plan_projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/binary_reader_codegen.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/opaque_plan_codegen.c"
  LIBS
    Salts::IDL
    Salts::PluginABI
    Salts::TinyTest
    ${DATABIND_COMPILER_TOOLING_TARGET}
    Salts::Core
  DEFS
    DATABIND_PROJECTION_CONFIG_FILE="${DATABIND_COMPILER_TEST_SOURCE_DIR}/http-rpc/test_method_plan_projection_config.json"
    DATABIND_PROJECTION_CONFIG_INVALID_FILE="${DATABIND_COMPILER_TEST_SOURCE_DIR}/http-rpc/test_method_plan_projection_config_invalid.json"
    DATABIND_SOCKET_PROJECTION_CONFIG_FILE="${DATABIND_COMPILER_TEST_SOURCE_DIR}/socket/test_socket_plan_projection_config.json"
    DATABIND_FLOWMQ_PROJECTION_CONFIG_FILE="${DATABIND_COMPILER_TEST_SOURCE_DIR}/flowmq/test_flowmq_plan_projection_config.json"
  INCLUDES
    ${DATABIND_SOURCE_ROOT}/idl/contract
    ${DATABIND_COMPILER_SOURCE_DIR}
  FOLDER "databind/compiler/tests/projection")
set_target_properties(test_databind_projection_frontend PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

target_link_libraries(test_databind_projection_frontend
  PRIVATE databind_binding_compiler)


# Compile an actual NativeSourceIR-emitted header with each platform's C11 compiler.
add_executable(databind_native_header_fixture_generator
  native_header_fixture_generator.c
  "${DATABIND_COMPILER_SOURCE_DIR}/native_source_ir.c")
target_include_directories(databind_native_header_fixture_generator PRIVATE
  "${DATABIND_COMPILER_SOURCE_DIR}"
  "${DATABIND_SOURCE_ROOT}/idl/include")
set_target_properties(databind_native_header_fixture_generator PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
set(_native_fixture_header "${CMAKE_CURRENT_BINARY_DIR}/native_contract_fixture.h")
add_custom_command(OUTPUT "${_native_fixture_header}"
  COMMAND $<TARGET_FILE:databind_native_header_fixture_generator>
          "${_native_fixture_header}"
  DEPENDS databind_native_header_fixture_generator
  VERBATIM)
add_executable(test_databind_native_header_c11
  native_header_c11_consumer.c "${_native_fixture_header}")
target_include_directories(test_databind_native_header_c11 PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}")
set_target_properties(test_databind_native_header_c11 PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
# Compile the optional CMeta descriptor path with the real Salts headers.
add_executable(test_databind_native_header_cmeta
  native_header_c11_consumer.c "${_native_fixture_header}")
add_dependencies(test_databind_native_header_cmeta
  databind_native_header_fixture_generator)
target_compile_definitions(test_databind_native_header_cmeta PRIVATE
  DATABIND_NATIVE_ENABLE_CMETA)
target_include_directories(test_databind_native_header_cmeta PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_databind_native_header_cmeta PRIVATE Salts::Core)
set_target_properties(test_databind_native_header_cmeta PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME test_databind_native_header_cmeta
  COMMAND $<TARGET_FILE:test_databind_native_header_cmeta>)
set_tests_properties(test_databind_native_header_cmeta PROPERTIES
  LABELS "databind;compiler;native-source;cmeta")

add_test(NAME test_databind_native_header_c11
  COMMAND $<TARGET_FILE:test_databind_native_header_c11>)
set_tests_properties(test_databind_native_header_c11 PROPERTIES
  LABELS "databind;compiler;native-source;c11")
