# =============================================================================
# Executable
# =============================================================================

set(DATABIND_COMPILER_TOOLING_TARGET databind_compiler_tooling)
add_library(${DATABIND_COMPILER_TOOLING_TARGET} INTERFACE)
target_link_libraries(${DATABIND_COMPILER_TOOLING_TARGET}
  INTERFACE Salts::CmdParser Salts::Mustache)
set_target_properties(${DATABIND_COMPILER_TOOLING_TARGET}
  PROPERTIES FOLDER "tbe/tbe_compiler")

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
  LIBS Salts::TbeSchema ${DATABIND_COMPILER_TOOLING_TARGET} Salts::Core
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

add_test(NAME test_databind_compiler_tooling_boundary
  COMMAND "${CMAKE_COMMAND}"
    "-DPROJECT_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/test_compiler_tooling_boundary.cmake")
set_tests_properties(test_databind_compiler_tooling_boundary
  PROPERTIES LABELS "databind;package;compiler")


cmake_add_test(test_tbe_compiler
  SOURCES test_tbe_compiler.c compiler_core.c database_schema.c mustache_helpers.c
  LIBS Salts::TbeSchema Salts::TinyTest ${DATABIND_COMPILER_TOOLING_TARGET} Salts::Core
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

cmake_add_test(test_tbe_compiler_schema_boundary
  SOURCES test_schema_boundary.c compiler_core.c database_schema.c mustache_helpers.c
  LIBS Salts::TbeSchema Salts::TinyTest ${DATABIND_COMPILER_TOOLING_TARGET} Salts::Core
  DEFS TBE_COMPILER_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  FOLDER "tbe/tbe_compiler")
cmake_add_test(test_tbe_compiler_cmeta_fields
  SOURCES test_cmeta_field_projection.c compiler_core.c database_schema.c mustache_helpers.c
  LIBS Salts::TbeSchema Salts::TinyTest ${DATABIND_COMPILER_TOOLING_TARGET} Salts::Core
  FOLDER "tbe/tbe_compiler")
target_include_directories(test_tbe_compiler_cmeta_fields
  PRIVATE ${CMAKE_SOURCE_DIR}/tbe/schema/src)
target_include_directories(test_tbe_compiler_schema_boundary
  PRIVATE ${CMAKE_SOURCE_DIR}/tbe/schema/src)
if(TARGET test_tbe_compiler_schema_boundary)
  target_sources(test_tbe_compiler_schema_boundary PRIVATE test_scalar_projection.c)
  set_source_files_properties(test_scalar_projection.c
    PROPERTIES COMPILE_DEFINITIONS TINYTEST_NO_MAIN)
endif()
