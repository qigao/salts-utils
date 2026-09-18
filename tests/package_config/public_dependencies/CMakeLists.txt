cmake_minimum_required(VERSION 3.27)

project(salts_utils_package_config_public_dependencies LANGUAGES C CXX)

if(NOT IS_DIRECTORY "${SALTS_UTILS_PACKAGE_DIR}")
  message(FATAL_ERROR
    "SALTS_UTILS_PACKAGE_DIR is not a directory: ${SALTS_UTILS_PACKAGE_DIR}")
endif()

find_package(SaltsUtils CONFIG REQUIRED
  PATHS "${SALTS_UTILS_PACKAGE_DIR}" NO_DEFAULT_PATH)

if(NOT TARGET OpenSSL::Crypto)
  message(FATAL_ERROR
    "SaltsUtils did not resolve the public OpenSSL::Crypto dependency")
endif()
