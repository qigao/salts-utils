# =============================================================================
# Executable
# =============================================================================

set(DATABIND_COMPILER_TOOLING_TARGET databind_compiler_tooling)
add_library(${DATABIND_COMPILER_TOOLING_TARGET} INTERFACE)
target_link_libraries(${DATABIND_COMPILER_TOOLING_TARGET}
  INTERFACE Salts::CmdParser Salts::Mustache Salts::JsonParser)
set_target_properties(${DATABIND_COMPILER_TOOLING_TARGET}
  PROPERTIES FOLDER "databind/compiler")

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

cmake_add_executable(databindc
  SOURCES main.c compiler_core.c projection.c projection_frontend.c projection_config.c service_native.c plugin_projection.c method_plan_projection.c database_schema.c mustache_helpers.c ${TBE_COMPILER_TEMPLATE_FILES}
  LIBS Salts::DataBindSchema Salts::PluginABI ${DATABIND_COMPILER_TOOLING_TARGET} Salts::Core
  FOLDER "databind/compiler")
target_include_directories(databindc
  PRIVATE ${DATABIND_SOURCE_ROOT}/schema/src)
set_property(TARGET databindc APPEND PROPERTY LINK_DEPENDS
             ${TBE_COMPILER_TEMPLATE_FILES})

# =============================================================================
# Copy templates next to executable for easy access
# =============================================================================

add_custom_command(
  TARGET databindc
  POST_BUILD
  COMMAND
    ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/templates
    $<TARGET_FILE_DIR:databindc>/templates
  COMMENT "Copying template files to output directory")

install(
  TARGETS databindc
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT DataBind)

install(
  DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/templates"
  DESTINATION ${CMAKE_INSTALL_BINDIR}
  COMPONENT DataBind)

# =============================================================================
# Tests
# =============================================================================

if(BUILD_TESTS)
add_test(NAME test_databind_compiler_tooling_boundary
  COMMAND "${CMAKE_COMMAND}"
    "-DDATABIND_SOURCE_ROOT=${DATABIND_SOURCE_ROOT}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/test_compiler_tooling_boundary.cmake")
set_tests_properties(test_databind_compiler_tooling_boundary
  PROPERTIES LABELS "databind;package;compiler")

