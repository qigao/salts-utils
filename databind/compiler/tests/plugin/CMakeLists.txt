# Contract-only public Plugin DSO: compile the canonical Record header,
# dynamically load the provider and exercise lease-safe client ABI. Binary
# *_native.c is not generated or linked for either target.
salts_idl_target(
  TARGET databind_contract_plugin_lease_fixture
  FOLDER "databind/compiler/generated/contract-plugin"
  IDL "${DATABIND_COMPILER_SOURCE_DIR}/package_config/databind_target/native_contract_service.schema"
  COMPONENT "WasmRuntime.Calculator"
  VERSION "1.0.0"
  ARTIFACT_NAME "contract_plugin"
  ARTIFACTS PLUGIN
  SOURCES "${DATABIND_COMPILER_SOURCE_DIR}/package_config/databind_target/contract_plugin_business.c")

add_executable(test_databind_contract_plugin_lease_c11
  "${DATABIND_COMPILER_SOURCE_DIR}/package_config/databind_target/contract_plugin_lease_consumer.c")
target_compile_features(test_databind_contract_plugin_lease_c11 PRIVATE c_std_11)
target_compile_definitions(test_databind_contract_plugin_lease_c11 PRIVATE
  GENERATED_CONTRACT_PLUGIN_PATH="$<TARGET_FILE:databind_contract_plugin_lease_fixture_plugin>")
target_link_libraries(test_databind_contract_plugin_lease_c11 PRIVATE
  databind_contract_plugin_lease_fixture_plugin_client Salts::Plugin)
add_dependencies(test_databind_contract_plugin_lease_c11
  databind_contract_plugin_lease_fixture_plugin
  databind_contract_plugin_lease_fixture_plugin_client)
set_target_properties(test_databind_contract_plugin_lease_c11 PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME test_databind_contract_plugin_lease_c11
  COMMAND $<TARGET_FILE:test_databind_contract_plugin_lease_c11>)
set_tests_properties(test_databind_contract_plugin_lease_c11 PROPERTIES
  LABELS "databind;compiler;plugin;contract-only;lease;no-binary;c11")

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


# #620: opt-in compiler-only candidate; do not change the ordinary PLUGIN
# output, production CMake defaults, provider ABI or loader.
cmake_add_executable(databind_component_candidate_codegen_tool
  SOURCES
    test_component_provider_candidate_codegen_tool.c
    "${DATABIND_COMPILER_SOURCE_DIR}/component_provider_candidate.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/compiler_core.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/native_source_ir.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/plugin_projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/wasm_projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/binary_reader_codegen.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/service_native.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/native_service_projection.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/database_schema.c"
    "${DATABIND_COMPILER_SOURCE_DIR}/mustache_helpers.c"
  LIBS
    Salts::IDL ${DATABIND_COMPILER_TOOLING_TARGET} Salts::Core
  INCLUDES
    "${DATABIND_SOURCE_ROOT}/idl/contract"
    "${DATABIND_COMPILER_SOURCE_DIR}"
  FOLDER "databind/compiler/generated/component-candidate")
target_link_libraries(databind_component_candidate_codegen_tool
  PRIVATE databind_binding_compiler)
set_target_properties(databind_component_candidate_codegen_tool PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)

set(_databind_component_candidate_dir
  "${databind_contract_plugin_lease_fixture_GENERATED_DIR}")
set(_databind_component_candidate_header
  "${_databind_component_candidate_dir}/contract_plugin.component.h")
set(_databind_component_candidate_source
  "${_databind_component_candidate_dir}/contract_plugin.component.c")

add_custom_command(
  OUTPUT
    "${_databind_component_candidate_header}"
    "${_databind_component_candidate_source}"
  COMMAND
    $<TARGET_FILE:databind_component_candidate_codegen_tool>
    "${DATABIND_COMPILER_SOURCE_DIR}/package_config/databind_target/native_contract_service.schema"
    "WasmRuntime.Calculator"
    "contract_plugin.plugin.h"
    "${_databind_component_candidate_header}"
    "${_databind_component_candidate_source}"
  DEPENDS
    databind_component_candidate_codegen_tool
    databind_contract_plugin_lease_fixture_idl_codegen
    "${DATABIND_COMPILER_SOURCE_DIR}/package_config/databind_target/native_contract_service.schema"
  VERBATIM
  COMMENT "Generating isolated exact C11 ComponentProvider FR2 candidate")

add_custom_target(databind_component_candidate_generated ALL
  DEPENDS
    "${_databind_component_candidate_header}"
    "${_databind_component_candidate_source}")
set_target_properties(databind_component_candidate_generated PROPERTIES
  FOLDER "databind/compiler/generated/component-candidate")
