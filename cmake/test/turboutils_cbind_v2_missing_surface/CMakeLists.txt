cmake_minimum_required(VERSION 3.20)

project(turboutils_cbind_v2_missing_surface LANGUAGES C)

if(NOT DEFINED TP_PROBE_MODULE OR "${TP_PROBE_MODULE}" STREQUAL "")
  message(FATAL_ERROR "TP_PROBE_MODULE must identify the TurboUtils CBind v2 probe")
endif()

# This models an older package whose imported targets exist but whose headers
# do not expose the fixed-width, enum, or canonical UUID metadata surface.
add_library(TurboUtils::Core INTERFACE IMPORTED)
add_library(TurboUtils::CBind INTERFACE IMPORTED)
set_target_properties(TurboUtils::Core TurboUtils::CBind PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")

include("${TP_PROBE_MODULE}")
turboparser_require_turboutils_cbind_v2()
