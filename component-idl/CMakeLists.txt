# Compiler-only, opt-in prototype. No install/export, no Plugin runtime.
add_library(salts_component_idl_candidate STATIC src/component_idl_codegen.c)
add_library(Salts::ComponentIdlCandidate ALIAS salts_component_idl_candidate)
target_compile_features(salts_component_idl_candidate PUBLIC c_std_11)
target_include_directories(salts_component_idl_candidate PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(salts_component_idl_candidate PUBLIC Salts::IDL)
set_target_properties(salts_component_idl_candidate PROPERTIES
  FOLDER "component-idl")
if(BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
  add_subdirectory(tests)
endif()
