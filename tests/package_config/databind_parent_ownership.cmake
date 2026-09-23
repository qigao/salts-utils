cmake_minimum_required(VERSION 3.27)

if(NOT DEFINED PARENT_SOURCE_ROOT OR "${PARENT_SOURCE_ROOT}" STREQUAL "")
  message(FATAL_ERROR "PARENT_SOURCE_ROOT is required")
endif()

file(READ "${PARENT_SOURCE_ROOT}/cmake/SaltsUtilsConfig.cmake.in" PACKAGE_CONFIG)
string(FIND "${PACKAGE_CONFIG}" "SaltsUtilsTargets.cmake" SALTS_UTILS_TARGETS_POSITION)
if(SALTS_UTILS_TARGETS_POSITION EQUAL -1)
  message(FATAL_ERROR "SaltsUtils package must import SaltsUtilsTargets.cmake")
endif()

foreach(FORBIDDEN_EXPORT IN ITEMS
        "SaltsUtilsDataBindTargets.cmake"
        "SaltsUtilsDataBindAdapterTargets.cmake")
  string(FIND "${PACKAGE_CONFIG}" "${FORBIDDEN_EXPORT}" FORBIDDEN_EXPORT_POSITION)
  if(NOT FORBIDDEN_EXPORT_POSITION EQUAL -1)
    message(FATAL_ERROR
      "DataBind still has an independent export surface: ${FORBIDDEN_EXPORT}")
  endif()
endforeach()

if(EXISTS "${PARENT_SOURCE_ROOT}/tbe/cmake")
  message(FATAL_ERROR "databind/cmake must not exist; DataBind uses SaltsUtils build helpers")
endif()

foreach(COMPONENT_CMAKE IN ITEMS
        "databind/schema/CMakeLists.txt"
        "databind/runtime/CMakeLists.txt")
  file(READ "${PARENT_SOURCE_ROOT}/${COMPONENT_CMAKE}" COMPONENT_CONTENT)
  string(FIND "${COMPONENT_CONTENT}" "EXPORT SaltsUtilsTargets" EXPORT_POSITION)
  if(EXPORT_POSITION EQUAL -1)
    message(FATAL_ERROR
      "${COMPONENT_CMAKE} does not export through SaltsUtilsTargets")
  endif()
  foreach(FORBIDDEN_EXPORT IN ITEMS "EXPORT DataBindTargets" "EXPORT DataBindAdapterTargets")
    string(FIND "${COMPONENT_CONTENT}" "${FORBIDDEN_EXPORT}" FORBIDDEN_POSITION)
    if(NOT FORBIDDEN_POSITION EQUAL -1)
      message(FATAL_ERROR
        "${COMPONENT_CMAKE} still uses independent DataBind export set: ${FORBIDDEN_EXPORT}")
    endif()
  endforeach()
endforeach()

message(STATUS "SaltsUtils owns TBE/DataBind through its single package surface")
