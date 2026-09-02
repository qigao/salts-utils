cmake_minimum_required(VERSION 3.27)
if(POLICY CMP0144)
  cmake_policy(SET CMP0144 OLD)
endif()
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
endif()

project(
  TurboParser
  VERSION 2.0.0
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

find_package(Threads REQUIRED)
if(NOT DEFINED ENV{ROCIDA_ROOT} OR "$ENV{ROCIDA_ROOT}" STREQUAL "")
  message(FATAL_ERROR "ROCIDA_ROOT must name the installed Rocida profile")
endif()
file(TO_CMAKE_PATH "$ENV{ROCIDA_ROOT}" ROCIDA_ROOT_PATH)
if(NOT IS_DIRECTORY "${ROCIDA_ROOT_PATH}")
  message(FATAL_ERROR "ROCIDA_ROOT is not a directory: $ENV{ROCIDA_ROOT}")
endif()

unset(Rocida_DIR CACHE)
unset(Rocida_DIR)
find_package(Rocida 1.1 CONFIG REQUIRED PATHS "${ROCIDA_ROOT_PATH}"
             NO_DEFAULT_PATH)

if(BUILD_TESTING)
  add_subdirectory(tests/runtime_path)
endif()

# Additional components
add_subdirectory(tools)
include(FindTools)
add_subdirectory(vendor)
add_subdirectory(turbo_parser)
if(TURBO_ENABLE_CAPTURE)
  add_subdirectory(capture)
endif()
add_subdirectory(turbo_serial)
if(TURBO_ENABLE_CFLOW_USB)
  add_subdirectory(cflow-usb)
endif()
add_subdirectory(cron)
add_subdirectory(mustache)
add_subdirectory(tbe)

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
  COMPATIBILITY SameMajorVersion)

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
