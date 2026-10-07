set(DATA_BIND_BINARY_FORMAT_OBJECTS databind_binary_format_objects)

# Internal implementation boundary for the Binary wire/storage format.
# This target is intentionally not exported or installed as a CMake target;
# Salts::DataBind remains the sole public DataBind consumer target.
add_library(${DATA_BIND_BINARY_FORMAT_OBJECTS} OBJECT
  src/data_bind_binary_version.c)
target_compile_features(${DATA_BIND_BINARY_FORMAT_OBJECTS} PRIVATE c_std_11)
target_include_directories(${DATA_BIND_BINARY_FORMAT_OBJECTS}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
set_target_properties(${DATA_BIND_BINARY_FORMAT_OBJECTS} PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
cmake_config_target(${DATA_BIND_BINARY_FORMAT_OBJECTS}
  FOLDER "databind/formats/binary")

# Install the canonical Binary wire headers used by generated code.
install(
  DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
  DESTINATION include
  COMPONENT DataBind
  FILES_MATCHING
  PATTERN "*.h")
