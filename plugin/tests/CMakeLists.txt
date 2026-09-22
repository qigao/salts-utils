cmake_add_test(salts_plugin_contract_test
  SOURCES
    plugin_contract_test.c
    interface_representation_a.c
    interface_representation_b.c
  LIBS Salts::Plugin Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
  FOLDER "plugin/tests")

set_target_properties(salts_plugin_contract_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(salts_plugin_header_cpp_test
  SOURCES plugin_header_cpp_test.cpp
  LIBS Salts::Plugin
  FOLDER "plugin/tests")

set_target_properties(salts_plugin_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
