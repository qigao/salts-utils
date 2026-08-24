cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0144)
  cmake_policy(SET CMP0144 OLD)
endif()
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
endif()

project(
  TurboParser
  VERSION 1.0.0
  DESCRIPTION "Parsing and query runtime library for C and C++"
  HOMEPAGE_URL "https://github.com/qigao/turbo-parser"
  LANGUAGES C CXX)

# Project configuration - output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Normalize install prefix (remove trailing slashes)
get_filename_component(CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}" ABSOLUTE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
# Language standards - unified approach
set(CMAKE_CXX_STANDARD 17) # C++17 for compatibility
set(CMAKE_C_STANDARD 11) # C17 to ensure C11 atomic support
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  add_compile_options(/experimental:c11atomics)
endif()
# Enable folder organization in IDEs
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "cmake")

# Enable testing
include(CTest)
enable_testing()

include(CMakeOptions.cmake)
include(Sanitizers)
include(CmakeUtils)
include(GNUInstallDirs)

if(NOT DEFINED ENV{TURBOUTILS_ROOT}
   OR "$ENV{TURBOUTILS_ROOT}" STREQUAL "")
  message(FATAL_ERROR "Required dependency root is not defined: TURBOUTILS_ROOT")
endif()
if(NOT IS_DIRECTORY "$ENV{TURBOUTILS_ROOT}")
  message(FATAL_ERROR
          "TURBOUTILS_ROOT is not a directory: $ENV{TURBOUTILS_ROOT}")
endif()

find_package(Threads REQUIRED)
find_package(
  TurboUtils CONFIG REQUIRED
  PATHS "$ENV{TURBOUTILS_ROOT}"
  NO_DEFAULT_PATH)

if(NOT TARGET TurboUtils::Core)
  message(FATAL_ERROR "TurboUtils::Core is required by TurboParser")
endif()
if(NOT TARGET TurboUtils::STL)
  message(FATAL_ERROR "TurboUtils::STL is required by TurboParser")
endif()
if(BUILD_TESTING AND NOT TARGET TurboUtils::TinyTest)
  message(FATAL_ERROR "TurboUtils::TinyTest is required when TurboParser tests are enabled")
endif()

# Additional components
add_subdirectory(tools)
include(FindTools)
add_subdirectory(parser/query_vm)
add_subdirectory(vendor)
add_subdirectory(parser)
add_subdirectory(turbo_parser)
add_subdirectory(cron)
add_subdirectory(mustache)
add_subdirectory(tools/junit_to_html)
add_subdirectory(tbe)
add_subdirectory(tools/lua)

# Package Configuration
include(CMakePackageConfigHelpers)

# Generate the config file
configure_package_config_file(
  "${CMAKE_SOURCE_DIR}/cmake/TurboParserConfig.cmake.in"
  "${CMAKE_BINARY_DIR}/TurboParserConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/TurboParser")

# Generate the version file
write_basic_package_version_file(
  "${CMAKE_BINARY_DIR}/TurboParserConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion)

# Install the config files
install(FILES "${CMAKE_BINARY_DIR}/TurboParserConfig.cmake"
              "${CMAKE_BINARY_DIR}/TurboParserConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/TurboParser")

# Install the export set
export(
  EXPORT TurboParserTargets
  FILE "${CMAKE_BINARY_DIR}/TurboParserTargets.cmake"
  NAMESPACE TurboParser::)

install(
  EXPORT TurboParserTargets
  FILE TurboParserTargets.cmake
  NAMESPACE TurboParser::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/TurboParser")
