# Monocypher - https://monocypher.org/
# Private DataBind schema-fingerprint implementation.
# Upstream files retain SPDX: BSD-2-Clause OR CC0-1.0.

# Keep all Monocypher globals/functions private to DataBind: Salts CNet
# embeds a distinct copy, and two DSOs must not export the same crypto_* ABI.
# This wrapper includes the unmodified upstream vendor implementation exactly
# once with a DataBind-private symbol prefix.
add_library(databind_monocypher STATIC
  databind_monocypher.c
  databind_monocypher_namespace.h
  monocypher.h)
set_target_properties(databind_monocypher PROPERTIES
  C_VISIBILITY_PRESET hidden)
target_include_directories(
  databind_monocypher PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
cmake_config_target(databind_monocypher
  FOLDER "databind/vendor")
