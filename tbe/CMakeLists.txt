cmake_minimum_required(VERSION 3.27)

set(DATABIND_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(DATABIND_STANDALONE OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(DATABIND_STANDALONE ON)

  project(
    DataBind
    VERSION 3.0.0
    DESCRIPTION "Schema and data-binding runtime for Salts"
    LANGUAGES C CXX)

  set(CMAKE_C_STANDARD 11)
  set(CMAKE_C_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)

  include(GNUInstallDirs)
  include(CTest)
  enable_testing()

  option(BUILD_BENCHMARKS "Build DataBind benchmark executables" OFF)

  if(NOT DEFINED ENV{SALTS_ROOT} OR "$ENV{SALTS_ROOT}" STREQUAL "")
    message(FATAL_ERROR "SALTS_ROOT must name the installed Salts profile")
  endif()
  file(TO_CMAKE_PATH "$ENV{SALTS_ROOT}" SALTS_ROOT_PATH)
  if(NOT IS_DIRECTORY "${SALTS_ROOT_PATH}")
    message(FATAL_ERROR "SALTS_ROOT is not a directory: $ENV{SALTS_ROOT}")
  endif()

  unset(Salts_DIR CACHE)
  unset(Salts_DIR)
  find_package(Salts CONFIG REQUIRED
    PATHS "${SALTS_ROOT_PATH}"
    NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

  if(NOT DEFINED ENV{SALTS_UTILS_ROOT} OR "$ENV{SALTS_UTILS_ROOT}" STREQUAL "")
    message(FATAL_ERROR
            "SALTS_UTILS_ROOT must name the installed SaltsUtils profile for DataBind adapters/compiler tooling")
  endif()
  file(TO_CMAKE_PATH "$ENV{SALTS_UTILS_ROOT}" SALTS_UTILS_ROOT_PATH)
  if(NOT IS_DIRECTORY "${SALTS_UTILS_ROOT_PATH}")
    message(FATAL_ERROR
            "SALTS_UTILS_ROOT is not a directory: $ENV{SALTS_UTILS_ROOT}")
  endif()

  unset(SaltsUtils_DIR CACHE)
  unset(SaltsUtils_DIR)
  find_package(SaltsUtils CONFIG REQUIRED
    PATHS "${SALTS_UTILS_ROOT_PATH}"
    NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

  foreach(required_target IN ITEMS
          Salts::Core
          Salts::CMeta
          Salts::CSTL
          Salts::CSerde
          Salts::CFlow
          Salts::TinyTest
          Salts::JsonParser
          Salts::JsonCSerdeAdapter
          Salts::CYamlJsonAdapter
          Salts::CsvParser
          Salts::XmlParser
          Salts::DateTimeParser
          Salts::CmdParser
          Salts::Mustache)
    if(NOT TARGET ${required_target})
      message(FATAL_ERROR
              "Standalone DataBind dependency is missing target ${required_target}")
    endif()
  endforeach()
endif()

include("${DATABIND_SOURCE_ROOT}/cmake/DataBindTools.cmake")
include("${DATABIND_SOURCE_ROOT}/cmake/DataBindBuild.cmake")

add_subdirectory(vendor/monocypher)
add_subdirectory(schema)
add_subdirectory(data_bind)
add_subdirectory(tbe_compiler)

if(BUILD_TESTING)
  add_subdirectory(tests/package_config)
endif()

include("${DATABIND_SOURCE_ROOT}/cmake/DataBindPackage.cmake")

# Benchmark helpers create executable targets unconditionally. Keep the TBE
# aggregate build faithful to BUILD_BENCHMARKS so CI gates configured with
# BUILD_BENCHMARKS=OFF do not compile stale/non-gating benchmark consumers.
if(NOT BUILD_BENCHMARKS)
  foreach(benchmark_target
          benchmark_tbe_simple
          benchmark_encoding
          benchmark_data_bind_pool
          benchmark_data_bind_json_path
          benchmark_tbe_typed)
    if(TARGET ${benchmark_target})
      set_target_properties(${benchmark_target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()
  endforeach()
endif()
