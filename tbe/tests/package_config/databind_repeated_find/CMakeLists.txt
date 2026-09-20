cmake_minimum_required(VERSION 3.27)

project(databind_package_config_repeated_find LANGUAGES C CXX)

if(NOT IS_DIRECTORY "${DATABIND_PACKAGE_DIR}")
  message(FATAL_ERROR
    "DATABIND_PACKAGE_DIR is not a directory: ${DATABIND_PACKAGE_DIR}")
endif()

find_package(DataBind CONFIG REQUIRED
  PATHS "${DATABIND_PACKAGE_DIR}" NO_DEFAULT_PATH)
find_package(DataBind CONFIG REQUIRED)

foreach(required_target IN ITEMS
    Salts::TbeSchema
    Salts::DataBindCore
    Salts::DataBind
    Salts::DataBindCMeta
    Salts::DataBindCFlow)
  if(NOT TARGET ${required_target})
    message(FATAL_ERROR
      "DataBind package is missing owned target ${required_target}")
  endif()
endforeach()

foreach(forbidden_target IN ITEMS
    Salts::JsonParser
    Salts::CsvParser
    Salts::XmlParser
    Salts::CYaml
    Salts::DateTimeParser
    Salts::QueryVM)
  if(TARGET ${forbidden_target})
    message(FATAL_ERROR
      "Base DataBind package unexpectedly resolved SaltsUtils target ${forbidden_target}")
  endif()
endforeach()
