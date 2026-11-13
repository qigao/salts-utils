cmake_minimum_required(VERSION 3.27)
if(POLICY CMP0144)
  cmake_policy(SET CMP0144 OLD)
endif()
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
endif()

include(CMakeOptions.cmake)

project(
  SaltsUtils
  VERSION 0.1.4
  DESCRIPTION "Higher-level utilities built on Salts&CMeta"
  HOMEPAGE_URL "https://github.com/qigao/salts-utils"
  LANGUAGES C CXX)

# Project configuration - output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Normalize install prefix (remove trailing slashes)
get_filename_component(CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}" ABSOLUTE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
# The root owns language mode and PIC for every module in this project.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(UNIX)
  # Strict C11 still needs declarations for the POSIX APIs used by IDL and
  # compiler tests, including strdup and fileno.
  add_compile_definitions("$<$<COMPILE_LANGUAGE:C>:_POSIX_C_SOURCE=200809L>")
endif()

if(MSVC)
  add_compile_options(-experimental:c11atomics)
endif()
# Enable folder organization in IDEs
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "cmake")

if(BUILD_TESTS)
  enable_testing()
endif()
include(Sanitizers)
include(CmakeUtils)
include(GNUInstallDirs)

# Consume the selected preinstalled dependency tree without a toolchain.
set(SALTS_UTILS_DEPENDENCY_PREFIX "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")

find_package(Threads REQUIRED)

find_package(SaltsBase "$ENV{SALTS_BASE_VERSION}" EXACT CONFIG REQUIRED PATHS "$ENV{SALTS_BASE_ROOT}"
             NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
find_package(CMeta "$ENV{CMETA_VERSION}" EXACT CONFIG REQUIRED PATHS "$ENV{CMETA_ROOT}"
             NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

include(FindTools)
add_subdirectory(vendor)
add_subdirectory(mustache)
add_subdirectory(jinja)
add_subdirectory(databind)
add_subdirectory(bindings)

# Package Configuration
include(CMakePackageConfigHelpers)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SaltsUtilsConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/SaltsUtilsConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SaltsUtils")

# Generate the version file
write_basic_package_version_file(
  "${CMAKE_BINARY_DIR}/SaltsUtilsConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

# Install the config files
install(
  FILES "${CMAKE_BINARY_DIR}/SaltsUtilsConfig.cmake"
        "${CMAKE_BINARY_DIR}/SaltsUtilsConfigVersion.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/IDL.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SaltsUtils"
  COMPONENT Development)

# Install the export set
export(
  EXPORT SaltsUtilsTargets
  FILE "${CMAKE_BINARY_DIR}/SaltsUtilsTargets.cmake"
  NAMESPACE Salts::)

install(
  EXPORT SaltsUtilsTargets
  FILE SaltsUtilsTargets.cmake
  NAMESPACE Salts::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SaltsUtils"
  COMPONENT Development)
