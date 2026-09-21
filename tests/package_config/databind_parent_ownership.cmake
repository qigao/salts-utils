cmake_minimum_required(VERSION 3.27)

if(NOT DEFINED PARENT_SOURCE_ROOT OR "${PARENT_SOURCE_ROOT}" STREQUAL "")
  message(FATAL_ERROR "PARENT_SOURCE_ROOT is required")
endif()

file(READ "${PARENT_SOURCE_ROOT}/cmake/SaltsUtilsConfig.cmake.in" PACKAGE_CONFIG)
foreach(COMPONENT_EXPORT IN ITEMS
        "SaltsUtilsDataBindTargets.cmake"
        "SaltsUtilsDataBindAdapterTargets.cmake")
  string(FIND "${PACKAGE_CONFIG}" "${COMPONENT_EXPORT}" EXPORT_POSITION)
  if(EXPORT_POSITION EQUAL -1)
    message(FATAL_ERROR
      "SaltsUtils does not import its DataBind component: ${COMPONENT_EXPORT}")
  endif()
endforeach()

foreach(FORBIDDEN_PACKAGE_ASSET IN ITEMS
        "tbe/cmake/DataBindConfig.cmake.in"
        "tbe/CMakePresets.json")
  if(EXISTS "${PARENT_SOURCE_ROOT}/${FORBIDDEN_PACKAGE_ASSET}")
    message(FATAL_ERROR
      "Independent DataBind package/build entry still exists: ${FORBIDDEN_PACKAGE_ASSET}")
  endif()
endforeach()

if(NOT EXISTS "${PARENT_SOURCE_ROOT}/tbe/vendor/monocypher")
  message(FATAL_ERROR "DataBind private Monocypher dependency is missing")
endif()

file(READ "${PARENT_SOURCE_ROOT}/CMakeLists.txt" PARENT_CMAKE)
foreach(REQUIRED_TOOL_MAPPING IN ITEMS
        "set(DATABIND_RE2C_EXECUTABLE \"\${RE2C_EXECUTABLE}\")"
        "set(DATABIND_LEMPAR \"\${LEMPAR}\")"
        "set(DATABIND_LEMON_TARGET lemon)")
  string(FIND "${PARENT_CMAKE}"
              "${REQUIRED_TOOL_MAPPING}"
              TOOL_MAPPING_POSITION)
  if(TOOL_MAPPING_POSITION EQUAL -1)
    message(FATAL_ERROR
      "SaltsUtils no longer maps its DataBind tool input: ${REQUIRED_TOOL_MAPPING}")
  endif()
endforeach()

message(STATUS "SaltsUtils ownership of its DataBind component passed")
