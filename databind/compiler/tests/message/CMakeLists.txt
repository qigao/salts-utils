set(DATABIND_NULLABLE_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/nullable_generated")
file(MAKE_DIRECTORY "${DATABIND_NULLABLE_GENERATED_DIR}")
set(DATABIND_NULLABLE_GENERATED_H
    "${DATABIND_NULLABLE_GENERATED_DIR}/nullable_generated.h")
set(DATABIND_NULLABLE_GENERATED_C
    "${DATABIND_NULLABLE_GENERATED_DIR}/nullable_generated.c")

add_custom_command(
  OUTPUT
    "${DATABIND_NULLABLE_GENERATED_H}"
    "${DATABIND_NULLABLE_GENERATED_C}"
  COMMAND
    $<TARGET_FILE:salts_idlc>
      "${CMAKE_CURRENT_SOURCE_DIR}/test_nullable_codegen.schema"
      --lang c
      --output "${DATABIND_NULLABLE_GENERATED_H}"
      --source-output "${DATABIND_NULLABLE_GENERATED_C}"
  DEPENDS
    salts_idlc
    "${CMAKE_CURRENT_SOURCE_DIR}/test_nullable_codegen.schema"
  VERBATIM
  COMMENT "Generating nullable DataBind artifact")

set_source_files_properties(
  "${DATABIND_NULLABLE_GENERATED_C}"
  PROPERTIES GENERATED TRUE)

cmake_add_test(test_databind_nullable_codegen
  SOURCES
    test_nullable_codegen.c
    "${DATABIND_NULLABLE_GENERATED_C}"
  LIBS
    Salts::DataBind
    Salts::CFlow
    Salts::TinyTest
  INCLUDES
    "${DATABIND_NULLABLE_GENERATED_DIR}"
  FOLDER "databind/compiler/tests/message")
set_tests_properties(test_databind_nullable_codegen
  PROPERTIES LABELS "databind;compiler;nullable")

cmake_add_test(test_databind_message_native
  SOURCES
    test_message_native.c
    "${DATABIND_COMPILER_SOURCE_DIR}/compiler_core.c" "${DATABIND_COMPILER_SOURCE_DIR}/native_service_projection.c" "${DATABIND_COMPILER_SOURCE_DIR}/service_native.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/binary_reader_codegen.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/database_schema.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/mustache_helpers.c"
  LIBS
    Salts::IDL
    Salts::TinyTest
    ${DATABIND_COMPILER_TOOLING_TARGET}
    Salts::Core
  DEFS
    MESSAGE_NATIVE_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/test_message_native.schema"
    TBE_COMPILER_RESOURCE_DIR="${DATABIND_COMPILER_SOURCE_DIR}"
  INCLUDES
    ${DATABIND_SOURCE_ROOT}/idl/contract
    ${DATABIND_COMPILER_SOURCE_DIR}
  FOLDER "databind/compiler/tests/message")
set_target_properties(test_databind_message_native PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
set_tests_properties(test_databind_message_native PROPERTIES
  LABELS "databind;compiler;message-native;channel")

salts_idl_target(
  TARGET databind_message_native_artifact_fixture
  FOLDER "databind/compiler/generated/message"
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/test_message_native_artifact.schema"
  ARTIFACT_NAME "message_native_artifact"
  ARTIFACTS MESSAGE)

salts_idl_target(
  TARGET databind_nested_native_generics_fixture
  FOLDER "databind/compiler/generated/message"
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/test_nested_native_generics.schema"
  ARTIFACT_NAME "nested_native_generics"
  ARTIFACTS MESSAGE)

cmake_add_test(test_databind_nested_native_generics
  SOURCES test_nested_native_generics.c test_nested_native_generics_cpp.cpp
  LIBS Salts::DataBind Salts::TinyTest
    ${databind_nested_native_generics_fixture_NATIVE_TARGET}
  INCLUDES "${databind_nested_native_generics_fixture_GENERATED_DIR}"
  FOLDER "databind/compiler/tests/message")
add_dependencies(test_databind_nested_native_generics
  databind_nested_native_generics_fixture_idl_codegen)
set_target_properties(test_databind_nested_native_generics PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
set_tests_properties(test_databind_nested_native_generics PROPERTIES
  LABELS "databind;compiler;generic;ownership;cpp")

cmake_add_test(test_databind_generated_text_cleanup
  SOURCES test_generated_text_cleanup.c
  LIBS Salts::DataBind Salts::TinyTest
  INCLUDES "${databind_nested_native_generics_fixture_GENERATED_DIR}"
  FOLDER "databind/compiler/tests/message")
add_dependencies(test_databind_generated_text_cleanup
  databind_nested_native_generics_fixture_idl_codegen)
set_tests_properties(test_databind_generated_text_cleanup PROPERTIES
  LABELS "databind;compiler;ownership;cleanup")

cmake_add_test(test_databind_message_native_artifact
  SOURCES
    test_public_message_native_artifact.c
    test_public_message_native_artifact_cpp.cpp
  LIBS
    Salts::DataBind
    Salts::TinyTest
    ${databind_message_native_artifact_fixture_NATIVE_TARGET}
  INCLUDES
    "${databind_message_native_artifact_fixture_GENERATED_DIR}"
  FOLDER "databind/compiler/tests/message")
add_dependencies(test_databind_message_native_artifact
  databind_message_native_artifact_fixture_idl_codegen)
set_target_properties(test_databind_message_native_artifact PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
set_tests_properties(test_databind_message_native_artifact PROPERTIES
  LABELS "databind;compiler;native;message;artifact;public")

target_link_libraries(test_databind_message_native
  PRIVATE databind_binding_compiler)
