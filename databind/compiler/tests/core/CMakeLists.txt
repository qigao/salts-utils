add_test(NAME test_databind_compiler_tooling_boundary
  COMMAND "${CMAKE_COMMAND}"
    "-DDATABIND_SOURCE_ROOT=${DATABIND_SOURCE_ROOT}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/test_compiler_tooling_boundary.cmake")
set_tests_properties(test_databind_compiler_tooling_boundary
  PROPERTIES LABELS "databind;package;compiler")

cmake_add_test(test_tbe_compiler
  SOURCES test_tbe_compiler.c "${DATABIND_COMPILER_SOURCE_DIR}/compiler_core.c" "${DATABIND_COMPILER_SOURCE_DIR}/plugin_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/wasm_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/native_service_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/service_native.c" "${DATABIND_COMPILER_SOURCE_DIR}/binary_reader_codegen.c" "${DATABIND_COMPILER_SOURCE_DIR}/projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/database_schema.c" "${DATABIND_COMPILER_SOURCE_DIR}/mustache_helpers.c"
  LIBS Salts::IDL Salts::TinyTest ${DATABIND_COMPILER_TOOLING_TARGET} databind_binding_compiler Salts::Core
  DEFS
    SCHEMA_EXAMPLE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/example.schema"
    C_STRUCT_TEMPLATE_FILE="${DATABIND_COMPILER_SOURCE_DIR}/templates/c/c_structs.mustache"
    CPP_TYPES_TEMPLATE_FILE="${DATABIND_COMPILER_SOURCE_DIR}/templates/cpp/cpp_types.mustache"
    GO_TYPES_TEMPLATE_FILE="${DATABIND_COMPILER_SOURCE_DIR}/templates/go/go_types.mustache"
    PYTHON_DATACLASS_TEMPLATE_FILE="${DATABIND_COMPILER_SOURCE_DIR}/templates/python/python_dataclass.mustache"
    RUST_STRUCTS_TEMPLATE_FILE="${DATABIND_COMPILER_SOURCE_DIR}/templates/rust/rust_structs.mustache"
    TS_TYPES_TEMPLATE_FILE="${DATABIND_COMPILER_SOURCE_DIR}/templates/typescript/ts_types.mustache"
    POSTGRESQL_SECURITY_PROBE_SCHEMA_FILE="${CMAKE_CURRENT_SOURCE_DIR}/test_postgresql_security_probe.schema"
    TBE_COMPILER_RESOURCE_DIR="${DATABIND_COMPILER_SOURCE_DIR}"
  FOLDER "databind/compiler/tests/core")
target_include_directories(test_tbe_compiler
  PRIVATE ${DATABIND_COMPILER_SOURCE_DIR}
          ${DATABIND_SOURCE_ROOT}/idl/contract
          ${DATABIND_SOURCE_ROOT}/formats/binary/include)

cmake_add_test(test_tbe_compiler_schema_boundary
  SOURCES test_schema_boundary.c "${DATABIND_COMPILER_SOURCE_DIR}/compiler_core.c" "${DATABIND_COMPILER_SOURCE_DIR}/plugin_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/wasm_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/native_service_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/service_native.c" "${DATABIND_COMPILER_SOURCE_DIR}/binary_reader_codegen.c" "${DATABIND_COMPILER_SOURCE_DIR}/projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/database_schema.c" "${DATABIND_COMPILER_SOURCE_DIR}/mustache_helpers.c"
  LIBS Salts::IDL Salts::TinyTest ${DATABIND_COMPILER_TOOLING_TARGET} databind_binding_compiler Salts::Core
  DEFS TBE_COMPILER_RESOURCE_DIR="${DATABIND_COMPILER_SOURCE_DIR}"
  FOLDER "databind/compiler/tests/core")
target_include_directories(test_tbe_compiler_schema_boundary
  PRIVATE ${DATABIND_COMPILER_SOURCE_DIR} ${DATABIND_SOURCE_ROOT}/idl/contract)

target_sources(test_tbe_compiler_schema_boundary PRIVATE test_scalar_projection.c)
set_source_files_properties(test_scalar_projection.c
  PROPERTIES COMPILE_DEFINITIONS TINYTEST_NO_MAIN)

target_sources(test_tbe_compiler_schema_boundary
  PRIVATE test_sql_projection_independence.c)
set_source_files_properties(test_sql_projection_independence.c
  PROPERTIES COMPILE_DEFINITIONS TINYTEST_NO_MAIN)
