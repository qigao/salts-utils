# Generated PLUGIN backend qualification through the canonical public
# salts_idl_target() -> salts_idlc projection frontend.

set(DATABIND_PLUGIN_TEST_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/databind_plugin_generated_fixture.databind")
file(MAKE_DIRECTORY "${DATABIND_PLUGIN_TEST_GENERATED_DIR}")
set(DATABIND_PLUGIN_TEST_NATIVE_H
    "${DATABIND_PLUGIN_TEST_GENERATED_DIR}/image_native.h")
set(DATABIND_PLUGIN_TEST_BINDING_H
    "${DATABIND_PLUGIN_TEST_GENERATED_DIR}/image_binding_native.h")
set(DATABIND_PLUGIN_TEST_BINDING_C
    "${DATABIND_PLUGIN_TEST_GENERATED_DIR}/image_binding_native.c")
set(DATABIND_PLUGIN_TEST_SERVICE_H
    "${DATABIND_PLUGIN_TEST_GENERATED_DIR}/image.plugin.h")
set(DATABIND_PLUGIN_TEST_SOURCE
    "${DATABIND_PLUGIN_TEST_GENERATED_DIR}/image.plugin.c")

salts_idl_target(
  TARGET databind_plugin_generated_fixture
  FOLDER "databind/compiler/generated/plugin"
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/test_plugin_projection.schema"
  COMPONENT "Image.ImageProcessor"
  VERSION "1.0.0"
  ARTIFACT_NAME "image"
  BINARY_CODEC
  ARTIFACTS PLUGIN
  SOURCES test_plugin_business_fixture.c)

get_target_property(
  _databind_plugin_generated_fixture_links
  databind_plugin_generated_fixture_plugin
  LINK_LIBRARIES)
if("Salts::IDL" IN_LIST _databind_plugin_generated_fixture_links)
  message(FATAL_ERROR
          "generated Plugin must not link Salts::IDL")
endif()
if(NOT "Salts::DataBind"
       IN_LIST _databind_plugin_generated_fixture_links)
  message(FATAL_ERROR
          "generated Plugin must link canonical Salts::DataBind")
endif()
if("Salts::DataBindGeneratedABI"
   IN_LIST _databind_plugin_generated_fixture_links)
  message(FATAL_ERROR
          "generated Plugin must not link removed Salts::DataBindGeneratedABI")
endif()

get_target_property(
  _databind_plugin_generated_client_links
  databind_plugin_generated_fixture_plugin_client
  LINK_LIBRARIES)
if("Salts::IDL"
   IN_LIST _databind_plugin_generated_client_links)
  message(FATAL_ERROR
          "generated Plugin client must not link Salts::IDL")
endif()
if(NOT "Salts::Plugin"
       IN_LIST _databind_plugin_generated_client_links)
  message(FATAL_ERROR
          "generated Plugin client must link Salts::Plugin")
endif()
if(NOT "Salts::DataBind"
       IN_LIST _databind_plugin_generated_client_links)
  message(FATAL_ERROR
          "generated Plugin client must link canonical Salts::DataBind")
endif()
if("Salts::DataBindGeneratedABI"
   IN_LIST _databind_plugin_generated_client_links)
  message(FATAL_ERROR
          "generated Plugin client must not link removed Salts::DataBindGeneratedABI")
endif()

add_custom_command(
  OUTPUT
    "${DATABIND_PLUGIN_TEST_BINDING_H}"
    "${DATABIND_PLUGIN_TEST_BINDING_C}"
  COMMAND
    $<TARGET_FILE:salts_idlc>
      "${CMAKE_CURRENT_SOURCE_DIR}/test_plugin_projection.schema"
      --lang c
      --output "${DATABIND_PLUGIN_TEST_BINDING_H}"
      --source-output "${DATABIND_PLUGIN_TEST_BINDING_C}"
  DEPENDS
    salts_idlc
    "${CMAKE_CURRENT_SOURCE_DIR}/test_plugin_projection.schema"
  VERBATIM
  COMMENT "Generating host-side DataBind binding fixture")

set_source_files_properties(
  "${DATABIND_PLUGIN_TEST_BINDING_C}"
  PROPERTIES GENERATED TRUE)

cmake_add_test(test_databind_plugin_generated_dso
  SOURCES
    test_plugin_generated_dso.c
    "${DATABIND_PLUGIN_TEST_BINDING_C}"
  LIBS Salts::DataBindPlugin Salts::TinyTest
  DEFS
    GENERATED_DATABIND_PLUGIN_PATH="$<TARGET_FILE:databind_plugin_generated_fixture_plugin>"
  INCLUDES
    "${DATABIND_PLUGIN_TEST_GENERATED_DIR}"
  FOLDER "databind/compiler/tests/plugin")
add_dependencies(test_databind_plugin_generated_dso
  databind_plugin_generated_fixture_plugin)

salts_idl_target(
  TARGET databind_plugin_generic_descriptor_fixture
  FOLDER "databind/compiler/generated/plugin"
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/test_plugin_generic_descriptor.schema"
  COMPONENT "GenericPlugin.GenericProcessor"
  VERSION "1.0.0"
  ARTIFACT_NAME "generic_plugin"
  BINARY_CODEC
  ARTIFACTS PLUGIN
  SOURCES test_plugin_generic_descriptor_business.c)

cmake_add_test(test_databind_plugin_generic_descriptor_client
  SOURCES test_plugin_generic_descriptor_client.c
  LIBS
    databind_plugin_generic_descriptor_fixture_plugin_client
    Salts::TinyTest
  DEFS
    GENERATED_GENERIC_PLUGIN_PATH="$<TARGET_FILE:databind_plugin_generic_descriptor_fixture_plugin>"
  INCLUDES
    "${databind_plugin_generic_descriptor_fixture_GENERATED_DIR}"
  FOLDER "databind/compiler/tests/plugin")
add_dependencies(test_databind_plugin_generic_descriptor_client
  databind_plugin_generic_descriptor_fixture_plugin
  databind_plugin_generic_descriptor_fixture_plugin_client)
set_target_properties(test_databind_plugin_generic_descriptor_client PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
set_tests_properties(test_databind_plugin_generic_descriptor_client PROPERTIES
  LABELS "databind;compiler;plugin;generic;lease")

salts_idl_target(
  TARGET databind_plugin_typed_error_fixture
  FOLDER "databind/compiler/generated/plugin"
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/test_plugin_typed_error.schema"
  COMPONENT "ErrorPlugin.StorePlugin"
  VERSION "1.0.0"
  ARTIFACT_NAME "error"
  BINARY_CODEC
  ARTIFACTS PLUGIN
  SOURCES test_plugin_typed_error_business_fixture.c)

cmake_add_test(test_databind_plugin_typed_error_client
  SOURCES test_plugin_typed_error_generated_client.c
  LIBS
    databind_plugin_typed_error_fixture_plugin_client
    Salts::DataBindPlugin
    Salts::TinyTest
  DEFS
    GENERATED_TYPED_ERROR_PLUGIN_PATH="$<TARGET_FILE:databind_plugin_typed_error_fixture_plugin>"
  FOLDER "databind/compiler/tests/plugin")
add_dependencies(test_databind_plugin_typed_error_client
  databind_plugin_typed_error_fixture_plugin
  databind_plugin_typed_error_fixture_plugin_client)

add_executable(test_databind_plugin_typed_error_client_cpp
  test_plugin_typed_error_generated_client_cpp.cpp)
target_compile_features(test_databind_plugin_typed_error_client_cpp PRIVATE
  cxx_std_17)
target_link_libraries(test_databind_plugin_typed_error_client_cpp PRIVATE
  databind_plugin_typed_error_fixture_plugin_client)
set_target_properties(test_databind_plugin_typed_error_client_cpp PROPERTIES
  CXX_EXTENSIONS NO)
cmake_config_target(test_databind_plugin_typed_error_client_cpp
  FOLDER "databind/compiler/tests/plugin")
add_test(NAME test_databind_plugin_typed_error_client_cpp
  COMMAND test_databind_plugin_typed_error_client_cpp)

salts_idl_target(
  TARGET databind_plugin_owned_typed_error_fixture
  FOLDER "databind/compiler/generated/plugin"
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/test_plugin_owned_typed_error.schema"
  COMPONENT "OwnedErrorPlugin.StorePlugin"
  VERSION "1.0.0"
  ARTIFACT_NAME "owned_error"
  BINARY_CODEC
  ARTIFACTS PLUGIN
  SOURCES test_plugin_owned_typed_error_business_fixture.c)

cmake_add_test(test_databind_plugin_owned_typed_error_client
  SOURCES test_plugin_owned_typed_error_generated_client.c
  LIBS
    databind_plugin_owned_typed_error_fixture_plugin_client
    Salts::TinyTest
  DEFS
    GENERATED_OWNED_TYPED_ERROR_PLUGIN_PATH="$<TARGET_FILE:databind_plugin_owned_typed_error_fixture_plugin>"
  FOLDER "databind/compiler/tests/plugin")
add_dependencies(test_databind_plugin_owned_typed_error_client
  databind_plugin_owned_typed_error_fixture_plugin
  databind_plugin_owned_typed_error_fixture_plugin_client)

add_executable(test_databind_plugin_owned_typed_error_client_cpp
  test_plugin_owned_typed_error_generated_client_cpp.cpp)
target_compile_features(test_databind_plugin_owned_typed_error_client_cpp PRIVATE
  cxx_std_17)
target_link_libraries(test_databind_plugin_owned_typed_error_client_cpp PRIVATE
  databind_plugin_owned_typed_error_fixture_plugin_client)
set_target_properties(test_databind_plugin_owned_typed_error_client_cpp PROPERTIES
  CXX_EXTENSIONS NO)
cmake_config_target(test_databind_plugin_owned_typed_error_client_cpp
  FOLDER "databind/compiler/tests/plugin")
add_test(NAME test_databind_plugin_owned_typed_error_client_cpp
  COMMAND test_databind_plugin_owned_typed_error_client_cpp)

cmake_add_test(test_databind_plugin_generated_client
  SOURCES test_plugin_generated_client.c
  LIBS
    databind_plugin_generated_fixture_plugin_client
    Salts::TinyTest
  DEFS
    GENERATED_DATABIND_PLUGIN_PATH="$<TARGET_FILE:databind_plugin_generated_fixture_plugin>"
  FOLDER "databind/compiler/tests/plugin")
add_dependencies(test_databind_plugin_generated_client
  databind_plugin_generated_fixture_plugin
  databind_plugin_generated_fixture_plugin_client)

add_executable(test_databind_plugin_generated_client_cpp
  test_plugin_generated_client_cpp.cpp)
target_compile_features(test_databind_plugin_generated_client_cpp PRIVATE
  cxx_std_17)
target_link_libraries(test_databind_plugin_generated_client_cpp PRIVATE
  databind_plugin_generated_fixture_plugin_client)
set_target_properties(test_databind_plugin_generated_client_cpp PROPERTIES
  CXX_EXTENSIONS NO)
cmake_config_target(test_databind_plugin_generated_client_cpp
  FOLDER "databind/compiler/tests/plugin")
add_test(NAME test_databind_plugin_generated_client_cpp
  COMMAND test_databind_plugin_generated_client_cpp)
