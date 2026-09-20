cmake_minimum_required(VERSION 3.27)

if(NOT DEFINED PARENT_SOURCE_ROOT OR "${PARENT_SOURCE_ROOT}" STREQUAL "")
  message(FATAL_ERROR "PARENT_SOURCE_ROOT is required")
endif()

foreach(FORBIDDEN_PARENT_ASSET IN ITEMS
        "cmake/DataBindConfig.cmake.in"
        "vendor/monocypher"
        "tests/package_config/databind_repeated_find"
        "tests/package_config/databind_adapters")
  if(EXISTS "${PARENT_SOURCE_ROOT}/${FORBIDDEN_PARENT_ASSET}")
    message(FATAL_ERROR
            "SaltsUtils regained DataBind parent ownership: ${FORBIDDEN_PARENT_ASSET}")
  endif()
endforeach()

file(READ "${PARENT_SOURCE_ROOT}/CMakeLists.txt" PARENT_CMAKE)
foreach(FORBIDDEN_PARENT_FRAGMENT IN ITEMS
        "cmake/DataBindConfig.cmake.in"
        "EXPORT DataBindTargets"
        "EXPORT DataBindAdapterTargets"
        "DataBindConfigVersion.cmake")
  string(FIND "${PARENT_CMAKE}"
              "${FORBIDDEN_PARENT_FRAGMENT}"
              PARENT_PACKAGE_POSITION)
  if(NOT PARENT_PACKAGE_POSITION EQUAL -1)
    message(FATAL_ERROR
            "SaltsUtils top-level CMake regained DataBind package ownership: ${FORBIDDEN_PARENT_FRAGMENT}")
  endif()
endforeach()

if(NOT EXISTS "${PARENT_SOURCE_ROOT}/tbe/cmake/DataBindPackage.cmake")
  message(FATAL_ERROR "DataBind package ownership is no longer under tbe/")
endif()
if(NOT EXISTS "${PARENT_SOURCE_ROOT}/tbe/vendor/monocypher")
  message(FATAL_ERROR "DataBind private Monocypher ownership is no longer under tbe/")
endif()

foreach(REQUIRED_TOOL_MAPPING IN ITEMS
        "set(DATABIND_RE2C_EXECUTABLE \"\${RE2C_EXECUTABLE}\")"
        "set(DATABIND_LEMPAR \"\${LEMPAR}\")"
        "set(DATABIND_LEMON_TARGET lemon)")
  string(FIND "${PARENT_CMAKE}"
              "${REQUIRED_TOOL_MAPPING}"
              TOOL_MAPPING_POSITION)
  if(TOOL_MAPPING_POSITION EQUAL -1)
    message(FATAL_ERROR
            "SaltsUtils parent no longer maps explicit DataBind tool input: ${REQUIRED_TOOL_MAPPING}")
  endif()
endforeach()

message(STATUS "SaltsUtils/DataBind parent ownership transition contract passed")
