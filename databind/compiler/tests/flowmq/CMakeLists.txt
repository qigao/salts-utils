cmake_add_test(test_databind_flowmq_plan_projection
  SOURCES
    test_flowmq_plan_projection.c
    "${DATABIND_COMPILER_SOURCE_DIR}/projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/flowmq_plan_projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/binary_reader_codegen.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/opaque_plan_codegen.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/compiler_core.c" "${DATABIND_COMPILER_SOURCE_DIR}/native_source_ir.c" "${DATABIND_COMPILER_SOURCE_DIR}/plugin_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/wasm_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/native_service_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/service_native.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/database_schema.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/mustache_helpers.c"
  LIBS
    Salts::IDL
    Salts::TinyTest
    ${DATABIND_COMPILER_TOOLING_TARGET}
    Salts::Core
  DEFS
    FLOWMQ_PLAN_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/test_flowmq_plan_projection.schema"
  INCLUDES
    ${DATABIND_SOURCE_ROOT}/idl/contract
    ${DATABIND_COMPILER_SOURCE_DIR}
  FOLDER "databind/compiler/tests/flowmq")
set_target_properties(test_databind_flowmq_plan_projection PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
set_tests_properties(test_databind_flowmq_plan_projection PROPERTIES
  LABELS "databind;compiler;flowmq-plan;transport")

salts_idl_target(
  TARGET databind_flowmq_plan_public_fixture
  FOLDER "databind/compiler/generated/flowmq"
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/test_flowmq_plan_public.schema"
  ARTIFACT_NAME "device"
  TRANSPORTS FLOWMQ
  PROJECTION_CONFIG
    "${CMAKE_CURRENT_SOURCE_DIR}/test_flowmq_plan_projection_config.json")


salts_idl_target(
  TARGET databind_flowmq_service_plan_public_fixture
  FOLDER "databind/compiler/generated/flowmq"
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/test_flowmq_service_plan.schema"
  ARTIFACT_NAME "service"
  TRANSPORTS FLOWMQ
  PROJECTION_CONFIG
    "${CMAKE_CURRENT_SOURCE_DIR}/test_flowmq_service_plan_config.json")

cmake_add_test(test_databind_flowmq_plan_public_frontend
  SOURCES test_public_flowmq_plan_frontend.c
  LIBS
    Salts::DataBind
    Salts::TinyTest
    ${databind_flowmq_plan_public_fixture_NATIVE_TARGET}
  INCLUDES
    "${databind_flowmq_plan_public_fixture_GENERATED_DIR}"
  FOLDER "databind/compiler/tests/flowmq")
add_dependencies(test_databind_flowmq_plan_public_frontend
  databind_flowmq_plan_public_fixture_idl_codegen)
set_target_properties(test_databind_flowmq_plan_public_frontend PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
set_tests_properties(test_databind_flowmq_plan_public_frontend PROPERTIES
  LABELS "databind;compiler;flowmq-plan;public")


cmake_add_test(test_databind_flowmq_service_plan_public_frontend
  SOURCES test_public_flowmq_service_plan.c
  LIBS
    Salts::DataBind
    Salts::TinyTest
    ${databind_flowmq_service_plan_public_fixture_NATIVE_TARGET}
  INCLUDES
    "${databind_flowmq_service_plan_public_fixture_GENERATED_DIR}"
  FOLDER "databind/compiler/tests/flowmq")
add_dependencies(test_databind_flowmq_service_plan_public_frontend
  databind_flowmq_service_plan_public_fixture_idl_codegen)
set_target_properties(test_databind_flowmq_service_plan_public_frontend PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
set_tests_properties(test_databind_flowmq_service_plan_public_frontend PROPERTIES
  LABELS "databind;compiler;flowmq-plan;service;binary;public")

target_link_libraries(test_databind_flowmq_plan_projection
  PRIVATE databind_binding_compiler)
