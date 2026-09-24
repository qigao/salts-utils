add_library(salts_plugin_cflow_abi INTERFACE)
cmake_config_target(
  salts_plugin_cflow_abi
  ALIAS Salts::PluginCFlowABI
  FOLDER "plugin-cflow"
  EXPORT_NAME PluginCFlowABI)
target_compile_features(salts_plugin_cflow_abi INTERFACE c_std_11)
target_include_directories(salts_plugin_cflow_abi INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(salts_plugin_cflow_abi INTERFACE
  Salts::PluginABI
  Salts::CFlow)

add_library(salts_plugin_cflow STATIC
  src/plugin_cflow.c)
cmake_config_target(
  salts_plugin_cflow
  ALIAS Salts::PluginCFlow
  FOLDER "plugin-cflow"
  EXPORT_NAME PluginCFlow)
target_compile_features(salts_plugin_cflow PUBLIC c_std_11)
target_link_libraries(salts_plugin_cflow
  PUBLIC
    Salts::Plugin
    salts_plugin_cflow_abi)

install(
  TARGETS salts_plugin_cflow_abi salts_plugin_cflow
  EXPORT SaltsUtilsTargets)
install(
  DIRECTORY include/salts
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
