cmake_add_test(test_databind_openapi_projection
  SOURCES
    test_openapi_projection.c
    "${DATABIND_COMPILER_SOURCE_DIR}/compiler_core.c" "${DATABIND_COMPILER_SOURCE_DIR}/wasm_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/native_service_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/service_native.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/binary_reader_codegen.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/openapi_projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/database_schema.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/mustache_helpers.c"
  LIBS
    Salts::IDL
    Salts::TinyTest
    ${DATABIND_COMPILER_TOOLING_TARGET} databind_binding_compiler
    Salts::Core
  DEFS
    OPENAPI_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/test_openapi_projection.schema"
    OPENAPI_OPTIONAL_PATH_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/test_openapi_optional_path.schema"
    OPENAPI_BYTES_SIZE_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/test_openapi_bytes_size.schema"
  INCLUDES
    ${DATABIND_SOURCE_ROOT}/idl/contract
    ${DATABIND_COMPILER_SOURCE_DIR}
  FOLDER "databind/compiler/tests/openapi")
set_target_properties(test_databind_openapi_projection PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
set_tests_properties(test_databind_openapi_projection PROPERTIES
  LABELS "databind;compiler;openapi;http;validation")