add_test(NAME test_databind_nullable_codegen_rejected
  COMMAND "${CMAKE_COMMAND}"
    "-DDATABINDC=$<TARGET_FILE:databindc>"
    "-DSCHEMA=${CMAKE_CURRENT_SOURCE_DIR}/test_nullable_rejection.schema"
    "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/nullable-rejected.h"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/test_nullable_codegen_rejection.cmake")
set_tests_properties(test_databind_nullable_codegen_rejected
  PROPERTIES LABELS "databind;compiler;nullable")


set(DATABIND_METHOD_PLAN_FIXTURE_HTTP
    "${CMAKE_CURRENT_BINARY_DIR}/generated_http_projection.h")
set(DATABIND_METHOD_PLAN_FIXTURE_RPC
    "${CMAKE_CURRENT_BINARY_DIR}/generated_rpc_projection.h")

add_executable(databind_method_plan_projection_fixture_gen
  test_method_plan_projection_generate.c
  projection.c
  method_plan_projection.c
  compiler_core.c
  database_schema.c
  mustache_helpers.c)
target_link_libraries(databind_method_plan_projection_fixture_gen
  PRIVATE
    Salts::DataBindSchema
    ${DATABIND_COMPILER_TOOLING_TARGET}
    Salts::Core)
target_include_directories(databind_method_plan_projection_fixture_gen
  PRIVATE
    ${DATABIND_SOURCE_ROOT}/schema/src
    ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(databind_method_plan_projection_fixture_gen
  PROPERTIES FOLDER "databind/compiler/tests")

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
set_target_properties(databind_method_plan_projection_artifacts
  PROPERTIES FOLDER "databind/compiler/tests")

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
  FOLDER "databind/compiler")
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


cmake_add_test(test_databind_projection_registry
  SOURCES
    test_projection_registry.c
    projection.c
    compiler_core.c
    database_schema.c
    mustache_helpers.c
  LIBS
    Salts::DataBindSchema
    Salts::TinyTest
    ${DATABIND_COMPILER_TOOLING_TARGET}
    Salts::Core
  DEFS
    SCHEMA_EXAMPLE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/example.schema"
    TBE_COMPILER_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  INCLUDES
    ${DATABIND_SOURCE_ROOT}/schema/src
    ${CMAKE_CURRENT_SOURCE_DIR}
  FOLDER "databind/compiler")
set_target_properties(test_databind_projection_registry PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(test_databind_projection_frontend
  SOURCES
    test_projection_frontend.c
    projection_frontend.c
    projection_config.c
    projection.c
    service_native.c
    plugin_projection.c
    method_plan_projection.c
  LIBS
    Salts::DataBindSchema
    Salts::PluginABI
    Salts::TinyTest
    ${DATABIND_COMPILER_TOOLING_TARGET}
    Salts::Core
  DEFS
    DATABIND_PROJECTION_CONFIG_FILE="${CMAKE_CURRENT_SOURCE_DIR}/test_method_plan_projection_config.json"
    DATABIND_PROJECTION_CONFIG_INVALID_FILE="${CMAKE_CURRENT_SOURCE_DIR}/test_method_plan_projection_config_invalid.json"
  INCLUDES
    ${DATABIND_SOURCE_ROOT}/schema/src
    ${CMAKE_CURRENT_SOURCE_DIR}
  FOLDER "databind/compiler")
set_target_properties(test_databind_projection_frontend PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

# Canonical Service -> native FunctionMeta/FunctionAbi lowering qualification.
set(DATABIND_SERVICE_NATIVE_TEST_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated-service-native")
file(MAKE_DIRECTORY "${DATABIND_SERVICE_NATIVE_TEST_DIR}")

set(DATABIND_SERVICE_NATIVE_RECORD_H
    "${DATABIND_SERVICE_NATIVE_TEST_DIR}/service_native_records.h")
set(DATABIND_SERVICE_NATIVE_RECORD_C
    "${DATABIND_SERVICE_NATIVE_TEST_DIR}/service_native_records.c")
set(DATABIND_SERVICE_NATIVE_HEADER
    "${DATABIND_SERVICE_NATIVE_TEST_DIR}/service_native_generated.h")
set(DATABIND_SERVICE_NATIVE_SOURCE
    "${DATABIND_SERVICE_NATIVE_TEST_DIR}/service_native_generated.c")

cmake_add_executable(databind_service_native_codegen_fixture_tool
  SOURCES
    test_service_native_codegen_tool.c
    compiler_core.c
    projection.c
    service_native.c
    database_schema.c
    mustache_helpers.c
  LIBS
    Salts::DataBindSchema
    ${DATABIND_COMPILER_TOOLING_TARGET}
    Salts::Core
  INCLUDES
    ${DATABIND_SOURCE_ROOT}/schema/src
    ${CMAKE_CURRENT_SOURCE_DIR}
  FOLDER "databind/compiler")

add_custom_command(
  OUTPUT
    "${DATABIND_SERVICE_NATIVE_RECORD_H}"
    "${DATABIND_SERVICE_NATIVE_RECORD_C}"
    "${DATABIND_SERVICE_NATIVE_HEADER}"
    "${DATABIND_SERVICE_NATIVE_SOURCE}"
  COMMAND
    $<TARGET_FILE:databindc>
      "${CMAKE_CURRENT_SOURCE_DIR}/test_service_native_lowering.schema"
      --lang c
      --output "${DATABIND_SERVICE_NATIVE_RECORD_H}"
      --source-output "${DATABIND_SERVICE_NATIVE_RECORD_C}"
  COMMAND
    $<TARGET_FILE:databind_service_native_codegen_fixture_tool>
      "${CMAKE_CURRENT_SOURCE_DIR}/test_service_native_lowering.schema"
      "${DATABIND_SERVICE_NATIVE_HEADER}"
      "${DATABIND_SERVICE_NATIVE_SOURCE}"
      "service_native_records.h"
      "${CMAKE_CURRENT_SOURCE_DIR}/test_service_native_optional.schema"
      "${CMAKE_CURRENT_SOURCE_DIR}/test_service_native_owned_error.schema"
  DEPENDS
    databindc
    databind_service_native_codegen_fixture_tool
    "${CMAKE_CURRENT_SOURCE_DIR}/test_service_native_lowering.schema"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_service_native_optional.schema"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_service_native_owned_error.schema"
  VERBATIM
  COMMENT "Generating canonical DataBind Service native metadata")

add_custom_target(databind_service_native_generated_sources
  DEPENDS
    "${DATABIND_SERVICE_NATIVE_RECORD_H}"
    "${DATABIND_SERVICE_NATIVE_RECORD_C}"
    "${DATABIND_SERVICE_NATIVE_HEADER}"
    "${DATABIND_SERVICE_NATIVE_SOURCE}")

set_source_files_properties(
  "${DATABIND_SERVICE_NATIVE_RECORD_C}"
  "${DATABIND_SERVICE_NATIVE_SOURCE}"
  PROPERTIES GENERATED TRUE)

cmake_add_test(test_databind_service_native_lowering
  SOURCES
    test_service_native_lowering.c
    test_service_native_business.c
    "${DATABIND_SERVICE_NATIVE_RECORD_C}"
    "${DATABIND_SERVICE_NATIVE_SOURCE}"
  LIBS
    Salts::DataBind
    Salts::TinyTest
  INCLUDES
    "${DATABIND_SERVICE_NATIVE_TEST_DIR}"
  FOLDER "databind/compiler")
add_dependencies(test_databind_service_native_lowering
  databind_service_native_generated_sources)

cmake_add_test(test_databind_service_native_public_cpp
  SOURCES test_service_native_public_cpp.cpp
  LIBS Salts::DataBind
  INCLUDES
    "${DATABIND_SERVICE_NATIVE_TEST_DIR}"
  FOLDER "databind/compiler")
add_dependencies(test_databind_service_native_public_cpp
  databind_service_native_generated_sources)
set_target_properties(test_databind_service_native_public_cpp PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO)

# Generated PLUGIN backend qualification through the canonical public
# databind_target() -> databindc projection frontend.
include("${CMAKE_SOURCE_DIR}/cmake/DataBind.cmake")

set(DATABIND_PLUGIN_TEST_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/databind_plugin_generated_fixture.databind")
file(MAKE_DIRECTORY "${DATABIND_PLUGIN_TEST_GENERATED_DIR}")
set(DATABIND_PLUGIN_TEST_NATIVE_H
    "${DATABIND_PLUGIN_TEST_GENERATED_DIR}/image_native.h")
set(DATABIND_PLUGIN_TEST_BINDING_H
    "${DATABIND_PLUGIN_TEST_GENERATED_DIR}/image_binding_native.h")
set(DATABIND_PLUGIN_TEST_BINDING_C
    "${DATABIND_PLUGIN_TEST_GENERATED_DIR}/image_binding_native.c")
set(DATABIND_PLUGIN_TEST_SERVICE_H
    "${DATABIND_PLUGIN_TEST_GENERATED_DIR}/image.plugin.h")
set(DATABIND_PLUGIN_TEST_SOURCE
    "${DATABIND_PLUGIN_TEST_GENERATED_DIR}/image.plugin.c")

databind_target(
  TARGET databind_plugin_generated_fixture
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/test_plugin_projection.schema"
  COMPONENT "Image.ImageProcessor"
  VERSION "1.0.0"
  ARTIFACT_NAME "image"
  PROJECTIONS PLUGIN
  SOURCES test_plugin_business_fixture.c)


databind_target(
  TARGET databind_method_plan_public_fixture
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/test_method_plan_projection.schema"
  ARTIFACT_NAME "public_calc"
  PROJECTIONS HTTP RPC)


databind_target(
  TARGET databind_method_plan_configured_fixture
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/test_method_plan_projection.schema"
  ARTIFACT_NAME "configured_calc"
  PROJECTIONS HTTP RPC
  PROJECTION_CONFIG
    "${CMAKE_CURRENT_SOURCE_DIR}/test_method_plan_projection_config.json")

cmake_add_test(test_databind_method_plan_public_frontend
  SOURCES test_public_method_plan_frontend.c
  LIBS Salts::DataBind Salts::TinyTest
  INCLUDES
    "${databind_method_plan_public_fixture_GENERATED_DIR}"
    "${databind_method_plan_configured_fixture_GENERATED_DIR}"
  FOLDER "databind/compiler")
add_dependencies(test_databind_method_plan_public_frontend
  databind_method_plan_public_fixture_databind_codegen
  databind_method_plan_configured_fixture_databind_codegen)

get_target_property(
  _databind_plugin_generated_fixture_links
  databind_plugin_generated_fixture_plugin
  LINK_LIBRARIES)
if("Salts::DataBindSchema" IN_LIST _databind_plugin_generated_fixture_links)
  message(FATAL_ERROR
          "generated Plugin must not link Salts::DataBindSchema")
endif()
if(NOT "Salts::DataBindGeneratedABI"
       IN_LIST _databind_plugin_generated_fixture_links)
  message(FATAL_ERROR
          "generated Plugin must link Salts::DataBindGeneratedABI")
endif()

get_target_property(
  _databind_plugin_generated_client_links
  databind_plugin_generated_fixture_plugin_client
  LINK_LIBRARIES)
if("Salts::DataBindSchema"
   IN_LIST _databind_plugin_generated_client_links)
  message(FATAL_ERROR
          "generated Plugin client must not link Salts::DataBindSchema")
endif()
if(NOT "Salts::Plugin"
       IN_LIST _databind_plugin_generated_client_links)
  message(FATAL_ERROR
          "generated Plugin client must link Salts::Plugin")
endif()
if(NOT "Salts::DataBindGeneratedABI"
       IN_LIST _databind_plugin_generated_client_links)
  message(FATAL_ERROR
          "generated Plugin client must link Salts::DataBindGeneratedABI")
endif()

add_custom_command(
  OUTPUT
    "${DATABIND_PLUGIN_TEST_BINDING_H}"
    "${DATABIND_PLUGIN_TEST_BINDING_C}"
  COMMAND
    $<TARGET_FILE:databindc>
      "${CMAKE_CURRENT_SOURCE_DIR}/test_plugin_projection.schema"
      --lang c
      --output "${DATABIND_PLUGIN_TEST_BINDING_H}"
      --source-output "${DATABIND_PLUGIN_TEST_BINDING_C}"
  DEPENDS
    databindc
    "${CMAKE_CURRENT_SOURCE_DIR}/test_plugin_projection.schema"
  VERBATIM
  COMMENT "Generating host-side DataBind binding fixture")

set_source_files_properties(
  "${DATABIND_PLUGIN_TEST_BINDING_C}"
  PROPERTIES GENERATED TRUE)

cmake_add_test(test_databind_plugin_generated_dso
  SOURCES
    test_plugin_generated_dso.c
    "${DATABIND_PLUGIN_TEST_BINDING_C}"
  LIBS Salts::Plugin Salts::DataBind Salts::TinyTest
  DEFS
    GENERATED_DATABIND_PLUGIN_PATH="$<TARGET_FILE:databind_plugin_generated_fixture_plugin>"
  INCLUDES
    "${DATABIND_PLUGIN_TEST_GENERATED_DIR}"
  FOLDER "databind/compiler")
add_dependencies(test_databind_plugin_generated_dso
  databind_plugin_generated_fixture_plugin)

databind_target(
  TARGET databind_plugin_typed_error_fixture
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/test_plugin_typed_error.schema"
  COMPONENT "ErrorPlugin.StorePlugin"
  VERSION "1.0.0"
  ARTIFACT_NAME "error"
  PROJECTIONS PLUGIN
  SOURCES test_plugin_typed_error_business_fixture.c)

cmake_add_test(test_databind_plugin_typed_error_client
  SOURCES test_plugin_typed_error_generated_client.c
  LIBS
    databind_plugin_typed_error_fixture_plugin_client
    Salts::TinyTest
  DEFS
    GENERATED_TYPED_ERROR_PLUGIN_PATH="$<TARGET_FILE:databind_plugin_typed_error_fixture_plugin>"
  FOLDER "databind/compiler")
add_dependencies(test_databind_plugin_typed_error_client
  databind_plugin_typed_error_fixture_plugin
  databind_plugin_typed_error_fixture_plugin_client)

add_executable(test_databind_plugin_typed_error_client_cpp
  test_plugin_typed_error_generated_client_cpp.cpp)
target_compile_features(test_databind_plugin_typed_error_client_cpp PRIVATE
  cxx_std_17)
target_link_libraries(test_databind_plugin_typed_error_client_cpp PRIVATE
  databind_plugin_typed_error_fixture_plugin_client)
set_target_properties(test_databind_plugin_typed_error_client_cpp PROPERTIES
  CXX_EXTENSIONS NO
  FOLDER "databind/compiler")
add_test(NAME test_databind_plugin_typed_error_client_cpp
  COMMAND test_databind_plugin_typed_error_client_cpp)

foreach(_client_fixture IN ITEMS
        bad_plugin_id
        bad_contract
        bad_export
        bad_function)
  add_library("databind_plugin_client_${_client_fixture}" SHARED
    test_plugin_client_mismatch_fixture.c)
  target_compile_features(
    "databind_plugin_client_${_client_fixture}" PRIVATE c_std_11)
  target_link_libraries(
    "databind_plugin_client_${_client_fixture}" PRIVATE Salts::PluginABI)
  set_target_properties(
    "databind_plugin_client_${_client_fixture}" PROPERTIES
    PREFIX ""
    FOLDER "databind/compiler/tests")
endforeach()
target_compile_definitions(databind_plugin_client_bad_plugin_id PRIVATE
  PLUGIN_CLIENT_MISMATCH_PLUGIN_ID=1)
target_compile_definitions(databind_plugin_client_bad_contract PRIVATE
  PLUGIN_CLIENT_MISMATCH_CONTRACT_ID=1)
target_compile_definitions(databind_plugin_client_bad_export PRIVATE
  PLUGIN_CLIENT_MISMATCH_EXPORT_ID=1)
target_compile_definitions(databind_plugin_client_bad_function PRIVATE
  PLUGIN_CLIENT_MISMATCH_FUNCTION=1)

cmake_add_test(test_databind_plugin_generated_client
  SOURCES test_plugin_generated_client.c
  LIBS
    databind_plugin_generated_fixture_plugin_client
    Salts::TinyTest
  DEFS
    GENERATED_DATABIND_PLUGIN_PATH="$<TARGET_FILE:databind_plugin_generated_fixture_plugin>"
    GENERATED_DATABIND_CLIENT_BAD_PLUGIN_ID_PATH="$<TARGET_FILE:databind_plugin_client_bad_plugin_id>"
    GENERATED_DATABIND_CLIENT_BAD_CONTRACT_PATH="$<TARGET_FILE:databind_plugin_client_bad_contract>"
    GENERATED_DATABIND_CLIENT_BAD_EXPORT_PATH="$<TARGET_FILE:databind_plugin_client_bad_export>"
    GENERATED_DATABIND_CLIENT_BAD_FUNCTION_PATH="$<TARGET_FILE:databind_plugin_client_bad_function>"
  FOLDER "databind/compiler")
add_dependencies(test_databind_plugin_generated_client
  databind_plugin_generated_fixture_plugin
  databind_plugin_generated_fixture_plugin_client
  databind_plugin_client_bad_plugin_id
  databind_plugin_client_bad_contract
  databind_plugin_client_bad_export
  databind_plugin_client_bad_function)

add_executable(test_databind_plugin_generated_client_cpp
  test_plugin_generated_client_cpp.cpp)
target_compile_features(test_databind_plugin_generated_client_cpp PRIVATE
  cxx_std_17)
target_link_libraries(test_databind_plugin_generated_client_cpp PRIVATE
  databind_plugin_generated_fixture_plugin_client)
set_target_properties(test_databind_plugin_generated_client_cpp PROPERTIES
  CXX_EXTENSIONS NO
  FOLDER "databind/compiler")
add_test(NAME test_databind_plugin_generated_client_cpp
  COMMAND test_databind_plugin_generated_client_cpp)

cmake_add_test(test_databind_plugin_projection_rejection
  SOURCES
    test_plugin_projection_rejection.c
    compiler_core.c
    projection.c
    service_native.c
    plugin_projection.c
    database_schema.c
    mustache_helpers.c
  LIBS
    Salts::DataBindSchema
    Salts::PluginABI
    Salts::TinyTest
    ${DATABIND_COMPILER_TOOLING_TARGET}
    Salts::Core
  DEFS
    PLUGIN_OWNED_TYPED_ERROR_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/test_plugin_owned_typed_error.schema"
    PLUGIN_MULTI_SERVICE_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/test_plugin_multi_service.schema"
    PLUGIN_COMPONENT_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/test_plugin_projection.schema"
    TBE_COMPILER_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  INCLUDES
    ${DATABIND_SOURCE_ROOT}/schema/src
    ${CMAKE_CURRENT_SOURCE_DIR}
  FOLDER "databind/compiler")

cmake_add_test(test_tbe_compiler
  SOURCES test_tbe_compiler.c compiler_core.c database_schema.c mustache_helpers.c
  LIBS Salts::DataBindSchema Salts::TinyTest ${DATABIND_COMPILER_TOOLING_TARGET} Salts::Core
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
  FOLDER "databind/compiler")
target_include_directories(test_tbe_compiler
  PRIVATE ${DATABIND_SOURCE_ROOT}/schema/src)

cmake_add_test(test_tbe_compiler_schema_boundary
  SOURCES test_schema_boundary.c compiler_core.c database_schema.c mustache_helpers.c
  LIBS Salts::DataBindSchema Salts::TinyTest ${DATABIND_COMPILER_TOOLING_TARGET} Salts::Core
  DEFS TBE_COMPILER_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  FOLDER "databind/compiler")
cmake_add_test(test_tbe_compiler_cmeta_fields
  SOURCES test_cmeta_field_projection.c compiler_core.c database_schema.c mustache_helpers.c
  LIBS Salts::DataBindSchema Salts::TinyTest ${DATABIND_COMPILER_TOOLING_TARGET} Salts::Core
  FOLDER "databind/compiler")
target_include_directories(test_tbe_compiler_cmeta_fields
  PRIVATE ${DATABIND_SOURCE_ROOT}/schema/src)
target_include_directories(test_tbe_compiler_schema_boundary
  PRIVATE ${DATABIND_SOURCE_ROOT}/schema/src)
if(TARGET test_tbe_compiler_schema_boundary)
  target_sources(test_tbe_compiler_schema_boundary PRIVATE test_scalar_projection.c)
  set_source_files_properties(test_scalar_projection.c
    PROPERTIES COMPILE_DEFINITIONS TINYTEST_NO_MAIN)
endif()
endif()
