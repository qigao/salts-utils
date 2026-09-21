cmake_minimum_required(VERSION 3.27)

project(databind_package_config_repeated_find LANGUAGES C CXX)

if(NOT IS_DIRECTORY "${SALTS_UTILS_PACKAGE_DIR}")
  message(FATAL_ERROR
    "SALTS_UTILS_PACKAGE_DIR is not a directory: ${SALTS_UTILS_PACKAGE_DIR}")
endif()

find_package(SaltsUtils CONFIG REQUIRED
  PATHS "${SALTS_UTILS_PACKAGE_DIR}" NO_DEFAULT_PATH)
find_package(SaltsUtils CONFIG REQUIRED)

foreach(required_target IN ITEMS
    Salts::TbeSchema
    Salts::DataBindCore
    Salts::DataBind
    Salts::DataBindCMeta
    Salts::DataBindCFlow
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
