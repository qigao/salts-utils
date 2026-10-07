add_library(salts_bindings STATIC module.c)
target_include_directories(salts_bindings PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(salts_bindings PUBLIC Salts::CMeta)
cmake_config_target(salts_bindings
  ALIAS Salts::Bindings EXPORT_NAME Bindings FOLDER "bindings")
install(TARGETS salts_bindings EXPORT SaltsUtilsTargets)
install(DIRECTORY include/ DESTINATION include)
