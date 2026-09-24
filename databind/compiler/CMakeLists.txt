# =============================================================================
# Executable
# =============================================================================

set(DATABIND_COMPILER_TOOLING_TARGET databind_compiler_tooling)
add_library(${DATABIND_COMPILER_TOOLING_TARGET} INTERFACE)
target_link_libraries(${DATABIND_COMPILER_TOOLING_TARGET}
  INTERFACE Salts::CmdParser Salts::Mustache)
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
  SOURCES main.c compiler_core.c projection.c service_native.c plugin_projection.c database_schema.c mustache_helpers.c ${TBE_COMPILER_TEMPLATE_FILES}
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
  DEPENDS
    databindc
    databind_service_native_codegen_fixture_tool
    "${CMAKE_CURRENT_SOURCE_DIR}/test_service_native_lowering.schema"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_service_native_optional.schema"
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

# Generated PLUGIN backend qualification. This is deliberately an internal
# codegen driver rather than a second public databindc frontend.
set(DATABIND_PLUGIN_TEST_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated-plugin")
file(MAKE_DIRECTORY "${DATABIND_PLUGIN_TEST_GENERATED_DIR}")

set(DATABIND_PLUGIN_TEST_NATIVE_H
    "${DATABIND_PLUGIN_TEST_GENERATED_DIR}/image_native.h")
set(DATABIND_PLUGIN_TEST_BINDING_H
    "${DATABIND_PLUGIN_TEST_GENERATED_DIR}/image_binding_native.h")
set(DATABIND_PLUGIN_TEST_BINDING_C
    "${DATABIND_PLUGIN_TEST_GENERATED_DIR}/image_binding_native.c")
set(DATABIND_PLUGIN_TEST_SERVICE_H
    "${DATABIND_PLUGIN_TEST_GENERATED_DIR}/image_plugin_service.h")
set(DATABIND_PLUGIN_TEST_SOURCE
    "${DATABIND_PLUGIN_TEST_GENERATED_DIR}/image_plugin.c")

cmake_add_executable(databind_plugin_codegen_fixture_tool
  SOURCES
    test_plugin_codegen_tool.c
    compiler_core.c
    projection.c
    service_native.c
    plugin_projection.c
    database_schema.c
    mustache_helpers.c
  LIBS
    Salts::DataBindSchema
    Salts::PluginABI
    ${DATABIND_COMPILER_TOOLING_TARGET}
    Salts::Core
  INCLUDES
    ${DATABIND_SOURCE_ROOT}/schema/src
    ${CMAKE_CURRENT_SOURCE_DIR}
  FOLDER "databind/compiler")

add_custom_command(
  OUTPUT
    "${DATABIND_PLUGIN_TEST_NATIVE_H}"
    "${DATABIND_PLUGIN_TEST_BINDING_H}"
    "${DATABIND_PLUGIN_TEST_BINDING_C}"
    "${DATABIND_PLUGIN_TEST_SERVICE_H}"
    "${DATABIND_PLUGIN_TEST_SOURCE}"
  COMMAND
    $<TARGET_FILE:databindc>
      "${CMAKE_CURRENT_SOURCE_DIR}/test_plugin_projection.schema"
      --lang c
      --output "${DATABIND_PLUGIN_TEST_NATIVE_H}"
  COMMAND
    $<TARGET_FILE:databindc>
      "${CMAKE_CURRENT_SOURCE_DIR}/test_plugin_projection.schema"
      --lang c
      --output "${DATABIND_PLUGIN_TEST_BINDING_H}"
      --source-output "${DATABIND_PLUGIN_TEST_BINDING_C}"
  COMMAND
    $<TARGET_FILE:databind_plugin_codegen_fixture_tool>
      "${CMAKE_CURRENT_SOURCE_DIR}/test_plugin_projection.schema"
      "${DATABIND_PLUGIN_TEST_SOURCE}"
      "${DATABIND_PLUGIN_TEST_SERVICE_H}"
      "image_native.h"
  DEPENDS
    databindc
    databind_plugin_codegen_fixture_tool
    "${CMAKE_CURRENT_SOURCE_DIR}/test_plugin_projection.schema"
  VERBATIM
  COMMENT "Generating DataBind Service Plugin fixture")

add_custom_target(databind_plugin_generated_fixture_sources
  DEPENDS
    "${DATABIND_PLUGIN_TEST_NATIVE_H}"
    "${DATABIND_PLUGIN_TEST_BINDING_H}"
    "${DATABIND_PLUGIN_TEST_BINDING_C}"
    "${DATABIND_PLUGIN_TEST_SERVICE_H}"
    "${DATABIND_PLUGIN_TEST_SOURCE}")

set_source_files_properties(
  "${DATABIND_PLUGIN_TEST_BINDING_C}"
  "${DATABIND_PLUGIN_TEST_SOURCE}"
  PROPERTIES GENERATED TRUE)

add_library(databind_plugin_generated_fixture SHARED
  "${DATABIND_PLUGIN_TEST_SOURCE}"
  test_plugin_business_fixture.c)
add_dependencies(databind_plugin_generated_fixture
  databind_plugin_generated_fixture_sources)
target_compile_features(databind_plugin_generated_fixture PRIVATE c_std_11)
target_include_directories(databind_plugin_generated_fixture PRIVATE
  "${DATABIND_PLUGIN_TEST_GENERATED_DIR}")
target_link_libraries(databind_plugin_generated_fixture PRIVATE
  Salts::PluginABI
  Salts::DataBindSchema)
set_target_properties(databind_plugin_generated_fixture PROPERTIES
  PREFIX ""
  FOLDER "databind/compiler")

cmake_add_test(test_databind_plugin_generated_dso
  SOURCES
    test_plugin_generated_dso.c
    "${DATABIND_PLUGIN_TEST_BINDING_C}"
  LIBS Salts::Plugin Salts::DataBind Salts::TinyTest
  DEFS
    GENERATED_DATABIND_PLUGIN_PATH="$<TARGET_FILE:databind_plugin_generated_fixture>"
  INCLUDES
    "${DATABIND_PLUGIN_TEST_GENERATED_DIR}"
  FOLDER "databind/compiler")
add_dependencies(test_databind_plugin_generated_dso
  databind_plugin_generated_fixture)

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
    PLUGIN_TYPED_ERROR_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/test_plugin_typed_error.schema"
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
