cmake_minimum_required(VERSION 3.27)

foreach(required_var IN ITEMS SOURCE_ROOT STAGE_ROOT)
  if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
    message(FATAL_ERROR "${required_var} is required")
  endif()
endforeach()

get_filename_component(SOURCE_ROOT "${SOURCE_ROOT}" ABSOLUTE)
get_filename_component(STAGE_ROOT "${STAGE_ROOT}" ABSOLUTE)

if(SOURCE_ROOT STREQUAL STAGE_ROOT)
  message(FATAL_ERROR "STAGE_ROOT must be separate from SOURCE_ROOT")
endif()

set(required_paths
  "tbe"
  "tools/lemon"
  "vendor/monocypher"
  "cmake/CmakeUtils.cmake"
  "cmake/FindTools.cmake"
  "cmake/DataBindConfig.cmake.in"
  "cmake/ci/install-re2c.sh"
  "tests/package_config/databind_adapters"
  "tests/package_config/databind_repeated_find"
  "tests/package_config/repeated_find"
  "cmake/databind-repository/CMakeLists.txt")

foreach(relative_path IN LISTS required_paths)
  if(NOT EXISTS "${SOURCE_ROOT}/${relative_path}")
    message(FATAL_ERROR "DataBind repository source is missing: ${relative_path}")
  endif()
endforeach()

file(REMOVE_RECURSE "${STAGE_ROOT}")
file(MAKE_DIRECTORY
  "${STAGE_ROOT}/cmake/ci"
  "${STAGE_ROOT}/tools"
  "${STAGE_ROOT}/vendor"
  "${STAGE_ROOT}/tests/package_config")

file(COPY "${SOURCE_ROOT}/tbe" DESTINATION "${STAGE_ROOT}")
file(COPY "${SOURCE_ROOT}/tools/lemon" DESTINATION "${STAGE_ROOT}/tools")
file(COPY "${SOURCE_ROOT}/vendor/monocypher" DESTINATION "${STAGE_ROOT}/vendor")
file(COPY
  "${SOURCE_ROOT}/tests/package_config/databind_adapters"
  "${SOURCE_ROOT}/tests/package_config/databind_repeated_find"
  "${SOURCE_ROOT}/tests/package_config/repeated_find"
  DESTINATION "${STAGE_ROOT}/tests/package_config")

foreach(cmake_file IN ITEMS CmakeUtils.cmake FindTools.cmake DataBindConfig.cmake.in)
  configure_file(
    "${SOURCE_ROOT}/cmake/${cmake_file}"
    "${STAGE_ROOT}/cmake/${cmake_file}"
    COPYONLY)
endforeach()
configure_file(
  "${SOURCE_ROOT}/cmake/ci/install-re2c.sh"
  "${STAGE_ROOT}/cmake/ci/install-re2c.sh"
  COPYONLY)
configure_file(
  "${SOURCE_ROOT}/cmake/databind-repository/CMakeLists.txt"
  "${STAGE_ROOT}/CMakeLists.txt"
  COPYONLY)

file(WRITE "${STAGE_ROOT}/EXTRACTION_SOURCE.txt"
  "source_repository=qigao/salts-utils\n"
  "source_root=${SOURCE_ROOT}\n")

foreach(forbidden_path IN ITEMS
    "crypto"
    "jinja"
    "mustache"
    "parser"
    "salts_serial"
    "cflow-fs"
    "cflow-process")
  if(EXISTS "${STAGE_ROOT}/${forbidden_path}")
    message(FATAL_ERROR
      "Standalone DataBind stage copied unrelated SaltsUtils source: ${forbidden_path}")
  endif()
endforeach()

message(STATUS "Staged standalone DataBind repository at ${STAGE_ROOT}")
