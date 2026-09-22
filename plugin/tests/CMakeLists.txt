add_library(salts_plugin_interface_representation_a SHARED
  interface_representation_a.c)
add_library(salts_plugin_interface_representation_b SHARED
  interface_representation_b.c)

foreach(rep_target IN ITEMS
    salts_plugin_interface_representation_a
    salts_plugin_interface_representation_b)
  target_compile_features(${rep_target} PRIVATE c_std_11)
  target_include_directories(${rep_target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${rep_target} PRIVATE Salts::CMeta)
  set_target_properties(${rep_target} PROPERTIES
    FOLDER "plugin/tests"
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
  if(WIN32)
    set_target_properties(${rep_target} PROPERTIES
      WINDOWS_EXPORT_ALL_SYMBOLS ON)
  endif()
endforeach()

cmake_add_test(salts_plugin_contract_test
  SOURCES plugin_contract_test.c
  LIBS
    Salts::Plugin
    Salts::TinyTest
    salts_plugin_interface_representation_a
    salts_plugin_interface_representation_b
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
