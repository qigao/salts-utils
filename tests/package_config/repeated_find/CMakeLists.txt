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

foreach(forbidden_target IN ITEMS
    Salts::DataBindSchema
    Salts::DataBindCore
    Salts::DataBind
    Salts::DataBindCMeta
    Salts::DataBindCFlow)
  if(TARGET ${forbidden_target})
    message(FATAL_ERROR
      "SaltsUtils package must not export DataBind-owned target ${forbidden_target}")
  endif()
endforeach()
