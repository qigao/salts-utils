# Test-only CSTL owner topology from Salts #1167 / Draft PR #1169.
# Source code and release tags remain unchanged. ASan ODR checking stays ON.
# This private static-owner topology is not a packaging/ABI migration decision.
if(NOT CMAKE_PROJECT_NAME STREQUAL "Salts")
  message(FATAL_ERROR "CSTL visibility overlay applies only to project(Salts)")
endif()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL
  set_target_properties cmeta_cstl PROPERTIES
  C_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)
