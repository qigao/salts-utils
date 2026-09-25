set(DATA_BIND_CORE_TARGET data_bind_core)
add_library(
  ${DATA_BIND_CORE_TARGET} STATIC
  data_bind_format_provider.c
  data_bind_format_provider.h
  data_bind_native.c
  data_bind_native.h
  data_bind_native_descriptor.c
  data_bind_native_descriptor.h)
target_include_directories(
  ${DATA_BIND_CORE_TARGET}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(
  ${DATA_BIND_CORE_TARGET}
  PUBLIC
    Salts::Core
    Salts::CMeta
    Salts::CSTL
    Salts::CSerde)
cmake_config_target(
  ${DATA_BIND_CORE_TARGET}
  ALIAS Salts::DataBindCore
  FOLDER "databind/runtime"
  EXPORT_NAME DataBindCore)

set(DATA_BIND_JSON_ADAPTER_TARGET data_bind_json_adapter)
add_library(
  ${DATA_BIND_JSON_ADAPTER_TARGET} STATIC
  data_bind_json_provider.c
  data_bind_json_provider.h)
target_include_directories(
  ${DATA_BIND_JSON_ADAPTER_TARGET}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(
  ${DATA_BIND_JSON_ADAPTER_TARGET}
  PUBLIC
    Salts::DataBindCore
    Salts::JsonCSerdeAdapter)
cmake_config_target(
  ${DATA_BIND_JSON_ADAPTER_TARGET}
  ALIAS Salts::DataBindJsonAdapter
  FOLDER "databind/runtime"
  EXPORT_NAME DataBindJsonAdapter)

set(DATA_BIND_YAML_ADAPTER_TARGET data_bind_yaml_adapter)
add_library(
  ${DATA_BIND_YAML_ADAPTER_TARGET} STATIC
  data_bind_yaml_provider.c
  data_bind_yaml_provider.h)
target_include_directories(
  ${DATA_BIND_YAML_ADAPTER_TARGET}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(
  ${DATA_BIND_YAML_ADAPTER_TARGET}
  PUBLIC
    Salts::DataBindCore
    Salts::CYamlJsonAdapter
    Salts::JsonCSerdeAdapter)
cmake_config_target(
  ${DATA_BIND_YAML_ADAPTER_TARGET}
  ALIAS Salts::DataBindYamlAdapter
  FOLDER "databind/runtime"
  EXPORT_NAME DataBindYamlAdapter)

set(DATA_BIND_CSV_ADAPTER_TARGET data_bind_csv_adapter)
add_library(
  ${DATA_BIND_CSV_ADAPTER_TARGET} STATIC
  data_bind_csv_provider.c
  data_bind_csv_provider.h)
target_include_directories(
  ${DATA_BIND_CSV_ADAPTER_TARGET}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(
  ${DATA_BIND_CSV_ADAPTER_TARGET}
  PUBLIC
    Salts::DataBindCore
    Salts::CsvParser)
cmake_config_target(
  ${DATA_BIND_CSV_ADAPTER_TARGET}
  ALIAS Salts::DataBindCsvAdapter
  FOLDER "databind/runtime"
  EXPORT_NAME DataBindCsvAdapter)

set(DATA_BIND_XML_ADAPTER_TARGET data_bind_xml_adapter)
add_library(
  ${DATA_BIND_XML_ADAPTER_TARGET} STATIC
  data_bind_xml_provider.c
  data_bind_xml_provider.h)
target_include_directories(
  ${DATA_BIND_XML_ADAPTER_TARGET}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(
  ${DATA_BIND_XML_ADAPTER_TARGET}
  PUBLIC
    Salts::DataBindCore
    Salts::XmlParser)
cmake_config_target(
  ${DATA_BIND_XML_ADAPTER_TARGET}
  ALIAS Salts::DataBindXmlAdapter
  FOLDER "databind/runtime"
  EXPORT_NAME DataBindXmlAdapter)

set(DATA_BIND_TEMPORAL_ADAPTER_TARGET data_bind_temporal_adapter)
add_library(
  ${DATA_BIND_TEMPORAL_ADAPTER_TARGET} STATIC
  data_bind_temporal_adapter.c
  data_bind_temporal_adapter.h)
target_include_directories(
  ${DATA_BIND_TEMPORAL_ADAPTER_TARGET}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(
  ${DATA_BIND_TEMPORAL_ADAPTER_TARGET}
  PUBLIC Salts::DataBindCore
  PRIVATE Salts::DateTimeParser)
cmake_config_target(
  ${DATA_BIND_TEMPORAL_ADAPTER_TARGET}
  ALIAS Salts::DataBindTemporalAdapter
  FOLDER "databind/runtime"
  EXPORT_NAME DataBindTemporalAdapter)

set(DATA_BIND_TARGET data_bind)
add_library(
  ${DATA_BIND_TARGET} SHARED
  data_bind.c
  data_bind.h
  data_bind_internal.h
  data_bind_value_internal.h
  data_bind_schema.c
  data_bind_schema_internal.h
  data_bind_record.c
  data_bind_binding_plan.c
  data_bind_binding_plan.h
  data_bind_method_plan.c
  data_bind_method_plan.h
  data_bind_projection_plan.c
  data_bind_projection_plan.h
  data_bind_validation_plan.c
  data_bind_validation_plan.h
  data_bind_stream_format_state.c
  data_bind_stream_format_state_internal.h
  tbe_typed.c
  tbe_typed.h)
target_compile_definitions(${DATA_BIND_TARGET} PRIVATE DATA_BIND_BUILD_DLL)
target_include_directories(
  ${DATA_BIND_TARGET}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${DATABIND_SOURCE_ROOT}/schema/include>
    $<BUILD_INTERFACE:${DATABIND_SOURCE_ROOT}/formats/tbe/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE $<BUILD_INTERFACE:${DATABIND_SOURCE_ROOT}/schema/parser>)
target_link_libraries(
  ${DATA_BIND_TARGET}
  PRIVATE
    databind_monocypher
    Salts::DataBindJsonAdapter
    Salts::DataBindYamlAdapter
    Salts::DataBindCsvAdapter
    Salts::DataBindXmlAdapter
    Salts::DataBindTemporalAdapter
    Salts::DataBindSchema
  PUBLIC
    Salts::DataBindCore)

if(BUILD_TESTS)
cmake_add_test(test_data_bind_native_descriptor
  SOURCES test_data_bind_native_descriptor.c
  LIBS Salts::DataBindCore Salts::TinyTest
  FOLDER "databind/runtime")
set_target_properties(test_data_bind_native_descriptor PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

  get_target_property(DATA_BIND_PUBLIC_LINKS
    ${DATA_BIND_TARGET} INTERFACE_LINK_LIBRARIES)
  if(DATA_BIND_PUBLIC_LINKS MATCHES
     "QueryVM|DateTimeParser|JsonParser|CsvParser|XmlParser|CYaml")
    message(FATAL_ERROR
      "DataBind public interface leaks a SaltsUtils parser/query target: "
      "${DATA_BIND_PUBLIC_LINKS}")
  endif()
endif()

cmake_config_target(
  ${DATA_BIND_TARGET}
  ALIAS Salts::DataBind
  FOLDER "databind/runtime"
  EXPORT_NAME DataBind)

add_library(data_bind_cmeta SHARED data_bind_cmeta.c data_bind_cmeta.h)
target_compile_definitions(data_bind_cmeta PRIVATE DATA_BIND_CMETA_BUILD_DLL)
target_include_directories(data_bind_cmeta
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
         $<INSTALL_INTERFACE:include>)
target_link_libraries(data_bind_cmeta
  PUBLIC Salts::DataBind Salts::CMeta)
cmake_config_target(
  data_bind_cmeta
  ALIAS Salts::DataBindCMeta
  FOLDER databind/runtime
  EXPORT_NAME DataBindCMeta)

add_library(data_bind_cflow SHARED data_bind_cflow.c data_bind_cflow.h)
target_compile_definitions(data_bind_cflow PRIVATE DATA_BIND_CFLOW_BUILD_DLL)
target_include_directories(data_bind_cflow
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
         $<INSTALL_INTERFACE:include>)
target_link_libraries(data_bind_cflow
  PUBLIC Salts::DataBindCMeta Salts::CFlow)
cmake_config_target(
  data_bind_cflow
  ALIAS Salts::DataBindCFlow
  FOLDER databind/runtime
  EXPORT_NAME DataBindCFlow)

install(
  TARGETS ${DATA_BIND_CORE_TARGET}
          ${DATA_BIND_TARGET} data_bind_cmeta data_bind_cflow
  EXPORT SaltsUtilsTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT DataBind
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT DataBind
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT DataBind)

install(
  TARGETS ${DATA_BIND_JSON_ADAPTER_TARGET}
          ${DATA_BIND_YAML_ADAPTER_TARGET}
          ${DATA_BIND_CSV_ADAPTER_TARGET}
          ${DATA_BIND_XML_ADAPTER_TARGET}
          ${DATA_BIND_TEMPORAL_ADAPTER_TARGET}
  EXPORT SaltsUtilsTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT DataBind
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT DataBind
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT DataBind)
install(
  FILES
    data_bind.h
    data_bind_format_provider.h
    data_bind_native.h
    data_bind_binding_plan.h
    data_bind_method_plan.h
    data_bind_projection_plan.h
    data_bind_validation_plan.h
    data_bind_json_provider.h
    data_bind_yaml_provider.h
    data_bind_csv_provider.h
    data_bind_xml_provider.h
    data_bind_temporal_adapter.h
    data_bind_cflow.h
    data_bind_cmeta.h
    data_bind_native_descriptor.h
    tbe_typed.h
  DESTINATION include
  COMPONENT DataBind)

if(BUILD_TESTS)
add_executable(test_data_bind_format_provider
  test_data_bind_format_provider.c)
target_compile_features(test_data_bind_format_provider PRIVATE c_std_11)
target_link_libraries(test_data_bind_format_provider
  PRIVATE Salts::DataBindCore)
set_target_properties(test_data_bind_format_provider
  PROPERTIES FOLDER "databind/runtime/tests")
add_test(NAME test_data_bind_format_provider
  COMMAND test_data_bind_format_provider)

add_executable(test_data_bind_json_provider
  test_data_bind_json_provider.c)
target_compile_features(test_data_bind_json_provider PRIVATE c_std_11)
target_link_libraries(test_data_bind_json_provider
  PRIVATE Salts::DataBindJsonAdapter)
set_target_properties(test_data_bind_json_provider
  PROPERTIES FOLDER "databind/runtime/tests")
add_test(NAME test_data_bind_json_provider
  COMMAND test_data_bind_json_provider)

add_executable(test_data_bind_yaml_provider
  test_data_bind_yaml_provider.c)
target_compile_features(test_data_bind_yaml_provider PRIVATE c_std_11)
target_link_libraries(test_data_bind_yaml_provider
  PRIVATE Salts::DataBindYamlAdapter)
set_target_properties(test_data_bind_yaml_provider
  PROPERTIES FOLDER "databind/runtime/tests")
add_test(NAME test_data_bind_yaml_provider
  COMMAND test_data_bind_yaml_provider)

add_executable(test_data_bind_csv_provider
  test_data_bind_csv_provider.c)
target_compile_features(test_data_bind_csv_provider PRIVATE c_std_11)
target_link_libraries(test_data_bind_csv_provider
  PRIVATE Salts::DataBindCsvAdapter)
set_target_properties(test_data_bind_csv_provider
  PROPERTIES FOLDER "databind/runtime/tests")
add_test(NAME test_data_bind_csv_provider
  COMMAND test_data_bind_csv_provider)

add_executable(test_data_bind_xml_provider
  test_data_bind_xml_provider.c)
target_compile_features(test_data_bind_xml_provider PRIVATE c_std_11)
target_link_libraries(test_data_bind_xml_provider
  PRIVATE Salts::DataBindXmlAdapter)
set_target_properties(test_data_bind_xml_provider
  PROPERTIES FOLDER "databind/runtime/tests")
add_test(NAME test_data_bind_xml_provider
  COMMAND test_data_bind_xml_provider)

add_executable(test_data_bind_temporal_adapter
  test_data_bind_temporal_adapter.c)
target_compile_features(test_data_bind_temporal_adapter PRIVATE c_std_11)
target_link_libraries(test_data_bind_temporal_adapter
  PRIVATE Salts::DataBindTemporalAdapter)
set_target_properties(test_data_bind_temporal_adapter
  PROPERTIES FOLDER "databind/runtime/tests")
add_test(NAME test_data_bind_temporal_adapter
  COMMAND test_data_bind_temporal_adapter)

set(DATA_BIND_TEST_LIBS Salts::DataBind Salts::TinyTest)
cmake_add_test(test_data_bind
  SOURCES test_data_bind.c
  LIBS ${DATA_BIND_TEST_LIBS}
  INCLUDES ${DATABIND_SOURCE_ROOT}/schema/include
  FOLDER "databind/runtime/tests")
cmake_add_test(test_data_bind_public_api
  SOURCES test_data_bind_public_api.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "databind/runtime/tests")

add_executable(test_data_bind_public_header_boundary
  test_data_bind_public_header_boundary.c)
target_compile_features(test_data_bind_public_header_boundary PRIVATE c_std_11)
target_link_libraries(test_data_bind_public_header_boundary PRIVATE Salts::DataBind)
set_target_properties(test_data_bind_public_header_boundary
  PROPERTIES FOLDER "databind/runtime/tests")
add_test(NAME test_data_bind_public_header_boundary
  COMMAND test_data_bind_public_header_boundary)
cmake_add_test(test_data_bind_value_boundary
  SOURCES test_data_bind_value_boundary.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "databind/runtime/tests")
cmake_add_test(test_data_bind_dynamic_cstl
  SOURCES test_data_bind_dynamic_cstl.c
  LIBS Salts::DataBind Salts::DataBindCMeta Salts::TinyTest
       Salts::CsvParser Salts::JsonParser
  INCLUDES ${DATABIND_SOURCE_ROOT}/schema/include
  FOLDER "databind/runtime/tests")
set_tests_properties(test_data_bind_dynamic_cstl PROPERTIES
  LABELS "databind;cmeta;cstl;dynamic")
cmake_add_test(test_data_bind_schema_reflection_contract
  SOURCES test_data_bind_schema_reflection_contract.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "databind/runtime/tests")
cmake_add_test(test_data_bind_validation_plan
  SOURCES test_data_bind_validation_plan.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "databind/runtime/tests")
set_tests_properties(test_data_bind_validation_plan PROPERTIES
  LABELS "databind;validation;plan")
cmake_add_test(test_data_bind_component
  SOURCES test_data_bind_component.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "databind/runtime/tests")
set_tests_properties(test_data_bind_component PROPERTIES
  LABELS "databind;component;reflection")

cmake_add_test(test_data_bind_component_public_cpp
  SOURCES test_data_bind_component_public_cpp.cpp
  LIBS Salts::DataBind
  FOLDER "databind/runtime/tests")
set_target_properties(test_data_bind_component_public_cpp PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
cmake_add_test(test_data_bind_channel
  SOURCES test_data_bind_channel.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "databind/runtime/tests")
set_tests_properties(test_data_bind_channel PROPERTIES
  LABELS "databind;channel;reflection")

cmake_add_test(test_data_bind_channel_public_cpp
  SOURCES test_data_bind_channel_public_cpp.cpp
  LIBS Salts::DataBind
  FOLDER "databind/runtime/tests")
set_target_properties(test_data_bind_channel_public_cpp PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
cmake_add_test(test_data_bind_binding_plan
  SOURCES test_data_bind_binding_plan.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "databind/runtime/tests")
set_tests_properties(test_data_bind_binding_plan PROPERTIES
  LABELS "databind;cmeta;service-contract;binding-plan")

cmake_add_test(test_data_bind_projection_plan
  SOURCES test_data_bind_projection_plan.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "databind/runtime/tests")
set_tests_properties(test_data_bind_projection_plan PROPERTIES
  LABELS "databind;format-plan;transport-plan;method-plan")
cmake_add_test(test_data_bind_cmeta_reflection
  SOURCES test_data_bind_cmeta_reflection.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "databind/runtime/tests")
cmake_add_test(test_data_bind_cmeta_public
  SOURCES test_data_bind_cmeta_public.c
  LIBS Salts::DataBind
  FOLDER "databind/runtime/tests")
cmake_add_test(test_data_bind_cmeta_public_cpp
  SOURCES test_data_bind_cmeta_public_cpp.cpp
  LIBS Salts::DataBind
  FOLDER "databind/runtime/tests")
set_target_properties(test_data_bind_cmeta_public_cpp PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)

cmake_add_test(test_data_bind_cmeta
  SOURCES test_data_bind_cmeta.c
  LIBS Salts::DataBindCMeta Salts::DataBindSchema Salts::TinyTest
  FOLDER "databind/runtime/tests")
cmake_add_test(test_data_bind_cmeta_kind
  SOURCES test_data_bind_cmeta_kind.c
  LIBS Salts::DataBindCMeta Salts::TinyTest
  FOLDER "databind/runtime/tests")

cmake_add_test(test_data_bind_cflow
  SOURCES test_data_bind_cflow.c
  LIBS Salts::DataBindCFlow Salts::TinyTest
  FOLDER "databind/runtime/tests")

cmake_add_test(test_data_bind_adapters_cpp
  SOURCES test_data_bind_adapters_cpp.cpp
  LIBS Salts::DataBindCFlow Salts::TinyTest
  FOLDER "databind/runtime/tests")
cmake_add_test(test_data_bind_record
  SOURCES test_data_bind_record.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "databind/runtime/tests")
cmake_add_test(test_data_bind_pool
  SOURCES test_data_bind_pool.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "databind/runtime/tests")
cmake_add_test(test_tbe_typed
  SOURCES test_tbe_typed.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "databind/runtime/tests")
cmake_add_test(test_tbe_typed_cmeta_mapping
  SOURCES test_tbe_typed_cmeta_mapping.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "databind/runtime/tests")
# Only the first test translation unit owns TinyTest's generated main.
set_property(SOURCE test_tbe_typed_optional_edges.c APPEND
  PROPERTY COMPILE_DEFINITIONS TINYTEST_NO_MAIN)
cmake_add_test(test_tbe_typed_optional_binary
  SOURCES test_tbe_typed_optional_binary.c test_tbe_typed_optional_edges.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "databind/runtime/tests")
cmake_add_test(test_tbe_typed_descriptor_safety
  SOURCES test_tbe_typed_descriptor_safety.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "databind/runtime/tests")
cmake_add_test(test_tbe_typed_descriptor_boundary
  SOURCES test_tbe_typed_descriptor_boundary.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "databind/runtime/tests")
cmake_add_test(test_tbe_typed_nullable_json
  SOURCES test_tbe_typed_nullable_json.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "databind/runtime/tests")
set_tests_properties(test_tbe_typed_nullable_json PROPERTIES
  LABELS "databind;typed;nullable;json;yaml")

cmake_add_test(test_tbe_typed_nullable_binary
  SOURCES test_tbe_typed_nullable_binary.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "databind/runtime/tests")
set_tests_properties(test_tbe_typed_nullable_binary PROPERTIES
  LABELS "databind;typed;nullable;binary;tbe")


# This fixture executes databindc during the build. Do not run a target-
# platform executable on the build host when cross-compiling (e.g. Android).
if(BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
  set(TBE_SAFETY_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/descriptor_safety_generated")
  set(TBE_SAFETY_GENERATED_HEADER "${TBE_SAFETY_GENERATED_DIR}/tbe30_generated.h")
  set(TBE_SAFETY_GENERATED_SOURCE "${TBE_SAFETY_GENERATED_DIR}/tbe30_generated.c")
  add_custom_command(
    OUTPUT "${TBE_SAFETY_GENERATED_HEADER}" "${TBE_SAFETY_GENERATED_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TBE_SAFETY_GENERATED_DIR}"
    COMMAND $<TARGET_FILE:databindc>
            "${DATABIND_SOURCE_ROOT}/compiler/example.schema"
            --lang c --output "${TBE_SAFETY_GENERATED_HEADER}"
            --source-output "${TBE_SAFETY_GENERATED_SOURCE}"
    DEPENDS databindc
            "${DATABIND_SOURCE_ROOT}/compiler/example.schema"
            "${DATABIND_SOURCE_ROOT}/compiler/templates/c_structs.mustache"
            "${DATABIND_SOURCE_ROOT}/compiler/templates/c_typed_source.mustache"
    COMMENT "Generating typed descriptor compatibility fixture"
    VERBATIM)
  cmake_add_test(test_tbe_typed_generated
    SOURCES test_tbe_typed_generated.c
            "${TBE_SAFETY_GENERATED_HEADER}" "${TBE_SAFETY_GENERATED_SOURCE}"
    LIBS ${DATA_BIND_TEST_LIBS}
    INCLUDES "${TBE_SAFETY_GENERATED_DIR}"
    FOLDER "databind/runtime/tests")
  set(TBE_CMETA_GRAPH_DIR "${CMAKE_CURRENT_BINARY_DIR}/cmeta_graph_generated")
  add_custom_command(
    OUTPUT "${TBE_CMETA_GRAPH_DIR}/cmeta_graph_generated.h"
           "${TBE_CMETA_GRAPH_DIR}/cmeta_graph_generated.c"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TBE_CMETA_GRAPH_DIR}"
    COMMAND $<TARGET_FILE:databindc>
            "${CMAKE_CURRENT_SOURCE_DIR}/test_cmeta_graph.schema"
            --lang c --output "${TBE_CMETA_GRAPH_DIR}/cmeta_graph_generated.h"
            --source-output "${TBE_CMETA_GRAPH_DIR}/cmeta_graph_generated.c"
    DEPENDS databindc "${CMAKE_CURRENT_SOURCE_DIR}/test_cmeta_graph.schema"
    VERBATIM)
  add_custom_target(tbe_cmeta_graph_generation
    DEPENDS "${TBE_CMETA_GRAPH_DIR}/cmeta_graph_generated.h"
            "${TBE_CMETA_GRAPH_DIR}/cmeta_graph_generated.c")
  cmake_add_test(test_tbe_typed_cmeta_graph
    SOURCES test_tbe_typed_cmeta_graph.c
            "${TBE_CMETA_GRAPH_DIR}/cmeta_graph_generated.c"
            "${TBE_CMETA_GRAPH_DIR}/cmeta_graph_generated.h"
    LIBS ${DATA_BIND_TEST_LIBS}
    INCLUDES "${TBE_CMETA_GRAPH_DIR}"
    FOLDER "databind/runtime/tests")
  add_dependencies(test_tbe_typed_cmeta_graph tbe_cmeta_graph_generation)

  # Compile the real CLI-generated implementation once as C. Neither public
  # consumer includes it or the private runtime validation header.
  add_library(tbe_cmeta_graph_public_fixture STATIC
    "${TBE_CMETA_GRAPH_DIR}/cmeta_graph_generated.c"
    "${TBE_CMETA_GRAPH_DIR}/cmeta_graph_generated.h")
  set_source_files_properties("${TBE_CMETA_GRAPH_DIR}/cmeta_graph_generated.c"
    PROPERTIES LANGUAGE C)
  target_link_libraries(tbe_cmeta_graph_public_fixture PUBLIC Salts::DataBind)
  target_include_directories(tbe_cmeta_graph_public_fixture PUBLIC "${TBE_CMETA_GRAPH_DIR}")
  add_dependencies(tbe_cmeta_graph_public_fixture tbe_cmeta_graph_generation)
  set_target_properties(tbe_cmeta_graph_public_fixture PROPERTIES FOLDER "databind/runtime/tests")

  cmake_add_test(test_tbe_cmeta_acceptance
    SOURCES test_tbe_cmeta_acceptance.c
    LIBS tbe_cmeta_graph_public_fixture Salts::DataBindSchema Salts::TinyTest
    FOLDER "databind/runtime/tests")
  add_test(NAME test_tbe_cmeta_cli_gates
    COMMAND ${CMAKE_COMMAND}
      "-DTBE_COMPILER=$<TARGET_FILE:databindc>"
      "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cmeta_cli_gates"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/test_cmeta_cli_gates.cmake")

  cmake_add_test(test_tbe_typed_cmeta_public
    SOURCES test_tbe_typed_cmeta_public.c
    LIBS tbe_cmeta_graph_public_fixture Salts::DataBindSchema
    FOLDER "databind/runtime/tests")
  set_tests_properties(test_tbe_typed_cmeta_public PROPERTIES
    LABELS "databind;cmeta;typed-contract")
  cmake_add_test(test_tbe_typed_cmeta_public_cpp
    SOURCES test_tbe_typed_cmeta_public_cpp.cpp
    LIBS tbe_cmeta_graph_public_fixture Salts::DataBindSchema
    FOLDER "databind/runtime/tests")
  set_target_properties(test_tbe_typed_cmeta_public_cpp PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
endif()

endif()

cmake_add_benchmark(benchmark_data_bind_pool
  SOURCES benchmark_data_bind_pool.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "databind/runtime/benchmarks")
cmake_add_benchmark(benchmark_data_bind_json_path
  SOURCES benchmark_data_bind_json_path.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "databind/runtime/benchmarks")
cmake_add_benchmark(benchmark_tbe_typed
  SOURCES benchmark_tbe_typed.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "databind/runtime/benchmarks")
