cmake_minimum_required(VERSION 3.27)

project(databind_package_config_repeated_find LANGUAGES C CXX)

# Simulate a clean installed consumer with no GmSSL CMake package. Merely
# importing DataBind (twice) must never force its unrelated Crypto provider.
# This is a REQUIRED, executed CTest package-config gate, unlike a dormant
# custom target. GmSSL-free import may not fabricate a substitute target.
set(CMAKE_DISABLE_FIND_PACKAGE_GmSSL TRUE)

if(NOT IS_DIRECTORY "${SALTS_UTILS_PACKAGE_DIR}")
  message(FATAL_ERROR
    "SALTS_UTILS_PACKAGE_DIR is not a directory: ${SALTS_UTILS_PACKAGE_DIR}")
endif()

find_package(SaltsUtils CONFIG REQUIRED
  PATHS "${SALTS_UTILS_PACKAGE_DIR}" NO_DEFAULT_PATH)
find_package(SaltsUtils CONFIG REQUIRED)

foreach(required_target IN ITEMS
    Salts::IDL
    Salts::Schema
    Salts::DataBindProducer
    Salts::DataBind
    Salts::JsonParser
    Salts::CsvParser
    Salts::XmlParser
    Salts::CYaml
    Salts::DateTimeParser
    Salts::QueryVM)
  if(NOT TARGET ${required_target})
    message(FATAL_ERROR
      "SaltsUtils package is missing component target ${required_target}")
  endif()
endforeach()

if(TARGET Salts::Databind)
  message(FATAL_ERROR "SaltsUtils 3.x must not export obsolete Salts::Databind spelling")
endif()
