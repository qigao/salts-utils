salts_idl_target(
  TARGET databind_channel_multi_transport_fixture
  FOLDER "databind/compiler/generated/multi-transport"
  IDL "${DATABIND_COMPILER_TEST_SOURCE_DIR}/flowmq/test_flowmq_plan_public.schema"
  ARTIFACT_NAME "device_multi"
  TRANSPORTS SOCKET FLOWMQ
  PROJECTION_CONFIG
    "${CMAKE_CURRENT_SOURCE_DIR}/test_flowmq_socket_projection_config.json")

salts_idl_target(
  TARGET databind_binary_transport_provider_fixture
  FOLDER "databind/compiler/generated/multi-transport"
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/test_binary_transport_provider.schema"
  ARTIFACT_NAME "binary_exec"
  TRANSPORTS SOCKET FLOWMQ
  PROJECTION_CONFIG
    "${CMAKE_CURRENT_SOURCE_DIR}/test_binary_transport_provider_config.json")

cmake_add_test(test_databind_binary_transport_provider
  SOURCES test_public_binary_transport_provider.c
  LIBS
    Salts::DataBind
    Salts::TinyTest
    ${databind_binary_transport_provider_fixture_NATIVE_TARGET}
  INCLUDES
    "${databind_binary_transport_provider_fixture_GENERATED_DIR}"
  FOLDER "databind/compiler/tests/multi-transport")
add_dependencies(test_databind_binary_transport_provider
  databind_binary_transport_provider_fixture_idl_codegen)
set_target_properties(test_databind_binary_transport_provider PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
set_tests_properties(test_databind_binary_transport_provider PROPERTIES
  LABELS "databind;compiler;binary-reader;transport;execution")

cmake_add_test(test_databind_binary_transport_provider_cpp
  SOURCES test_public_binary_transport_provider_cpp.cpp
  LIBS
    Salts::DataBind
    ${databind_binary_transport_provider_fixture_NATIVE_TARGET}
  INCLUDES
    "${databind_binary_transport_provider_fixture_GENERATED_DIR}"
  FOLDER "databind/compiler/tests/multi-transport")
add_dependencies(test_databind_binary_transport_provider_cpp
  databind_binary_transport_provider_fixture_idl_codegen)
set_target_properties(test_databind_binary_transport_provider_cpp PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
set_tests_properties(test_databind_binary_transport_provider_cpp PROPERTIES
  LABELS "databind;compiler;binary-reader;transport;public;cpp")


salts_idl_target(
  TARGET databind_opaque_transport_fixture
  FOLDER "databind/compiler/generated/multi-transport"
  IDL "${DATABIND_COMPILER_TEST_SOURCE_DIR}/flowmq/test_flowmq_plan_public.schema"
  ARTIFACT_NAME "opaque_bytes"
  TRANSPORTS SOCKET FLOWMQ
  PROJECTION_CONFIG
    "${CMAKE_CURRENT_SOURCE_DIR}/test_opaque_transport_config.json")

cmake_add_test(test_databind_opaque_transport
  SOURCES test_public_opaque_transport.c
  LIBS
    Salts::DataBind
    Salts::TinyTest
  INCLUDES
    "${databind_opaque_transport_fixture_GENERATED_DIR}"
  FOLDER "databind/compiler/tests/multi-transport")
add_dependencies(test_databind_opaque_transport
  databind_opaque_transport_fixture_idl_codegen)
set_target_properties(test_databind_opaque_transport PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
set_tests_properties(test_databind_opaque_transport PROPERTIES
  LABELS "databind;compiler;opaque;bytes;transport;public")


salts_idl_target(
  TARGET databind_opaque_service_fixture
  FOLDER "databind/compiler/generated/multi-transport"
  IDL "${CMAKE_CURRENT_SOURCE_DIR}/test_opaque_service.schema"
  ARTIFACT_NAME "opaque_service"
  TRANSPORTS HTTP FLOWMQ
  PROJECTION_CONFIG
    "${CMAKE_CURRENT_SOURCE_DIR}/test_opaque_service_config.json")

cmake_add_test(test_databind_opaque_service
  SOURCES test_public_opaque_service.c
  LIBS
    Salts::DataBind
    Salts::TinyTest
  INCLUDES
    "${databind_opaque_service_fixture_GENERATED_DIR}"
  FOLDER "databind/compiler/tests/multi-transport")
add_dependencies(test_databind_opaque_service
  databind_opaque_service_fixture_idl_codegen)
set_target_properties(test_databind_opaque_service PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
set_tests_properties(test_databind_opaque_service PROPERTIES
  LABELS "databind;compiler;opaque;bytes;service;http;flowmq;public")


salts_idl_target(
  TARGET databind_binary_var_data_transport_fixture
  FOLDER "databind/compiler/generated/multi-transport"
  IDL "${DATABIND_COMPILER_TEST_SOURCE_DIR}/binary/test_binary_reader_var_data.schema"
  ARTIFACT_NAME "binary_var"
  TRANSPORTS SOCKET FLOWMQ
  PROJECTION_CONFIG
    "${CMAKE_CURRENT_SOURCE_DIR}/test_binary_var_data_transport_config.json")

cmake_add_test(test_databind_binary_var_data_transport
  SOURCES test_public_binary_var_data_transport.c
  LIBS
    Salts::DataBind
    Salts::TinyTest
    ${databind_binary_var_data_transport_fixture_NATIVE_TARGET}
  INCLUDES
    "${databind_binary_var_data_transport_fixture_GENERATED_DIR}"
  FOLDER "databind/compiler/tests/multi-transport")
add_dependencies(test_databind_binary_var_data_transport
  databind_binary_var_data_transport_fixture_idl_codegen)
set_target_properties(test_databind_binary_var_data_transport PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
set_tests_properties(test_databind_binary_var_data_transport PROPERTIES
  LABELS "databind;compiler;binary-reader;var-data;transport;public")

cmake_add_test(test_databind_channel_multi_transport
  SOURCES test_public_channel_multi_transport.c
  LIBS
    Salts::DataBind
    Salts::TinyTest
    ${databind_channel_multi_transport_fixture_NATIVE_TARGET}
  INCLUDES
    "${databind_channel_multi_transport_fixture_GENERATED_DIR}"
  FOLDER "databind/compiler/tests/multi-transport")
add_dependencies(test_databind_channel_multi_transport
  databind_channel_multi_transport_fixture_idl_codegen)
set_target_properties(test_databind_channel_multi_transport PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
set_tests_properties(test_databind_channel_multi_transport PROPERTIES
  LABELS "databind;compiler;transport;composition")
