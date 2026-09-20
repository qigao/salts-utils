# Monocypher - https://monocypher.org/
# Private DataBind schema-fingerprint implementation.
# Upstream files retain SPDX: BSD-2-Clause OR CC0-1.0.

add_library(databind_monocypher STATIC monocypher.c monocypher.h)
target_include_directories(
  databind_monocypher PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
set_target_properties(databind_monocypher PROPERTIES FOLDER "tbe/vendor")
