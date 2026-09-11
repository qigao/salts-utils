set(DATA_BIND_TARGET data_bind)
set(DATA_BIND_PARSER_COMPAT_TARGET data_bind_parser_compat)

add_library(
  ${DATA_BIND_PARSER_COMPAT_TARGET} STATIC
  parser_compat/src/parser_compat.c)
target_include_directories(
  ${DATA_BIND_PARSER_COMPAT_TARGET}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/parser_compat/include)
target_link_libraries(
  ${DATA_BIND_PARSER_COMPAT_TARGET}
  PRIVATE
    Salts::Core
    Salts::CmdParser
    Salts::CsvParser
    Salts::CYaml
    Salts::CYamlJsonAdapter
    Salts::DateTimeParser
    Salts::DotEnvParser
    Salts::IniParser
    Salts::JsonParser
    Salts::LtvParser
    Salts::ModbusParser
    Salts::QueryVM
    Salts::SoaParser
    Salts::TLVParser
    Salts::TomlParser
    Salts::Toon
    Salts::UriParser
    Salts::XmlParser)
set_target_properties(
  ${DATA_BIND_PARSER_COMPAT_TARGET}
  PROPERTIES FOLDER "tbe/data_bind/internal")

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
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/parser_compat/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/tbe/schema/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/tbe/schema/parser>)
target_link_libraries(
  ${DATA_BIND_TARGET}
  PRIVATE
    monocypher
    ${DATA_BIND_PARSER_COMPAT_TARGET}
    Salts::TbeSchema
  PUBLIC
    Salts::Core
    Salts::CSTL)
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
    parser_compat/include/turbo_parser_common.h
    parser_compat/include/turbo_parser_datetime.h
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

cmake_add_test(test_data_bind_cmeta
  SOURCES test_data_bind_cmeta.c
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
