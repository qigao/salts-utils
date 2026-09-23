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
  SOURCES
    main.c
    compiler_core.c
    projection.c
    plugin_projection.c
    database_schema.c
    mustache_helpers.c
    ${TBE_COMPILER_TEMPLATE_FILES}
  LIBS
    Salts::DataBindSchema
    Salts::Plugin
    ${DATABIND_COMPILER_TOOLING_TARGET}
    Salts::Core
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

cmake_add_test(test_databind_plugin_projection
  SOURCES
    test_plugin_projection.c
    plugin_projection.c
    projection.c
    compiler_core.c
    database_schema.c
    mustache_helpers.c
  LIBS
    Salts::DataBindSchema
    Salts::TinyTest
    Salts::Plugin
    ${DATABIND_COMPILER_TOOLING_TARGET}
    Salts::Core
  DEFS
    PLUGIN_PROJECTION_SCHEMA_FILE="${CMAKE_CURRENT_SOURCE_DIR}/test_plugin_projection.schema"
    PLUGIN_PROJECTION_NO_VERSION_SCHEMA_FILE="${CMAKE_CURRENT_SOURCE_DIR}/test_plugin_projection_no_version.schema"
    PLUGIN_PROJECTION_OUTPUT_FILE="${CMAKE_CURRENT_BINARY_DIR}/test_plugin_projection.generated.c"
    PLUGIN_PROJECTION_INVALID_OUTPUT_FILE="${CMAKE_CURRENT_BINARY_DIR}/test_plugin_projection.invalid.c"
    PLUGIN_NATIVE_HEADER_OUTPUT_FILE="${CMAKE_CURRENT_BINARY_DIR}/image.generated.h"
    PLUGIN_NATIVE_SOURCE_OUTPUT_FILE="${CMAKE_CURRENT_BINARY_DIR}/image.generated.c"
    TBE_COMPILER_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  INCLUDES
    ${DATABIND_SOURCE_ROOT}/schema/src
    ${CMAKE_CURRENT_SOURCE_DIR}
  FOLDER "databind/compiler")
set_target_properties(test_databind_plugin_projection PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

set(DATABIND_PLUGIN_FIXTURE_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/plugin-fixture")
set(DATABIND_PLUGIN_FIXTURE_HEADER
  "${DATABIND_PLUGIN_FIXTURE_DIR}/image.generated.h")
set(DATABIND_PLUGIN_FIXTURE_SOURCE
  "${DATABIND_PLUGIN_FIXTURE_DIR}/image.generated.c")
set(DATABIND_PLUGIN_FIXTURE_PLUGIN_SOURCE
  "${DATABIND_PLUGIN_FIXTURE_DIR}/image.plugin.c")

cmake_add_executable(test_databind_plugin_fixture_generator
  SOURCES
    test_plugin_projection_generator.c
    plugin_projection.c
    projection.c
    compiler_core.c
    database_schema.c
    mustache_helpers.c
  LIBS
    Salts::DataBindSchema
    Salts::Plugin
    ${DATABIND_COMPILER_TOOLING_TARGET}
    Salts::Core
  DEFS
    TBE_COMPILER_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  FOLDER "databind/compiler/tests")
target_include_directories(test_databind_plugin_fixture_generator PRIVATE
  ${DATABIND_SOURCE_ROOT}/schema/src
  ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(test_databind_plugin_fixture_generator PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

add_custom_command(
  OUTPUT
    "${DATABIND_PLUGIN_FIXTURE_HEADER}"
    "${DATABIND_PLUGIN_FIXTURE_SOURCE}"
    "${DATABIND_PLUGIN_FIXTURE_PLUGIN_SOURCE}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
          "${DATABIND_PLUGIN_FIXTURE_DIR}"
  COMMAND "$<TARGET_FILE:test_databind_plugin_fixture_generator>"
          "${CMAKE_CURRENT_SOURCE_DIR}/test_plugin_projection.schema"
          "${DATABIND_PLUGIN_FIXTURE_HEADER}"
          "${DATABIND_PLUGIN_FIXTURE_SOURCE}"
          "${DATABIND_PLUGIN_FIXTURE_PLUGIN_SOURCE}"
  DEPENDS
    test_databind_plugin_fixture_generator
    "${CMAKE_CURRENT_SOURCE_DIR}/test_plugin_projection.schema"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_structs.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_typed_source.mustache"
  VERBATIM)

set_source_files_properties(
  "${DATABIND_PLUGIN_FIXTURE_PLUGIN_SOURCE}"
  PROPERTIES GENERATED TRUE)

add_library(test_databind_generated_plugin SHARED
  "${DATABIND_PLUGIN_FIXTURE_PLUGIN_SOURCE}"
  test_plugin_projection_business.c)
set_source_files_properties(
  test_plugin_projection_business.c
  PROPERTIES
    OBJECT_DEPENDS "${DATABIND_PLUGIN_FIXTURE_HEADER}")
target_include_directories(test_databind_generated_plugin PRIVATE
  "${DATABIND_PLUGIN_FIXTURE_DIR}")
target_link_libraries(test_databind_generated_plugin PRIVATE
  Salts::Plugin
  Salts::DataBind)
set_target_properties(test_databind_generated_plugin PROPERTIES
  FOLDER "databind/compiler/tests"
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(test_databind_plugin_projection_dso
  SOURCES test_plugin_projection_dso.c
  LIBS
    Salts::Plugin
    Salts::DataBind
    Salts::TinyTest
  INCLUDES
    "${DATABIND_PLUGIN_FIXTURE_DIR}"
  DEFS
    "DATABIND_GENERATED_PLUGIN_PATH=\"$<TARGET_FILE:test_databind_generated_plugin>\""
  RUNTIME_TARGETS
    test_databind_generated_plugin
  FOLDER "databind/compiler/tests")
add_dependencies(
  test_databind_plugin_projection_dso
  test_databind_generated_plugin)
set_target_properties(test_databind_plugin_projection_dso PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

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
