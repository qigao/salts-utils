cmake_minimum_required(VERSION 3.20)

project(tbe_cbind_install_consumer LANGUAGES C)

find_package(TurboParser CONFIG REQUIRED
  PATHS "${TurboParser_DIR}"
  NO_DEFAULT_PATH)

if(TBE_CBIND_TEST_INJECT_CONDITIONAL_DATABIND)
  add_library(DataBindProbe STATIC data_bind_probe.c)
  add_library(WrapperTarget INTERFACE)
  target_link_libraries(WrapperTarget INTERFACE DataBindProbe)
  set_property(TARGET TurboParser::TbeCBind APPEND PROPERTY
    INTERFACE_LINK_LIBRARIES
    "$<$<CONFIG:Release>:WrapperTarget>")
endif()

add_executable(tbe_cbind_install_consumer main.c)
target_link_libraries(tbe_cbind_install_consumer PRIVATE
  TurboParser::TbeCBind
  TurboParser::Parser)
set_target_properties(tbe_cbind_install_consumer PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON)
if(TBE_CBIND_TEST_DATABIND_PATH_ONLY)
  set(databind_path_only_output
    "${CMAKE_BINARY_DIR}/DataBindCheckoutBuildOutput")
  set_target_properties(tbe_cbind_install_consumer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${databind_path_only_output}")
  string(TOUPPER "${CMAKE_BUILD_TYPE}" databind_path_only_config)
  if(NOT "${databind_path_only_config}" STREQUAL "")
    set_target_properties(tbe_cbind_install_consumer PROPERTIES
      "RUNTIME_OUTPUT_DIRECTORY_${databind_path_only_config}"
      "${databind_path_only_output}")
  endif()
endif()
file(GENERATE
  OUTPUT "${CMAKE_BINARY_DIR}/tbe-cbind-consumer-location-$<CONFIG>.txt"
  CONTENT "$<TARGET_FILE:tbe_cbind_install_consumer>")

set(tbe_cbind_evidence_dir
  "${CMAKE_BINARY_DIR}/tbe-cbind-dependency-evidence")
file(MAKE_DIRECTORY "${tbe_cbind_evidence_dir}")
get_property(tbe_cbind_imported_targets DIRECTORY PROPERTY IMPORTED_TARGETS)
get_property(tbe_cbind_local_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
set(tbe_cbind_evidence_targets
  ${tbe_cbind_imported_targets} ${tbe_cbind_local_targets})
list(REMOVE_DUPLICATES tbe_cbind_evidence_targets)
file(WRITE "${tbe_cbind_evidence_dir}/manifest.txt" "")
foreach(evidence_target IN LISTS tbe_cbind_evidence_targets)
  string(MD5 evidence_key "${evidence_target}")
  file(APPEND "${tbe_cbind_evidence_dir}/manifest.txt"
    "${evidence_target}|${evidence_key}\n")
  file(GENERATE
    OUTPUT "${tbe_cbind_evidence_dir}/${evidence_key}-$<CONFIG>.txt"
    CONTENT
      "$<TARGET_GENEX_EVAL:${evidence_target},$<TARGET_PROPERTY:${evidence_target},INTERFACE_LINK_LIBRARIES>>")
endforeach()
