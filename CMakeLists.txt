cmake_minimum_required(VERSION 3.27)
if(POLICY CMP0144)
  cmake_policy(SET CMP0144 OLD)
endif()
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
endif()

# Single canonical C source profile for C11 workflow consumers. Its build
# graph does not configure the unrelated legacy data/UI/serial systems, and
# does not provide alternate sources for any selected module.
option(SALTS_UTILS_NATIVE_C11_WORKFLOW_SUBSET
       "Build only the native C11 QueryVM/JsonParser/CYaml/BehaviorTree/Process providers"
       OFF)
if(SALTS_UTILS_NATIVE_C11_WORKFLOW_SUBSET)
  set(_salts_utils_languages C)
else()
  set(_salts_utils_languages C CXX)
endif()
project(
  SaltsUtils
  VERSION 4.3.0
  DESCRIPTION "Higher-level utilities built on Salts"
  HOMEPAGE_URL "https://github.com/qigao/salts-utils"
  LANGUAGES ${_salts_utils_languages})
unset(_salts_utils_languages)

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
  add_compile_options(-experimental:c11atomics)
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

# Run a selected native program with the configured preset's runtime
# environment. Praktor supplies the executable and argv through the environment.
add_custom_target(
  run-program
  COMMAND
    "${CMAKE_COMMAND}" -DLOCAL_ACTION=run
    "-DSALTS_UTILS_CROSSCOMPILING=${CMAKE_CROSSCOMPILING}" -P
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunLocal.cmake"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  USES_TERMINAL
  VERBATIM)
set_target_properties(run-program PROPERTIES FOLDER "Tools")

find_package(Threads REQUIRED)

if(NOT DEFINED ENV{SALTS_ROOT} OR NOT IS_DIRECTORY "$ENV{SALTS_ROOT}")
  message(FATAL_ERROR "SALTS_ROOT must name the restored Salts SDK; run cmake/ci/restore-native-sdk first")
endif()
# A previous configure must not keep selecting an older restored SDK.
file(TO_CMAKE_PATH "$ENV{SALTS_ROOT}" SALTS_ROOT_PATH)
unset(Salts_DIR CACHE)
unset(Salts_DIR)
find_package(Salts CONFIG REQUIRED PATHS "${SALTS_ROOT_PATH}"
             NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
# SHA256, Ed448, SM3, X.509 and TLS are sourced solely from qigao/crypto.
# No fallback implementation or local Crypto target may be built by Utils.
# For cross-compilation, do not re-root the explicitly provisioned Crypto
# SDK through the Android/iOS/vcpkg sysroot. Do not search another provider.
if(DEFINED ENV{CRYPTO_ROOT} AND NOT "$ENV{CRYPTO_ROOT}" STREQUAL "")
  file(TO_CMAKE_PATH "$ENV{CRYPTO_ROOT}" SALTS_UTILS_CRYPTO_ROOT)
  if(NOT EXISTS "${SALTS_UTILS_CRYPTO_ROOT}/lib/cmake/Crypto/CryptoConfig.cmake")
    message(FATAL_ERROR "CRYPTO_ROOT must point to the installed static Crypto SDK")
  endif()
  find_package(Crypto 0.1 CONFIG REQUIRED
    PATHS "${SALTS_UTILS_CRYPTO_ROOT}"
    NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
  unset(SALTS_UTILS_CRYPTO_ROOT)
else()
  find_package(Crypto 0.1 CONFIG REQUIRED)
endif()
if(NOT TARGET Salts::Crypto)
  message(FATAL_ERROR "Canonical Salts::Crypto static SDK is required")
endif()
get_target_property(_crypto_kind Salts::Crypto TYPE)
get_target_property(_crypto_imported Salts::Crypto IMPORTED)
if(NOT _crypto_imported OR NOT _crypto_kind STREQUAL "STATIC_LIBRARY")
  message(FATAL_ERROR "Salts::Crypto must be the imported static Crypto package owner")
endif()
unset(_crypto_kind)
unset(_crypto_imported)

add_subdirectory(tools)
include(FindTools)
if(NOT SALTS_UTILS_NATIVE_C11_WORKFLOW_SUBSET)
  add_subdirectory(vendor)
endif()
if(SALTS_UTILS_NATIVE_C11_WORKFLOW_SUBSET)
  # Exact native source owners only, without a second parser, provider or C++
  # compatibility adapter. Keep the C11 dependency DAG explicit.
  add_subdirectory(parser/query_vm)
  add_subdirectory(parser/json_parser)
  add_subdirectory(parser/cyaml)
  add_subdirectory(cflow-process)
  add_subdirectory(behavior-tree)
else()
  add_subdirectory(parser)
  add_subdirectory(bindings)
  add_subdirectory(tools/junit_to_html)
  add_subdirectory(cflow-fs)
  add_subdirectory(cflow-process)
  add_subdirectory(behavior-tree)
  if(SALTS_UTILS_ENABLE_CAPTURE)
    add_subdirectory(playback)
    add_subdirectory(capture)
  endif()
  add_subdirectory(salts_serial)
  if(SALTS_UTILS_ENABLE_CFLOW_USB)
    add_subdirectory(cflow-usb)
  endif()
  add_subdirectory(cron)
  add_subdirectory(mustache)
endif()
# The search core is a standalone C11 algorithm target; not an installed SDK.
add_subdirectory(search)
# Salts owns Unicode 17. The released Salts SDK must provide the one
# canonical imported target before Jinja or any other consumer is configured.
# Never compile or export the legacy local Unicode implementation here.
if(NOT TARGET Salts::Unicode)
  message(FATAL_ERROR "SaltsUtils requires a published Salts SDK with Salts::Unicode")
endif()
get_target_property(_salts_utils_unicode_imported Salts::Unicode IMPORTED)
if(NOT _salts_utils_unicode_imported)
  message(FATAL_ERROR "Salts::Unicode must be imported from the Salts SDK")
endif()
unset(_salts_utils_unicode_imported)
if(NOT SALTS_UTILS_NATIVE_C11_WORKFLOW_SUBSET)
  add_subdirectory(jinja)
  add_subdirectory(databind)
endif()

# Package Configuration
include(CMakePackageConfigHelpers)

# Generate the config file
configure_file("${CMAKE_SOURCE_DIR}/cmake/IDL.cmake"
               "${CMAKE_BINARY_DIR}/IDL.cmake" COPYONLY)
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
install(
  FILES "${CMAKE_BINARY_DIR}/SaltsUtilsConfig.cmake"
        "${CMAKE_BINARY_DIR}/SaltsUtilsConfigVersion.cmake"
        "${CMAKE_SOURCE_DIR}/cmake/IDL.cmake"
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
