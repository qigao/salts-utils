cmake_add_test(test_tbe_compiler_cmeta_fields
  SOURCES test_cmeta_field_projection.c "${DATABIND_COMPILER_SOURCE_DIR}/compiler_core.c" "${DATABIND_COMPILER_SOURCE_DIR}/wasm_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/native_service_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/service_native.c" "${DATABIND_COMPILER_SOURCE_DIR}/binary_reader_codegen.c" "${DATABIND_COMPILER_SOURCE_DIR}/projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/database_schema.c" "${DATABIND_COMPILER_SOURCE_DIR}/mustache_helpers.c"
  LIBS Salts::IDL Salts::TinyTest ${DATABIND_COMPILER_TOOLING_TARGET} databind_binding_compiler Salts::Core
  FOLDER "databind/compiler/tests/cmeta")
target_include_directories(test_tbe_compiler_cmeta_fields
  PRIVATE ${DATABIND_COMPILER_SOURCE_DIR} ${DATABIND_SOURCE_ROOT}/idl/contract)
