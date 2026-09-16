add_library(salts_cflow_process STATIC src/process.c)
cmake_config_target(
  salts_cflow_process
  ALIAS Salts::Process
  FOLDER "cflow-process"
  EXPORT_NAME Process)
target_compile_features(salts_cflow_process PUBLIC c_std_11)
target_include_directories(salts_cflow_process PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(salts_cflow_process
  PUBLIC Salts::Core Salts::CFlow
  PRIVATE Salts::Platform)
install(TARGETS salts_cflow_process EXPORT SaltsUtilsTargets)
install(DIRECTORY include/salts DESTINATION include FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
