set(DATA_BIND_TARGET data_bind)
add_library(
  ${DATA_BIND_TARGET} SHARED
  data_bind.c
  data_bind.h
  data_bind_internal.h
  data_bind_record.c
  tbe_typed.c
  tbe_typed.h)
target_compile_definitions(${DATA_BIND_TARGET} PRIVATE DATA_BIND_BUILD_DLL)
target_include_directories(
  ${DATA_BIND_TARGET}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/tbe/schema/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/tbe/schema/parser>)
target_link_libraries(
  ${DATA_BIND_TARGET}
  PRIVATE
    monocypher
    Salts::JsonParser
    Salts::CsvParser
    Salts::XmlParser
    Salts::CYaml
    Salts::CYamlJsonAdapter
    Salts::TbeSchema
  PUBLIC
    Salts::Core
    Salts::CSTL
    Salts::DateTimeParser
    Salts::QueryVM)
cmake_config_target(
  ${DATA_BIND_TARGET}
  ALIAS Salts::DataBind
  FOLDER "tbe/data_bind"
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
  FOLDER tbe/data_bind
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
  FOLDER tbe/data_bind
  EXPORT_NAME DataBindCFlow)

install(
  TARGETS ${DATA_BIND_TARGET} data_bind_cmeta data_bind_cflow
  EXPORT SaltsUtilsTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(
  FILES
    data_bind.h
    data_bind_cflow.h
    data_bind_cmeta.h
    tbe_typed.h
  DESTINATION include)

set(DATA_BIND_TEST_LIBS Salts::DataBind Salts::TinyTest)
cmake_add_test(test_data_bind
  SOURCES test_data_bind.c
  LIBS ${DATA_BIND_TEST_LIBS}
  INCLUDES ${CMAKE_SOURCE_DIR}/tbe/schema/include
  FOLDER "tbe/data_bind/tests")
cmake_add_test(test_data_bind_public_api
  SOURCES test_data_bind_public_api.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "tbe/data_bind/tests")
cmake_add_test(test_data_bind_schema_reflection_contract
  SOURCES test_data_bind_schema_reflection_contract.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "tbe/data_bind/tests")
cmake_add_test(test_data_bind_cmeta_reflection
  SOURCES test_data_bind_cmeta_reflection.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "tbe/data_bind/tests")
cmake_add_test(test_data_bind_cmeta_public
  SOURCES test_data_bind_cmeta_public.c
  LIBS Salts::DataBind
  FOLDER "tbe/data_bind/tests")
cmake_add_test(test_data_bind_cmeta_public_cpp
  SOURCES test_data_bind_cmeta_public_cpp.cpp
  LIBS Salts::DataBind
  FOLDER "tbe/data_bind/tests")
set_target_properties(test_data_bind_cmeta_public_cpp PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)

cmake_add_test(test_data_bind_cmeta
  SOURCES test_data_bind_cmeta.c
  LIBS Salts::DataBindCMeta Salts::TinyTest
  FOLDER "tbe/data_bind/tests")
cmake_add_test(test_data_bind_cmeta_kind
  SOURCES test_data_bind_cmeta_kind.c
  LIBS Salts::DataBindCMeta Salts::TinyTest
  FOLDER "tbe/data_bind/tests")

cmake_add_test(test_data_bind_cflow
  SOURCES test_data_bind_cflow.c
  LIBS Salts::DataBindCFlow Salts::TinyTest
  FOLDER "tbe/data_bind/tests")

cmake_add_test(test_data_bind_adapters_cpp
  SOURCES test_data_bind_adapters_cpp.cpp
  LIBS Salts::DataBindCFlow Salts::TinyTest
  FOLDER "tbe/data_bind/tests")
cmake_add_test(test_data_bind_record
  SOURCES test_data_bind_record.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "tbe/data_bind/tests")
cmake_add_test(test_data_bind_pool
  SOURCES test_data_bind_pool.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "tbe/data_bind/tests")
cmake_add_test(test_tbe_typed
  SOURCES test_tbe_typed.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "tbe/data_bind/tests")
cmake_add_test(test_tbe_typed_cmeta_mapping
  SOURCES test_tbe_typed_cmeta_mapping.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "tbe/data_bind/tests")
# Only the first test translation unit owns TinyTest's generated main.
set_property(SOURCE test_tbe_typed_optional_edges.c APPEND
  PROPERTY COMPILE_DEFINITIONS TINYTEST_NO_MAIN)
cmake_add_test(test_tbe_typed_optional_binary
  SOURCES test_tbe_typed_optional_binary.c test_tbe_typed_optional_edges.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "tbe/data_bind/tests")
cmake_add_test(test_tbe_typed_descriptor_safety
  SOURCES test_tbe_typed_descriptor_safety.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "tbe/data_bind/tests")
cmake_add_test(test_tbe_typed_descriptor_boundary
  SOURCES test_tbe_typed_descriptor_boundary.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "tbe/data_bind/tests")

# This fixture executes tbe_compiler during the build. Do not run a target-
# platform executable on the build host when cross-compiling (e.g. Android).
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
  set(TBE_SAFETY_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/descriptor_safety_generated")
  set(TBE_SAFETY_GENERATED_HEADER "${TBE_SAFETY_GENERATED_DIR}/tbe30_generated.h")
  set(TBE_SAFETY_GENERATED_SOURCE "${TBE_SAFETY_GENERATED_DIR}/tbe30_generated.c")
  add_custom_command(
    OUTPUT "${TBE_SAFETY_GENERATED_HEADER}" "${TBE_SAFETY_GENERATED_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TBE_SAFETY_GENERATED_DIR}"
    COMMAND $<TARGET_FILE:tbe_compiler>
            "${CMAKE_SOURCE_DIR}/tbe/tbe_compiler/example.schema"
            --lang c --output "${TBE_SAFETY_GENERATED_HEADER}"
            --source-output "${TBE_SAFETY_GENERATED_SOURCE}"
    DEPENDS tbe_compiler
            "${CMAKE_SOURCE_DIR}/tbe/tbe_compiler/example.schema"
            "${CMAKE_SOURCE_DIR}/tbe/tbe_compiler/templates/c_structs.mustache"
            "${CMAKE_SOURCE_DIR}/tbe/tbe_compiler/templates/c_typed_source.mustache"
    COMMENT "Generating typed descriptor compatibility fixture"
    VERBATIM)
  cmake_add_test(test_tbe_typed_generated
    SOURCES test_tbe_typed_generated.c
            "${TBE_SAFETY_GENERATED_HEADER}" "${TBE_SAFETY_GENERATED_SOURCE}"
    LIBS ${DATA_BIND_TEST_LIBS}
    INCLUDES "${TBE_SAFETY_GENERATED_DIR}"
    FOLDER "tbe/data_bind/tests")
  set(TBE_CMETA_GRAPH_DIR "${CMAKE_CURRENT_BINARY_DIR}/cmeta_graph_generated")
  add_custom_command(
    OUTPUT "${TBE_CMETA_GRAPH_DIR}/cmeta_graph_generated.h"
           "${TBE_CMETA_GRAPH_DIR}/cmeta_graph_generated.c"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TBE_CMETA_GRAPH_DIR}"
    COMMAND $<TARGET_FILE:tbe_compiler>
            "${CMAKE_CURRENT_SOURCE_DIR}/test_cmeta_graph.schema"
            --lang c --output "${TBE_CMETA_GRAPH_DIR}/cmeta_graph_generated.h"
            --source-output "${TBE_CMETA_GRAPH_DIR}/cmeta_graph_generated.c"
    DEPENDS tbe_compiler "${CMAKE_CURRENT_SOURCE_DIR}/test_cmeta_graph.schema"
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
    FOLDER "tbe/data_bind/tests")
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
  set_target_properties(tbe_cmeta_graph_public_fixture PROPERTIES FOLDER "tbe/data_bind/tests")

  cmake_add_test(test_tbe_cmeta_acceptance
    SOURCES test_tbe_cmeta_acceptance.c
    LIBS tbe_cmeta_graph_public_fixture Salts::TbeSchema Salts::TinyTest
    FOLDER "tbe/data_bind/tests")
  add_test(NAME test_tbe_cmeta_cli_gates
    COMMAND ${CMAKE_COMMAND}
      "-DTBE_COMPILER=$<TARGET_FILE:tbe_compiler>"
      "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cmeta_cli_gates"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/test_cmeta_cli_gates.cmake")

  cmake_add_test(test_tbe_typed_cmeta_public
    SOURCES test_tbe_typed_cmeta_public.c
    LIBS tbe_cmeta_graph_public_fixture Salts::TbeSchema
    FOLDER "tbe/data_bind/tests")
  set_tests_properties(test_tbe_typed_cmeta_public PROPERTIES
    LABELS "databind;cmeta;typed-contract")
  cmake_add_test(test_tbe_typed_cmeta_public_cpp
    SOURCES test_tbe_typed_cmeta_public_cpp.cpp
    LIBS tbe_cmeta_graph_public_fixture Salts::TbeSchema
    FOLDER "tbe/data_bind/tests")
  set_target_properties(test_tbe_typed_cmeta_public_cpp PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
endif()

cmake_add_benchmark(benchmark_data_bind_pool
  SOURCES benchmark_data_bind_pool.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "tbe/data_bind/benchmarks")
cmake_add_benchmark(benchmark_data_bind_json_path
  SOURCES benchmark_data_bind_json_path.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "tbe/data_bind/benchmarks")
cmake_add_benchmark(benchmark_tbe_typed
  SOURCES benchmark_tbe_typed.c
  LIBS ${DATA_BIND_TEST_LIBS}
  FOLDER "tbe/data_bind/benchmarks")
