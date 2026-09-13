cmake_minimum_required(VERSION 3.20)
project(DataBindNativeInstalled C CXX)
find_package(SaltsUtils CONFIG REQUIRED)
get_target_property(imported Salts::DataBind IMPORTED)
if(NOT imported)
  message(FATAL_ERROR "Test requires the installed DataBind target")
endif()
add_executable(databind_native_consumer consumer.c)
target_compile_features(databind_native_consumer PRIVATE c_std_11)
target_link_libraries(databind_native_consumer PRIVATE Salts::DataBind)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
  target_compile_options(databind_native_consumer PRIVATE -fsanitize=address,undefined)
  target_link_options(databind_native_consumer PRIVATE -fsanitize=address,undefined)
endif()
enable_testing()
add_test(NAME databind_native_installed COMMAND databind_native_consumer)

# Generate with the installed compiler, then compile its implementation as C
# in a static archive. Both independent consumers use installed public headers.
get_target_property(schema_imported Salts::TbeSchema IMPORTED)
if(NOT schema_imported)
  message(FATAL_ERROR "Test requires the installed Schema target")
endif()
find_program(TBE_CMETA_COMPILER NAMES tbe_compiler
  HINTS "${SaltsUtils_DIR}/../../../bin" REQUIRED)
set(graph_dir "${CMAKE_CURRENT_BINARY_DIR}/cmeta_graph_generated")
set(graph_tests "${CMAKE_CURRENT_SOURCE_DIR}/../../../tbe/data_bind")
add_custom_command(
  OUTPUT "${graph_dir}/cmeta_graph_generated.c" "${graph_dir}/cmeta_graph_generated.h"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${graph_dir}"
  COMMAND "${TBE_CMETA_COMPILER}" "${graph_tests}/test_cmeta_graph.schema"
          --lang c --output "${graph_dir}/cmeta_graph_generated.h"
          --source-output "${graph_dir}/cmeta_graph_generated.c"
  DEPENDS "${graph_tests}/test_cmeta_graph.schema" "${TBE_CMETA_COMPILER}"
  VERBATIM)
add_library(cmeta_installed_graph STATIC "${graph_dir}/cmeta_graph_generated.c")
target_compile_features(cmeta_installed_graph PRIVATE c_std_11)
target_link_libraries(cmeta_installed_graph PUBLIC Salts::DataBind)
target_include_directories(cmeta_installed_graph PUBLIC "${graph_dir}")
foreach(language c cpp)
  if(language STREQUAL "cpp")
    set(consumer "${graph_tests}/test_tbe_typed_cmeta_public_cpp.cpp")
  else()
    set(consumer "${graph_tests}/test_tbe_typed_cmeta_public.c")
  endif()
  add_executable(cmeta_installed_${language} "${consumer}")
  target_link_libraries(cmeta_installed_${language} PRIVATE cmeta_installed_graph Salts::TbeSchema)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
    target_compile_options(cmeta_installed_${language} PRIVATE -fsanitize=address,undefined)
    target_link_options(cmeta_installed_${language} PRIVATE -fsanitize=address,undefined)
  endif()
  add_test(NAME cmeta_installed_${language} COMMAND cmeta_installed_${language})
endforeach()
set_target_properties(cmeta_installed_cpp PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
