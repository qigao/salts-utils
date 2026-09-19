cmake_minimum_required(VERSION 3.27)

if(POLICY CMP0144)
  cmake_policy(SET CMP0144 OLD)
endif()
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
endif()

project(
  DataBind
  VERSION 3.0.0
  DESCRIPTION "Schema-driven C data binding and code generation for Salts"
  HOMEPAGE_URL "https://github.com/qigao/databind"
  LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

if(MSVC)
  add_compile_options(/experimental:c11atomics)
endif()

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(BUILD_BENCHMARKS "Build DataBind benchmarks" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CmakeUtils)

find_package(Threads REQUIRED)

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

foreach(required_salts_target IN ITEMS
    Salts::Core
    Salts::CFlow
    Salts::CMeta
    Salts::CSTL
    Salts::CSerde
    Salts::TinyTest)
  if(NOT TARGET ${required_salts_target})
    message(FATAL_ERROR
      "SALTS_ROOT package is missing required target ${required_salts_target}")
  endif()
endforeach()

if(NOT DEFINED ENV{SALTS_UTILS_ROOT} OR "$ENV{SALTS_UTILS_ROOT}" STREQUAL "")
  message(FATAL_ERROR
    "SALTS_UTILS_ROOT is required to build DataBind adapters and compiler tooling")
endif()
file(TO_CMAKE_PATH "$ENV{SALTS_UTILS_ROOT}" SALTS_UTILS_ROOT_PATH)
if(NOT IS_DIRECTORY "${SALTS_UTILS_ROOT_PATH}")
  message(FATAL_ERROR
    "SALTS_UTILS_ROOT is not a directory: $ENV{SALTS_UTILS_ROOT}")
endif()

foreach(stale_databind_target IN ITEMS
    Salts::DataBind
    Salts::DataBindCore
    Salts::DataBindCMeta
    Salts::DataBindCFlow
    Salts::TbeSchema)
  if(TARGET ${stale_databind_target})
    message(FATAL_ERROR
      "DataBind target was loaded before the standalone package: ${stale_databind_target}")
  endif()
endforeach()

unset(SaltsUtils_DIR CACHE)
unset(SaltsUtils_DIR)
find_package(SaltsUtils CONFIG REQUIRED
  PATHS "${SALTS_UTILS_ROOT_PATH}"
  NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

file(REAL_PATH "${SALTS_UTILS_ROOT_PATH}" DATABIND_SALTS_UTILS_REAL_ROOT)
file(REAL_PATH "${SaltsUtils_DIR}" DATABIND_SALTS_UTILS_REAL_DIR)
cmake_path(IS_PREFIX DATABIND_SALTS_UTILS_REAL_ROOT
  "${DATABIND_SALTS_UTILS_REAL_DIR}" NORMALIZE
  DATABIND_SALTS_UTILS_IS_WITHIN_ROOT)
if(NOT DATABIND_SALTS_UTILS_IS_WITHIN_ROOT)
  message(FATAL_ERROR
    "SaltsUtils resolved outside SALTS_UTILS_ROOT: ${SaltsUtils_DIR}")
endif()

foreach(stale_databind_target IN ITEMS
    Salts::DataBind
    Salts::DataBindCore
    Salts::DataBindCMeta
    Salts::DataBindCFlow
    Salts::TbeSchema)
  if(TARGET ${stale_databind_target})
    message(FATAL_ERROR
      "SALTS_UTILS_ROOT still owns DataBind target: ${stale_databind_target}")
  endif()
endforeach()

foreach(required_utils_target IN ITEMS
    Salts::JsonCSerdeAdapter
    Salts::CYamlJsonAdapter
    Salts::CsvParser
    Salts::XmlParser
    Salts::DateTimeParser
    Salts::CmdParser
    Salts::Mustache)
  if(NOT TARGET ${required_utils_target})
    message(FATAL_ERROR
      "SALTS_UTILS_ROOT is missing DataBind build dependency ${required_utils_target}")
  endif()
endforeach()

add_subdirectory(tools/lemon)
include(FindTools)
add_subdirectory(vendor/monocypher)

set(DATABIND_TEST_SALTS_UTILS_ROOT "${SALTS_UTILS_ROOT_PATH}")
add_subdirectory(tbe)

set(DATABIND_PACKAGE_VERSION 3.0.0)
configure_package_config_file(
  "${CMAKE_SOURCE_DIR}/cmake/DataBindConfig.cmake.in"
  "${CMAKE_BINARY_DIR}/DataBindConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/DataBind")
write_basic_package_version_file(
  "${CMAKE_BINARY_DIR}/DataBindConfigVersion.cmake"
  VERSION ${DATABIND_PACKAGE_VERSION}
  COMPATIBILITY SameMajorVersion)

export(
  EXPORT DataBindTargets
  FILE "${CMAKE_BINARY_DIR}/DataBindTargets.cmake"
  NAMESPACE Salts::)
export(
  EXPORT DataBindAdapterTargets
  FILE "${CMAKE_BINARY_DIR}/DataBindAdapterTargets.cmake"
  NAMESPACE Salts::)

install(
  EXPORT DataBindTargets
  FILE DataBindTargets.cmake
  NAMESPACE Salts::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/DataBind"
  COMPONENT DataBind)
install(
  EXPORT DataBindAdapterTargets
  FILE DataBindAdapterTargets.cmake
  NAMESPACE Salts::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/DataBind"
  COMPONENT DataBind)
install(
  FILES "${CMAKE_BINARY_DIR}/DataBindConfig.cmake"
        "${CMAKE_BINARY_DIR}/DataBindConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/DataBind"
  COMPONENT DataBind)
