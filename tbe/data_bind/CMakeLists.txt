# =============================================================================
# Library
# =============================================================================

set(TARGET_NAME data_bind)

add_library(${TARGET_NAME} SHARED data_bind.c data_bind.h data_bind_internal.h data_bind_record.c
                                  tbe_typed.c tbe_typed.h)



target_compile_definitions(${TARGET_NAME} PRIVATE DATA_BIND_BUILD_DLL)

target_include_directories(${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
         $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/tbe/schema/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/tbe/schema/parser>
)

target_link_libraries(${TARGET_NAME}
  PRIVATE
    monocypher
    TurboParser::TbeSchema
  PUBLIC
    Rocida::Core
    Rocida::STL
    TurboParser::Parser
)

cmake_config_target(
  ${TARGET_NAME}
  ALIAS TurboParser::DataBind
  FOLDER tbe/data_bind
  EXPORT_NAME DataBind)

add_library(data_bind_cmeta SHARED data_bind_cmeta.c data_bind_cmeta.h)
target_compile_definitions(data_bind_cmeta PRIVATE DATA_BIND_CMETA_BUILD_DLL)
target_include_directories(data_bind_cmeta
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
         $<INSTALL_INTERFACE:include>)
target_link_libraries(data_bind_cmeta
  PUBLIC TurboParser::DataBind Rocida::CMeta)
cmake_config_target(
  data_bind_cmeta
  ALIAS TurboParser::DataBindCMeta
  FOLDER tbe/data_bind
  EXPORT_NAME DataBindCMeta)

add_library(data_bind_cflow SHARED data_bind_cflow.c data_bind_cflow.h)
target_compile_definitions(data_bind_cflow PRIVATE DATA_BIND_CFLOW_BUILD_DLL)
target_include_directories(data_bind_cflow
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
         $<INSTALL_INTERFACE:include>)
target_link_libraries(data_bind_cflow
  PUBLIC TurboParser::DataBindCMeta Rocida::CFlow)
cmake_config_target(
  data_bind_cflow
  ALIAS TurboParser::DataBindCFlow
  FOLDER tbe/data_bind
  EXPORT_NAME DataBindCFlow)

install(
  TARGETS ${TARGET_NAME} data_bind_cmeta data_bind_cflow
  EXPORT TurboParserTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES data_bind.h data_bind_cflow.h data_bind_cmeta.h tbe_typed.h DESTINATION include)

# =============================================================================
# Tests
# =============================================================================

cmake_add_test(test_data_bind
  SOURCES test_data_bind.c
  LIBS TurboParser::DataBind Rocida::TinyTest
  INCLUDES ${CMAKE_SOURCE_DIR}/tbe/schema/include
  FOLDER "tbe/data_bind")

cmake_add_test(test_data_bind_public_api
  SOURCES test_data_bind_public_api.c
  LIBS TurboParser::DataBind Rocida::TinyTest
  FOLDER "tbe/data_bind")

cmake_add_test(test_data_bind_cmeta
  SOURCES test_data_bind_cmeta.c
  LIBS TurboParser::DataBindCMeta Rocida::TinyTest
  FOLDER "tbe/data_bind")

cmake_add_test(test_data_bind_cflow
  SOURCES test_data_bind_cflow.c
  LIBS TurboParser::DataBindCFlow Rocida::TinyTest
  FOLDER "tbe/data_bind")

cmake_add_test(test_data_bind_adapters_cpp
  SOURCES test_data_bind_adapters_cpp.cpp
  LIBS TurboParser::DataBindCFlow Rocida::TinyTest
  FOLDER "tbe/data_bind")

cmake_add_test(test_data_bind_record
  SOURCES test_data_bind_record.c
  LIBS TurboParser::DataBind Rocida::TinyTest
  FOLDER "tbe/data_bind")

cmake_add_test(test_data_bind_pool
  SOURCES test_data_bind_pool.c
  LIBS TurboParser::DataBind Rocida::TinyTest
  FOLDER "tbe/data_bind")

cmake_add_test(test_tbe_typed
  SOURCES test_tbe_typed.c
  LIBS TurboParser::DataBind Rocida::TinyTest
  FOLDER "tbe/data_bind")

cmake_add_benchmark(benchmark_data_bind_pool
  SOURCES benchmark_data_bind_pool.c
  LIBS TurboParser::DataBind Rocida::TinyTest
  FOLDER "tbe/data_bind")

cmake_add_benchmark(benchmark_data_bind_json_path
  SOURCES benchmark_data_bind_json_path.c
  LIBS TurboParser::DataBind Rocida::TinyTest
  FOLDER "tbe/data_bind")

cmake_add_benchmark(benchmark_tbe_typed
  SOURCES benchmark_tbe_typed.c
  LIBS TurboParser::DataBind Rocida::TinyTest
  FOLDER "tbe/data_bind")
