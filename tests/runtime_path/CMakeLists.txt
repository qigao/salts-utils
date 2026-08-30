add_library(runtime_path_dependency SHARED runtime_dependency.c)
set_target_properties(
  runtime_path_dependency
  PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/dependency-bin"
    WINDOWS_EXPORT_ALL_SYMBOLS ON)
cmake_config_target(runtime_path_dependency FOLDER "cmake/tests")

add_library(runtime_path_extra_dependency SHARED runtime_extra_dependency.c)
set_target_properties(
  runtime_path_extra_dependency
  PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/extra-dependency-bin"
    WINDOWS_EXPORT_ALL_SYMBOLS ON)
cmake_config_target(runtime_path_extra_dependency FOLDER "cmake/tests")

cmake_add_test(
  test_runtime_path
  SOURCES runtime_consumer.c
  LIBS runtime_path_dependency
       $<TARGET_LINKER_FILE:runtime_path_extra_dependency>
  RUNTIME_TARGETS runtime_path_extra_dependency
  FOLDER "cmake/tests")
