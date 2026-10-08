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
    "${DATABIND_COMPILER_SOURCE_DIR}/compiler_core.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/binary_reader_codegen.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/service_native.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/native_service_projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/database_schema.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/mustache_helpers.c"
  LIBS
    Salts::IDL
    ${DATABIND_COMPILER_TOOLING_TARGET}
    Salts::Core
  INCLUDES
    ${DATABIND_SOURCE_ROOT}/idl/contract
    ${DATABIND_COMPILER_SOURCE_DIR}
  FOLDER "databind/compiler/generated/native")

add_custom_command(
  OUTPUT
    "${DATABIND_SERVICE_NATIVE_RECORD_H}"
    "${DATABIND_SERVICE_NATIVE_RECORD_C}"
    "${DATABIND_SERVICE_NATIVE_HEADER}"
    "${DATABIND_SERVICE_NATIVE_SOURCE}"
  COMMAND
    $<TARGET_FILE:salts_idlc>
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
    salts_idlc
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

cmake_config_target(databind_service_native_generated_sources
  FOLDER "databind/compiler/generated/native")

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
    Salts::CFlow
    Salts::TinyTest
  INCLUDES
    "${DATABIND_SERVICE_NATIVE_TEST_DIR}"
  FOLDER "databind/compiler/tests/native")
add_dependencies(test_databind_service_native_lowering
  databind_service_native_generated_sources)

cmake_add_test(test_databind_service_native_public_cpp
  SOURCES test_service_native_public_cpp.cpp
  LIBS Salts::DataBind
  INCLUDES
    "${DATABIND_SERVICE_NATIVE_TEST_DIR}"
  FOLDER "databind/compiler/tests/native")
add_dependencies(test_databind_service_native_public_cpp
  databind_service_native_generated_sources)
set_target_properties(test_databind_service_native_public_cpp PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO)

salts_idl_target(
  TARGET databind_native_service_public_fixture
  FOLDER "databind/compiler/generated/native"
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/test_service_native_lowering.schema"
  ARTIFACT_NAME "service_native_public"
  ARTIFACTS NATIVE
  TRANSPORTS HTTP RPC)

target_sources(
  ${databind_native_service_public_fixture_NATIVE_TARGET}
  PRIVATE test_public_native_service_business.c)

cmake_add_test(test_databind_native_service_public_artifact
  SOURCES test_public_native_service_artifact.c
  LIBS
    Salts::DataBind
    Salts::TinyTest
    ${databind_native_service_public_fixture_NATIVE_TARGET}
  INCLUDES
    "${databind_native_service_public_fixture_GENERATED_DIR}"
  FOLDER "databind/compiler/tests/native")
add_dependencies(test_databind_native_service_public_artifact
  databind_native_service_public_fixture_idl_codegen)
set_target_properties(test_databind_native_service_public_artifact PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
set_tests_properties(test_databind_native_service_public_artifact PROPERTIES
  LABELS "databind;compiler;native-service;method-plan;public")

cmake_add_test(test_databind_native_service_public_artifact_cpp
  SOURCES test_public_native_service_artifact_cpp.cpp
  LIBS
    Salts::DataBind
    ${databind_native_service_public_fixture_NATIVE_TARGET}
  INCLUDES
    "${databind_native_service_public_fixture_GENERATED_DIR}"
  FOLDER "databind/compiler/tests/native")
add_dependencies(test_databind_native_service_public_artifact_cpp
  databind_native_service_public_fixture_idl_codegen)
set_target_properties(test_databind_native_service_public_artifact_cpp PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
set_tests_properties(test_databind_native_service_public_artifact_cpp PROPERTIES
  LABELS "databind;compiler;native-service;public;cpp")

target_link_libraries(databind_service_native_codegen_fixture_tool
  PRIVATE databind_binding_compiler)
