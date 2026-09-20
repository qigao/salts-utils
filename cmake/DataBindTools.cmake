# Explicit DataBind build-tool inputs.
#
# The movable DataBind subtree must not discover or own SaltsUtils build tools.
# Its parent build is responsible for supplying these DataBind-named inputs.

foreach(required_input IN ITEMS DATABIND_RE2C_EXECUTABLE DATABIND_LEMPAR)
  if(NOT DEFINED ${required_input} OR "${${required_input}}" STREQUAL "")
    message(FATAL_ERROR "${required_input} is required")
  endif()
  get_filename_component(
    ${required_input} "${${required_input}}" ABSOLUTE
    BASE_DIR "${CMAKE_BINARY_DIR}")
  if(NOT EXISTS "${${required_input}}")
    message(FATAL_ERROR
            "${required_input} does not exist: ${${required_input}}")
  endif()
endforeach()

if(DEFINED DATABIND_HOST_LEMON_EXECUTABLE AND
   NOT "${DATABIND_HOST_LEMON_EXECUTABLE}" STREQUAL "")
  get_filename_component(
    DATABIND_HOST_LEMON_EXECUTABLE
    "${DATABIND_HOST_LEMON_EXECUTABLE}" ABSOLUTE
    BASE_DIR "${CMAKE_BINARY_DIR}")
  if(NOT EXISTS "${DATABIND_HOST_LEMON_EXECUTABLE}")
    message(FATAL_ERROR
            "DATABIND_HOST_LEMON_EXECUTABLE does not exist: ${DATABIND_HOST_LEMON_EXECUTABLE}")
  endif()
  set(DATABIND_LEMON_COMMAND "${DATABIND_HOST_LEMON_EXECUTABLE}")
  set(DATABIND_LEMON_DEPENDS "${DATABIND_HOST_LEMON_EXECUTABLE}")
else()
  if(NOT DEFINED DATABIND_LEMON_TARGET OR
     "${DATABIND_LEMON_TARGET}" STREQUAL "")
    message(FATAL_ERROR
            "DATABIND_LEMON_TARGET is required when no host Lemon executable is supplied")
  endif()
  if(NOT TARGET ${DATABIND_LEMON_TARGET})
    message(FATAL_ERROR
            "DATABIND_LEMON_TARGET does not name a target: ${DATABIND_LEMON_TARGET}")
  endif()
  get_target_property(DATABIND_LEMON_TARGET_TYPE
                      ${DATABIND_LEMON_TARGET} TYPE)
  if(NOT DATABIND_LEMON_TARGET_TYPE STREQUAL "EXECUTABLE")
    message(FATAL_ERROR
            "DATABIND_LEMON_TARGET is not executable: ${DATABIND_LEMON_TARGET}")
  endif()
  set(DATABIND_LEMON_COMMAND "$<TARGET_FILE:${DATABIND_LEMON_TARGET}>")
  set(DATABIND_LEMON_DEPENDS "${DATABIND_LEMON_TARGET}")
endif()
