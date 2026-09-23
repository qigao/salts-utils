add_library(salts_plugin_cflow STATIC
  src/plugin_cflow.c)

cmake_config_target(
  salts_plugin_cflow
  ALIAS Salts::PluginCFlow
  FOLDER "plugin/cflow"
  EXPORT_NAME PluginCFlow)

target_compile_features(salts_plugin_cflow PUBLIC c_std_11)
target_include_directories(salts_plugin_cflow PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

target_link_libraries(salts_plugin_cflow
  PUBLIC Salts::Plugin Salts::CFlow)

install(TARGETS salts_plugin_cflow EXPORT SaltsUtilsTargets)
install(
  DIRECTORY include/salts
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
