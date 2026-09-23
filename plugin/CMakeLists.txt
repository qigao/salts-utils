add_library(salts_plugin STATIC
  src/plugin.c
  src/plugin_registry.c)

if(WIN32)
  target_sources(salts_plugin PRIVATE src/plugin_windows.c)
else()
  target_sources(salts_plugin PRIVATE src/plugin_posix.c)
endif()

cmake_config_target(
  salts_plugin
  ALIAS Salts::Plugin
  FOLDER "plugin"
  EXPORT_NAME Plugin)

target_compile_features(salts_plugin PUBLIC c_std_11)
target_include_directories(salts_plugin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(salts_plugin
  PUBLIC Salts::CMeta
  PRIVATE Salts::Core Salts::Platform ${CMAKE_DL_LIBS})

install(TARGETS salts_plugin EXPORT SaltsUtilsTargets)
install(
  DIRECTORY include/salts
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

add_subdirectory(cflow)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
