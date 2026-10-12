cmake_minimum_required(VERSION 3.27)
project(ComponentIdlSdkIsolation LANGUAGES C CXX)
enable_testing()

# This is an independent installed-SDK consumer, NOT a build-tree alias.
if(NOT DEFINED ENV{SALTS_UTILS_ROOT} OR
   NOT IS_DIRECTORY "$ENV{SALTS_UTILS_ROOT}")
  message(FATAL_ERROR "Exact installed SALTS_UTILS_ROOT is required")
endif()
file(TO_CMAKE_PATH "$ENV{SALTS_UTILS_ROOT}" _sdk)
unset(SaltsUtils_DIR CACHE)
unset(SaltsUtils_DIR)
find_package(SaltsUtils CONFIG REQUIRED PATHS "${_sdk}"
             NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

foreach(_old_target IN ITEMS
    Salts::DataBind Salts::IDL Salts::CSTL
    Salts::PluginABI Salts::Plugin)
  if(NOT TARGET ${_old_target})
    message(FATAL_ERROR "Existing native SDK target missing: ${_old_target}")
  endif()
endforeach()

# Opt-in source compilation does not authorize a public Component-IDL SDK
# surface. No generated candidate target/header/library may be installed.
foreach(_private_target IN ITEMS
    Salts::ComponentIdlCandidate
    salts_component_idl_candidate
    component_idl_candidate_provider_dso
    component_idl_candidate_negative_dso_host)
  if(TARGET ${_private_target})
    message(FATAL_ERROR "Unqualified candidate target leaked: ${_private_target}")
  endif()
endforeach()
file(GLOB_RECURSE _sdk_files LIST_DIRECTORIES FALSE
     RELATIVE "${_sdk}" "${_sdk}/*")
foreach(_entry IN LISTS _sdk_files)
  if(_entry MATCHES "(^|/)(component[-_]idl|calc_component|libsalts_component_idl)")
    message(FATAL_ERROR "Unqualified candidate artifact installed: ${_entry}")
  endif()
endforeach()

get_filename_component(_source_root
  "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(_public_dir
  "${_source_root}/databind/compiler/package_config/databind_public")
set(_target_dir
  "${_source_root}/databind/compiler/package_config/databind_target")

# Compile the UNCHANGED existing external C11 DataBind consumer.
add_executable(sdk_legacy_databind "${_public_dir}/main.c")
target_compile_features(sdk_legacy_databind PRIVATE c_std_11)
target_link_libraries(sdk_legacy_databind PRIVATE Salts::DataBind)
add_test(NAME sdk_legacy_databind COMMAND sdk_legacy_databind)

# Compile and execute the ORIGINAL installed Contract-only TYPES client.
# Never substitute the private Component-IDL backend for the public generator.
salts_idl_target(
  TARGET sdk_legacy_types
  IDL "${_target_dir}/contract_types.schema"
  ARTIFACT_NAME contract_types
  ARTIFACTS TYPES)
add_executable(sdk_legacy_types_client
  "${_target_dir}/contract_types_consumer.c")
target_compile_features(sdk_legacy_types_client PRIVATE c_std_11)
target_link_libraries(sdk_legacy_types_client PRIVATE
  sdk_legacy_types_types)
add_dependencies(sdk_legacy_types_client sdk_legacy_types_idl_codegen)
add_test(NAME sdk_legacy_types_client COMMAND sdk_legacy_types_client)

# The existing public PLUGIN artifact is a typed FUNCTION export and must
# remain independently creatable/usable; it must NOT become a Provider
# INTERFACE export because the new private code generator is enabled.
salts_idl_target(
  TARGET sdk_legacy_function_plugin
  IDL "${_target_dir}/native_contract_service.schema"
  COMPONENT "WasmRuntime.Calculator"
  VERSION "1.0.0"
  ARTIFACT_NAME "contract_plugin"
  ARTIFACTS PLUGIN
  SOURCES "${_target_dir}/contract_plugin_business.c")
add_executable(sdk_legacy_function_lease
  "${_target_dir}/contract_plugin_lease_consumer.c")
target_compile_features(sdk_legacy_function_lease PRIVATE c_std_11)
target_compile_definitions(sdk_legacy_function_lease PRIVATE
  "GENERATED_CONTRACT_PLUGIN_PATH=\"$<TARGET_FILE:sdk_legacy_function_plugin_plugin>\"")
target_link_libraries(sdk_legacy_function_lease PRIVATE
  sdk_legacy_function_plugin_plugin_client Salts::Plugin)
add_dependencies(sdk_legacy_function_lease
  sdk_legacy_function_plugin_plugin
  sdk_legacy_function_plugin_plugin_client)
add_test(NAME sdk_legacy_function_lease COMMAND sdk_legacy_function_lease)

# The installed C++17 surface remains native and separate from the opt-in
# Component-IDL-generated opaque C++ declarations.
add_executable(sdk_legacy_cpp legacy_cpp.cpp)
target_link_libraries(sdk_legacy_cpp PRIVATE Salts::DataBind)
target_compile_features(sdk_legacy_cpp PRIVATE cxx_std_17)
add_test(NAME sdk_legacy_cpp COMMAND sdk_legacy_cpp)
