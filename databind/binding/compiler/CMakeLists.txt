set(DATABIND_BINDING_COMPILER_TARGET databind_binding_compiler)

# Compiler-private DataBind lowering. This target consumes typed IDL data
# semantics and produces DataBind native-binding metadata only. It does not own
# Service/Channel/Component identity, transport projection, artifacts, or wire
# format semantics.
add_library(${DATABIND_BINDING_COMPILER_TARGET} STATIC
  message_native.c)

target_compile_features(${DATABIND_BINDING_COMPILER_TARGET} PUBLIC c_std_11)
target_include_directories(${DATABIND_BINDING_COMPILER_TARGET}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  PRIVATE
    ${DATABIND_SOURCE_ROOT}/idl/src)
target_link_libraries(${DATABIND_BINDING_COMPILER_TARGET}
  PUBLIC Salts::IDL)
set_target_properties(${DATABIND_BINDING_COMPILER_TARGET} PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  FOLDER "databind/binding/compiler")
