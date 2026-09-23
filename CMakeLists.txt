cmake_minimum_required(VERSION 3.27)
if(POLICY CMP0144)
  cmake_policy(SET CMP0144 OLD)
endif()
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
endif()

project(
  SaltsUtils
  VERSION 3.0.0
  DESCRIPTION "Higher-level utilities built on Salts"
  HOMEPAGE_URL "https://github.com/qigao/salts-utils"
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

include(CMakeOptions.cmake)

if(BUILD_TESTS)
  enable_testing()
endif()
include(Sanitizers)
include(CmakeUtils)
include(GNUInstallDirs)

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
find_package(Salts 1.2.0 EXACT CONFIG REQUIRED PATHS "${SALTS_ROOT_PATH}"
             NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

foreach(required_salts_target IN ITEMS
        Salts::Platform
        Salts::Core
        Salts::CMeta
        Salts::CFlow
        Salts::CSTL
        Salts::CSerde
        Salts::UriParser)
  if(NOT TARGET ${required_salts_target})
    message(FATAL_ERROR
            "SALTS_ROOT package is missing required target ${required_salts_target}")
  endif()
endforeach()

foreach(pre_migration_target IN ITEMS
        Salts::Crypto
        Salts::CFlowFS
        Salts::CFlowProcess
        Salts::QueryVM
        Salts::IniParser
        Salts::JsonParser
        Salts::JsonCSerdeAdapter
        Salts::XmlParser
        Salts::CsvParser
        Salts::TLVParser
        Salts::LtvParser
        Salts::ModbusParser
        Salts::SoaParser
        Salts::DotEnvParser
        Salts::CmdParser
        Salts::Toon
        Salts::TomlParser
        Salts::DateTimeParser
        Salts::CYaml
        Salts::CYamlJsonAdapter
        Salts::Selector)
  if(TARGET ${pre_migration_target})
    message(FATAL_ERROR
            "SALTS_ROOT points to a pre-migration Salts package that still exports ${pre_migration_target}")
  endif()
endforeach()

if(BUILD_TESTS)
  add_subdirectory(tests/runtime_path)
  add_subdirectory(tests/package_config)
endif()

# Additional components
add_subdirectory(tools)
include(FindTools)
add_subdirectory(vendor)
add_subdirectory(parser)
add_subdirectory(tools/junit_to_html)
add_subdirectory(crypto)
add_subdirectory(plugin)
add_subdirectory(cflow-fs)
add_subdirectory(cflow-process)
add_subdirectory(playback)
if(SALTS_UTILS_ENABLE_CAPTURE)
  add_subdirectory(capture)
endif()
add_subdirectory(salts_serial)
if(SALTS_UTILS_ENABLE_CFLOW_USB)
  add_subdirectory(cflow-usb)
endif()
add_subdirectory(cron)
add_subdirectory(mustache)
add_subdirectory(unicode)
add_subdirectory(jinja)

add_subdirectory(databind)

# Package Configuration
include(CMakePackageConfigHelpers)

# Generate the config file
configure_package_config_file(
  "${CMAKE_SOURCE_DIR}/cmake/SaltsUtilsConfig.cmake.in"
  "${CMAKE_BINARY_DIR}/SaltsUtilsConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SaltsUtils")

# Generate the version file
write_basic_package_version_file(
  "${CMAKE_BINARY_DIR}/SaltsUtilsConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

# Install the config files
install(FILES "${CMAKE_BINARY_DIR}/SaltsUtilsConfig.cmake"
              "${CMAKE_BINARY_DIR}/SaltsUtilsConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SaltsUtils")

# Install the export set
export(
  EXPORT SaltsUtilsTargets
  FILE "${CMAKE_BINARY_DIR}/SaltsUtilsTargets.cmake"
  NAMESPACE Salts::)

install(
  EXPORT SaltsUtilsTargets
  FILE SaltsUtilsTargets.cmake
  NAMESPACE Salts::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SaltsUtils")
