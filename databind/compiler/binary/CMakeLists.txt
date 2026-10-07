# Private Binary layout compilation, shared by idlc and runtime-known schemas.
# Wire facts belong here; the target exposes no native storage/lifecycle engine.
add_library(databind_binary_contract STATIC
  src/binary_contract_overlay.c
  src/binary_layout_ir.c
  src/binary_layout_lowering.c)
target_compile_features(databind_binary_contract PUBLIC c_std_11)
target_include_directories(databind_binary_contract
  PUBLIC
    $<BUILD_INTERFACE:${DATABIND_SOURCE_ROOT}/formats/binary/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${DATABIND_SOURCE_ROOT}/formats/binary/internal>
  PRIVATE
    ${DATABIND_SOURCE_ROOT}/runtime
    ${DATABIND_SOURCE_ROOT}/schema/include
    ${DATABIND_SOURCE_ROOT}/idl/contract
    ${DATABIND_SOURCE_ROOT}/idl/parser)
set_target_properties(databind_binary_contract PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
cmake_config_target(databind_binary_contract
  FOLDER "databind/compiler")

target_link_libraries(databind_binary_contract
  PUBLIC Salts::IDL Salts::CSerde
  PRIVATE Salts::Schema Salts::Core)
