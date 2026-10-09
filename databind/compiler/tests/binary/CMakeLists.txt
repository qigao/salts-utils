cmake_add_test(test_databind_method_plan_binary_admission
  SOURCES
    test_method_plan_binary_admission.c
    "${DATABIND_COMPILER_SOURCE_DIR}/projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/method_plan_projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/opaque_plan_codegen.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/compiler_core.c" "${DATABIND_COMPILER_SOURCE_DIR}/native_source_ir.c" "${DATABIND_COMPILER_SOURCE_DIR}/plugin_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/wasm_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/native_service_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/service_native.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/binary_reader_codegen.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/database_schema.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/mustache_helpers.c"
  LIBS
    Salts::IDL
    Salts::TinyTest
    ${DATABIND_COMPILER_TOOLING_TARGET} databind_binding_compiler
    Salts::Core
  DEFS
    BINARY_ADMISSION_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/test_method_plan_binary_admission.schema"
  INCLUDES
    ${DATABIND_SOURCE_ROOT}/idl/contract
    ${DATABIND_COMPILER_SOURCE_DIR}
  FOLDER "databind/compiler/tests/binary")
set_target_properties(test_databind_method_plan_binary_admission PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
set_tests_properties(test_databind_method_plan_binary_admission PROPERTIES
  LABELS "databind;compiler;binary-layout;transport")

cmake_add_test(test_databind_binary_reader_codegen
  SOURCES
    test_binary_reader_codegen.c
    "${DATABIND_COMPILER_SOURCE_DIR}/binary_reader_codegen.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/compiler_core.c" "${DATABIND_COMPILER_SOURCE_DIR}/native_source_ir.c" "${DATABIND_COMPILER_SOURCE_DIR}/plugin_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/wasm_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/native_service_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/service_native.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/database_schema.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/mustache_helpers.c"
  LIBS
    Salts::IDL
    Salts::TinyTest
    ${DATABIND_COMPILER_TOOLING_TARGET} databind_binding_compiler
    Salts::Core
  DEFS
    BINARY_SCALAR_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/test_binary_layout_scalar.schema"
    BINARY_VAR_DATA_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/test_binary_reader_var_data.schema"
    BINARY_GROUP_SCHEMA="${DATABIND_SOURCE_ROOT}/runtime/test_binary_tail_only.schema"
    BINARY_ARRAY_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/test_binary_fixed_array.schema"
    SCHEMA_EXAMPLE_FILE="${DATABIND_COMPILER_TEST_SOURCE_DIR}/core/example.schema"
  INCLUDES
    ${DATABIND_SOURCE_ROOT}/idl/contract
    ${DATABIND_COMPILER_SOURCE_DIR}
  FOLDER "databind/compiler/tests/binary")
set_target_properties(test_databind_binary_reader_codegen PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
set_tests_properties(test_databind_binary_reader_codegen PROPERTIES
  LABELS "databind;compiler;binary-reader")

cmake_add_test(test_databind_binary_layout_ir
  SOURCES
    test_binary_layout_ir.c
    "${DATABIND_COMPILER_SOURCE_DIR}/compiler_core.c" "${DATABIND_COMPILER_SOURCE_DIR}/native_source_ir.c" "${DATABIND_COMPILER_SOURCE_DIR}/plugin_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/wasm_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/native_service_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/service_native.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/binary_reader_codegen.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/database_schema.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/mustache_helpers.c"
  LIBS
    Salts::IDL
    Salts::DataBind
    Salts::TinyTest
    ${DATABIND_COMPILER_TOOLING_TARGET} databind_binding_compiler
    Salts::Core
  DEFS
    SCHEMA_EXAMPLE_FILE="${DATABIND_COMPILER_TEST_SOURCE_DIR}/core/example.schema"
    BINARY_SCALAR_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/test_binary_layout_scalar.schema"
    BINARY_VAR_DATA_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/test_binary_reader_var_data.schema"
    BINARY_ARRAY_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/test_binary_fixed_array.schema"
    BINARY_COUNTED_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/test_binary_counted.schema"
  INCLUDES
    ${DATABIND_SOURCE_ROOT}/idl/contract
    ${DATABIND_COMPILER_SOURCE_DIR}
  FOLDER "databind/compiler/tests/binary")
set_target_properties(test_databind_binary_layout_ir PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
set_tests_properties(test_databind_binary_layout_ir PROPERTIES
  LABELS "databind;compiler;binary-layout")
