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
  POSITION_INDEPENDENT_CODE ON
  FOLDER "databind/formats/binary")

# Install the canonical Binary wire headers used by generated code.
install(
  DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
  DESTINATION include
  COMPONENT DataBind
  FILES_MATCHING
  PATTERN "*.h")

# Private Binary layout compilation, shared by idlc and runtime-known schemas.
# Wire facts belong here; the target exposes no native storage/lifecycle engine.
add_library(databind_binary_contract STATIC
  compiler/src/binary_contract_overlay.c
  compiler/src/binary_layout_ir.c
  compiler/src/binary_layout_lowering.c)
target_compile_features(databind_binary_contract PUBLIC c_std_11)
target_include_directories(databind_binary_contract
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/compiler/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal>
  PRIVATE
    ${DATABIND_SOURCE_ROOT}/runtime
    ${DATABIND_SOURCE_ROOT}/schema/include
    ${DATABIND_SOURCE_ROOT}/idl/src
    ${DATABIND_SOURCE_ROOT}/idl/parser)
set_target_properties(databind_binary_contract PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  FOLDER "databind/formats/binary/compiler")
