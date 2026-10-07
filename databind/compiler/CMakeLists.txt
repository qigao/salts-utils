# =============================================================================
# Executable
# =============================================================================

# IDL fixture targets inherit this folder when the public generator creates them.
set(CMAKE_FOLDER "databind/compiler")

add_subdirectory(binary)
add_subdirectory(binding)
add_subdirectory(cmeta)

set(DATABIND_COMPILER_TOOLING_TARGET databind_compiler_tooling)
add_library(${DATABIND_COMPILER_TOOLING_TARGET} INTERFACE)
target_link_libraries(${DATABIND_COMPILER_TOOLING_TARGET}
  INTERFACE
    Salts::CmdParser
    Salts::Mustache
    Salts::JsonParser
    Salts::Schema
    databind_binary_contract)
cmake_config_target(${DATABIND_COMPILER_TOOLING_TARGET}
  FOLDER "databind/compiler/support")

cmake_add_executable(salts_idlc
  SOURCES main.c compiler_core.c projection.c projection_frontend.c projection_config.c service_native.c native_service_projection.c plugin_projection.c wasm_projection.c openapi_projection.c method_plan_projection.c socket_plan_projection.c flowmq_plan_projection.c binary_reader_codegen.c opaque_plan_codegen.c database_schema.c mustache_helpers.c
  LIBS databind_binding_compiler Salts::IDL Salts::PluginABI ${DATABIND_COMPILER_TOOLING_TARGET} Salts::Core
  FOLDER "databind/compiler/tools")
target_include_directories(salts_idlc
  PRIVATE ${DATABIND_SOURCE_ROOT}/idl/contract)
set_target_properties(salts_idlc PROPERTIES
  OUTPUT_NAME "salts-idlc")

add_subdirectory(templates)
get_target_property(TBE_COMPILER_TEMPLATE_FILES salts_idlc SOURCES)
list(FILTER TBE_COMPILER_TEMPLATE_FILES INCLUDE REGEX "\\.mustache$")
set_property(TARGET salts_idlc APPEND PROPERTY LINK_DEPENDS
             ${TBE_COMPILER_TEMPLATE_FILES})

# =============================================================================
# Copy templates next to executable for easy access
# =============================================================================

foreach(template_file IN LISTS TBE_COMPILER_TEMPLATE_FILES)
  get_filename_component(template_dir "${template_file}" DIRECTORY)
  file(RELATIVE_PATH template_relative_dir "${CMAKE_CURRENT_SOURCE_DIR}"
       "${template_dir}")
  add_custom_command(TARGET salts_idlc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:salts_idlc>/${template_relative_dir}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${template_file}"
      "$<TARGET_FILE_DIR:salts_idlc>/${template_relative_dir}"
    VERBATIM)
endforeach()

install(
  TARGETS salts_idlc
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT DataBind)

# =============================================================================
# Tests
# =============================================================================

if(BUILD_TESTS)
  add_subdirectory(tests)
  add_subdirectory(package_config)
endif()
