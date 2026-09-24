set(DATA_BIND_TBE_FORMAT_OBJECTS databind_tbe_format_objects)

# Internal implementation boundary for the TBE wire/storage format.
# This target is intentionally not exported or installed as a CMake target;
# Salts::DataBind remains the sole public DataBind consumer target.
add_library(${DATA_BIND_TBE_FORMAT_OBJECTS} OBJECT
  src/tbe_version.c)
target_compile_features(${DATA_BIND_TBE_FORMAT_OBJECTS} PRIVATE c_std_11)
target_include_directories(${DATA_BIND_TBE_FORMAT_OBJECTS}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
set_target_properties(${DATA_BIND_TBE_FORMAT_OBJECTS} PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  FOLDER "databind/formats/tbe")

# Preserve the installed TBE wire header spelling used by generated code.
install(
  DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
  DESTINATION include
  COMPONENT DataBind
  FILES_MATCHING
  PATTERN "*.h")
