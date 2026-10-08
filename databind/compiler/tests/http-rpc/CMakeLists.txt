set(DATABIND_METHOD_PLAN_FIXTURE_HTTP
    "${CMAKE_CURRENT_BINARY_DIR}/generated_http_projection.h")
set(DATABIND_METHOD_PLAN_FIXTURE_RPC
    "${CMAKE_CURRENT_BINARY_DIR}/generated_rpc_projection.h")

add_executable(databind_method_plan_projection_fixture_gen
  test_method_plan_projection_generate.c
  "${DATABIND_COMPILER_SOURCE_DIR}/projection.c"
  "${DATABIND_COMPILER_SOURCE_DIR}/method_plan_projection.c"
  "${DATABIND_COMPILER_SOURCE_DIR}/opaque_plan_codegen.c"
  "${DATABIND_COMPILER_SOURCE_DIR}/compiler_core.c" "${DATABIND_COMPILER_SOURCE_DIR}/native_source_ir.c" "${DATABIND_COMPILER_SOURCE_DIR}/plugin_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/wasm_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/native_service_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/service_native.c"
  "${DATABIND_COMPILER_SOURCE_DIR}/binary_reader_codegen.c"
  "${DATABIND_COMPILER_SOURCE_DIR}/database_schema.c"
  "${DATABIND_COMPILER_SOURCE_DIR}/mustache_helpers.c")
target_link_libraries(databind_method_plan_projection_fixture_gen
  PRIVATE
    Salts::IDL
    ${DATABIND_COMPILER_TOOLING_TARGET} databind_binding_compiler
    Salts::Core)
target_include_directories(databind_method_plan_projection_fixture_gen
  PRIVATE
    ${DATABIND_SOURCE_ROOT}/idl/contract
    ${DATABIND_COMPILER_SOURCE_DIR})
cmake_config_target(databind_method_plan_projection_fixture_gen
  FOLDER "databind/compiler/generated/http-rpc")

add_custom_command(
  OUTPUT
    "${DATABIND_METHOD_PLAN_FIXTURE_HTTP}"
    "${DATABIND_METHOD_PLAN_FIXTURE_RPC}"
  COMMAND
    $<TARGET_FILE:databind_method_plan_projection_fixture_gen>
    "${CMAKE_CURRENT_SOURCE_DIR}/test_method_plan_projection.schema"
    "${DATABIND_METHOD_PLAN_FIXTURE_HTTP}"
    "${DATABIND_METHOD_PLAN_FIXTURE_RPC}"
  DEPENDS
    databind_method_plan_projection_fixture_gen
    "${CMAKE_CURRENT_SOURCE_DIR}/test_method_plan_projection.schema"
  VERBATIM)

add_custom_target(databind_method_plan_projection_artifacts
  DEPENDS
    "${DATABIND_METHOD_PLAN_FIXTURE_HTTP}"
    "${DATABIND_METHOD_PLAN_FIXTURE_RPC}")
cmake_config_target(databind_method_plan_projection_artifacts
  FOLDER "databind/compiler/generated/http-rpc")

cmake_add_test(test_databind_generated_method_plan_projection
  SOURCES
    test_generated_method_plan_projection.c
    "${DATABIND_METHOD_PLAN_FIXTURE_HTTP}"
    "${DATABIND_METHOD_PLAN_FIXTURE_RPC}"
  LIBS
    Salts::DataBind
    Salts::TinyTest
  INCLUDES
    "${CMAKE_CURRENT_BINARY_DIR}"
  FOLDER "databind/compiler/tests/http-rpc")
add_dependencies(test_databind_generated_method_plan_projection
  databind_method_plan_projection_artifacts)
set_source_files_properties(
  "${DATABIND_METHOD_PLAN_FIXTURE_HTTP}"
  "${DATABIND_METHOD_PLAN_FIXTURE_RPC}"
  PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE)
set_target_properties(test_databind_generated_method_plan_projection PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

salts_idl_target(
  TARGET databind_method_plan_public_fixture
  FOLDER "databind/compiler/generated/http-rpc"
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/test_method_plan_projection.schema"
  ARTIFACT_NAME "public_calc"
  TRANSPORTS HTTP RPC)


salts_idl_target(
  TARGET databind_method_plan_configured_fixture
  FOLDER "databind/compiler/generated/http-rpc"
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/test_method_plan_projection.schema"
  ARTIFACT_NAME "configured_calc"
  TRANSPORTS HTTP RPC
  PROJECTION_CONFIG
    "${CMAKE_CURRENT_SOURCE_DIR}/test_method_plan_projection_config.json")

cmake_add_test(test_databind_method_plan_public_frontend
  SOURCES test_public_method_plan_frontend.c
  LIBS Salts::DataBind Salts::TinyTest
  INCLUDES
    "${databind_method_plan_public_fixture_GENERATED_DIR}"
    "${databind_method_plan_configured_fixture_GENERATED_DIR}"
  FOLDER "databind/compiler/tests/http-rpc")
add_dependencies(test_databind_method_plan_public_frontend
  databind_method_plan_public_fixture_idl_codegen
  databind_method_plan_configured_fixture_idl_codegen)
