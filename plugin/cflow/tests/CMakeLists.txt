get_target_property(
  salts_plugin_base_links salts_plugin INTERFACE_LINK_LIBRARIES)
if("${salts_plugin_base_links}" MATCHES "Salts::CFlow")
  message(FATAL_ERROR
    "Salts::Plugin must remain independent of the PluginCFlow adapter")
endif()

add_library(salts_plugin_cflow_fixture SHARED
  fixture_plugin_cflow.c)
target_compile_features(salts_plugin_cflow_fixture PRIVATE c_std_11)
target_include_directories(salts_plugin_cflow_fixture PRIVATE
  ${PROJECT_SOURCE_DIR}/plugin/include)
target_link_libraries(salts_plugin_cflow_fixture PRIVATE Salts::CFlow)
set_target_properties(salts_plugin_cflow_fixture PROPERTIES
  FOLDER "plugin/cflow/tests/fixtures"
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(salts_plugin_cflow_test
  SOURCES plugin_cflow_test.c
  LIBS Salts::PluginCFlow Salts::TinyTest
  DEFS
    "PLUGIN_CFLOW_FIXTURE_PATH=\"$<TARGET_FILE:salts_plugin_cflow_fixture>\""
  RUNTIME_TARGETS salts_plugin_cflow_fixture
  FOLDER "plugin/cflow/tests")

add_dependencies(salts_plugin_cflow_test salts_plugin_cflow_fixture)
set_target_properties(salts_plugin_cflow_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(salts_plugin_cflow_header_cpp_test
  SOURCES plugin_cflow_header_cpp_test.cpp
  LIBS Salts::PluginCFlow
  FOLDER "plugin/cflow/tests")

set_target_properties(salts_plugin_cflow_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
