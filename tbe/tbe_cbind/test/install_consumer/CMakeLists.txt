cmake_minimum_required(VERSION 3.20)

project(tbe_cbind_install_consumer LANGUAGES C)

find_package(TurboParser CONFIG REQUIRED
  PATHS "${TurboParser_DIR}"
  NO_DEFAULT_PATH)

if(TBE_CBIND_TEST_INJECT_CONDITIONAL_EXTRA_DEPENDENCY)
  add_library(ExtraDependencyProbe INTERFACE)
  add_library(WrapperTarget INTERFACE)
  target_link_libraries(WrapperTarget INTERFACE ExtraDependencyProbe)
  set_property(TARGET TurboParser::TbeCBind APPEND PROPERTY
    INTERFACE_LINK_LIBRARIES
    "$<$<CONFIG:Release>:WrapperTarget>")
endif()
if(TBE_CBIND_TEST_INJECT_DIRECT_EXTRA_DEPENDENCY)
  add_library(DirectExtraDependency INTERFACE)
  set_property(TARGET TurboParser::TbeCBind APPEND PROPERTY
    INTERFACE_LINK_LIBRARIES DirectExtraDependency)
endif()

add_executable(tbe_cbind_install_consumer main.c)
target_link_libraries(tbe_cbind_install_consumer PRIVATE
  TurboParser::TbeCBind
  TurboParser::Parser)
set_target_properties(tbe_cbind_install_consumer PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON)
file(GENERATE
  OUTPUT "${CMAKE_BINARY_DIR}/tbe-cbind-consumer-location-$<CONFIG>.txt"
  CONTENT "$<TARGET_FILE:tbe_cbind_install_consumer>")

set(tbe_cbind_evidence_dir "${CMAKE_BINARY_DIR}/tbe-cbind-interface-evidence")
file(MAKE_DIRECTORY "${tbe_cbind_evidence_dir}")
get_target_property(tbe_cbind_raw_interface TurboParser::TbeCBind
  INTERFACE_LINK_LIBRARIES)
file(WRITE "${tbe_cbind_evidence_dir}/raw-interface.txt"
  "${tbe_cbind_raw_interface}")
file(GENERATE
  OUTPUT "${tbe_cbind_evidence_dir}/evaluated-interface-$<CONFIG>.txt"
  CONTENT
    "$<TARGET_GENEX_EVAL:TurboParser::TbeCBind,$<TARGET_PROPERTY:TurboParser::TbeCBind,INTERFACE_LINK_LIBRARIES>>")