set_source_files_properties(
  "${_databind_component_candidate_header}"
  "${_databind_component_candidate_source}" PROPERTIES GENERATED TRUE)

# Real DSO/host test requires an explicitly supplied upstream Salts
# ComponentPlugin source/package target. Its ABI is deliberately not part
# of the currently published Salts.Native SDK and must not be shadowed here.
option(DATABIND_ENABLE_COMPONENT_PROVIDER_CANDIDATE_DSO
  "Run source-qualified unpublished Salts ComponentPlugin DSO tests" OFF)
if(DATABIND_ENABLE_COMPONENT_PROVIDER_CANDIDATE_DSO)
  if(NOT TARGET Salts::ComponentPluginABI OR
     NOT TARGET Salts::ComponentPlugin)
    message(FATAL_ERROR
      "ComponentProvider DSO gate requires exact Salts ComponentPluginABI and ComponentPlugin targets; no fallback or shadow ABI allowed")
  endif()
add_library(databind_component_candidate_provider SHARED
  "${_databind_component_candidate_header}"
  "${_databind_component_candidate_source}"
  "${DATABIND_COMPILER_SOURCE_DIR}/package_config/databind_target/contract_plugin_business.c")
add_dependencies(databind_component_candidate_provider
  databind_component_candidate_generated)
target_compile_features(databind_component_candidate_provider PRIVATE c_std_11)
target_include_directories(databind_component_candidate_provider PRIVATE
  "${_databind_component_candidate_dir}")
target_link_libraries(databind_component_candidate_provider PRIVATE
  Salts::ComponentPluginABI Salts::PluginABI Salts::CMeta Salts::DataBind)
set_target_properties(databind_component_candidate_provider PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  PREFIX "" OUTPUT_NAME "contract_component_candidate"
  FOLDER "databind/compiler/generated/component-candidate")

add_executable(test_databind_component_candidate_dso_c11
  test_component_provider_candidate_dso.c)
add_dependencies(test_databind_component_candidate_dso_c11
  databind_component_candidate_provider)
target_include_directories(test_databind_component_candidate_dso_c11 PRIVATE
  "${_databind_component_candidate_dir}")
target_compile_definitions(test_databind_component_candidate_dso_c11 PRIVATE
  GENERATED_COMPONENT_PROVIDER_PATH="$<TARGET_FILE:databind_component_candidate_provider>"
  DATABIND_COMPONENT_PROVIDER_STRICT_SOURCE=1)
target_link_libraries(test_databind_component_candidate_dso_c11 PRIVATE
  Salts::ComponentPlugin Salts::Plugin Salts::CMeta Salts::DataBind)
set_target_properties(test_databind_component_candidate_dso_c11 PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  FOLDER "databind/compiler/tests/plugin")
add_test(NAME test_databind_component_candidate_dso_c11
  COMMAND test_databind_component_candidate_dso_c11)
set_tests_properties(test_databind_component_candidate_dso_c11 PROPERTIES
  LABELS "databind;compiler;component;real-dso;no-fallback;c11")

add_executable(test_databind_component_candidate_header_cpp
  test_component_provider_candidate_cpp.cpp)
add_dependencies(test_databind_component_candidate_header_cpp
  databind_component_candidate_generated)
target_include_directories(test_databind_component_candidate_header_cpp PRIVATE
  "${_databind_component_candidate_dir}")
target_link_libraries(test_databind_component_candidate_header_cpp PRIVATE
  Salts::ComponentPluginABI Salts::CMeta Salts::DataBind)
set_target_properties(test_databind_component_candidate_header_cpp PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
  FOLDER "databind/compiler/tests/plugin")
add_test(NAME test_databind_component_candidate_header_cpp
  COMMAND test_databind_component_candidate_header_cpp)
set_tests_properties(test_databind_component_candidate_header_cpp PROPERTIES
  LABELS "databind;compiler;component;fr2;cpp17")

endif() # DATABIND_ENABLE_COMPONENT_PROVIDER_CANDIDATE_DSO

# Codegen-only conformance is always active in test builds. It proves
# canonical one-Service FR2 shapes, exact metadata declarations, standard
# ProviderBinding and no generated Function-only fallback without linking an
# unpublished runtime/ABI from a published SDK.
add_test(NAME test_databind_component_candidate_codegen_content
  COMMAND "${CMAKE_COMMAND}"
    "-DHEADER=${_databind_component_candidate_header}"
    "-DSOURCE=${_databind_component_candidate_source}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_component_candidate_content.cmake")
set_tests_properties(test_databind_component_candidate_codegen_content PROPERTIES
  LABELS "databind;compiler;component;codegen;candidate;no-fallback")
