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

install(
  TARGETS ${DATA_BIND_TARGET}
  EXPORT SaltsUtilsTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(
  FILES
    data_bind.h
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
