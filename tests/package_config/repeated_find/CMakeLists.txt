cmake_minimum_required(VERSION 3.27)

project(salts_utils_package_config_repeated_find LANGUAGES C CXX)

if(NOT IS_DIRECTORY "${SALTS_UTILS_PACKAGE_DIR}")
  message(FATAL_ERROR
    "SALTS_UTILS_PACKAGE_DIR is not a directory: ${SALTS_UTILS_PACKAGE_DIR}")
endif()

find_package(OpenSSL CONFIG REQUIRED)
find_package(SaltsUtils CONFIG REQUIRED
  PATHS "${SALTS_UTILS_PACKAGE_DIR}" NO_DEFAULT_PATH)
find_package(SaltsUtils CONFIG REQUIRED)

if(NOT TARGET Salts::DataBind)
  message(FATAL_ERROR "Repeated package discovery lost Salts::DataBind")
endif()
